// SPDX-License-Identifier: Apache-2.0

#include "vpi_user.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/tf_call_bridge.h"
#include "fsim/runtime/tf_containment.hpp"
#include "acc_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

thread_local fsim_tf_call_context_v3* current_call_context{};
// The host services bound to the current call (pvc flags, delayed puts).
thread_local const fsim_tf_host_bridge_v3* current_host{};
thread_local s_tfexprinfo expression_info{};
thread_local s_tfnodeinfo node_info{};
thread_local s_strengthval strength_value{};
thread_local p_tfexprinfo last_expression_info{};
thread_local PLI_INT32 last_expression_parameter{};
thread_local std::string formatted_value;
thread_local std::string formatted_cstring;
thread_local std::string module_instance_name;
thread_local std::string scope_name;
// Text stored by tf_text, displayed by the next tf_message (IEEE 1364-2001
// 25.60).
thread_local std::string pending_text;
thread_local std::array<PLI_BYTE8, FSIM_TF_CONTROL_MAX_TEXT_SIZE + 1U>
    control_text{};

bool runtime_phase(const uint32_t phase) {
  return phase == FSIM_TF_CALL_PHASE_CALL ||
         phase == FSIM_TF_CALL_PHASE_SYNCHRONIZE ||
         phase == FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE ||
         phase == FSIM_TF_CALL_PHASE_REACTIVATE;
}

bool writable_phase(const uint32_t phase) {
  return phase == FSIM_TF_CALL_PHASE_CALL ||
         phase == FSIM_TF_CALL_PHASE_SYNCHRONIZE ||
         phase == FSIM_TF_CALL_PHASE_REACTIVATE;
}

const fsim_tf_argument_bridge_v3* current_argument(
    const PLI_INT32 parameter) {
  if (current_call_context == nullptr || parameter <= 0 ||
      static_cast<uint32_t>(parameter) >
          current_call_context->argument_count) {
    return nullptr;
  }
  return &current_call_context->arguments[parameter - 1];
}

fsim_tf_value_bridge_v3* current_value(const PLI_INT32 parameter) {
  if (current_call_context == nullptr ||
      !runtime_phase(current_call_context->phase) ||
      current_call_context->values == nullptr || parameter <= 0 ||
      static_cast<uint32_t>(parameter) >
          current_call_context->argument_count) {
    return nullptr;
  }
  return &current_call_context->values[parameter - 1];
}

bool current_instance_matches(const PLI_BYTE8* const instance) {
  return current_call_context != nullptr && instance != nullptr &&
         reinterpret_cast<const void*>(instance) ==
             static_cast<const void*>(current_call_context->instance);
}

fsim_tf_time_bridge_v3* current_time() {
  return current_call_context == nullptr ||
                 !runtime_phase(current_call_context->phase)
             ? nullptr
             : current_call_context->time;
}

bool bounded_text_size(const PLI_BYTE8* value, uint32_t maximum,
                       uint32_t& size);

PLI_BYTE8* context_name(const PLI_BYTE8* const source,
                        std::string& storage) {
  if (source == nullptr) {
    return nullptr;
  }
  try {
    uint32_t size{};
    if (!bounded_text_size(source, FSIM_TF_CONTROL_MAX_TEXT_SIZE, size)) {
      return nullptr;
    }
    storage.assign(source, size);
    return storage.data();
  } catch (...) {
    storage.clear();
    return nullptr;
  }
}

void fail_control() {
  if (current_call_context != nullptr) {
    current_call_context->control_failed = 1;
  }
}

bool bounded_text_size(const PLI_BYTE8* const value,
                       const uint32_t maximum, uint32_t& size) {
  size = 0;
  if (value == nullptr) {
    return true;
  }
  uint32_t checked{};
  while (checked <= maximum) {
    const auto remaining = maximum + 1U - checked;
    const auto step = std::min(UINT32_C(64), remaining);
    if (fsim::runtime::validate_tf_native_pointer(
            value + checked, step,
            fsim::runtime::TfNativePointerAccess::Read) !=
        fsim::runtime::TfContainmentError::None) {
      return false;
    }
    for (uint32_t index = 0; index < step; ++index) {
      if (value[checked + index] == '\0') {
        size = checked + index;
        return true;
      }
    }
    checked += step;
  }
  return false;
}

template <typename Value>
bool writable_pointer(Value* const pointer) {
  return pointer != nullptr &&
         fsim::runtime::validate_tf_native_pointer(
             pointer, sizeof(Value),
             fsim::runtime::TfNativePointerAccess::Write) ==
             fsim::runtime::TfContainmentError::None;
}

PLI_INT32 emit_control(const uint32_t kind, const PLI_INT32 channel,
                       const PLI_INT32 level,
                       const PLI_BYTE8* const facility,
                       const PLI_BYTE8* const message_number,
                       const PLI_BYTE8* const text,
                       const uint32_t text_size) {
  if (current_call_context == nullptr ||
      current_call_context->control_emit == nullptr) {
    return 1;
  }
  uint32_t facility_size{};
  uint32_t message_number_size{};
  if (!bounded_text_size(facility, FSIM_TF_CONTROL_MAX_METADATA_SIZE,
                         facility_size) ||
      !bounded_text_size(message_number, FSIM_TF_CONTROL_MAX_METADATA_SIZE,
                         message_number_size) ||
      current_call_context->control_emit(
          current_call_context->control_user_data, kind, channel, level,
          facility, facility_size, message_number, message_number_size, text,
          text_size) != 0) {
    fail_control();
    return 1;
  }
  return 0;
}

PLI_INT32 format_control(const uint32_t kind, const PLI_INT32 channel,
                         const PLI_INT32 level,
                         const PLI_BYTE8* const facility,
                         const PLI_BYTE8* const message_number,
                         const PLI_BYTE8* const format, va_list arguments) {
  if (format == nullptr || current_call_context == nullptr) {
    fail_control();
    return 1;
  }
  uint32_t format_size{};
  if (!bounded_text_size(
          format, FSIM_TF_CONTROL_MAX_TEXT_SIZE, format_size)) {
    fail_control();
    return 1;
  }
  (void)format_size;
  const auto formatted = std::vsnprintf(control_text.data(),
                                        control_text.size(), format, arguments);
  if (formatted < 0 ||
      static_cast<uint32_t>(formatted) > FSIM_TF_CONTROL_MAX_TEXT_SIZE) {
    fail_control();
    return 1;
  }
  return emit_control(kind, channel, level, facility, message_number,
                      control_text.data(), static_cast<uint32_t>(formatted));
}

// Facility and code strings are shorter than 10 characters (IEEE 1364-2001
// 25.31).
constexpr uint32_t kMaxMessageTag = 9U;

bool message_tag(const PLI_BYTE8* const tag) {
  uint32_t size{};
  return bounded_text_size(tag, kMaxMessageTag, size);
}

// A tf_message displays the text stored by tf_text first, then clears it.
PLI_INT32 format_message(const PLI_INT32 level,
                         const PLI_BYTE8* const facility,
                         const PLI_BYTE8* const message_number,
                         const PLI_BYTE8* const format, va_list arguments) {
  if (format == nullptr || current_call_context == nullptr ||
      !message_tag(facility) || !message_tag(message_number)) {
    fail_control();
    return 1;
  }
  uint32_t format_size{};
  if (!bounded_text_size(format, FSIM_TF_CONTROL_MAX_TEXT_SIZE,
                         format_size)) {
    fail_control();
    return 1;
  }
  const auto formatted = std::vsnprintf(control_text.data(),
                                        control_text.size(), format, arguments);
  if (formatted < 0 ||
      static_cast<uint32_t>(formatted) > FSIM_TF_CONTROL_MAX_TEXT_SIZE ||
      pending_text.size() + static_cast<uint32_t>(formatted) >
          FSIM_TF_CONTROL_MAX_TEXT_SIZE) {
    fail_control();
    return 1;
  }
  try {
    pending_text.append(control_text.data(), static_cast<uint32_t>(formatted));
    const auto result = emit_control(
        FSIM_TF_CONTROL_MESSAGE, 0, level, facility, message_number,
        pending_text.data(), static_cast<uint32_t>(pending_text.size()));
    pending_text.clear();
    return result;
  } catch (...) {
    pending_text.clear();
    fail_control();
    return 1;
  }
}

// IEEE 1364-2001 24.3.1 reads an X or Z bit as 0.
uint32_t known_bits(const s_vecval& word) {
  return static_cast<PLI_UINT32>(word.avalbits) &
         ~static_cast<PLI_UINT32>(word.bvalbits);
}

uint64_t join_long(const PLI_INT32 low, const PLI_INT32 high) {
  return (static_cast<uint64_t>(static_cast<PLI_UINT32>(high)) << 32u) |
         static_cast<PLI_UINT32>(low);
}

void store_long(const uint64_t value, PLI_INT32* const low,
                PLI_INT32* const high) {
  *low = static_cast<PLI_INT32>(static_cast<PLI_UINT32>(value));
  *high = static_cast<PLI_INT32>(static_cast<PLI_UINT32>(value >> 32u));
}

// The two-operand 64-bit routines update the first operand in place
// (IEEE 1364-2001 25.4, 25.10, 25.34, 25.57).
template <typename Operation>
void update_long(PLI_INT32* const low, PLI_INT32* const high,
                 const PLI_INT32 low2, const PLI_INT32 high2,
                 Operation operation) {
  if (!writable_pointer(low) || !writable_pointer(high)) {
    return;
  }
  store_long(operation(join_long(*low, *high), join_long(low2, high2)), low,
             high);
}

// Simulation time in the simulation's time precision, unscaled (IEEE
// 1364-2001 25.54).
uint64_t simulation_ticks() {
  const auto* const time = current_time();
  if (time != nullptr && current_host != nullptr &&
      current_host->has_simulation_precision != 0) {
    return current_host->simulation_ticks;
  }
  return time == nullptr ? 0 : time->scheduler_ticks;
}

// The simulation time unit: the smallest precision of the design, which is
// the module's own where the host reports no other (IEEE 1364-2001 25.26,
// 25.27).
PLI_INT32 simulation_precision(const fsim_tf_time_bridge_v3& time) {
  return current_host != nullptr && current_host->has_simulation_precision != 0
             ? current_host->simulation_precision_exponent
             : time.precision_exponent;
}

uint64_t time_scale(const fsim_tf_time_bridge_v3& time) {
  uint64_t scale{1};
  for (auto exponent = time.precision_exponent;
       exponent < time.unit_exponent; ++exponent) {
    scale *= 10u;
  }
  return scale;
}

bool add_ratio(uint64_t& quotient, uint64_t& remainder,
               const uint64_t addend_quotient,
               const uint64_t addend_remainder,
               const uint64_t denominator) {
  if (quotient >
      std::numeric_limits<uint64_t>::max() - addend_quotient) {
    return false;
  }
  quotient += addend_quotient;
  if (addend_remainder != 0 &&
      remainder >= denominator - addend_remainder) {
    remainder -= denominator - addend_remainder;
    if (quotient == std::numeric_limits<uint64_t>::max()) {
      return false;
    }
    ++quotient;
  } else {
    remainder += addend_remainder;
  }
  return true;
}

bool multiply_divide_rounded(const uint64_t multiplicand,
                             uint64_t multiplier,
                             const uint64_t denominator, uint64_t& result) {
  uint64_t quotient{};
  uint64_t remainder{};
  uint64_t term_quotient = multiplicand / denominator;
  uint64_t term_remainder = multiplicand % denominator;
  while (multiplier != 0) {
    if ((multiplier & UINT64_C(1)) != 0 &&
        !add_ratio(quotient, remainder, term_quotient, term_remainder,
                   denominator)) {
      return false;
    }
    multiplier >>= 1u;
    if (multiplier == 0) {
      break;
    }
    const bool carry = term_remainder >= denominator - term_remainder;
    term_remainder = carry
        ? term_remainder - (denominator - term_remainder)
        : term_remainder + term_remainder;
    const auto carry_value = static_cast<uint64_t>(carry);
    if (term_quotient >
        (std::numeric_limits<uint64_t>::max() - carry_value) / 2u) {
      return false;
    }
    term_quotient = term_quotient * 2u + carry_value;
  }
  const bool round_up =
      remainder >= denominator / 2u + denominator % 2u;
  if (round_up && quotient == std::numeric_limits<uint64_t>::max()) {
    return false;
  }
  result = quotient + static_cast<uint64_t>(round_up);
  return true;
}

bool local_time(const uint64_t ticks, uint64_t& result) {
  const auto* const time = current_time();
  if (time == nullptr) {
    return false;
  }
  return multiply_divide_rounded(ticks, time->tick_multiplier,
                                 time_scale(*time), result);
}

bool delay_ticks(const uint64_t local, uint64_t& result) {
  const auto* const time = current_time();
  if (time == nullptr) {
    return false;
  }
  const auto scale = time_scale(*time);
  return multiply_divide_rounded(local, scale, time->tick_multiplier, result);
}

PLI_INT32 append_delay(const uint64_t ticks) {
  auto* const time = current_time();
  if (current_call_context == nullptr ||
      !writable_phase(current_call_context->phase) || time == nullptr ||
      time->delay_count >= time->delay_capacity) {
    return 1;
  }
  time->delay_ticks[time->delay_count++] = ticks;
  return 0;
}

PLI_INT32 append_synchronization(const uint32_t kind) {
  // A checktf routine may also ask for synchronization before simulation;
  // any routine may switch asynchronous parameter-change calls.
  const bool asynchronous = kind == FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_ON ||
                            kind == FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_OFF;
  if (current_call_context == nullptr ||
      (!asynchronous && !writable_phase(current_call_context->phase) &&
       current_call_context->phase != FSIM_TF_CALL_PHASE_CHECK) ||
      (kind != FSIM_TF_SYNCHRONIZATION_READ_WRITE &&
       kind != FSIM_TF_SYNCHRONIZATION_READ_ONLY && !asynchronous) ||
      current_call_context->synchronization_count >=
          current_call_context->synchronization_capacity) {
    return 1;
  }
  current_call_context->synchronization_kinds
      [current_call_context->synchronization_count++] = kind;
  return 0;
}

// The argument numbers a pvc routine covers: one argument, or all of them
// for narg -1 (IEEE 1364-2001 25.9); false for a number outside the call.
bool pvc_range(const PLI_INT32 narg, uint32_t& first, uint32_t& last) {
  if (current_host == nullptr || current_host->pvc_count == 0) {
    return false;
  }
  if (narg == -1) {
    first = 1;
    last = current_host->pvc_count;
    return true;
  }
  if (narg < 1 || static_cast<uint32_t>(narg) > current_host->pvc_count) {
    return false;
  }
  first = last = static_cast<uint32_t>(narg);
  return true;
}

// tf_copypvc_flag and tf_movepvc_flag save the current flag; the value
// returned is the OR of the flags saved (25.9, 25.33).
PLI_INT32 save_pvc_flags(const PLI_INT32 narg, const bool clear) {
  uint32_t first{};
  uint32_t last{};
  if (!pvc_range(narg, first, last)) {
    return 0;
  }
  uint8_t any = 0;
  for (auto number = first; number <= last; ++number) {
    auto& current = current_host->pvc_current[number - 1];
    current_host->pvc_saved[number - 1] = current;
    any |= current;
    if (clear) {
      current = 0;
    }
  }
  return any;
}

bool writable_kind(const PLI_INT32 kind) {
  return kind == tf_readwrite || kind == tf_rwbitselect ||
         kind == tf_rwpartselect || kind == tf_rwmemselect ||
         kind == tf_readwritereal;
}

bool valid_value(const fsim_tf_argument_bridge_v3& argument,
                 const fsim_tf_value_bridge_v3& value,
                 const bool values_are_writable) {
  if (value.width != argument.width || value.assigned != 0 ||
      value.reserved != 0 ||
      value.writable != static_cast<uint32_t>(
                            values_are_writable &&
                            writable_kind(argument.kind))) {
    return false;
  }
  if (argument.kind == tf_nullparam) {
    return value.kind == FSIM_TF_VALUE_NONE && value.word_count == 0 &&
           value.vector_words == nullptr && value.string_size == 0 &&
           value.string_value == nullptr;
  }
  if (argument.kind == tf_string) {
    return value.kind == FSIM_TF_VALUE_STRING && value.word_count == 0 &&
           value.vector_words == nullptr && value.string_value != nullptr &&
           value.string_size <= argument.width / 8u &&
           fsim::runtime::validate_tf_native_pointer(
               value.string_value,
               static_cast<std::size_t>(value.string_size) + 1U,
               fsim::runtime::TfNativePointerAccess::Read) ==
               fsim::runtime::TfContainmentError::None &&
           value.string_value[value.string_size] == '\0';
  }
  if (argument.kind == tf_readonlyreal ||
      argument.kind == tf_readwritereal) {
    return value.kind == FSIM_TF_VALUE_REAL && value.width == 64u &&
           value.word_count == 0 && value.vector_words == nullptr &&
           value.string_size == 0 && value.string_value == nullptr;
  }
  return value.kind == FSIM_TF_VALUE_INTEGRAL && value.word_count != 0 &&
         value.word_count == (value.width + 31u) / 32u &&
         value.vector_words != nullptr &&
         fsim::runtime::validate_tf_native_pointer(
             value.vector_words,
             static_cast<std::size_t>(value.word_count) *
                 sizeof(*value.vector_words),
             value.writable != 0
                 ? fsim::runtime::TfNativePointerAccess::Write
                 : fsim::runtime::TfNativePointerAccess::Read) ==
             fsim::runtime::TfContainmentError::None &&
         value.string_size == 0 && value.string_value == nullptr;
}

void mask_unused_bits(fsim_tf_value_bridge_v3& value) {
  const auto remainder = value.width % 32u;
  if (value.kind != FSIM_TF_VALUE_INTEGRAL || remainder == 0u ||
      value.word_count == 0u) {
    return;
  }
  const auto mask = (UINT32_C(1) << remainder) - UINT32_C(1);
  auto& word = value.vector_words[value.word_count - 1u];
  word.avalbits = static_cast<PLI_INT32>(
      static_cast<PLI_UINT32>(word.avalbits) & mask);
  word.bvalbits = static_cast<PLI_INT32>(
      static_cast<PLI_UINT32>(word.bvalbits) & mask);
}

char logic_character(const s_vecval& word, const uint32_t bit) {
  const auto mask = UINT32_C(1) << bit;
  const bool aval = (static_cast<PLI_UINT32>(word.avalbits) & mask) != 0;
  const bool bval = (static_cast<PLI_UINT32>(word.bvalbits) & mask) != 0;
  if (!bval) {
    return aval ? '1' : '0';
  }
  return aval ? 'x' : 'z';
}

bool format_radix(const fsim_tf_value_bridge_v3& value,
                  const uint32_t bits_per_digit) {
  static constexpr char digits[] = "0123456789abcdef";
  const auto digit_count =
      (value.width + bits_per_digit - 1u) / bits_per_digit;
  formatted_value.assign(digit_count, '0');
  for (uint32_t output = 0; output < digit_count; ++output) {
    const auto digit = digit_count - 1u - output;
    const auto first_bit = digit * bits_per_digit;
    const auto bit_count =
        first_bit + bits_per_digit > value.width
            ? value.width - first_bit
            : bits_per_digit;
    uint32_t known_value{};
    bool unknown{};
    bool all_x{true};
    bool all_z{true};
    for (uint32_t offset = 0; offset < bit_count; ++offset) {
      const auto bit = first_bit + offset;
      const auto state = logic_character(
          value.vector_words[bit / 32u], bit % 32u);
      if (state == '0' || state == '1') {
        known_value |= static_cast<uint32_t>(state == '1') << offset;
        all_x = false;
        all_z = false;
      } else {
        unknown = true;
        all_x = all_x && state == 'x';
        all_z = all_z && state == 'z';
      }
    }
    formatted_value[output] =
        !unknown ? digits[known_value] : (all_z ? 'z' : (all_x ? 'x' : 'x'));
  }
  return true;
}

// The decimal text of a vector of any width (IEEE 1364-2001 25.53, 17.1.1.2):
// x or z when every bit is that value, X or Z when only some are (x ruling
// z), else the number, negative when the argument is signed and its sign bit
// is set.
bool format_decimal(const fsim_tf_argument_bridge_v3& argument,
                    const fsim_tf_value_bridge_v3& value) {
  uint32_t x_bits{};
  uint32_t z_bits{};
  std::vector<uint32_t> magnitude(value.word_count, 0u);
  for (uint32_t bit = 0; bit < value.width; ++bit) {
    const auto state = logic_character(
        value.vector_words[bit / 32u], bit % 32u);
    if (state == 'x') {
      ++x_bits;
    } else if (state == 'z') {
      ++z_bits;
    } else if (state == '1') {
      magnitude[bit / 32u] |= UINT32_C(1) << (bit % 32u);
    }
  }
  if (x_bits != 0 || z_bits != 0) {
    const bool x_rules = x_bits != 0;
    const auto count = x_rules ? x_bits : z_bits;
    const char lower = x_rules ? 'x' : 'z';
    formatted_value.assign(
        1, count == value.width ? lower
                                : static_cast<char>(std::toupper(
                                      static_cast<unsigned char>(lower))));
    return true;
  }
  const bool negative =
      argument.is_signed != 0 && value.width != 0 &&
      ((magnitude[(value.width - 1u) / 32u] >> ((value.width - 1u) % 32u)) &
       1u) != 0;
  if (negative) {
    // Two's complement negation over the whole width.
    uint32_t carry = 1u;
    for (auto& word : magnitude) {
      const uint64_t sum = static_cast<uint64_t>(~word) + carry;
      word = static_cast<uint32_t>(sum);
      carry = static_cast<uint32_t>(sum >> 32u);
    }
    const auto remainder = value.width % 32u;
    if (remainder != 0u) {
      magnitude.back() &= (UINT32_C(1) << remainder) - UINT32_C(1);
    }
  }
  // Repeated division by 10^9, least significant group first.
  std::string reversed;
  while (std::any_of(magnitude.begin(), magnitude.end(),
                     [](const uint32_t word) { return word != 0u; })) {
    uint64_t remainder{};
    for (auto word = magnitude.size(); word-- > 0;) {
      const uint64_t current = (remainder << 32u) | magnitude[word];
      magnitude[word] = static_cast<uint32_t>(current / 1000000000u);
      remainder = current % 1000000000u;
    }
    auto group = static_cast<uint32_t>(remainder);
    for (int digit = 0; digit < 9; ++digit) {
      reversed.push_back(static_cast<char>('0' + group % 10u));
      group /= 10u;
    }
  }
  while (reversed.size() > 1u && reversed.back() == '0') {
    reversed.pop_back();
  }
  if (reversed.empty()) {
    reversed = "0";
  }
  if (negative) {
    reversed.push_back('-');
  }
  formatted_value.assign(reversed.rbegin(), reversed.rend());
  return true;
}

bool format_integral(const fsim_tf_argument_bridge_v3& argument,
                     const fsim_tf_value_bridge_v3& value,
                     const PLI_INT32 format) {
  const auto character = static_cast<char>(
      std::tolower(static_cast<unsigned char>(format)));
  if (character == 'b') {
    formatted_value.assign(value.width, '0');
    for (uint32_t bit = 0; bit < value.width; ++bit) {
      formatted_value[value.width - 1u - bit] = logic_character(
          value.vector_words[bit / 32u], bit % 32u);
    }
    return true;
  }
  if (character == 'o') {
    return format_radix(value, 3);
  }
  if (character == 'h') {
    return format_radix(value, 4);
  }
  if (character != 'd') {
    return false;
  }
  return format_decimal(argument, value);
}

}  // namespace

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL fsim_tf_call_context_enter_v3(
    fsim_tf_call_context_v3* const context) {
  if (context == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          context, sizeof(*context),
          fsim::runtime::TfNativePointerAccess::Write) !=
          fsim::runtime::TfContainmentError::None ||
      current_call_context != nullptr ||
      context->phase < FSIM_TF_CALL_PHASE_CHECK ||
      context->phase > FSIM_TF_CALL_PHASE_REACTIVATE ||
      context->kind > FSIM_TF_CALL_RESULT_REAL || context->reserved != 0 ||
      (context->argument_count != 0 && context->arguments == nullptr) ||
      (runtime_phase(context->phase) &&
       context->argument_count != 0 && context->values == nullptr) ||
      (!runtime_phase(context->phase) &&
       context->values != nullptr) || context->instance == nullptr ||
      (runtime_phase(context->phase) && context->time == nullptr) ||
      (!runtime_phase(context->phase) && context->time != nullptr)) {
    return 1;
  }
  if (fsim::runtime::validate_tf_native_pointer(
          context->instance, sizeof(*context->instance),
          fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None ||
      context->instance->abi_version != FSIM_TF_INSTANCE_ABI_VERSION ||
      context->instance->struct_size < sizeof(fsim_tf_instance_bridge_v3) ||
      context->instance->design_id == 0 ||
      context->instance->hierarchy_id == 0 ||
      context->instance->generation == 0 ||
      context->instance->reserved != 0 ||
      (context->argument_count != 0 &&
       (context->argument_count > 4096U ||
        fsim::runtime::validate_tf_native_pointer(
            context->arguments,
            static_cast<std::size_t>(context->argument_count) *
                sizeof(*context->arguments),
            fsim::runtime::TfNativePointerAccess::Read) !=
            fsim::runtime::TfContainmentError::None ||
        (runtime_phase(context->phase) &&
         fsim::runtime::validate_tf_native_pointer(
             context->values,
             static_cast<std::size_t>(context->argument_count) *
                 sizeof(*context->values),
             fsim::runtime::TfNativePointerAccess::Write) !=
             fsim::runtime::TfContainmentError::None)))) {
    return 1;
  }
  if (context->context_reserved != 0 ||
      context->module_instance_name == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          context->module_instance_name, 1,
          fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None ||
      context->module_instance_name[0] == '\0' || context->scope_name == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          context->scope_name, 1,
          fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None ||
      context->scope_name[0] == '\0' || context->routine_name == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          context->routine_name, 1,
          fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None ||
      context->routine_name[0] == '\0' ||
      context->control_user_data == nullptr || context->control_emit == nullptr ||
      fsim::runtime::validate_tf_callback_pointer(context->control_emit) !=
          fsim::runtime::TfContainmentError::None ||
      context->control_failed != 0 || context->control_reserved != 0) {
    return 1;
  }
  if (context->width > UINT32_MAX - 31u) {
    return 1;
  }
  const uint32_t expected_words = (context->width + 31u) / 32u;
  if ((context->kind == FSIM_TF_CALL_RESULT_NONE &&
       (context->width != 0 || context->word_count != 0 ||
        context->aval_words != nullptr || context->bval_words != nullptr)) ||
      (context->kind == FSIM_TF_CALL_RESULT_INTEGRAL &&
       (context->phase != FSIM_TF_CALL_PHASE_CALL || context->width == 0 ||
        context->word_count != expected_words ||
        context->aval_words == nullptr || context->bval_words == nullptr)) ||
      (context->kind == FSIM_TF_CALL_RESULT_REAL &&
       (context->phase != FSIM_TF_CALL_PHASE_CALL || context->width != 64u ||
        context->word_count != 0 || context->aval_words != nullptr ||
        context->bval_words != nullptr))) {
    return 1;
  }
  if (context->kind == FSIM_TF_CALL_RESULT_INTEGRAL &&
      (fsim::runtime::validate_tf_native_pointer(
           context->aval_words,
           static_cast<std::size_t>(context->word_count) *
               sizeof(*context->aval_words),
           fsim::runtime::TfNativePointerAccess::Write) !=
           fsim::runtime::TfContainmentError::None ||
       fsim::runtime::validate_tf_native_pointer(
           context->bval_words,
           static_cast<std::size_t>(context->word_count) *
               sizeof(*context->bval_words),
           fsim::runtime::TfNativePointerAccess::Write) !=
           fsim::runtime::TfContainmentError::None)) {
    return 1;
  }
  if ((runtime_phase(context->phase) &&
       (context->synchronization_count != 0 ||
        context->synchronization_capacity == 0 ||
        context->synchronization_capacity >
            FSIM_TF_SYNCHRONIZATION_MAX_REQUESTS ||
        context->synchronization_reserved != 0 ||
        context->synchronization_kinds == nullptr)) ||
      (context->phase == FSIM_TF_CALL_PHASE_CHECK &&
       (context->synchronization_count != 0 ||
        context->synchronization_capacity >
            FSIM_TF_SYNCHRONIZATION_MAX_REQUESTS ||
        context->synchronization_reserved != 0 ||
        (context->synchronization_capacity == 0) !=
            (context->synchronization_kinds == nullptr) ||
        (context->synchronization_kinds != nullptr &&
         fsim::runtime::validate_tf_native_pointer(
             context->synchronization_kinds,
             static_cast<std::size_t>(context->synchronization_capacity) *
                 sizeof(*context->synchronization_kinds),
             fsim::runtime::TfNativePointerAccess::Write) !=
             fsim::runtime::TfContainmentError::None))) ||
      (!runtime_phase(context->phase) &&
       context->phase != FSIM_TF_CALL_PHASE_CHECK &&
       (context->synchronization_count != 0 ||
        context->synchronization_capacity != 0 ||
        context->synchronization_reserved != 0 ||
        context->synchronization_kinds != nullptr))) {
    return 1;
  }
  if (runtime_phase(context->phase)) {
    const auto* const time = context->time;
    if (fsim::runtime::validate_tf_native_pointer(
            time, sizeof(*time),
            fsim::runtime::TfNativePointerAccess::Write) !=
            fsim::runtime::TfContainmentError::None ||
        time->unit_exponent > 0 || time->unit_exponent < -15 ||
        time->precision_exponent > time->unit_exponent ||
        time->precision_exponent < -15 || time->tick_multiplier == 0 ||
        time->has_next_event > 1 || time->delay_count != 0 ||
        time->delay_capacity == 0 || time->delay_capacity > 256u ||
        time->reserved16 != 0 || time->reserved32 != 0 ||
        time->reserved != 0 || time->delay_ticks == nullptr ||
        fsim::runtime::validate_tf_native_pointer(
            time->delay_ticks,
            static_cast<std::size_t>(time->delay_capacity) *
                sizeof(*time->delay_ticks),
            fsim::runtime::TfNativePointerAccess::Write) !=
            fsim::runtime::TfContainmentError::None ||
        fsim::runtime::validate_tf_native_pointer(
            context->synchronization_kinds,
            static_cast<std::size_t>(context->synchronization_capacity) *
                sizeof(*context->synchronization_kinds),
            fsim::runtime::TfNativePointerAccess::Write) !=
            fsim::runtime::TfContainmentError::None) {
      return 1;
    }
    for (uint32_t index = 0; index < context->argument_count; ++index) {
      const auto& argument = context->arguments[index];
      if (argument.expression_size != 0 &&
          (argument.expression == nullptr ||
           fsim::runtime::validate_tf_native_pointer(
               argument.expression, argument.expression_size,
               fsim::runtime::TfNativePointerAccess::Read) !=
               fsim::runtime::TfContainmentError::None)) {
        return 1;
      }
      if (!valid_value(context->arguments[index], context->values[index],
                       writable_phase(context->phase))) {
        return 1;
      }
    }
  }
  context->assigned = 0;
  context->real = 0.0;
  last_expression_info = nullptr;
  last_expression_parameter = 0;
  current_call_context = context;
  return 0;
}

void FSIM_NATIVE_PLUGIN_CALL fsim_tf_call_context_leave_v3(
    fsim_tf_call_context_v3* const context) {
  if (current_call_context == context) {
    current_call_context = nullptr;
    current_host = nullptr;
    pending_text.clear();
    last_expression_info = nullptr;
    last_expression_parameter = 0;
  }
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL fsim_tf_call_host_bind_v3(
    fsim_tf_call_context_v3* const context,
    const fsim_tf_host_bridge_v3* const host) {
  if (context == nullptr || context != current_call_context ||
      host == nullptr || host->reserved != 0 ||
      (host->pvc_count != 0 &&
       (host->pvc_current == nullptr || host->pvc_saved == nullptr))) {
    return 1;
  }
  current_host = host;
  return 0;
}

const fsim_tf_call_context_v3* FSIM_NATIVE_PLUGIN_CALL
fsim_tf_current_call_context_v3(void) {
  return current_call_context;
}

#define FSIM_TF_INT(name, parameters) \
  PLI_INT32 name parameters { return 0; }
#define FSIM_TF_DOUBLE(name, parameters) \
  double name parameters { return 0.0; }
#define FSIM_TF_BYTES(name, parameters) \
  PLI_BYTE8* name parameters { return nullptr; }
#define FSIM_TF_EXPR(name, parameters) \
  p_tfexprinfo name parameters { return nullptr; }
#define FSIM_TF_NODE(name, parameters) \
  p_tfnodeinfo name parameters { return nullptr; }
#define FSIM_TF_VOID(name, parameters) void name parameters {}

void io_mcdprintf(const PLI_INT32 mcd, PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  if (current_call_context == nullptr) {
    (void)vpi_mcd_vprintf(static_cast<PLI_UINT32>(mcd), format, arguments);
  } else {
    (void)format_control(FSIM_TF_CONTROL_OUTPUT, mcd, 0, nullptr, nullptr,
                         format, arguments);
  }
  va_end(arguments);
}
void io_printf(PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  if (current_call_context == nullptr) {
    (void)vpi_vprintf(format, arguments);
  } else {
    (void)format_control(FSIM_TF_CONTROL_OUTPUT, 1, 0, nullptr, nullptr,
                         format, arguments);
  }
  va_end(arguments);
}
// The first invocation option "+<plusarg>..." that starts with the string
// returns its remaining text (IEEE 1364-2001 25.3).
PLI_BYTE8* mc_scan_plusargs(PLI_BYTE8* const plusarg) {
  uint32_t size{};
  if (plusarg == nullptr ||
      !bounded_text_size(plusarg, FSIM_TF_CONTROL_MAX_METADATA_SIZE, size)) {
    return nullptr;
  }
  s_vpi_vlog_info information{};
  if (vpi_get_vlog_info(&information) == 0 || information.argv == nullptr) {
    return nullptr;
  }
  for (PLI_INT32 index = 0; index < information.argc; ++index) {
    PLI_BYTE8* const option = information.argv[index];
    if (option != nullptr && option[0] == '+' &&
        std::strncmp(option + 1, plusarg, size) == 0) {
      return option + 1 + size;
    }
  }
  return nullptr;
}
PLI_INT32 tf_add_long(PLI_INT32* const low1, PLI_INT32* const high1,
                      const PLI_INT32 low2, const PLI_INT32 high2) {
  update_long(low1, high1, low2, high2,
              [](const uint64_t left, const uint64_t right) {
                return left + right;
              });
  return 0;
}
namespace {

// The delayed string puts (IEEE 1364-2001 25.52, 25.55, 25.56) hand the
// text to the host, which schedules it on the argument by delaytype
// (Table 196). Argument 0, a real or constant argument, a bad format or
// delay type, and a non-writable phase are errors.
PLI_INT32 put_string(const PLI_INT32 narg, const PLI_INT32 bitlength,
                     const PLI_INT32 format, const PLI_BYTE8* const value,
                     const bool delay_valid, const uint64_t ticks,
                     const PLI_INT32 delaytype) {
  const auto* const argument = current_argument(narg);
  uint32_t size{};
  if (current_host == nullptr || current_host->put == nullptr ||
      !writable_phase(current_call_context->phase) || argument == nullptr ||
      !writable_kind(argument->kind) || argument->kind == tf_readwritereal ||
      bitlength <= 0 || !delay_valid || delaytype < 0 || delaytype > 2 ||
      value == nullptr ||
      !bounded_text_size(value, FSIM_TF_CONTROL_MAX_TEXT_SIZE, size)) {
    return 0;
  }
  const auto radix = std::tolower(static_cast<unsigned char>(format));
  if (radix != 'b' && radix != 'o' && radix != 'd' && radix != 'h') {
    return 0;
  }
  return current_host->put(current_host->put_user_data, narg, radix, value,
                           ticks, delaytype);
}

// A real delay in the module's units as precision ticks, rounded to the
// nearest tick; false for a negative, infinite or too large delay.
bool real_delay_ticks(const double delay, uint64_t& ticks) {
  const auto* const time = current_time();
  if (time == nullptr || !std::isfinite(delay) || delay < 0.0) {
    return false;
  }
  const auto scaled = static_cast<long double>(delay) * time_scale(*time) /
                      time->tick_multiplier;
  const auto rounded = std::floor(scaled + 0.5L);
  if (!(rounded < std::ldexp(1.0L, 64))) {
    return false;
  }
  ticks = static_cast<uint64_t>(rounded);
  return true;
}

}  // namespace

// tf_asynchon returns 0 on success and 1 on error; tf_asynchoff always
// returns 0 (IEEE 1364-2001 25.5, 25.6).
PLI_INT32 tf_asynchoff(void) {
  (void)append_synchronization(FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_OFF);
  return 0;
}
PLI_INT32 tf_asynchon(void) {
  return append_synchronization(FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_ON);
}
// Always returns 1 (IEEE 1364-2001 25.7).
PLI_INT32 tf_clearalldelays(void) {
  auto* const time = current_time();
  if (current_call_context != nullptr &&
      writable_phase(current_call_context->phase) && time != nullptr) {
    time->delay_count = 0;
  }
  return 1;
}
// The operands are PLI_UINT32 halves of unsigned 64-bit values (IEEE
// 1364-2001 25.8).
PLI_INT32 tf_compare_long(const PLI_UINT32 low1, const PLI_UINT32 high1,
                          const PLI_UINT32 low2, const PLI_UINT32 high2) {
  const auto left = join_long(static_cast<PLI_INT32>(low1),
                              static_cast<PLI_INT32>(high1));
  const auto right = join_long(static_cast<PLI_INT32>(low2),
                               static_cast<PLI_INT32>(high2));
  return left < right ? -1 : (left > right ? 1 : 0);
}
// The pvc routines (IEEE 1364-2001 25.9, 25.22, 25.33, 25.59) work on the
// flags the host keeps for the call's instance; they return 0 where the host
// keeps none.
PLI_INT32 tf_copypvc_flag(const PLI_INT32 nparam) {
  return save_pvc_flags(nparam, false);
}
// Two's complement operands (IEEE 1364-2001 25.10); a zero divisor leaves
// the first operand unchanged because the standard defines no result.
void tf_divide_long(PLI_INT32* const low1, PLI_INT32* const high1,
                    const PLI_INT32 low2, const PLI_INT32 high2) {
  update_long(low1, high1, low2, high2,
              [](const uint64_t left, const uint64_t right) {
                const auto dividend = std::bit_cast<int64_t>(left);
                const auto divisor = std::bit_cast<int64_t>(right);
                if (divisor == 0) {
                  return left;
                }
                if (divisor == -1) {
                  return UINT64_C(0) - left;
                }
                return std::bit_cast<uint64_t>(dividend / divisor);
              });
}
PLI_INT32 tf_dofinish(void) {
  return emit_control(FSIM_TF_CONTROL_FINISH, 0, 0, nullptr, nullptr, nullptr,
                      0);
}
PLI_INT32 tf_dostop(void) {
  return emit_control(FSIM_TF_CONTROL_STOP, 0, 0, nullptr, nullptr, nullptr, 0);
}
PLI_INT32 tf_error(PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const auto result = format_control(FSIM_TF_CONTROL_ERROR, 0, ERR_ERROR,
                                     nullptr, nullptr, format, arguments);
  va_end(arguments);
  return result;
}
PLI_INT32 tf_nump(void) {
  return current_call_context == nullptr
             ? 0
             : static_cast<PLI_INT32>(current_call_context->argument_count);
}

PLI_INT32 tf_typep(const PLI_INT32 parameter) {
  const auto* const argument = current_argument(parameter);
  return argument == nullptr ? tf_nullparam : argument->kind;
}

// The bit length of a vector argument; the length of a literal string; 0 for
// a real or null argument or an error (IEEE 1364-2001 25.50).
PLI_INT32 tf_sizep(const PLI_INT32 parameter) {
  const auto* const argument = current_argument(parameter);
  if (argument == nullptr || argument->kind == tf_nullparam ||
      argument->kind == tf_readonlyreal || argument->kind == tf_readwritereal) {
    return 0;
  }
  if (argument->kind == tf_string) {
    const auto* const value = current_value(parameter);
    return static_cast<PLI_INT32>(value != nullptr && value->string_value != nullptr
                                      ? value->string_size
                                      : argument->width / 8u);
  }
  return static_cast<PLI_INT32>(argument->width);
}

p_tfexprinfo tf_exprinfo(const PLI_INT32 parameter, p_tfexprinfo info) {
  const auto* const argument = current_argument(parameter);
  if (argument == nullptr) {
    return nullptr;
  }
  if (info == nullptr) {
    info = &expression_info;
  } else if (!writable_pointer(info)) {
    return nullptr;
  }
  // A real argument has no vector groups or bits (IEEE 1364-2001 25.15).
  const bool real = argument->kind == tf_readonlyreal ||
                    argument->kind == tf_readwritereal;
  *info = {
      static_cast<PLI_INT16>(argument->kind),
      0,
      nullptr,
      0.0,
      argument->expression,
      real ? 0 : static_cast<PLI_INT32>((argument->width + 31u) / 32u),
      real ? 0 : static_cast<PLI_INT32>(argument->width),
      static_cast<PLI_INT32>(argument->is_signed),
      argument->lhs_select,
      argument->rhs_select,
  };
  if (auto* const value = current_value(parameter); value != nullptr) {
    if (value->kind == FSIM_TF_VALUE_INTEGRAL) {
      info->expr_value_p = value->vector_words;
    } else if (value->kind == FSIM_TF_VALUE_REAL) {
      info->real_value = value->real;
    } else if (value->kind == FSIM_TF_VALUE_STRING) {
      info->expr_string = value->string_value;
    }
  }
  last_expression_info = info;
  last_expression_parameter = parameter;
  return info;
}

PLI_INT32 tf_evaluatep(const PLI_INT32 parameter) {
  return tf_exprinfo(parameter, &expression_info) == nullptr ? 1 : 0;
}

// A string argument returns its text; a vector converts byte by byte without
// leading zeros; a null or real argument returns null (IEEE 1364-2001 25.16).
PLI_BYTE8* tf_getcstringp(const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
  if (value == nullptr) {
    return nullptr;
  }
  if (value->kind == FSIM_TF_VALUE_STRING) {
    return value->string_value;
  }
  if (value->kind != FSIM_TF_VALUE_INTEGRAL || value->word_count == 0) {
    return nullptr;
  }
  try {
    formatted_cstring.clear();
    const auto bytes = (value->width + 7u) / 8u;
    for (uint32_t index = bytes; index != 0; --index) {
      const auto byte = index - 1u;
      const auto word = known_bits(value->vector_words[byte / 4u]);
      const auto character = static_cast<char>((word >> (8u * (byte % 4u))) &
                                               UINT32_C(0xff));
      if (character != '\0' || !formatted_cstring.empty()) {
        formatted_cstring.push_back(character);
      }
    }
    return formatted_cstring.data();
  } catch (...) {
    formatted_cstring.clear();
    return nullptr;
  }
}
PLI_BYTE8* tf_getinstance(void) {
  return current_call_context == nullptr
             ? nullptr
             : reinterpret_cast<PLI_BYTE8*>(
                   const_cast<fsim_tf_instance_bridge_v3*>(
                       current_call_context->instance));
}
PLI_INT32 tf_getlongp(PLI_INT32* const high_value,
                      const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
  if (value == nullptr || value->kind != FSIM_TF_VALUE_INTEGRAL ||
      value->word_count == 0) {
    if (writable_pointer(high_value)) {
      *high_value = 0;
    }
    return 0;
  }
  if (writable_pointer(high_value)) {
    *high_value = value->word_count > 1
                      ? static_cast<PLI_INT32>(
                            known_bits(value->vector_words[1]))
                      : 0;
  }
  return static_cast<PLI_INT32>(known_bits(value->vector_words[0]));
}
PLI_INT32 tf_getlongtime(PLI_INT32* const high_time) {
  uint64_t local{};
  if (!local_time(current_time() == nullptr ? 0 : current_time()->scheduler_ticks,
                  local)) {
    if (writable_pointer(high_time)) {
      *high_time = 0;
    }
    return 0;
  }
  if (writable_pointer(high_time)) {
    *high_time = static_cast<PLI_INT32>(local >> 32u);
  }
  return static_cast<PLI_INT32>(local & UINT32_MAX);
}
// Table 189 of IEEE 1364-2001 25.20: 0 with the next event time in a read-only
// synchronize call, 1 with 0 when no event is left, 2 with the current time
// anywhere else; 2 takes precedence.
PLI_INT32 tf_getnextlongtime(PLI_INT32* const low_time,
                             PLI_INT32* const high_time) {
  const auto* const time = current_time();
  const bool read_only =
      time != nullptr &&
      current_call_context->phase == FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE;
  PLI_INT32 code = 2;
  uint64_t ticks{};
  if (read_only) {
    code = time->has_next_event == 0 ? 1 : 0;
    ticks = time->next_event_ticks;
  } else if (time != nullptr) {
    ticks = time->scheduler_ticks;
  }
  uint64_t local{};
  if (code == 1 || time == nullptr || !local_time(ticks, local)) {
    local = 0;
  }
  if (writable_pointer(low_time)) {
    *low_time = static_cast<PLI_INT32>(static_cast<PLI_UINT32>(local));
  }
  if (writable_pointer(high_time)) {
    *high_time = static_cast<PLI_INT32>(static_cast<PLI_UINT32>(local >> 32u));
  }
  return code;
}
PLI_INT32 tf_getp(const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
  if (value != nullptr && value->kind == FSIM_TF_VALUE_REAL) {
    // A real argument is rounded to the nearest integer.
    return std::isfinite(value->real) &&
                   std::fabs(value->real) < 2147483647.5
               ? static_cast<PLI_INT32>(std::lround(value->real))
               : 0;
  }
  if (value != nullptr && value->kind == FSIM_TF_VALUE_STRING) {
    // A literal string returns a C string pointer (IEEE 1364-2001 25.21),
    // which a 32-bit result keeps only on a 32-bit host.
    return static_cast<PLI_INT32>(
        reinterpret_cast<std::uintptr_t>(value->string_value));
  }
  return value == nullptr || value->kind != FSIM_TF_VALUE_INTEGRAL ||
                 value->word_count == 0
             ? 0
             : static_cast<PLI_INT32>(known_bits(value->vector_words[0]));
}
// The next argument after nparam whose saved flag is set, else 0 (25.22).
PLI_INT32 tf_getpchange(const PLI_INT32 nparam) {
  if (current_host == nullptr || nparam < 0) {
    return 0;
  }
  for (auto number = static_cast<uint32_t>(nparam) + 1U;
       number <= current_host->pvc_count; ++number) {
    if (current_host->pvc_saved[number - 1] != 0) {
      return static_cast<PLI_INT32>(number);
    }
  }
  return 0;
}
double tf_getrealp(const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
  if (value != nullptr && value->kind == FSIM_TF_VALUE_INTEGRAL &&
      value->word_count != 0) {
    // An integral argument converts to real.
    const auto* const argument = current_argument(parameter);
    double result{};
    double scale{1.0};
    for (uint32_t word = 0; word < value->word_count && word < 2u; ++word) {
      result += static_cast<double>(known_bits(value->vector_words[word])) *
                scale;
      scale *= 4294967296.0;
    }
    if (argument != nullptr && argument->is_signed != 0 &&
        argument->width != 0 && argument->width <= 64u &&
        result >= std::ldexp(1.0, static_cast<int>(argument->width) - 1)) {
      result -= std::ldexp(1.0, static_cast<int>(argument->width));
    }
    return result;
  }
  return value == nullptr || value->kind != FSIM_TF_VALUE_REAL
             ? 0.0
             : value->real;
}
double tf_getrealtime(void) {
  const auto* const time = current_time();
  return time == nullptr
             ? 0.0
             : static_cast<double>(time->scheduler_ticks) *
                   static_cast<double>(time->tick_multiplier) /
                   static_cast<double>(time_scale(*time));
}
PLI_INT32 tf_gettime(void) { return tf_getlongtime(nullptr); }
PLI_INT32 tf_gettimeprecision(void) {
  const auto* const time = current_time();
  return time == nullptr ? 0 : time->precision_exponent;
}
PLI_INT32 tf_gettimeunit(void) {
  const auto* const time = current_time();
  return time == nullptr ? 0 : time->unit_exponent;
}
PLI_BYTE8* tf_getworkarea(void) {
  return current_call_context == nullptr ? nullptr
                                         : current_call_context->work_area;
}

PLI_INT32 tf_iasynchoff(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_asynchoff() : 0;
}
PLI_INT32 tf_iasynchon(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_asynchon() : 1;
}
PLI_INT32 tf_iclearalldelays(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_clearalldelays() : 1;
}
PLI_INT32 tf_icopypvc_flag(const PLI_INT32 nparam, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_copypvc_flag(nparam) : 0;
}
PLI_INT32 tf_ievaluatep(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_evaluatep(parameter) : 1;
}
p_tfexprinfo tf_iexprinfo(const PLI_INT32 parameter, p_tfexprinfo info,
                          PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_exprinfo(parameter, info)
                                            : nullptr;
}
PLI_BYTE8* tf_igetcstringp(const PLI_INT32 parameter,
                           PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getcstringp(parameter)
                                            : nullptr;
}
PLI_INT32 tf_igetlongp(PLI_INT32* const high_value,
                       const PLI_INT32 parameter,
                       PLI_BYTE8* const instance) {
  if (!current_instance_matches(instance)) {
    if (writable_pointer(high_value)) {
      *high_value = 0;
    }
    return 0;
  }
  return tf_getlongp(high_value, parameter);
}
PLI_INT32 tf_igetlongtime(PLI_INT32* const high_time,
                          PLI_BYTE8* const instance) {
  if (!current_instance_matches(instance)) {
    if (writable_pointer(high_time)) {
      *high_time = 0;
    }
    return 0;
  }
  return tf_getlongtime(high_time);
}
PLI_INT32 tf_igetp(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getp(parameter) : 0;
}
PLI_INT32 tf_igetpchange(const PLI_INT32 nparam, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getpchange(nparam) : 0;
}
double tf_igetrealp(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getrealp(parameter) : 0.0;
}
double tf_igetrealtime(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getrealtime() : 0.0;
}
PLI_INT32 tf_igettime(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_gettime() : 0;
}
// A null instance asks for the simulation time unit, the smallest
// precision of the design.
PLI_INT32 tf_igettimeprecision(PLI_BYTE8* const instance) {
  if (instance == nullptr) {
    const auto* const time = current_time();
    return time == nullptr ? 0 : simulation_precision(*time);
  }
  return current_instance_matches(instance) ? tf_gettimeprecision() : 0;
}
PLI_INT32 tf_igettimeunit(PLI_BYTE8* const instance) {
  if (instance == nullptr) {
    const auto* const time = current_time();
    return time == nullptr ? 0 : simulation_precision(*time);
  }
  return current_instance_matches(instance) ? tf_gettimeunit() : 0;
}
PLI_BYTE8* tf_igetworkarea(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getworkarea() : nullptr;
}
PLI_BYTE8* tf_imipname(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_mipname() : nullptr;
}
PLI_INT32 tf_imovepvc_flag(const PLI_INT32 nparam, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_movepvc_flag(nparam) : 0;
}
p_tfnodeinfo tf_inodeinfo(const PLI_INT32 parameter, p_tfnodeinfo info,
                          PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_nodeinfo(parameter, info)
                                            : nullptr;
}
PLI_INT32 tf_inump(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_nump() : 0;
}
PLI_INT32 tf_ipropagatep(const PLI_INT32 parameter,
                         PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_propagatep(parameter) : 1;
}
PLI_INT32 tf_iputlongp(const PLI_INT32 parameter, const PLI_INT32 low,
                       const PLI_INT32 high, PLI_BYTE8* const instance) {
  return current_instance_matches(instance)
             ? tf_putlongp(parameter, low, high)
             : 1;
}
PLI_INT32 tf_iputp(const PLI_INT32 parameter, const PLI_INT32 value,
                   PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_putp(parameter, value) : 1;
}
PLI_INT32 tf_iputrealp(const PLI_INT32 parameter, const double value,
                       PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_putrealp(parameter, value)
                                            : 1;
}
PLI_INT32 tf_irosynchronize(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_rosynchronize() : 1;
}
PLI_INT32 tf_isetdelay(const PLI_INT32 delay, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_setdelay(delay) : 0;
}
PLI_INT32 tf_isetlongdelay(const PLI_INT32 low, const PLI_INT32 high,
                           PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_setlongdelay(low, high) : 0;
}
PLI_INT32 tf_isetrealdelay(const double delay, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_setrealdelay(delay) : 0;
}
PLI_INT32 tf_isetworkarea(PLI_BYTE8* const work_area,
                          PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_setworkarea(work_area) : 0;
}
PLI_INT32 tf_isizep(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_sizep(parameter) : 0;
}
PLI_BYTE8* tf_ispname(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_spname() : nullptr;
}
PLI_INT32 tf_istrdelputp(const PLI_INT32 nparam, const PLI_INT32 bitlength,
                         const PLI_INT32 format, PLI_BYTE8* const value_p,
                         const PLI_INT32 delay, const PLI_INT32 delaytype,
                         PLI_BYTE8* const instance) {
  return current_instance_matches(instance)
             ? tf_strdelputp(nparam, bitlength, format, value_p, delay,
                             delaytype)
             : 0;
}
PLI_BYTE8* tf_istrgetp(const PLI_INT32 parameter, const PLI_INT32 format,
                       PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_strgetp(parameter, format)
                                            : nullptr;
}
PLI_INT32 tf_istrlongdelputp(const PLI_INT32 nparam, const PLI_INT32 bitlength,
                             const PLI_INT32 format, PLI_BYTE8* const value_p,
                             const PLI_INT32 lowdelay,
                             const PLI_INT32 highdelay,
                             const PLI_INT32 delaytype,
                             PLI_BYTE8* const instance) {
  return current_instance_matches(instance)
             ? tf_strlongdelputp(nparam, bitlength, format, value_p, lowdelay,
                                 highdelay, delaytype)
             : 0;
}
PLI_INT32 tf_istrrealdelputp(const PLI_INT32 nparam, const PLI_INT32 bitlength,
                             const PLI_INT32 format, PLI_BYTE8* const value_p,
                             const double realdelay, const PLI_INT32 delaytype,
                             PLI_BYTE8* const instance) {
  return current_instance_matches(instance)
             ? tf_strrealdelputp(nparam, bitlength, format, value_p, realdelay,
                                 delaytype)
             : 0;
}
PLI_INT32 tf_isynchronize(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_synchronize() : 1;
}
PLI_INT32 tf_itestpvc_flag(const PLI_INT32 nparam, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_testpvc_flag(nparam) : 0;
}
PLI_INT32 tf_itypep(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_typep(parameter)
                                            : tf_nullparam;
}

// A signed two's complement conversion (IEEE 1364-2001 25.29).
void tf_long_to_real(const PLI_INT32 low, const PLI_INT32 high,
                     double* const result) {
  if (writable_pointer(result)) {
    *result = static_cast<double>(std::bit_cast<int64_t>(join_long(low, high)));
  }
}
PLI_BYTE8* tf_longtime_tostr(const PLI_INT32 low, const PLI_INT32 high) {
  const auto value = join_long(low, high);
  try {
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec != std::errc{}) return nullptr;
    formatted_value.assign(buffer.data(), converted.ptr);
    return formatted_value.data();
  } catch (...) {
    return nullptr;
  }
}
PLI_INT32 tf_message(const PLI_INT32 level, PLI_BYTE8* const facility,
                     PLI_BYTE8* const message_number,
                     PLI_BYTE8* const message, ...) {
  va_list arguments;
  va_start(arguments, message);
  const auto result =
      format_message(level, facility, message_number, message, arguments);
  va_end(arguments);
  return result;
}
PLI_BYTE8* tf_mipname(void) {
  return current_call_context == nullptr
             ? nullptr
             : context_name(current_call_context->module_instance_name,
                            module_instance_name);
}
PLI_INT32 tf_movepvc_flag(const PLI_INT32 nparam) {
  return save_pvc_flags(nparam, true);
}
void tf_multiply_long(PLI_INT32* const low1, PLI_INT32* const high1,
                      const PLI_INT32 low2, const PLI_INT32 high2) {
  update_long(low1, high1, low2, high2,
              [](const uint64_t left, const uint64_t right) {
                return left * right;
              });
}
namespace {

// The 0 and 1 strength bit patterns of a scalar net (IEEE 1364-2001 25.35,
// 7.10). The host reports the strengths of the net's drivers when it can; a
// host that cannot gets the strong strength for a known bit, strong on both
// scales for an x, and high impedance on both for a z.
s_strengthval* scalar_strength(const PLI_INT32 parameter,
                               const fsim_tf_value_bridge_v3& value) {
  constexpr PLI_INT32 strong = 0x40;
  constexpr PLI_INT32 high_impedance = 0x01;
  if (current_host != nullptr && current_host->strength != nullptr) {
    PLI_INT32 zero{};
    PLI_INT32 one{};
    if (current_host->strength(current_host->put_user_data, parameter, &zero,
                               &one) == 0) {
      strength_value = s_strengthval{zero, one};
      return &strength_value;
    }
  }
  const auto state = value.kind == FSIM_TF_VALUE_INTEGRAL &&
                             value.word_count != 0
                         ? logic_character(value.vector_words[0], 0)
                         : 'x';
  strength_value = state == '0'   ? s_strengthval{strong, 0}
                   : state == '1' ? s_strengthval{0, strong}
                   : state == 'z' ? s_strengthval{high_impedance,
                                                  high_impedance}
                                  : s_strengthval{strong, strong};
  return &strength_value;
}

}  // namespace

p_tfnodeinfo tf_nodeinfo(const PLI_INT32 parameter, p_tfnodeinfo info) {
  const auto* const argument = current_argument(parameter);
  if (argument == nullptr || argument->kind == tf_string) {
    return nullptr;
  }
  if (info == nullptr) {
    info = &node_info;
  } else if (!writable_pointer(info)) {
    return nullptr;
  }
  *info = {};
  if (argument->kind == tf_nullparam) {
    info->node_type = tf_null_node;
    return info;
  }
  const bool real = argument->kind == tf_readonlyreal ||
                    argument->kind == tf_readwritereal;
  // The host's facts about the node: its class, declared bounds and, for a
  // memory, its size (IEEE 1364-2001 25.35, Table 192).
  const fsim_tf_host_node_v3* const given =
      current_host != nullptr && current_host->nodes != nullptr &&
              static_cast<uint32_t>(parameter) <= current_host->node_count &&
              current_host->nodes[parameter - 1].node_type != 0
          ? &current_host->nodes[parameter - 1]
          : nullptr;
  info->node_symbol = argument->expression;
  // A real node has no vector groups or bits (IEEE 1364-2001 25.35).
  info->node_ngroups =
      real ? 0 : static_cast<PLI_INT32>((argument->width + 31u) / 32u);
  info->node_vec_size = real ? 0 : static_cast<PLI_INT32>(argument->width);
  info->node_sign = static_cast<PLI_INT32>(argument->is_signed);
  // The msb and lsb of the vector, not of a select of it.
  info->node_ms_index =
      given != nullptr ? given->ms_index
      : argument->lhs_select >= 0
          ? argument->lhs_select
          : static_cast<PLI_INT32>(argument->width - 1u);
  info->node_ls_index = given != nullptr ? given->ls_index
                        : argument->rhs_select >= 0 ? argument->rhs_select
                                                    : 0;
  info->node_lhs_element = argument->lhs_select;
  info->node_rhs_element = argument->rhs_select;
  if (real) {
    info->node_type = tf_real_node;
  } else if (given != nullptr) {
    info->node_type = static_cast<PLI_INT16>(given->node_type);
  } else if (writable_kind(argument->kind)) {
    info->node_type = argument->width == 32u && argument->is_signed != 0
                          ? tf_integer_node
                          : tf_reg_node;
  } else {
    info->node_type = argument->width == 1u ? tf_netscalar_node
                                            : tf_netvector_node;
  }
  if (info->node_type == tf_memory_node) {
    // A memory has no vector groups: each element is its aval bytes then its
    // bval bytes, and node_ngroups counts the bytes of each (Figure 169).
    info->node_mem_size = given != nullptr ? given->mem_size : 0;
    uint32_t group_bytes{};
    uint32_t count{};
    const PLI_BYTE8* data{};
    if (current_host != nullptr && current_host->memory != nullptr &&
        current_host->memory(current_host->put_user_data, parameter,
                             &group_bytes, &count, &data) == 0) {
      info->node_ngroups = static_cast<PLI_INT32>(group_bytes);
      info->node_mem_size = static_cast<PLI_INT32>(count);
      info->node_value.memoryval_p = const_cast<PLI_BYTE8*>(data);
    }
    return info;
  }
  if (auto* const value = current_value(parameter); value != nullptr) {
    if (info->node_type == tf_netscalar_node) {
      info->node_value.strengthval_p = scalar_strength(parameter, *value);
    } else if (value->kind == FSIM_TF_VALUE_INTEGRAL) {
      info->node_value.vecval_p = value->vector_words;
    } else if (value->kind == FSIM_TF_VALUE_REAL) {
      info->node_value.real_val_p = &value->real;
    }
  }
  return info;
}
PLI_INT32 tf_propagatep(const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
  if (value == nullptr || value->writable == 0) {
    return 1;
  }
  if (value->kind == FSIM_TF_VALUE_REAL) {
    if (last_expression_parameter != parameter ||
        last_expression_info == nullptr) {
      return 1;
    }
    value->real = last_expression_info->real_value;
  } else if (value->kind != FSIM_TF_VALUE_INTEGRAL) {
    return 1;
  }
  mask_unused_bits(*value);
  value->assigned = 1;
  return 0;
}
namespace {

// An integer written to a real variable converts to real (IEEE 1364-2001
// 24.3.7); tf_putp passes a 32-bit signed value, tf_putlongp a 64-bit one.
double integer_to_real(const PLI_UINT32 low, const PLI_UINT32 high,
                       const bool wide) {
  return wide ? static_cast<double>(std::bit_cast<int64_t>(
                    join_long(static_cast<PLI_INT32>(low),
                              static_cast<PLI_INT32>(high))))
              : static_cast<double>(std::bit_cast<int32_t>(low));
}

PLI_INT32 assign_integral_result(const PLI_INT32 parameter,
                                 const PLI_UINT32 low, const PLI_UINT32 high,
                                 const bool wide) {
  if (parameter == 0 && current_call_context != nullptr &&
      current_call_context->phase == FSIM_TF_CALL_PHASE_CALL &&
      current_call_context->kind == FSIM_TF_CALL_RESULT_REAL) {
    current_call_context->real = integer_to_real(low, high, wide);
    current_call_context->assigned = 1;
    return 0;
  }
  if (parameter != 0 || current_call_context == nullptr ||
      current_call_context->phase != FSIM_TF_CALL_PHASE_CALL ||
      current_call_context->kind != FSIM_TF_CALL_RESULT_INTEGRAL ||
      current_call_context->word_count == 0 ||
      current_call_context->aval_words == nullptr ||
      current_call_context->bval_words == nullptr) {
    return 1;
  }
  for (uint32_t index = 0; index < current_call_context->word_count; ++index) {
    current_call_context->aval_words[index] = 0;
    current_call_context->bval_words[index] = 0;
  }
  current_call_context->aval_words[0] = low;
  if (current_call_context->word_count > 1) {
    current_call_context->aval_words[1] = high;
  }
  current_call_context->assigned = 1;
  return 0;
}

PLI_INT32 assign_integral_argument(const PLI_INT32 parameter,
                                   const PLI_UINT32 low,
                                   const PLI_UINT32 high, const bool wide) {
  auto* const value = current_value(parameter);
  if (value != nullptr && value->kind == FSIM_TF_VALUE_REAL &&
      value->writable != 0) {
    value->real = integer_to_real(low, high, wide);
    value->assigned = 1;
    return 0;
  }
  if (value == nullptr || value->kind != FSIM_TF_VALUE_INTEGRAL ||
      value->writable == 0 || value->word_count == 0 ||
      value->vector_words == nullptr) {
    return 1;
  }
  for (uint32_t index = 0; index < value->word_count; ++index) {
    value->vector_words[index] = {};
  }
  value->vector_words[0].avalbits = static_cast<PLI_INT32>(low);
  if (value->word_count > 1) {
    value->vector_words[1].avalbits = static_cast<PLI_INT32>(high);
  }
  mask_unused_bits(*value);
  value->assigned = 1;
  return 0;
}

}  // namespace

PLI_INT32 tf_putlongp(const PLI_INT32 parameter, const PLI_INT32 low,
                      const PLI_INT32 high) {
  return parameter == 0
             ? assign_integral_result(parameter,
                                      static_cast<PLI_UINT32>(low),
                                      static_cast<PLI_UINT32>(high), true)
             : assign_integral_argument(parameter,
                                        static_cast<PLI_UINT32>(low),
                                        static_cast<PLI_UINT32>(high), true);
}

PLI_INT32 tf_putp(const PLI_INT32 parameter, const PLI_INT32 value) {
  return parameter == 0
             ? assign_integral_result(parameter,
                                      static_cast<PLI_UINT32>(value), 0, false)
             : assign_integral_argument(parameter,
                                        static_cast<PLI_UINT32>(value), 0,
                                        false);
}

PLI_INT32 tf_putrealp(const PLI_INT32 parameter, const double value) {
  if (parameter == 0) {
    if (current_call_context == nullptr ||
        current_call_context->phase != FSIM_TF_CALL_PHASE_CALL ||
        current_call_context->kind != FSIM_TF_CALL_RESULT_REAL) {
      return 1;
    }
    current_call_context->real = value;
    current_call_context->assigned = 1;
    return 0;
  }
  auto* const argument_value = current_value(parameter);
  if (argument_value == nullptr ||
      argument_value->kind != FSIM_TF_VALUE_REAL ||
      argument_value->writable == 0) {
    return 1;
  }
  argument_value->real = value;
  argument_value->assigned = 1;
  return 0;
}
// Reads back a block that tf_write_save saved (IEEE 1364-2001 25.41): legal
// only from misctf with reason_restart; exactly as many bytes as were
// written must be read in all. Nonzero when the block was filled.
PLI_INT32 tf_read_restart(PLI_BYTE8* const blockptr, const PLI_INT32 blocklen) {
  if (blockptr == nullptr || blocklen <= 0 ||
      fsim::runtime::validate_tf_native_pointer(
          blockptr, static_cast<std::size_t>(blocklen),
          fsim::runtime::TfNativePointerAccess::Write) !=
          fsim::runtime::TfContainmentError::None) {
    return 0;
  }
  return emit_control(FSIM_TF_CONTROL_RESTART_READ, 0, 0, nullptr, nullptr,
                      blockptr, static_cast<uint32_t>(blocklen)) == 0
             ? 1
             : 0;
}
// Rounds to the nearest integer, halves away from zero, saturating (IEEE
// 1364-2001 25.42 and the Verilog rounding rule of 25.21).
void tf_real_to_long(const double value, PLI_INT32* const low,
                     PLI_INT32* const high) {
  if (!writable_pointer(low) || !writable_pointer(high)) {
    return;
  }
  int64_t converted{};
  if (std::isnan(value)) {
    converted = 0;
  } else if (value >= 9223372036854775808.0) {
    converted = std::numeric_limits<int64_t>::max();
  } else if (value <= -9223372036854775808.0) {
    converted = std::numeric_limits<int64_t>::min();
  } else {
    converted = static_cast<int64_t>(std::llround(value));
  }
  store_long(std::bit_cast<uint64_t>(converted), low, high);
}
PLI_INT32 tf_rosynchronize(void) {
  return append_synchronization(FSIM_TF_SYNCHRONIZATION_READ_ONLY);
}
void tf_scale_longdelay(PLI_BYTE8* const instance, const PLI_INT32 low,
                        const PLI_INT32 high, PLI_INT32* const scaled_low,
                        PLI_INT32* const scaled_high) {
  uint64_t ticks{};
  const auto local = (static_cast<uint64_t>(static_cast<PLI_UINT32>(high)) << 32u) |
                     static_cast<PLI_UINT32>(low);
  if (!current_instance_matches(instance) || !delay_ticks(local, ticks)) {
    ticks = 0;
  }
  if (writable_pointer(scaled_low)) {
    *scaled_low = static_cast<PLI_INT32>(ticks);
  }
  if (writable_pointer(scaled_high)) {
    *scaled_high = static_cast<PLI_INT32>(ticks >> 32u);
  }
}
void tf_scale_realdelay(PLI_BYTE8* const instance, const double delay,
                        double* const scaled_delay) {
  auto* const time = current_time();
  if (!writable_pointer(scaled_delay)) return;
  *scaled_delay = !current_instance_matches(instance) || time == nullptr ||
                          !std::isfinite(delay) || delay < 0.0
                      ? 0.0
                      : delay * static_cast<double>(time_scale(*time)) /
                            static_cast<double>(time->tick_multiplier);
}
// The delay routines return 1 when the reactivation is scheduled and 0 on
// error (IEEE 1364-2001 25.46-25.48).
PLI_INT32 tf_setdelay(const PLI_INT32 delay) {
  uint64_t ticks{};
  return delay < 0 || !delay_ticks(static_cast<uint64_t>(delay), ticks)
             ? 0
             : static_cast<PLI_INT32>(append_delay(ticks) == 0);
}
PLI_INT32 tf_setlongdelay(const PLI_INT32 low, const PLI_INT32 high) {
  uint64_t ticks{};
  return high < 0 || !delay_ticks(join_long(low, high), ticks)
             ? 0
             : static_cast<PLI_INT32>(append_delay(ticks) == 0);
}
PLI_INT32 tf_setrealdelay(const double delay) {
  uint64_t ticks{};
  return !real_delay_ticks(delay, ticks)
             ? 0
             : static_cast<PLI_INT32>(append_delay(ticks) == 0);
}
// Always returns 0 (IEEE 1364-2001 25.49); outside a call there is no work
// area to set.
PLI_INT32 tf_setworkarea(PLI_BYTE8* const work_area) {
  if (current_call_context != nullptr) {
    current_call_context->work_area = work_area;
  }
  return 0;
}
PLI_BYTE8* tf_spname(void) {
  return current_call_context == nullptr ? nullptr
                                         : context_name(
                                               current_call_context->scope_name,
                                               scope_name);
}
// The delay is a non-negative count of the module's time units (25.52).
PLI_INT32 tf_strdelputp(const PLI_INT32 nparam, const PLI_INT32 bitlength,
                        const PLI_INT32 format, PLI_BYTE8* const value_p,
                        const PLI_INT32 delay, const PLI_INT32 delaytype) {
  uint64_t ticks{};
  const bool valid =
      delay >= 0 && delay_ticks(static_cast<uint64_t>(delay), ticks);
  return put_string(nparam, bitlength, format, value_p, valid, ticks,
                    delaytype);
}
PLI_BYTE8* tf_strgetp(const PLI_INT32 parameter, const PLI_INT32 format) {
  const auto* const argument = current_argument(parameter);
  auto* const value = current_value(parameter);
  if (argument == nullptr || value == nullptr) {
    return nullptr;
  }
  // A string argument returns its text and ignores the format (IEEE
  // 1364-2001 25.53).
  if (value->kind == FSIM_TF_VALUE_STRING) {
    return value->string_value;
  }
  if (value->kind != FSIM_TF_VALUE_INTEGRAL) {
    return nullptr;
  }
  try {
    if (!format_integral(*argument, *value, format)) {
      return nullptr;
    }
    return formatted_value.data();
  } catch (...) {
    formatted_value.clear();
    return nullptr;
  }
}
PLI_BYTE8* tf_strgettime(void) {
  if (current_time() == nullptr) {
    return nullptr;
  }
  const auto ticks = simulation_ticks();
  return tf_longtime_tostr(static_cast<PLI_INT32>(static_cast<PLI_UINT32>(ticks)),
                           static_cast<PLI_INT32>(static_cast<PLI_UINT32>(
                               ticks >> 32u)));
}
PLI_INT32 tf_strlongdelputp(const PLI_INT32 nparam, const PLI_INT32 bitlength,
                            const PLI_INT32 format, PLI_BYTE8* const value_p,
                            const PLI_INT32 lowdelay, const PLI_INT32 highdelay,
                            const PLI_INT32 delaytype) {
  uint64_t ticks{};
  const bool valid = highdelay >= 0 &&
                     delay_ticks(join_long(lowdelay, highdelay), ticks);
  return put_string(nparam, bitlength, format, value_p, valid, ticks,
                    delaytype);
}
PLI_INT32 tf_strrealdelputp(const PLI_INT32 nparam, const PLI_INT32 bitlength,
                            const PLI_INT32 format, PLI_BYTE8* const value_p,
                            const double realdelay, const PLI_INT32 delaytype) {
  uint64_t ticks{};
  const bool valid = real_delay_ticks(realdelay, ticks);
  return put_string(nparam, bitlength, format, value_p, valid, ticks,
                    delaytype);
}
PLI_INT32 tf_subtract_long(PLI_INT32* const low1, PLI_INT32* const high1,
                           const PLI_INT32 low2, const PLI_INT32 high2) {
  update_long(low1, high1, low2, high2,
              [](const uint64_t left, const uint64_t right) {
                return left - right;
              });
  return 0;
}
PLI_INT32 tf_synchronize(void) {
  return append_synchronization(FSIM_TF_SYNCHRONIZATION_READ_WRITE);
}
// The OR of the saved flags (25.59).
PLI_INT32 tf_testpvc_flag(const PLI_INT32 nparam) {
  uint32_t first{};
  uint32_t last{};
  if (!pvc_range(nparam, first, last)) {
    return 0;
  }
  uint8_t any = 0;
  for (auto number = first; number <= last; ++number) {
    any |= current_host->pvc_saved[number - 1];
  }
  return any;
}
// Stores the text for the next tf_message (IEEE 1364-2001 25.60).
PLI_INT32 tf_text(PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  PLI_INT32 result = 1;
  uint32_t format_size{};
  if (format != nullptr && current_call_context != nullptr &&
      bounded_text_size(format, FSIM_TF_CONTROL_MAX_TEXT_SIZE, format_size)) {
    const auto formatted = std::vsnprintf(control_text.data(),
                                          control_text.size(), format,
                                          arguments);
    if (formatted >= 0 &&
        static_cast<uint32_t>(formatted) <= FSIM_TF_CONTROL_MAX_TEXT_SIZE &&
        pending_text.size() + static_cast<uint32_t>(formatted) <=
            FSIM_TF_CONTROL_MAX_TEXT_SIZE) {
      try {
        pending_text.append(control_text.data(),
                            static_cast<uint32_t>(formatted));
        result = 0;
      } catch (...) {
        pending_text.clear();
      }
    }
  }
  va_end(arguments);
  if (result != 0) {
    fail_control();
  }
  return result;
}
void tf_unscale_longdelay(PLI_BYTE8* const instance, const PLI_INT32 low,
                          const PLI_INT32 high, PLI_INT32* const result_low,
                          PLI_INT32* const result_high) {
  const auto ticks = (static_cast<uint64_t>(static_cast<PLI_UINT32>(high)) << 32u) |
                     static_cast<PLI_UINT32>(low);
  uint64_t local{};
  if (!current_instance_matches(instance) || !local_time(ticks, local)) local = 0;
  if (writable_pointer(result_low)) {
    *result_low = static_cast<PLI_INT32>(local);
  }
  if (writable_pointer(result_high)) {
    *result_high = static_cast<PLI_INT32>(local >> 32u);
  }
}
void tf_unscale_realdelay(PLI_BYTE8* const instance, const double delay,
                          double* const result) {
  auto* const time = current_time();
  if (!writable_pointer(result)) return;
  *result = !current_instance_matches(instance) || time == nullptr ||
                    !std::isfinite(delay) || delay < 0.0
                ? 0.0
                : delay * static_cast<double>(time->tick_multiplier) /
                      static_cast<double>(time_scale(*time));
}
PLI_INT32 tf_warning(PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const auto result = format_control(FSIM_TF_CONTROL_WARNING, 0, ERR_WARNING,
                                     nullptr, nullptr, format, arguments);
  va_end(arguments);
  return result;
}
// Appends a block to the save file (IEEE 1364-2001 25.65): legal only from
// misctf with reason_save. Nonzero when the block was written.
PLI_INT32 tf_write_save(PLI_BYTE8* const blockptr, const PLI_INT32 blocklen) {
  if (blockptr == nullptr || blocklen <= 0 ||
      fsim::runtime::validate_tf_native_pointer(
          blockptr, static_cast<std::size_t>(blocklen),
          fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None) {
    return 0;
  }
  return emit_control(FSIM_TF_CONTROL_SAVE_WRITE, 0, 0, nullptr, nullptr,
                      blockptr, static_cast<uint32_t>(blocklen)) == 0
             ? 1
             : 0;
}

#undef FSIM_TF_INT
#undef FSIM_TF_DOUBLE
#undef FSIM_TF_BYTES
#undef FSIM_TF_EXPR
#undef FSIM_TF_NODE
#undef FSIM_TF_VOID

namespace fsim::runtime::acc_detail {

std::shared_ptr<void> suspend_tf_call() {
  struct Suspended {
    fsim_tf_call_context_v3* context;
    s_tfexprinfo expression;
    s_tfnodeinfo node;
    s_strengthval strength;
    p_tfexprinfo last_expression;
    PLI_INT32 last_parameter;
    std::string text;
  };
  auto* const saved = new Suspended{current_call_context, expression_info,
                                    node_info,           strength_value,
                                    last_expression_info,
                                    last_expression_parameter, pending_text};
  current_call_context = nullptr;
  last_expression_info = nullptr;
  last_expression_parameter = 0;
  pending_text.clear();
  return std::shared_ptr<void>(saved, [](void* const pointer) {
    auto* const suspended = static_cast<Suspended*>(pointer);
    current_call_context = suspended->context;
    expression_info = suspended->expression;
    node_info = suspended->node;
    strength_value = suspended->strength;
    last_expression_info = suspended->last_expression;
    last_expression_parameter = suspended->last_parameter;
    pending_text = std::move(suspended->text);
    delete suspended;
  });
}

}  // namespace fsim::runtime::acc_detail

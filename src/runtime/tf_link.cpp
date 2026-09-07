// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/tf_call_bridge.h"
#include "fsim/runtime/tf_containment.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {

thread_local fsim_tf_call_context_v3* current_call_context{};
thread_local s_tfexprinfo expression_info{};
thread_local s_tfnodeinfo node_info{};
thread_local p_tfexprinfo last_expression_info{};
thread_local PLI_INT32 last_expression_parameter{};
thread_local std::string formatted_value;
thread_local std::string module_instance_name;
thread_local std::string scope_name;
thread_local std::string routine_name;
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
  if (current_call_context == nullptr ||
      !writable_phase(current_call_context->phase) ||
      (kind != FSIM_TF_SYNCHRONIZATION_READ_WRITE &&
       kind != FSIM_TF_SYNCHRONIZATION_READ_ONLY) ||
      current_call_context->synchronization_count >=
          current_call_context->synchronization_capacity) {
    return 1;
  }
  current_call_context->synchronization_kinds
      [current_call_context->synchronization_count++] = kind;
  return 0;
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
  if (character != 'd' || value.width > 64u) {
    return false;
  }
  uint64_t number{};
  bool unknown{};
  for (uint32_t bit = 0; bit < value.width; ++bit) {
    const auto state = logic_character(
        value.vector_words[bit / 32u], bit % 32u);
    if (state == 'x' || state == 'z') {
      unknown = true;
      break;
    }
    number |= static_cast<uint64_t>(state == '1') << bit;
  }
  if (unknown) {
    formatted_value = "x";
    return true;
  }
  std::array<char, 32> buffer{};
  if (argument.is_signed != 0 && value.width != 0 && value.width < 64u &&
      (number & (UINT64_C(1) << (value.width - 1u))) != 0) {
    number |= ~((UINT64_C(1) << value.width) - UINT64_C(1));
  }
  const auto converted = argument.is_signed != 0
      ? std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                      std::bit_cast<int64_t>(number))
      : std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
  if (converted.ec != std::errc{}) {
    return false;
  }
  formatted_value.assign(buffer.data(), converted.ptr);
  return true;
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
      (!runtime_phase(context->phase) &&
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
    last_expression_info = nullptr;
    last_expression_parameter = 0;
  }
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
  (void)format_control(FSIM_TF_CONTROL_OUTPUT, mcd, 0, nullptr, nullptr,
                       format, arguments);
  va_end(arguments);
}
void io_printf(PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  (void)format_control(FSIM_TF_CONTROL_OUTPUT, 1, 0, nullptr, nullptr,
                       format, arguments);
  va_end(arguments);
}
FSIM_TF_BYTES(mc_scan_plusargs, (PLI_BYTE8*))
FSIM_TF_INT(tf_add_long, (PLI_INT32*, PLI_INT32*, PLI_INT32, PLI_INT32))
FSIM_TF_INT(tf_asynchoff, (void))
FSIM_TF_INT(tf_asynchon, (void))
PLI_INT32 tf_clearalldelays(void) {
  auto* const time = current_time();
  if (current_call_context == nullptr ||
      !writable_phase(current_call_context->phase) || time == nullptr) {
    return 1;
  }
  time->delay_count = 0;
  return 0;
}
FSIM_TF_INT(tf_compare_long,
            (PLI_UINT32, PLI_UINT32, PLI_UINT32, PLI_UINT32))
FSIM_TF_INT(tf_copypvc_flag, (PLI_INT32))
FSIM_TF_VOID(tf_divide_long, (PLI_INT32*, PLI_INT32*, PLI_INT32, PLI_INT32))
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

PLI_INT32 tf_sizep(const PLI_INT32 parameter) {
  const auto* const argument = current_argument(parameter);
  return argument == nullptr ? 0 : static_cast<PLI_INT32>(argument->width);
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
  *info = {
      static_cast<PLI_INT16>(argument->kind),
      0,
      nullptr,
      0.0,
      argument->expression,
      static_cast<PLI_INT32>((argument->width + 31u) / 32u),
      static_cast<PLI_INT32>(argument->width),
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

PLI_BYTE8* tf_getcstringp(const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
  return value == nullptr || value->kind != FSIM_TF_VALUE_STRING
             ? nullptr
             : value->string_value;
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
    *high_value = value->word_count > 1 ? value->vector_words[1].avalbits : 0;
  }
  return value->vector_words[0].avalbits;
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
PLI_INT32 tf_getnextlongtime(PLI_INT32* const low_time,
                             PLI_INT32* const high_time) {
  auto* const time = current_time();
  uint64_t local{};
  if (time == nullptr || time->has_next_event == 0 ||
      !local_time(time->next_event_ticks, local)) {
    if (writable_pointer(low_time)) *low_time = 0;
    if (writable_pointer(high_time)) *high_time = 0;
    return 0;
  }
  if (writable_pointer(low_time)) *low_time = static_cast<PLI_INT32>(local);
  if (writable_pointer(high_time)) {
    *high_time = static_cast<PLI_INT32>(local >> 32u);
  }
  return 1;
}
PLI_INT32 tf_getp(const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
  return value == nullptr || value->kind != FSIM_TF_VALUE_INTEGRAL ||
                 value->word_count == 0
             ? 0
             : value->vector_words[0].avalbits;
}
FSIM_TF_INT(tf_getpchange, (PLI_INT32))
double tf_getrealp(const PLI_INT32 parameter) {
  auto* const value = current_value(parameter);
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
FSIM_TF_BYTES(tf_gettflist, (void))
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

FSIM_TF_INT(tf_iasynchoff, (PLI_BYTE8*))
FSIM_TF_INT(tf_iasynchon, (PLI_BYTE8*))
PLI_INT32 tf_iclearalldelays(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_clearalldelays() : 1;
}
FSIM_TF_INT(tf_icopypvc_flag, (PLI_INT32, PLI_BYTE8*))
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
FSIM_TF_INT(tf_igetpchange, (PLI_INT32, PLI_BYTE8*))
double tf_igetrealp(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getrealp(parameter) : 0.0;
}
double tf_igetrealtime(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getrealtime() : 0.0;
}
PLI_INT32 tf_igettime(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_gettime() : 0;
}
PLI_INT32 tf_igettimeprecision(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_gettimeprecision() : 0;
}
PLI_INT32 tf_igettimeunit(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_gettimeunit() : 0;
}
PLI_BYTE8* tf_igetworkarea(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getworkarea() : nullptr;
}
PLI_BYTE8* tf_imipname(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_mipname() : nullptr;
}
FSIM_TF_INT(tf_imovepvc_flag, (PLI_INT32, PLI_BYTE8*))
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
  return current_instance_matches(instance) ? tf_setdelay(delay) : 1;
}
PLI_INT32 tf_isetlongdelay(const PLI_INT32 low, const PLI_INT32 high,
                           PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_setlongdelay(low, high) : 1;
}
PLI_INT32 tf_isetrealdelay(const double delay, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_setrealdelay(delay) : 1;
}
PLI_INT32 tf_isetworkarea(PLI_BYTE8* const work_area,
                          PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_setworkarea(work_area) : 1;
}
PLI_INT32 tf_isizep(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_sizep(parameter) : 0;
}
PLI_BYTE8* tf_ispname(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_spname() : nullptr;
}
FSIM_TF_INT(tf_istrdelputp,
            (PLI_INT32, PLI_INT32, PLI_INT32, PLI_BYTE8*, PLI_INT32,
             PLI_INT32, PLI_BYTE8*))
PLI_BYTE8* tf_istrgetp(const PLI_INT32 parameter, const PLI_INT32 format,
                       PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_strgetp(parameter, format)
                                            : nullptr;
}
FSIM_TF_INT(tf_istrlongdelputp,
            (PLI_INT32, PLI_INT32, PLI_INT32, PLI_BYTE8*, PLI_INT32,
             PLI_INT32, PLI_INT32, PLI_BYTE8*))
FSIM_TF_INT(tf_istrrealdelputp,
            (PLI_INT32, PLI_INT32, PLI_INT32, PLI_BYTE8*, double, PLI_INT32,
             PLI_BYTE8*))
PLI_INT32 tf_isynchronize(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_synchronize() : 1;
}
FSIM_TF_INT(tf_itestpvc_flag, (PLI_INT32, PLI_BYTE8*))
PLI_INT32 tf_itypep(const PLI_INT32 parameter, PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_typep(parameter)
                                            : tf_nullparam;
}

FSIM_TF_VOID(tf_long_to_real, (PLI_INT32, PLI_INT32, double*))
PLI_BYTE8* tf_longtime_tostr(const PLI_INT32 low, const PLI_INT32 high) {
  const auto value = (static_cast<uint64_t>(static_cast<PLI_UINT32>(high)) << 32u) |
                     static_cast<PLI_UINT32>(low);
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
  const auto result = format_control(FSIM_TF_CONTROL_MESSAGE, 0, level,
                                     facility, message_number, message,
                                     arguments);
  va_end(arguments);
  return result;
}
PLI_BYTE8* tf_mipname(void) {
  return current_call_context == nullptr
             ? nullptr
             : context_name(current_call_context->module_instance_name,
                            module_instance_name);
}
FSIM_TF_INT(tf_movepvc_flag, (PLI_INT32))
FSIM_TF_VOID(tf_multiply_long, (PLI_INT32*, PLI_INT32*, PLI_INT32, PLI_INT32))
p_tfnodeinfo tf_nodeinfo(const PLI_INT32 parameter, p_tfnodeinfo info) {
  const auto* const argument = current_argument(parameter);
  if (argument == nullptr || argument->kind == tf_nullparam ||
      argument->kind == tf_string) {
    return nullptr;
  }
  if (info == nullptr) {
    info = &node_info;
  } else if (!writable_pointer(info)) {
    return nullptr;
  }
  *info = {};
  info->node_symbol = argument->expression;
  info->node_ngroups = static_cast<PLI_INT32>((argument->width + 31u) / 32u);
  info->node_vec_size = static_cast<PLI_INT32>(argument->width);
  info->node_sign = static_cast<PLI_INT32>(argument->is_signed);
  info->node_ms_index = argument->lhs_select >= 0
                            ? argument->lhs_select
                            : static_cast<PLI_INT32>(argument->width - 1u);
  info->node_ls_index = argument->rhs_select >= 0 ? argument->rhs_select : 0;
  if (argument->kind == tf_readonlyreal ||
      argument->kind == tf_readwritereal) {
    info->node_type = tf_real_node;
  } else if (argument->width == 1u) {
    info->node_type = tf_netscalar_node;
  } else if (argument->width == 32u && argument->is_signed != 0) {
    info->node_type = tf_integer_node;
  } else {
    info->node_type = writable_kind(argument->kind) ? tf_reg_node
                                                    : tf_netvector_node;
  }
  if (auto* const value = current_value(parameter); value != nullptr) {
    if (value->kind == FSIM_TF_VALUE_INTEGRAL) {
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

PLI_INT32 assign_integral_result(const PLI_INT32 parameter,
                                 const PLI_UINT32 low,
                                 const PLI_UINT32 high) {
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
                                   const PLI_UINT32 high) {
  auto* const value = current_value(parameter);
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
                                      static_cast<PLI_UINT32>(high))
             : assign_integral_argument(parameter,
                                        static_cast<PLI_UINT32>(low),
                                        static_cast<PLI_UINT32>(high));
}

PLI_INT32 tf_putp(const PLI_INT32 parameter, const PLI_INT32 value) {
  return parameter == 0
             ? assign_integral_result(parameter,
                                      static_cast<PLI_UINT32>(value), 0)
             : assign_integral_argument(parameter,
                                        static_cast<PLI_UINT32>(value), 0);
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
FSIM_TF_INT(tf_read_restart, (PLI_BYTE8*, PLI_INT32))
FSIM_TF_VOID(tf_real_to_long, (double, PLI_INT32*, PLI_INT32*))
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
PLI_INT32 tf_setdelay(const PLI_INT32 delay) {
  uint64_t ticks{};
  return delay < 0 || !delay_ticks(static_cast<uint64_t>(delay), ticks)
             ? 1
             : append_delay(ticks);
}
PLI_INT32 tf_setlongdelay(const PLI_INT32 low, const PLI_INT32 high) {
  uint64_t ticks{};
  const auto local = (static_cast<uint64_t>(static_cast<PLI_UINT32>(high)) << 32u) |
                     static_cast<PLI_UINT32>(low);
  return !delay_ticks(local, ticks) ? 1 : append_delay(ticks);
}
PLI_INT32 tf_setrealdelay(const double delay) {
  auto* const time = current_time();
  if (time == nullptr || !std::isfinite(delay) || delay < 0.0) return 1;
  const auto scaled = static_cast<long double>(delay) * time_scale(*time) /
                      time->tick_multiplier;
  const auto rounded = std::floor(scaled + 0.5L);
  if (!(rounded < std::ldexp(1.0L, 64))) return 1;
  return append_delay(static_cast<uint64_t>(rounded));
}
PLI_INT32 tf_setworkarea(PLI_BYTE8* const work_area) {
  if (current_call_context == nullptr) {
    return 1;
  }
  current_call_context->work_area = work_area;
  return 0;
}
PLI_BYTE8* tf_spname(void) {
  return current_call_context == nullptr ? nullptr
                                         : context_name(
                                               current_call_context->scope_name,
                                               scope_name);
}
FSIM_TF_INT(tf_strdelputp,
            (PLI_INT32, PLI_INT32, PLI_INT32, PLI_BYTE8*, PLI_INT32,
             PLI_INT32))
PLI_BYTE8* tf_strgetp(const PLI_INT32 parameter, const PLI_INT32 format) {
  const auto* const argument = current_argument(parameter);
  auto* const value = current_value(parameter);
  if (argument == nullptr || value == nullptr ||
      value->kind != FSIM_TF_VALUE_INTEGRAL) {
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
  PLI_INT32 high{};
  const auto low = tf_getlongtime(&high);
  return tf_longtime_tostr(low, high);
}
FSIM_TF_INT(tf_strlongdelputp,
            (PLI_INT32, PLI_INT32, PLI_INT32, PLI_BYTE8*, PLI_INT32,
             PLI_INT32, PLI_INT32))
FSIM_TF_INT(tf_strrealdelputp,
            (PLI_INT32, PLI_INT32, PLI_INT32, PLI_BYTE8*, double, PLI_INT32))
FSIM_TF_INT(tf_subtract_long,
            (PLI_INT32*, PLI_INT32*, PLI_INT32, PLI_INT32))
PLI_INT32 tf_synchronize(void) {
  return append_synchronization(FSIM_TF_SYNCHRONIZATION_READ_WRITE);
}
FSIM_TF_INT(tf_testpvc_flag, (PLI_INT32))
PLI_INT32 tf_text(PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const auto result = format_control(FSIM_TF_CONTROL_OUTPUT, 1, 0, nullptr,
                                     nullptr, format, arguments);
  va_end(arguments);
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
FSIM_TF_INT(tf_write_save, (PLI_BYTE8*, PLI_INT32))
PLI_BYTE8* tf_getroutine(void) {
  return current_call_context == nullptr ? nullptr
                                         : context_name(
                                               current_call_context->routine_name,
                                               routine_name);
}
PLI_BYTE8* tf_igetroutine(PLI_BYTE8* const instance) {
  return current_instance_matches(instance) ? tf_getroutine() : nullptr;
}

#undef FSIM_TF_INT
#undef FSIM_TF_DOUBLE
#undef FSIM_TF_BYTES
#undef FSIM_TF_EXPR
#undef FSIM_TF_NODE
#undef FSIM_TF_VOID

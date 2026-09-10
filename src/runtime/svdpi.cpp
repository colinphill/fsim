// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/svdpi_bridge.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

constexpr int word_bits = 32;
constexpr std::size_t maximum_context_depth = 64U;

thread_local std::array<fsim_svdpi_call_context_v3*, maximum_context_depth>
    context_stack{};
thread_local std::size_t context_depth{};

[[nodiscard]] constexpr bool valid_index(const int index) noexcept {
  return index >= 0;
}

[[nodiscard]] constexpr bool valid_width(const int width) noexcept {
  return width >= 0 && width <= word_bits;
}

[[nodiscard]] constexpr std::uint32_t bit_mask(const int index) noexcept {
  return UINT32_C(1) << (index % word_bits);
}

[[nodiscard]] fsim_svdpi_call_context_v3* current_context() noexcept {
  return context_depth == 0U ? nullptr : context_stack[context_depth - 1U];
}

[[nodiscard]] bool valid_context(
    const fsim_svdpi_call_context_v3* const context) noexcept {
  return context != nullptr
      && context->abi_version == FSIM_SVDPI_CONTEXT_ABI_VERSION
      && context->struct_size >= sizeof(fsim_svdpi_call_context_v3)
      && context->user_data != nullptr
      && context->get_scope != nullptr
      && context->set_scope != nullptr
      && context->get_name_from_scope != nullptr
      && context->get_scope_from_name != nullptr
      && context->put_user_data != nullptr
      && context->get_user_data != nullptr
      && context->get_caller_info != nullptr
      && context->is_disabled_state != nullptr
      && context->acknowledge_disabled_state != nullptr
      && context->get_time != nullptr
      && context->get_time_unit != nullptr
      && context->get_time_precision != nullptr;
}

}  // namespace

extern "C" {

DPI_DLLESPEC const char* svDpiVersion(void) {
  return "1800-2023";
}

DPI_DLLESPEC svBit svGetBitselBit(
    const svBitVecVal* const source, const int index) {
  if (source == nullptr || !valid_index(index)) return sv_0;
  return (source[index / word_bits] & bit_mask(index)) == 0U ? sv_0 : sv_1;
}

DPI_DLLESPEC svLogic svGetBitselLogic(
    const svLogicVecVal* const source, const int index) {
  if (source == nullptr || !valid_index(index)) return sv_x;
  const auto mask = bit_mask(index);
  const auto& word = source[index / word_bits];
  const auto aval = (word.aval & mask) == 0U ? 0U : 1U;
  const auto bval = (word.bval & mask) == 0U ? 0U : 2U;
  return static_cast<svLogic>(aval | bval);
}

DPI_DLLESPEC void svPutBitselBit(
    svBitVecVal* const destination, const int index, const svBit value) {
  if (destination == nullptr || !valid_index(index)) return;
  const auto mask = bit_mask(index);
  auto& word = destination[index / word_bits];
  word = value == sv_0 ? word & ~mask : word | mask;
}

DPI_DLLESPEC void svPutBitselLogic(
    svLogicVecVal* const destination, const int index,
    const svLogic value) {
  if (destination == nullptr || !valid_index(index)) return;
  const auto mask = bit_mask(index);
  auto& word = destination[index / word_bits];
  word.aval = (value & 1U) == 0U ? word.aval & ~mask : word.aval | mask;
  word.bval = (value & 2U) == 0U ? word.bval & ~mask : word.bval | mask;
}

DPI_DLLESPEC void svGetPartselBit(
    svBitVecVal* const destination, const svBitVecVal* const source,
    const int index, const int width) {
  if (destination == nullptr || source == nullptr || !valid_index(index)
      || !valid_width(width)) {
    return;
  }
  *destination = 0U;
  for (int bit = 0; bit < width; ++bit) {
    if (svGetBitselBit(source, index + bit) == sv_1) {
      *destination |= UINT32_C(1) << bit;
    }
  }
}

DPI_DLLESPEC void svGetPartselLogic(
    svLogicVecVal* const destination, const svLogicVecVal* const source,
    const int index, const int width) {
  if (destination == nullptr || source == nullptr || !valid_index(index)
      || !valid_width(width)) {
    return;
  }
  *destination = {};
  for (int bit = 0; bit < width; ++bit) {
    const auto value = svGetBitselLogic(source, index + bit);
    destination->aval |= static_cast<std::uint32_t>(value & 1U) << bit;
    destination->bval |= static_cast<std::uint32_t>((value >> 1U) & 1U) << bit;
  }
}

DPI_DLLESPEC void svPutPartselBit(
    svBitVecVal* const destination, const svBitVecVal source,
    const int index, const int width) {
  if (destination == nullptr || !valid_index(index) || !valid_width(width)) {
    return;
  }
  for (int bit = 0; bit < width; ++bit) {
    svPutBitselBit(
        destination, index + bit,
        static_cast<svBit>((source >> bit) & UINT32_C(1)));
  }
}

DPI_DLLESPEC void svPutPartselLogic(
    svLogicVecVal* const destination, const svLogicVecVal source,
    const int index, const int width) {
  if (destination == nullptr || !valid_index(index) || !valid_width(width)) {
    return;
  }
  for (int bit = 0; bit < width; ++bit) {
    const auto aval = (source.aval >> bit) & UINT32_C(1);
    const auto bval = (source.bval >> bit) & UINT32_C(1);
    svPutBitselLogic(
        destination, index + bit,
        static_cast<svLogic>(aval | (bval << 1U)));
  }
}

DPI_DLLESPEC svScope svGetScope(void) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr : context->get_scope(context->user_data);
}

DPI_DLLESPEC svScope svSetScope(const svScope scope) {
  const auto* const context = current_context();
  return context == nullptr
      ? nullptr : context->set_scope(context->user_data, scope);
}

DPI_DLLESPEC const char* svGetNameFromScope(const svScope scope) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr
      : context->get_name_from_scope(context->user_data, scope);
}

DPI_DLLESPEC svScope svGetScopeFromName(const char* const scope_name) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr
      : context->get_scope_from_name(context->user_data, scope_name);
}

DPI_DLLESPEC int svPutUserData(
    const svScope scope, void* const user_key, void* const user_data) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->put_user_data(
          context->user_data, scope, user_key, user_data);
}

DPI_DLLESPEC void* svGetUserData(
    const svScope scope, void* const user_key) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr
      : context->get_user_data(context->user_data, scope, user_key);
}

DPI_DLLESPEC int svGetCallerInfo(
    const char** const file_name, int* const line_number) {
  const auto* const context = current_context();
  return context == nullptr ? 0
      : context->get_caller_info(
          context->user_data, file_name, line_number);
}

DPI_DLLESPEC int svIsDisabledState(void) {
  const auto* const context = current_context();
  return context == nullptr ? 0
      : context->is_disabled_state(context->user_data);
}

DPI_DLLESPEC void svAckDisabledState(void) {
  const auto* const context = current_context();
  if (context != nullptr) {
    context->acknowledge_disabled_state(context->user_data);
  }
}

DPI_DLLESPEC int svGetTime(
    const svScope scope, svTimeVal* const time_value) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->get_time(context->user_data, scope, time_value);
}

DPI_DLLESPEC int svGetTimeUnit(
    const svScope scope, int32_t* const time_unit) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->get_time_unit(context->user_data, scope, time_unit);
}

DPI_DLLESPEC int svGetTimePrecision(
    const svScope scope, int32_t* const time_precision) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->get_time_precision(
          context->user_data, scope, time_precision);
}

FSIM_SVDPI_BRIDGE_API int FSIM_SVDPI_CALL
fsim_svdpi_call_context_enter_v3(
    fsim_svdpi_call_context_v3* const context) {
  if (!valid_context(context) || context_depth >= context_stack.size()) {
    return -1;
  }
  context_stack[context_depth++] = context;
  return 0;
}

FSIM_SVDPI_BRIDGE_API int FSIM_SVDPI_CALL
fsim_svdpi_call_context_leave_v3(
    fsim_svdpi_call_context_v3* const context) {
  if (context_depth == 0U || context_stack[context_depth - 1U] != context) {
    return -1;
  }
  context_stack[--context_depth] = nullptr;
  return 0;
}

FSIM_SVDPI_BRIDGE_API const fsim_svdpi_call_context_v3* FSIM_SVDPI_CALL
fsim_svdpi_current_call_context_v3(void) {
  return current_context();
}

}  // extern "C"

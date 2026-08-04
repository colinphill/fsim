// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

std::uint64_t LlvmProcessExecutor::signal_last_active(
    void* context,
    const std::uint32_t signal) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure || state.context == nullptr
      || signal >= state.signal_widths.size()) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  try {
    return state.context->signal_last_active(signal);
  } catch (...) {
    capture_failure(state);
    return std::numeric_limits<std::uint64_t>::max();
  }
}

std::uint32_t LlvmProcessExecutor::signal_driving(
    void* context,
    const std::uint32_t signal) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure || state.context == nullptr
      || signal >= state.signal_widths.size()) {
    return 0;
  }
  try {
    return state.context->signal_driving(signal) ? 1U : 0U;
  } catch (...) {
    capture_failure(state);
    return 0;
  }
}

std::uint64_t LlvmProcessExecutor::signal_driving_value(
    void* context,
    const std::uint32_t signal,
    std::uint64_t* bval) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    if (bval != nullptr) {
      *bval = 0;
    }
    return 0;
  }
  try {
    if (bval == nullptr || state.context == nullptr
        || signal >= state.signal_widths.size()) {
      throw std::logic_error{
          "invalid generated signal-driving-value callback"};
    }
    const auto value =
        state.context->signal_driving_value_word(signal);
    if (value.width != state.signal_widths[signal]
        || value.width == 0 || value.width > 64) {
      throw std::logic_error{
          "generated signal-driving-value callback observed an invalid "
          "width"};
    }
    *bval = value.bval;
    return value.aval;
  } catch (...) {
    capture_failure(state);
    if (bval != nullptr) {
      *bval = 0;
    }
    return 0;
  }
}

void LlvmProcessExecutor::signal_driving_value_logic9(
    void* context,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  clear_logic9_word(result);
  if (state.failure) {
    return;
  }
  try {
    require_logic9_signal(state, signal, result);
    const auto value =
        state.context->signal_driving_value_logic9_word(signal);
    if (value.width != state.signal_widths[signal]) {
      throw std::logic_error{
          "generated Logic9 driving-value read observed an invalid width"};
    }
    result->planes[0] = value.planes[0];
    result->planes[1] = value.planes[1];
    result->planes[2] = value.planes[2];
    result->planes[3] = value.planes[3];
  } catch (...) {
    capture_failure(state);
    clear_logic9_word(result);
  }
}

#endif

}  // namespace fsim::app::application_detail

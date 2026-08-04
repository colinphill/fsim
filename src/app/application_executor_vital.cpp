// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

std::uint32_t LlvmProcessExecutor::vital_timing_check(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure || state.context == nullptr || state.process == nullptr
      || process != state.process->id
      || instruction >= state.process->operations.size()) {
    return static_cast<std::uint32_t>(runtime::Logic9::x);
  }
  try {
    const auto* operation = runtime::simir::operation_get_if<
        runtime::simir::VitalTimingCheck>(
            &state.process->operations[instruction]);
    if (operation == nullptr) {
      throw std::logic_error{
          "generated VITAL callback references the wrong operation"};
    }
    return static_cast<std::uint32_t>(
        state.context->evaluate_vital_timing_check(
            instruction, *operation));
  } catch (...) {
    capture_failure(state);
    return static_cast<std::uint32_t>(runtime::Logic9::x);
  }
}

void LlvmProcessExecutor::vital_delay(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure || state.executor == nullptr || state.context == nullptr
      || state.process == nullptr || process != state.process->id
      || instruction >= state.process->operations.size()) return;
  try {
    const auto* operation = runtime::simir::operation_get_if<
        runtime::simir::VitalDelay>(&state.process->operations[instruction]);
    if (operation == nullptr) {
      throw std::logic_error{
          "generated VITAL delay callback references the wrong operation"};
    }
    const auto tick = [&](const runtime::simir::RegisterId id) {
      const auto word = state.executor->read_register(id, 64U).low_word();
      if (word.bval != 0U) {
        throw std::invalid_argument{
            "VITAL delay contains an unknown time value"};
      }
      return static_cast<runtime::SimulationTick>(word.aval);
    };
    runtime::simir::VitalDelayRuntimeValues values;
    values.source = state.executor
        ->read_register(operation->source, 1U).get_logic9(0U);
    for (std::size_t index = 0; index < 6U; ++index) {
      values.default_delays[index] = tick(operation->default_delays[index]);
    }
    if (operation->shape == runtime::simir::VitalDelayShape::delay01z) {
      const auto map = state.executor->read_register(operation->output_map, 9U);
      for (std::size_t index = 0; index < 9U; ++index) {
        values.output_map[index] = map.get_logic9(8U - index);
      }
    }
    values.paths.reserve(operation->paths.size());
    for (const auto& path : operation->paths) {
      runtime::simir::VitalPathRuntimeValue value;
      value.input_change_time = tick(path.input_change_time);
      const auto condition = state.executor
          ->read_register(path.condition, 1U).low_word();
      if (condition.bval != 0U) {
        throw std::invalid_argument{
            "VITAL path condition contains an unknown value"};
      }
      value.condition = (condition.aval & 1U) != 0U;
      for (std::size_t index = 0; index < 6U; ++index) {
        value.delays[index] = tick(path.delays[index]);
      }
      values.paths.push_back(value);
    }
    state.context->execute_vital_delay(instruction, *operation, values);
  } catch (...) {
    capture_failure(state);
  }
}

}  // namespace fsim::app::application_detail

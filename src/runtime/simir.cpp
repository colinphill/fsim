// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

std::optional<SimulationTick> transition_delay(
    const PackedLogic4& current,
    const PackedLogic4& next,
    const TransitionDelays& delays) {
  if (current.width() == 0 || current.width() != next.width()) {
    throw std::invalid_argument(
        "transition-delay values must have the same non-zero width");
  }
  std::optional<SimulationTick> selected;
  const auto consider =
      [&](const SimulationTick candidate) {
        if (!selected || candidate < *selected) {
          selected = candidate;
        }
      };
  for (std::size_t bit = 0; bit < current.width(); ++bit) {
    if (current.get(bit) == next.get(bit)) {
      continue;
    }
    switch (next.get(bit)) {
      case Logic4::zero:
        consider(delays.fall);
        break;
      case Logic4::one:
        consider(delays.rise);
        break;
      case Logic4::z:
        consider(delays.turnoff);
        break;
      case Logic4::x:
        consider(std::min(
            {delays.rise, delays.fall, delays.turnoff}));
        break;
    }
  }
  return selected;
}

InterpreterError::InterpreterError(ProcessId process,
                                   InstructionIndex instruction,
                                   std::string message)
    : std::runtime_error(error_text(process, instruction, message)),
      process_(process), instruction_(instruction) {}

AssertionError::AssertionError(ProcessId process,
                               InstructionIndex instruction,
                               std::string message,
                               AssertionSeverity severity,
                               SourceLocation source,
                               const bool reported)
    : InterpreterError(
          process, instruction,
          message.empty() ? "assertion failed" : std::move(message)),
      severity_(severity), source_(std::move(source)),
      reported_(reported) {}


} // namespace fsim::runtime::simir

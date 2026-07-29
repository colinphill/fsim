// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app {
using namespace application_detail;

struct DebuggerControl::Impl {
  Impl(
      Simulation& simulation,
      std::ostream& output,
      std::ostream& error)
      : session(simulation, output, error) {}

  Impl(
      Simulation& simulation,
      std::ostream& output,
      std::ostream& error,
      const project::Config& config,
      diagnostic::Engine& diagnostics)
      : trace(attach_trace(simulation, config, diagnostics, true)),
        session(simulation, output, error, trace.get()) {
    if (config.run.trace_file && !trace) {
      throw std::runtime_error{"failed to initialize debugger trace output"};
    }
  }

  std::unique_ptr<TraceState> trace;
  DebuggerSession session;
};

DebuggerControl::DebuggerControl(
    Simulation& simulation,
    std::ostream& output,
    std::ostream& error)
    : impl_(std::make_unique<Impl>(simulation, output, error)) {}

DebuggerControl::DebuggerControl(
    Simulation& simulation,
    std::ostream& output,
    std::ostream& error,
    const project::Config& config,
    diagnostic::Engine& diagnostics)
    : impl_(std::make_unique<Impl>(
          simulation, output, error, config, diagnostics)) {}

DebuggerControl::~DebuggerControl() = default;
DebuggerControl::DebuggerControl(DebuggerControl&&) noexcept = default;
DebuggerControl& DebuggerControl::operator=(
    DebuggerControl&&) noexcept = default;

void DebuggerControl::execute(
    const std::vector<std::string>& command) {
  if (command.empty()) {
    throw std::invalid_argument("debugger command cannot be empty");
  }
  impl_->session.execute(command);
}


} // namespace fsim::app

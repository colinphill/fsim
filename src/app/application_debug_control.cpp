// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_trace_control.hpp"

namespace fsim::app {
using namespace application_detail;

struct DebuggerControl::Impl {
  Impl(
      Simulation& simulation_ref,
      std::ostream& output,
      std::ostream& error)
      : simulation(simulation_ref), session(simulation_ref, output, error) {}

  Impl(
      Simulation& simulation_ref,
      std::ostream& output,
      std::ostream& error,
      const project::Config& config,
      diagnostic::Engine& diagnostics)
      : simulation(simulation_ref),
        trace(attach_trace(simulation_ref, config, diagnostics, true)),
        session(simulation_ref, output, error, trace.get()) {
    if (config.run.trace_file && config.run.trace_enabled && !trace) {
      std::string message{"failed to initialize debugger trace output"};
      if (!diagnostics.diagnostics().empty()) {
        const auto& failure = diagnostics.diagnostics().back();
        message += ": " + failure.code + ": " + failure.message;
      }
      throw std::runtime_error{std::move(message)};
    }
  }

  ~Impl() {
    if (trace && trace->diagnostics) {
      static_cast<void>(finish_trace(*trace, *trace->diagnostics));
    }
  }

  void finish_trace_if_finished()
  {
    if (trace && trace->diagnostics && simulation.finished()
        && !finish_trace(*trace, *trace->diagnostics)) {
      throw std::runtime_error("failed to finalize debugger trace output");
    }
  }

  Simulation& simulation;
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
  impl_->finish_trace_if_finished();
}

DebuggerStatus DebuggerControl::status() const
{
  return impl_->session.status();
}

void DebuggerControl::step(const std::string_view kind)
{
  impl_->session.step(kind);
  impl_->finish_trace_if_finished();
}

void DebuggerControl::continue_run(
    const std::optional<std::string_view> duration)
{
  impl_->session.continue_run(duration);
  impl_->finish_trace_if_finished();
}

DebuggerBreakpointInfo DebuggerControl::add_breakpoint(
    const std::string_view kind,
    const std::string_view location,
    const std::optional<std::string_view> comparison,
    const std::optional<std::string_view> value)
{
  return impl_->session.add_breakpoint(kind, location, comparison, value);
}

DebuggerBreakpointInfo DebuggerControl::add_watch(
    const std::string_view signal,
    const std::optional<std::string_view> comparison,
    const std::optional<std::string_view> value)
{
  return impl_->session.add_watch(signal, comparison, value);
}

std::vector<DebuggerBreakpointInfo> DebuggerControl::breakpoints() const
{
  return impl_->session.breakpoints();
}

std::vector<DebuggerBreakpointInfo> DebuggerControl::watches() const
{
  return impl_->session.watches();
}

void DebuggerControl::delete_breakpoint(const std::uint64_t id)
{
  impl_->session.delete_breakpoint(id);
}

void DebuggerControl::delete_watch(const std::uint64_t id)
{
  impl_->session.delete_watch(id);
}

void DebuggerControl::clear_breakpoints()
{
  impl_->session.clear_breakpoints();
}

std::vector<DebuggerFrameInfo> DebuggerControl::frames() const
{
  return impl_->session.frames();
}

DebuggerFrameInfo DebuggerControl::frame(const std::size_t index) const
{
  return impl_->session.frame(index);
}

DebuggerScopeInfo DebuggerControl::scope(
    const std::optional<std::string_view> path)
{
  return impl_->session.scope(path);
}

DebuggerInspection DebuggerControl::inspect(
    const std::string_view path) const
{
  return impl_->session.inspect(path);
}

std::vector<DebuggerSourceProvenance> DebuggerControl::provenance(
    const std::optional<std::string_view> path) const
{
  return impl_->session.provenance(path);
}

std::optional<TraceControlStatus> DebuggerControl::trace_status() const
{
  if (!impl_->trace || !impl_->trace->control) {
    return std::nullopt;
  }
  auto result = impl_->trace->control->status();
  switch (impl_->trace->terminal_status) {
  case TraceTerminalStatus::open:
    result.lifecycle = TraceLifecycle::Open;
    break;
  case TraceTerminalStatus::complete:
    result.lifecycle = TraceLifecycle::Complete;
    break;
  case TraceTerminalStatus::failed:
    result.lifecycle = TraceLifecycle::Failed;
    break;
  }
  if (impl_->trace->selection) {
    const auto selection = impl_->trace->selection->status();
    result.selection_count = selection.selected;
    result.generation = selection.generation;
  }
  return result;
}

std::span<const TraceControlReportEntry>
DebuggerControl::trace_report() const noexcept
{
  return impl_->trace && impl_->trace->control
      ? impl_->trace->control->report()
      : std::span<const TraceControlReportEntry> { };
}


} // namespace fsim::app

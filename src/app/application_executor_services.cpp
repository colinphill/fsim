// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

SystemCProcessExecutor::SystemCProcessExecutor(
    std::shared_ptr<systemc::HierarchyRegistry> hierarchy,
    const std::uint64_t process)
    : hierarchy_(std::move(hierarchy)), process_(process) {
  if (!hierarchy_) {
    throw std::invalid_argument{
        "SystemC process executor requires a hierarchy registry"};
  }
}

runtime::simir::ProcessResumeResult SystemCProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    runtime::simir::InstructionIndex) {
  const auto suspension = hierarchy_->invoke_process(process_, context);
  runtime::simir::ProcessResumeResult result{0, 1};
  switch (suspension.kind) {
  case systemc::MethodSuspendKind::halt:
    result.external.kind = runtime::simir::ExternalSuspendKind::halt;
    break;
  case systemc::MethodSuspendKind::static_sensitivity:
    result.external.kind =
        runtime::simir::ExternalSuspendKind::wait_sensitivity;
    break;
  case systemc::MethodSuspendKind::wait_for:
    result.external.kind =
        suspension.delay_ticks == 0
            ? runtime::simir::ExternalSuspendKind::yield
            : runtime::simir::ExternalSuspendKind::wait_for;
    result.external.delay = suspension.delay_ticks;
    break;
  case systemc::MethodSuspendKind::wait_event:
    result.external.kind = runtime::simir::ExternalSuspendKind::wait_on;
    result.external.wait_all = suspension.wait_all;
    result.external.sensitivity.reserve(
        suspension.event_signals.size());
    for (const auto event : suspension.event_signals) {
      result.external.sensitivity.push_back(
          {event, runtime::simir::EdgeKind::any});
    }
    break;
  }
  return result;
}

void SystemCProcessExecutor::update_channel(
    const std::uint64_t channel,
    runtime::simir::ProcessExecutionContext& context) {
  hierarchy_->invoke_primitive_channel(channel, context);
}

std::string_view report_severity_name(
    const runtime::simir::AssertionSeverity severity) noexcept {
  switch (severity) {
  case runtime::simir::AssertionSeverity::note: return "note";
  case runtime::simir::AssertionSeverity::warning: return "warning";
  case runtime::simir::AssertionSeverity::error: return "error";
  case runtime::simir::AssertionSeverity::failure: return "failure";
  }
  return "error";
}

std::uint64_t entropy_seed() {
  std::random_device source;
  const auto high = static_cast<std::uint64_t>(source());
  const auto low = static_cast<std::uint64_t>(source());
  return (high << 32U) ^ low;
}

#if defined(FSIM_HAS_LLVM)

compiler::JitOptimizationLevel jit_optimization(
    const project::Optimization optimization) noexcept {
  return optimization == project::Optimization::o0
      ? compiler::JitOptimizationLevel::o0
      : compiler::JitOptimizationLevel::o2;
}

#endif

}  // namespace fsim::app::application_detail

// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_execution.hpp"
#include "fsim/frontend/coverage_limits.hpp"

#include <ranges>
#include <utility>

namespace fsim::frontend {
namespace {

bool trigger_matches(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageSampleTrigger trigger) {
  if (trigger == SystemVerilogCoverageSampleTrigger::Explicit) {
      return !declaration.sampling
          || declaration.sampling->kind
          == SystemVerilogCovergroupSamplingKind::Event;
  }
  if (!declaration.sampling) return false;
  if (trigger == SystemVerilogCoverageSampleTrigger::Event) {
    return declaration.sampling->kind
        == SystemVerilogCovergroupSamplingKind::Event;
  }
  return declaration.sampling->kind
      == SystemVerilogCovergroupSamplingKind::WithFunctionSample;
}

void emit(
    SystemVerilogCoverageExecutionResult& result,
    SystemVerilogCoverageExecutionState& state,
    const SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCoverageSampleTrigger trigger,
    const SystemVerilogCoverageExecutionMode mode,
    const SystemVerilogCoverageCallbackKind kind,
    std::optional<std::string> identity,
    const std::optional<std::int64_t> value,
    const std::span<const SystemVerilogCoverageCallback> callbacks) {
  SystemVerilogCoverageCallbackEvent event;
  event.sequence = state.next_callback_sequence++;
  event.kind = kind;
  event.trigger = trigger;
  event.mode = mode;
  event.runtime_identity = instance.runtime_identity;
  event.bin_identity = std::move(identity);
  event.value = value;
  result.events.push_back(event);
  for (const auto& callback : callbacks) callback(event);
}

class SamplingGuard {
 public:
  explicit SamplingGuard(SystemVerilogCoverageExecutionState& state)
      : state_{state} {
    state_.sampling = true;
  }

  SamplingGuard(const SamplingGuard&) = delete;
  SamplingGuard& operator=(const SamplingGuard&) = delete;

  ~SamplingGuard() { state_.sampling = false; }

 private:
  SystemVerilogCoverageExecutionState& state_;
};

}  // namespace

SystemVerilogCoverageExecutionResult
execute_systemverilog_covergroup_sample(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageSampleTrigger trigger,
    const SystemVerilogCoverageExecutionMode mode,
    const std::span<const SystemVerilogCovergroupSampleInput> inputs,
    const std::span<const SystemVerilogCoverageCallback> callbacks,
    SystemVerilogCoverageExecutionState& state,
    std::vector<Diagnostic>& diagnostics) {
  SystemVerilogCoverageExecutionResult result;
  if (state.sampling) {
    result.reentrant_rejected = true;
    diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::Error,
        "FSIM-SV-COV-003",
        "reentrant covergroup sampling is not permitted for '"
            + declaration.canonical_identity + "'",
        declaration.span,
        {}});
    return result;
  }
  if (!trigger_matches(declaration, trigger)) {
    result.trigger_rejected = true;
    diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::Error,
        "FSIM-SV-COV-004",
        "covergroup sampling trigger does not match declaration '"
            + declaration.canonical_identity + "'",
        declaration.span,
        {}});
    return result;
  }
  if (!validate_systemverilog_coverage_sample_resources(
          instance, declaration, inputs.size(), diagnostics)) {
    return result;
  }

  SamplingGuard guard{state};
  result.accepted = true;
  emit(
      result, state, instance, trigger, mode,
      SystemVerilogCoverageCallbackKind::PreSample,
      std::nullopt, std::nullopt, callbacks);
  result.sample = sample_systemverilog_covergroup(
      instance, declaration, inputs, diagnostics);
  for (const auto& sampled : result.sample.coverpoints) {
    const auto input = std::ranges::find(
        inputs,
        sampled.coverage_declaration_index,
        &SystemVerilogCovergroupSampleInput::coverage_declaration_index);
    const auto value = input == inputs.end()
        ? std::optional<std::int64_t>{}
        : std::optional<std::int64_t>{input->value.value};
    for (const auto& identity : sampled.result.hit_bin_identities) {
      emit(
          result, state, instance, trigger, mode,
          SystemVerilogCoverageCallbackKind::Hit,
          identity, value, callbacks);
    }
    if (sampled.result.illegal
        && sampled.result.selected_bin_identity) {
      emit(
          result, state, instance, trigger, mode,
          SystemVerilogCoverageCallbackKind::IllegalBin,
          sampled.result.selected_bin_identity, value, callbacks);
    }
  }
  for (const auto& identity : result.sample.hit_cross_bin_identities) {
    emit(
        result, state, instance, trigger, mode,
        SystemVerilogCoverageCallbackKind::Hit,
        identity, std::nullopt, callbacks);
  }
  emit(
      result, state, instance, trigger, mode,
      SystemVerilogCoverageCallbackKind::PostSample,
      std::nullopt, std::nullopt, callbacks);
  return result;
}

}  // namespace fsim::frontend

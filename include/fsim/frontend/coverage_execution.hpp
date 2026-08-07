// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_sampling.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {

enum class SystemVerilogCoverageSampleTrigger {
  Explicit,
  Event,
  Procedural,
};

enum class SystemVerilogCoverageExecutionMode {
  Interpreter,
  LlvmO0,
  LlvmO2,
};

enum class SystemVerilogCoverageCallbackKind {
  PreSample,
  Hit,
  IllegalBin,
  PostSample,
};

struct SystemVerilogCoverageCallbackEvent {
  std::uint64_t sequence{};
  SystemVerilogCoverageCallbackKind kind{
      SystemVerilogCoverageCallbackKind::PreSample};
  SystemVerilogCoverageSampleTrigger trigger{
      SystemVerilogCoverageSampleTrigger::Explicit};
  SystemVerilogCoverageExecutionMode mode{
      SystemVerilogCoverageExecutionMode::Interpreter};
  std::string runtime_identity;
  std::optional<std::string> bin_identity;
  std::optional<std::int64_t> value;
};

using SystemVerilogCoverageCallback =
    std::function<void(const SystemVerilogCoverageCallbackEvent&)>;

struct SystemVerilogCoverageExecutionState {
  std::uint64_t next_callback_sequence{};
  bool sampling{};
};

struct SystemVerilogCoverageExecutionResult {
  SystemVerilogCovergroupSampleResult sample;
  std::vector<SystemVerilogCoverageCallbackEvent> events;
  bool accepted{};
  bool reentrant_rejected{};
  bool trigger_rejected{};
};

[[nodiscard]] SystemVerilogCoverageExecutionResult
execute_systemverilog_covergroup_sample(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    SystemVerilogCoverageSampleTrigger trigger,
    SystemVerilogCoverageExecutionMode mode,
    std::span<const SystemVerilogCovergroupSampleInput> inputs,
    std::span<const SystemVerilogCoverageCallback> callbacks,
    SystemVerilogCoverageExecutionState& state,
    std::vector<Diagnostic>& diagnostics);

}  // namespace fsim::frontend

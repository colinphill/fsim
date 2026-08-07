// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_execution.hpp"
#include "fsim/frontend/coverage_report.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {

enum class SystemVerilogCoverageObservationKind {
  Unsigned,
  Boolean,
  Text,
};

struct SystemVerilogCoverageRoot {
  std::string path;
  SystemVerilogCoverageTypeReport report;
};

struct SystemVerilogCoverageAlias {
  std::string path;
  std::string canonical_root;
};

struct SystemVerilogCoverageObservation {
  std::string path;
  std::string canonical_path;
  SystemVerilogCoverageObservationKind kind{
      SystemVerilogCoverageObservationKind::Unsigned};
  std::uint64_t unsigned_value{};
  std::string text_value;
  bool vcd_compatible{};
};

struct SystemVerilogCoverageTraceEvent {
  std::uint64_t sequence{};
  std::uint64_t time{};
  std::uint64_t delta{};
  std::string path;
  std::string canonical_path;
  SystemVerilogCoverageCallbackKind kind{
      SystemVerilogCoverageCallbackKind::PreSample};
  std::optional<std::int64_t> sampled_value;
  bool vcd_compatible{};
};

[[nodiscard]] std::vector<SystemVerilogCoverageObservation>
build_systemverilog_coverage_debug_snapshot(
    std::span<const SystemVerilogCoverageRoot> roots,
    std::span<const SystemVerilogCoverageAlias> aliases);

[[nodiscard]] std::vector<SystemVerilogCoverageTraceEvent>
build_systemverilog_coverage_trace_events(
    const SystemVerilogCoverageExecutionResult& execution,
    std::uint64_t time,
    std::uint64_t delta,
    std::span<const SystemVerilogCoverageAlias> aliases);

}  // namespace fsim::frontend

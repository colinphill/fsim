// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_execution.hpp"
#include "fsim/frontend/coverage_observation.hpp"
#include "fsim/frontend/coverage_report.hpp"
#include "fsim/frontend/design.hpp"

#include <vector>

namespace fsim::frontend {

/// Owning, host-independent coverage state used by save/restore and artifacts.
struct SystemVerilogCoverageState {
  std::vector<SystemVerilogCovergroupDeclaration> declarations;
  std::vector<SystemVerilogCovergroupInstance> instances;
  std::vector<SystemVerilogCoverageTypeReport> reports;
  std::vector<SystemVerilogCoverageCallbackEvent> callback_events;
  std::vector<SystemVerilogCoverageTraceEvent> trace_events;
  std::vector<SystemVerilogCoverageAlias> aliases;
};

[[nodiscard]] SystemVerilogCoverageState
capture_systemverilog_coverage_state(const ParsedDesign& design);

/// Rebuild derived reports after the mutable hit/progress inventory changes.
void refresh_systemverilog_coverage_reports(
    SystemVerilogCoverageState& state);

}  // namespace fsim::frontend

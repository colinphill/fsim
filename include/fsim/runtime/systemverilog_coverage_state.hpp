// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_observation.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <string>
#include <vector>

namespace fsim::runtime {

/// Stable reference to one covergroup declaration owned by compiled HIR.
/// Runtime coverage state deliberately retains no declaration syntax or
/// structural frontend node.
struct SystemVerilogCoverageDeclarationReference {
    std::string canonical_identity;
};

/// Mutable, parser-independent functional-coverage state. Covergroup
/// declarations remain single-owned by semantic::sv::Hir; this record owns
/// only declaration identities, HIR-native instance state, and syntax-free
/// report/observation leaves.
struct SystemVerilogCoverageState {
    std::vector<SystemVerilogCoverageDeclarationReference> declarations;
    std::vector<semantic::sv::CovergroupInstance> instances;
    std::vector<frontend::SystemVerilogCoverageTypeReport> reports;
    std::vector<frontend::SystemVerilogCoverageCallbackEvent> callback_events;
    std::vector<frontend::SystemVerilogCoverageTraceEvent> trace_events;
    std::vector<frontend::SystemVerilogCoverageAlias> aliases;
};

} // namespace fsim::runtime

// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_observation.hpp"
#include "fsim/frontend/coverage_options.hpp"
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
/// The instance options of one covergroup instance (IEEE 1800-2023 19.7)
/// as its construction and procedural assignments left them.
struct SystemVerilogCovergroupOptionState {
    std::string runtime_identity;
    frontend::SystemVerilogCovergroupInstanceOptions options;
};

/// The type options of one covergroup type (IEEE 1800-2023 19.7.1) as its
/// definition and procedural assignments left them.
struct SystemVerilogCovergroupTypeOptionState {
    std::string declaration_identity;
    frontend::SystemVerilogCovergroupTypeOptions options;
};

struct SystemVerilogCoverageState {
    std::vector<SystemVerilogCoverageDeclarationReference> declarations;
    // One entry per declaration, in the order of `declarations`.
    std::vector<SystemVerilogCovergroupTypeOptionState> type_options;
    std::vector<semantic::sv::CovergroupInstance> instances;
    // One entry per instance, in the order of `instances`.
    std::vector<SystemVerilogCovergroupOptionState> options;
    std::vector<frontend::SystemVerilogCoverageTypeReport> reports;
    std::vector<frontend::SystemVerilogCoverageCallbackEvent> callback_events;
    std::vector<frontend::SystemVerilogCoverageTraceEvent> trace_events;
    std::vector<frontend::SystemVerilogCoverageAlias> aliases;
};

} // namespace fsim::runtime

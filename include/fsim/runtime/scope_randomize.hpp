// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_randomize.hpp"

#include <string>
#include <vector>

namespace fsim::runtime {

struct SystemVerilogScopeRandomizeVariable {
  std::string canonical_identity;
  SystemVerilogConstraintVariableProfile profile;
  /// Empty requests the complete known two-state domain for the exact width.
  /// Enum and materialized container callers provide their bounded domain.
  std::vector<PackedLogic4> domain;
  PackedLogic4* target{};
};

struct SystemVerilogScopeRandomizeRequest {
  std::vector<SystemVerilogScopeRandomizeVariable> variables;
  SystemVerilogClassConstraintConfigurator inline_constraints;
  SystemVerilogConstraintSolverLimits limits;
  std::uint64_t selection{};
};

/// Transactionally randomize caller-owned local values. No target is mutated
/// unless one complete bounded solution has been validated and staged.
[[nodiscard]] SystemVerilogClassRandomizeResult
randomize_systemverilog_scope(
    const SystemVerilogScopeRandomizeRequest& request);

}  // namespace fsim::runtime

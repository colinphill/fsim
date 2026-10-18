// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/constraint_solver.hpp"

#include <functional>
#include <map>
#include <span>
#include <vector>

namespace fsim::runtime {

/// The bounded domain of `variable`, reading each variable in `fixed` as the
/// constant it holds: a state variable of the randomized object or scope is
/// not random during the call (IEEE 1800-2017 18.3), so `x == state` bounds x.
[[nodiscard]] std::vector<PackedLogic4>
systemverilog_constraint_variable_domain_with_fixed(
    std::span<const SystemVerilogConstraintClause> clauses,
    SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random,
    const std::map<SystemVerilogConstraintVariableId, PackedLogic4>& fixed);

}  // namespace fsim::runtime

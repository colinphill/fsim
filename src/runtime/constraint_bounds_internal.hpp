// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/constraint_solver.hpp"

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace fsim::runtime {

/// The inclusive range of values a variable can take, read by its own
/// signedness.
struct SystemVerilogConstraintValueInterval {
  __int128 low{};
  __int128 high{};
  friend bool operator==(const SystemVerilogConstraintValueInterval&,
      const SystemVerilogConstraintValueInterval&) = default;
};
using SystemVerilogConstraintValueIntervals
    = std::map<SystemVerilogConstraintVariableId,
        SystemVerilogConstraintValueInterval>;

/// The interval spanning `domain`'s values, when each is a known value of
/// 1 to 64 bits.
[[nodiscard]] std::optional<SystemVerilogConstraintValueInterval>
systemverilog_constraint_value_interval(
    std::span<const PackedLogic4> domain,
    const SystemVerilogConstraintVariableProfile& profile);

/// Candidate values for a variable wider than 64 bits. An unsigned variable
/// whose hard comparisons with constants bound it below 2^64 draws from that
/// bounded domain; otherwise it draws from the constants its clauses compare
/// it equal to and from uniform samples (IEEE 1800-2017 18.5).
[[nodiscard]] std::vector<PackedLogic4>
systemverilog_constraint_wide_variable_domain(
    std::span<const SystemVerilogConstraintClause> clauses,
    SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random,
    const std::map<SystemVerilogConstraintVariableId, PackedLogic4>& fixed
    = {});

/// `clauses` with each variable in `fixed` other than `variable` read as the
/// constant it holds (IEEE 1800-2017 18.3).
[[nodiscard]] std::vector<SystemVerilogConstraintClause> clauses_with_fixed(
    std::span<const SystemVerilogConstraintClause> clauses,
    SystemVerilogConstraintVariableId variable,
    const std::map<SystemVerilogConstraintVariableId, PackedLogic4>& fixed);

/// The bounded domain of `variable`, reading each variable in `fixed` as the
/// constant it holds: a state variable of the randomized object or scope is
/// not random during the call (IEEE 1800-2017 18.3), so `x == state` bounds x.
/// A comparison with an expression over variables of known `intervals`
/// (`x < a + 5`) bounds `variable` by that expression's extreme values.
[[nodiscard]] std::vector<PackedLogic4>
systemverilog_constraint_variable_domain_with_fixed(
    std::span<const SystemVerilogConstraintClause> clauses,
    SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random,
    const std::map<SystemVerilogConstraintVariableId, PackedLogic4>& fixed,
    const SystemVerilogConstraintValueIntervals* intervals = nullptr);

}  // namespace fsim::runtime

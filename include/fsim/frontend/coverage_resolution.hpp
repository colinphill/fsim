// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <string_view>
#include <vector>

namespace fsim::frontend {

/// A covergroup variable's `get_inst_coverage()` or `get_coverage()`, or
/// that of one of its coverpoints or crosses (IEEE 1800-2023 19.8, 19.9),
/// becomes, once the variable is bound to its instance, a call marked
/// `sv_coverage_query_instance` (payload: the instance's runtime identity)
/// or `sv_coverage_query_type` (payload: the covergroup's declaration
/// identity) whose one operand is the item: 0 for the covergroup, n for its
/// coverage declaration n - 1.
[[nodiscard]] constexpr bool is_coverage_query(
    const support::ExpressionMarker marker) noexcept
{
    return marker == support::ExpressionMarker::sv_coverage_query_instance
        || marker == support::ExpressionMarker::sv_coverage_query_type;
}

/// Resolve SystemVerilog covergroup types, coverage references, constructor
/// and sample profiles, and deterministic instance identities in one merged
/// project snapshot. Source-owned tokens remain unchanged.
[[nodiscard]] bool resolve_systemverilog_covergroups(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

} // namespace fsim::frontend

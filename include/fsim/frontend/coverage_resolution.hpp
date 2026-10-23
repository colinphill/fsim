// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <string_view>
#include <vector>

namespace fsim::frontend {

/// The call a covergroup variable's `get_inst_coverage()` or `get_coverage()`
/// (IEEE 1800-2023 19.8) becomes once the variable is bound to its
/// instance. The text after the prefix is the instance's runtime identity or
/// the covergroup's declaration identity, and the call has no operands.
inline constexpr std::string_view coverage_instance_query_prefix
    = "@sv-coverage-query:instance:";
inline constexpr std::string_view coverage_type_query_prefix
    = "@sv-coverage-query:type:";

[[nodiscard]] constexpr bool is_coverage_query_call(
    const std::string_view text) noexcept
{
    return text.starts_with(coverage_instance_query_prefix)
        || text.starts_with(coverage_type_query_prefix);
}

/// Resolve SystemVerilog covergroup types, coverage references, constructor
/// and sample profiles, and deterministic instance identities in one merged
/// project snapshot. Source-owned tokens remain unchanged.
[[nodiscard]] bool resolve_systemverilog_covergroups(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

} // namespace fsim::frontend

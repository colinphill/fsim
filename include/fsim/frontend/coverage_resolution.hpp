// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <vector>

namespace fsim::frontend {

/// Resolve SystemVerilog covergroup types, coverage references, constructor
/// and sample profiles, and deterministic instance identities in one merged
/// project snapshot. Source-owned tokens remain unchanged.
[[nodiscard]] bool resolve_systemverilog_covergroups(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

} // namespace fsim::frontend

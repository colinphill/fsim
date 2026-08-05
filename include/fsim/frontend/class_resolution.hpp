// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <vector>

namespace fsim::frontend {

/// Resolve SystemVerilog class declarations and handle types in one merged
/// project snapshot. Diagnostics are appended transactionally; declarations
/// retain their source spellings and gain canonical identities on success.
[[nodiscard]] bool resolve_systemverilog_classes(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

}  // namespace fsim::frontend

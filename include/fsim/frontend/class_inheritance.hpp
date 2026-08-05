// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <vector>

namespace fsim::frontend {

/// Validate resolved single inheritance, interface implementation, method
/// overrides, and pure/final obligations.
[[nodiscard]] bool validate_systemverilog_class_inheritance(
    const ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

}  // namespace fsim::frontend

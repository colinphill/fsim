// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <vector>

namespace fsim::frontend {

// Materialize the finite cross-bin denominator before the first sample. The
// operation is idempotent and publishes no partial state on resource failure.
[[nodiscard]] bool initialize_systemverilog_cross_inventory(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::vector<Diagnostic>& diagnostics);

} // namespace fsim::frontend

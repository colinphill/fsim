// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <vector>

namespace fsim::frontend {

[[nodiscard]] bool resolve_systemverilog_class_expressions(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

}  // namespace fsim::frontend

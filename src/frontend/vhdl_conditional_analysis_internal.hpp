// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/source.hpp"
#include "fsim/frontend/token.hpp"

#include <vector>

namespace fsim::frontend {

struct VhdlConditionalAnalysisResult {
    SourceText source;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] VhdlConditionalAnalysisResult analyze_vhdl_conditionals(
    SourceText source, VhdlStandard standard);

} // namespace fsim::frontend

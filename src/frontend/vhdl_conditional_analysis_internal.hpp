// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/source.hpp"
#include "fsim/frontend/token.hpp"

#include <string>
#include <utility>
#include <vector>

namespace fsim::frontend {

struct VhdlConditionalAnalysisResult {
    SourceText source;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] VhdlConditionalAnalysisResult analyze_vhdl_conditionals(
    SourceText source, VhdlStandard standard,
    const VhdlConditionalIdentifiers& identifiers);

} // namespace fsim::frontend

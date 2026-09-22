// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <string>
#include <vector>

namespace fsim::elaboration {

struct VhdlHirTypeValidationIssue {
    std::string code;
    std::string message;
    semantic::SourceSpanId source;
};

[[nodiscard]] std::vector<VhdlHirTypeValidationIssue>
validate_vhdl_hir_types(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture);

} // namespace fsim::elaboration

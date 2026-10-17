// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/vhdl_hir.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

struct VhdlHirTypeValidationIssue {
    std::string code;
    std::string message;
    semantic::SourceSpanId source;
};

[[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
resolve_vhdl_user_attribute_subtype(
    const semantic::SpecializedHirUnit& specialization,
    std::string_view nominal_type,
    semantic::ScopeId use_scope);

/// Whether a VHDL expression is locally static (IEEE 1076-2019 9.4.2). The
/// type validator owns this rule; other elaboration checks defer to it.
[[nodiscard]] bool vhdl_locally_static_expression(
    const semantic::SpecializedHirUnit& specialization,
    semantic::ExpressionId expression);

[[nodiscard]] std::vector<VhdlHirTypeValidationIssue>
validate_vhdl_hir_types(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture);

} // namespace fsim::elaboration

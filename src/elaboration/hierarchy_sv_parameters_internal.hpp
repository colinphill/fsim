// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design_core.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <string>
#include <string_view>

namespace fsim::elaboration::hierarchy_sv_parameters_detail {

[[nodiscard]] const char* systemverilog_parameter_diagnostic_code(
    semantic::SpecializedHirAssociationDiagnostic diagnostic);


[[nodiscard]] std::string compiled_systemverilog_integral_identity(
    const semantic::sv::Declaration& declaration,
    std::string_view display);

// Whether a parameter actual cannot be assigned to its formal (IEEE
// 1800-2017 6.16): a `string` formal needs a string value, and a value of
// type string needs a `string` formal. A string literal is integral (5.9),
// so it suits either.
[[nodiscard]] bool systemverilog_string_actual_incompatible(
    const semantic::SpecializedHirUnit& actual_scope,
    const semantic::sv::Declaration& formal,
    semantic::ExpressionId actual);

[[nodiscard]] bool compiled_systemverilog_string_declaration(
    const semantic::sv::Declaration& declaration) noexcept;

[[nodiscard]] std::string compiled_systemverilog_type_identity(
    const semantic::sv::TypeReference& type);

} // namespace fsim::elaboration::hierarchy_sv_parameters_detail

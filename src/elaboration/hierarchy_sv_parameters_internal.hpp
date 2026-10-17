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

// Whether a parameter actual cannot be assigned to its formal: an explicit
// `string` formal needs a string actual, and a string-valued actual (a string
// literal is integral, IEEE 1800-2017 5.9) suits a packed formal or an
// untyped one, which takes the type of its final value (6.20.2).
[[nodiscard]] bool systemverilog_string_actual_incompatible(
    const semantic::sv::Declaration& formal, bool actual_string) noexcept;

[[nodiscard]] bool compiled_systemverilog_string_declaration(
    const semantic::sv::Declaration& declaration) noexcept;

[[nodiscard]] std::string compiled_systemverilog_type_identity(
    const semantic::sv::TypeReference& type);

} // namespace fsim::elaboration::hierarchy_sv_parameters_detail

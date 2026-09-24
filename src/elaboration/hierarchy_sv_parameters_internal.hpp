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

[[nodiscard]] frontend::SystemVerilogScalarKind
compiled_systemverilog_scalar_kind(std::string_view spelling) noexcept;

[[nodiscard]] std::string compiled_systemverilog_integral_identity(
    const semantic::sv::Declaration& declaration,
    std::string_view display);

[[nodiscard]] bool compiled_systemverilog_string_declaration(
    const semantic::sv::Declaration& declaration) noexcept;

[[nodiscard]] std::string compiled_systemverilog_type_identity(
    const semantic::sv::TypeReference& type);

} // namespace fsim::elaboration::hierarchy_sv_parameters_detail

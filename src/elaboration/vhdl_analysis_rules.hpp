// SPDX-License-Identifier: Apache-2.0
#pragma once

// Compile-time VHDL analysis rules (gate BD D7, IEEE 1076-2019 9-11).
//
// Each group checks one architecture as analyzed: the specialization has no
// generic actuals and nothing the architecture instantiates is elaborated.
// A rule reports a violation only when it is decided by the unit alone:
// never from a generic's value (not even its default), a generic type, an
// actual, or the bounds of an unconstrained interface object (6.5.6, 14.3).
// Those decisions belong to elaboration, which keeps its own checks.
//
// Groups are called by validate_vhdl_architecture_analysis after the
// Validator (vhdl_hir_type_validation.cpp).

#include "vhdl_hir_type_validation.hpp"

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/vhdl_hir.hpp"

#include <vector>

namespace fsim::elaboration {

using VhdlAnalysisRuleGroup = std::vector<VhdlHirTypeValidationIssue> (*)(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture);

/// Case choices (10.9: locally static choices, coverage, duplicates),
/// aggregate choices and array lengths decided by locally static values.
[[nodiscard]] std::vector<VhdlHirTypeValidationIssue> check_vhdl_choice_rules(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture);

/// Physical types and literals (5.2.4), and names that must denote a
/// particular kind of entity (8, 12.3).
[[nodiscard]] std::vector<VhdlHirTypeValidationIssue>
check_vhdl_physical_name_rules(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture);

/// Subprograms, processes and their statements (4, 10, 11.3).
[[nodiscard]] std::vector<VhdlHirTypeValidationIssue>
check_vhdl_subprogram_rules(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture);

} // namespace fsim::elaboration

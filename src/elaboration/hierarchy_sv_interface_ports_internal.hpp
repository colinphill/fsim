// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <optional>

namespace fsim::elaboration::hierarchy_sv_interface_ports_detail {

[[nodiscard]] bool compiled_systemverilog_callable_profile_matches(
    const semantic::CompiledDesign& compiled,
    const std::optional<semantic::SpecializedHirUnit>& expected_specialization,
    const semantic::SpecializedHirUnit& actual_specialization,
    const semantic::sv::Declaration& expected,
    const semantic::sv::Declaration& actual);

} // namespace fsim::elaboration::hierarchy_sv_interface_ports_detail

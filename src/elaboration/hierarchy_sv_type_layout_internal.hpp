// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstddef>
#include <optional>

namespace fsim::elaboration::hierarchy_sv_type_layout_detail {

[[nodiscard]] std::optional<std::size_t> systemverilog_declaration_width(
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::sv::Declaration& declaration);

[[nodiscard]] std::optional<runtime::simir::ContainerType>
systemverilog_container_type(
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::sv::TypeReference& reference);

} // namespace fsim::elaboration::hierarchy_sv_type_layout_detail

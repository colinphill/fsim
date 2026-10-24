// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fsim::elaboration::hierarchy_sv_type_layout_detail {

// A packed range bound: a self-determined constant expression whose sized
// operands keep their widths, or the width-free value when that fails.
[[nodiscard]] std::optional<std::int64_t> sized_range_bound(
    const semantic::SpecializedHirUnit& specialization,
    semantic::ExpressionId expression);

[[nodiscard]] std::optional<std::size_t> systemverilog_declaration_width(
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::sv::Declaration& declaration);

// A tagged union with a member that has no packed form (a real, a string, an
// unpacked array or aggregate) is an unpacked aggregate of its tag and its
// members; that representation is not observable (IEEE 1800-2023 7.3.2).
[[nodiscard]] bool systemverilog_tagged_union_aggregate(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeDefinition& type);

// The name of the hidden tag member that leads the members of such a union.
inline constexpr std::string_view systemverilog_tagged_union_tag_member {
    "@tag"
};

[[nodiscard]] std::optional<runtime::simir::ContainerType>
systemverilog_container_type(
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::sv::TypeReference& reference);

// The constant value of a container declaration's initializer (IEEE
// 1800-2017 10.9, 10.10, 7.9.11); `element_type` is the declared element.
[[nodiscard]] std::optional<runtime::simir::ContainerValue>
systemverilog_constant_container_initializer(
    const semantic::SpecializedHirUnit& specialization,
    const runtime::simir::ContainerType& type,
    semantic::ExpressionId initializer,
    const semantic::sv::TypeReference* element_type);

} // namespace fsim::elaboration::hierarchy_sv_type_layout_detail

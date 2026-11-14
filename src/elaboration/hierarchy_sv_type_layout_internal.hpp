// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/runtime/vpi_types.hpp"
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

// Whether a container of `type` can be stored: a one-dimensional packed
// array beyond the dense storage budget becomes sparse (`type.sparse`), so
// it stores only its written elements, as a design variable does.
[[nodiscard]] bool materialize_container_storage(
    runtime::simir::ContainerType& type);

// The constant value of a container declaration's initializer (IEEE
// 1800-2017 10.9, 10.10, 7.9.11); `element_type` is the declared element.
[[nodiscard]] std::optional<runtime::simir::ContainerValue>
systemverilog_constant_container_initializer(
    const semantic::SpecializedHirUnit& specialization,
    const runtime::simir::ContainerType& type,
    semantic::ExpressionId initializer,
    const semantic::sv::TypeReference* element_type);

// The declared type of a SystemVerilog data type as VPI describes it: a
// structure, union, enumeration, packed array of them, string or integral
// vector, following typedefs and type parameters (IEEE 1800-2023 37.17,
// 37.18, 37.26). Nullopt for any other type or a bound that is not constant.
[[nodiscard]] std::optional<runtime::SystemVerilogVpiTypeDescriptor>
systemverilog_vpi_declared_type(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeReference& reference, semantic::ScopeId scope);

// The typespec of a SystemVerilog data type, one node per typedef level
// (IEEE 1800-2023 37.25). Nullopt for a type VPI gives no typespec here,
// such as a virtual interface, or a bound that is not constant.
[[nodiscard]] std::optional<runtime::SystemVerilogVpiTypespec>
systemverilog_vpi_typespec(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeReference& reference, semantic::ScopeId scope);

// The fixed unpacked array type of a named-event array declared with
// `reference` (IEEE 1800-2023 7.4, 15.5), following a typedef of the array
// and of its element (6.18); nullopt for any other type.
[[nodiscard]] std::optional<semantic::sv::TypeReference>
systemverilog_event_array_type(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeReference& reference, semantic::ScopeId scope);

} // namespace fsim::elaboration::hierarchy_sv_type_layout_detail

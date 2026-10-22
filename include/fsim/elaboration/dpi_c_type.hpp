// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fsim::elaboration {

// The C side of a DPI-C formal or result type spelled with SystemVerilog
// built-in type keywords and decimal packed ranges.
struct DpiCType {
    std::size_t width { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    bool signed_value { };
    bool string { };
    // The C type of the value (IEEE 1800-2023 Annex H.7.4): c s i l
    // for byte, shortint, int and longint (upper case when unsigned),
    // b svBit, g svLogic, d double, f float, p chandle, z string, B an
    // svBitVecVal vector, V an svLogicVecVal vector.
    char code { };
    frontend::SystemVerilogScalarKind scalar {
        frontend::SystemVerilogScalarKind::None
    };
};

[[nodiscard]] std::optional<DpiCType> dpi_c_type(
    std::span<const semantic::sv::SourceToken> tokens);

// What mapping a named or resolved type needs beyond built-in keywords:
// the type a typedef name denotes in the DPI declaration's scope, a type's
// definition, and a packed bound's constant value.
struct DpiTypeContext {
    std::function<std::optional<semantic::sv::TypeReference>(
        std::string_view)>
        named_type;
    std::function<const semantic::sv::TypeDefinition*(semantic::TypeId)>
        definition;
    std::function<std::optional<std::int64_t>(semantic::ExpressionId)>
        evaluate;
};

// A formal or result type that may name a typedef: one that resolves to a
// built-in type maps like it, and a packed structure, union or array maps
// to a bit or logic vector (IEEE 1800-2023 35.5.6, H.7.6).
[[nodiscard]] std::optional<DpiCType> dpi_c_type(
    std::span<const semantic::sv::SourceToken> tokens,
    const DpiTypeContext& context);
[[nodiscard]] std::optional<DpiCType> dpi_c_type(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context);

// The type context of declarations in `scope` of a whole design's HIR: a
// typedef of that scope, or else the only typedef of that name.
[[nodiscard]] DpiTypeContext dpi_hir_type_context(
    const semantic::sv::Hir& hir, semantic::ScopeId scope);

// A C header declaring every validated DPI-C import and export with its
// Annex H C prototype, like other simulators' DPI header generation. A
// declaration whose types this mapping cannot spell is listed in a comment.
[[nodiscard]] std::string systemverilog_dpi_header(
    const semantic::sv::Hir& hir);

}  // namespace fsim::elaboration

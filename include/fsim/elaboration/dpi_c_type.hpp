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
#include <utility>
#include <vector>

namespace fsim::elaboration {

struct DpiCMember;

// The C side of a DPI-C formal or result type spelled with SystemVerilog
// built-in type keywords and constant packed ranges.
struct DpiCType {
    std::size_t width { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    bool signed_value { };
    bool string { };
    // The C type of the value (IEEE 1800-2023 Annex H.7.4): c s i l
    // for byte, shortint, int and longint (upper case when unsigned),
    // b svBit, g svLogic, d double, f float, p chandle, z string, B an
    // svBitVecVal vector, V an svLogicVecVal vector, R an unpacked
    // structure (H.7.8).
    char code { };
    frontend::SystemVerilogScalarKind scalar {
        frontend::SystemVerilogScalarKind::None
    };
    // `bit []` or `logic []`: the packed dimension is open and `width` is
    // 0 until an actual gives it (IEEE 1800-2023 35.5.6.1, H.12).
    bool open_packed { };
    // An unpacked structure: `width` is the bits of all its members as
    // one stream, the first member first.
    std::vector<DpiCMember> members;
    // An unpacked structure: the name of its C typedef in a generated
    // header, from DpiTypeContext::struct_tag; empty when there is none.
    std::string tag;
};

// One member of an unpacked structure; its unpacked dimensions, as (left,
// right) pairs outermost first, are those of the member declaration.
struct DpiCMember {
    std::string name;
    DpiCType type;
    std::vector<std::pair<std::int64_t, std::int64_t>> dimensions;
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
    // The value of a named constant (a parameter or local parameter) of
    // the declaration's scope, for ranges spelled in tokens.
    std::function<std::optional<std::int64_t>(std::string_view)> constant;
    // Whether a name denotes a class.
    std::function<bool(std::string_view)> class_named;
    // The C typedef name of an unpacked structure type referenced with
    // `spelling`, unique per SystemVerilog typedef: typedefs of equal
    // layout share one structure type but keep their own C names.
    std::function<std::string(semantic::TypeId, std::string_view spelling)>
        struct_tag;
};

// The value of a constant integral expression spelled in tokens: decimal
// and based literals, named constants, parentheses, unary + - ~, the
// binary operators + - * / % << >> & | ^ ** and $clog2.
[[nodiscard]] std::optional<std::int64_t> dpi_constant_expression(
    std::span<const semantic::sv::SourceToken> tokens,
    const DpiTypeContext& context);

// One unpacked dimension of a formal: `[]` is open, `[N]` is [0:N-1].
struct DpiDimension {
    std::int64_t left { };
    std::int64_t right { };
    bool open { };
};

// The unpacked dimensions a formal declares after its name, outermost first;
// nullopt when one is not a constant range or `[]`.
[[nodiscard]] std::optional<std::vector<DpiDimension>> dpi_unpacked_dimensions(
    std::span<const semantic::sv::SourceToken> tokens,
    const DpiTypeContext& context);

// A type the DPI forbids (IEEE 1800-2023 35.5.5, 35.5.6).
enum class DpiTypeIssue : std::uint8_t {
    class_handle,
    unpacked_union,
    dynamic_array,
    not_small_value,
};

// Why a formal or result type that maps to no C type is illegal, when the
// type is one of the forbidden kinds.
[[nodiscard]] std::optional<DpiTypeIssue> dpi_type_issue(
    std::span<const semantic::sv::SourceToken> tokens,
    const DpiTypeContext& context);

// The signature text of a C type, as the DPI call service parses it: its
// code, a vector's width, or `R(` the members `;`-separated, each followed
// by `/left/right` per unpacked dimension, `)`.
[[nodiscard]] std::string dpi_c_type_signature(const DpiCType& type);

// The type's bit width as one stream: a vector's or atom's bits, or a
// structure's members' (each times its elements).
[[nodiscard]] std::uint64_t dpi_c_type_stream_bits(const DpiCType& type);

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
// Annex H C prototype, like other simulators' DPI header generation. Each
// unpacked structure a formal uses, directly or nested, has a C typedef
// with its members in declaration order (IEEE 1800-2023 H.7.8). A
// declaration whose types this mapping cannot spell is listed in a comment.
[[nodiscard]] std::string systemverilog_dpi_header(
    const semantic::sv::Hir& hir);

}  // namespace fsim::elaboration

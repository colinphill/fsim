// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design_core.hpp"
#include "fsim/frontend/systemverilog_scalar_folding.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::elaboration {

inline constexpr std::uint32_t hir_systemverilog_maximum_constant_width
    = 16U * 1024U * 1024U;

/// An owning, syntax-free SystemVerilog integral constant used while a HIR
/// specialization is being constructed.  The packed value is authoritative;
/// the remaining fields retain the IEEE 1800 self-determined expression
/// profile needed by parent expressions.
struct HirSystemVerilogConstant {
    runtime::PackedLogic4 packed;
    std::uint32_t width { 1U };
    bool signed_value { };
    bool unsized { };
    bool unbounded { };
    frontend::ValueDomain domain { frontend::ValueDomain::Logic4 };
    std::string nominal_type;
    std::optional<semantic::sv::PackedRange> packed_range;

    [[nodiscard]] bool known() const noexcept;
    [[nodiscard]] std::optional<std::int64_t>
    integer_value() const noexcept;
    [[nodiscard]] std::string display() const;
    [[nodiscard]] std::string canonical() const;
};

[[nodiscard]] bool hir_systemverilog_explicit_integral_type(
    const semantic::sv::TypeReference& type) noexcept;

/// Decode the deterministic syntax-free identity used by specialization
/// keys.  This is used for second-phase conversion after all sibling actuals
/// are available in the child specialization context.
[[nodiscard]] std::optional<HirSystemVerilogConstant>
decode_hir_systemverilog_constant(std::string_view identity);

/// Decode the deterministic scalar identity used by specialization keys.
[[nodiscard]] std::optional<frontend::SystemVerilogScalarConstant>
decode_hir_systemverilog_scalar_constant(std::string_view identity);

/// Evaluate an integral expression exclusively from specialized HIR.  No
/// parser or syntax object is consulted or retained.
[[nodiscard]] std::optional<HirSystemVerilogConstant>
evaluate_hir_systemverilog_constant(
    const semantic::SpecializedHirUnit& specialization,
    semantic::ExpressionId expression,
    std::string& error);

/// Return true when an expression contains real-family, time, or chandle HIR
/// that requires the scalar evaluator instead of the packed evaluator.
[[nodiscard]] bool hir_systemverilog_scalar_expression_applicable(
    const semantic::SpecializedHirUnit& specialization,
    semantic::ExpressionId expression);

/// Evaluate a real-family, time, chandle, or scalar-dependent expression
/// exclusively from compiled HIR.
[[nodiscard]] std::optional<frontend::SystemVerilogScalarConstant>
evaluate_hir_systemverilog_scalar_constant(
    const semantic::SpecializedHirUnit& specialization,
    semantic::ExpressionId expression,
    std::string& error);

/// Evaluate a declaration with source-order checking for dependencies.
[[nodiscard]] std::optional<frontend::SystemVerilogScalarConstant>
evaluate_hir_systemverilog_scalar_declaration(
    const semantic::SpecializedHirUnit& specialization,
    semantic::DeclarationId declaration,
    std::string& error);

/// Apply a declaration's resolved packed profile to a self-determined value.
[[nodiscard]] std::optional<HirSystemVerilogConstant>
convert_hir_systemverilog_constant(
    HirSystemVerilogConstant value,
    const semantic::sv::TypeReference& type,
    std::string& error,
    const semantic::SpecializedHirUnit* specialization = nullptr);

} // namespace fsim::elaboration

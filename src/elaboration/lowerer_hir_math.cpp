// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <charconv>
#include <limits>

namespace fsim::elaboration {
using namespace runtime::simir;
namespace {

    [[nodiscard]] std::optional<runtime::SystemVerilogMathFunction>
    systemverilog_math_function(const std::string_view name) noexcept
    {
        using Function = runtime::SystemVerilogMathFunction;
        if (name == "$rtoi")
            return Function::Rtoi;
        if (name == "$itor")
            return Function::Itor;
        if (name == "$bitstoreal")
            return Function::BitsToReal;
        if (name == "$realtobits")
            return Function::RealToBits;
        if (name == "$bitstoshortreal")
            return Function::BitsToShortReal;
        if (name == "$shortrealtobits")
            return Function::ShortRealToBits;
        if (name == "$ln")
            return Function::Ln;
        if (name == "$log10")
            return Function::Log10;
        if (name == "$exp")
            return Function::Exp;
        if (name == "$sqrt")
            return Function::Sqrt;
        if (name == "$pow")
            return Function::Pow;
        if (name == "$floor")
            return Function::Floor;
        if (name == "$ceil")
            return Function::Ceil;
        if (name == "$sin")
            return Function::Sin;
        if (name == "$cos")
            return Function::Cos;
        if (name == "$tan")
            return Function::Tan;
        if (name == "$asin")
            return Function::Asin;
        if (name == "$acos")
            return Function::Acos;
        if (name == "$atan")
            return Function::Atan;
        if (name == "$atan2")
            return Function::Atan2;
        if (name == "$hypot")
            return Function::Hypot;
        if (name == "$sinh")
            return Function::Sinh;
        if (name == "$cosh")
            return Function::Cosh;
        if (name == "$tanh")
            return Function::Tanh;
        if (name == "$asinh")
            return Function::Asinh;
        if (name == "$acosh")
            return Function::Acosh;
        if (name == "$atanh")
            return Function::Atanh;
        if (name == "$time")
            return Function::Time;
        if (name == "$stime")
            return Function::Stime;
        if (name == "$realtime")
            return Function::Realtime;
        return std::nullopt;
    }

    [[nodiscard]] bool binary_math_function(
        const runtime::SystemVerilogMathFunction function) noexcept
    {
        using Function = runtime::SystemVerilogMathFunction;
        return function == Function::Pow
            || function == Function::Atan2
            || function == Function::Hypot;
    }

    [[nodiscard]] bool time_math_function(
        const runtime::SystemVerilogMathFunction function) noexcept
    {
        using Function = runtime::SystemVerilogMathFunction;
        return function == Function::Time
            || function == Function::Stime
            || function == Function::Realtime;
    }

    [[nodiscard]] std::optional<std::uint64_t>
    systemverilog_time_scale_femtoseconds(
        const std::string_view spelling) noexcept
    {
        const auto unit_begin = spelling.find_first_not_of("0123456789");
        const auto magnitude_text = unit_begin == std::string_view::npos
            ? spelling
            : spelling.substr(0U, unit_begin);
        const auto unit = unit_begin == std::string_view::npos
            ? std::string_view { }
            : spelling.substr(unit_begin);
        std::uint64_t magnitude = 1U;
        if (!magnitude_text.empty()) {
            const auto [end, error] = std::from_chars(
                magnitude_text.data(),
                magnitude_text.data() + magnitude_text.size(), magnitude);
            if (error != std::errc { }
                || end != magnitude_text.data() + magnitude_text.size()) {
                return std::nullopt;
            }
        }
        const auto factor = unit == "s" ? 1'000'000'000'000'000ULL
            : unit == "ms"              ? 1'000'000'000'000ULL
            : unit == "us"              ? 1'000'000'000ULL
            : unit == "ns"              ? 1'000'000ULL
            : unit == "ps"              ? 1'000ULL
            : unit == "fs"              ? 1ULL
                                        : 0ULL;
        if (factor == 0U
            || magnitude
                > std::numeric_limits<std::uint64_t>::max() / factor) {
            return std::nullopt;
        }
        return magnitude * factor;
    }

} // namespace

std::optional<runtime::SystemVerilogMathFunction>
Lowerer::hir_systemverilog_math_function(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call) {
        return std::nullopt;
    }
    return systemverilog_math_function(
        expression->systemverilog->text);
}

std::optional<RegisterId> Lowerer::lower_hir_systemverilog_math_call(
    const semantic::ExpressionId expression_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto function = hir_systemverilog_math_function(expression_id);
    if (!expression || expression->systemverilog == nullptr || !function) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    const auto arity = time_math_function(*function)
        ? 0U
        : binary_math_function(*function) ? 2U
                                          : 1U;
    if (call.operands.size() != arity) {
        report(
            time_math_function(*function)
                ? "FSIM-ELAB-SVTIME-003"
                : "FSIM-ELAB-SVMATH-001",
            time_math_function(*function)
                ? call.text + " requires SystemVerilog and no arguments"
                : call.text + " requires " + std::to_string(arity)
                    + (arity == 1U
                            ? " numeric operand"
                            : " numeric operands"),
            hir_source_span(call.source));
        return std::nullopt;
    }
    if (time_math_function(*function)) {
        const auto unit = specialized_hir_unit_->design().find_unit(
            specialized_hir_unit_->unit());
        const auto* const context = unit && unit->systemverilog != nullptr
            ? &unit->systemverilog->compilation
            : nullptr;
        const auto time_unit = context == nullptr
            ? std::optional<std::uint64_t> { }
            : systemverilog_time_scale_femtoseconds(context->time_unit);
        const auto time_precision = context == nullptr
            ? std::optional<std::uint64_t> { }
            : systemverilog_time_scale_femtoseconds(
                  context->time_precision);
        const auto result_width
            = *function == runtime::SystemVerilogMathFunction::Stime
            ? 32U
            : 64U;
        const auto destination = allocate_register(
            result_width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogMath {
            .function = *function,
            .destination = destination,
            .time_unit_femtoseconds
            = time_unit.value_or(1U),
            .time_precision_femtoseconds
            = time_precision.value_or(1U),
        });
        return destination;
    }

    struct Operand {
        RegisterId id { };
        std::uint32_t width { };
        frontend::SystemVerilogScalarKind kind {
            frontend::SystemVerilogScalarKind::None
        };
        bool signed_value { };
    };
    const auto lower_operand = [&](const semantic::ExpressionId operand_id)
        -> std::optional<Operand> {
        const auto kind = hir_systemverilog_scalar_kind(operand_id);
        const auto width = kind
                == frontend::SystemVerilogScalarKind::ShortReal
            ? std::optional<std::size_t> { 32U }
            : kind != frontend::SystemVerilogScalarKind::None
                && kind != frontend::SystemVerilogScalarKind::Chandle
            ? std::optional<std::size_t> { 64U }
            : hir_expression_width(operand_id, hir_process_scope_);
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()
            || kind == frontend::SystemVerilogScalarKind::Chandle) {
            report(
                "FSIM-ELAB-SVMATH-002",
                call.text
                    + " requires a statically sized packed integral, real, "
                      "shortreal, realtime, or time operand",
                hir_source_span(call.source));
            return std::nullopt;
        }
        const auto lowered = lower_hir_expression(
            operand_id, *width, kind);
        if (!lowered) {
            return std::nullopt;
        }
        return Operand {
            *lowered,
            static_cast<std::uint32_t>(*width),
            kind,
            hir_expression_signed(operand_id),
        };
    };

    const auto first = lower_operand(call.operands.front());
    const auto second = arity == 2U
        ? lower_operand(call.operands[1])
        : std::optional<Operand> { };
    if (!first || (arity == 2U && !second)) {
        return std::nullopt;
    }

    using Function = runtime::SystemVerilogMathFunction;
    const bool first_real = first->kind
            == frontend::SystemVerilogScalarKind::ShortReal
        || first->kind == frontend::SystemVerilogScalarKind::Real
        || first->kind == frontend::SystemVerilogScalarKind::Realtime;
    const bool valid_profile = *function == Function::Rtoi
        ? first_real
        : *function == Function::Itor
        ? first->kind == frontend::SystemVerilogScalarKind::None
            && first->width == 32U
        : *function == Function::BitsToReal
        ? first->kind == frontend::SystemVerilogScalarKind::None
            && first->width == 64U
        : *function == Function::RealToBits
        ? first->kind == frontend::SystemVerilogScalarKind::Real
            || first->kind == frontend::SystemVerilogScalarKind::Realtime
        : *function == Function::BitsToShortReal
        ? first->kind == frontend::SystemVerilogScalarKind::None
            && first->width == 32U
        : *function == Function::ShortRealToBits
        ? first->kind == frontend::SystemVerilogScalarKind::ShortReal
        : true;
    if (!valid_profile) {
        report(
            "FSIM-ELAB-SVMATH-002",
            call.text
                + " operand type does not match its IEEE 1800 scalar profile",
            hir_source_span(call.source));
        return std::nullopt;
    }

    const auto result_width = *function == Function::Rtoi
            || *function == Function::BitsToShortReal
            || *function == Function::ShortRealToBits
        ? 32U
        : 64U;
    const auto destination = allocate_register(
        result_width, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(SystemVerilogMath {
        *function,
        destination,
        first->id,
        second ? second->id : 0U,
        first->width,
        second ? second->width : 0U,
        first->kind,
        second ? second->kind
               : frontend::SystemVerilogScalarKind::None,
        first->signed_value,
        second ? second->signed_value : false,
    });
    return destination;
}

} // namespace fsim::elaboration

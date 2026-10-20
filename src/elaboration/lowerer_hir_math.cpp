// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
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
        if (name == "$time" || name == "$simtime")
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

std::uint64_t Lowerer::hir_time_unit_femtoseconds() const
{
    // A unit without a timescale counts in the project resolution, which
    // the maximum value names.
    const auto unit = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->design().find_unit(
              specialized_hir_unit_->unit())
        : std::nullopt;
    const auto* const context = unit && unit->systemverilog != nullptr
        ? &unit->systemverilog->compilation
        : nullptr;
    const auto scale = context
        ? systemverilog_unit_time_scale_femtoseconds(context->time_unit)
        : std::nullopt;
    return scale.value_or(std::numeric_limits<std::uint64_t>::max());
}

std::optional<std::uint64_t> systemverilog_unit_time_scale_femtoseconds(
    const std::string_view spelling) noexcept
{
    return systemverilog_time_scale_femtoseconds(spelling);
}

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

const semantic::sv::Unit* Lowerer::hir_time_scope_unit(
    const semantic::ScopeId scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return nullptr;
    }
    const auto& design = specialized_hir_unit_->design();
    const auto current = design.find_unit(specialized_hir_unit_->unit());
    const auto* const fallback
        = current ? current->systemverilog : nullptr;
    const auto& scopes = design.semantics.scopes();
    if (!scope.valid() || scope.value() >= scopes.size()) {
        return fallback;
    }
    const auto owner = design.find_unit(scopes[scope.value()].unit);
    return owner && owner->systemverilog != nullptr
            && owner->systemverilog->kind == semantic::sv::UnitKind::package
        ? owner->systemverilog
        : fallback;
}

std::optional<std::string> Lowerer::hir_vhdl_math_real_function(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::call
        || expression->vhdl->operands.empty()
        || expression->vhdl->operands.size() > 2U
        || expression->vhdl->text.starts_with('@')
        || expression->vhdl->text.starts_with('\'')) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    std::string name = source.text;
    if (const auto separator = name.find_last_of('.');
        separator != std::string::npos) {
        name.erase(0U, separator + 1U);
    }
    std::ranges::transform(name, name.begin(), [](const char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    static constexpr std::array<std::string_view, 20> unary {
        "ceil", "floor", "sqrt", "exp", "log", "log2", "log10", "sin", "cos",
        "tan", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
        "arcsinh", "arccosh", "arctanh", "cbrt"
    };
    if (std::ranges::find(unary, name) == unary.end()
        || (source.operands.size() == 2U && name != "log"
            && name != "arctan")) {
        return std::nullopt;
    }
    // A user function of that name hides nothing in IEEE.MATH_REAL unless
    // it is that package's own declaration.
    if (source.referenced_name) {
        std::vector<semantic::DeclarationId> candidates
            = source.referenced_name->overloads;
        if (source.referenced_name->selected) {
            candidates.push_back(*source.referenced_name->selected);
        }
        for (const auto candidate : candidates) {
            const auto declaration
                = specialized_hir_unit_->find_declaration(candidate);
            if (!declaration || declaration->vhdl == nullptr
                || !specialized_hir_unit_->design().vhdl_scope_in_package(
                    declaration->vhdl->scope, "ieee", "math_real")) {
                return std::nullopt;
            }
        }
    }
    if (!std::ranges::all_of(source.operands,
            [&](const semantic::ExpressionId operand) {
                return hir_vhdl_expression_is_real(operand);
            })) {
        return std::nullopt;
    }
    return name;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_math_real_call(
    const semantic::ExpressionId expression_id)
{
    const auto name = hir_vhdl_math_real_function(expression_id);
    if (!name) {
        return std::nullopt;
    }
    using Function = runtime::SystemVerilogMathFunction;
    using ScalarKind = frontend::SystemVerilogScalarKind;
    const auto& operands
        = specialized_hir_unit_->find_expression(expression_id)->vhdl->operands;
    std::vector<RegisterId> values;
    for (const auto operand : operands) {
        const auto value = lower_hir_expression(operand, 64U, ScalarKind::Real);
        if (!value) {
            return std::nullopt;
        }
        values.push_back(*value);
    }
    const auto math = [&](const Function function, const RegisterId first,
                          const std::optional<RegisterId> second
                          = std::nullopt) {
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogMath {
            function, destination, first, second.value_or(0U), 64U,
            second ? 64U : 0U, ScalarKind::Real,
            second ? ScalarKind::Real : ScalarKind::None, false, false });
        return destination;
    };
    const auto binary = [&](const runtime::SystemVerilogScalarBinaryOperator
                                operation,
                            const RegisterId left, const RegisterId right,
                            const ScalarKind result = ScalarKind::Real) {
        const auto destination = allocate_register(
            result == ScalarKind::Real ? 64U : 1U,
            result == ScalarKind::Real ? frontend::ValueDomain::Bit2
                                       : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            operation, destination, left, right, ScalarKind::Real,
            ScalarKind::Real, result });
        return destination;
    };
    const auto real = [&](const double value) {
        return load_constant(
            unsigned_value(std::bit_cast<std::uint64_t>(value), 64U),
            frontend::ValueDomain::Bit2);
    };
    static constexpr std::array<std::pair<std::string_view, Function>, 17>
        direct {
            std::pair { std::string_view { "ceil" }, Function::Ceil },
            std::pair { std::string_view { "floor" }, Function::Floor },
            std::pair { std::string_view { "sqrt" }, Function::Sqrt },
            std::pair { std::string_view { "exp" }, Function::Exp },
            std::pair { std::string_view { "log" }, Function::Ln },
            std::pair { std::string_view { "log10" }, Function::Log10 },
            std::pair { std::string_view { "sin" }, Function::Sin },
            std::pair { std::string_view { "cos" }, Function::Cos },
            std::pair { std::string_view { "tan" }, Function::Tan },
            std::pair { std::string_view { "arcsin" }, Function::Asin },
            std::pair { std::string_view { "arccos" }, Function::Acos },
            std::pair { std::string_view { "arctan" }, Function::Atan },
            std::pair { std::string_view { "sinh" }, Function::Sinh },
            std::pair { std::string_view { "cosh" }, Function::Cosh },
            std::pair { std::string_view { "tanh" }, Function::Tanh },
            std::pair { std::string_view { "arcsinh" }, Function::Asinh },
            std::pair { std::string_view { "arccosh" }, Function::Acosh },
        };
    using Operator = runtime::SystemVerilogScalarBinaryOperator;
    if (*name == "arctanh") {
        return math(Function::Atanh, values.front());
    }
    // ARCTAN(Y, X) is the angle of the point (X, Y).
    if (*name == "arctan" && values.size() == 2U) {
        return math(Function::Atan2, values[0], values[1]);
    }
    // LOG(X, BASE) is LOG(X) / LOG(BASE).
    if (*name == "log" && values.size() == 2U) {
        return binary(Operator::Divide, math(Function::Ln, values[0]),
            math(Function::Ln, values[1]));
    }
    // CBRT(X) is X ** (1/3), keeping the sign of X.
    if (*name == "cbrt") {
        const auto zero = real(0.0);
        const auto negative = binary(
            Operator::Less, values.front(), zero, ScalarKind::None);
        const auto negated = binary(Operator::Subtract, zero, values.front());
        const auto magnitude = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ConditionalSelect {
            magnitude, negative, negated, values.front() });
        const auto root = math(Function::Pow, magnitude, real(1.0 / 3.0));
        const auto negative_root = binary(Operator::Subtract, zero, root);
        const auto result = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ConditionalSelect {
            result, negative, negative_root, root });
        return result;
    }
    // LOG2(X) is LN(X) / LN(2), exact at the powers of two.
    if (*name == "log2") {
        const auto ratio = binary(Operator::Divide,
            math(Function::Ln, values.front()), real(std::log(2.0)));
        const auto nearest = math(
            Function::Floor, binary(Operator::Add, ratio, real(0.5)));
        const auto exact = binary(Operator::Equal,
            math(Function::Pow, real(2.0), nearest), values.front(),
            ScalarKind::None);
        const auto result = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ConditionalSelect {
            result, exact, nearest, ratio });
        return result;
    }
    const auto function = std::ranges::find(
        direct, *name, &std::pair<std::string_view, Function>::first);
    if (function == direct.end()) {
        return std::nullopt;
    }
    return math(function->second, values.front());
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
        const auto* const time_unit_owner = hir_time_scope_unit(call.scope);
        const auto* const context = time_unit_owner != nullptr
            ? &time_unit_owner->compilation
            : nullptr;
        // $simtime counts simulation-precision ticks: the runtime's zero
        // unit is the project resolution.
        const auto time_unit = context == nullptr || call.text == "$simtime"
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
            // A unit without a timescale runs at the project resolution,
            // which the runtime substitutes for zero.
            .time_unit_femtoseconds
            = time_unit.value_or(0U),
            .time_precision_femtoseconds
            = time_precision.value_or(0U),
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
        auto kind = hir_systemverilog_scalar_kind(operand_id);
        // A real argument of $shortrealtobits converts to shortreal, as an
        // assignment would (IEEE 1800-2017 20.5, 6.12.1).
        if (*function == runtime::SystemVerilogMathFunction::ShortRealToBits
            && (kind == frontend::SystemVerilogScalarKind::Real
                || kind == frontend::SystemVerilogScalarKind::Realtime)) {
            kind = frontend::SystemVerilogScalarKind::ShortReal;
        }
        // Likewise a shortreal argument of $realtobits converts to real.
        if (*function == runtime::SystemVerilogMathFunction::RealToBits
            && kind == frontend::SystemVerilogScalarKind::ShortReal) {
            kind = frontend::SystemVerilogScalarKind::Real;
        }
        // The bit_val argument of $bitstoreal and $bitstoshortreal is
        // bit [63:0] and bit [31:0]; an integral argument converts to it.
        if (kind == frontend::SystemVerilogScalarKind::None
            && (*function == runtime::SystemVerilogMathFunction::BitsToReal
                || *function
                    == runtime::SystemVerilogMathFunction::BitsToShortReal)) {
            const auto width = *function
                    == runtime::SystemVerilogMathFunction::BitsToReal
                ? 64U
                : 32U;
            const auto lowered = lower_hir_expression(
                operand_id, width, kind);
            if (!lowered) {
                return std::nullopt;
            }
            return Operand { *lowered, width, kind, false };
        }
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
    const bool first_real = frontend::is_real_scalar_kind(first->kind);
    const bool valid_profile = *function == Function::Rtoi
        ? first_real
        // $itor converts an integral value of any width (20.5).
        : *function == Function::Itor
        ? first->kind == frontend::SystemVerilogScalarKind::None
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

RegisterId Lowerer::convert_hir_integral_to_real(const RegisterId value,
    const bool signed_value, const frontend::SystemVerilogScalarKind target)
{
    auto source = value;
    if (register_domain(value) != frontend::ValueDomain::Bit2) {
        source = allocate_register(
            register_width(value), frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ConvertToTwoState { source, value });
    }
    const auto real = allocate_register(64U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(SystemVerilogMath {
        runtime::SystemVerilogMathFunction::Itor,
        real,
        source,
        0U,
        static_cast<std::uint32_t>(register_width(source)),
        0U,
        frontend::SystemVerilogScalarKind::None,
        frontend::SystemVerilogScalarKind::None,
        signed_value,
        false,
    });
    if (target != frontend::SystemVerilogScalarKind::ShortReal) {
        return real;
    }
    const auto narrowed = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(SystemVerilogScalarBinary {
        runtime::SystemVerilogScalarBinaryOperator::Convert,
        narrowed,
        real,
        real,
        frontend::SystemVerilogScalarKind::Real,
        frontend::SystemVerilogScalarKind::Real,
        frontend::SystemVerilogScalarKind::ShortReal,
    });
    return narrowed;
}

RegisterId Lowerer::convert_hir_real_to_integral(const RegisterId value,
    const frontend::SystemVerilogScalarKind source, const std::size_t width)
{
    using ScalarKind = frontend::SystemVerilogScalarKind;
    using Operator = runtime::SystemVerilogScalarBinaryOperator;
    const auto convert64 = [&](const RegisterId real, const ScalarKind kind) {
        const auto integral
            = allocate_register(64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            Operator::Convert, integral, real, real, kind, kind,
            ScalarKind::None });
        return integral;
    };
    if (width <= 64U) {
        return convert64(value, source);
    }
    // A target wider than 64 bits takes the whole rounded value (IEEE
    // 1800-2017 6.12.2). Below 2**63 in magnitude the 64-bit conversion is
    // exact and sign-extends; beyond it a double is an exact integer that
    // splits into a high part (floor(r / 2**64)) and a low 64-bit word.
    auto real = value;
    if (source != ScalarKind::Real && source != ScalarKind::Realtime) {
        real = allocate_register(64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            Operator::Convert, real, value, value, source, source,
            ScalarKind::Real });
    }
    const auto real_constant = [&](const double number) {
        return load_constant(
            unsigned_value(std::bit_cast<std::uint64_t>(number), 64U),
            frontend::ValueDomain::Bit2);
    };
    const auto real_binary = [&](const Operator operation,
                                 const RegisterId left, const RegisterId right,
                                 const ScalarKind result) {
        const auto destination = allocate_register(
            result == ScalarKind::None ? 1U : 64U,
            frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(SystemVerilogScalarBinary {
            operation, destination, left, right, ScalarKind::Real,
            ScalarKind::Real, result });
        return destination;
    };
    const auto result = allocate_register(width, frontend::ValueDomain::Bit2);
    const auto narrow = resize_register(
        convert64(real, ScalarKind::Real), width, true);
    const auto above = real_binary(Operator::GreaterEqual, real,
        real_constant(9223372036854775808.0), ScalarKind::None);
    const auto below = real_binary(Operator::LessEqual, real,
        real_constant(-9223372036854775808.0), ScalarKind::None);
    const auto wide = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(
        Binary { BinaryOperator::bit_or, wide, above, below });
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch { wide, 0U, 0U });
    process_.operations.emplace_back(CopyRegister { result, narrow });
    const auto skip = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump { 0U });
    const auto split = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto two64 = real_constant(18446744073709551616.0);
    const auto quotient
        = real_binary(Operator::Divide, real, two64, ScalarKind::Real);
    const auto high_real = allocate_register(64U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(SystemVerilogMath {
        .function = runtime::SystemVerilogMathFunction::Floor,
        .destination = high_real,
        .first = quotient,
        .first_width = 64U,
        .first_kind = ScalarKind::Real,
    });
    const auto high_scaled
        = real_binary(Operator::Multiply, high_real, two64, ScalarKind::Real);
    const auto low_real = real_binary(
        Operator::Subtract, real, high_scaled, ScalarKind::Real);
    const auto low = convert64(low_real, ScalarKind::Real);
    const auto high = convert_hir_real_to_integral(
        high_real, ScalarKind::Real, width - 64U);
    const auto high_sized = register_width(high) == width - 64U
        ? high
        : resize_register(high, width - 64U, true);
    const auto joined = allocate_register(width, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Concatenate {
        joined, { high_sized, low }, static_cast<std::uint32_t>(width) });
    process_.operations.emplace_back(CopyRegister { result, joined });
    process_.operations[branch] = Branch { wide, split,
        static_cast<InstructionIndex>(branch + 1U) };
    process_.operations[skip]
        = Jump { static_cast<InstructionIndex>(process_.operations.size()) };
    return result;
}

std::optional<RegisterId> Lowerer::lower_hir_scalar_converted_value(
    const semantic::ExpressionId value, const std::size_t width,
    const frontend::SystemVerilogScalarKind target)
{
    const auto source = hir_systemverilog_scalar_kind(value);
    const bool real_target = frontend::is_real_scalar_kind(target);
    if (real_target && source == frontend::SystemVerilogScalarKind::None) {
        const auto lowered = lower_hir_expression(value,
            hir_expression_width(value, hir_process_scope_).value_or(32U));
        return lowered
            ? std::optional { convert_hir_integral_to_real(
                  *lowered, hir_expression_signed(value), target) }
            : std::nullopt;
    }
    if (!real_target && frontend::is_real_scalar_kind(source)) {
        const auto lowered = lower_hir_expression(value, 64U, source);
        if (!lowered) {
            return std::nullopt;
        }
        const auto integral = convert_hir_real_to_integral(
            *lowered, source, width);
        return register_width(integral) == width ? integral
                            : resize_register(integral, width, true);
    }
    return lower_hir_expression(value, width, target);
}

} // namespace fsim::elaboration

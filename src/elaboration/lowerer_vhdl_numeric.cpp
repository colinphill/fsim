// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

    std::string_view simple_name(const std::string_view name)
    {
        const auto separator = name.find_last_of('.');
        return name.substr(
            separator == std::string_view::npos ? 0 : separator + 1);
    }

    SourceLocation numeric_source_location(const frontend::SourceSpan& span)
    {
        return SourceLocation {
            span.source_name.str(),
            static_cast<std::uint32_t>(span.begin.line),
            static_cast<std::uint32_t>(span.begin.column)
        };
    }

} // namespace

Lowerer::ExpressionAttempt
Lowerer::lower_vhdl_numeric_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type)
{
    if (language_ != frontend::Language::Vhdl2008
        || expression.kind != ExpressionKind::Call) {
        return ExpressionAttempt { };
    }
    const auto name = simple_name(expression.text);
    const bool synopsys_integer = name == "conv_integer";
    const bool synopsys_conversion = name == "conv_signed"
        || name == "conv_unsigned" || name == "conv_std_logic_vector"
        || name == "ext" || name == "sxt";
    const bool conversion = name == "to_integer"
        || name == "to_signed" || name == "to_unsigned"
        || name == "resize" || synopsys_integer || synopsys_conversion;
    const bool shift = name == "shift_left" || name == "shift_right"
        || name == "rotate_left" || name == "rotate_right"
        || name == "shl" || name == "shr";
    if (!conversion && !shift) {
        return ExpressionAttempt { };
    }
    if (synopsys_integer && vhdl_synopsys_signed_visible_
        && vhdl_synopsys_unsigned_visible_
        && expression.text.find("std_logic_signed") == std::string::npos
        && expression.text.find("std_logic_unsigned") == std::string::npos) {
        report(
            "FSIM-ELAB-VHSYN-001",
            "std_logic_signed and std_logic_unsigned expose conflicting "
            "conv_integer overloads; qualify the package or remove one use "
            "clause",
            expression.span);
        return std::nullopt;
    }

    if (shift) {
        if (expression.operands.size() != 2) {
            report(
                "FSIM-ELAB-VHNUM-001",
                "a numeric shift or rotate requires a vector and an integer count",
                expression.span);
            return std::nullopt;
        }
        auto operation = name == "shift_left" || name == "shl" ? "sll"
            : name == "rotate_left"                            ? "rol"
            : name == "rotate_right"                           ? "ror"
            : name == "shr" && expression.text.find("std_logic_signed") != std::string::npos
            ? "sra"
            : is_signed_expression(expression.operands.front()) ? "sra"
                                                                : "srl";
        auto operands = expression.operands;
        if ((name == "shl" || name == "shr")
            && !is_integer_expression(operands[1])) {
            operands[1] = Expression {
                ExpressionKind::Call,
                "conv_integer",
                { std::move(operands[1]) },
                expression.operands[1].span
            };
        }
        return lower_expression(
            Expression {
                ExpressionKind::Binary,
                operation,
                std::move(operands),
                expression.span },
            expected_width,
            expected_type);
    }

    if (name == "to_integer" || synopsys_integer) {
        const auto integer_width = static_cast<std::size_t>(
            frontend::vhdl_predefined_integer_storage_width(
                vhdl_standard_));
        if (expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-VHNUM-001",
                "to_integer requires exactly one signed or unsigned vector",
                expression.span);
            return std::nullopt;
        }
        const auto width = infer_width(expression.operands.front());
        const bool signed_operand = expression.text.find("std_logic_signed")
                != std::string::npos
            || (expression.text.find("std_logic_unsigned") == std::string::npos
                && (is_signed_expression(expression.operands.front())
                    || (synopsys_integer && vhdl_synopsys_signed_visible_
                        && !vhdl_synopsys_unsigned_visible_)));
        if (!width || *width == 0) {
            report(
                "FSIM-ELAB-VHNUM-003",
                "to_integer requires a constrained signed or unsigned vector",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto source = lower_expression(
            expression.operands.front(), *width);
        if (!source) {
            return std::nullopt;
        }
        const auto value_width = signed_operand
            ? integer_width : integer_width - 1U;
        const auto narrowed = resize_register(
            *source, value_width, signed_operand);
        const auto restored = resize_register(
            narrowed, *width, signed_operand);
        const auto self_equal = allocate_register(
            1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::equal, self_equal, *source, *source });
        const auto one = allocate_register(
            1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LoadConstant {
            one, PackedLogic4(1, Logic4::one) });
        const auto known = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, known, self_equal, one });
        process_.operations.emplace_back(Assert {
            known,
            "numeric_std.to_integer detected a metavalue and returned zero",
            AssertionSeverity::warning,
            numeric_source_location(expression.operands.front().span) });
        const auto fits = allocate_register(
            1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::equal, fits, *source, restored });
        const auto fits_or_unknown = allocate_register(
            1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ConditionalSelect {
            fits_or_unknown, known, fits, one });
        process_.operations.emplace_back(Assert {
            fits_or_unknown,
            "VHDL numeric to_integer operand is outside the predefined "
            "integer range",
            AssertionSeverity::failure,
            numeric_source_location(expression.operands.front().span) });
        const auto resized = resize_register(
            narrowed, integer_width, signed_operand);
        const auto zero = allocate_register(
            integer_width, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LoadConstant {
            zero, PackedLogic4(integer_width, Logic4::zero) });
        const auto selected = allocate_register(
            integer_width, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ConditionalSelect {
            selected, known, resized, zero });
        const auto result = allocate_register(
            integer_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(CopyRegister { result, selected });
        return result;
    }

    if (expression.operands.size() != 2) {
        report(
            "FSIM-ELAB-VHNUM-001",
            std::string { name }
                + " requires a value and a locally static result size",
            expression.span);
        return std::nullopt;
    }
    const auto requested = static_integer_value(expression.operands[1]);
    const bool permits_null = synopsys_conversion;
    if (!requested || *requested < (permits_null ? 0 : 1)
        || static_cast<std::uint64_t>(*requested)
            > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-VHNUM-002",
            std::string { name }
                + (permits_null
                        ? " requires a nonnegative locally static result size "
                        : " requires a positive locally static result size ")
                + "representable by SimIR",
            expression.operands[1].span);
        return std::nullopt;
    }
    const auto result_width = static_cast<std::size_t>(*requested);
    if (expected_width != result_width) {
        report(
            "FSIM-ELAB-VHNUM-004",
            std::string { name } + " result width "
                + std::to_string(result_width)
                + " does not match its contextual width "
                + std::to_string(expected_width),
            expression.span);
        return std::nullopt;
    }

    if (name == "resize" || name == "ext" || name == "sxt") {
        const auto source_width = infer_width(expression.operands.front());
        if (!source_width || *source_width == 0) {
            report(
                "FSIM-ELAB-VHNUM-001",
                "resize requires a bounded signed or unsigned vector",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto source = lower_expression(
            expression.operands.front(), *source_width);
        if (!source) {
            return std::nullopt;
        }
        auto result = resize_register(
            *source,
            result_width,
            name == "sxt" || (name != "ext" && is_signed_expression(expression.operands.front())));
        if (expected_type != nullptr
            && register_domain(result) != expected_type->domain) {
            const auto converted = allocate_register(
                result_width, expected_type->domain);
            process_.operations.emplace_back(CopyRegister { converted, result });
            result = converted;
        }
        return result;
    }

    const auto& value = expression.operands.front();
    const bool signed_result = name == "to_signed" || name == "conv_signed";
    const bool unsigned_result = name == "to_unsigned" || name == "conv_unsigned";
    if (!is_integer_expression(value) && !synopsys_conversion) {
        report(
            "FSIM-ELAB-VHNUM-001",
            std::string { name } + " requires an integer value operand",
            value.span);
        return std::nullopt;
    }
    const auto source_width = infer_width(value);
    if (!source_width) {
        report(
            "FSIM-ELAB-VHNUM-003",
            std::string { name } + " requires a bounded vector or integer value",
            value.span);
        return std::nullopt;
    }
    const auto source = lower_expression(value, *source_width);
    if (!source) {
        return std::nullopt;
    }
    if (unsigned_result && is_integer_expression(value)) {
        const auto integer_range = frontend::vhdl_predefined_integer_range(
            vhdl_standard_, "integer");
        process_.operations.emplace_back(IntegerCheck {
            *source, 0, integer_range.right });
    }
    const auto resized = resize_register(
        *source, result_width,
        signed_result || (name == "conv_std_logic_vector" && is_signed_expression(value)));
    const auto* visible_result_type = visible_type_mark(
        signed_result ? "signed" : unsigned_result ? "unsigned"
                                                   : "std_logic_vector");
    const auto domain = expected_type != nullptr
            && (expected_type->domain == frontend::ValueDomain::Bit2
                || expected_type->domain == frontend::ValueDomain::Logic9)
        ? expected_type->domain
        : visible_result_type != nullptr
            && (visible_result_type->domain == frontend::ValueDomain::Bit2
                || visible_result_type->domain
                    == frontend::ValueDomain::Logic9)
        ? visible_result_type->domain
        : frontend::ValueDomain::Logic9;
    const auto result = allocate_register(result_width, domain);
    process_.operations.emplace_back(CopyRegister { result, resized });
    return result;
}

} // namespace fsim::elaboration

// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt Lowerer::lower_system_function_expression(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type* expected_type) {
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$dimensions"
                || expression.text == "$unpacked_dimensions")) {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-090",
                    expression.text
                        + " requires SystemVerilog and exactly one "
                          "statically sized packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto operand_width =
                infer_width(expression.operands.front());
            const auto range =
                operand_width
                    ? expression_range(
                          expression.operands.front(),
                          *operand_width)
                    : std::nullopt;
            if (!operand_width || !range || *operand_width == 0) {
                report(
                    "FSIM-ELAB-090",
                    expression.text
                        + " cannot infer a static packed dimension "
                          "for its argument",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(
                    expression.text == "$dimensions" ? 1 : 0,
                    32)});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$onehot"
                || expression.text == "$onehot0")) {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-087",
                    expression.text
                        + " requires SystemVerilog and exactly one "
                          "packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front())
                    .value_or(expected_width);
            const auto source = lower_expression(
                expression.operands.front(), source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Reduction{
                expression.text == "$onehot"
                    ? ReductionOperator::one_hot
                    : ReductionOperator::one_hot_or_zero,
                destination,
                *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countones") {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-088",
                    "$countones requires SystemVerilog and exactly one "
                    "packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front());
            if (!source_width || *source_width == 0
                || *source_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-088",
                    "$countones cannot infer a representable static "
                    "packed width for its argument",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto source = lower_expression(
                expression.operands.front(), *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                CountOnes{destination, *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countbits") {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() < 2) {
                report(
                    "FSIM-ELAB-089",
                    "$countbits requires SystemVerilog, one packed "
                    "expression, and at least one constant one-bit "
                    "control",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front());
            if (!source_width || *source_width == 0
                || *source_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-089",
                    "$countbits cannot infer a representable static "
                    "packed width for its expression",
                    expression.operands.front().span);
                return std::nullopt;
            }
            std::uint8_t state_mask = 0;
            for (std::size_t index = 1;
                 index < expression.operands.size();
                 ++index) {
                const auto control = literal_value(
                    expression.operands[index],
                    1,
                    frontend::Language::SystemVerilog2017);
                if (!control || control->value.width() != 1) {
                    report(
                        "FSIM-ELAB-089",
                        "$countbits controls must be constant one-bit "
                        "0, 1, X, or Z values",
                        expression.operands[index].span);
                    return std::nullopt;
                }
                const auto state = static_cast<std::uint8_t>(
                    control->value.get(0));
                state_mask |=
                    static_cast<std::uint8_t>(
                        std::uint8_t{1} << state);
            }
            const auto source = lower_expression(
                expression.operands.front(), *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                CountBits{
                    destination, *source, state_mask});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "rising_edge"
                || expression.text == "falling_edge")) {
            report(
                "FSIM-ELAB-045",
                "a VHDL edge predicate is executable only as the sole, "
                "else-free outer statement of a sensitive process",
                expression.span);
            return std::nullopt;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "?:"
            && expression.operands.size() == 3) {
            const auto condition =
                lower_expression(expression.operands[0], 1);
            if (!condition) {
                return std::nullopt;
            }
            if (register_width(*condition) != 1) {
                report(
                    "FSIM-ELAB-064",
                    "a conditional-expression condition must produce one "
                    "bit in this executable slice",
                    expression.operands[0].span);
                return std::nullopt;
            }
            if (language_ == frontend::Language::Vhdl2008
                && register_domain(*condition)
                    != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-092",
                    "a VHDL conditional-assignment condition must have "
                    "type boolean",
                    expression.operands[0].span);
                return std::nullopt;
            }
            const auto value_width =
                infer_width(expression.operands[1])
                    .value_or(
                        infer_width(expression.operands[2])
                            .value_or(expected_width));
            const auto when_true =
                lower_expression(
                    expression.operands[1],
                    value_width,
                    expected_type);
            const auto when_false =
                lower_expression(
                    expression.operands[2],
                    value_width,
                    expected_type);
            if (!when_true || !when_false) {
                return std::nullopt;
            }
            if (register_width(*when_true)
                != register_width(*when_false)) {
                report(
                    "FSIM-ELAB-065",
                    "conditional-expression alternatives have different "
                    "widths ("
                        + std::to_string(register_width(*when_true))
                        + " and "
                        + std::to_string(register_width(*when_false))
                        + ")",
                    expression.span);
                return std::nullopt;
            }
            const auto result_domain =
                language_ == frontend::Language::Vhdl2008
                        && is_integer_expression(
                            expression.operands[1])
                        && is_integer_expression(
                            expression.operands[2])
                    ? frontend::ValueDomain::Integer
                : register_domain(*when_true)
                        == register_domain(*when_false)
                    ? register_domain(*when_true)
                : is_two_state_domain(register_domain(*when_true))
                        && is_two_state_domain(
                            register_domain(*when_false))
                    ? frontend::ValueDomain::Bit2
                : register_domain(*when_true)
                            == frontend::ValueDomain::Logic9
                        || register_domain(*when_false)
                            == frontend::ValueDomain::Logic9
                    ? frontend::ValueDomain::Logic9
                    : frontend::ValueDomain::Logic4;
            const auto destination =
                allocate_register(
                    register_width(*when_true), result_domain);
            process_.operations.emplace_back(ConditionalSelect{
                destination,
                *condition,
                *when_true,
                *when_false});
            return destination;
        }

        return ExpressionAttempt{};
    }

} // namespace fsim::elaboration

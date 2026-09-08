// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<std::vector<runtime::SystemVerilogConstraintTemplate>>
Lowerer::lower_inline_constraints(const Expression& expression)
{
    const auto marker = std::ranges::find(
        expression.aggregate_choices, "@sv-inline-constraint");
    if (marker == expression.aggregate_choices.end()) {
        return std::vector<runtime::SystemVerilogConstraintTemplate> { };
    }
    const auto marker_index = static_cast<std::size_t>(std::distance(
        expression.aggregate_choices.begin(), marker));
    if (marker_index >= expression.aggregate_choice_expressions.size()) {
        report(
            "FSIM-ELAB-SVRAND-005",
            "inline constraint syntax has no retained expression block",
            expression.span);
        return std::nullopt;
    }

    using Template = runtime::SystemVerilogConstraintTemplate;
    using TemplateKind = runtime::SystemVerilogConstraintTemplateKind;
    std::function<std::optional<Template>(const Expression&)> lower;
    lower = [&](const Expression& input) -> std::optional<Template> {
        Template output;
        output.text = input.text;
        if (input.text == "@sv-null") {
            output.kind = TemplateKind::Constant;
            output.constant = PackedLogic4::from_aval_bval(64, 0, 0);
            output.profile = {
                runtime::SystemVerilogConstraintDomainKind::BitVector,
                64,
                false,
                "null",
                false
            };
            return output;
        }
        if (input.kind == ExpressionKind::Identifier) {
            std::string error;
            const auto value = evaluate_systemverilog_constant_expression(
                input, { }, { }, error);
            if (!value) {
                output.kind = TemplateKind::Name;
                return output;
            }
            output.kind = TemplateKind::Constant;
            output.constant = value->packed;
            output.profile = {
                value->domain == frontend::ValueDomain::Integer
                    ? runtime::SystemVerilogConstraintDomainKind::Integer
                    : runtime::SystemVerilogConstraintDomainKind::BitVector,
                value->width,
                value->is_signed,
                value->nominal_type.empty()
                    ? std::string { "$inline-constant" }
                    : value->nominal_type,
                value->domain == frontend::ValueDomain::Logic4
            };
            return output;
        }
        const auto special_kind = [&]() -> std::optional<TemplateKind> {
            if (input.kind == ExpressionKind::Unary) {
                return TemplateKind::Unary;
            }
            if (input.kind == ExpressionKind::Binary) {
                return TemplateKind::Binary;
            }
            if (input.kind != ExpressionKind::Call)
                return std::nullopt;
            if (input.text == "?:")
                return TemplateKind::Conditional;
            if (input.text == "inside")
                return TemplateKind::InsideSet;
            if (input.text == "@inside-range") {
                return TemplateKind::InsideRange;
            }
            if (input.text == "dist")
                return TemplateKind::Distribution;
            if (input.text == "@dist-:=" || input.text == "@dist-:/") {
                return TemplateKind::DistributionItem;
            }
            if (input.text == "soft")
                return TemplateKind::Soft;
            if (input.text == "@constraint-block") {
                return TemplateKind::Block;
            }
            if (input.text == "@constraint-implies") {
                return TemplateKind::Implication;
            }
            if (input.text == "@constraint-if") {
                return TemplateKind::ConditionalConstraint;
            }
            if (input.text == "@solve-before") {
                return TemplateKind::SolveBefore;
            }
            if (input.text == "@solve-list") {
                return TemplateKind::SolveList;
            }
            return std::nullopt;
        }();
        if (special_kind) {
            output.kind = *special_kind;
            output.operands.reserve(input.operands.size());
            for (const auto& operand : input.operands) {
                auto retained = lower(operand);
                if (!retained)
                    return std::nullopt;
                output.operands.push_back(std::move(*retained));
            }
            return output;
        }
        std::string error;
        const auto value = evaluate_systemverilog_constant_expression(
            input, { }, { }, error);
        if (!value) {
            report(
                "FSIM-ELAB-SVRAND-006",
                "inline constraint expression '" + input.text
                    + "' is not a supported variable, constant, or operator: "
                    + error,
                input.span);
            return std::nullopt;
        }
        output.kind = TemplateKind::Constant;
        output.constant = value->packed;
        output.profile = {
            value->domain == frontend::ValueDomain::Integer
                ? runtime::SystemVerilogConstraintDomainKind::Integer
                : runtime::SystemVerilogConstraintDomainKind::BitVector,
            value->width,
            value->is_signed,
            value->nominal_type.empty()
                ? std::string { "$inline-constant" }
                : value->nominal_type,
            value->domain == frontend::ValueDomain::Logic4
        };
        return output;
    };

    std::vector<Template> result;
    const auto& expressions
        = expression.aggregate_choice_expressions[marker_index];
    result.reserve(expressions.size());
    for (const auto& item : expressions) {
        auto retained = lower(item);
        if (!retained)
            return std::nullopt;
        result.push_back(std::move(*retained));
    }
    return result;
}

Lowerer::ExpressionAttempt Lowerer::lower_unary_attribute_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    auto container_query = lower_container_query(expression);
    if (container_query.handled) {
        return container_query;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && expression.text == "!") {
        const auto source_width = infer_width(expression.operands[0])
                                      .value_or(expected_width);
        const auto source = lower_expression(
            expression.operands[0], source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto source_domain = register_domain(*source);
        const auto result_domain = source_domain == frontend::ValueDomain::Bit2
                || source_domain
                    == frontend::ValueDomain::Boolean
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        const auto destination = allocate_register(1, result_domain);
        process_.operations.emplace_back(
            LogicalNot { destination, *source });
        return destination;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && (expression.text == "&"
            || expression.text == "|"
            || expression.text == "^"
            || expression.text == "~&"
            || expression.text == "~|"
            || expression.text == "~^"
            || expression.text == "^~"
            || expression.text == "and"
            || expression.text == "or"
            || expression.text == "nand"
            || expression.text == "nor"
            || expression.text == "xor"
            || expression.text == "xnor")) {
        const auto source_width = infer_width(expression.operands[0])
                                      .value_or(expected_width);
        const auto source = lower_expression(
            expression.operands[0], source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto source_domain = register_domain(*source);
        const auto result_domain = source_domain == frontend::ValueDomain::Bit2
                || source_domain
                    == frontend::ValueDomain::Boolean
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4;
        auto operation = ReductionOperator::bit_xor;
        if (expression.text == "&"
            || expression.text == "~&"
            || expression.text == "and"
            || expression.text == "nand") {
            operation = ReductionOperator::bit_and;
        } else if (expression.text == "|"
            || expression.text == "~|"
            || expression.text == "or"
            || expression.text == "nor") {
            operation = ReductionOperator::bit_or;
        }
        const auto destination = allocate_register(1, result_domain);
        process_.operations.emplace_back(
            Reduction { operation, destination, *source });
        if (expression.text.starts_with("~")
            || expression.text == "^~"
            || expression.text == "nand"
            || expression.text == "nor"
            || expression.text == "xnor") {
            const auto inverted = allocate_register(1, result_domain);
            process_.operations.emplace_back(
                UnaryNot { inverted, destination });
            return inverted;
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && expression.text == "??") {
        const auto source_width = infer_width(expression.operands[0])
                                      .value_or(expected_width);
        const auto source = lower_expression(
            expression.operands[0], source_width);
        if (!source || register_width(*source) != 1U) {
            report(
                "FSIM-ELAB-082",
                "the VHDL condition operator requires a scalar Boolean, "
                "bit, or logic operand",
                expression.span);
            return std::nullopt;
        }
        const auto result = allocate_register(
            1, frontend::ValueDomain::Boolean);
        const auto one = allocate_register(
            1, register_domain(*source));
        process_.operations.emplace_back(LoadConstant {
            one, PackedLogic4(1, Logic4::one) });
        process_.operations.emplace_back(Binary {
            register_domain(*source)
                    == frontend::ValueDomain::Logic9
                ? BinaryOperator::vhdl_match_equal
                : BinaryOperator::case_equal,
            result,
            *source,
            one });
        return result;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && (expression.text == "+"
            || expression.text == "-")) {
        if ((language_ == frontend::Language::SystemVerilog2017
                || language_ == frontend::Language::Vhdl2008)
            && expected_type != nullptr
            && expected_type->systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None) {
            std::string error;
            const auto evaluated = frontend::evaluate_systemverilog_scalar_constant(
                expression, { }, { }, error);
            if (evaluated) {
                const auto converted = frontend::convert_systemverilog_scalar_constant(
                    *evaluated,
                    expected_type->systemverilog_scalar,
                    error);
                if (!converted) {
                    report(
                        "FSIM-ELAB-SVSCALAR-001",
                        "cannot convert scalar expression '"
                            + expression.text + "': " + error,
                        expression.span);
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    expected_width, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    PackedLogic4::from_aval_bval(
                        expected_width, converted->bits, 0) });
                return destination;
            }
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.operands[0].kind
                == ExpressionKind::IntegerLiteral) {
            std::string error;
            const auto value = evaluate_constant_expression(
                expression, { }, error);
            const auto integer_width = static_cast<std::size_t>(
                frontend::vhdl_predefined_integer_storage_width(
                    vhdl_standard_));
            const auto integer_range =
                frontend::vhdl_predefined_integer_range(
                    vhdl_standard_, "integer");
            if (value && *value >= integer_range.left
                && *value <= integer_range.right) {
                const auto destination = allocate_register(
                    integer_width, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(
                    LoadConstant {
                        destination, integer_value(*value, integer_width) });
                return destination;
            }
        }
        const auto vhdl_operand_type =
            language_ == frontend::Language::Vhdl2008
            ? vhdl_expression_type(expression.operands[0])
            : std::optional<frontend::Type> { };
        const auto* scalar_type = expected_type != nullptr
                && expected_type->systemverilog_scalar
                    != frontend::SystemVerilogScalarKind::None
            ? expected_type
            : vhdl_operand_type
                    && vhdl_operand_type->systemverilog_scalar
                        != frontend::SystemVerilogScalarKind::None
            ? &*vhdl_operand_type
            : nullptr;
        const auto source_width = language_ == frontend::Language::SystemVerilog2017
            ? std::max(
                  expected_width,
                  infer_width(expression.operands[0])
                      .value_or(expected_width))
            : infer_width(expression.operands[0])
                  .value_or(expected_width);
        auto source = lower_expression(
            expression.operands[0], source_width, scalar_type);
        if (source
            && language_
                == frontend::Language::SystemVerilog2017
            && register_width(*source) != source_width) {
            *source = resize_register(
                *source,
                source_width,
                is_signed_expression(expression.operands[0]));
        }
        if (!source || expression.text == "+") {
            return source;
        }
        if (language_ == frontend::Language::Vhdl2008
            && scalar_type != nullptr) {
            const auto zero = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                zero, PackedLogic4::from_aval_bval(
                          64U, std::bit_cast<std::uint64_t>(0.0), 0U) });
            const auto destination = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(SystemVerilogScalarBinary {
                runtime::SystemVerilogScalarBinaryOperator::Subtract,
                destination, zero, *source,
                frontend::SystemVerilogScalarKind::Real,
                frontend::SystemVerilogScalarKind::Real,
                frontend::SystemVerilogScalarKind::Real });
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008
            && register_domain(*source)
                == frontend::ValueDomain::Integer) {
            const auto destination = allocate_register(
                register_width(*source), frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerUnary {
                IntegerUnaryOperator::negate,
                destination,
                *source });
            return destination;
        }
        const auto zero = allocate_register(
            register_width(*source), register_domain(*source));
        process_.operations.emplace_back(LoadConstant {
            zero,
            PackedLogic4(
                register_width(*source), Logic4::zero) });
        const auto destination = allocate_register(
            register_width(*source), register_domain(*source));
        process_.operations.emplace_back(Binary {
            is_signed_expression(expression.operands[0])
                ? BinaryOperator::subtract_signed
                : BinaryOperator::subtract_unsigned,
            destination,
            zero,
            *source });
        return destination;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && expression.text == "abs") {
        if (language_ != frontend::Language::Vhdl2008
            || !is_signed_expression(expression.operands[0])) {
            report(
                "FSIM-ELAB-082",
                "the bounded packed abs operator requires a signed "
                "VHDL operand",
                expression.span);
            return std::nullopt;
        }
        const auto source_width = infer_width(expression.operands[0])
                                      .value_or(expected_width);
        const auto source = lower_expression(
            expression.operands[0], source_width);
        if (!source) {
            return std::nullopt;
        }
        if (register_domain(*source)
            == frontend::ValueDomain::Integer) {
            const auto destination = allocate_register(
                register_width(*source), frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerUnary {
                IntegerUnaryOperator::absolute,
                destination,
                *source });
            return destination;
        }
        const auto zero = allocate_register(
            register_width(*source), register_domain(*source));
        process_.operations.emplace_back(LoadConstant {
            zero,
            PackedLogic4(
                register_width(*source), Logic4::zero) });
        const auto negated = allocate_register(
            register_width(*source), register_domain(*source));
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_signed,
            negated,
            zero,
            *source });
        const auto sign = allocate_register(
            1, register_domain(*source));
        process_.operations.emplace_back(Extract {
            sign,
            *source,
            static_cast<std::uint32_t>(
                register_width(*source) - 1U),
            1 });
        const auto destination = allocate_register(
            register_width(*source), register_domain(*source));
        process_.operations.emplace_back(ConditionalSelect {
            destination,
            sign,
            negated,
            *source });
        return destination;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && (expression.text == "not" || expression.text == "~")) {
        const auto source = lower_expression(expression.operands[0], expected_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            register_width(*source), register_domain(*source));
        process_.operations.emplace_back(UnaryNot { destination, *source });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'event") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-094",
                "'event requires one signal name and no dimension",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-094",
                "'event object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            SignalEvent { destination, signal->second });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'last_value") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-095",
                "'last_value requires one signal name and no dimension",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-095",
                "'last_value object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto& info = design_.signal_info_[signal->second];
        const auto destination = allocate_register(info.width, info.source_domain);
        process_.operations.emplace_back(
            SignalLastValue { destination, signal->second });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'last_event") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-096",
                "'last_event requires one signal name and no duration",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-096",
                "'last_event object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            64, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            SignalLastEvent { destination, signal->second });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'last_active") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHATTR-001",
                "'last_active requires one signal name and no duration",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-VHATTR-001",
                "'last_active object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            64, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            SignalLastActive { destination, signal->second });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'driving") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHATTR-002",
                "'driving requires one signal name and no argument",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-VHATTR-002",
                "'driving object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            SignalDriving { destination, signal->second });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'driving_value") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHATTR-003",
                "'driving_value requires one signal name and no argument",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-VHATTR-003",
                "'driving_value object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto& info = design_.signal_info_[signal->second];
        const auto destination = allocate_register(info.width, info.source_domain);
        process_.operations.emplace_back(
            SignalDrivingValue { destination, signal->second });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'stable") {
        if ((expression.operands.size() != 1
                && expression.operands.size() != 2)
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-097",
                "'stable requires one signal name and at most one "
                "static nonnegative duration",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-097",
                "'stable object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        runtime::SimulationTick duration { };
        if (expression.operands.size() == 2) {
            std::string error;
            const auto value = evaluate_constant_expression(
                expression.operands[1], { }, error);
            if (!value || *value < 0) {
                report(
                    "FSIM-ELAB-VHATTR-004",
                    "'stable duration must be a static nonnegative "
                    "time value in project ticks",
                    expression.operands[1].span);
                return std::nullopt;
            }
            duration = static_cast<runtime::SimulationTick>(*value);
        }
        if (duration != 0) {
            const auto derived = vhdl_implicit_signal_attribute(
                signal->second, "stable", duration, expression.span);
            if (!derived) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                ReadSignal { destination, *derived });
            return destination;
        }
        const auto event = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            SignalEvent { event, signal->second });
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            UnaryNot { destination, event });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'quiet") {
        if ((expression.operands.size() != 1
                && expression.operands.size() != 2)
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHATTR-005",
                "'quiet requires one signal name and at most one "
                "static nonnegative duration",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-VHATTR-005",
                "'quiet object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        runtime::SimulationTick duration { };
        if (expression.operands.size() == 2) {
            std::string error;
            const auto value = evaluate_constant_expression(
                expression.operands[1], { }, error);
            if (!value || *value < 0) {
                report(
                    "FSIM-ELAB-VHATTR-005",
                    "'quiet duration must be a static nonnegative time "
                    "value in project ticks",
                    expression.operands[1].span);
                return std::nullopt;
            }
            duration = static_cast<runtime::SimulationTick>(*value);
        }
        if (duration != 0) {
            const auto derived = vhdl_implicit_signal_attribute(
                signal->second, "quiet", duration, expression.span);
            if (!derived) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                ReadSignal { destination, *derived });
            return destination;
        }
        const auto active = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            SignalActive { active, signal->second });
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            UnaryNot { destination, active });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'active") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-098",
                "'active requires one signal name and no duration",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-098",
                "'active object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            SignalActive { destination, signal->second });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'transaction") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHATTR-006",
                "'transaction requires one signal name and no argument",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-VHATTR-006",
                "'transaction object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto derived = vhdl_implicit_signal_attribute(
            signal->second, "transaction", 0, expression.span);
        if (!derived) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            ReadSignal { destination, *derived });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'delayed") {
        if ((expression.operands.size() != 1
                && expression.operands.size() != 2)
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHATTR-007",
                "'delayed requires one signal name and at most one "
                "static nonnegative duration",
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        if (signal == signals_.end()) {
            report(
                "FSIM-ELAB-VHATTR-007",
                "'delayed object is not a visible signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        runtime::SimulationTick duration { };
        if (expression.operands.size() == 2) {
            std::string error;
            const auto value = evaluate_constant_expression(
                expression.operands[1], { }, error);
            if (!value || *value < 0) {
                report(
                    "FSIM-ELAB-VHATTR-007",
                    "'delayed duration must be a static nonnegative time "
                    "value in project ticks",
                    expression.operands[1].span);
                return std::nullopt;
            }
            duration = static_cast<runtime::SimulationTick>(*value);
        }
        const auto derived = vhdl_implicit_signal_attribute(
            signal->second, "delayed", duration, expression.span);
        if (!derived) {
            return std::nullopt;
        }
        const auto& info = design_.signal_info_[*derived];
        const auto destination = allocate_register(info.width, info.source_domain);
        process_.operations.emplace_back(
            ReadSignal { destination, *derived });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && (expression.text == "'range"
            || expression.text == "'reverse_range")) {
        const auto* scalar_prefix = !expression.operands.empty()
                && expression.operands.front().kind
                    == ExpressionKind::Identifier
            ? visible_type_mark(
                  expression.operands.front().text)
            : nullptr;
        if (scalar_prefix != nullptr
            && !is_vhdl_array_like(*scalar_prefix)) {
            report(
                "FSIM-ELAB-VHSCALARATTR-003",
                expression.text
                    + " is a discrete range and cannot be used as a "
                      "scalar expression",
                expression.span);
            return std::nullopt;
        }
        if (!vhdl_array_attribute_range(
                expression, true)) {
            return std::nullopt;
        }
        report(
            "FSIM-ELAB-VHARRAYATTR-003",
            expression.text
                + " is a discrete range and cannot be used as a "
                  "scalar expression",
            expression.span);
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && (expression.text == "'left"
            || expression.text == "'right"
            || expression.text == "'low"
            || expression.text == "'high"
            || expression.text == "'length"
            || expression.text == "'ascending")) {
        const auto range = vhdl_array_attribute_range(
            expression, true);
        if (!range) {
            return std::nullopt;
        }
        if (expression.text == "'ascending") {
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LoadConstant {
                destination,
                PackedLogic4(
                    1,
                    range->descending
                        ? Logic4::zero
                        : Logic4::one) });
            return destination;
        }
        std::int64_t result = 0;
        if (expression.text == "'left") {
            result = range->left;
        } else if (expression.text == "'right") {
            result = range->right;
        } else if (expression.text == "'low") {
            result = std::min(range->left, range->right);
        } else if (expression.text == "'high") {
            result = std::max(range->left, range->right);
        } else {
            const auto* type = vhdl_array_attribute_prefix_type(expression);
            const auto dimension = expression.operands.size() == 2
                ? static_integer_value(expression.operands[1]).value_or(1)
                : std::int64_t { 1 };
            const bool null_array = type != nullptr && type->vhdl_array
                && dimension > 0
                && static_cast<std::uint64_t>(dimension)
                    <= type->vhdl_array->dimensions.size()
                && type->vhdl_array->dimensions[static_cast<std::size_t>(dimension - 1)].null;
            const auto width = null_array ? std::uint64_t { 0 } : range->width();
            const auto integer_range =
                frontend::vhdl_predefined_integer_range(
                    vhdl_standard_, "integer");
            if (width > static_cast<std::uint64_t>(integer_range.right)) {
                report(
                    "FSIM-ELAB-VHARRAYATTR-004",
                    expression.text
                        + " result is outside the predefined "
                          "integer range",
                    expression.span);
                return std::nullopt;
            }
            result = static_cast<std::int64_t>(width);
        }
        const auto integer_range =
            frontend::vhdl_predefined_integer_range(
                vhdl_standard_, "integer");
        if (result < integer_range.left || result > integer_range.right) {
            report(
                "FSIM-ELAB-VHARRAYATTR-004",
                expression.text
                    + " result is outside the predefined "
                      "integer range",
                expression.span);
            return std::nullopt;
        }
        const auto integer_width = static_cast<std::size_t>(
            frontend::vhdl_predefined_integer_storage_width(
                vhdl_standard_));
        const auto destination = allocate_register(
            integer_width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination,
            integer_value(result, integer_width) });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && language_ != frontend::Language::Vhdl2008
        && (expression.text == "$signed"
            || expression.text == "$unsigned")) {
        if (expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-083",
                expression.text
                    + " requires exactly one packed argument",
                expression.span);
            return std::nullopt;
        }
        const auto source_width = infer_width(expression.operands.front())
                                      .value_or(expected_width);
        return lower_expression(
            expression.operands.front(), source_width);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "std::randomize") {
        if (language_ != frontend::Language::SystemVerilog2017
            || expression.operands.empty()) {
            report(
                "FSIM-ELAB-SVRAND-001",
                "std::randomize requires one or more local packed "
                "arguments in SystemVerilog",
                expression.span);
            return std::nullopt;
        }
        ScopeRandomize operation;
        operation.destination = allocate_register(
            32, frontend::ValueDomain::Integer);
        auto inline_constraints = lower_inline_constraints(expression);
        if (!inline_constraints)
            return std::nullopt;
        operation.inline_constraints = std::move(*inline_constraints);
        operation.maximum_domain_values = std::size_t { 1 } << 20U;
        struct CopyOut {
            const Expression* expression { };
            const frontend::Type* type { };
            RegisterId value { };
        };
        std::vector<CopyOut> copy_outs;
        for (const auto& operand : expression.operands) {
            if (operand.kind != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-SVRAND-002",
                    "std::randomize arguments must be writable local "
                    "identifiers",
                    operand.span);
                return std::nullopt;
            }
            const auto local = local_types_.find(operand.text);
            if (local == local_types_.end() || local->second == nullptr
                || local->second->systemverilog_container) {
                report(
                    "FSIM-ELAB-SVRAND-003",
                    "std::randomize argument '" + operand.text
                        + "' is not a supported packed local",
                    operand.span);
                return std::nullopt;
            }
            const auto& type = *local->second;
            const auto width = type.width();
            if (!width || *width == 0
                || *width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVRAND-004",
                    "std::randomize packed locals require a nonempty "
                    "width representable by SimIR metadata",
                    operand.span);
                return std::nullopt;
            }
            const auto target = lower_expression(operand, *width);
            if (!target)
                return std::nullopt;
            ScopeRandomizeTarget retained;
            retained.target = *target;
            retained.canonical_identity = process_.name + "::"
                + operand.text;
            retained.width = static_cast<std::uint32_t>(*width);
            retained.signed_value = type.is_signed;
            retained.nominal_type = !type.named_type.empty()
                ? type.named_type
                : type.spelling;
            retained.domain_kind = !type.enumeration_literals.empty()
                ? ScopeRandomizeDomainKind::enumeration
                : type.domain == frontend::ValueDomain::Integer
                ? ScopeRandomizeDomainKind::integer
                : ScopeRandomizeDomainKind::bit_vector;
            if (!type.enumeration_literals.empty()) {
                for (std::size_t ordinal = 0;
                    ordinal < type.enumeration_literals.size();
                    ++ordinal) {
                    retained.domain.push_back(
                        PackedLogic4::from_aval_bval(
                            *width, ordinal, 0));
                }
            }
            operation.targets.push_back(std::move(retained));
            copy_outs.push_back({ &operand, &type, *target });
        }
        const auto destination = operation.destination;
        process_.operations.emplace_back(std::move(operation));
        for (std::size_t index = 0; index < copy_outs.size(); ++index) {
            lower_callable_copy_out(
                *copy_outs[index].expression,
                *copy_outs[index].type,
                copy_outs[index].value,
                { }, { }, false, false,
                "@std_randomize_copyout_" + std::to_string(index));
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$urandom"
            || expression.text == "$random")) {
        const bool urandom = expression.text == "$urandom";
        if ((urandom
                && language_
                    != frontend::Language::SystemVerilog2017)
            || (!urandom
                && language_
                    == frontend::Language::Vhdl2008)
            || !expression.operands.empty()) {
            report(
                "FSIM-ELAB-104",
                expression.text
                    + " requires no arguments"
                    + (urandom ? " in SystemVerilog" : ""),
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            RandomValue {
                destination,
                urandom
                    ? RandomKind::urandom
                    : RandomKind::random,
                std::nullopt,
                std::nullopt });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$dist_uniform"
            || expression.text == "$dist_normal"
            || expression.text == "$dist_exponential"
            || expression.text == "$dist_poisson"
            || expression.text == "$dist_chi_square"
            || expression.text == "$dist_t"
            || expression.text == "$dist_erlang")) {
        using Kind = RandomDistributionKind;
        const auto kind = expression.text == "$dist_uniform"
            ? Kind::uniform
            : expression.text == "$dist_normal"
            ? Kind::normal
            : expression.text == "$dist_exponential"
            ? Kind::exponential
            : expression.text == "$dist_poisson"
            ? Kind::poisson
            : expression.text == "$dist_chi_square"
            ? Kind::chi_square
            : expression.text == "$dist_t"
            ? Kind::student_t
            : Kind::erlang;
        const bool three_arguments = kind == Kind::uniform
            || kind == Kind::normal || kind == Kind::erlang;
        const auto required_arity = three_arguments ? 3U : 2U;
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != required_arity
            || expression.operands.empty()
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVRAND-008",
                expression.text
                    + " requires a direct writable seed followed by "
                    + std::to_string(required_arity - 1U)
                    + " integer argument"
                    + (required_arity == 3U ? "s" : ""),
                expression.span);
            return std::nullopt;
        }

        const auto& seed_expression = expression.operands.front();
        const auto* seed_type = object_type(seed_expression.text);
        const auto invalid_seed_type = seed_type == nullptr
            || seed_type->systemverilog_scalar
                == frontend::SystemVerilogScalarKind::ShortReal
            || seed_type->systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Real
            || seed_type->systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Realtime
            || seed_type->systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Chandle;
        if (invalid_seed_type) {
            report(
                "FSIM-ELAB-SVRAND-008",
                expression.text
                    + " seed must be a writable packed integer variable at least 32 bits wide",
                seed_expression.span);
            return std::nullopt;
        }

        std::optional<RegisterId> seed_register;
        std::optional<RegisterId> seed_local;
        std::optional<SignalId> seed_signal;
        std::size_t seed_width { };
        if (const auto local = locals_.find(seed_expression.text);
            local != locals_.end()) {
            seed_local = local->second;
            seed_width = register_width(local->second);
            seed_register = local->second;
        } else if (const auto signal = signals_.find(seed_expression.text);
            signal != signals_.end()
            && !read_only_signals_.contains(signal->second)) {
            seed_signal = signal->second;
            seed_width = design_.signal_info_[signal->second].width;
            seed_register = allocate_register(
                seed_width,
                design_.signal_info_[signal->second].source_domain);
            process_.operations.emplace_back(
                ReadSignal { *seed_register, signal->second });
        }
        if (!seed_register || seed_width < 32U) {
            report(
                "FSIM-ELAB-SVRAND-008",
                expression.text
                    + " seed must be a writable packed integer variable at least 32 bits wide",
                seed_expression.span);
            return std::nullopt;
        }
        if (seed_width != 32U) {
            *seed_register = resize_register(
                *seed_register, 32U, true);
        }

        const auto lower_integer_argument = [&](const std::size_t index)
            -> std::optional<RegisterId> {
            const auto& operand = expression.operands[index];
            const auto* type = operand.kind == ExpressionKind::Identifier
                ? object_type(operand.text)
                : nullptr;
            const auto scalar = type != nullptr
                ? type->systemverilog_scalar
                : operand.systemverilog_scalar_kind;
            if (scalar == frontend::SystemVerilogScalarKind::ShortReal
                || scalar == frontend::SystemVerilogScalarKind::Real
                || scalar == frontend::SystemVerilogScalarKind::Realtime
                || scalar == frontend::SystemVerilogScalarKind::Chandle) {
                report(
                    "FSIM-ELAB-SVRAND-008",
                    expression.text + " arguments must be integral",
                    operand.span);
                return std::nullopt;
            }
            auto value = lower_expression(operand, 32U);
            if (value && register_width(*value) != 32U) {
                *value = resize_register(
                    *value, 32U, is_signed_expression(operand));
            }
            return value;
        };
        const auto first = lower_integer_argument(1U);
        const auto second = three_arguments
            ? lower_integer_argument(2U)
            : std::optional<RegisterId> { };
        if (!first || (three_arguments && !second))
            return std::nullopt;

        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(RandomDistribution {
            destination, *seed_register, kind, *first, second });
        if (seed_width != 32U) {
            const auto widened = resize_register(
                *seed_register, seed_width, true);
            if (seed_local) {
                process_.operations.emplace_back(
                    CopyRegister { *seed_local, widened });
            } else {
                process_.operations.emplace_back(
                    WriteBlocking { *seed_signal, widened });
            }
        } else if (seed_signal) {
            process_.operations.emplace_back(
                WriteBlocking { *seed_signal, *seed_register });
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$urandom_range") {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.empty()
            || expression.operands.size() > 2) {
            report(
                "FSIM-ELAB-104",
                "$urandom_range requires one maximum and an "
                "optional minimum argument in SystemVerilog",
                expression.span);
            return std::nullopt;
        }
        const auto maximum = lower_expression(expression.operands[0], 32);
        if (!maximum) {
            return std::nullopt;
        }
        std::optional<RegisterId> minimum;
        if (expression.operands.size() == 2) {
            minimum = lower_expression(expression.operands[1], 32);
            if (!minimum) {
                return std::nullopt;
            }
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            RandomValue {
                destination,
                RandomKind::urandom_range,
                *maximum,
                minimum });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunknown") {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-084",
                "$isunknown requires SystemVerilog and exactly one "
                "packed argument",
                expression.span);
            return std::nullopt;
        }
        const auto source_width = infer_width(expression.operands.front())
                                      .value_or(expected_width);
        const auto source = lower_expression(
            expression.operands.front(), source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto self_equal = allocate_register(
            1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::equal,
            self_equal,
            *source,
            *source });
        const auto unknown = allocate_register(
            1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LoadConstant {
            unknown, PackedLogic4(1, Logic4::x) });
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal,
            destination,
            self_equal,
            unknown });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$bits") {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-085",
                "$bits requires SystemVerilog and exactly one "
                "statically sized packed argument",
                expression.span);
            return std::nullopt;
        }
        const auto operand_width = infer_width(expression.operands.front());
        if (!operand_width || *operand_width == 0
            || *operand_width
                > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-085",
                "$bits cannot infer a representable static packed "
                "width for its argument",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination,
            unsigned_value(*operand_width, 32) });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$left"
            || expression.text == "$right"
            || expression.text == "$low"
            || expression.text == "$high"
            || expression.text == "$size"
            || expression.text == "$increment")) {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.empty()
            || expression.operands.size() > 2) {
            report(
                "FSIM-ELAB-086",
                expression.text
                    + " requires SystemVerilog, one "
                      "one-dimensional packed argument, and at "
                      "most one dimension argument",
                expression.span);
            return std::nullopt;
        }
        if (expression.operands.size() == 2) {
            const auto dimension = constant_index(expression.operands[1]);
            if (!dimension || *dimension != 1) {
                report(
                    "FSIM-ELAB-086",
                    expression.text
                        + " supports only the constant packed "
                          "dimension 1",
                    expression.operands[1].span);
                return std::nullopt;
            }
        }
        const auto operand_width = infer_width(expression.operands.front());
        const auto range = operand_width
            ? expression_range(
                  expression.operands.front(),
                  *operand_width)
            : std::nullopt;
        if (!operand_width || !range
            || *operand_width == 0
            || *operand_width
                > std::numeric_limits<std::int32_t>::max()) {
            report(
                "FSIM-ELAB-086",
                expression.text
                    + " cannot infer a representable static packed "
                      "range for its argument",
                expression.operands.front().span);
            return std::nullopt;
        }
        std::int64_t result = 0;
        if (expression.text == "$left") {
            result = range->left;
        } else if (expression.text == "$right") {
            result = range->right;
        } else if (expression.text == "$low") {
            result = std::min(range->left, range->right);
        } else if (expression.text == "$high") {
            result = std::max(range->left, range->right);
        } else if (expression.text == "$increment") {
            result = range->left >= range->right ? 1 : -1;
        } else {
            result = static_cast<std::int64_t>(*operand_width);
        }
        if (result < std::numeric_limits<std::int32_t>::min()
            || result
                > std::numeric_limits<std::int32_t>::max()) {
            report(
                "FSIM-ELAB-086",
                expression.text
                    + " result is outside the bounded 32-bit "
                      "integer range",
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination,
            unsigned_value(
                static_cast<std::uint32_t>(result), 32) });
        return destination;
    }
    return ExpressionAttempt { };
}

} // namespace fsim::elaboration

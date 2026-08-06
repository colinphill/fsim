// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt Lowerer::lower_unary_attribute_expression(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type* expected_type) {
        auto container_query =
            lower_container_query(expression);
        if (container_query.handled) {
            return container_query;
        }
        if (expression.kind == ExpressionKind::Unary
            && expression.operands.size() == 1
            && expression.text == "!") {
            const auto source_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto source =
                lower_expression(
                    expression.operands[0], source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto source_domain = register_domain(*source);
            const auto result_domain =
                source_domain == frontend::ValueDomain::Bit2
                        || source_domain
                            == frontend::ValueDomain::Boolean
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            const auto destination =
                allocate_register(1, result_domain);
            process_.operations.emplace_back(
                LogicalNot{destination, *source});
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
                || expression.text == "^~")) {
            const auto source_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto source =
                lower_expression(
                    expression.operands[0], source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto source_domain = register_domain(*source);
            const auto result_domain =
                source_domain == frontend::ValueDomain::Bit2
                        || source_domain
                            == frontend::ValueDomain::Boolean
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            auto operation = ReductionOperator::bit_xor;
            if (expression.text == "&"
                || expression.text == "~&") {
                operation = ReductionOperator::bit_and;
            } else if (expression.text == "|"
                       || expression.text == "~|") {
                operation = ReductionOperator::bit_or;
            }
            const auto destination =
                allocate_register(1, result_domain);
            process_.operations.emplace_back(
                Reduction{operation, destination, *source});
            if (expression.text.starts_with("~")
                || expression.text == "^~") {
                const auto inverted =
                    allocate_register(1, result_domain);
                process_.operations.emplace_back(
                    UnaryNot{inverted, destination});
                return inverted;
            }
            return destination;
        }
        if (expression.kind == ExpressionKind::Unary
            && expression.operands.size() == 1
            && (expression.text == "+"
                || expression.text == "-")) {
            if (language_ == frontend::Language::SystemVerilog2017
                && expected_type != nullptr
                && expected_type->systemverilog_scalar
                    != frontend::SystemVerilogScalarKind::None) {
                std::string error;
                const auto evaluated =
                    frontend::evaluate_systemverilog_scalar_constant(
                        expression, {}, {}, error);
                if (evaluated) {
                    const auto converted =
                        frontend::convert_systemverilog_scalar_constant(
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
                    process_.operations.emplace_back(LoadConstant{
                        destination,
                        PackedLogic4::from_aval_bval(
                            expected_width, converted->bits, 0)});
                    return destination;
                }
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.operands[0].kind
                    == ExpressionKind::IntegerLiteral) {
                std::string error;
                const auto value =
                    evaluate_constant_expression(
                        expression, {}, error);
                if (value
                    && *value
                        >= std::numeric_limits<std::int32_t>::min()
                    && *value
                        <= std::numeric_limits<std::int32_t>::max()) {
                    const auto destination = allocate_register(
                        32, frontend::ValueDomain::Integer);
                    process_.operations.emplace_back(
                        LoadConstant{
                            destination, integer_value(*value)});
                    return destination;
                }
            }
            const auto source_width =
                language_ == frontend::Language::SystemVerilog2017
                    ? std::max(
                          expected_width,
                          infer_width(expression.operands[0])
                              .value_or(expected_width))
                    : infer_width(expression.operands[0])
                          .value_or(expected_width);
            auto source =
                lower_expression(
                    expression.operands[0], source_width);
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
                && register_domain(*source)
                    == frontend::ValueDomain::Integer) {
                const auto destination = allocate_register(
                    32, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(IntegerUnary{
                    IntegerUnaryOperator::negate,
                    destination,
                    *source});
                return destination;
            }
            const auto zero = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(LoadConstant{
                zero,
                PackedLogic4(
                    register_width(*source), Logic4::zero)});
            const auto destination = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(Binary{
                is_signed_expression(expression.operands[0])
                    ? BinaryOperator::subtract_signed
                    : BinaryOperator::subtract_unsigned,
                destination,
                zero,
                *source});
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
            const auto source_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto source = lower_expression(
                expression.operands[0], source_width);
            if (!source) {
                return std::nullopt;
            }
            if (register_domain(*source)
                    == frontend::ValueDomain::Integer) {
                const auto destination = allocate_register(
                    32, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(IntegerUnary{
                    IntegerUnaryOperator::absolute,
                    destination,
                    *source});
                return destination;
            }
            const auto zero = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(LoadConstant{
                zero,
                PackedLogic4(
                    register_width(*source), Logic4::zero)});
            const auto negated = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(Binary{
                BinaryOperator::subtract_signed,
                negated,
                zero,
                *source});
            const auto sign = allocate_register(
                1, register_domain(*source));
            process_.operations.emplace_back(Extract{
                sign,
                *source,
                static_cast<std::uint32_t>(
                    register_width(*source) - 1U),
                1});
            const auto destination = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(ConditionalSelect{
                destination,
                sign,
                negated,
                *source});
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
            process_.operations.emplace_back(UnaryNot{destination, *source});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
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
                SignalEvent{destination, signal->second});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-095",
                    "'last_value object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto& info = design_.signal_info_[signal->second];
            const auto destination =
                allocate_register(info.width, info.source_domain);
            process_.operations.emplace_back(
                SignalLastValue{destination, signal->second});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
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
                SignalLastEvent{destination, signal->second});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
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
                SignalLastActive{destination, signal->second});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
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
                SignalDriving{destination, signal->second});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-VHATTR-003",
                    "'driving_value object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto& info = design_.signal_info_[signal->second];
            const auto destination =
                allocate_register(info.width, info.source_domain);
            process_.operations.emplace_back(
                SignalDrivingValue{destination, signal->second});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-097",
                    "'stable object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            runtime::SimulationTick duration{};
            if (expression.operands.size() == 2) {
                std::string error;
                const auto value = evaluate_constant_expression(
                    expression.operands[1], {}, error);
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
                    ReadSignal{destination, *derived});
                return destination;
            }
            const auto event = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                SignalEvent{event, signal->second});
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                UnaryNot{destination, event});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-VHATTR-005",
                    "'quiet object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            runtime::SimulationTick duration{};
            if (expression.operands.size() == 2) {
                std::string error;
                const auto value = evaluate_constant_expression(
                    expression.operands[1], {}, error);
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
                    ReadSignal{destination, *derived});
                return destination;
            }
            const auto active = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                SignalActive{active, signal->second});
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                UnaryNot{destination, active});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
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
                SignalActive{destination, signal->second});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
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
                ReadSignal{destination, *derived});
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
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-VHATTR-007",
                    "'delayed object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            runtime::SimulationTick duration{};
            if (expression.operands.size() == 2) {
                std::string error;
                const auto value = evaluate_constant_expression(
                    expression.operands[1], {}, error);
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
            const auto destination =
                allocate_register(info.width, info.source_domain);
            process_.operations.emplace_back(
                ReadSignal{destination, *derived});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && (expression.text == "'range"
                || expression.text == "'reverse_range")) {
            const auto* scalar_prefix =
                !expression.operands.empty()
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
            const auto range =
                vhdl_array_attribute_range(
                    expression, true);
            if (!range) {
                return std::nullopt;
            }
            if (expression.text == "'ascending") {
                const auto destination = allocate_register(
                    1, frontend::ValueDomain::Boolean);
                process_.operations.emplace_back(LoadConstant{
                    destination,
                    PackedLogic4(
                        1,
                        range->descending
                            ? Logic4::zero
                            : Logic4::one)});
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
                const auto* type =
                    vhdl_array_attribute_prefix_type(expression);
                const auto dimension = expression.operands.size() == 2
                    ? static_integer_value(expression.operands[1]).value_or(1)
                    : std::int64_t{1};
                const bool null_array = type != nullptr && type->vhdl_array
                    && dimension > 0
                    && static_cast<std::uint64_t>(dimension)
                        <= type->vhdl_array->dimensions.size()
                    && type->vhdl_array->dimensions[
                        static_cast<std::size_t>(dimension - 1)].null;
                const auto width =
                    null_array ? std::uint64_t{0} : range->width();
                if (width
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max())) {
                    report(
                        "FSIM-ELAB-VHARRAYATTR-004",
                        expression.text
                            + " result is outside the bounded 32-bit "
                              "integer range",
                        expression.span);
                    return std::nullopt;
                }
                result = static_cast<std::int64_t>(width);
            }
            if (result < std::numeric_limits<std::int32_t>::min()
                || result
                    > std::numeric_limits<std::int32_t>::max()) {
                report(
                    "FSIM-ELAB-VHARRAYATTR-004",
                    expression.text
                        + " result is outside the bounded 32-bit "
                          "integer range",
                    expression.span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant{
                destination,
                integer_value(
                    static_cast<std::int32_t>(result))});
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
            const auto source_width =
                infer_width(expression.operands.front())
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
            operation.maximum_domain_values = std::size_t{1} << 20U;
            struct CopyOut {
                const Expression* expression{};
                const frontend::Type* type{};
                RegisterId value{};
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
                if (!width || *width == 0 || *width > 64) {
                    report(
                        "FSIM-ELAB-SVRAND-004",
                        "std::randomize packed locals require a width in "
                        "1..64",
                        operand.span);
                    return std::nullopt;
                }
                const auto target = lower_expression(operand, *width);
                if (!target) return std::nullopt;
                ScopeRandomizeTarget retained;
                retained.target = *target;
                retained.canonical_identity = process_.name + "::"
                    + operand.text;
                retained.width = static_cast<std::uint32_t>(*width);
                retained.signed_value = type.is_signed;
                retained.nominal_type = !type.named_type.empty()
                    ? type.named_type : type.spelling;
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
                copy_outs.push_back({&operand, &type, *target});
            }
            const auto destination = operation.destination;
            process_.operations.emplace_back(std::move(operation));
            for (std::size_t index = 0; index < copy_outs.size(); ++index) {
                lower_callable_copy_out(
                    *copy_outs[index].expression,
                    *copy_outs[index].type,
                    copy_outs[index].value,
                    {}, {}, false, false,
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
                RandomValue{
                    destination,
                    urandom
                        ? RandomKind::urandom
                        : RandomKind::random,
                    std::nullopt,
                    std::nullopt});
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
            const auto maximum =
                lower_expression(expression.operands[0], 32);
            if (!maximum) {
                return std::nullopt;
            }
            std::optional<RegisterId> minimum;
            if (expression.operands.size() == 2) {
                minimum =
                    lower_expression(expression.operands[1], 32);
                if (!minimum) {
                    return std::nullopt;
                }
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                RandomValue{
                    destination,
                    RandomKind::urandom_range,
                    *maximum,
                    minimum});
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
            const auto source_width =
                infer_width(expression.operands.front())
                    .value_or(expected_width);
            const auto source = lower_expression(
                expression.operands.front(), source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto self_equal = allocate_register(
                1, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(Binary{
                BinaryOperator::equal,
                self_equal,
                *source,
                *source});
            const auto unknown = allocate_register(
                1, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(LoadConstant{
                unknown, PackedLogic4(1, Logic4::x)});
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary{
                BinaryOperator::case_equal,
                destination,
                self_equal,
                unknown});
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
            const auto operand_width =
                infer_width(expression.operands.front());
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
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(*operand_width, 32)});
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
                const auto dimension =
                    constant_index(expression.operands[1]);
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
            const auto operand_width =
                infer_width(expression.operands.front());
            const auto range =
                operand_width
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
                result =
                    range->left >= range->right ? 1 : -1;
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
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(
                    static_cast<std::uint32_t>(result), 32)});
            return destination;
        }
        return ExpressionAttempt{};
    }

} // namespace fsim::elaboration

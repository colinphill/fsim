// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include "fsim/frontend/input_format.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;
namespace {

    [[nodiscard]] InputScanFormat plusarg_scan_format(
        const frontend::InputScanFormat format)
    {
        switch (format) {
        case frontend::InputScanFormat::Binary:
            return InputScanFormat::binary;
        case frontend::InputScanFormat::Octal:
            return InputScanFormat::octal;
        case frontend::InputScanFormat::Decimal:
            return InputScanFormat::decimal;
        case frontend::InputScanFormat::UnsignedDecimal:
            return InputScanFormat::unsigned_decimal;
        case frontend::InputScanFormat::Hexadecimal:
            return InputScanFormat::hexadecimal;
        case frontend::InputScanFormat::Character:
            return InputScanFormat::character;
        case frontend::InputScanFormat::String:
            return InputScanFormat::string;
        case frontend::InputScanFormat::Real:
            return InputScanFormat::real;
        }
        throw std::logic_error { "invalid plusarg input format" };
    }

    [[nodiscard]] bool plusarg_text_format(
        const frontend::InputScanFormat format) noexcept
    {
        return format == frontend::InputScanFormat::Character
            || format == frontend::InputScanFormat::String;
    }

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

} // namespace

Lowerer::ExpressionAttempt Lowerer::lower_system_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (auto floating = lower_vhdl_float_function_expression(
            expression, expected_width, expected_type);
        floating.handled) {
        return floating;
    }
    if (auto logic = lower_vhdl_logic_function_expression(
            expression, expected_width, expected_type);
        logic.handled) {
        return logic;
    }
    if (auto fixed = lower_vhdl_fixed_function_expression(
            expression, expected_width, expected_type);
        fixed.handled) {
        return fixed;
    }
    if (auto numeric = lower_vhdl_numeric_function_expression(
            expression, expected_width, expected_type);
        numeric.handled) {
        return numeric;
    }
    if (auto binary = lower_file_binary_read(expression); binary.handled) {
        return binary;
    }
    if (auto scan = lower_file_scan(expression); scan.handled) {
        return scan;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunbounded") {
        if (language_ != frontend::Language::SystemVerilog2017
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-SVCONST-001",
                "$isunbounded requires SystemVerilog and exactly one "
                "constant argument",
                expression.span);
            return std::nullopt;
        }
        std::string error;
        const auto value = evaluate_systemverilog_constant_expression(
            expression, { }, { }, error);
        const auto integer = value ? value->integer_value()
                                   : std::optional<std::int64_t> { };
        if (!integer) {
            report(
                "FSIM-ELAB-SVCONST-001",
                "cannot evaluate $isunbounded at compile time: " + error,
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination, unsigned_value(*integer != 0 ? 1 : 0, 1) });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$sampled"
            || expression.text == "$rose"
            || expression.text == "$fell"
            || expression.text == "$stable"
            || expression.text == "$changed"
            || expression.text == "$past"
            || expression.text == "$past_gclk"
            || expression.text == "$rose_gclk"
            || expression.text == "$fell_gclk"
            || expression.text == "$stable_gclk"
            || expression.text == "$changed_gclk"
            || expression.text == "$future_gclk"
            || expression.text == "$rising_gclk"
            || expression.text == "$falling_gclk"
            || expression.text == "$steady_gclk"
            || expression.text == "$changing_gclk")) {
        const bool past = expression.text == "$past";
        const bool global = expression.text.ends_with("_gclk");
        const auto maximum_arity = global ? 1U : past ? 4U
                                                      : 2U;
        const bool named = std::ranges::any_of(
            expression.call_argument_names,
            [](const std::string& name) { return !name.empty(); });
        if (language_ != frontend::Language::SystemVerilog2017
            || named || expression.operands.empty()
            || expression.operands.size() > maximum_arity
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVSAMPLE-001",
                expression.text
                    + " currently requires one direct packed signal"
                    + (past
                            ? ", optional constant positive depth, direct gate, and direct clocking event"
                            : " and an optional direct clocking event"),
                expression.span);
            return std::nullopt;
        }
        const auto signal = signals_.find(expression.operands.front().text);
        const auto width = infer_width(expression.operands.front());
        if (signal == signals_.end() || !width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-SVSAMPLE-001",
                expression.text
                    + " argument must be a visible, statically sized packed signal",
                expression.operands.front().span);
            return std::nullopt;
        }
        std::uint32_t ticks { 1U };
        if (past && expression.operands.size() >= 2U
            && expression.operands[1].kind != ExpressionKind::Invalid) {
            std::string error;
            const auto value = evaluate_systemverilog_constant_expression(
                expression.operands[1], { }, { }, error);
            const auto integer = value ? value->integer_value()
                                       : std::optional<std::int64_t> { };
            if (!integer || *integer <= 0
                || static_cast<std::uint64_t>(*integer)
                    > 4096U) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    "$past depth must be a positive elaboration-time constant no greater than 4096: "
                        + error,
                    expression.operands[1].span);
                return std::nullopt;
            }
            ticks = static_cast<std::uint32_t>(*integer);
        }
        std::optional<SignalId> gate;
        if (past && expression.operands.size() >= 3U
            && expression.operands[2].kind != ExpressionKind::Invalid) {
            if (expression.operands[2].kind != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    "$past gating currently requires one direct packed signal",
                    expression.operands[2].span);
                return std::nullopt;
            }
            const auto found = signals_.find(expression.operands[2].text);
            const auto gate_width = infer_width(expression.operands[2]);
            if (found == signals_.end() || !gate_width || *gate_width != 1U) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    "$past gating signal must be visible and scalar",
                    expression.operands[2].span);
                return std::nullopt;
            }
            gate = found->second;
        }
        const auto clock_index = past ? 3U : 1U;
        std::optional<SignalId> clock;
        SampledClockEdge clock_edge { SampledClockEdge::any };
        if (expression.operands.size() > clock_index
            && expression.operands[clock_index].kind
                != ExpressionKind::Invalid) {
            const auto& event = expression.operands[clock_index];
            if (event.kind != ExpressionKind::Call
                || event.text != "@sv-clocking-event"
                || event.operands.size() != 1U
                || event.operands.front().kind
                    != ExpressionKind::Identifier
                || event.call_result_width
                    > static_cast<std::uint64_t>(frontend::EdgeKind::Negative)) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    expression.text
                        + " clocking event must name one direct signal",
                    event.span);
                return std::nullopt;
            }
            const auto found = signals_.find(event.operands.front().text);
            const auto clock_width = infer_width(event.operands.front());
            if (found == signals_.end() || !clock_width || *clock_width != 1U) {
                report(
                    "FSIM-ELAB-SVSAMPLE-001",
                    expression.text
                        + " clocking signal must be visible and scalar",
                    event.span);
                return std::nullopt;
            }
            clock = found->second;
            clock_edge = event.call_result_width
                    == static_cast<std::uint64_t>(frontend::EdgeKind::Positive)
                ? SampledClockEdge::positive
                : event.call_result_width
                    == static_cast<std::uint64_t>(frontend::EdgeKind::Negative)
                ? SampledClockEdge::negative
                : SampledClockEdge::any;
        }
        const auto kind = expression.text == "$sampled"
            ? SignalReadKind::sampled
            : expression.text == "$rose"
                || expression.text == "$rose_gclk"
            ? SignalReadKind::rose
            : expression.text == "$fell"
                || expression.text == "$fell_gclk"
            ? SignalReadKind::fell
            : expression.text == "$stable"
                || expression.text == "$stable_gclk"
            ? SignalReadKind::stable
            : expression.text == "$changed"
                || expression.text == "$changed_gclk"
            ? SignalReadKind::changed
            : expression.text == "$future_gclk"
            ? SignalReadKind::future
            : expression.text == "$rising_gclk"
            ? SignalReadKind::rising
            : expression.text == "$falling_gclk"
            ? SignalReadKind::falling
            : expression.text == "$steady_gclk"
            ? SignalReadKind::steady
            : expression.text == "$changing_gclk"
            ? SignalReadKind::changing
            : SignalReadKind::past;
        const auto result_width = kind == SignalReadKind::sampled
                || kind == SignalReadKind::past
                || kind == SignalReadKind::future
            ? *width
            : 1U;
        const auto result_domain = kind == SignalReadKind::sampled
                || kind == SignalReadKind::past
                || kind == SignalReadKind::future
            ? design_.signal_info_[signal->second].source_domain
            : frontend::ValueDomain::Bit2;
        const auto destination = allocate_register(
            result_width, result_domain);
        process_.operations.emplace_back(ReadSignal {
            destination, signal->second, kind, ticks,
            clock, clock_edge, gate });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$get_coverage"
            || expression.text == "$get_inst_coverage")) {
        if (language_ != frontend::Language::SystemVerilog2017
            || !expression.operands.empty()) {
            report(
                "FSIM-ELAB-SVCOV-001",
                expression.text
                    + " requires SystemVerilog and no arguments",
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(CoverageQuery {
            destination,
            expression.text == "$get_inst_coverage"
                ? CoverageQueryKind::overall_instance
                : CoverageQueryKind::overall_type });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$system") {
        if (language_ != frontend::Language::SystemVerilog2017
            || expression.operands.size() > 1U
            || (!expression.operands.empty()
                && !is_string_expression(expression.operands.front()))) {
            report(
                "FSIM-ELAB-SVSYS-001",
                "$system requires SystemVerilog and zero or one command string",
                expression.span);
            return std::nullopt;
        }
        const auto command = expression.operands.empty()
            ? std::optional<StringRegisterId> { }
            : lower_string_expression(expression.operands.front());
        if (!expression.operands.empty() && !command) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(SystemCommand {
            command,
            destination });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$q_full") {
        const bool named = std::ranges::any_of(
            expression.call_argument_names,
            [](const std::string& name) { return !name.empty(); });
        if (language_ != frontend::Language::SystemVerilog2017
            || named || expression.operands.size() != 2U) {
            report(
                "FSIM-ELAB-SVQUEUE-001",
                "$q_full requires a 32-bit integer queue ID and writable integer status",
                expression.span);
            return std::nullopt;
        }
        auto status_target = capture_callable_copy_out_target(
            expression.operands[1],
            "@q_full_status_target_"
                + std::to_string(process_.operations.size()));
        if (!status_target)
            return std::nullopt;
        auto queue_id = lower_expression(expression.operands[0], 32U);
        if (!queue_id)
            return std::nullopt;
        if (register_width(*queue_id) != 32U) {
            *queue_id = resize_register(
                *queue_id, 32U,
                is_signed_expression(expression.operands[0]));
        }
        const auto status = allocate_register(
            32U, frontend::ValueDomain::Integer);
        const auto result = allocate_register(
            32U, frontend::ValueDomain::Integer);
        StochasticQueueOperation operation;
        operation.kind = StochasticQueueKind::full;
        operation.queue_id = *queue_id;
        operation.status = status;
        operation.result = result;
        process_.operations.emplace_back(std::move(operation));
        frontend::Type integer_type;
        integer_type.spelling = "integer";
        integer_type.domain = frontend::ValueDomain::Integer;
        integer_type.packed_range = frontend::PackedRange { 31, 0, true };
        integer_type.is_signed = true;
        lower_callable_copy_out(
            *status_target,
            integer_type,
            status,
            0,
            0,
            false,
            false,
            "@q_full_status_" + std::to_string(process_.operations.size()));
        return result;
    }
    if (expression.kind == ExpressionKind::Call) {
        const auto time_function = [&]()
            -> std::optional<runtime::SystemVerilogTimeFunction> {
            if (expression.text == "$time")
                return runtime::SystemVerilogTimeFunction::Time;
            if (expression.text == "$stime")
                return runtime::SystemVerilogTimeFunction::Stime;
            if (expression.text == "$realtime")
                return runtime::SystemVerilogTimeFunction::Realtime;
            return std::nullopt;
        }();
        if (time_function) {
            if (language_ != frontend::Language::SystemVerilog2017
                || !expression.operands.empty()) {
                report(
                    "FSIM-ELAB-SVTIME-003",
                    expression.text
                        + " requires SystemVerilog and no arguments",
                    expression.span);
                return std::nullopt;
            }
            using MathFunction = runtime::SystemVerilogMathFunction;
            const auto function
                = *time_function == runtime::SystemVerilogTimeFunction::Time
                ? MathFunction::Time
                : *time_function == runtime::SystemVerilogTimeFunction::Stime
                ? MathFunction::Stime
                : MathFunction::Realtime;
            const auto result_width
                = function == MathFunction::Stime ? 32U : 64U;
            const auto destination = allocate_register(
                result_width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(SystemVerilogMath {
                .function = function,
                .destination = destination,
                .time_unit_femtoseconds
                = scalar_context_.time_unit_femtoseconds,
                .time_precision_femtoseconds
                = scalar_context_.time_precision_femtoseconds });
            return destination;
        }
        const auto function = systemverilog_math_function(expression.text);
        if (function) {
            const auto arity = binary_math_function(*function) ? 2U : 1U;
            if (language_ != frontend::Language::SystemVerilog2017
                || expression.operands.size() != arity) {
                report(
                    "FSIM-ELAB-SVMATH-001",
                    expression.text
                        + " requires SystemVerilog and "
                        + std::to_string(arity)
                        + (arity == 1U
                                ? " numeric operand"
                                : " numeric operands"),
                    expression.span);
                return std::nullopt;
            }
            struct Operand {
                RegisterId id { };
                std::uint32_t width { };
                frontend::SystemVerilogScalarKind kind {
                    frontend::SystemVerilogScalarKind::None
                };
                bool is_signed { };
            };
            const auto lower_operand = [&](const Expression& operand)
                -> std::optional<Operand> {
                const frontend::Type* direct = nullptr;
                if (operand.kind == ExpressionKind::Identifier) {
                    direct = object_type(operand.text);
                } else if (operand.kind == ExpressionKind::Call) {
                    if (const auto* callable = visible_function(operand.text)) {
                        direct = &callable->return_type;
                    }
                }
                auto kind = direct != nullptr
                    ? direct->systemverilog_scalar
                    : operand.systemverilog_scalar_kind;
                if (kind == frontend::SystemVerilogScalarKind::None
                    && operand.kind == ExpressionKind::Unary
                    && operand.operands.size() == 1U) {
                    kind = operand.operands.front()
                               .systemverilog_scalar_kind;
                }
                const auto width = kind
                        == frontend::SystemVerilogScalarKind::ShortReal
                    ? std::optional<std::size_t> { 32U }
                    : kind != frontend::SystemVerilogScalarKind::None
                    ? std::optional<std::size_t> { 64U }
                    : infer_width(operand);
                if (!width || *width == 0
                    || *width > std::numeric_limits<std::uint32_t>::max()
                    || kind == frontend::SystemVerilogScalarKind::Chandle) {
                    report(
                        "FSIM-ELAB-SVMATH-002",
                        expression.text
                            + " requires a statically sized packed integral, real, shortreal, realtime, or time operand",
                        operand.span);
                    return std::nullopt;
                }
                frontend::Type literal_type;
                if (direct == nullptr
                    && kind != frontend::SystemVerilogScalarKind::None) {
                    literal_type.spelling = kind
                            == frontend::SystemVerilogScalarKind::ShortReal
                        ? "shortreal"
                        : kind == frontend::SystemVerilogScalarKind::Time
                        ? "time"
                        : "real";
                    literal_type.domain = frontend::ValueDomain::Bit2;
                    literal_type.packed_range = frontend::PackedRange {
                        static_cast<std::int64_t>(*width - 1U), 0, true
                    };
                    literal_type.systemverilog_scalar = kind;
                    direct = &literal_type;
                }
                const auto lowered = lower_expression(
                    operand, *width, direct);
                if (!lowered)
                    return std::nullopt;
                return Operand {
                    *lowered,
                    static_cast<std::uint32_t>(*width),
                    kind,
                    is_signed_expression(operand)
                };
            };
            const auto first = lower_operand(expression.operands[0]);
            const auto second = arity == 2U
                ? lower_operand(expression.operands[1])
                : std::optional<Operand> { };
            if (!first || (arity == 2U && !second)) {
                return std::nullopt;
            }
            using Function = runtime::SystemVerilogMathFunction;
            const bool first_real = first->kind
                    == frontend::SystemVerilogScalarKind::ShortReal
                || first->kind == frontend::SystemVerilogScalarKind::Real
                || first->kind
                    == frontend::SystemVerilogScalarKind::Realtime;
            const bool valid_profile = *function == Function::Rtoi
                ? first_real
                : *function == Function::Itor
                ? first->kind
                        == frontend::SystemVerilogScalarKind::None
                    && first->width == 32U
                : *function == Function::BitsToReal
                ? first->kind
                        == frontend::SystemVerilogScalarKind::None
                    && first->width == 64U
                : *function == Function::RealToBits
                ? first->kind == frontend::SystemVerilogScalarKind::Real
                    || first->kind
                        == frontend::SystemVerilogScalarKind::Realtime
                : *function == Function::BitsToShortReal
                ? first->kind
                        == frontend::SystemVerilogScalarKind::None
                    && first->width == 32U
                : *function == Function::ShortRealToBits
                ? first->kind
                    == frontend::SystemVerilogScalarKind::ShortReal
                : true;
            if (!valid_profile) {
                report(
                    "FSIM-ELAB-SVMATH-002",
                    expression.text
                        + " operand type does not match its IEEE 1800 scalar profile",
                    expression.operands[0].span);
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
                first->is_signed,
                second ? second->is_signed : false });
            return destination;
        }
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$test$plusargs"
            || expression.text == "$value$plusargs")) {
        const bool value_query = expression.text == "$value$plusargs";
        const auto expected_operands = value_query ? 2U : 1U;
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != expected_operands
            || !is_string_expression(expression.operands[0])) {
            report(
                "FSIM-ELAB-SVCLI-001",
                expression.text
                    + (value_query
                            ? " requires a format string and writable target"
                            : " requires one string expression"),
                expression.span);
            return std::nullopt;
        }
        if (!value_query) {
            const auto query = lower_string_expression(expression.operands[0]);
            if (!query)
                return std::nullopt;
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                PlusArgSelect { destination, *query, std::nullopt });
            return destination;
        }
        const auto& format_expression = expression.operands[0];
        if (format_expression.kind != ExpressionKind::StringLiteral
            || !format_expression.decoded_string) {
            report(
                "FSIM-ELAB-SVCLI-002",
                "$value$plusargs currently requires a literal format string",
                format_expression.span);
            return std::nullopt;
        }
        const auto parsed = frontend::parse_input_format(
            *format_expression.decoded_string);
        if (!parsed.valid || parsed.conversions.size() != 1U
            || parsed.conversions.front().suppress) {
            report(
                "FSIM-ELAB-SVCLI-002",
                "$value$plusargs format must contain exactly one assignment conversion",
                format_expression.span);
            return std::nullopt;
        }
        const auto& target = expression.operands[1];
        if (target.kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVCLI-003",
                "$value$plusargs target must be a direct writable packed or string variable",
                target.span);
            return std::nullopt;
        }
        const auto& parsed_conversion = parsed.conversions.front();
        InputScanConversion conversion;
        conversion.prefix = parsed_conversion.prefix;
        conversion.format = plusarg_scan_format(parsed_conversion.format);
        conversion.maximum_characters = parsed_conversion.maximum_characters;
        const auto* target_type = object_type(target.text);
        const auto scalar_kind = target_type == nullptr
            ? frontend::SystemVerilogScalarKind::None
            : target_type->systemverilog_scalar;
        const bool real_target = scalar_kind == frontend::SystemVerilogScalarKind::ShortReal
            || scalar_kind == frontend::SystemVerilogScalarKind::Real
            || scalar_kind == frontend::SystemVerilogScalarKind::Realtime;
        const bool scalar_format_matches = scalar_kind == frontend::SystemVerilogScalarKind::None
            ? parsed_conversion.format != frontend::InputScanFormat::Real
            : real_target
            ? parsed_conversion.format == frontend::InputScanFormat::Real
            : scalar_kind == frontend::SystemVerilogScalarKind::Time
            ? parsed_conversion.format == frontend::InputScanFormat::Decimal
                || parsed_conversion.format
                    == frontend::InputScanFormat::UnsignedDecimal
                || parsed_conversion.format
                    == frontend::InputScanFormat::Real
            : scalar_kind == frontend::SystemVerilogScalarKind::Chandle
            ? parsed_conversion.format
                == frontend::InputScanFormat::Hexadecimal
            : false;
        if (!scalar_format_matches) {
            report(
                "FSIM-ELAB-SVCLI-003",
                "$value$plusargs conversion is incompatible with the target type",
                target.span);
            return std::nullopt;
        }
        if (const auto local = string_locals_.find(target.text);
            local != string_locals_.end()) {
            if (!plusarg_text_format(parsed_conversion.format)) {
                report(
                    "FSIM-ELAB-SVCLI-003",
                    "$value$plusargs numeric conversion requires a packed target",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::string_register,
                local->second, 1U, false
            };
        } else if (const auto object = string_objects_.find(target.text);
            object != string_objects_.end()) {
            if (read_only_string_objects_.contains(object->second)
                || !plusarg_text_format(parsed_conversion.format)) {
                report(
                    "FSIM-ELAB-SVCLI-003",
                    "$value$plusargs string target is read-only or incompatible",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::string_object,
                object->second, 1U, false
            };
        } else if (const auto packed_local = locals_.find(target.text);
            packed_local != locals_.end()) {
            const auto width = register_width(packed_local->second);
            if (width == 0
                || width > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVCLI-003",
                    "$value$plusargs target width is not representable by SimIR metadata",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::packed_register,
                packed_local->second,
                static_cast<std::uint32_t>(width),
                scalar_kind != frontend::SystemVerilogScalarKind::None
                    || is_two_state_domain(
                        register_domain(packed_local->second)),
                scalar_kind
            };
        } else if (const auto signal = signals_.find(target.text);
            signal != signals_.end()
            && !read_only_signals_.contains(signal->second)) {
            const auto width = design_.signal_info_[signal->second].width;
            if (width == 0
                || width > std::numeric_limits<std::uint32_t>::max()
                || target_type == nullptr) {
                report(
                    "FSIM-ELAB-SVCLI-003",
                    "$value$plusargs packed signal target has no representable type",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::packed_signal,
                signal->second,
                static_cast<std::uint32_t>(width),
                scalar_kind != frontend::SystemVerilogScalarKind::None
                    || is_two_state_domain(target_type->domain),
                scalar_kind
            };
        } else {
            report(
                "FSIM-ELAB-SVCLI-003",
                "$value$plusargs target is unknown or read-only",
                target.span);
            return std::nullopt;
        }
        const auto query = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant {
            query, parsed_conversion.prefix });
        const auto selected = allocate_string_register();
        const auto found = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            PlusArgSelect { found, query, selected });
        const auto scanned = allocate_register(
            32, frontend::ValueDomain::Bit2);
        const auto success = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(FileScan {
            scanned,
            0,
            selected,
            true,
            { std::move(conversion) },
            parsed.trailing_text,
            false,
            success,
            false });
        return resize_register(success, 32, false);
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "@stream-left"
            || expression.text == "@stream-right")) {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() < 2) {
            report(
                "FSIM-ELAB-SVEXPR-001",
                "streaming concatenation requires SystemVerilog and "
                "at least one packed operand",
                expression.span);
            return std::nullopt;
        }
        const auto slice_size = static_integer_value(
            expression.operands.front());
        if (!slice_size || *slice_size <= 0
            || static_cast<std::uint64_t>(*slice_size)
                > std::numeric_limits<std::size_t>::max()) {
            report(
                "FSIM-ELAB-SVEXPR-002",
                "streaming concatenation requires a positive locally "
                "constant host-addressable slice size",
                expression.operands.front().span);
            return std::nullopt;
        }
        std::vector<Expression> stream_operands;
        stream_operands.reserve(expression.operands.size() - 1);
        for (std::size_t index = 1;
            index < expression.operands.size(); ++index) {
            if (is_container_expression(
                    expression.operands[index])) {
                report(
                    "FSIM-ELAB-SVEXPR-003",
                    "streaming concatenation supports only fixed-width "
                    "packed integral operands",
                    expression.operands[index].span);
                return std::nullopt;
            }
            stream_operands.push_back(
                expression.operands[index]);
        }
        const Expression ordinary_stream {
            ExpressionKind::Concatenation,
            "concat",
            std::move(stream_operands),
            expression.span
        };
        const auto width = infer_width(ordinary_stream);
        if (!width || *width == 0) {
            report(
                "FSIM-ELAB-SVEXPR-003",
                "streaming concatenation requires a statically known "
                "nonempty packed result width",
                expression.span);
            return std::nullopt;
        }
        const auto source = lower_expression(
            ordinary_stream, *width);
        if (!source) {
            return std::nullopt;
        }
        if (expression.text == "@stream-right"
            || static_cast<std::size_t>(*slice_size) >= *width) {
            return *source;
        }
        std::vector<RegisterId> slices;
        for (std::size_t offset = 0; offset < *width;) {
            const auto chunk = std::min(
                static_cast<std::size_t>(*slice_size),
                *width - offset);
            const auto slice = allocate_register(
                chunk, register_domain(*source));
            process_.operations.emplace_back(Extract {
                slice,
                *source,
                static_cast<std::uint32_t>(offset),
                static_cast<std::uint32_t>(chunk) });
            slices.push_back(slice);
            offset += chunk;
        }
        const auto destination = allocate_register(
            *width, register_domain(*source));
        process_.operations.emplace_back(Concatenate {
            destination,
            std::move(slices),
            static_cast<std::uint32_t>(*width) });
        return destination;
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && expression.text == "endfile") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHFILE-009",
                "endfile requires exactly one whole VHDL file object",
                expression.span);
            return std::nullopt;
        }
        const auto& file = expression.operands.front();
        const auto local = locals_.find(file.text);
        const auto* type = object_type(file.text);
        if (local == locals_.end() || type == nullptr
            || !type->vhdl_file) {
            report(
                "FSIM-ELAB-VHFILE-009",
                "unknown VHDL file object '" + file.text + "'",
                file.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(FileEndOfFile {
            destination, local->second, true });
        return destination;
    }
    const auto lower_handle =
        [&](const Expression& handle)
        -> std::optional<RegisterId> {
        const bool integer_handle = is_file_handle_expression(handle)
            || (handle.kind == ExpressionKind::Call
                && (handle.text == "$fopen"
                    || handle.text == "$fgets"
                    || handle.text == "$fgetc"
                    || handle.text == "$ungetc"
                    || handle.text == "$feof"
                    || handle.text == "$ferror"
                    || handle.text == "$fscanf"
                    || handle.text == "$sscanf"
                    || handle.text == "$fread"
                    || handle.text == "$fseek"
                    || handle.text == "$ftell"
                    || handle.text == "$rewind"));
        if (!integer_handle) {
            report(
                "FSIM-ELAB-SVFILE-001",
                "a file handle must be a 32-bit integer expression",
                handle.span);
            return std::nullopt;
        }
        auto value = lower_expression(handle, 32);
        if (value && register_width(*value) != 32) {
            *value = resize_register(
                *value, 32, is_signed_expression(handle));
        }
        return value;
    };
    struct FileTextTarget {
        FileTextTargetKind kind { FileTextTargetKind::string_register };
        std::uint32_t id { };
        std::uint32_t width { 1 };
        std::optional<StringObjectId> string_object;
    };
    const auto text_target =
        [&](const Expression& target) -> std::optional<FileTextTarget> {
        if (target.kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVFILE-002",
                "a file read/error target must be a whole writable packed or "
                "string variable",
                target.span);
            return std::nullopt;
        }
        if (const auto local = string_locals_.find(target.text);
            local != string_locals_.end()) {
            return FileTextTarget {
                FileTextTargetKind::string_register, local->second, 1, { }
            };
        }
        if (const auto object = string_objects_.find(target.text);
            object != string_objects_.end()) {
            if (read_only_string_objects_.contains(object->second)) {
                report(
                    "FSIM-ELAB-SVPORT-011",
                    "an input mutable string port is read-only",
                    target.span);
                return std::nullopt;
            }
            return FileTextTarget {
                FileTextTargetKind::string_register,
                allocate_string_register(), 1, object->second
            };
        }
        if (const auto local = locals_.find(target.text);
            local != locals_.end()) {
            const auto width = register_width(local->second);
            if (width != 0
                && width <= std::numeric_limits<std::uint32_t>::max()) {
                return FileTextTarget {
                    FileTextTargetKind::packed_register, local->second,
                    static_cast<std::uint32_t>(width), { }
                };
            }
        }
        if (const auto signal = signals_.find(target.text);
            signal != signals_.end()
            && !read_only_signals_.contains(signal->second)) {
            const auto width = design_.signal_info_[signal->second].width;
            if (width != 0
                && width <= std::numeric_limits<std::uint32_t>::max()) {
                return FileTextTarget {
                    FileTextTargetKind::packed_signal, signal->second,
                    static_cast<std::uint32_t>(width), { }
                };
            }
        }
        report(
            "FSIM-ELAB-SVFILE-002",
            "unknown, read-only, or unsupported file text target '"
                + target.text + "'",
            target.span);
        return std::nullopt;
    };
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$fopen") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.empty()
            || expression.operands.size() > 2
            || !is_string_expression(expression.operands[0])
            || (expression.operands.size() == 2
                && !is_string_expression(
                    expression.operands[1]))) {
            report(
                "FSIM-ELAB-SVFILE-003",
                "$fopen requires a Verilog byte-string filename "
                "and optional mode expression",
                expression.span);
            return std::nullopt;
        }
        const auto path = lower_string_expression(expression.operands[0]);
        std::optional<StringRegisterId> mode;
        if (expression.operands.size() == 2) {
            mode = lower_string_expression(expression.operands[1]);
        } else {
            mode = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant {
                    *mode, "\x1f"
                           "fsim-multichannel-write" });
        }
        if (!path || !mode) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileOpen { destination, *path, *mode });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$fgets") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 2) {
            report(
                "FSIM-ELAB-SVFILE-004",
                "$fgets requires a writable packed or string target and integer handle",
                expression.span);
            return std::nullopt;
        }
        const auto target = text_target(expression.operands[0]);
        const auto handle = lower_handle(expression.operands[1]);
        if (!target || !handle) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        FileReadLine operation {
            destination, *handle, target->id, 0, FileReadKind::line
        };
        operation.target_kind = target->kind;
        operation.target_width = target->width;
        process_.operations.emplace_back(operation);
        if (target->string_object) {
            process_.operations.emplace_back(
                WriteStringObject { *target->string_object, target->id });
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$fgetc") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-SVFILE-009",
                "$fgetc requires one integer handle",
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        if (!handle) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileReadLine {
                destination, *handle, 0, 0,
                FileReadKind::character });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$ungetc") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 2) {
            report(
                "FSIM-ELAB-SVFILE-010",
                "$ungetc requires a character and integer handle",
                expression.span);
            return std::nullopt;
        }
        auto character = lower_expression(expression.operands[0], 32);
        const auto handle = lower_handle(expression.operands[1]);
        if (!character || !handle) {
            return std::nullopt;
        }
        if (register_width(*character) != 32) {
            *character = resize_register(
                *character, 32,
                is_signed_expression(expression.operands[0]));
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileReadLine {
                destination, *handle, 0, *character,
                FileReadKind::unget });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$feof") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-SVFILE-005",
                "$feof requires one integer handle",
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        if (!handle) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileEndOfFile { destination, *handle });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$ferror") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 2) {
            report(
                "FSIM-ELAB-SVFILE-006",
                "$ferror requires an integer handle and writable packed or string target",
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        const auto target = text_target(expression.operands[1]);
        if (!handle || !target) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        FileErrorStatus operation { destination, *handle, target->id };
        operation.target_kind = target->kind;
        operation.target_width = target->width;
        process_.operations.emplace_back(operation);
        if (target->string_object) {
            process_.operations.emplace_back(
                WriteStringObject { *target->string_object, target->id });
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$fseek"
            || expression.text == "$ftell"
            || expression.text == "$rewind")) {
        const bool seek = expression.text == "$fseek";
        const bool named = std::ranges::any_of(
            expression.call_argument_names,
            [](const std::string& name) { return !name.empty(); });
        if (language_ == frontend::Language::Vhdl2008
            || named
            || expression.operands.size() != (seek ? 3U : 1U)) {
            report(
                "FSIM-ELAB-SVFILE-015",
                expression.text
                    + " requires an integer handle"
                    + (seek ? ", offset, and origin" : ""),
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        if (!handle)
            return std::nullopt;
        FilePosition operation;
        operation.handle = *handle;
        operation.kind = expression.text == "$fseek"
            ? FilePositionKind::seek
            : expression.text == "$ftell"
            ? FilePositionKind::tell
            : FilePositionKind::rewind;
        const auto lower_integer = [&](const std::size_t operand)
            -> std::optional<RegisterId> {
            const auto& value_expression = expression.operands[operand];
            const auto width = infer_width(value_expression);
            if (!width || *width == 0
                || is_string_expression(value_expression)
                || is_container_expression(value_expression)) {
                report(
                    "FSIM-ELAB-SVFILE-015",
                    "$fseek offset and origin must be integer expressions",
                    value_expression.span);
                return std::nullopt;
            }
            auto value = lower_expression(value_expression, 32);
            if (value && register_width(*value) != 32) {
                *value = resize_register(
                    *value, 32, is_signed_expression(value_expression));
            }
            return value;
        };
        if (seek) {
            const auto offset = lower_integer(1);
            const auto origin = lower_integer(2);
            if (!offset || !origin)
                return std::nullopt;
            operation.offset = *offset;
            operation.origin = *origin;
        }
        operation.destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(operation);
        return operation.destination;
    }
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
        const auto operand_width = infer_width(expression.operands.front());
        const auto range = operand_width
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
        process_.operations.emplace_back(LoadConstant {
            destination,
            unsigned_value(
                expression.text == "$dimensions" ? 1 : 0,
                32) });
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
        const auto source_width = infer_width(expression.operands.front())
                                      .value_or(expected_width);
        const auto source = lower_expression(
            expression.operands.front(), source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Reduction {
            expression.text == "$onehot"
                ? ReductionOperator::one_hot
                : ReductionOperator::one_hot_or_zero,
            destination,
            *source });
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
        const auto source_width = infer_width(expression.operands.front());
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
            CountOnes { destination, *source });
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
        const auto source_width = infer_width(expression.operands.front());
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
            state_mask |= static_cast<std::uint8_t>(
                std::uint8_t { 1 } << state);
        }
        const auto source = lower_expression(
            expression.operands.front(), *source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            CountBits {
                destination, *source, state_mask });
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
        if (language_ != frontend::Language::Vhdl2008) {
            if (const auto condition =
                    static_integer_value(expression.operands[0])) {
                return lower_expression(
                    expression.operands[*condition != 0 ? 1U : 2U],
                    expected_width,
                    expected_type);
            }
            const auto condition = lower_condition(
                expression.operands[0],
                "FSIM-ELAB-064",
                "conditional-expression");
            if (!condition) {
                return std::nullopt;
            }
            const auto true_width = infer_width(expression.operands[1])
                                        .value_or(expected_width);
            const auto false_width = infer_width(expression.operands[2])
                                         .value_or(expected_width);
            const auto value_width = std::max(
                expected_width,
                std::max(true_width, false_width));
            const bool result_signed = is_signed_expression(expression.operands[1])
                && is_signed_expression(expression.operands[2]);

            const auto known_one = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                known_one, unsigned_value(1, 1) });
            const auto definitely_true = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                definitely_true,
                *condition,
                known_one });
            const auto true_branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                definitely_true,
                0,
                true_branch + 1,
                UnknownBranchPolicy::when_false });

            const auto known_zero = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                known_zero, unsigned_value(0, 1) });
            const auto definitely_false = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                definitely_false,
                *condition,
                known_zero });
            const auto false_branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                definitely_false,
                0,
                false_branch + 1,
                UnknownBranchPolicy::when_false });

            auto unknown_true = lower_expression(
                expression.operands[1],
                value_width,
                expected_type);
            auto unknown_false = lower_expression(
                expression.operands[2],
                value_width,
                expected_type);
            if (!unknown_true || !unknown_false) {
                return std::nullopt;
            }
            *unknown_true = resize_register(
                *unknown_true, value_width, result_signed);
            *unknown_false = resize_register(
                *unknown_false, value_width, result_signed);
            const auto result_domain = register_domain(*condition)
                        == frontend::ValueDomain::Bit2
                    && is_two_state_domain(
                        register_domain(*unknown_true))
                    && is_two_state_domain(
                        register_domain(*unknown_false))
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            const auto destination = allocate_register(
                value_width, result_domain);
            process_.operations.emplace_back(ConditionalSelect {
                destination,
                *condition,
                *unknown_true,
                *unknown_false });
            const auto unknown_exit = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0 });

            const auto true_start = static_cast<InstructionIndex>(
                process_.operations.size());
            auto when_true = lower_expression(
                expression.operands[1],
                value_width,
                expected_type);
            if (!when_true) {
                return std::nullopt;
            }
            *when_true = resize_register(
                *when_true, value_width, result_signed);
            process_.operations.emplace_back(
                CopyRegister { destination, *when_true });
            const auto true_exit = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0 });

            const auto false_start = static_cast<InstructionIndex>(
                process_.operations.size());
            auto when_false = lower_expression(
                expression.operands[2],
                value_width,
                expected_type);
            if (!when_false) {
                return std::nullopt;
            }
            *when_false = resize_register(
                *when_false, value_width, result_signed);
            process_.operations.emplace_back(
                CopyRegister { destination, *when_false });
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());

            process_.operations[true_branch] = Branch {
                definitely_true,
                true_start,
                true_branch + 1,
                UnknownBranchPolicy::when_false
            };
            process_.operations[false_branch] = Branch {
                definitely_false,
                false_start,
                false_branch + 1,
                UnknownBranchPolicy::when_false
            };
            process_.operations[unknown_exit] = Jump { end };
            process_.operations[true_exit] = Jump { end };
            return destination;
        }
        const auto condition = lower_expression(expression.operands[0], 1);
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
        const auto value_width = infer_width(expression.operands[1])
                                     .value_or(
                                         infer_width(expression.operands[2])
                                             .value_or(expected_width));
        auto result_domain = frontend::ValueDomain::Logic4;
        if (is_integer_expression(expression.operands[1])
            && is_integer_expression(expression.operands[2])) {
            result_domain = frontend::ValueDomain::Integer;
        } else if (expected_type != nullptr
            && expected_type->domain
                != frontend::ValueDomain::Unknown) {
            result_domain = expected_type->domain;
        } else if (const auto type = vhdl_expression_type(expression)) {
            result_domain = type->domain;
        }
        const auto destination = allocate_register(value_width, result_domain);
        const auto branch_index = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            *condition, 0, 0, UnknownBranchPolicy::error });

        const auto true_start = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto when_true = lower_expression(
            expression.operands[1], value_width, expected_type);
        if (!when_true) {
            return std::nullopt;
        }
        process_.operations.emplace_back(
            CopyRegister { destination, *when_true });
        const auto true_exit = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Jump { 0 });

        const auto false_start = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto when_false = lower_expression(
            expression.operands[2], value_width, expected_type);
        if (!when_false) {
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
        process_.operations.emplace_back(
            CopyRegister { destination, *when_false });
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch_index] = Branch {
            *condition,
            true_start,
            false_start,
            UnknownBranchPolicy::error
        };
        process_.operations[true_exit] = Jump { end };
        return destination;
    }

    return ExpressionAttempt { };
}

} // namespace fsim::elaboration

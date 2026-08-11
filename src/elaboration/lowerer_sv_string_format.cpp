// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include "fsim/frontend/output_format.hpp"

namespace fsim::elaboration {
using namespace elaboration_detail;
using namespace runtime::simir;
namespace {

    [[nodiscard]] OutputFormat runtime_format(
        const frontend::OutputFormat format)
    {
        switch (format) {
        case frontend::OutputFormat::Binary:
            return OutputFormat::binary;
        case frontend::OutputFormat::Hexadecimal:
            return OutputFormat::hexadecimal;
        case frontend::OutputFormat::Octal:
            return OutputFormat::octal;
        case frontend::OutputFormat::Decimal:
            return OutputFormat::decimal;
        case frontend::OutputFormat::Character:
            return OutputFormat::character;
        case frontend::OutputFormat::String:
            return OutputFormat::string;
        case frontend::OutputFormat::RealScientific:
            return OutputFormat::real_scientific;
        case frontend::OutputFormat::RealFixed:
            return OutputFormat::real_fixed;
        case frontend::OutputFormat::RealGeneral:
            return OutputFormat::real_general;
        case frontend::OutputFormat::Hierarchy:
        case frontend::OutputFormat::Time:
            break;
        }
        throw std::logic_error { "invalid runtime string format" };
    }

    [[nodiscard]] bool consumes_value(
        const frontend::OutputFormat format) noexcept
    {
        return format != frontend::OutputFormat::Hierarchy
            && format != frontend::OutputFormat::Time;
    }

} // namespace

std::optional<StringRegisterId> Lowerer::lower_string_format(
    const std::vector<Expression>& arguments,
    const std::size_t format_index,
    const std::string_view call_name,
    const frontend::SourceSpan& span)
{
    if (language_ != frontend::Language::SystemVerilog2017
        || arguments.size() <= format_index
        || arguments[format_index].kind != ExpressionKind::StringLiteral
        || !arguments[format_index].decoded_string) {
        report(
            "FSIM-ELAB-SVSTRING-019",
            std::string { call_name }
                + " requires a literal SystemVerilog format string",
            span);
        return std::nullopt;
    }
    const auto parsed = frontend::parse_output_format(
        *arguments[format_index].decoded_string);
    if (!parsed.valid || parsed.conversions.size() > 64U
        || arguments.size() - format_index - 1U > 64U) {
        report(
            "FSIM-ELAB-SVSTRING-019",
            std::string { call_name }
                + " has an invalid or oversized bounded format",
            arguments[format_index].span);
        return std::nullopt;
    }
    const auto required = static_cast<std::size_t>(std::ranges::count_if(
        parsed.conversions,
        [](const auto& conversion) {
            return consumes_value(conversion.format);
        }));
    const auto supplied = arguments.size() - format_index - 1U;
    if (supplied < required) {
        report(
            "FSIM-ELAB-SVSTRING-019",
            std::string { call_name }
                + " format conversions require matching value arguments",
            arguments[format_index].span);
        return std::nullopt;
    }

    const auto destination = allocate_string_register();
    process_.operations.emplace_back(
        LoadStringConstant { destination, { } });
    const auto append_literal = [&](const std::string_view text) {
        if (text.empty()) {
            return;
        }
        const auto literal = allocate_string_register();
        process_.operations.emplace_back(
            LoadStringConstant { literal, std::string { text } });
        process_.operations.emplace_back(
            ConcatenateStrings { destination, { destination, literal } });
    };
    const auto append_value = [&](
                                  const frontend::ParsedOutputConversion& conversion,
                                  const Expression* value) {
        append_literal(conversion.prefix);
        if (conversion.format == frontend::OutputFormat::Hierarchy) {
            append_literal(hierarchy_);
            return true;
        }
        StringMethod operation;
        operation.source = destination;
        operation.minimum_width = conversion.minimum_width;
        operation.left_justify = conversion.left_justify;
        operation.zero_pad = conversion.zero_pad;
        operation.suppress_leading_zero = conversion.suppress_leading_zero;
        if (conversion.format == frontend::OutputFormat::Time) {
            operation.operation = StringMethodOperator::format_time;
            process_.operations.emplace_back(operation);
            return true;
        }
        if (value == nullptr) {
            return false;
        }
        if (conversion.format == frontend::OutputFormat::String
            && is_string_expression(*value)) {
            const auto source = lower_string_expression(*value);
            if (!source) {
                return false;
            }
            operation.operation = StringMethodOperator::format_string;
            operation.argument = *source;
            process_.operations.emplace_back(operation);
            return true;
        }
        const auto width = infer_width(*value).value_or(std::size_t { 32 });
        const auto* value_type = value->kind == ExpressionKind::Identifier
            ? object_type(value->text)
            : nullptr;
        const auto scalar_kind = value_type != nullptr
            ? value_type->systemverilog_scalar
            : value->systemverilog_scalar_kind;
        const bool real_scalar = scalar_kind == frontend::SystemVerilogScalarKind::ShortReal
            || scalar_kind == frontend::SystemVerilogScalarKind::Real
            || scalar_kind == frontend::SystemVerilogScalarKind::Realtime;
        const bool real_format = conversion.format == frontend::OutputFormat::RealScientific
            || conversion.format == frontend::OutputFormat::RealFixed
            || conversion.format == frontend::OutputFormat::RealGeneral;
        if ((real_scalar && !real_format)
            || (!real_scalar && real_format)
            || (scalar_kind == frontend::SystemVerilogScalarKind::Time
                && conversion.format != frontend::OutputFormat::Decimal)
            || (scalar_kind == frontend::SystemVerilogScalarKind::Chandle
                && conversion.format != frontend::OutputFormat::Hexadecimal)) {
            report(
                "FSIM-ELAB-SVSTRING-019",
                "formatted string conversion is incompatible with the value type",
                value->span);
            return false;
        }
        const auto source = lower_expression(*value, width);
        if (!source || width == 0) {
            report(
                "FSIM-ELAB-SVSTRING-019",
                "formatted string value must be a nonempty packed expression",
                value->span);
            return false;
        }
        const auto width_register = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { width_register, unsigned_value(width, 32) });
        operation.operation = StringMethodOperator::format_packed;
        operation.first = *source;
        operation.second = width_register;
        operation.format = runtime_format(conversion.format);
        operation.scalar_kind = scalar_kind;
        operation.signed_decimal = operation.format == OutputFormat::decimal
            && is_signed_expression(*value);
        process_.operations.emplace_back(operation);
        return true;
    };

    std::size_t value_index = format_index + 1U;
    for (const auto& conversion : parsed.conversions) {
        const Expression* value = consumes_value(conversion.format)
            ? &arguments[value_index++]
            : nullptr;
        if (!append_value(conversion, value)) {
            return std::nullopt;
        }
    }
    append_literal(parsed.trailing_text);
    for (; value_index < arguments.size(); ++value_index) {
        if (!append_value(
                frontend::ParsedOutputConversion {
                    frontend::OutputFormat::Decimal,
                    { }, false, 0U, false, false },
                &arguments[value_index])) {
            return std::nullopt;
        }
    }
    return destination;
}

bool Lowerer::lower_string_format_task(const Statement& statement)
{
    if (statement.task_name != "$swrite"
        && statement.task_name != "$sformat") {
        return false;
    }
    const auto named = std::ranges::any_of(
        statement.task_argument_names,
        [](const std::string& name) { return !name.empty(); });
    if (named || statement.task_arguments.size() < 2U
        || statement.task_arguments.front().kind
            != ExpressionKind::Identifier) {
        report(
            "FSIM-ELAB-SVSTRING-020",
            statement.task_name
                + " requires a direct string target and positional arguments",
            statement.span);
        return true;
    }
    const auto& target = statement.task_arguments.front();
    const auto local = string_locals_.find(target.text);
    const auto object = string_objects_.find(target.text);
    if (local == string_locals_.end()
        && object == string_objects_.end()) {
        report(
            "FSIM-ELAB-SVSTRING-020",
            statement.task_name + " target must be a writable string object",
            target.span);
        return true;
    }
    if (object != string_objects_.end()
        && read_only_string_objects_.contains(object->second)) {
        report(
            "FSIM-ELAB-SVPORT-011",
            "an input mutable string port is read-only",
            target.span);
        return true;
    }
    const auto result = lower_string_format(
        statement.task_arguments, 1U,
        statement.task_name, statement.span);
    if (!result) {
        return true;
    }
    if (local != string_locals_.end()) {
        process_.operations.emplace_back(
            CopyStringRegister { local->second, *result });
    } else {
        process_.operations.emplace_back(
            WriteStringObject { object->second, *result });
    }
    return true;
}

} // namespace fsim::elaboration

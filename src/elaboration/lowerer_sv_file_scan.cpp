// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include "fsim/frontend/input_format.hpp"

namespace fsim::elaboration {
using namespace elaboration_detail;
using namespace runtime::simir;
namespace {

    [[nodiscard]] InputScanFormat runtime_format(
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
        throw std::logic_error { "invalid input scan format" };
    }

    [[nodiscard]] bool text_format(
        const frontend::InputScanFormat format) noexcept
    {
        return format == frontend::InputScanFormat::Character
            || format == frontend::InputScanFormat::String;
    }

} // namespace

Lowerer::ExpressionAttempt Lowerer::lower_file_scan(
    const Expression& expression)
{
    if (expression.kind != ExpressionKind::Call
        || (expression.text != "$fscanf" && expression.text != "$sscanf")) {
        return { };
    }
    const bool string_source = expression.text == "$sscanf";
    const bool named = std::ranges::any_of(
        expression.call_argument_names,
        [](const std::string& name) { return !name.empty(); });
    if (language_ == frontend::Language::Vhdl2008 || named
        || expression.operands.size() < 2U
        || expression.operands[1].kind != ExpressionKind::StringLiteral
        || !expression.operands[1].decoded_string) {
        report(
            "FSIM-ELAB-SVFILE-011",
            expression.text
                + " requires a source, literal format, and positional targets",
            expression.span);
        return std::nullopt;
    }
    const auto parsed = frontend::parse_input_format(
        *expression.operands[1].decoded_string);
    const auto required = static_cast<std::size_t>(std::ranges::count_if(
        parsed.conversions,
        [](const auto& conversion) { return !conversion.suppress; }));
    if (!parsed.valid || parsed.conversions.empty()
        || parsed.conversions.size() > 64U
        || required != expression.operands.size() - 2U
        || parsed.trailing_text.size() > maximum_string_bytes
        || std::ranges::any_of(parsed.conversions, [](const auto& conversion) {
               return conversion.prefix.size() > maximum_string_bytes
                   || conversion.maximum_characters > maximum_string_bytes;
           })) {
        report(
            "FSIM-ELAB-SVFILE-011",
            expression.text
                + " has an invalid, oversized, or target-mismatched format",
            expression.operands[1].span);
        return std::nullopt;
    }

    FileScan operation;
    operation.string_source = string_source;
    if (string_source) {
        if (!is_string_expression(expression.operands[0])) {
            report(
                "FSIM-ELAB-SVFILE-011",
                "$sscanf source must be a bounded byte-string expression",
                expression.operands[0].span);
            return std::nullopt;
        }
        const auto source = lower_string_expression(expression.operands[0]);
        if (!source)
            return std::nullopt;
        operation.source = *source;
    } else {
        const auto& source = expression.operands[0];
        if (!is_file_handle_expression(source)) {
            report(
                "FSIM-ELAB-SVFILE-011",
                "$fscanf source must be a 32-bit integer file handle",
                source.span);
            return std::nullopt;
        }
        auto handle = lower_expression(source, 32);
        if (!handle)
            return std::nullopt;
        if (register_width(*handle) != 32)
            *handle = resize_register(*handle, 32, is_signed_expression(source));
        operation.handle = *handle;
    }

    std::size_t target_index = 2U;
    for (const auto& parsed_conversion : parsed.conversions) {
        InputScanConversion conversion;
        conversion.prefix = parsed_conversion.prefix;
        conversion.format = runtime_format(parsed_conversion.format);
        conversion.maximum_characters = parsed_conversion.maximum_characters;
        conversion.suppress = parsed_conversion.suppress;
        if (conversion.suppress) {
            operation.conversions.push_back(std::move(conversion));
            continue;
        }
        const auto& target = expression.operands[target_index++];
        if (target.kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVFILE-012",
                "scan targets must be direct writable packed or string variables",
                target.span);
            return std::nullopt;
        }
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
                "FSIM-ELAB-SVFILE-012",
                "scan conversion is incompatible with the scalar target type",
                target.span);
            return std::nullopt;
        }
        if (const auto local = string_locals_.find(target.text);
            local != string_locals_.end()) {
            if (!text_format(parsed_conversion.format)) {
                report(
                    "FSIM-ELAB-SVFILE-012",
                    "numeric scan conversions require a packed integral target",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::string_register, local->second, 1U, false
            };
        } else if (const auto object = string_objects_.find(target.text);
            object != string_objects_.end()) {
            if (read_only_string_objects_.contains(object->second)) {
                report(
                    "FSIM-ELAB-SVPORT-011",
                    "an input mutable string port is read-only",
                    target.span);
                return std::nullopt;
            }
            if (!text_format(parsed_conversion.format)) {
                report(
                    "FSIM-ELAB-SVFILE-012",
                    "numeric scan conversions require a packed integral target",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::string_object, object->second, 1U, false
            };
        } else if (const auto packed_local = locals_.find(target.text);
            packed_local != locals_.end()) {
            const auto width = register_width(packed_local->second);
            if (width == 0
                || width > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVFILE-012",
                    "packed scan target width is not representable by SimIR metadata",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::packed_register, packed_local->second,
                static_cast<std::uint32_t>(width),
                scalar_kind != frontend::SystemVerilogScalarKind::None
                    || is_two_state_domain(register_domain(packed_local->second)),
                scalar_kind
            };
        } else if (const auto signal = signals_.find(target.text);
            signal != signals_.end()
            && !read_only_signals_.contains(signal->second)) {
            const auto width = design_.signal_info_[signal->second].width;
            const auto* type = object_type(target.text);
            if (width == 0
                || width > std::numeric_limits<std::uint32_t>::max()
                || type == nullptr) {
                report(
                    "FSIM-ELAB-SVFILE-012",
                    "packed scan target requires a type and a width representable by "
                    "SimIR metadata",
                    target.span);
                return std::nullopt;
            }
            conversion.target = {
                InputScanTargetKind::packed_signal, signal->second,
                static_cast<std::uint32_t>(width),
                scalar_kind != frontend::SystemVerilogScalarKind::None
                    || is_two_state_domain(type->domain),
                scalar_kind
            };
        } else {
            report(
                "FSIM-ELAB-SVFILE-012",
                "scan target is unknown or read-only",
                target.span);
            return std::nullopt;
        }
        operation.conversions.push_back(std::move(conversion));
    }
    operation.trailing_text = parsed.trailing_text;
    const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
    operation.destination = destination;
    process_.operations.emplace_back(std::move(operation));
    return destination;
}

} // namespace fsim::elaboration

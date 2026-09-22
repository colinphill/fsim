// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"


#include "fsim/frontend/input_format.hpp"
#include "fsim/frontend/output_format.hpp"

namespace fsim::elaboration {
namespace {

[[nodiscard]] bool hir_file_call_name(const std::string_view name) noexcept
{
    return name == "$fopen" || name == "$fgets" || name == "$fgetc"
        || name == "$ungetc" || name == "$feof" || name == "$ferror"
        || name == "$fscanf" || name == "$sscanf" || name == "$fread"
        || name == "$fseek" || name == "$ftell" || name == "$rewind"
        || name == "$test$plusargs" || name == "$value$plusargs";
}

[[nodiscard]] InputScanFormat runtime_input_format(
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
    case frontend::InputScanFormat::Unformatted2:
        return InputScanFormat::unformatted2;
    case frontend::InputScanFormat::Unformatted4:
        return InputScanFormat::unformatted4;
    }
    throw std::logic_error { "invalid input scan format" };
}

[[nodiscard]] bool text_input_format(
    const frontend::InputScanFormat format) noexcept
{
    return format == frontend::InputScanFormat::String
        || format == frontend::InputScanFormat::Character;
}

[[nodiscard]] frontend::SystemVerilogScalarKind scalar_kind(
    const std::string_view spelling) noexcept
{
    if (spelling == "shortreal") {
        return frontend::SystemVerilogScalarKind::ShortReal;
    }
    if (spelling == "real") {
        return frontend::SystemVerilogScalarKind::Real;
    }
    if (spelling == "realtime") {
        return frontend::SystemVerilogScalarKind::Realtime;
    }
    if (spelling == "time") {
        return frontend::SystemVerilogScalarKind::Time;
    }
    if (spelling == "chandle") {
        return frontend::SystemVerilogScalarKind::Chandle;
    }
    return frontend::SystemVerilogScalarKind::None;
}

[[nodiscard]] runtime::simir::OutputFormat runtime_output_format(
    const semantic::sv::OutputFormat format) noexcept
{
    using Source = semantic::sv::OutputFormat;
    using Target = runtime::simir::OutputFormat;
    switch (format) {
    case Source::binary:
        return Target::binary;
    case Source::hexadecimal:
        return Target::hexadecimal;
    case Source::octal:
        return Target::octal;
    case Source::decimal:
        return Target::decimal;
    case Source::character:
        return Target::character;
    case Source::string:
    case Source::hierarchy:
        return Target::string;
    case Source::real_scientific:
        return Target::real_scientific;
    case Source::real_fixed:
        return Target::real_fixed;
    case Source::real_general:
        return Target::real_general;
    case Source::time:
        return Target::time;
    case Source::unformatted2:
        return Target::unformatted2;
    case Source::unformatted4:
        return Target::unformatted4;
    }
    return Target::decimal;
}

[[nodiscard]] runtime::simir::OutputFormat runtime_output_format(
    const frontend::OutputFormat format)
{
    using Source = frontend::OutputFormat;
    using Target = runtime::simir::OutputFormat;
    switch (format) {
    case Source::Binary:
        return Target::binary;
    case Source::Hexadecimal:
        return Target::hexadecimal;
    case Source::Octal:
        return Target::octal;
    case Source::Decimal:
        return Target::decimal;
    case Source::Character:
        return Target::character;
    case Source::String:
        return Target::string;
    case Source::RealScientific:
        return Target::real_scientific;
    case Source::RealFixed:
        return Target::real_fixed;
    case Source::RealGeneral:
        return Target::real_general;
    case Source::Unformatted2:
        return Target::unformatted2;
    case Source::Unformatted4:
        return Target::unformatted4;
    case Source::Hierarchy:
    case Source::Time:
        break;
    }
    throw std::logic_error { "invalid runtime string format" };
}

[[nodiscard]] bool output_conversion_consumes_value(
    const frontend::OutputFormat format) noexcept
{
    return format != frontend::OutputFormat::Hierarchy
        && format != frontend::OutputFormat::Time;
}

[[nodiscard]] std::string_view vhdl_simple_name(
    std::string_view name) noexcept
{
    const auto separator = name.find_last_of('.');
    if (separator != std::string_view::npos) {
        name.remove_prefix(separator + 1U);
    }
    return name;
}

[[nodiscard]] bool vhdl_file_procedure_name(
    const std::string_view name) noexcept
{
    const auto simple = vhdl_simple_name(name);
    return simple == "file_open" || simple == "file_close"
        || simple == "read" || simple == "write"
        || simple == "readline" || simple == "writeline";
}

} // namespace

bool Lowerer::is_hir_systemverilog_file_call(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    return expression && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call
        && hir_file_call_name(expression->systemverilog->text);
}

bool Lowerer::is_hir_vhdl_file_call(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto name = expression && expression->vhdl != nullptr
            && expression->vhdl->referenced_name
        ? expression->vhdl->referenced_name->canonical.empty()
            ? std::string_view {
                  expression->vhdl->referenced_name->spelling }
            : std::string_view {
                  expression->vhdl->referenced_name->canonical }
        : expression && expression->vhdl != nullptr
        ? std::string_view { expression->vhdl->text }
        : std::string_view { };
    if (!expression || expression->vhdl == nullptr
        || (expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::call
            && expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::index)) {
        return false;
    }
    if (vhdl_simple_name(name) == "endfile") {
        return true;
    }
    if (expression->vhdl->referenced_name
        || expression->vhdl->operands.size() != 1U) {
        return false;
    }
    const auto file = hir_referenced_declaration(
        expression->vhdl->operands.front());
    const auto declaration = file
        ? specialized_hir_unit_->find_declaration(*file)
        : std::nullopt;
    return declaration && declaration->vhdl != nullptr
        && (declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::file
            || declaration->vhdl->object_class
                == semantic::vhdl::ObjectClass::file);
}

std::optional<std::uint64_t>
Lowerer::hir_vhdl_standard_enumeration_literal(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::name) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto name = vhdl_simple_name(source.text);
    const auto predefined_type = name == "open_ok"
            || name == "status_error" || name == "name_error"
            || name == "mode_error"
        ? std::string_view { "file_open_status" }
        : name == "read_mode" || name == "write_mode"
                || name == "append_mode"
        ? std::string_view { "file_open_kind" }
        : name == "note" || name == "warning" || name == "error"
                || name == "failure"
        ? std::string_view { "severity_level" }
        : name == "right" || name == "left"
        ? std::string_view { "side" }
        : std::string_view { };
    if (source.referenced_name && source.referenced_name->selected) {
        if (predefined_type.empty()) {
            return std::nullopt;
        }
        const auto selected = *source.referenced_name->selected;
        std::optional<std::uint64_t> selected_ordinal;
        bool conflicting_owner { };
        const auto consider_type = [&](const auto& type) {
            const auto literal = std::ranges::find(
                type.enumeration_literals, selected,
                &semantic::vhdl::EnumerationLiteral::declaration);
            if (literal == type.enumeration_literals.end()) {
                return;
            }
            const auto declaration
                = specialized_hir_unit_->find_declaration(
                    type.declaration);
            const auto owner = declaration
                    && declaration->vhdl != nullptr
                ? vhdl_simple_name(declaration->vhdl->name)
                : std::string_view { };
            if (owner != predefined_type) {
                conflicting_owner = true;
                return;
            }
            if (selected_ordinal
                && *selected_ordinal != literal->ordinal) {
                conflicting_owner = true;
                return;
            }
            selected_ordinal = literal->ordinal;
        };
        for (const auto& type : specialized_hir_unit_->vhdl_types()) {
            consider_type(type);
        }
        for (const auto& type :
            specialized_hir_unit_->design().vhdl_hir.types()) {
            consider_type(type);
        }
        if (conflicting_owner || !selected_ordinal) {
            return std::nullopt;
        }
        return selected_ordinal;
    } else if (source.referenced_name
        && !source.referenced_name->overloads.empty()) {
        return std::nullopt;
    }
    // These predefined enumeration literals are represented without an
    // owning declaration when their standard-package profile is implicit.
    // Keep their language-defined ordinals available to all HIR lowering,
    // including severity_level variables used by report/assert statements.
    if (name == "open_ok" || name == "read_mode" || name == "note"
        || name == "right") {
        return 0U;
    }
    if (name == "status_error" || name == "write_mode"
        || name == "warning" || name == "left") {
        return 1U;
    }
    if (name == "name_error" || name == "append_mode"
        || name == "error") {
        return 2U;
    }
    if (name == "mode_error" || name == "failure") {
        return 3U;
    }
    return std::nullopt;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_file_call(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    const auto name = expression && expression->vhdl != nullptr
            && expression->vhdl->referenced_name
        ? expression->vhdl->referenced_name->canonical.empty()
            ? std::string_view {
                  expression->vhdl->referenced_name->spelling }
            : std::string_view {
                  expression->vhdl->referenced_name->canonical }
        : expression && expression->vhdl != nullptr
        ? std::string_view { expression->vhdl->text }
        : std::string_view { };
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->operands.size() != 1U) {
        return std::nullopt;
    }
    const auto file = hir_referenced_declaration(
        expression->vhdl->operands.front());
    const auto declaration = file && specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_declaration(*file)
        : std::nullopt;
    const auto unnamed_file_call
        = !expression->vhdl->referenced_name
        && declaration && declaration->vhdl != nullptr
        && (declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::file
            || declaration->vhdl->object_class
                == semantic::vhdl::ObjectClass::file);
    if (vhdl_simple_name(name) != "endfile" && !unnamed_file_call) {
        return std::nullopt;
    }
    const auto binding = file
        ? hir_runtime_binding(*file, hir_process_scope_, true)
        : std::nullopt;
    if (!declaration || declaration->vhdl == nullptr
        || (declaration->vhdl->form
                != semantic::vhdl::DeclarationForm::file
            && declaration->vhdl->object_class
                != semantic::vhdl::ObjectClass::file)
        || !binding || binding->kind != HirRuntimeBindingKind::local
        || !binding->local) {
        report("FSIM-ELAB-VHFILE-009",
            "endfile requires exactly one whole VHDL file object",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    const auto destination = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(FileEndOfFile {
        destination, *binding->local, true });
    return destination;
}

bool Lowerer::is_hir_vhdl_file_statement(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::procedure_call
        || !vhdl_file_procedure_name(
            statement->vhdl->procedure.canonical.empty()
                ? statement->vhdl->procedure.spelling
                : statement->vhdl->procedure.canonical)) {
        return false;
    }
    const auto name = vhdl_simple_name(
        statement->vhdl->procedure.canonical.empty()
            ? std::string_view { statement->vhdl->procedure.spelling }
            : std::string_view { statement->vhdl->procedure.canonical });
    if (name != "read" && name != "write") {
        return true;
    }
    if (statement->vhdl->procedure_arguments.empty()) {
        return true;
    }
    const auto first = statement->vhdl->procedure_arguments.front().actual;
    const auto selected = hir_referenced_declaration(first);
    const auto declaration = selected
        ? specialized_hir_unit_->find_declaration(*selected)
        : std::nullopt;
    return declaration && declaration->vhdl != nullptr
        && (declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::file
            || declaration->vhdl->object_class
                == semantic::vhdl::ObjectClass::file
            || hir_string_binding(
                   *selected, statement->vhdl->scope, false)
                .has_value());
}

Lowerer::HirStringFormatStatus
Lowerer::hir_string_format_expression_status(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return HirStringFormatStatus::not_applicable;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call
        || expression->systemverilog->text != "$sformatf") {
        return HirStringFormatStatus::not_applicable;
    }
    const auto& call = *expression->systemverilog;
    const auto named = std::ranges::any_of(
        call.call_arguments,
        [](const semantic::sv::CallAssociation& association) {
            return association.formal && !association.formal->empty();
        });
    if (named || call.operands.empty()
        || call.operands.size() > 65U) {
        return HirStringFormatStatus::invalid;
    }
    const auto format = specialized_hir_unit_->find_expression(
        call.operands.front());
    if (!format || format->systemverilog == nullptr
        || format->systemverilog->kind
            != semantic::sv::ExpressionKind::string_literal
        || !format->systemverilog->decoded_string) {
        return HirStringFormatStatus::invalid;
    }
    const auto parsed = frontend::parse_output_format(
        *format->systemverilog->decoded_string);
    const auto consumes_value = [](const frontend::OutputFormat output) {
        return output != frontend::OutputFormat::Hierarchy
            && output != frontend::OutputFormat::Time;
    };
    const auto invalid_conversion = std::ranges::any_of(
        parsed.conversions,
        [](const frontend::ParsedOutputConversion& conversion) {
            return conversion.format
                    == frontend::OutputFormat::Unformatted2
                || conversion.format
                    == frontend::OutputFormat::Unformatted4;
        });
    const auto required = static_cast<std::size_t>(
        std::ranges::count_if(parsed.conversions,
            [&](const frontend::ParsedOutputConversion& conversion) {
                return consumes_value(conversion.format);
            }));
    if (!parsed.valid || invalid_conversion
        || parsed.conversions.size() > 64U
        || call.operands.size() - 1U < required) {
        return HirStringFormatStatus::invalid;
    }
    std::size_t value_index = 1U;
    for (const auto& conversion : parsed.conversions) {
        if (!consumes_value(conversion.format)) {
            continue;
        }
        const auto value = call.operands[value_index++];
        if ((conversion.format == frontend::OutputFormat::String
                && !hir_expression_is_string(value, process_scope))
            || (conversion.format != frontend::OutputFormat::String
                && !hir_expression_width(value, process_scope))) {
            return HirStringFormatStatus::invalid;
        }
    }
    return HirStringFormatStatus::valid;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_formatted_string(
    const std::span<const semantic::ExpressionId> arguments,
    const std::optional<frontend::OutputFormat> default_format)
{
    if (specialized_hir_unit_ == nullptr || arguments.empty()) {
        return std::nullopt;
    }
    frontend::ParsedOutputFormat parsed;
    if (default_format) {
        parsed.conversions.resize(arguments.size());
        for (auto& conversion : parsed.conversions) {
            conversion.format = *default_format;
        }
    } else {
        const auto format = specialized_hir_unit_->find_expression(
            arguments.front());
        if (!format || format->systemverilog == nullptr
            || !format->systemverilog->decoded_string) {
            return std::nullopt;
        }
        parsed = frontend::parse_output_format(
            *format->systemverilog->decoded_string);
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant {
        destination, { } });
    const auto append_literal = [&](const std::string_view text) {
        if (text.empty()) {
            return;
        }
        const auto literal = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant {
            literal, std::string { text } });
        process_.operations.emplace_back(ConcatenateStrings {
            destination, { destination, literal } });
    };
    const auto append_value = [&](const frontend::ParsedOutputConversion& conversion,
                                  const std::optional<semantic::ExpressionId> value) {
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
        operation.suppress_leading_zero
            = conversion.suppress_leading_zero;
        if (conversion.format == frontend::OutputFormat::Time) {
            operation.operation = StringMethodOperator::format_time;
            operation.use_timeformat_width
                = use_systemverilog_timeformat_width(
                    conversion.minimum_width,
                    conversion.suppress_leading_zero);
            process_.operations.emplace_back(operation);
            return true;
        }
        if (!value) {
            return false;
        }
        if (conversion.format == frontend::OutputFormat::String
            && hir_expression_is_string(
                *value, hir_process_scope_)) {
            const auto lowered = lower_hir_string_expression(*value);
            if (!lowered) {
                return false;
            }
            operation.operation
                = StringMethodOperator::format_string;
            operation.argument = *lowered;
            process_.operations.emplace_back(operation);
            return true;
        }
        const auto width = hir_expression_width(
            *value, hir_process_scope_);
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto scalar = hir_systemverilog_scalar_kind(*value);
        const auto lowered = lower_hir_expression(
            *value, *width, scalar);
        if (!lowered) {
            return false;
        }
        const auto width_register = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            width_register, unsigned_value(*width, 32U) });
        operation.operation = StringMethodOperator::format_packed;
        operation.first = *lowered;
        operation.second = width_register;
        operation.format = runtime_output_format(conversion.format);
        operation.scalar_kind = scalar;
        operation.signed_decimal
            = operation.format == OutputFormat::decimal
            && hir_expression_signed(*value);
        process_.operations.emplace_back(operation);
        return true;
    };

    std::size_t value_index = default_format ? 0U : 1U;
    for (const auto& conversion : parsed.conversions) {
        if (conversion.format == frontend::OutputFormat::Time
            && value_index < arguments.size()) {
            const auto value = specialized_hir_unit_->find_expression(
                arguments[value_index]);
            if (value && value->systemverilog != nullptr
                && value->systemverilog->kind
                    == semantic::sv::ExpressionKind::call
                && value->systemverilog->text == "$time") {
                ++value_index;
            }
        }
        const auto value = output_conversion_consumes_value(
                               conversion.format)
            ? std::optional { arguments[value_index++] }
            : std::nullopt;
        if (!append_value(conversion, value)) {
            return std::nullopt;
        }
    }
    append_literal(parsed.trailing_text);
    for (; value_index < arguments.size(); ++value_index) {
        frontend::ParsedOutputConversion conversion;
        conversion.format = frontend::OutputFormat::Decimal;
        if (!append_value(conversion, arguments[value_index])) {
            return std::nullopt;
        }
    }
    return destination;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_string_format_expression(
    const semantic::ExpressionId expression_id)
{
    if (specialized_hir_unit_ == nullptr
        || hir_string_format_expression_status(
               expression_id, hir_process_scope_)
            != HirStringFormatStatus::valid) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    emit_debug_point(
        DebugPointKind::call, hir_source_span(call.source));
    return lower_hir_formatted_string(call.operands, std::nullopt);
}

Lowerer::HirStringFormatStatus Lowerer::hir_string_format_task_status(
    const semantic::StatementId statement_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return HirStringFormatStatus::not_applicable;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->systemverilog == nullptr
        || statement->systemverilog->kind
            != semantic::sv::StatementKind::task_call) {
        return HirStringFormatStatus::not_applicable;
    }
    const auto& call = *statement->systemverilog;
    const auto format_task = call.task.spelling == "$swrite"
        || call.task.spelling == "$sformat"
        || call.task.spelling == "$swriteb"
        || call.task.spelling == "$swriteh"
        || call.task.spelling == "$swriteo";
    if (!format_task) {
        return HirStringFormatStatus::not_applicable;
    }
    const auto named = std::ranges::any_of(
        call.task_arguments,
        [](const semantic::sv::TaskAssociation& association) {
            return association.formal && !association.formal->empty();
        });
    if (named || call.task_arguments.size() < 2U
        || call.task_arguments.size() > 65U
        || std::ranges::any_of(
            call.task_arguments,
            [](const semantic::sv::TaskAssociation& association) {
                return !association.actual;
            })) {
        return HirStringFormatStatus::invalid;
    }
    const auto target = *call.task_arguments.front().actual;
    const auto target_expression
        = specialized_hir_unit_->find_expression(target);
    const auto target_declaration = hir_target_declaration(target);
    const auto binding = target_declaration
        ? hir_string_binding(*target_declaration, process_scope, false)
        : std::nullopt;
    if (!target_expression || target_expression->systemverilog == nullptr
        || target_expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name
        || !binding
        || (binding->kind == HirStringBindingKind::object
            && (!binding->object
                || read_only_string_objects_.contains(*binding->object)))) {
        return HirStringFormatStatus::invalid;
    }
    const auto default_format = call.task.spelling == "$swriteb"
        || call.task.spelling == "$swriteh"
        || call.task.spelling == "$swriteo";
    if (default_format) {
        return std::ranges::all_of(
                   call.task_arguments.begin() + 1,
                   call.task_arguments.end(),
                   [&](const semantic::sv::TaskAssociation& argument) {
                       const auto width = hir_expression_width(
                           *argument.actual, process_scope);
                       return width && *width != 0U
                           && *width
                           <= std::numeric_limits<std::uint32_t>::max();
                   })
            ? HirStringFormatStatus::valid
            : HirStringFormatStatus::invalid;
    }
    const auto format_id = *call.task_arguments[1].actual;
    const auto format = specialized_hir_unit_->find_expression(format_id);
    if (!format || format->systemverilog == nullptr
        || format->systemverilog->kind
            != semantic::sv::ExpressionKind::string_literal
        || !format->systemverilog->decoded_string) {
        return HirStringFormatStatus::invalid;
    }
    const auto parsed = frontend::parse_output_format(
        *format->systemverilog->decoded_string);
    const auto consumes_value = [](const frontend::OutputFormat output) {
        return output != frontend::OutputFormat::Hierarchy
            && output != frontend::OutputFormat::Time;
    };
    const auto invalid_conversion = std::ranges::any_of(
        parsed.conversions,
        [](const frontend::ParsedOutputConversion& conversion) {
            return conversion.format
                == frontend::OutputFormat::Unformatted2
                || conversion.format
                == frontend::OutputFormat::Unformatted4;
        });
    const auto required = static_cast<std::size_t>(
        std::ranges::count_if(parsed.conversions,
            [&](const frontend::ParsedOutputConversion& conversion) {
                return consumes_value(conversion.format);
            }));
    if (!parsed.valid || invalid_conversion
        || parsed.conversions.size() > 64U
        || call.task_arguments.size() - 2U < required) {
        return HirStringFormatStatus::invalid;
    }
    std::size_t value_index { 2U };
    for (const auto& conversion : parsed.conversions) {
        if (!consumes_value(conversion.format)) {
            continue;
        }
        const auto value
            = *call.task_arguments[value_index++].actual;
        const auto width = hir_expression_width(value, process_scope);
        if ((conversion.format == frontend::OutputFormat::String
                && !hir_expression_is_string(value, process_scope))
            || (conversion.format != frontend::OutputFormat::String
                && (!width || *width == 0U
                    || *width
                        > std::numeric_limits<std::uint32_t>::max()))) {
            return HirStringFormatStatus::invalid;
        }
    }
    for (; value_index < call.task_arguments.size(); ++value_index) {
        const auto width = hir_expression_width(
            *call.task_arguments[value_index].actual, process_scope);
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            return HirStringFormatStatus::invalid;
        }
    }
    return HirStringFormatStatus::valid;
}

bool Lowerer::lower_hir_string_format_task(
    const semantic::StatementId statement_id)
{
    const auto status = hir_string_format_task_status(
        statement_id, hir_process_scope_);
    if (status == HirStringFormatStatus::not_applicable) {
        return false;
    }
    if (status == HirStringFormatStatus::invalid) {
        return true;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->systemverilog == nullptr) {
        return false;
    }
    const auto& call = *statement->systemverilog;
    std::vector<semantic::ExpressionId> arguments;
    arguments.reserve(call.task_arguments.size() - 1U);
    for (std::size_t index { 1U };
        index < call.task_arguments.size(); ++index) {
        arguments.push_back(*call.task_arguments[index].actual);
    }
    std::optional<frontend::OutputFormat> default_format;
    if (call.task.spelling == "$swriteb") {
        default_format = frontend::OutputFormat::Binary;
    } else if (call.task.spelling == "$swriteh") {
        default_format = frontend::OutputFormat::Hexadecimal;
    } else if (call.task.spelling == "$swriteo") {
        default_format = frontend::OutputFormat::Octal;
    }
    emit_debug_point(
        DebugPointKind::call, hir_source_span(call.source));
    const auto result = lower_hir_formatted_string(
        arguments, default_format);
    return result
        && lower_hir_string_copy_out(
            *call.task_arguments.front().actual, *result);
}

void Lowerer::diagnose_hir_systemverilog_file_process(
    const semantic::ProcessId process_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return;
    }
    const auto process = specialized_hir_unit_->find_process(process_id);
    if (!process || process->systemverilog == nullptr) {
        return;
    }
    const auto process_scope = process->systemverilog->scope;
    const auto expression_scope = [&](const semantic::ExpressionId input) {
        const auto expression = specialized_hir_unit_->find_expression(
            input);
        return expression && expression->systemverilog != nullptr
            ? expression->systemverilog->scope
            : process_scope;
    };
    const auto report_expression = [&]
        (const semantic::ExpressionId expression,
         std::string code,
         std::string message) {
            const auto source = specialized_hir_unit_->find_expression(
                expression);
            if (source && source->systemverilog != nullptr) {
                report(std::move(code), std::move(message),
                    hir_source_span(source->systemverilog->source));
            }
        };
    const auto expression_scalar_kind = [&]
        (const semantic::ExpressionId expression) {
            const auto declaration_id = hir_referenced_declaration(
                expression);
            const auto declaration = declaration_id
                ? specialized_hir_unit_->find_declaration(*declaration_id)
                : std::nullopt;
            return declaration && declaration->systemverilog != nullptr
                    && declaration->systemverilog->type
                ? scalar_kind(
                      declaration->systemverilog->type->target.spelling)
                : frontend::SystemVerilogScalarKind::None;
        };
    const auto integral_expression = [&]
        (const semantic::ExpressionId expression) {
            const auto scope = expression_scope(expression);
            if (hir_expression_is_string(expression, scope)) {
                return false;
            }
            const auto kind = expression_scalar_kind(expression);
            if (kind == frontend::SystemVerilogScalarKind::ShortReal
                || kind == frontend::SystemVerilogScalarKind::Real
                || kind == frontend::SystemVerilogScalarKind::Realtime
                || kind == frontend::SystemVerilogScalarKind::Chandle) {
                return false;
            }
            const auto width = hir_expression_width(
                expression, scope);
            const auto domain = hir_expression_domain(
                expression, scope);
            return width && *width != 0U && domain
                && (*domain == frontend::ValueDomain::Bit2
                    || *domain == frontend::ValueDomain::Logic4
                    || *domain == frontend::ValueDomain::Integer);
        };
    const auto file_handle = [&]
        (const semantic::ExpressionId expression) {
            const auto source = specialized_hir_unit_->find_expression(
                expression);
            if (!source || source->systemverilog == nullptr) {
                return false;
            }
            if (source->systemverilog->kind
                == semantic::sv::ExpressionKind::integer_literal) {
                return true;
            }
            if (is_hir_systemverilog_file_call(expression)) {
                return true;
            }
            return expression_scalar_kind(expression)
                    == frontend::SystemVerilogScalarKind::None
                && hir_expression_width(
                       expression, expression_scope(expression))
                    == std::optional<std::size_t> { 32U }
                && integral_expression(expression);
        };
    const auto writable_runtime_binding = [&]
        (const HirRuntimeBinding& binding) {
            return binding.kind == HirRuntimeBindingKind::local
                || (binding.signal
                    && !read_only_signals_.contains(*binding.signal));
        };
    const auto writable_string_binding = [&]
        (const HirStringBinding& binding) {
            return binding.kind == HirStringBindingKind::local
                || (binding.object
                    && !read_only_string_objects_.contains(
                        *binding.object));
        };
    const auto writable_text_target = [&]
        (const semantic::ExpressionId expression) {
            const auto declaration = hir_target_declaration(expression);
            if (!declaration) {
                return false;
            }
            if (const auto binding = hir_string_binding(
                    *declaration, expression_scope(expression), false)) {
                return writable_string_binding(*binding);
            }
            const auto binding = hir_runtime_binding(
                *declaration, expression_scope(expression), false);
            return binding && binding->width != 0U
                && binding->width
                    <= std::numeric_limits<std::uint32_t>::max()
                && writable_runtime_binding(*binding);
        };
    const auto scan_target_supported = [&]
        (const semantic::ExpressionId expression,
         const frontend::InputScanFormat format) {
            const auto declaration = hir_target_declaration(expression);
            if (!declaration) {
                return false;
            }
            if (const auto binding = hir_string_binding(
                    *declaration, expression_scope(expression), false)) {
                return text_input_format(format)
                    && writable_string_binding(*binding);
            }
            const auto binding = hir_runtime_binding(
                *declaration, expression_scope(expression), false);
            if (!binding || binding->width == 0U
                || binding->width
                    > std::numeric_limits<std::uint32_t>::max()
                || !writable_runtime_binding(*binding)) {
                return false;
            }
            const auto kind = expression_scalar_kind(expression);
            const auto real_target
                = kind == frontend::SystemVerilogScalarKind::ShortReal
                || kind == frontend::SystemVerilogScalarKind::Real
                || kind == frontend::SystemVerilogScalarKind::Realtime;
            if (kind == frontend::SystemVerilogScalarKind::None) {
                return format != frontend::InputScanFormat::Real;
            }
            if (real_target) {
                return format == frontend::InputScanFormat::Real;
            }
            if (kind == frontend::SystemVerilogScalarKind::Time) {
                return format == frontend::InputScanFormat::Decimal
                    || format
                        == frontend::InputScanFormat::UnsignedDecimal
                    || format == frontend::InputScanFormat::Real
                    || format == frontend::InputScanFormat::Unformatted2
                    || format == frontend::InputScanFormat::Unformatted4;
            }
            return kind == frontend::SystemVerilogScalarKind::Chandle
                && format == frontend::InputScanFormat::Hexadecimal;
        };
    const auto writable_container_target = [&]
        (const semantic::ExpressionId expression) {
            const auto name = hir_name(expression);
            const auto object = name
                ? container_objects_.find(*name)
                : container_objects_.end();
            if (object == container_objects_.end()
                || read_only_container_objects_.contains(*name)
                || static_cast<std::size_t>(object->second)
                    >= design_.container_object_info_.size()) {
                return false;
            }
            const auto& type
                = design_.container_object_info_[object->second].type;
            const auto width = container_packed_element_width(type);
            return type.fixed && width
                && *width <= std::numeric_limits<std::uint32_t>::max();
        };

    std::unordered_set<std::uint32_t> visited_expressions;
    const auto diagnose_expression = [&]
        (const auto& self,
         const semantic::ExpressionId expression_id) -> void {
            if (!visited_expressions.insert(
                    expression_id.value()).second) {
                return;
            }
            const auto expression = specialized_hir_unit_->find_expression(
                expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                return;
            }
            const auto& call = *expression->systemverilog;
            if (call.kind == semantic::sv::ExpressionKind::call
                && hir_file_call_name(call.text)) {
                const auto fail = [&](std::string code,
                                      std::string message) {
                    report_expression(
                        expression_id, std::move(code), std::move(message));
                };
                const auto named = std::ranges::any_of(
                    call.call_arguments,
                    [](const semantic::sv::CallAssociation& association) {
                        return association.formal
                            && !association.formal->empty();
                    });
                const auto plusarg = call.text == "$test$plusargs"
                    || call.text == "$value$plusargs";
                if (named && !plusarg) {
                    fail("FSIM-ELAB-SVFILE-011",
                        call.text + " requires positional arguments");
                } else if (call.text == "$fopen") {
                    if (call.operands.empty()
                        || call.operands.size() > 2U
                        || !hir_expression_is_string(
                            call.operands.front(), call.scope)
                        || (call.operands.size() == 2U
                            && !hir_expression_is_string(
                                call.operands.back(), call.scope))) {
                        fail("FSIM-ELAB-SVFILE-003",
                            "$fopen requires a byte-string filename and "
                            "optional mode");
                    }
                } else if (call.text == "$fgets") {
                    if (call.operands.size() != 2U) {
                        fail("FSIM-ELAB-SVFILE-004",
                            "$fgets requires a target and integer handle");
                    } else if (!writable_text_target(call.operands[0])) {
                        fail("FSIM-ELAB-SVFILE-002",
                            "$fgets target must be a writable packed or "
                            "string variable");
                    } else if (!file_handle(call.operands[1])) {
                        fail("FSIM-ELAB-SVFILE-001",
                            "$fgets handle must be an integer expression");
                    }
                } else if (call.text == "$fgetc"
                    || call.text == "$feof" || call.text == "$ftell"
                    || call.text == "$rewind") {
                    if (call.operands.size() != 1U) {
                        fail("FSIM-ELAB-SVFILE-009",
                            call.text + " requires one integer handle");
                    } else if (!file_handle(call.operands.front())) {
                        fail("FSIM-ELAB-SVFILE-001",
                            call.text
                                + " handle must be an integer expression");
                    }
                } else if (call.text == "$ungetc") {
                    if (call.operands.size() != 2U
                        || !integral_expression(call.operands[0])
                        || !file_handle(call.operands[1])) {
                        fail("FSIM-ELAB-SVFILE-010",
                            "$ungetc requires a character and integer "
                            "handle");
                    }
                } else if (call.text == "$ferror") {
                    if (call.operands.size() != 2U) {
                        fail("FSIM-ELAB-SVFILE-006",
                            "$ferror requires an integer handle and "
                            "writable target");
                    } else if (!file_handle(call.operands[0])
                        || !writable_text_target(call.operands[1])) {
                        fail("FSIM-ELAB-SVFILE-002",
                            "$ferror target must be a writable packed or "
                            "string variable");
                    }
                } else if (call.text == "$fseek") {
                    if (call.operands.size() != 3U
                        || !file_handle(call.operands[0])
                        || !integral_expression(call.operands[1])
                        || !integral_expression(call.operands[2])) {
                        fail("FSIM-ELAB-SVFILE-015",
                            "$fseek requires integral handle, offset, and "
                            "origin expressions");
                    }
                } else if (call.text == "$fscanf"
                    || call.text == "$sscanf") {
                    const auto string_source = call.text == "$sscanf";
                    if (call.operands.size() < 2U) {
                        fail("FSIM-ELAB-SVFILE-011",
                            call.text + " requires source, literal format, "
                            "and targets");
                    } else {
                        const auto format
                            = specialized_hir_unit_->find_expression(
                                call.operands[1]);
                        if (!format || format->systemverilog == nullptr
                            || format->systemverilog->kind
                                != semantic::sv::ExpressionKind::string_literal
                            || !format->systemverilog->decoded_string) {
                            fail("FSIM-ELAB-SVFILE-011",
                                call.text
                                    + " format must be a string literal");
                        } else {
                            const auto parsed = frontend::parse_input_format(
                                *format->systemverilog->decoded_string);
                            const auto required = static_cast<std::size_t>(
                                std::ranges::count_if(
                                    parsed.conversions,
                                    [](const auto& conversion) {
                                        return !conversion.suppress;
                                    }));
                            if (!parsed.valid || parsed.conversions.empty()
                                || parsed.conversions.size() > 64U
                                || required != call.operands.size() - 2U
                                || parsed.trailing_text.size()
                                    > maximum_string_bytes
                                || std::ranges::any_of(
                                    parsed.conversions,
                                    [](const auto& conversion) {
                                        return conversion.prefix.size()
                                                > maximum_string_bytes
                                            || conversion.maximum_characters
                                                > maximum_string_bytes;
                                    })) {
                                fail("FSIM-ELAB-SVFILE-011",
                                    call.text + " has an invalid or "
                                    "target-mismatched format");
                            } else if ((string_source
                                    && !hir_expression_is_string(
                                        call.operands[0], call.scope))
                                || (!string_source
                                    && !file_handle(call.operands[0]))) {
                                fail("FSIM-ELAB-SVFILE-011",
                                    call.text
                                        + " source has an incompatible "
                                        "type");
                            } else {
                                std::size_t target_index = 2U;
                                for (const auto& conversion :
                                    parsed.conversions) {
                                    if (conversion.suppress) {
                                        continue;
                                    }
                                    const auto target
                                        = call.operands[target_index++];
                                    if (!scan_target_supported(
                                            target, conversion.format)) {
                                        fail("FSIM-ELAB-SVFILE-012",
                                            "scan target is incompatible, "
                                            "read-only, or unsupported");
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } else if (call.text == "$fread") {
                    if (call.operands.size() < 2U
                        || call.operands.size() > 4U) {
                        fail("FSIM-ELAB-SVFILE-013",
                            "$fread requires target, handle, and optional "
                            "start/count");
                    } else {
                        const auto declaration = hir_target_declaration(
                            call.operands[0]);
                        const auto binding = declaration
                            ? hir_runtime_binding(
                                *declaration, call.scope, false)
                            : std::nullopt;
                        const auto packed_target = binding
                            && call.operands.size() == 2U
                            && binding->width != 0U
                            && binding->width
                                <= std::numeric_limits<std::uint32_t>::max()
                            && writable_runtime_binding(*binding);
                        if (!writable_container_target(call.operands[0])
                            && !packed_target) {
                            fail("FSIM-ELAB-SVFILE-014",
                                "$fread target must be a writable packed "
                                "value or memory");
                        } else if (!file_handle(call.operands[1])
                            || (call.operands.size() >= 3U
                                && !integral_expression(call.operands[2]))
                            || (call.operands.size() == 4U
                                && !integral_expression(
                                    call.operands[3]))) {
                            fail("FSIM-ELAB-SVFILE-013",
                                "$fread handle and bounds must be integral "
                                "expressions");
                        }
                    }
                }
            }
            if (call.kind == semantic::sv::ExpressionKind::call
                && call.text == "$sformatf") {
                const auto fail = [&](std::string message) {
                    report_expression(expression_id,
                        "FSIM-ELAB-SVSTRING-019",
                        std::move(message));
                };
                const auto format = call.operands.empty()
                    ? std::optional<semantic::CompiledExpressionView> { }
                    : specialized_hir_unit_->find_expression(
                        call.operands.front());
                if (!format || format->systemverilog == nullptr
                    || format->systemverilog->kind
                        != semantic::sv::ExpressionKind::string_literal
                    || !format->systemverilog->decoded_string) {
                    fail("$sformatf requires a literal SystemVerilog "
                        "format string");
                } else {
                    const auto parsed = frontend::parse_output_format(
                        *format->systemverilog->decoded_string);
                    const auto consumes_value = []
                        (const frontend::OutputFormat output) {
                            return output
                                    != frontend::OutputFormat::Hierarchy
                                && output
                                    != frontend::OutputFormat::Time;
                        };
                    const auto has_unformatted = std::ranges::any_of(
                        parsed.conversions,
                        [](const auto& conversion) {
                            return conversion.format
                                    == frontend::OutputFormat::Unformatted2
                                || conversion.format
                                    == frontend::OutputFormat::Unformatted4;
                        });
                    const auto required = static_cast<std::size_t>(
                        std::ranges::count_if(
                            parsed.conversions,
                            [&](const auto& conversion) {
                                return consumes_value(conversion.format);
                            }));
                    const auto supplied = call.operands.size() - 1U;
                    if (!parsed.valid || has_unformatted
                        || parsed.conversions.size() > 64U
                        || supplied > 64U) {
                        fail("$sformatf has an invalid or oversized "
                            "bounded format");
                    } else if (supplied < required) {
                        fail("$sformatf format conversions require "
                            "matching value arguments");
                    } else {
                        const auto compatible = [&]
                            (const semantic::ExpressionId value,
                             const frontend::OutputFormat output) {
                                if (output
                                        == frontend::OutputFormat::String
                                    && hir_expression_is_string(
                                        value, call.scope)) {
                                    return true;
                                }
                                const auto kind
                                    = expression_scalar_kind(value);
                                const auto real_scalar
                                    = kind
                                        == frontend::SystemVerilogScalarKind::
                                            ShortReal
                                    || kind
                                        == frontend::SystemVerilogScalarKind::
                                            Real
                                    || kind
                                        == frontend::SystemVerilogScalarKind::
                                            Realtime;
                                const auto real_format
                                    = output
                                        == frontend::OutputFormat::
                                            RealScientific
                                    || output
                                        == frontend::OutputFormat::RealFixed
                                    || output
                                        == frontend::OutputFormat::
                                            RealGeneral;
                                if ((real_scalar && !real_format)
                                    || (!real_scalar && real_format)
                                    || (kind
                                            == frontend::
                                                SystemVerilogScalarKind::Time
                                        && output
                                            != frontend::OutputFormat::
                                                Decimal)
                                    || (kind
                                            == frontend::
                                                SystemVerilogScalarKind::
                                                    Chandle
                                        && output
                                            != frontend::OutputFormat::
                                                Hexadecimal)) {
                                    return false;
                                }
                                const auto width = hir_expression_width(
                                    value, call.scope);
                                return width && *width != 0U;
                            };
                        std::size_t value_index = 1U;
                        for (const auto& conversion :
                            parsed.conversions) {
                            if (!consumes_value(conversion.format)) {
                                continue;
                            }
                            if (!compatible(call.operands[value_index++],
                                    conversion.format)) {
                                fail("formatted string conversion is "
                                    "incompatible with the value type");
                                break;
                            }
                        }
                        for (; value_index < call.operands.size();
                            ++value_index) {
                            if (!compatible(call.operands[value_index],
                                    frontend::OutputFormat::Decimal)) {
                                fail("formatted string conversion is "
                                    "incompatible with the value type");
                                break;
                            }
                        }
                    }
                }
            }
            if (call.kind == semantic::sv::ExpressionKind::call
                && (call.text == "$test$plusargs"
                    || call.text == "$value$plusargs")) {
                const auto value_query
                    = call.text == "$value$plusargs";
                const auto expected_operands = value_query ? 2U : 1U;
                const auto fail = [&](std::string code,
                                      std::string message) {
                    report_expression(expression_id,
                        std::move(code), std::move(message));
                };
                const auto named = std::ranges::any_of(
                    call.call_arguments,
                    [](const semantic::sv::CallAssociation& association) {
                        return association.formal
                            && !association.formal->empty();
                    });
                if (named || call.operands.size() != expected_operands
                    || call.operands.empty()
                    || !hir_expression_is_string(
                        call.operands.front(), call.scope)) {
                    fail("FSIM-ELAB-SVCLI-001",
                        call.text
                            + (value_query
                                    ? " requires a format string and "
                                      "writable target"
                                    : " requires one string expression"));
                } else if (value_query) {
                    const auto format
                        = specialized_hir_unit_->find_expression(
                            call.operands.front());
                    if (!format || format->systemverilog == nullptr
                        || format->systemverilog->kind
                            != semantic::sv::ExpressionKind::string_literal
                        || !format->systemverilog->decoded_string) {
                        fail("FSIM-ELAB-SVCLI-002",
                            "$value$plusargs currently requires a literal "
                            "format string");
                    } else {
                        const auto parsed = frontend::parse_input_format(
                            *format->systemverilog->decoded_string);
                        if (!parsed.valid
                            || parsed.conversions.size() != 1U
                            || parsed.conversions.front().suppress) {
                            fail("FSIM-ELAB-SVCLI-002",
                                "$value$plusargs format must contain "
                                "exactly one assignment conversion");
                        } else {
                            const auto conversion
                                = parsed.conversions.front().format;
                            if (conversion
                                    == frontend::InputScanFormat::
                                        Unformatted2
                                || conversion
                                    == frontend::InputScanFormat::
                                        Unformatted4) {
                                fail("FSIM-ELAB-SVCLI-002",
                                    "$value$plusargs does not accept "
                                    "unformatted %u or %z conversions");
                            } else if (!scan_target_supported(
                                    call.operands[1], conversion)) {
                                fail("FSIM-ELAB-SVCLI-003",
                                    "$value$plusargs target is read-only, "
                                    "incompatible, or unsupported");
                            }
                        }
                    }
                }
            }
            for (const auto operand : call.operands) {
                self(self, operand);
            }
            for (const auto& association : call.call_arguments) {
                if (association.actual) {
                    self(self, *association.actual);
                }
            }
        };

    std::unordered_set<std::uint32_t> visited_statements;
    const auto diagnose_statement = [&]
        (const auto& self,
         const semantic::StatementId statement_id) -> void {
            if (!visited_statements.insert(statement_id.value()).second) {
                return;
            }
            const auto statement = specialized_hir_unit_->find_statement(
                statement_id);
            if (!statement || statement->systemverilog == nullptr) {
                return;
            }
            const auto& source = *statement->systemverilog;
            const auto statement_fail = [&](std::string code,
                                            std::string message) {
                report(std::move(code), std::move(message),
                    hir_source_span(source.source));
            };
            if ((source.kind == semantic::sv::StatementKind::display
                    || source.kind
                        == semantic::sv::StatementKind::report)
                && source.value
                && source.output_values.empty()
                && !source.output_format
                && !specialized_hir_unit_->evaluate_string_expression(
                    *source.value)) {
                statement_fail("FSIM-ELAB-SVSTRING-003",
                    "report message is not a constant string expression");
            } else if (hir_string_format_task_status(
                           statement_id, source.scope)
                == HirStringFormatStatus::invalid) {
                const auto target = !source.task_arguments.empty()
                        && source.task_arguments.front().actual
                    ? specialized_hir_unit_->find_expression(
                        *source.task_arguments.front().actual)
                    : std::nullopt;
                const auto declaration = target
                    ? hir_target_declaration(
                        *source.task_arguments.front().actual)
                    : std::nullopt;
                const auto binding = declaration
                    ? hir_string_binding(
                        *declaration, source.scope, false)
                    : std::nullopt;
                const auto direct_string_target = target
                    && target->systemverilog != nullptr
                    && target->systemverilog->kind
                        == semantic::sv::ExpressionKind::name
                    && binding.has_value();
                statement_fail(
                    direct_string_target
                        ? "FSIM-ELAB-SVSTRING-019"
                        : "FSIM-ELAB-SVSTRING-020",
                    direct_string_target
                        ? source.task.spelling
                            + " has an invalid or target-mismatched format"
                        : source.task.spelling
                            + " requires a direct writable string target");
            } else if (source.kind
                    == semantic::sv::StatementKind::file_close
                && (!source.file_handle
                    || !file_handle(*source.file_handle))) {
                statement_fail("FSIM-ELAB-SVFILE-001",
                    "$fclose handle must be an integer expression");
            } else if (source.kind
                    == semantic::sv::StatementKind::file_flush
                && source.file_handle
                && !file_handle(*source.file_handle)) {
                statement_fail("FSIM-ELAB-SVFILE-016",
                    "$fflush handle must be an integer expression");
            } else if (source.kind
                    == semantic::sv::StatementKind::file_display
                && (!source.file_handle
                    || !file_handle(*source.file_handle))) {
                statement_fail("FSIM-ELAB-SVFILE-001",
                    "file output handle must be an integer expression");
            } else if (source.kind
                == semantic::sv::StatementKind::memory_transfer) {
                const auto name = source.target
                    ? hir_name(*source.target)
                    : std::nullopt;
                const auto object = name
                    ? container_objects_.find(*name)
                    : container_objects_.end();
                const auto valid_object = source.value && source.target
                    && hir_expression_is_string(
                        *source.value, source.scope)
                    && object != container_objects_.end()
                    && (source.memory_write
                        || !read_only_container_objects_.contains(*name))
                    && static_cast<std::size_t>(object->second)
                        < design_.container_object_info_.size()
                    && design_.container_object_info_[object->second]
                        .type.fixed;
                if (!valid_object) {
                    statement_fail("FSIM-ELAB-SVMEMORY-003",
                        "memory transfer requires a writable bounded "
                        "static-array object");
                }
            }
            const auto diagnose_optional = [&](const auto& expression) {
                if (expression) {
                    diagnose_expression(
                        diagnose_expression, *expression);
                }
            };
            diagnose_optional(source.target);
            diagnose_optional(source.value);
            diagnose_optional(source.condition);
            diagnose_optional(source.loop_initial);
            diagnose_optional(source.loop_limit);
            diagnose_optional(source.loop_update_target);
            diagnose_optional(source.file_handle);
            if (source.delay) {
                diagnose_optional(source.delay->primary.expression);
                diagnose_optional(source.delay->minimum
                        ? source.delay->minimum->expression
                        : std::optional<semantic::ExpressionId> { });
                diagnose_optional(source.delay->typical
                        ? source.delay->typical->expression
                        : std::optional<semantic::ExpressionId> { });
                diagnose_optional(source.delay->maximum
                        ? source.delay->maximum->expression
                        : std::optional<semantic::ExpressionId> { });
            }
            for (const auto& sensitivity : source.sensitivities) {
                diagnose_optional(sensitivity.expression);
            }
            for (const auto& argument : source.task_arguments) {
                diagnose_optional(argument.actual);
            }
            for (const auto& output : source.output_values) {
                diagnose_optional(output.value);
            }
            for (const auto nested : source.statements) {
                self(self, nested);
            }
            for (const auto nested : source.else_statements) {
                self(self, nested);
            }
            for (const auto nested : source.loop_updates) {
                self(self, nested);
            }
            for (const auto& alternative : source.case_alternatives) {
                for (const auto choice : alternative.choices) {
                    diagnose_expression(diagnose_expression, choice);
                }
                for (const auto nested : alternative.statements) {
                    self(self, nested);
                }
            }
        };
    for (const auto statement : process->systemverilog->statements) {
        diagnose_statement(diagnose_statement, statement);
    }
}

std::optional<RegisterId> Lowerer::lower_hir_systemverilog_file_call(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call
        || !hir_file_call_name(expression->systemverilog->text)) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    const auto span = hir_source_span(call.source);
    const auto fail = [&](std::string code, std::string message) {
        report(std::move(code), std::move(message), span);
        return std::optional<RegisterId> { };
    };
    const auto named = std::ranges::any_of(
        call.call_arguments,
        [](const semantic::sv::CallAssociation& association) {
            return association.formal && !association.formal->empty();
        });
    if (named) {
        return fail(
            call.text == "$test$plusargs"
                    || call.text == "$value$plusargs"
                ? "FSIM-ELAB-SVCLI-001"
                : "FSIM-ELAB-SVFILE-011",
            call.text + " requires positional arguments");
    }
    emit_debug_point(DebugPointKind::call, span);
    const auto finish = [&](const RegisterId result)
        -> std::optional<RegisterId> {
        return expected_width != 0U && expected_width != 32U
            ? std::optional { resize_register(result, expected_width, true) }
            : std::optional { result };
    };
    const auto lower_integer = [&](const semantic::ExpressionId operand)
        -> std::optional<RegisterId> {
        const auto width = hir_expression_width(
            operand, hir_process_scope_).value_or(32U);
        auto result = lower_hir_expression(operand, width);
        if (result && register_width(*result) != 32U) {
            result = resize_register(
                *result, 32U, hir_expression_signed(operand));
        }
        return result;
    };
    const auto expression_scalar_kind = [&](
                                             const semantic::ExpressionId operand) {
        const auto declaration_id = hir_referenced_declaration(operand);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->type) {
            return frontend::SystemVerilogScalarKind::None;
        }
        return scalar_kind(
            declaration->systemverilog->type->target.spelling);
    };
    struct TextTarget {
        FileTextTargetKind kind { FileTextTargetKind::string_register };
        std::uint32_t id { };
        std::uint32_t width { 1U };
        std::optional<StringObjectId> copied_object;
    };
    const auto text_target = [&](const semantic::ExpressionId operand)
        -> std::optional<TextTarget> {
        const auto declaration_id = hir_target_declaration(operand);
        if (!declaration_id) {
            return std::nullopt;
        }
        if (const auto binding = hir_string_binding(
                *declaration_id, hir_process_scope_, true)) {
            if (binding->kind == HirStringBindingKind::local
                && binding->local) {
                return TextTarget {
                    FileTextTargetKind::string_register,
                    *binding->local,
                    1U,
                    std::nullopt,
                };
            }
            if (binding->object
                && !read_only_string_objects_.contains(*binding->object)) {
                return TextTarget {
                    FileTextTargetKind::string_register,
                    allocate_string_register(),
                    1U,
                    *binding->object,
                };
            }
            return std::nullopt;
        }
        const auto binding = hir_runtime_binding(
            *declaration_id, hir_process_scope_, true);
        if (!binding || binding->width == 0U
            || binding->width
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        if (binding->kind == HirRuntimeBindingKind::local
            && binding->local) {
            return TextTarget {
                FileTextTargetKind::packed_register,
                *binding->local,
                static_cast<std::uint32_t>(binding->width),
                std::nullopt,
            };
        }
        if (binding->signal
            && !read_only_signals_.contains(*binding->signal)) {
            return TextTarget {
                FileTextTargetKind::packed_signal,
                *binding->signal,
                static_cast<std::uint32_t>(binding->width),
                std::nullopt,
            };
        }
        return std::nullopt;
    };

    if (call.text == "$test$plusargs"
        || call.text == "$value$plusargs") {
        const auto value_query = call.text == "$value$plusargs";
        const auto expected_operands = value_query ? 2U : 1U;
        if (call.operands.size() != expected_operands
            || call.operands.empty()
            || !hir_expression_is_string(
                call.operands.front(), hir_process_scope_)) {
            return fail("FSIM-ELAB-SVCLI-001",
                call.text
                    + (value_query
                            ? " requires a format string and writable target"
                            : " requires one string expression"));
        }
        if (!value_query) {
            const auto query = lower_hir_string_expression(
                call.operands.front());
            if (!query) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(PlusArgSelect {
                destination, *query, std::nullopt });
            return finish(destination);
        }

        const auto format = specialized_hir_unit_->find_expression(
            call.operands.front());
        if (!format || format->systemverilog == nullptr
            || format->systemverilog->kind
                != semantic::sv::ExpressionKind::string_literal
            || !format->systemverilog->decoded_string) {
            return fail("FSIM-ELAB-SVCLI-002",
                "$value$plusargs currently requires a literal format "
                "string");
        }
        const auto parsed = frontend::parse_input_format(
            *format->systemverilog->decoded_string);
        if (!parsed.valid || parsed.conversions.size() != 1U
            || parsed.conversions.front().suppress) {
            return fail("FSIM-ELAB-SVCLI-002",
                "$value$plusargs format must contain exactly one "
                "assignment conversion");
        }
        const auto& parsed_conversion = parsed.conversions.front();
        if (parsed_conversion.format
                == frontend::InputScanFormat::Unformatted2
            || parsed_conversion.format
                == frontend::InputScanFormat::Unformatted4) {
            return fail("FSIM-ELAB-SVCLI-002",
                "$value$plusargs does not accept unformatted %u or %z "
                "conversions");
        }

        const auto target_id = call.operands[1];
        const auto target_declaration = hir_target_declaration(target_id);
        if (!target_declaration) {
            return fail("FSIM-ELAB-SVCLI-003",
                "$value$plusargs target must be a direct writable packed "
                "or string variable");
        }
        InputScanConversion conversion;
        conversion.prefix = parsed_conversion.prefix;
        conversion.format = runtime_input_format(parsed_conversion.format);
        conversion.maximum_characters
            = parsed_conversion.maximum_characters;
        if (const auto string_binding = hir_string_binding(
                *target_declaration, hir_process_scope_, true)) {
            if (!text_input_format(parsed_conversion.format)) {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs numeric conversion requires a packed "
                    "target");
            }
            if (string_binding->kind == HirStringBindingKind::local
                && string_binding->local) {
                conversion.target = {
                    InputScanTargetKind::string_register,
                    *string_binding->local, 1U, false,
                };
            } else if (string_binding->object
                && !read_only_string_objects_.contains(
                    *string_binding->object)) {
                conversion.target = {
                    InputScanTargetKind::string_object,
                    *string_binding->object, 1U, false,
                };
            } else {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs string target is read-only");
            }
        } else {
            const auto binding = hir_runtime_binding(
                *target_declaration, hir_process_scope_, true);
            if (!binding || binding->width == 0U
                || binding->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs target has no writable packed "
                    "storage");
            }
            const auto kind = hir_systemverilog_scalar_kind(target_id);
            const auto real_target
                = kind == frontend::SystemVerilogScalarKind::ShortReal
                || kind == frontend::SystemVerilogScalarKind::Real
                || kind == frontend::SystemVerilogScalarKind::Realtime;
            const auto format_matches
                = kind == frontend::SystemVerilogScalarKind::None
                ? parsed_conversion.format
                    != frontend::InputScanFormat::Real
                : real_target
                ? parsed_conversion.format
                    == frontend::InputScanFormat::Real
                : kind == frontend::SystemVerilogScalarKind::Time
                ? parsed_conversion.format
                        == frontend::InputScanFormat::Decimal
                    || parsed_conversion.format
                        == frontend::InputScanFormat::UnsignedDecimal
                    || parsed_conversion.format
                        == frontend::InputScanFormat::Real
                : kind == frontend::SystemVerilogScalarKind::Chandle
                ? parsed_conversion.format
                    == frontend::InputScanFormat::Hexadecimal
                : false;
            if (!format_matches) {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs conversion is incompatible with the "
                    "target type");
            }
            conversion.target.width = static_cast<std::uint32_t>(
                binding->width);
            conversion.target.scalar_kind = kind;
            conversion.target.two_state
                = kind != frontend::SystemVerilogScalarKind::None
                || binding->domain == frontend::ValueDomain::Bit2;
            if (binding->kind == HirRuntimeBindingKind::local
                && binding->local) {
                conversion.target.kind
                    = InputScanTargetKind::packed_register;
                conversion.target.id = *binding->local;
            } else if (binding->signal
                && !read_only_signals_.contains(*binding->signal)) {
                conversion.target.kind = InputScanTargetKind::packed_signal;
                conversion.target.id = *binding->signal;
            } else {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs target is read-only");
            }
        }

        const auto query = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant {
            query, parsed_conversion.prefix });
        const auto selected = allocate_string_register();
        const auto found = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(PlusArgSelect {
            found, query, selected });
        const auto scanned = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        const auto success = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(FileScan {
            scanned,
            0U,
            selected,
            true,
            { std::move(conversion) },
            parsed.trailing_text,
            false,
            success,
            false,
        });
        const auto expanded = resize_register(success, 32U, false);
        return expected_width != 0U && expected_width != 32U
            ? std::optional {
                  resize_register(expanded, expected_width, false) }
            : std::optional { expanded };
    }

    if (call.text == "$fopen") {
        if (call.operands.empty() || call.operands.size() > 2U
            || !hir_expression_is_string(
                call.operands.front(), hir_process_scope_)
            || (call.operands.size() == 2U
                && !hir_expression_is_string(
                    call.operands.back(), hir_process_scope_))) {
            return fail("FSIM-ELAB-SVFILE-003",
                "$fopen requires a byte-string filename and optional mode");
        }
        const auto path = lower_hir_string_expression(call.operands.front());
        auto mode = call.operands.size() == 2U
            ? lower_hir_string_expression(call.operands.back())
            : std::optional<StringRegisterId> { };
        if (!mode) {
            mode = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                *mode, "\x1f" "fsim-multichannel-write" });
        }
        if (!path) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(FileOpen {
            destination, *path, *mode });
        return finish(destination);
    }
    if (call.text == "$fgets") {
        if (call.operands.size() != 2U) {
            return fail("FSIM-ELAB-SVFILE-004",
                "$fgets requires a target and integer handle");
        }
        const auto target = text_target(call.operands[0]);
        const auto handle = lower_integer(call.operands[1]);
        if (!target || !handle) {
            return fail("FSIM-ELAB-SVFILE-002",
                "$fgets target must be a writable packed or string variable");
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        FileReadLine operation {
            destination, *handle, target->id, 0U, FileReadKind::line
        };
        operation.target_kind = target->kind;
        operation.target_width = target->width;
        process_.operations.emplace_back(operation);
        if (target->copied_object) {
            process_.operations.emplace_back(WriteStringObject {
                *target->copied_object, target->id });
        }
        return finish(destination);
    }
    if (call.text == "$fgetc" || call.text == "$feof"
        || call.text == "$ftell" || call.text == "$rewind") {
        if (call.operands.size() != 1U) {
            return fail("FSIM-ELAB-SVFILE-009",
                call.text + " requires one integer handle");
        }
        const auto handle = lower_integer(call.operands.front());
        if (!handle) {
            return fail("FSIM-ELAB-SVFILE-001",
                call.text + " handle must be an integer expression");
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        if (call.text == "$fgetc") {
            process_.operations.emplace_back(FileReadLine {
                destination, *handle, 0U, 0U, FileReadKind::character });
        } else if (call.text == "$feof") {
            process_.operations.emplace_back(FileEndOfFile {
                destination, *handle });
        } else {
            process_.operations.emplace_back(FilePosition {
                destination,
                *handle,
                0U,
                0U,
                call.text == "$ftell"
                    ? FilePositionKind::tell
                    : FilePositionKind::rewind,
            });
        }
        return finish(destination);
    }
    if (call.text == "$ungetc") {
        if (call.operands.size() != 2U) {
            return fail("FSIM-ELAB-SVFILE-010",
                "$ungetc requires a character and integer handle");
        }
        const auto character = lower_integer(call.operands[0]);
        const auto handle = lower_integer(call.operands[1]);
        if (!character || !handle) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(FileReadLine {
            destination, *handle, 0U, *character, FileReadKind::unget });
        return finish(destination);
    }
    if (call.text == "$ferror") {
        if (call.operands.size() != 2U) {
            return fail("FSIM-ELAB-SVFILE-006",
                "$ferror requires an integer handle and writable target");
        }
        const auto handle = lower_integer(call.operands[0]);
        const auto target = text_target(call.operands[1]);
        if (!handle || !target) {
            return fail("FSIM-ELAB-SVFILE-002",
                "$ferror target must be a writable packed or string variable");
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(FileErrorStatus {
            destination, *handle, target->id,
            target->kind, target->width,
        });
        if (target->copied_object) {
            process_.operations.emplace_back(WriteStringObject {
                *target->copied_object, target->id });
        }
        return finish(destination);
    }
    if (call.text == "$fseek") {
        if (call.operands.size() != 3U) {
            return fail("FSIM-ELAB-SVFILE-015",
                "$fseek requires handle, offset, and origin expressions");
        }
        const auto handle = lower_integer(call.operands[0]);
        const auto offset = lower_integer(call.operands[1]);
        const auto origin = lower_integer(call.operands[2]);
        if (!handle || !offset || !origin) {
            return fail("FSIM-ELAB-SVFILE-015",
                "$fseek arguments must be integral expressions");
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(FilePosition {
            destination, *handle, *offset, *origin, FilePositionKind::seek });
        return finish(destination);
    }

    if (call.text == "$fscanf" || call.text == "$sscanf") {
        const auto string_source = call.text == "$sscanf";
        if (call.operands.size() < 2U) {
            return fail("FSIM-ELAB-SVFILE-011",
                call.text + " requires source, literal format, and targets");
        }
        const auto format = specialized_hir_unit_->find_expression(
            call.operands[1]);
        if (!format || format->systemverilog == nullptr
            || format->systemverilog->kind
                != semantic::sv::ExpressionKind::string_literal
            || !format->systemverilog->decoded_string) {
            return fail("FSIM-ELAB-SVFILE-011",
                call.text + " format must be a string literal");
        }
        const auto parsed = frontend::parse_input_format(
            *format->systemverilog->decoded_string);
        const auto required = static_cast<std::size_t>(std::ranges::count_if(
            parsed.conversions,
            [](const auto& conversion) { return !conversion.suppress; }));
        if (!parsed.valid || parsed.conversions.empty()
            || parsed.conversions.size() > 64U
            || required != call.operands.size() - 2U) {
            return fail("FSIM-ELAB-SVFILE-011",
                call.text + " has an invalid or target-mismatched format");
        }
        FileScan operation;
        operation.string_source = string_source;
        if (string_source) {
            const auto source = lower_hir_string_expression(call.operands[0]);
            if (!source) {
                return fail("FSIM-ELAB-SVFILE-011",
                    "$sscanf source must be a byte-string expression");
            }
            operation.source = *source;
        } else {
            const auto handle = lower_integer(call.operands[0]);
            if (!handle) {
                return fail("FSIM-ELAB-SVFILE-011",
                    "$fscanf source must be an integer file handle");
            }
            operation.handle = *handle;
        }
        std::size_t target_index = 2U;
        for (const auto& parsed_conversion : parsed.conversions) {
            InputScanConversion conversion;
            conversion.prefix = parsed_conversion.prefix;
            conversion.format = runtime_input_format(
                parsed_conversion.format);
            conversion.maximum_characters
                = parsed_conversion.maximum_characters;
            conversion.suppress = parsed_conversion.suppress;
            if (conversion.suppress) {
                operation.conversions.push_back(std::move(conversion));
                continue;
            }
            const auto target_id = call.operands[target_index++];
            const auto target_declaration = hir_target_declaration(target_id);
            if (!target_declaration) {
                return fail("FSIM-ELAB-SVFILE-012",
                    "scan targets must be direct writable variables");
            }
            if (const auto string_binding = hir_string_binding(
                    *target_declaration, hir_process_scope_, true)) {
                if (!text_input_format(parsed_conversion.format)) {
                    return fail("FSIM-ELAB-SVFILE-012",
                        "numeric scan conversions require a packed target");
                }
                if (string_binding->kind == HirStringBindingKind::local
                    && string_binding->local) {
                    conversion.target = {
                        InputScanTargetKind::string_register,
                        *string_binding->local, 1U, false,
                    };
                } else if (string_binding->object
                    && !read_only_string_objects_.contains(
                        *string_binding->object)) {
                    conversion.target = {
                        InputScanTargetKind::string_object,
                        *string_binding->object, 1U, false,
                    };
                } else {
                    return fail("FSIM-ELAB-SVFILE-012",
                        "scan string target is read-only");
                }
            } else {
                const auto binding = hir_runtime_binding(
                    *target_declaration, hir_process_scope_, true);
                if (!binding || binding->width == 0U
                    || binding->width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return fail("FSIM-ELAB-SVFILE-012",
                        "scan target has no writable packed storage");
                }
                conversion.target.width = static_cast<std::uint32_t>(
                    binding->width);
                conversion.target.scalar_kind
                    = expression_scalar_kind(target_id);
                conversion.target.two_state
                    = conversion.target.scalar_kind
                        != frontend::SystemVerilogScalarKind::None
                    || binding->domain == frontend::ValueDomain::Bit2;
                if (binding->kind == HirRuntimeBindingKind::local
                    && binding->local) {
                    conversion.target.kind
                        = InputScanTargetKind::packed_register;
                    conversion.target.id = *binding->local;
                } else if (binding->signal
                    && !read_only_signals_.contains(*binding->signal)) {
                    conversion.target.kind
                        = InputScanTargetKind::packed_signal;
                    conversion.target.id = *binding->signal;
                } else {
                    return fail("FSIM-ELAB-SVFILE-012",
                        "scan target is read-only");
                }
            }
            operation.conversions.push_back(std::move(conversion));
        }
        operation.trailing_text = parsed.trailing_text;
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        operation.destination = destination;
        process_.operations.emplace_back(std::move(operation));
        return finish(destination);
    }

    if (call.text == "$fread") {
        if (call.operands.size() < 2U || call.operands.size() > 4U) {
            return fail("FSIM-ELAB-SVFILE-013",
                "$fread requires target, handle, and optional start/count");
        }
        FileBinaryRead operation;
        const auto target_id = call.operands.front();
        const auto target_name = hir_name(target_id);
        const auto object = target_name
            ? container_objects_.find(*target_name)
            : container_objects_.end();
        if (object != container_objects_.end()
            && !read_only_container_objects_.contains(*target_name)
            && static_cast<std::size_t>(object->second)
                < design_.container_object_info_.size()) {
            const auto& type
                = design_.container_object_info_[object->second].type;
            const auto width = container_packed_element_width(type);
            if (!type.fixed || !width
                || *width > std::numeric_limits<std::uint32_t>::max()) {
                return fail("FSIM-ELAB-SVFILE-014",
                    "$fread memory target must have fixed packed elements");
            }
            operation.target_kind = FileBinaryTargetKind::container_object;
            operation.target = object->second;
            operation.width = static_cast<std::uint32_t>(*width);
            operation.two_state = type.two_state;
            operation.scalar_kind = type.scalar_kind;
        } else {
            const auto declaration_id = hir_target_declaration(target_id);
            const auto binding = declaration_id
                ? hir_runtime_binding(
                    *declaration_id, hir_process_scope_, true)
                : std::nullopt;
            if (!binding || call.operands.size() != 2U
                || binding->width == 0U
                || binding->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return fail("FSIM-ELAB-SVFILE-014",
                    "$fread target must be a writable packed value or memory");
            }
            operation.width = static_cast<std::uint32_t>(binding->width);
            operation.scalar_kind = expression_scalar_kind(target_id);
            operation.two_state = operation.scalar_kind
                    != frontend::SystemVerilogScalarKind::None
                || binding->domain == frontend::ValueDomain::Bit2;
            if (binding->kind == HirRuntimeBindingKind::local
                && binding->local) {
                operation.target_kind
                    = FileBinaryTargetKind::packed_register;
                operation.target = *binding->local;
            } else if (binding->signal
                && !read_only_signals_.contains(*binding->signal)) {
                operation.target_kind
                    = FileBinaryTargetKind::packed_signal;
                operation.target = *binding->signal;
            } else {
                return fail("FSIM-ELAB-SVFILE-014",
                    "$fread target is read-only");
            }
        }
        const auto handle = lower_integer(call.operands[1]);
        if (!handle) {
            return fail("FSIM-ELAB-SVFILE-013",
                "$fread handle must be an integer expression");
        }
        operation.handle = *handle;
        if (call.operands.size() >= 3U) {
            const auto start = lower_integer(call.operands[2]);
            if (!start) {
                return std::nullopt;
            }
            operation.start = *start;
            operation.has_start = true;
        }
        if (call.operands.size() == 4U) {
            const auto count = lower_integer(call.operands[3]);
            if (!count) {
                return std::nullopt;
            }
            operation.count = *count;
            operation.has_count = true;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        operation.destination = destination;
        process_.operations.emplace_back(std::move(operation));
        return finish(destination);
    }
    return std::nullopt;
}

bool Lowerer::lower_hir_vhdl_file_statement(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_statement(statement_id)
        : std::nullopt;
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::procedure_call) {
        return false;
    }
    const auto& call = *statement->vhdl;
    const auto name = vhdl_simple_name(
        call.procedure.canonical.empty()
            ? std::string_view { call.procedure.spelling }
            : std::string_view { call.procedure.canonical });
    if (!vhdl_file_procedure_name(name)) {
        return false;
    }
    const auto span = hir_source_span(call.source);
    const auto fail = [&](std::string code, std::string message) {
        report(std::move(code), std::move(message), span);
        return true;
    };
    const auto actual = [&](const std::string_view formal,
                            const std::size_t position)
        -> std::optional<semantic::ExpressionId> {
        std::size_t positional { };
        for (const auto& association : call.procedure_arguments) {
            if (association.formal) {
                const auto& formal_name
                    = association.formal->canonical.empty()
                    ? association.formal->spelling
                    : association.formal->canonical;
                if (vhdl_simple_name(formal_name) == formal) {
                    return association.actual;
                }
                continue;
            }
            if (positional++ == position) {
                return association.actual;
            }
        }
        return std::nullopt;
    };
    const auto runtime_binding = [&](const semantic::ExpressionId value)
        -> std::optional<HirRuntimeBinding> {
        const auto declaration = hir_target_declaration(value);
        return declaration
            ? hir_runtime_binding(
                  *declaration, hir_process_scope_, true)
            : std::nullopt;
    };
    const auto string_binding = [&](const semantic::ExpressionId value)
        -> std::optional<HirStringBinding> {
        const auto declaration = hir_target_declaration(value);
        return declaration
            ? hir_string_binding(
                  *declaration, hir_process_scope_, true)
            : std::nullopt;
    };
    const auto file_binding = [&](const semantic::ExpressionId value)
        -> std::optional<HirRuntimeBinding> {
        const auto declaration_id = hir_target_declaration(value);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        if (!declaration || declaration->vhdl == nullptr
            || (declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::file
                && declaration->vhdl->object_class
                    != semantic::vhdl::ObjectClass::file)) {
            return std::nullopt;
        }
        return hir_runtime_binding(
            *declaration_id, hir_process_scope_, true);
    };
    const auto file_actual = [&](const std::size_t position)
        -> std::optional<std::pair<semantic::ExpressionId,
            HirRuntimeBinding>> {
        const auto expression = actual("f", position);
        const auto binding = expression
            ? file_binding(*expression)
            : std::nullopt;
        return expression && binding && binding->local
            ? std::optional { std::pair { *expression, *binding } }
            : std::nullopt;
    };
    const auto file_element_subtype = [&](const semantic::ExpressionId value)
        -> std::optional<semantic::vhdl::SubtypeIndication> {
        const auto declaration_id = hir_target_declaration(value);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return std::nullopt;
        }
        const auto subtype = hir_effective_vhdl_subtype(
            *declaration->vhdl->subtype);
        if (!subtype || !subtype->type_mark.target.valid()) {
            return std::nullopt;
        }
        const auto type = specialized_hir_unit_->find_type(
            subtype->type_mark.target);
        if (!type || type->vhdl == nullptr
            || type->vhdl->form != semantic::vhdl::TypeForm::file
            || !type->vhdl->element_subtype) {
            return std::nullopt;
        }
        return hir_effective_vhdl_subtype(
            *type->vhdl->element_subtype);
    };
    enum class TextioScalarClass : std::uint8_t {
        unsupported,
        integer,
        boolean,
        bit,
    };
    const auto textio_scalar_class = [&](const semantic::ExpressionId value) {
        const auto expression = specialized_hir_unit_->find_expression(
            value);
        if (expression && expression->vhdl != nullptr) {
            switch (expression->vhdl->kind) {
            case semantic::vhdl::ExpressionKind::integer_literal:
                return TextioScalarClass::integer;
            case semantic::vhdl::ExpressionKind::boolean_literal:
                return TextioScalarClass::boolean;
            case semantic::vhdl::ExpressionKind::logic_literal:
                return TextioScalarClass::bit;
            default:
                break;
            }
        }
        const auto subtype = hir_vhdl_expression_subtype(value);
        if (!subtype) {
            return TextioScalarClass::unsupported;
        }
        const auto subtype_name = vhdl_simple_name(
            subtype->type_mark.spelling);
        if (subtype_name == "integer" || subtype_name == "natural"
            || subtype_name == "positive") {
            return TextioScalarClass::integer;
        }
        if (subtype_name == "boolean") {
            return TextioScalarClass::boolean;
        }
        if (subtype_name == "bit") {
            return TextioScalarClass::bit;
        }
        auto type_id = subtype->type_mark.target;
        std::unordered_set<std::uint32_t> visiting;
        while (type_id.valid()
            && visiting.insert(type_id.value()).second) {
            const auto type = specialized_hir_unit_->find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                break;
            }
            if (type->vhdl->form
                == semantic::vhdl::TypeForm::enumeration) {
                return TextioScalarClass::unsupported;
            }
            if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                && type->vhdl->form
                    != semantic::vhdl::TypeForm::alias) {
                break;
            }
            type_id = type->vhdl->base.type_mark.target;
        }
        return subtype->domain == semantic::vhdl::ValueDomain::integer
            ? TextioScalarClass::integer
            : subtype->domain == semantic::vhdl::ValueDomain::boolean
            ? TextioScalarClass::boolean
            : subtype->domain == semantic::vhdl::ValueDomain::bit2
                    && subtype->executable_width
                    && *subtype->executable_width == 1U
            ? TextioScalarClass::bit
            : TextioScalarClass::unsupported;
    };

    if (name == "readline" || name == "writeline") {
        const auto file = file_actual(0U);
        const auto element_subtype = file
            ? file_element_subtype(file->first)
            : std::nullopt;
        const auto line = actual("l", 1U);
        const auto line_local = line ? string_binding(*line) : std::nullopt;
        if (call.procedure_arguments.size() != 2U || !file
            || !element_subtype
            || element_subtype->domain
                != semantic::vhdl::ValueDomain::string
            || !line_local || !line_local->local) {
            return fail("FSIM-ELAB-VHTEXTIO-001",
                std::string { name }
                    + " requires a text file and writable line object");
        }
        if (name == "readline") {
            const auto count = allocate_register(
                32U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(FileReadLine {
                count, *file->second.local, *line_local->local, 0U,
                FileReadKind::line, true });
        } else {
            process_.operations.emplace_back(FileWriteString {
                *file->second.local, *line_local->local,
                { }, { }, true, true });
        }
        return true;
    }

    bool status_form { };
    if (name == "file_open") {
        status_form = call.procedure_arguments.size() == 4U
            || std::ranges::any_of(
                call.procedure_arguments,
                [](const semantic::vhdl::ProcedureAssociation& association) {
                    return association.formal
                        && vhdl_simple_name(
                            association.formal->canonical.empty()
                                ? association.formal->spelling
                                : association.formal->canonical)
                            == "status";
                });
    }
    const auto file = file_actual(status_form ? 1U : 0U);
    if ((name == "file_open" || name == "file_close") && !file) {
        return fail("FSIM-ELAB-VHFILE-005",
            "VHDL file operation requires a whole file object");
    }
    if (name == "file_close") {
        if (call.procedure_arguments.size() != 1U) {
            return fail("FSIM-ELAB-VHFILE-006",
                "file_close requires exactly one file object");
        }
        process_.operations.emplace_back(FileClose {
            *file->second.local, true, false });
        return true;
    }
    if (name == "file_open") {
        const auto path = actual(
            "external_name", status_form ? 2U : 1U);
        const auto kind = actual(
            "open_kind", status_form ? 3U : 2U);
        const auto minimum = status_form ? 3U : 2U;
        const auto maximum = status_form ? 4U : 3U;
        if (call.procedure_arguments.size() < minimum
            || call.procedure_arguments.size() > maximum || !path
            || !hir_expression_is_string(*path, hir_process_scope_)) {
            return fail("FSIM-ELAB-VHFILE-007",
                "file_open requires a file object, string logical name, "
                "and optional file_open_kind");
        }
        const auto kind_expression = kind
            ? specialized_hir_unit_->find_expression(*kind)
            : std::nullopt;
        auto kind_name = kind_expression
                && kind_expression->vhdl != nullptr
            ? std::string_view { kind_expression->vhdl->text }
            : std::string_view { "read_mode" };
        kind_name = vhdl_simple_name(kind_name);
        const auto mode_text = kind_name == "read_mode"
            ? std::string_view { "r" }
            : kind_name == "write_mode"
            ? std::string_view { "w" }
            : kind_name == "append_mode"
            ? std::string_view { "a" }
            : std::string_view { };
        if (mode_text.empty()) {
            return fail("FSIM-ELAB-VHFILE-004",
                "VHDL file open kind must be read_mode, write_mode, "
                "or append_mode");
        }
        std::optional<RegisterId> status;
        if (status_form) {
            const auto status_actual = actual("status", 0U);
            const auto status_binding = status_actual
                ? runtime_binding(*status_actual)
                : std::nullopt;
            const auto status_subtype = status_actual
                ? hir_vhdl_expression_subtype(*status_actual)
                : std::nullopt;
            const auto status_type = status_subtype
                ? vhdl_simple_name(
                      status_subtype->type_mark.spelling)
                : std::string_view { };
            if (!status_binding || !status_binding->local
                || status_type != "file_open_status") {
                return fail("FSIM-ELAB-VHFILE-008",
                    "file_open status actual must be a writable "
                    "file_open_status object");
            }
            status = *status_binding->local;
        }
        const auto path_register = lower_hir_string_expression(*path);
        if (!path_register) {
            return false;
        }
        const auto mode_register = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant {
            mode_register, std::string { mode_text } });
        process_.operations.emplace_back(FileOpen {
            *file->second.local, *path_register, mode_register,
            status, true });
        return true;
    }

    const auto first = actual("l", 0U);
    const auto line = first ? string_binding(*first) : std::nullopt;
    if (line && line->local) {
        const auto value = actual("value", 1U);
        if (!value) {
            return fail("FSIM-ELAB-VHTEXTIO-003",
                "TextIO read/write requires a value actual");
        }
        if (name == "read") {
            if (call.procedure_arguments.size() < 2U
                || call.procedure_arguments.size() > 3U) {
                return fail("FSIM-ELAB-VHTEXTIO-003",
                    "TextIO read accepts line, value, and optional good "
                    "actuals");
            }
            const auto target = runtime_binding(*value);
            if (!target || !target->local || target->width == 0U
                || target->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return fail("FSIM-ELAB-VHTEXTIO-004",
                    "TextIO read value must be a writable bounded scalar");
            }
            InputScanConversion conversion;
            conversion.target = {
                InputScanTargetKind::packed_register,
                *target->local,
                static_cast<std::uint32_t>(target->width),
                true,
            };
            const auto value_class = textio_scalar_class(*value);
            if (value_class == TextioScalarClass::integer) {
                conversion.format = InputScanFormat::decimal;
            } else if (value_class == TextioScalarClass::boolean) {
                conversion.format = InputScanFormat::boolean_value;
            } else if (value_class == TextioScalarClass::bit) {
                conversion.format = InputScanFormat::binary;
            } else {
                return fail("FSIM-ELAB-VHTEXTIO-005",
                    "bounded TextIO read supports integer, boolean, "
                    "and bit values");
            }
            std::optional<RegisterId> success;
            if (const auto good = actual("good", 2U)) {
                const auto good_binding = runtime_binding(*good);
                if (!good_binding || !good_binding->local
                    || good_binding->domain
                        != frontend::ValueDomain::Boolean) {
                    return fail("FSIM-ELAB-VHTEXTIO-006",
                        "TextIO read good actual must be a writable "
                        "boolean");
                }
                success = *good_binding->local;
            } else if (vhdl_standard_
                >= frontend::VhdlStandard::Vhdl2019) {
                success = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
            }
            const auto count = allocate_register(
                32U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(FileScan {
                count, 0U, *line->local, true,
                { std::move(conversion) }, { }, !success,
                success, true });
            if (!actual("good", 2U)
                && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
                const auto branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    *success, branch + 2U, branch + 1U,
                    UnknownBranchPolicy::when_false });
                process_.operations.emplace_back(VhdlAssertApi {
                    VhdlAssertApiKind::record_read_failure,
                    std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, std::nullopt, std::nullopt,
                    SourceLocation {
                        span.source_name.str(),
                        static_cast<std::uint32_t>(span.begin.line),
                        static_cast<std::uint32_t>(span.begin.column),
                    },
                });
            }
            return true;
        }

        if (call.procedure_arguments.size() < 2U
            || call.procedure_arguments.size() > 4U) {
            return fail("FSIM-ELAB-VHTEXTIO-003",
                "TextIO write accepts line, value, justified, and field "
                "actuals");
        }
        bool left_justify { };
        if (const auto justified = actual("justified", 2U)) {
            const auto expression
                = specialized_hir_unit_->find_expression(*justified);
            auto justification = expression && expression->vhdl != nullptr
                ? std::string_view { expression->vhdl->text }
                : std::string_view { };
            justification = vhdl_simple_name(justification);
            if (justification != "left" && justification != "right") {
                return fail("FSIM-ELAB-VHTEXTIO-007",
                    "bounded TextIO justification must be static left or "
                    "right");
            }
            left_justify = justification == "left";
        }
        std::uint32_t minimum_width { };
        if (const auto field = actual("field", 3U)) {
            const auto width = hir_constant_integer(*field);
            if (!width || *width < 0
                || *width
                    > static_cast<std::int64_t>(maximum_string_bytes)) {
                return fail("FSIM-ELAB-VHTEXTIO-008",
                    "bounded TextIO field must be a static value in "
                    "0..4096");
            }
            minimum_width = static_cast<std::uint32_t>(*width);
        }
        const auto append_string = [&](const StringRegisterId source) {
            StringMethod operation;
            operation.operation = StringMethodOperator::format_string;
            operation.source = *line->local;
            operation.argument = source;
            operation.minimum_width = minimum_width;
            operation.left_justify = left_justify;
            process_.operations.emplace_back(operation);
        };
        if (hir_expression_is_string(*value, hir_process_scope_)) {
            const auto lowered = lower_hir_string_expression(*value);
            if (!lowered) {
                return false;
            }
            append_string(*lowered);
            return true;
        }
        const auto domain = hir_expression_domain(
            *value, hir_process_scope_);
        const auto width = hir_expression_width(
            *value, hir_process_scope_);
        if (!domain || !width || *width == 0U) {
            return false;
        }
        const auto value_class = textio_scalar_class(*value);
        if (value_class == TextioScalarClass::integer
            || value_class == TextioScalarClass::bit) {
            const auto lowered = lower_hir_expression(*value, *width);
            if (!lowered) {
                return false;
            }
            StringMethod operation;
            operation.operation = StringMethodOperator::format_packed;
            operation.source = *line->local;
            operation.first = *lowered;
            operation.second = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                operation.second, unsigned_value(*width, 32U) });
            operation.format
                = value_class == TextioScalarClass::integer
                ? OutputFormat::decimal
                : OutputFormat::binary;
            operation.signed_decimal
                = value_class == TextioScalarClass::integer;
            operation.minimum_width = minimum_width;
            operation.left_justify = left_justify;
            process_.operations.emplace_back(operation);
            return true;
        }
        if (value_class == TextioScalarClass::boolean) {
            const auto condition = lower_hir_expression(*value, 1U);
            if (!condition) {
                return false;
            }
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *condition, 0U, 0U, UnknownBranchPolicy::error });
            const auto true_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto true_text = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                true_text, "TRUE" });
            append_string(true_text);
            const auto skip_false = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { });
            const auto false_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto false_text = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                false_text, "FALSE" });
            append_string(false_text);
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *condition, true_start, false_start,
                UnknownBranchPolicy::error };
            process_.operations[skip_false] = Jump { end };
            return true;
        }
        return fail("FSIM-ELAB-VHTEXTIO-009",
            "bounded TextIO write supports integer, boolean, bit, and "
            "string values");
    }

    const auto direct_file = file_actual(0U);
    const auto value = actual("value", 1U);
    if (!direct_file || !value
        || call.procedure_arguments.size() != 2U) {
        return fail("FSIM-ELAB-VHFILE-010",
            "direct VHDL file read/write requires a file object and one "
            "value");
    }
    const auto element_subtype = file_element_subtype(
        direct_file->first);
    if (!element_subtype
        || element_subtype->domain
            != semantic::vhdl::ValueDomain::integer
        || !element_subtype->executable_width
        || *element_subtype->executable_width == 0U
        || *element_subtype->executable_width > 32U) {
        return fail("FSIM-ELAB-VHFILE-011",
            "direct VHDL file I/O supports bounded integer elements");
    }
    if (name == "write") {
        auto lowered = lower_hir_expression(*value, 32U);
        if (!lowered) {
            return false;
        }
        if (register_width(*lowered) != 32U) {
            lowered = resize_register(*lowered, 32U, true);
        }
        process_.operations.emplace_back(FileWriteFormatted {
            *direct_file->second.local,
            *lowered,
            32U,
            OutputFormat::decimal,
            { },
            { },
            true,
            true,
        });
        return true;
    }
    const auto target = runtime_binding(*value);
    if (!target || !target->local
        || target->domain != frontend::ValueDomain::Integer) {
        return fail("FSIM-ELAB-VHFILE-012",
            "direct VHDL file read target must be a writable integer "
            "variable");
    }
    InputScanConversion conversion;
    conversion.format = InputScanFormat::decimal;
    conversion.target = {
        InputScanTargetKind::packed_register,
        *target->local,
        32U,
        true,
    };
    const auto count = allocate_register(
        32U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(FileScan {
        count,
        *direct_file->second.local,
        0U,
        false,
        { std::move(conversion) },
        { },
        true,
    });
    return true;
}

bool Lowerer::lower_hir_systemverilog_file_statement(
    const semantic::StatementId statement_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->systemverilog == nullptr) {
        return false;
    }
    const auto& source = *statement->systemverilog;
    const auto lower_integer = [&](const semantic::ExpressionId expression)
        -> std::optional<RegisterId> {
        auto value = lower_hir_expression(
            expression,
            hir_expression_width(expression, hir_process_scope_)
                .value_or(32U));
        if (value && register_width(*value) != 32U) {
            value = resize_register(
                *value, 32U, hir_expression_signed(expression));
        }
        return value;
    };
    if (source.kind == semantic::sv::StatementKind::file_display) {
        if (!source.file_handle) {
            return false;
        }
        const auto handle = lower_integer(*source.file_handle);
        if (!handle) {
            return false;
        }
        const auto expression_scalar_kind = [&]
            (const semantic::ExpressionId expression) {
                const auto declaration_id = hir_referenced_declaration(
                    expression);
                const auto declaration = declaration_id
                    ? specialized_hir_unit_->find_declaration(
                          *declaration_id)
                    : std::nullopt;
                return declaration
                        && declaration->systemverilog != nullptr
                        && declaration->systemverilog->type
                    ? scalar_kind(
                          declaration->systemverilog->type
                              ->target.spelling)
                    : frontend::SystemVerilogScalarKind::None;
            };
        const auto scalar_format_matches = [&]
            (const semantic::ExpressionId expression,
             const semantic::sv::OutputFormat format) {
                const auto kind = expression_scalar_kind(expression);
                const auto real_kind
                    = kind == frontend::SystemVerilogScalarKind::ShortReal
                    || kind == frontend::SystemVerilogScalarKind::Real
                    || kind == frontend::SystemVerilogScalarKind::Realtime;
                const auto real_format
                    = format == semantic::sv::OutputFormat::real_scientific
                    || format == semantic::sv::OutputFormat::real_fixed
                    || format == semantic::sv::OutputFormat::real_general;
                return real_kind ? real_format
                    : kind == frontend::SystemVerilogScalarKind::Time
                    ? format == semantic::sv::OutputFormat::time
                        || format == semantic::sv::OutputFormat::decimal
                    : kind == frontend::SystemVerilogScalarKind::Chandle
                    ? format == semantic::sv::OutputFormat::hexadecimal
                    : !real_format
                        && format != semantic::sv::OutputFormat::time;
            };
        if (source.output_postponed) {
            MonitorInstall monitor;
            monitor.file_handle = *handle;
            monitor.newline = source.output_newline;
            monitor.one_shot = !source.output_monitor;
            const auto task_name = source.output_monitor
                ? std::string_view { "$fmonitor" }
                : std::string_view { "$fstrobe" };
            if (source.output_values.empty() && !source.output_format) {
                monitor.trailing_text = source.output_text;
                process_.operations.emplace_back(std::move(monitor));
                return true;
            }
            bool valid = true;
            std::string pending_prefix;
            const auto append_value = [&]
                (const semantic::sv::OutputValue& output) {
                    auto prefix = std::move(pending_prefix)
                        + output.prefix;
                    if (output.format
                        == semantic::sv::OutputFormat::hierarchy) {
                        pending_prefix = std::move(prefix) + hierarchy_;
                        return;
                    }
                    MonitorValue value;
                    value.prefix = std::move(prefix);
                    value.minimum_width = output.minimum_width;
                    value.left_justify = output.left_justify;
                    value.zero_pad = output.zero_pad;
                    value.suppress_leading_zero
                        = output.suppress_leading_zero;
                    if (output.format
                        == semantic::sv::OutputFormat::time) {
                        value.kind = MonitorValueKind::time;
                        value.use_timeformat_width
                            = use_systemverilog_timeformat_width(
                                output.minimum_width,
                                output.suppress_leading_zero);
                        monitor.values.push_back(std::move(value));
                        return;
                    }
                    const auto expression = output.value
                        ? specialized_hir_unit_->find_expression(
                              *output.value)
                        : std::nullopt;
                    const auto span = expression
                            && expression->systemverilog != nullptr
                        ? hir_source_span(expression->systemverilog->source)
                        : hir_source_span(source.source);
                    if (!output.value
                        || !scalar_format_matches(
                            *output.value, output.format)) {
                        report("FSIM-ELAB-SVFILE-007",
                            std::string { task_name }
                                + " conversion is incompatible with the "
                                  "value type",
                            span);
                        valid = false;
                        return;
                    }
                    if (!expression
                        || expression->systemverilog == nullptr
                        || expression->systemverilog->kind
                            != semantic::sv::ExpressionKind::name) {
                        report("FSIM-ELAB-SVFILE-007",
                            std::string { task_name }
                                + " requires direct packed-signal value "
                                  "expressions",
                            span);
                        valid = false;
                        return;
                    }
                    const auto declaration = hir_referenced_declaration(
                        *output.value);
                    const auto binding = declaration
                        ? hir_runtime_binding(
                              *declaration, hir_process_scope_, false)
                        : std::nullopt;
                    if (!binding || !binding->signal) {
                        report("FSIM-ELAB-020",
                            "unknown file-strobe signal '"
                                + expression->systemverilog->text + "'",
                            span);
                        valid = false;
                        return;
                    }
                    value.kind = MonitorValueKind::signal;
                    value.signal = *binding->signal;
                    value.format = runtime_output_format(output.format);
                    value.scalar_kind = expression_scalar_kind(
                        *output.value);
                    value.signed_decimal
                        = value.format
                            == runtime::simir::OutputFormat::decimal
                        && hir_expression_signed(*output.value);
                    monitor.values.push_back(std::move(value));
                };
            if (!source.output_values.empty()) {
                for (const auto& output : source.output_values) {
                    append_value(output);
                }
                monitor.trailing_text = std::move(pending_prefix)
                    + source.output_trailing_text;
            } else {
                append_value(semantic::sv::OutputValue {
                    source.value,
                    *source.output_format,
                    source.output_prefix,
                    source.output_suppress_leading_zero,
                    source.output_minimum_width,
                    source.output_left_justify,
                    source.output_zero_pad,
                });
                monitor.trailing_text = std::move(pending_prefix)
                    + source.output_suffix;
            }
            if (valid && (!monitor.values.empty()
                    || !monitor.trailing_text.empty())) {
                process_.operations.emplace_back(std::move(monitor));
            }
            return true;
        }
        if (source.output_monitor) {
            return false;
        }
        const auto lower_output = [&]
            (const semantic::sv::OutputValue& output,
             std::string suffix,
             const bool newline) {
                if (output.format == semantic::sv::OutputFormat::hierarchy) {
                    process_.operations.emplace_back(FileWriteLiteral {
                        *handle,
                        output.prefix + hierarchy_ + std::move(suffix),
                        newline,
                    });
                    return true;
                }
                if (!output.value) {
                    return false;
                }
                if ((output.format == semantic::sv::OutputFormat::string
                        || output.format
                            == semantic::sv::OutputFormat::decimal)
                    && hir_expression_is_string(
                        *output.value, hir_process_scope_)) {
                    const auto value = lower_hir_string_expression(
                        *output.value);
                    if (!value) {
                        return false;
                    }
                    process_.operations.emplace_back(FileWriteString {
                        *handle, *value, output.prefix,
                        std::move(suffix), newline,
                    });
                    return true;
                }
                const auto width = hir_expression_width(
                    *output.value, hir_process_scope_).value_or(32U);
                const auto value = lower_hir_expression(
                    *output.value, width);
                if (!value
                    || width > std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                const auto format = runtime_output_format(output.format);
                const auto kind = expression_scalar_kind(*output.value);
                process_.operations.emplace_back(FileWriteFormatted {
                    *handle,
                    *value,
                    static_cast<std::uint32_t>(width),
                    format,
                    output.prefix,
                    std::move(suffix),
                    newline,
                    format == runtime::simir::OutputFormat::decimal
                        && hir_expression_signed(*output.value),
                    output.suppress_leading_zero,
                    output.minimum_width,
                    output.left_justify,
                    output.zero_pad,
                    kind,
                });
                return true;
            };
        if (!source.output_values.empty()) {
            for (std::size_t index { };
                index < source.output_values.size(); ++index) {
                const auto last = index + 1U
                    == source.output_values.size();
                if (!lower_output(
                        source.output_values[index],
                        last ? source.output_trailing_text
                             : std::string { },
                        last && source.output_newline)) {
                    return false;
                }
            }
            return true;
        }
        if (!source.output_format) {
            process_.operations.emplace_back(FileWriteLiteral {
                *handle, source.output_text, source.output_newline });
            return true;
        }
        if (!source.value) {
            return false;
        }
        return lower_output(
            semantic::sv::OutputValue {
                *source.value,
                *source.output_format,
                source.output_prefix,
                source.output_suppress_leading_zero,
                source.output_minimum_width,
                source.output_left_justify,
                source.output_zero_pad,
            },
            source.output_suffix,
            source.output_newline);
    }
    if (source.kind != semantic::sv::StatementKind::memory_transfer
        || !source.value || !source.target) {
        return false;
    }
    const auto path = lower_hir_string_expression(*source.value);
    const auto name = hir_name(*source.target);
    const auto object = name
        ? container_objects_.find(*name)
        : container_objects_.end();
    if (!path || object == container_objects_.end()
        || (!source.memory_write
            && read_only_container_objects_.contains(*name))
        || static_cast<std::size_t>(object->second)
            >= design_.container_object_info_.size()) {
        report("FSIM-ELAB-SVMEMORY-003",
            "memory transfer requires a writable fixed-array object",
            hir_source_span(source.source));
        return true;
    }
    const auto& type = design_.container_object_info_[object->second].type;
    if (!type.fixed) {
        report("FSIM-ELAB-SVMEMORY-003",
            "$readmem*/$writemem* requires a bounded static array",
            hir_source_span(source.source));
        return true;
    }
    const auto target = allocate_container_register(type);
    process_.operations.emplace_back(ReadContainerObject {
        target, object->second });
    const auto lower_bound = [&](const std::size_t index)
        -> std::optional<RegisterId> {
        return index < source.task_arguments.size()
                && source.task_arguments[index].actual
            ? lower_integer(*source.task_arguments[index].actual)
            : std::optional<RegisterId> { };
    };
    const auto start = lower_bound(0U);
    const auto finish = lower_bound(1U);
    process_.operations.emplace_back(LoadMemory {
        target, *path, start, finish,
        source.memory_hex, source.memory_write,
    });
    if (!source.memory_write) {
        process_.operations.emplace_back(WriteContainerObject {
            object->second, target, std::nullopt });
    }
    return true;
}

} // namespace fsim::elaboration

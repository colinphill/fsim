// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "hir_output_conversion.hpp"


#include "fsim/frontend/input_format.hpp"
#include "fsim/frontend/output_format.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace fsim::elaboration {
namespace {

    using runtime::simir::vhdl_textio_radix_read_2008_marker;
    using runtime::simir::vhdl_textio_read_2008_marker;
    using runtime::simir::vhdl_textio_read_marker;
    using runtime::simir::vhdl_textio_sread_marker;

[[nodiscard]] bool hir_file_call_name(const std::string_view name) noexcept
{
    return name == "$fopen" || name == "$fgets" || name == "$fgetc"
        || name == "$ungetc" || name == "$feof" || name == "$ferror"
        || name == "$fscanf" || name == "$sscanf" || name == "$fread"
        || name == "$fseek" || name == "$ftell" || name == "$rewind"
        || name == "$test$plusargs" || name == "$value$plusargs";
}

[[nodiscard]] bool hir_sscanf_unknown_integral_format(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::Expression& call)
{
    // A variable with an X/Z initializer is not a language constant
    // expression. Do not fold it here and skip its run-time evaluation.
    if (call.text != "$sscanf" || call.operands.size() < 2U
        || !hir_systemverilog_constant_expression(
            specialization, call.operands[1])) {
        return false;
    }
    const auto bits = specialization.evaluate_systemverilog_bits_expression(
        call.operands[1]);
    return bits && bits->find_first_of("xXzZ") != std::string::npos;
}

[[nodiscard]] bool hir_sscanf_unknown_format_simple_source(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::Expression& call)
{
    if (call.text != "$sscanf" || call.operands.size() < 2U) {
        return false;
    }
    const auto source = specialization.find_expression(call.operands[0]);
    return source && source->systemverilog != nullptr
        && (source->systemverilog->kind
                == semantic::sv::ExpressionKind::name
            || source->systemverilog->kind
                == semantic::sv::ExpressionKind::string_literal);
}

[[nodiscard]] bool text_input_format(
    const frontend::InputScanFormat format) noexcept
{
    return format == frontend::InputScanFormat::String
        || format == frontend::InputScanFormat::Character
        || format == frontend::InputScanFormat::Hierarchy;
}

[[nodiscard]] bool output_conversion_consumes_value(
    const frontend::OutputFormat format) noexcept
{
    return format != frontend::OutputFormat::Hierarchy
        && format != frontend::OutputFormat::Library
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
        || simple == "read" || simple == "write" || simple == "swrite"
        || simple == "sread" || simple == "string_read"
        || simple == "hread" || simple == "hex_read"
        || simple == "oread" || simple == "octal_read"
        || simple == "hwrite" || simple == "hex_write"
        || simple == "owrite" || simple == "octal_write"
        || simple == "readline" || simple == "writeline"
        || simple == "tee" || simple == "flush"
        || simple == "file_rewind" || simple == "file_seek"
        || simple == "file_truncate";
}

// The VHDL-2019 file functions other than ENDFILE (IEEE 1076-2019 5.5.2);
// FILE_OPEN is also an impure function.
[[nodiscard]] std::optional<FilePositionKind> vhdl_file_function_kind(
    const std::string_view simple) noexcept
{
    return simple == "file_state" ? std::optional { FilePositionKind::state }
        : simple == "file_mode"   ? std::optional { FilePositionKind::mode }
        : simple == "file_position"
        ? std::optional { FilePositionKind::position }
        : simple == "file_size" ? std::optional { FilePositionKind::size }
        : simple == "file_canseek"
        ? std::optional { FilePositionKind::canseek }
        : std::nullopt;
}

// The mode string a FILE_OPEN_KIND literal opens with (IEEE 1076-2019
// 5.5.2, 16.3).
[[nodiscard]] std::string_view vhdl_file_open_mode(
    const std::string_view kind) noexcept
{
    return kind == "read_mode"   ? std::string_view { "r" }
        : kind == "write_mode"   ? std::string_view { "w" }
        : kind == "append_mode"  ? std::string_view { "a" }
        : kind == "read_write_mode" ? std::string_view { "r+" }
                                    : std::string_view { };
}

} // namespace

Lowerer::HirSystemVerilogScanValueKind
Lowerer::hir_systemverilog_scan_value_kind(
    const semantic::ExpressionId expression_id) const
{
    using Kind = HirSystemVerilogScanValueKind;
    using TypeForm = semantic::sv::TypeForm;
    if (specialized_hir_unit_ == nullptr) {
        return Kind::unsupported;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return Kind::unsupported;
    }
    const auto& source = *expression->systemverilog;
    if (hir_expression_is_string(expression_id, source.scope)) {
        return Kind::string;
    }
    if (source.kind != semantic::sv::ExpressionKind::name) {
        return Kind::unsupported;
    }
    const auto declared = hir_systemverilog_value_type(expression_id);
    if (!declared) {
        return Kind::unsupported;
    }
    const auto invalid_shape = [](const semantic::sv::TypeReference& type) {
        return type.container_form.has_value()
            || !type.unpacked_dimensions.empty()
            || !type.container_element_types.empty()
            || !type.class_identity.empty()
            || type.virtual_interface
            || !type.interface_type.empty()
            || !type.interface_modport.empty();
    };
    if (invalid_shape(*declared)) {
        return Kind::unsupported;
    }
    const auto scalar_kind = hir_systemverilog_scalar_kind(expression_id);
    if (scalar_kind == frontend::SystemVerilogScalarKind::Chandle) {
        return Kind::unsupported;
    }
    if (frontend::is_real_scalar_kind(scalar_kind)) {
        const auto width = hir_expression_width(expression_id, source.scope);
        return width && *width != 0U ? Kind::real : Kind::unsupported;
    }

    const auto resolver = hir_resolver();
    auto resolved = resolver.effective_systemverilog_type(
        *declared, source.scope).value_or(*declared);
    std::unordered_set<std::uint32_t> visited;
    std::optional<TypeForm> form;
    while (true) {
        if (invalid_shape(resolved)) {
            return Kind::unsupported;
        }
        if (!resolved.target.target.valid()) {
            form = resolved.value_form;
            break;
        }
        if (!visited.insert(resolved.target.target.value()).second) {
            return Kind::unsupported;
        }
        const auto definition = specialized_hir_unit_->find_type(
            resolved.target.target);
        if (!definition || definition->systemverilog == nullptr) {
            form = resolved.value_form;
            break;
        }
        const auto& definition_form = definition->systemverilog->form;
        const auto& type_definition = *definition->systemverilog;
        // The HIR form of a scalar typedef records its value category rather
        // than an alias marker. Follow that base unless it carries an
        // aggregate or enumeration payload of its own.
        const bool transparent_integral_alias
            = definition_form == TypeForm::packed_integral
            && type_definition.members.empty()
            && type_definition.enumeration_literals.empty()
            && !type_definition.container;
        if (definition_form != TypeForm::alias
            && !transparent_integral_alias) {
            form = definition_form;
            break;
        }
        const auto& base = type_definition.base;
        resolved = resolver.effective_systemverilog_type(
            base, source.scope).value_or(base);
    }
    if (!form) {
        return Kind::unsupported;
    }
    if (*form == TypeForm::string) {
        return Kind::string;
    }
    const auto width = hir_expression_width(expression_id, source.scope);
    if (!width || *width == 0U) {
        return Kind::unsupported;
    }
    const bool packed_integral = *form == TypeForm::packed_integral
        || *form == TypeForm::enumeration
        || *form == TypeForm::packed_structure
        || *form == TypeForm::packed_union;
    if (!packed_integral) {
        return Kind::unsupported;
    }
    // A width/domain alone is not proof of an integral type: event and
    // handles can have scalar storage too. Once aliases are followed, a
    // plain packed-integral form must name a standard integral type.
    if (*form == TypeForm::packed_integral
        && !frontend::systemverilog_integral_type_descriptor(
            resolved.target.spelling)) {
        return Kind::unsupported;
    }
    const auto domain = hir_expression_domain(expression_id, source.scope);
    if (!domain
        || (*domain != frontend::ValueDomain::Bit2
            && *domain != frontend::ValueDomain::Logic4
            && *domain != frontend::ValueDomain::Integer)) {
        return Kind::unsupported;
    }
    return Kind::packed_integral;
}

bool Lowerer::set_hir_string_scan_target(
    InputScanTarget& target, const HirStringBinding& binding) const
{
    if (binding.kind == HirStringBindingKind::local && binding.local) {
        target = { InputScanTargetKind::string_register, *binding.local, 1U,
            false };
        return true;
    }
    if (binding.object
        && !read_only_string_objects_.contains(*binding.object)) {
        target = { InputScanTargetKind::string_object, *binding.object, 1U,
            false };
        return true;
    }
    return false;
}

bool Lowerer::set_hir_packed_scan_target(
    InputScanTarget& target, const HirRuntimeBinding& binding) const
{
    if (binding.kind == HirRuntimeBindingKind::local && binding.local) {
        target.kind = InputScanTargetKind::packed_register;
        target.id = *binding.local;
        return true;
    }
    if (binding.signal && !read_only_signals_.contains(*binding.signal)) {
        target.kind = InputScanTargetKind::packed_signal;
        target.id = *binding.signal;
        return true;
    }
    return false;
}

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
        ? expression->vhdl->referenced_name->text()
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
    if ((vhdl_file_function_kind(vhdl_simple_name(name))
            || vhdl_simple_name(name) == "file_open")
        && !expression->vhdl->operands.empty()) {
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

std::pair<std::size_t, frontend::ValueDomain>
Lowerer::hir_vhdl_file_call_type(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    const auto name = expression && expression->vhdl != nullptr
            && expression->vhdl->referenced_name
        ? expression->vhdl->referenced_name->text()
        : expression && expression->vhdl != nullptr
        ? std::string_view { expression->vhdl->text }
        : std::string_view { };
    const auto simple = vhdl_simple_name(name);
    const auto kind = vhdl_file_function_kind(simple);
    if (simple == "file_open" || kind == FilePositionKind::state
        || kind == FilePositionKind::mode) {
        return { 2U, frontend::ValueDomain::Bit2 };
    }
    if (kind == FilePositionKind::position
        || kind == FilePositionKind::size) {
        return { 32U, frontend::ValueDomain::Integer };
    }
    return { 1U, frontend::ValueDomain::Boolean };
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
                || name == "append_mode" || name == "read_write_mode"
        ? std::string_view { "file_open_kind" }
        : name == "file_origin_begin" || name == "file_origin_current"
                || name == "file_origin_end"
        ? std::string_view { "file_origin_kind" }
        : name == "state_open" || name == "state_closed"
        ? std::string_view { "file_open_state" }
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
        for (const auto& member :
            specialized_hir_unit_->vhdl_enumeration_literals_declared_by(
                selected)) {
            const auto& type = *member.type;
            const auto* literal = &type.enumeration_literals[member.position];
            const auto declaration
                = specialized_hir_unit_->find_declaration(
                    type.declaration);
            const auto owner = declaration
                    && declaration->vhdl != nullptr
                ? vhdl_simple_name(declaration->vhdl->name)
                : std::string_view { };
            if (owner != predefined_type) {
                conflicting_owner = true;
                continue;
            }
            if (selected_ordinal
                && *selected_ordinal != literal->ordinal) {
                conflicting_owner = true;
                continue;
            }
            selected_ordinal = literal->ordinal;
        }
        if (conflicting_owner || !selected_ordinal) {
            return std::nullopt;
        }
        return selected_ordinal;
    } else if (source.referenced_name
        && !source.referenced_name->overloads.empty()) {
        return std::nullopt;
    }
    // `c.left` selects an element of a record object, not the SIDE literal;
    // only a package prefix (`std.textio.left`) names a literal (IEEE
    // 1076-2008 8.3).
    if (const auto dot = std::string_view { source.text }.rfind('.');
        dot != std::string_view::npos) {
        auto prefix = std::string { std::string_view { source.text }
                                        .substr(0U, dot) };
        std::ranges::transform(prefix, prefix.begin(), [](const char c) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        });
        const auto package = std::string_view { prefix }.substr(
            prefix.rfind('.') == std::string::npos ? 0U
                                                   : prefix.rfind('.') + 1U);
        if (package != "standard" && package != "textio"
            && package != "env") {
            return std::nullopt;
        }
    }
    // These predefined enumeration literals are represented without an
    // owning declaration when their standard-package profile is implicit.
    // Keep their language-defined ordinals available to all HIR lowering,
    // including severity_level variables used by report/assert statements.
    if (name == "open_ok" || name == "read_mode" || name == "note"
        || name == "right" || name == "file_origin_begin"
        || name == "state_open") {
        return 0U;
    }
    if (name == "status_error" || name == "write_mode"
        || name == "warning" || name == "left"
        || name == "file_origin_current" || name == "state_closed") {
        return 1U;
    }
    if (name == "name_error" || name == "append_mode"
        || name == "error" || name == "file_origin_end") {
        return 2U;
    }
    if (name == "mode_error" || name == "failure"
        || name == "read_write_mode") {
        return 3U;
    }
    // STD.ENV.DAYOFWEEK is compiler-owned as well (IEEE 1076-2019 16.5).
    constexpr auto days = std::to_array<std::string_view>({
        "sunday", "monday", "tuesday", "wednesday", "thursday", "friday",
        "saturday",
    });
    if (const auto day = std::ranges::find(days, name);
        day != days.end()
        && semantic::CompiledDesignResolver { *specialized_hir_unit_ }
               .vhdl_builtin_type_visible("dayofweek", source.scope)) {
        return static_cast<std::uint64_t>(
            std::distance(days.begin(), day));
    }
    return std::nullopt;
}

std::optional<std::uint64_t> Lowerer::hir_vhdl_reflection_class_literal(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::name
        || (expression->vhdl->referenced_name
            && (expression->vhdl->referenced_name->selected
                || !expression->vhdl->referenced_name->overloads.empty()))) {
        return std::nullopt;
    }
    constexpr auto literals = std::to_array<std::string_view>({
        "class_enumeration", "class_integer", "class_floating",
        "class_physical", "class_record", "class_array", "class_access",
        "class_file", "class_protected",
    });
    const auto name = vhdl_simple_name(expression->vhdl->text);
    for (std::size_t index = 0U; index < literals.size(); ++index) {
        if (support::same_vhdl_identifier(name, literals[index])) {
            return index;
        }
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
        ? expression->vhdl->referenced_name->text()
        : expression && expression->vhdl != nullptr
        ? std::string_view { expression->vhdl->text }
        : std::string_view { };
    if (expression && expression->vhdl != nullptr
        && !expression->vhdl->operands.empty()
        && (vhdl_file_function_kind(vhdl_simple_name(name))
            || vhdl_simple_name(name) == "file_open")) {
        const auto simple = vhdl_simple_name(name);
        const auto& operands = expression->vhdl->operands;
        const auto span = hir_source_span(expression->vhdl->source);
        const auto file = hir_referenced_declaration(operands.front());
        const auto binding = file
            ? hir_runtime_binding(*file, hir_process_scope_, true)
            : std::nullopt;
        if (!binding || binding->kind != HirRuntimeBindingKind::local
            || !binding->local) {
            report("FSIM-ELAB-VHFILE-005",
                "VHDL file operation requires a whole file object", span);
            return std::nullopt;
        }
        if (simple == "file_open") {
            // The impure function form returns the FILE_OPEN_STATUS.
            const auto kind_expression = operands.size() > 2U
                ? specialized_hir_unit_->find_expression(operands[2])
                : std::nullopt;
            const auto mode_text = vhdl_file_open_mode(
                kind_expression && kind_expression->vhdl != nullptr
                    ? vhdl_simple_name(kind_expression->vhdl->text)
                    : std::string_view { "read_mode" });
            if (operands.size() < 2U || operands.size() > 3U
                || mode_text.empty()
                || !hir_expression_is_string(
                    operands[1], hir_process_scope_)) {
                report("FSIM-ELAB-VHFILE-007",
                    "file_open requires a file object, string logical "
                    "name, and optional file_open_kind",
                    span);
                return std::nullopt;
            }
            const auto path = lower_hir_string_expression(operands[1]);
            if (!path) {
                return std::nullopt;
            }
            const auto mode = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                mode, std::string { mode_text } });
            const auto status = allocate_register(
                2U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(FileOpen {
                *binding->local, *path, mode, status, true });
            return status;
        }
        const auto kind = *vhdl_file_function_kind(simple);
        if (operands.size()
            > (kind == FilePositionKind::position ? 2U : 1U)) {
            report("FSIM-ELAB-VHFILE-009",
                std::string { simple }
                    + " takes one file object"
                    + (kind == FilePositionKind::position
                            ? " and an optional FILE_ORIGIN_KIND"
                            : ""),
                span);
            return std::nullopt;
        }
        const auto result = lower_hir_vhdl_file_position(kind,
            *binding->local, operands.front(), std::nullopt,
            operands.size() > 1U ? std::optional { operands[1] }
                                 : std::nullopt);
        if (!result) {
            return std::nullopt;
        }
        // FILE_OPEN_STATE and FILE_OPEN_KIND are enumerations and
        // FILE_CANSEEK a BOOLEAN; FILE_POSITION and FILE_SIZE INTEGER.
        const auto narrow = kind == FilePositionKind::canseek ? 1U
            : kind == FilePositionKind::state
                || kind == FilePositionKind::mode
            ? 2U
            : 0U;
        if (narrow == 0U) {
            return result;
        }
        const auto destination = allocate_register(narrow,
            narrow == 1U ? frontend::ValueDomain::Boolean
                         : frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            Extract { destination, *result, 0U, narrow });
        return destination;
    }
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

std::optional<RegisterId> Lowerer::lower_hir_vhdl_file_position(
    const FilePositionKind kind,
    const RegisterId file,
    const semantic::ExpressionId file_expression,
    const std::optional<semantic::ExpressionId> amount,
    const std::optional<semantic::ExpressionId> origin)
{
    // The unit is the file's element, or the element of an unconstrained
    // array element type (5.5.2). TEXT and other files of unconstrained
    // arrays count characters; other typed files hold one element per line.
    bool lines { };
    const auto declaration_id = hir_target_declaration(file_expression);
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    const auto subtype = declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype
        ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
        : std::nullopt;
    const auto type = subtype && subtype->type_mark.target.valid()
        ? specialized_hir_unit_->find_type(subtype->type_mark.target)
        : std::nullopt;
    if (type && type->vhdl != nullptr
        && type->vhdl->form == semantic::vhdl::TypeForm::file
        && type->vhdl->element_subtype
        && vhdl_simple_name(type->vhdl->name) != "text") {
        const auto element
            = hir_effective_vhdl_subtype(*type->vhdl->element_subtype);
        lines = element
            && element->domain != semantic::vhdl::ValueDomain::string
            && !element->unconstrained;
    }
    const auto integer = [&](const std::optional<semantic::ExpressionId>
                                 expression) -> std::optional<RegisterId> {
        if (!expression) {
            return load_constant(unsigned_value(0U, 32U),
                frontend::ValueDomain::Integer);
        }
        const auto value = lower_hir_expression(*expression, 32U);
        if (!value) {
            return std::nullopt;
        }
        return resize_register(*value, 32U, false);
    };
    const auto offset = integer(amount);
    const auto start = integer(origin);
    if (!offset || !start) {
        return std::nullopt;
    }
    const auto destination = allocate_register(
        32U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(FilePosition {
        destination, file, *offset, *start, kind, lines });
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
        statement->vhdl->procedure.text());
    // READ, WRITE, TEE and FLUSH are file operations only when their first
    // actual is a file object or line; other procedures share these names.
    if (name != "read" && name != "write" && name != "swrite"
        && name != "tee" && name != "flush") {
        return true;
    }
    if (statement->vhdl->procedure_arguments.empty()) {
        return name != "tee" && name != "flush";
    }
    const auto first = statement->vhdl->procedure_arguments.front().actual;
    const auto selected = hir_referenced_declaration(first);
    if (!selected && name != "read" && name != "swrite") {
        // std.textio.OUTPUT has no declaration in the design.
        const auto expression = specialized_hir_unit_->find_expression(first);
        if (expression && expression->vhdl != nullptr
            && expression->vhdl->referenced_name) {
            const auto& referenced = *expression->vhdl->referenced_name;
            return vhdl_simple_name(referenced.canonical.empty()
                           ? referenced.spelling
                           : referenced.canonical)
                == "output";
        }
    }
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
    if (format && format->systemverilog != nullptr
        && format->systemverilog->kind
            != semantic::sv::ExpressionKind::string_literal) {
        return hir_runtime_format_status(call.operands, process_scope);
    }
    if (!format || format->systemverilog == nullptr
        || !format->systemverilog->decoded_string) {
        return HirStringFormatStatus::invalid;
    }
    const auto parsed = frontend::parse_output_format(
        *format->systemverilog->decoded_string);
    const auto consumes_value = [](const frontend::OutputFormat output) {
        return output != frontend::OutputFormat::Hierarchy
            && output != frontend::OutputFormat::Library
            && output != frontend::OutputFormat::Time;
    };
    const auto required = static_cast<std::size_t>(
        std::ranges::count_if(parsed.conversions,
            [&](const frontend::ParsedOutputConversion& conversion) {
                return consumes_value(conversion.format);
            }));
    if (!parsed.valid
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
        if (conversion.format == frontend::OutputFormat::Pattern) {
            continue;
        }
        // `%u` and `%z` write an integral value's unformatted bits (IEEE
        // 1800-2017 21.2.1.7).
        if ((conversion.format == frontend::OutputFormat::Unformatted2
                || conversion.format == frontend::OutputFormat::Unformatted4)
            && hir_systemverilog_scan_value_kind(value)
                != HirSystemVerilogScanValueKind::packed_integral) {
            const auto literal = specialized_hir_unit_->find_expression(value);
            if (!literal || literal->systemverilog == nullptr
                || literal->systemverilog->kind
                    != semantic::sv::ExpressionKind::string_literal) {
                return HirStringFormatStatus::invalid;
            }
        }
        // `%s` of an integral value shows its 8-bit characters.
        if (!hir_expression_width(value, process_scope)
            && (conversion.format != frontend::OutputFormat::String
                || !hir_expression_is_string(value, process_scope))) {
            return HirStringFormatStatus::invalid;
        }
    }
    return HirStringFormatStatus::valid;
}

Lowerer::HirStringFormatStatus Lowerer::hir_runtime_format_status(
    const std::span<const semantic::ExpressionId> arguments,
    const semantic::ScopeId process_scope) const
{
    if (arguments.empty()
        || !hir_text_operand(arguments.front(), process_scope)) {
        return HirStringFormatStatus::invalid;
    }
    return std::all_of(arguments.begin() + 1, arguments.end(),
               [&](const semantic::ExpressionId value) {
                   if (hir_expression_is_string(value, process_scope)) {
                       return true;
                   }
                   const auto width
                       = hir_expression_width(value, process_scope);
                   return width && *width != 0U
                       && *width <= std::numeric_limits<std::uint32_t>::max();
               })
        ? HirStringFormatStatus::valid
        : HirStringFormatStatus::invalid;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_runtime_formatted_string(
    const std::span<const semantic::ExpressionId> arguments)
{
    const auto format = lower_hir_text_value(arguments.front());
    if (!format) {
        return std::nullopt;
    }
    RuntimeStringFormat operation;
    operation.format = *format;
    for (const auto value : arguments.subspan(1U)) {
        RuntimeFormatArgument argument;
        if (hir_expression_is_string(value, hir_process_scope_)) {
            const auto lowered = lower_hir_string_expression(value);
            if (!lowered) {
                return std::nullopt;
            }
            argument.value = *lowered;
            argument.string = true;
            operation.arguments.push_back(argument);
            continue;
        }
        const auto width = hir_expression_width(value, hir_process_scope_);
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        auto scalar = hir_systemverilog_scalar_kind(value);
        const auto lowered = lower_hir_expression(value, *width, scalar);
        if (!lowered) {
            return std::nullopt;
        }
        const auto record = specialized_hir_unit_->find_expression(value);
        argument.value = *lowered;
        argument.width = static_cast<std::uint32_t>(*width);
        argument.is_signed = hir_expression_signed(value);
        argument.scalar_kind = scalar;
        argument.current_time = record && record->systemverilog != nullptr
            && record->systemverilog->kind
                == semantic::sv::ExpressionKind::call
            && record->systemverilog->text == "$time";
        operation.arguments.push_back(argument);
    }
    operation.destination = allocate_string_register();
    operation.time_unit_femtoseconds = hir_time_unit_femtoseconds();
    operation.hierarchy = hir_scope_hierarchy();
    operation.library_cell = hir_library_cell();
    const auto destination = operation.destination;
    process_.operations.emplace_back(std::move(operation));
    return destination;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_formatted_string(
    const std::span<const semantic::ExpressionId> arguments,
    const std::optional<frontend::OutputFormat> default_format)
{
    if (specialized_hir_unit_ == nullptr
        || (arguments.empty() && !default_format)) {
        return std::nullopt;
    }
    frontend::ParsedOutputFormat parsed;
    if (default_format) {
        parsed.conversions.resize(arguments.size());
        for (std::size_t index = 0; index < parsed.conversions.size(); ++index) {
            auto& conversion = parsed.conversions[index];
            conversion.format = *default_format;
            if (*default_format == frontend::OutputFormat::Decimal
                && hir_expression_is_string(
                    arguments[index], hir_process_scope_)) {
                conversion.format = frontend::OutputFormat::String;
            }
        }
    } else {
        const auto format = specialized_hir_unit_->find_expression(
            arguments.front());
        if (!format || format->systemverilog == nullptr) {
            return std::nullopt;
        }
        if (format->systemverilog->kind
            != semantic::sv::ExpressionKind::string_literal) {
            return lower_hir_runtime_formatted_string(arguments);
        }
        if (!format->systemverilog->decoded_string) {
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
    const auto append_value_impl = [&](const frontend::ParsedOutputConversion& conversion,
                                  const std::optional<semantic::ExpressionId> value) {
        append_literal(conversion.prefix);
        if (conversion.format == frontend::OutputFormat::Hierarchy) {
            append_literal(hir_scope_hierarchy());
            return true;
        }
        if (conversion.format == frontend::OutputFormat::Library) {
            append_literal(hir_library_cell());
            return true;
        }
        StringMethod operation;
        operation.source = destination;
        operation.minimum_width = conversion.minimum_width;
        operation.precision = conversion.precision;
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
            // An integral value other than $time is in the module's time
            // unit (IEEE 1800-2017 21.2.1.3).
            const auto record = value
                ? specialized_hir_unit_->find_expression(*value)
                : std::nullopt;
            const bool current_time = !record || record->systemverilog == nullptr
                || (record->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                    && record->systemverilog->text == "$time");
            const auto width = value
                ? hir_expression_width(*value, hir_process_scope_)
                : std::nullopt;
            const auto value_kind = value
                ? hir_systemverilog_scalar_kind(*value)
                : frontend::SystemVerilogScalarKind::None;
            if (!current_time && width && *width != 0U && *width <= 64U
                && (value_kind == frontend::SystemVerilogScalarKind::None
                    || value_kind == frontend::SystemVerilogScalarKind::Time)) {
                const auto lowered = lower_hir_expression(*value, *width);
                if (!lowered) {
                    return false;
                }
                operation.first = *lowered;
                operation.time_unit_femtoseconds
                    = hir_time_unit_femtoseconds();
                operation.time_signed = hir_expression_signed(*value);
            }
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
        // %e/%f/%g convert an integral value to real (21.2.1.3).
        const auto real_conversion = conversion.format
                == frontend::OutputFormat::RealScientific
            || conversion.format == frontend::OutputFormat::RealFixed
            || conversion.format == frontend::OutputFormat::RealGeneral;
        auto scalar = hir_systemverilog_scalar_kind(*value);
        auto value_width = *width;
        auto lowered = lower_hir_expression(*value, value_width, scalar);
        if (!lowered) {
            return false;
        }
        if (real_conversion
            && scalar == frontend::SystemVerilogScalarKind::None) {
            lowered = convert_hir_integral_to_real(*lowered,
                hir_expression_signed(*value),
                frontend::SystemVerilogScalarKind::Real);
            scalar = frontend::SystemVerilogScalarKind::Real;
            value_width = 64U;
        }
        // %d of a real shows it rounded to an integer (21.2.1.3, 6.12.2).
        const bool real_decimal = conversion.format
                == frontend::OutputFormat::Decimal
            && frontend::is_real_scalar_kind(scalar);
        if (real_decimal) {
            lowered = convert_hir_real_to_integral(*lowered, scalar);
            scalar = frontend::SystemVerilogScalarKind::None;
            value_width = register_width(*lowered);
        }
        const auto width_register = load_constant(
            unsigned_value(value_width, 32U), frontend::ValueDomain::Bit2);
        operation.operation = StringMethodOperator::format_packed;
        operation.first = *lowered;
        operation.second = width_register;
        operation.format = runtime_output_format(conversion.format);
        operation.scalar_kind = scalar;
        operation.signed_decimal
            = operation.format == OutputFormat::decimal
            && (real_decimal || hir_expression_signed(*value));
        operation.minimum_width = real_decimal
            ? conversion.minimum_width
            : hir_systemverilog_decimal_width(*value,
                  operation.format,
                  conversion.suppress_leading_zero || conversion.left_justify,
                  conversion.minimum_width);
        process_.operations.emplace_back(operation);
        return true;
    };
    // `%p` (IEEE 1800-2017 21.2.1.7): an unpacked value as an assignment
    // pattern, a string in quotes, an enumeration by name, and other
    // singular values as `%0d` or `%g`.
    const auto append_value = [&](const frontend::ParsedOutputConversion& conversion,
                                  const std::optional<semantic::ExpressionId> value) {
        const auto append_text = [&](const StringRegisterId text) {
            process_.operations.emplace_back(ConcatenateStrings {
                destination, { destination, text } });
        };
        if (conversion.format == frontend::OutputFormat::Strength && value) {
            const auto text = lower_hir_strength_text(*value);
            if (!text) {
                return false;
            }
            append_literal(conversion.prefix);
            append_text(*text);
            return true;
        }
        if (conversion.format != frontend::OutputFormat::Pattern || !value) {
            return append_value_impl(conversion, value);
        }
        const auto pattern = lower_hir_pattern_text(*value);
        if (pattern.failed) {
            return false;
        }
        if (pattern.text) {
            append_literal(conversion.prefix + (pattern.quoted ? "\"" : ""));
            append_text(*pattern.text);
            if (pattern.quoted) {
                append_literal("\"");
            }
            return true;
        }
        auto singular = conversion;
        const auto kind = hir_systemverilog_scalar_kind(*value);
        singular.format = frontend::is_real_scalar_kind(kind)
            ? frontend::OutputFormat::RealGeneral
            : frontend::OutputFormat::Decimal;
        singular.suppress_leading_zero = conversion.minimum_width == 0U
            || conversion.suppress_leading_zero;
        return append_value_impl(singular, value);
    };

    std::size_t value_index = default_format ? 0U : 1U;
    // %t takes an argument when the arguments outnumber the other
    // conversions; otherwise it shows the current time.
    const auto other_values = static_cast<std::size_t>(std::ranges::count_if(
        parsed.conversions,
        [](const frontend::ParsedOutputConversion& conversion) {
            return output_conversion_consumes_value(conversion.format);
        }));
    auto time_values = arguments.size() > value_index + other_values
        ? arguments.size() - value_index - other_values
        : std::size_t { };
    const auto append_format = [&](const frontend::ParsedOutputFormat& format) {
        for (const auto& conversion : format.conversions) {
            std::optional<semantic::ExpressionId> value;
            if (conversion.format == frontend::OutputFormat::Time) {
                if (time_values != 0U && value_index < arguments.size()) {
                    value = arguments[value_index++];
                    --time_values;
                }
            } else if (output_conversion_consumes_value(conversion.format)) {
                if (value_index >= arguments.size()) {
                    return false;
                }
                value = arguments[value_index++];
            }
            if (!append_value(conversion, value)) {
                return false;
            }
        }
        append_literal(format.trailing_text);
        return true;
    };
    if (!append_format(parsed)) {
        return std::nullopt;
    }
    // Arguments past the format show as $display shows them (IEEE
    // 1800-2017 21.2.1): a string literal is more format text, a string
    // value its characters, and another value its decimal form.
    while (value_index < arguments.size()) {
        const auto argument = arguments[value_index++];
        const auto record = specialized_hir_unit_->find_expression(argument);
        if (!default_format && record && record->systemverilog != nullptr
            && record->systemverilog->kind
                == semantic::sv::ExpressionKind::string_literal
            && record->systemverilog->decoded_string) {
            const auto format = frontend::parse_output_format(
                *record->systemverilog->decoded_string);
            if (!format.valid || !append_format(format)) {
                return std::nullopt;
            }
            continue;
        }
        frontend::ParsedOutputConversion conversion;
        conversion.format = hir_expression_is_string(
                                argument, hir_process_scope_)
            ? frontend::OutputFormat::String
            : frontend::OutputFormat::Decimal;
        if (!append_value(conversion, argument)) {
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
    const bool string_write = call.task.spelling == "$swrite";
    const bool string_format = call.task.spelling == "$sformat";
    const auto format_task = string_write || string_format
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
    const auto minimum_arguments = string_format ? 2U : 1U;
    if (named || call.task_arguments.size() < minimum_arguments
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
    // A hierarchical reference to a string variable (IEEE 1800-2017 23.6).
    auto binding = hir_hierarchical_string_binding(target);
    const bool hierarchical_target = binding.has_value();
    if (!binding && target_declaration) {
        binding = hir_string_binding(*target_declaration, process_scope, false);
    }
    // The output variable may also be integral (IEEE 1800-2017 21.3.3).
    const bool packed_target_type
        = hir_systemverilog_scan_value_kind(target)
            == HirSystemVerilogScanValueKind::packed_integral;
    const auto packed_target_width = !binding && target_declaration
            && target_expression
            && target_expression->systemverilog != nullptr
            && packed_target_type
        ? hir_expression_width(target, process_scope)
        : std::nullopt;
    const bool packed_target = packed_target_width
        && *packed_target_width != 0U
        && *packed_target_width <= (std::size_t { 1 } << 16U);
    if (!packed_target
        && (!target_expression || target_expression->systemverilog == nullptr
            || (target_expression->systemverilog->kind
                    != semantic::sv::ExpressionKind::name
                && !hierarchical_target)
            || !binding
            || (binding->kind == HirStringBindingKind::object
                && (!binding->object
                    || read_only_string_objects_.contains(
                        *binding->object))))) {
        return HirStringFormatStatus::invalid;
    }
    const bool default_radix_format = call.task.spelling == "$swriteb"
        || call.task.spelling == "$swriteh"
        || call.task.spelling == "$swriteo";
    const auto first_value = call.task_arguments.size() > 1U
        ? specialized_hir_unit_->find_expression(
              *call.task_arguments[1].actual)
        : std::nullopt;
    const bool swrite_format_literal = string_write && first_value
        && first_value->systemverilog != nullptr
        && first_value->systemverilog->kind
            == semantic::sv::ExpressionKind::string_literal
        && first_value->systemverilog->decoded_string.has_value();
    const bool default_decimal_format = string_write
        && !swrite_format_literal;
    // A later string literal can introduce another format segment in the
    // SystemVerilog output-task argument model. This lowering handles one
    // leading format string, so reject mixed format segments instead of
    // treating their conversion text as ordinary string data.
    const bool unsupported_mixed_format_literal = default_decimal_format
        && call.task_arguments.size() > 2U
        && std::ranges::any_of(
            call.task_arguments.begin() + 2,
            call.task_arguments.end(),
            [&](const semantic::sv::TaskAssociation& argument) {
                const auto expression = specialized_hir_unit_->find_expression(
                    *argument.actual);
                return expression && expression->systemverilog != nullptr
                    && expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::string_literal
                    && expression->systemverilog->decoded_string
                    && expression->systemverilog->decoded_string->find('%')
                        != std::string::npos;
            });
    if (unsupported_mixed_format_literal) {
        return HirStringFormatStatus::invalid;
    }
    if (default_radix_format || default_decimal_format) {
        return std::ranges::all_of(
                   call.task_arguments.begin() + 1,
                   call.task_arguments.end(),
                   [&](const semantic::sv::TaskAssociation& argument) {
                       const auto value = *argument.actual;
                       if (default_decimal_format
                           && hir_expression_is_string(value, process_scope)) {
                           return true;
                       }
                       const auto width = hir_expression_width(
                           value, process_scope);
                       return width && *width != 0U
                           && *width
                           <= std::numeric_limits<std::uint32_t>::max();
                   })
            ? HirStringFormatStatus::valid
            : HirStringFormatStatus::invalid;
    }
    const auto format_id = *call.task_arguments[1].actual;
    const auto format = specialized_hir_unit_->find_expression(format_id);
    if (format && format->systemverilog != nullptr
        && format->systemverilog->kind
            != semantic::sv::ExpressionKind::string_literal) {
        std::vector<semantic::ExpressionId> arguments;
        for (std::size_t index = 1U; index < call.task_arguments.size();
            ++index) {
            arguments.push_back(*call.task_arguments[index].actual);
        }
        return hir_runtime_format_status(arguments, process_scope);
    }
    if (!format || format->systemverilog == nullptr
        || !format->systemverilog->decoded_string) {
        return HirStringFormatStatus::invalid;
    }
    const auto parsed = frontend::parse_output_format(
        *format->systemverilog->decoded_string);
    const auto consumes_value = [](const frontend::OutputFormat output) {
        return output != frontend::OutputFormat::Hierarchy
            && output != frontend::OutputFormat::Library
            && output != frontend::OutputFormat::Time;
    };
    const auto required = static_cast<std::size_t>(
        std::ranges::count_if(parsed.conversions,
            [&](const frontend::ParsedOutputConversion& conversion) {
                return consumes_value(conversion.format);
            }));
    if (!parsed.valid || parsed.conversions.size() > 64U
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
        if (conversion.format == frontend::OutputFormat::Pattern) {
            continue;
        }
        const auto unformatted = conversion.format
                == frontend::OutputFormat::Unformatted2
            || conversion.format == frontend::OutputFormat::Unformatted4;
        const bool string_target
            = hir_expression_is_string(target, process_scope);
        const auto value_expression
            = specialized_hir_unit_->find_expression(value);
        const bool packed_string_literal = value_expression
            && value_expression->systemverilog != nullptr
            && value_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::string_literal
            && value_expression->systemverilog->decoded_string.has_value();
        const bool integral_value
            = hir_systemverilog_scan_value_kind(value)
                == HirSystemVerilogScanValueKind::packed_integral
            || packed_string_literal;
        const bool unsupported_unformatted = unformatted
            && ((!packed_target && !string_target) || !integral_value);
        const auto width = hir_expression_width(value, process_scope);
        // `%s` of an integral value shows its 8-bit characters.
        if (unsupported_unformatted
            || (conversion.format == frontend::OutputFormat::String
                && !hir_expression_is_string(value, process_scope)
                && (!width || *width == 0U))
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
    } else if (call.task.spelling == "$swrite") {
        const auto first = arguments.empty()
            ? std::optional<semantic::CompiledExpressionView> { }
            : specialized_hir_unit_->find_expression(arguments.front());
        const bool format_literal = first
            && first->systemverilog != nullptr
            && first->systemverilog->kind
                == semantic::sv::ExpressionKind::string_literal
            && first->systemverilog->decoded_string.has_value();
        if (!format_literal) {
            default_format = frontend::OutputFormat::Decimal;
        }
    }
    emit_debug_point(
        DebugPointKind::call, hir_source_span(call.source));
    const auto result = lower_hir_formatted_string(
        arguments, default_format);
    if (!result) {
        return false;
    }
    const auto target = *call.task_arguments.front().actual;
    const auto target_declaration = hir_target_declaration(target);
    const auto string_target = hir_hierarchical_string_binding(target)
        || (target_declaration
            && hir_string_binding(
                *target_declaration, hir_process_scope_, false));
    if (string_target || hir_expression_is_string(target, hir_process_scope_)) {
        return lower_hir_string_copy_out(target, *result);
    }
    // An integral output variable receives the string's characters, the
    // last character least significant, zero-filled on the left (IEEE
    // 1800-2017 21.3.3, 5.9).
    const auto width = hir_expression_width(target, hir_process_scope_);
    if (!width || *width == 0U) {
        return false;
    }
    const auto length = allocate_register(32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(StringLength { length, *result });
    auto packed = load_constant(
        unsigned_value(0U, *width), frontend::ValueDomain::Bit2);
    const auto bytes = (*width + 7U) / 8U;
    for (std::size_t byte = 0; byte < bytes; ++byte) {
        const auto distance = load_constant(
            unsigned_value(byte + 1U, 32U), frontend::ValueDomain::Bit2);
        const auto index = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_unsigned, index, length, distance });
        // getc of an index outside the string is zero (6.16.3).
        const auto character = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        StringMethod getc;
        getc.operation = StringMethodOperator::getc;
        getc.destination = character;
        getc.source = *result;
        getc.first = index;
        process_.operations.emplace_back(getc);
        const auto bits = std::min<std::size_t>(8U, *width - byte * 8U);
        const auto part = allocate_register(bits, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Extract {
            part, character, 0U, static_cast<std::uint32_t>(bits) });
        const auto next = allocate_register(*width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Insert {
            next, packed, part, static_cast<std::uint32_t>(byte * 8U) });
        packed = next;
    }
    return lower_hir_packed_copy_out(target, packed);
}

void Lowerer::diagnose_hir_systemverilog_file_process(
    const semantic::ProcessId process_id, const std::string_view hierarchy)
{
    if (specialized_hir_unit_ == nullptr) {
        return;
    }
    // Hierarchical references resolve from this instance (IEEE 1800-2017
    // 23.6).
    if (!hierarchy.empty()) {
        hierarchy_ = std::string { hierarchy };
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
                ? frontend::systemverilog_scalar_kind(
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
            if (frontend::is_real_scalar_kind(kind)
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
                = frontend::is_real_scalar_kind(kind);
            if (kind == frontend::SystemVerilogScalarKind::None) {
                return format != frontend::InputScanFormat::Real
                    && format != frontend::InputScanFormat::Time;
            }
            if (real_target) {
                return format == frontend::InputScanFormat::Real
                    || format == frontend::InputScanFormat::Time;
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
                        || !hir_filename_operand(
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
                    // A string literal ("x") is an integral value (IEEE
                    // 1800-2017 5.9).
                    const auto literal_character = call.operands.size() == 2U
                        && [&] {
                               const auto found = specialized_hir_unit_->find_expression(
                                   call.operands[0]);
                               return found && found->systemverilog != nullptr
                                   && found->systemverilog->kind
                                       == semantic::sv::ExpressionKind::string_literal;
                           }();
                    if (call.operands.size() != 2U
                        || (!integral_expression(call.operands[0]) && !literal_character)
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
                        const auto source_expression
                            = specialized_hir_unit_->find_expression(
                                call.operands[0]);
                        const auto source_kind
                            = hir_systemverilog_scan_value_kind(
                                call.operands[0]);
                        const bool string_source_value
                            = source_kind
                                    == HirSystemVerilogScanValueKind::string
                            || (source_expression
                                && source_expression->systemverilog != nullptr
                                && source_expression->systemverilog->kind
                                    == semantic::sv::ExpressionKind::string_literal);
                        const bool integral_source_value
                            = source_kind
                                == HirSystemVerilogScanValueKind::packed_integral;
                        const bool unknown_integral_format = string_source
                            && (string_source_value || integral_source_value)
                            && hir_sscanf_unknown_integral_format(
                                *specialized_hir_unit_, call)
                            && hir_sscanf_unknown_format_simple_source(
                                *specialized_hir_unit_, call);
                        const auto format
                            = specialized_hir_unit_->find_expression(
                                call.operands[1]);
                        if ((!format || format->systemverilog == nullptr
                                || format->systemverilog->kind
                                    != semantic::sv::ExpressionKind::string_literal
                                || !format->systemverilog->decoded_string)
                            && !unknown_integral_format) {
                            fail("FSIM-ELAB-SVFILE-011",
                                call.text
                                    + " format must be a string literal");
                        } else if (unknown_integral_format) {
                            const auto writable_unknown_format_target =
                                [&](const semantic::ExpressionId target_id) {
                                    const auto target_expression
                                        = specialized_hir_unit_->find_expression(
                                            target_id);
                                    const auto declaration_id
                                        = hir_target_declaration(target_id);
                                    const auto declaration = declaration_id
                                        ? specialized_hir_unit_->find_declaration(
                                            *declaration_id)
                                        : std::nullopt;
                                    if (!target_expression
                                        || target_expression->systemverilog == nullptr
                                        || target_expression->systemverilog->kind
                                            != semantic::sv::ExpressionKind::name
                                        || !declaration
                                        || declaration->systemverilog == nullptr) {
                                        return false;
                                    }
                                    const auto form
                                        = declaration->systemverilog->form;
                                    const auto direction
                                        = declaration->systemverilog->direction;
                                    const auto& target_type
                                        = declaration->systemverilog->type;
                                    const bool net_typed_port = form
                                            == semantic::sv::DeclarationForm::
                                                port
                                        && target_type
                                        && (!target_type->systemverilog_net_type
                                                .empty()
                                            || semantic::sv::
                                                is_builtin_net_kind(target_type
                                                        ->target.spelling)
                                            || target_type->target.spelling
                                                == "interconnect");
                                    const bool writable_port_direction
                                        = direction
                                                == semantic::sv::Direction::output
                                        || direction
                                                == semantic::sv::Direction::ref;
                                    const bool writable_form
                                        = form
                                            == semantic::sv::DeclarationForm::variable
                                        || (form
                                                == semantic::sv::DeclarationForm::port
                                            && writable_port_direction
                                            && !net_typed_port);
                                    using ScanValueKind
                                        = HirSystemVerilogScanValueKind;
                                    const auto target_kind
                                        = hir_systemverilog_scan_value_kind(
                                            target_id);
                                    const bool supported_target_type
                                        = target_kind
                                            == ScanValueKind::packed_integral
                                        || target_kind
                                            == ScanValueKind::string
                                        || target_kind
                                            == ScanValueKind::real;
                                    return writable_form
                                        && supported_target_type
                                        && writable_text_target(target_id);
                                };
                            if (!std::ranges::all_of(
                                    call.operands.begin() + 2,
                                    call.operands.end(),
                                    writable_unknown_format_target)) {
                                fail("FSIM-ELAB-SVFILE-012",
                                    "scan target is incompatible, read-only, "
                                    "or unsupported");
                            }
                        } else {
                            const auto parsed = frontend::parse_input_format(
                                *format->systemverilog->decoded_string);
                            const auto required = static_cast<std::size_t>(
                                std::ranges::count_if(
                                    parsed.conversions,
                                    [](const auto& conversion) {
                                        return !conversion.suppress;
                                    }));
                            if ((string_source || parsed.trailing_text.empty())
                                && parsed.valid
                                && parsed.conversions.empty()
                                && call.operands.size() == 2U) {
                                // No conversions: the call is 0.
                            } else if (!parsed.valid || parsed.conversions.empty()
                                || parsed.conversions.size() > 64U
                                || required != call.operands.size() - 2U
                                || parsed.trailing_text.size()
                                    > maximum_string_bytes
                                // A field width only bounds the scan.
                                || std::ranges::any_of(
                                    parsed.conversions,
                                    [](const auto& conversion) {
                                        return conversion.prefix.size()
                                            > maximum_string_bytes;
                                    })) {
                                fail("FSIM-ELAB-SVFILE-011",
                                    call.text + " has an invalid or "
                                    "target-mismatched format");
                            } else if ((string_source
                                    && !hir_expression_is_string(
                                        call.operands[0], call.scope)
                                    && !integral_expression(call.operands[0]))
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
                if (format && format->systemverilog != nullptr
                    && format->systemverilog->kind
                        != semantic::sv::ExpressionKind::string_literal) {
                    if (hir_runtime_format_status(call.operands, call.scope)
                        == HirStringFormatStatus::invalid) {
                        fail("$sformatf run-time format and values must be "
                            "strings or integral values");
                    }
                } else if (!format || format->systemverilog == nullptr
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
                                    != frontend::OutputFormat::Library
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
                        const auto unresolved_hierarchical = [&]
                            (const semantic::ExpressionId value) {
                                const auto expression
                                    = specialized_hir_unit_->find_expression(
                                        value);
                                return expression
                                    && expression->systemverilog != nullptr
                                    && expression->systemverilog->text.find('.')
                                        != std::string::npos
                                    && !hir_referenced_declaration(value);
                            };
                        const auto compatible = [&]
                            (const semantic::ExpressionId value,
                             const frontend::OutputFormat output) {
                                // `%p` formats a value of any type.
                                if (output
                                    == frontend::OutputFormat::Pattern) {
                                    return true;
                                }
                                if (output
                                        == frontend::OutputFormat::String
                                    && hir_expression_is_string(
                                        value, call.scope)) {
                                    return true;
                                }
                                const auto kind
                                    = expression_scalar_kind(value);
                                const auto real_scalar
                                    = frontend::is_real_scalar_kind(kind);
                                const auto real_format
                                    = output
                                        == frontend::OutputFormat::
                                            RealScientific
                                    || output
                                        == frontend::OutputFormat::RealFixed
                                    || output
                                        == frontend::OutputFormat::
                                            RealGeneral;
                                // A real format converts an integral
                                // value (21.2.1.3).
                                if ((real_scalar && !real_format)
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
                                // A hierarchical reference to an instance
                                // elaborated after this process has no
                                // type yet; its lowering retries once the
                                // instance exists (IEEE 1800-2023 23.6).
                                if (!width && unresolved_hierarchical(value)) {
                                    return true;
                                }
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
                    || !hir_text_operand(
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
                        // A run-time user string is checked as it executes;
                        // the target must accept some conversion.
                        if (!scan_target_supported(call.operands[1],
                                frontend::InputScanFormat::Decimal)
                            && !scan_target_supported(call.operands[1],
                                frontend::InputScanFormat::String)
                            && !scan_target_supported(call.operands[1],
                                frontend::InputScanFormat::Real)) {
                            fail("FSIM-ELAB-SVCLI-003",
                                "$value$plusargs target is read-only, "
                                "incompatible, or unsupported");
                        }
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
                            } else if (conversion
                                == frontend::InputScanFormat::Hierarchy) {
                                fail("FSIM-ELAB-SVCLI-002",
                                    "$value$plusargs does not accept %m");
                            } else if (conversion
                                == frontend::InputScanFormat::Time) {
                                fail("FSIM-ELAB-SVCLI-002",
                                    "$value$plusargs does not support %t");
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
                && source.task_arguments.empty()
                && !source.output_format
                && !hir_expression_width(*source.value, source.scope)
                && !specialized_hir_unit_->evaluate_string_expression(
                    *source.value)
                && !(source.kind == semantic::sv::StatementKind::report
                    && hir_expression_is_string(*source.value, source.scope))) {
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
                auto binding = target
                    ? hir_hierarchical_string_binding(
                          *source.task_arguments.front().actual)
                    : std::nullopt;
                const bool hierarchical_target = binding.has_value();
                if (!binding && declaration) {
                    binding = hir_string_binding(
                        *declaration, source.scope, false);
                }
                const auto direct_string_target = target
                    && target->systemverilog != nullptr
                    && (target->systemverilog->kind
                            == semantic::sv::ExpressionKind::name
                        || hierarchical_target)
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
                // A memory is an unpacked array; a dynamic array, queue or
                // integral-indexed associative array holds packed elements
                // (IEEE 1800-2017 21.4.1).
                const auto memory_type = [](const ContainerType& type) {
                    return type.fixed
                        || (type.element_kind == ContainerElementKind::Packed
                            && (!type.associative
                                || (!type.string_indices
                                    && type.index_width <= 64U)));
                };
                // A subprogram's local or formal unpacked array is bound
                // when its body is lowered (IEEE 1800-2017 21.4).
                const auto declared_fixed_local = [&] {
                    const auto id = source.target
                        ? hir_referenced_declaration(*source.target)
                        : std::nullopt;
                    const auto record = id
                        ? specialized_hir_unit_->find_declaration(*id)
                        : std::nullopt;
                    if (!record || record->systemverilog == nullptr
                        || !record->systemverilog->type) {
                        return false;
                    }
                    const auto type = hir_systemverilog_container_type(
                        *record->systemverilog->type);
                    return type && memory_type(*type);
                };
                const auto valid_object = source.value && source.target
                    && hir_filename_operand(*source.value, source.scope)
                    && ((object != container_objects_.end()
                            && (source.memory_write
                                || !read_only_container_objects_.contains(
                                    *name))
                            && static_cast<std::size_t>(object->second)
                                < design_.container_object_info_.size()
                            && memory_type(design_.container_object_info_
                                    [object->second].type))
                        || (object == container_objects_.end()
                            && declared_fixed_local()));
                if (!valid_object) {
                    statement_fail("FSIM-ELAB-SVMEMORY-003",
                        "memory transfer requires a writable unpacked-array "
                        "object");
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
        return frontend::systemverilog_scalar_kind(
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
            || !hir_text_operand(
                call.operands.front(), hir_process_scope_)) {
            return fail("FSIM-ELAB-SVCLI-001",
                call.text
                    + (value_query
                            ? " requires a format string and writable target"
                            : " requires one string expression"));
        }
        if (!value_query) {
            const auto query = lower_hir_text_value(call.operands.front());
            if (!query) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(PlusArgSelect {
                destination, *query, std::nullopt, false });
            return finish(destination);
        }

        // A user string that is not a literal is parsed as it executes
        // (IEEE 1800-2017 21.6).
        const auto format = specialized_hir_unit_->find_expression(
            call.operands.front());
        const bool literal_format = format && format->systemverilog != nullptr
            && format->systemverilog->kind
                == semantic::sv::ExpressionKind::string_literal
            && format->systemverilog->decoded_string;
        frontend::ParsedInputFormat parsed;
        if (literal_format) {
            parsed = frontend::parse_input_format(
                *format->systemverilog->decoded_string);
            if (!parsed.valid || parsed.conversions.size() != 1U
                || parsed.conversions.front().suppress) {
                return fail("FSIM-ELAB-SVCLI-002",
                    "$value$plusargs format must contain exactly one "
                    "assignment conversion");
            }
            const auto parsed_format = parsed.conversions.front().format;
            if (parsed_format == frontend::InputScanFormat::Unformatted2
                || parsed_format == frontend::InputScanFormat::Unformatted4) {
                return fail("FSIM-ELAB-SVCLI-002",
                    "$value$plusargs does not accept unformatted %u or %z "
                    "conversions");
            }
            if (parsed_format == frontend::InputScanFormat::Hierarchy) {
                return fail("FSIM-ELAB-SVCLI-002",
                    "$value$plusargs does not accept %m");
            }
            if (parsed_format == frontend::InputScanFormat::Time) {
                return fail("FSIM-ELAB-SVCLI-002",
                    "$value$plusargs does not support %t");
            }
        }

        const auto target_id = call.operands[1];
        const auto target_declaration = hir_target_declaration(target_id);
        if (!target_declaration) {
            return fail("FSIM-ELAB-SVCLI-003",
                "$value$plusargs target must be a direct writable packed "
                "or string variable");
        }
        InputScanConversion conversion;
        if (literal_format) {
            const auto& parsed_conversion = parsed.conversions.front();
            conversion.prefix = parsed_conversion.prefix;
            conversion.format = runtime_input_format(parsed_conversion.format);
            conversion.maximum_characters
                = parsed_conversion.maximum_characters;
        }
        if (const auto string_binding = hir_string_binding(
                *target_declaration, hir_process_scope_, true)) {
            // A run-time format's conversion is a placeholder this target
            // accepts until the format is parsed.
            if (!literal_format) {
                conversion.format = InputScanFormat::string;
            }
            InputScanTarget string_target;
            string_target.kind = InputScanTargetKind::string_register;
            if (!input_scan_target_accepts(string_target, conversion.format)) {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs numeric conversion requires a packed "
                    "target");
            }
            if (!set_hir_string_scan_target(
                    conversion.target, *string_binding)) {
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
            conversion.target.width = static_cast<std::uint32_t>(
                binding->width);
            conversion.target.scalar_kind = kind;
            conversion.target.two_state
                = kind != frontend::SystemVerilogScalarKind::None
                || binding->domain == frontend::ValueDomain::Bit2;
            if (!literal_format) {
                conversion.format = frontend::is_real_scalar_kind(kind)
                    ? InputScanFormat::real
                    : kind == frontend::SystemVerilogScalarKind::Chandle
                    ? InputScanFormat::hexadecimal
                    : InputScanFormat::decimal;
            }
            if (!input_scan_target_accepts(
                    conversion.target, conversion.format)) {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs conversion is incompatible with the "
                    "target type");
            }
            if (!set_hir_packed_scan_target(
                    conversion.target, *binding)) {
                return fail("FSIM-ELAB-SVCLI-003",
                    "$value$plusargs target is read-only");
            }
        }

        std::optional<StringRegisterId> query;
        if (literal_format) {
            query = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                *query, parsed.conversions.front().prefix });
        } else {
            query = lower_hir_text_value(call.operands.front());
            if (!query) {
                return std::nullopt;
            }
        }
        const auto selected = allocate_string_register();
        const auto found = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(PlusArgSelect {
            found, *query, selected, !literal_format });
        const auto scanned = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        FileScan scan {
            scanned,
            0U,
            selected,
            true,
            { std::move(conversion) },
            parsed.trailing_text,
        };
        if (!literal_format) {
            scan.runtime_format = *query;
        }
        scan.plusarg_value = true;
        process_.operations.emplace_back(std::move(scan));
        // The result tells whether a plusarg matched, whatever the
        // conversion stored (IEEE 1800-2017 21.6).
        return expected_width != 0U && expected_width != 32U
            ? std::optional { resize_register(found, expected_width, false) }
            : std::optional { found };
    }

    if (call.text == "$fopen") {
        if (call.operands.empty() || call.operands.size() > 2U
            || !hir_filename_operand(
                call.operands.front(), hir_process_scope_)
            || (call.operands.size() == 2U
                && !hir_expression_is_string(
                    call.operands.back(), hir_process_scope_))) {
            return fail("FSIM-ELAB-SVFILE-003",
                "$fopen requires a byte-string filename and optional mode");
        }
        const auto path = lower_hir_filename(call.operands.front());
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
        const auto source_expression
            = specialized_hir_unit_->find_expression(call.operands[0]);
        const auto source_kind
            = hir_systemverilog_scan_value_kind(call.operands[0]);
        const bool packed_integral_source
            = source_kind
                == HirSystemVerilogScanValueKind::packed_integral;
        const bool string_source_value
            = source_kind == HirSystemVerilogScanValueKind::string
            || packed_integral_source
            || (source_expression
                && source_expression->systemverilog != nullptr
                && source_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::string_literal);
        const bool unknown_integral_format = string_source
            && string_source_value
            && hir_sscanf_unknown_integral_format(
                *specialized_hir_unit_, call)
            && hir_sscanf_unknown_format_simple_source(
                *specialized_hir_unit_, call);
        const auto format = specialized_hir_unit_->find_expression(
            call.operands[1]);
        if ((!format || format->systemverilog == nullptr
                || format->systemverilog->kind
                    != semantic::sv::ExpressionKind::string_literal
                || !format->systemverilog->decoded_string)
            && !unknown_integral_format) {
            return fail("FSIM-ELAB-SVFILE-011",
                call.text + " format must be a string literal");
        }
        if (unknown_integral_format) {
            // IEEE 1800-2017 21.3.4.3 requires EOF for an unknown bit in
            // the format; no scan target is assigned.
            const auto destination = load_constant(
                unsigned_value(std::numeric_limits<std::uint32_t>::max(), 32U),
                frontend::ValueDomain::Bit2);
            return finish(destination);
        }
        const auto parsed = frontend::parse_input_format(
            *format->systemverilog->decoded_string);
        // A format without conversions assigns nothing: `$sscanf("x", "")`
        // is 0 (IEEE 1800-2017 21.3.4.3).
        if ((string_source || parsed.trailing_text.empty()) && parsed.valid
            && parsed.conversions.empty() && call.operands.size() == 2U) {
            const auto destination = load_constant(
                unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
            return finish(destination);
        }
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
        if (std::ranges::any_of(parsed.conversions,
                [](const auto& conversion) {
                    return conversion.format
                        == frontend::InputScanFormat::Time;
                })) {
            operation.time_unit_femtoseconds
                = hir_time_unit_femtoseconds();
        }
        if (std::ranges::any_of(
                parsed.conversions,
                [](const auto& conversion) {
                    return conversion.format
                        == frontend::InputScanFormat::Hierarchy;
                })) {
            operation.hierarchy_path = hir_scope_hierarchy();
        }
        if (string_source) {
            auto source = lower_hir_string_expression(call.operands[0]);
            // Verilog packed strings are integral vectors whose rightmost
            // character occupies the least-significant byte (IEEE 1800-2017
            // 21.3.4.3). Convert once before any scan destination is written.
            if (!source) {
                const auto operand = call.operands[0];
                const auto width = hir_expression_width(
                    operand, hir_process_scope_);
                const auto domain = hir_expression_domain(
                    operand, hir_process_scope_);
                const auto kind = hir_systemverilog_scalar_kind(operand);
                if (width && *width != 0U && domain
                    && !frontend::is_real_scalar_kind(kind)
                    && kind != frontend::SystemVerilogScalarKind::Chandle
                    && (*domain == frontend::ValueDomain::Bit2
                        || *domain == frontend::ValueDomain::Logic4
                        || *domain == frontend::ValueDomain::Integer)) {
                    if (const auto packed = lower_hir_expression(operand, *width)) {
                        source = allocate_string_register();
                        process_.operations.emplace_back(
                            LoadStringConstant { *source, std::string { } });
                        auto format_packed = hir_format_packed(
                            *source, *packed, *width, OutputFormat::string);
                        format_packed.suppress_leading_zero = true;
                        process_.operations.emplace_back(format_packed);
                    }
                }
            }
            if (!source) {
                return fail("FSIM-ELAB-SVFILE-011",
                    "$sscanf source must be a string or packed integral expression");
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
        std::vector<std::pair<semantic::ExpressionId, RegisterId>>
            selected_scan_targets;
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
                if (!set_hir_string_scan_target(
                        conversion.target, *string_binding)) {
                    return fail("FSIM-ELAB-SVFILE-012",
                        "scan string target is read-only");
                }
            } else if (const auto target_expression
                = specialized_hir_unit_->find_expression(target_id);
                target_expression
                && target_expression->systemverilog != nullptr
                && target_expression->systemverilog->kind
                    != semantic::sv::ExpressionKind::name) {
                // A selected target (`v[i]`, `x[7:0]`, `s.m`) scans into a
                // copy of its value that is written back afterwards, so an
                // unconverted target keeps its value (21.3.4.3).
                const auto width = hir_expression_width(
                    target_id, hir_process_scope_);
                if (!width || *width == 0U
                    || *width > std::numeric_limits<std::uint32_t>::max()) {
                    return fail("FSIM-ELAB-SVFILE-012",
                        "scan target has no writable packed storage");
                }
                const auto copy = lower_hir_expression(target_id, *width);
                if (!copy) {
                    return fail("FSIM-ELAB-SVFILE-012",
                        "scan target has no writable packed storage");
                }
                conversion.target.width = static_cast<std::uint32_t>(*width);
                conversion.target.scalar_kind
                    = expression_scalar_kind(target_id);
                conversion.target.two_state
                    = conversion.target.scalar_kind
                        != frontend::SystemVerilogScalarKind::None
                    || register_domain(*copy) == frontend::ValueDomain::Bit2;
                conversion.target.kind = InputScanTargetKind::packed_register;
                conversion.target.id = *copy;
                selected_scan_targets.emplace_back(target_id, *copy);
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
                if (!set_hir_packed_scan_target(
                        conversion.target, *binding)) {
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
        for (const auto& [target, copy] : selected_scan_targets) {
            if (!lower_hir_output_actual_write(target, copy)) {
                return fail("FSIM-ELAB-SVFILE-012",
                    "scan target has no writable packed storage");
            }
        }
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
    const auto called = vhdl_simple_name(
        call.procedure.text());
    if (!vhdl_file_procedure_name(called)) {
        return false;
    }
    // VHDL-2008 SWRITE(L, VALUE) is WRITE of a STRING (16.4).
    const auto name = called == "swrite" ? std::string_view { "write" }
        : called == "string_read" ? std::string_view { "sread" }
        : called == "hex_read" ? std::string_view { "hread" }
        : called == "octal_read" ? std::string_view { "oread" }
        : called == "hex_write" ? std::string_view { "hwrite" }
        : called == "octal_write" ? std::string_view { "owrite" }
                                   : called;
    const auto span = hir_source_span(call.source);
    const auto fail = [&](std::string code, std::string message) {
        report(std::move(code), std::move(message), span);
        return true;
    };
    if (name == "sread"
        && vhdl_standard_ < frontend::VhdlStandard::Vhdl2008) {
        return fail("FSIM-ELAB-VHSTD-001",
            "TextIO SREAD and STRING_READ require VHDL-2008");
    }
    if ((name == "hread" || name == "oread" || name == "hwrite"
            || name == "owrite")
        && vhdl_standard_ < frontend::VhdlStandard::Vhdl2008) {
        return fail("FSIM-ELAB-VHSTD-001",
            "TextIO HREAD, OREAD, HWRITE, and OWRITE require VHDL-2008");
    }
    const auto actual = [&](const std::string_view formal,
                            const std::size_t position)
        -> std::optional<semantic::ExpressionId> {
        std::size_t positional { };
        for (const auto& association : call.procedure_arguments) {
            if (association.formal) {
                const auto formal_name = association.formal->text();
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
        if (subtype && !subtype->type_mark.target.valid()
            && vhdl_simple_name(subtype->type_mark.spelling) == "text") {
            // IEEE 1076-2008 16.4: std.textio.TEXT is a file of STRING.
            semantic::vhdl::SubtypeIndication element;
            element.domain = semantic::vhdl::ValueDomain::string;
            return element;
        }
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
    const auto standard_text_file = [&](const semantic::ExpressionId value) {
        const auto declaration_id = hir_target_declaration(value);
        const auto declaration = declaration_id
            ? specialized_hir_unit_->find_declaration(*declaration_id)
            : std::nullopt;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return false;
        }
        const auto subtype = hir_effective_vhdl_subtype(
            *declaration->vhdl->subtype);
        return subtype && !subtype->type_mark.target.valid()
            && vhdl_simple_name(subtype->type_mark.spelling) == "text";
    };
    const auto standard_output = [&](const semantic::ExpressionId value) {
        if (const auto declared = hir_target_declaration(value)) {
            // A file opened on STD_OUTPUT, the logical name std.textio
            // gives OUTPUT, is that same host file (IEEE 1076-2008 16.4).
            const auto file = specialized_hir_unit_->find_declaration(
                *declared);
            if (!file || file->vhdl == nullptr
                || file->vhdl->form != semantic::vhdl::DeclarationForm::file
                || !file->vhdl->initializer) {
                return false;
            }
            const auto logical_name
                = specialized_hir_unit_->evaluate_string_expression(
                    *file->vhdl->initializer);
            return logical_name && *logical_name == "STD_OUTPUT";
        }
        const auto expression = specialized_hir_unit_->find_expression(
            value);
        if (!expression || expression->vhdl == nullptr
            || !expression->vhdl->referenced_name) {
            return false;
        }
        const auto& referenced = *expression->vhdl->referenced_name;
        const auto spelling = referenced.text();
        return vhdl_simple_name(spelling) == "output";
    };
    enum class TextioScalarClass : std::uint8_t {
        unsupported,
        integer,
        boolean,
        bit,
        bit_vector,
        character,
        real,
        string,
    };
    const auto textio_scalar_class = [&](const semantic::ExpressionId value) {
        if (hir_expression_is_string(value, hir_process_scope_)) {
            return TextioScalarClass::string;
        }
        if (hir_vhdl_character_expression(value)) {
            return TextioScalarClass::character;
        }
        if (hir_systemverilog_scalar_kind(value)
            == frontend::SystemVerilogScalarKind::Real) {
            return TextioScalarClass::real;
        }
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
        if (subtype_name == "character") {
            return TextioScalarClass::character;
        }
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
            const auto type_name = vhdl_simple_name(type->vhdl->name);
            if (type_name == "character") {
                return TextioScalarClass::character;
            }
            if (type_name == "bit_vector") {
                return TextioScalarClass::bit_vector;
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
            const auto base_name = vhdl_simple_name(
                type->vhdl->base.type_mark.spelling);
            if (base_name == "character") {
                return TextioScalarClass::character;
            }
            if (base_name == "bit_vector") {
                return TextioScalarClass::bit_vector;
            }
            type_id = type->vhdl->base.type_mark.target;
        }
        if (subtype->domain == semantic::vhdl::ValueDomain::bit2
            && subtype->executable_width
            && *subtype->executable_width > 1U
            && subtype_name == "bit_vector") {
            return TextioScalarClass::bit_vector;
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

    const auto output_handle = [&] {
        const auto handle = load_constant(
            unsigned_value(1U, 32U), frontend::ValueDomain::Bit2);
        return handle;
    };
    // VHDL-2008 FLUSH (5.5.2) writes out buffered data of an open file.
    if (name == "flush") {
        const auto target = actual("f", 0U);
        if (call.procedure_arguments.size() == 1U && target
            && standard_output(*target)) {
            process_.operations.emplace_back(
                FileFlush { output_handle(), false });
            return true;
        }
        const auto file = file_actual(0U);
        if (call.procedure_arguments.size() != 1U || !file) {
            return fail("FSIM-ELAB-VHFILE-005",
                "VHDL file operation requires a whole file object");
        }
        process_.operations.emplace_back(
            FileFlush { *file->second.local, false });
        return true;
    }
    if (name == "readline" || name == "writeline" || name == "tee") {
        const auto line = actual("l", 1U);
        const auto line_local = line ? string_binding(*line) : std::nullopt;
        // IEEE 1076-2008 16.4: std.textio.OUTPUT is the host standard
        // output, the runtime's multichannel stdout channel.
        if (const auto target = actual("f", 0U);
            name != "readline" && target && standard_output(*target)
            && call.procedure_arguments.size() == 2U && line_local
            && line_local->local) {
            if (name == "tee") {
                process_.operations.emplace_back(FileWriteString {
                    output_handle(), *line_local->local, { }, { }, true,
                    false });
            }
            process_.operations.emplace_back(FileWriteString {
                output_handle(), *line_local->local, { }, { }, true, true });
            return true;
        }
        const auto file = file_actual(0U);
        const auto element_subtype = file
            ? file_element_subtype(file->first)
            : std::nullopt;
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
        } else if (name == "tee") {
            // VHDL-2008 TEE (16.4) writes the line to the file and to
            // OUTPUT, then empties it like WRITELINE.
            process_.operations.emplace_back(FileWriteString {
                *file->second.local, *line_local->local,
                { }, { }, true, false });
            process_.operations.emplace_back(FileWriteString {
                output_handle(), *line_local->local,
                { }, { }, true, true });
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
    const bool positioning = name == "file_rewind" || name == "file_seek"
        || name == "file_truncate";
    if ((name == "file_open" || name == "file_close" || positioning)
        && !file) {
        return fail("FSIM-ELAB-VHFILE-005",
            "VHDL file operation requires a whole file object");
    }
    if (positioning) {
        // IEEE 1076-2019 5.5.2.
        const auto rewind = name == "file_rewind";
        const auto amount = rewind
            ? std::nullopt
            : actual(name == "file_seek" ? "offset" : "size", 1U);
        const auto origin = rewind ? std::nullopt : actual("origin", 2U);
        if (rewind ? call.procedure_arguments.size() != 1U
                   : !amount || call.procedure_arguments.size() > 3U) {
            return fail("FSIM-ELAB-VHFILE-005",
                std::string { name }
                    + (rewind ? " requires exactly one file object"
                              : " requires a file object, an INTEGER, and "
                                "an optional FILE_ORIGIN_KIND"));
        }
        return lower_hir_vhdl_file_position(rewind
                    ? FilePositionKind::rewind
                    : name == "file_seek" ? FilePositionKind::seek
                                          : FilePositionKind::truncate,
                   *file->second.local, file->first, amount, origin)
            .has_value();
    }
    if (name == "file_close") {
        if (call.procedure_arguments.size() != 1U) {
            return fail("FSIM-ELAB-VHFILE-006",
                "file_close requires exactly one file object");
        }
        // Closing a file that is not open has no effect (IEEE 1076-2008
        // 5.5.2).
        process_.operations.emplace_back(FileClose {
            *file->second.local, true, true });
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
        const auto mode_text = vhdl_file_open_mode(kind_name);
        if (mode_text.empty()) {
            return fail("FSIM-ELAB-VHFILE-004",
                "VHDL file open kind must be read_mode, write_mode, "
                "append_mode, or read_write_mode");
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
        if (name == "sread") {
            const auto value = actual("value", 1U);
            const auto length = actual("strlen", 2U);
            if (call.procedure_arguments.size() != 3U || !value || !length) {
                return fail("FSIM-ELAB-VHTEXTIO-003",
                    "TextIO SREAD requires line, string value, and "
                    "character-count actuals");
            }
            const auto target = string_binding(*value);
            const auto width = hir_expression_width(
                *value, hir_process_scope_);
            const auto length_binding = runtime_binding(*length);
            if (textio_scalar_class(*value) != TextioScalarClass::string
                || !target || !target->local || !width || *width % 8U != 0U
                || *width / 8U
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max())
                || !length_binding || !length_binding->local
                || length_binding->domain != frontend::ValueDomain::Integer
                || length_binding->width != 32U
                || textio_scalar_class(*length) != TextioScalarClass::integer) {
                return fail("FSIM-ELAB-VHTEXTIO-004",
                    "TextIO SREAD requires a bounded writable STRING and "
                    "a writable NATURAL-compatible integer count");
            }
            InputScanConversion conversion;
            conversion.prefix = vhdl_textio_sread_marker;
            conversion.format = InputScanFormat::string;
            conversion.maximum_characters
                = static_cast<std::uint32_t>(*width / 8U);
            conversion.target = {
                InputScanTargetKind::string_register,
                *target->local, 1U, true,
            };
            process_.operations.emplace_back(FileScan {
                *length_binding->local, 0U, *line->local, true,
                { std::move(conversion) }, { }, false,
                std::nullopt, true });
            return true;
        }
        const auto value = actual("value", 1U);
        if (!value) {
            return fail("FSIM-ELAB-VHTEXTIO-003",
                "TextIO read/write requires a value actual");
        }
        if (name == "read" || name == "hread" || name == "oread") {
            const bool radix_read = name == "hread" || name == "oread";
            if (call.procedure_arguments.size() < 2U
                || call.procedure_arguments.size() > 3U) {
                return fail("FSIM-ELAB-VHTEXTIO-003",
                    "TextIO read accepts line, value, and optional good "
                    "actuals");
            }
            InputScanConversion conversion;
            conversion.prefix = radix_read
                ? vhdl_textio_radix_read_2008_marker
                : vhdl_standard_ >= frontend::VhdlStandard::Vhdl2008
                    ? vhdl_textio_read_2008_marker
                    : vhdl_textio_read_marker;
            const auto value_class = textio_scalar_class(*value);
            std::optional<RegisterId> selection_target;
            std::optional<RegisterId> selection_scan_target;
            std::optional<std::uint32_t> selection_offset;
            std::optional<HirVhdlRuntimeSlice> runtime_slice;
            if (radix_read) {
                const auto target = runtime_binding(*value);
                const auto target_width = hir_expression_width(
                    *value, hir_process_scope_);
                const auto target_expression
                    = specialized_hir_unit_->find_expression(*value);
                bool selected_target = false;
                std::optional<HirConstantSelection> target_selection;
                if (target_expression
                    && target_expression->vhdl != nullptr) {
                    const auto& selected = *target_expression->vhdl;
                    if (selected.kind
                            == semantic::vhdl::ExpressionKind::index
                        || selected.kind
                            == semantic::vhdl::ExpressionKind::slice) {
                        selected_target = true;
                        if (!selected.operands.empty()) {
                            const auto base = selected.operands.front();
                            const auto base_expression
                                = specialized_hir_unit_->find_expression(base);
                            if (base_expression
                                && base_expression->vhdl != nullptr
                                && base_expression->vhdl->kind
                                    == semantic::vhdl::ExpressionKind::name
                                && hir_target_declaration(base)
                                    == hir_target_declaration(*value)
                                && !hir_vhdl_member_selection(base)) {
                                target_selection = hir_constant_selection(
                                    *value, hir_process_scope_);
                            }
                        }
                    } else if (hir_vhdl_member_selection(*value)) {
                        // A record member needs its own range-relative
                        // storage mapping; do not treat the record register
                        // as the selected BIT_VECTOR.
                        selected_target = true;
                    }
                }
                if (value_class != TextioScalarClass::bit_vector
                    || !target || !target->local || !target_width
                    || *target_width == 0U
                    || *target_width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return fail("FSIM-ELAB-VHTEXTIO-004",
                        "TextIO HREAD and OREAD require a writable bounded "
                        "BIT_VECTOR value");
                }
                if (selected_target
                    && (!target_selection
                        || target_selection->width != *target_width
                        || target_selection->offset
                            > std::numeric_limits<std::uint32_t>::max()
                        || target_selection->offset > target->width
                        || target_selection->width
                            > target->width - target_selection->offset)) {
                    return fail("FSIM-ELAB-VHTEXTIO-004",
                        "TextIO HREAD and OREAD require a statically "
                        "bounded writable BIT_VECTOR selection");
                }
                const auto scan_target = selected_target
                    ? allocate_register(
                          static_cast<std::uint32_t>(*target_width),
                          target->domain)
                    : *target->local;
                conversion.format = name == "hread"
                    ? InputScanFormat::hexadecimal
                    : InputScanFormat::octal;
                conversion.target = {
                    InputScanTargetKind::packed_register,
                    scan_target,
                    static_cast<std::uint32_t>(*target_width),
                    true,
                };
                if (selected_target) {
                    selection_target = *target->local;
                    selection_scan_target = scan_target;
                    selection_offset = static_cast<std::uint32_t>(
                        target_selection->offset);
                }
            } else if (value_class == TextioScalarClass::string) {
                const auto target = string_binding(*value);
                const auto width = hir_expression_width(
                    *value, hir_process_scope_);
                if (!target || !target->local
                    || (width && *width % 8U != 0U)
                    || (width && *width / 8U
                        > std::numeric_limits<std::uint32_t>::max())) {
                    return fail("FSIM-ELAB-VHTEXTIO-004",
                        "TextIO string read requires a writable STRING "
                        "target");
                }
                conversion.target = {
                    InputScanTargetKind::string_register,
                    *target->local, 1U, true,
                };
                conversion.format = InputScanFormat::string;
                // Zero asks the runtime to use the target string's
                // retained bound. This covers dynamically constrained and
                // null STRING targets without changing the FileScan shape.
                conversion.maximum_characters = width
                        && *width / 8U
                            <= std::numeric_limits<std::uint32_t>::max()
                    ? static_cast<std::uint32_t>(*width / 8U)
                    : 0U;
            } else {
                const auto target = runtime_binding(*value);
                if (!target || !target->local || target->width == 0U
                    || target->width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return fail("FSIM-ELAB-VHTEXTIO-004",
                        "TextIO read value must be a writable bounded "
                        "object");
                }
                // A static slice of a BIT_VECTOR reads into that part of
                // the object (IEEE 1076-2008 16.4).
                auto scan_target = *target->local;
                auto scan_width = static_cast<std::uint32_t>(target->width);
                if (value_class == TextioScalarClass::bit_vector) {
                    const auto selected
                        = specialized_hir_unit_->find_expression(*value);
                    const auto base = selected && selected->vhdl != nullptr
                            && selected->vhdl->kind
                                == semantic::vhdl::ExpressionKind::slice
                            && !selected->vhdl->operands.empty()
                        ? specialized_hir_unit_->find_expression(
                              selected->vhdl->operands.front())
                        : std::nullopt;
                    const auto selection = base && base->vhdl != nullptr
                            && base->vhdl->kind
                                == semantic::vhdl::ExpressionKind::name
                            && hir_target_declaration(
                                   selected->vhdl->operands.front())
                                == hir_target_declaration(*value)
                            && !hir_vhdl_member_selection(
                                selected->vhdl->operands.front())
                        ? hir_constant_selection(*value, hir_process_scope_)
                        : std::nullopt;
                    if (selection && selection->width != 0U
                        && selection->offset <= target->width
                        && selection->width
                            <= target->width - selection->offset) {
                        scan_width = static_cast<std::uint32_t>(
                            selection->width);
                        scan_target = allocate_register(
                            scan_width, target->domain);
                        selection_target = *target->local;
                        selection_scan_target = scan_target;
                        selection_offset = static_cast<std::uint32_t>(
                            selection->offset);
                    } else if (selected && selected->vhdl != nullptr
                        && selected->vhdl->kind
                            == semantic::vhdl::ExpressionKind::slice) {
                        // A slice with run-time bounds reads exactly its
                        // length (16.4, 8.5).
                        runtime_slice = base && base->vhdl != nullptr
                                && base->vhdl->kind
                                    == semantic::vhdl::ExpressionKind::name
                                && hir_target_declaration(
                                       selected->vhdl->operands.front())
                                    == hir_target_declaration(*value)
                                && !hir_vhdl_member_selection(
                                    selected->vhdl->operands.front())
                                && target->width <= 4096U
                            ? lower_hir_vhdl_runtime_slice(
                                  *value, target->width)
                            : std::nullopt;
                        if (!runtime_slice
                            || runtime_slice->element_width == 0U) {
                            return fail("FSIM-ELAB-VHTEXTIO-004",
                                "TextIO read value must be a writable "
                                "bounded object");
                        }
                        selection_target = *target->local;
                    }
                }
                conversion.target = {
                    InputScanTargetKind::packed_register,
                    scan_target,
                    scan_width,
                    true,
                };
                // A TIME value is an abstract literal and a unit name
                // (IEEE 1076-2008 16.4, 15.5.3).
                const auto time_target = [&] {
                    const auto subtype = hir_vhdl_expression_subtype(*value);
                    if (!subtype) {
                        return false;
                    }
                    const auto is_time_name = [](const std::string_view name) {
                        return name == "time" || name == "delay_length";
                    };
                    if (is_time_name(vhdl_simple_name(
                            semantic::vhdl::without_builtin_prefix(
                                subtype->type_mark.spelling)))) {
                        return true;
                    }
                    const auto* base = semantic::vhdl_base_type_definition(
                        *specialized_hir_unit_, subtype->type_mark.target);
                    return base != nullptr
                        && base->form == semantic::vhdl::TypeForm::physical
                        && is_time_name(vhdl_simple_name(base->name));
                };
                if (value_class == TextioScalarClass::integer
                    && time_target()) {
                    conversion.format = InputScanFormat::time;
                } else if (value_class == TextioScalarClass::integer) {
                    conversion.format = InputScanFormat::decimal;
                } else if (value_class == TextioScalarClass::boolean) {
                    conversion.format = InputScanFormat::boolean_value;
                } else if (value_class == TextioScalarClass::bit
                    || value_class == TextioScalarClass::bit_vector) {
                    conversion.format = InputScanFormat::binary;
                } else if (value_class == TextioScalarClass::character) {
                    conversion.format = InputScanFormat::character;
                } else if (value_class == TextioScalarClass::real) {
                    conversion.format = InputScanFormat::real;
                    conversion.target.scalar_kind
                        = frontend::SystemVerilogScalarKind::Real;
                } else {
                    return fail("FSIM-ELAB-VHTEXTIO-005",
                        "TextIO read supports INTEGER, BOOLEAN, BIT, "
                        "BIT_VECTOR, CHARACTER, REAL, and STRING values");
                }
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
            const bool time_conversion
                = conversion.format == InputScanFormat::time;
            if (runtime_slice) {
                // One scan per possible length, each into a temporary of
                // that width inserted at the slice's run-time offset; a
                // null slice reads nothing (IEEE 1076-2008 16.4).
                const auto& slice = *runtime_slice;
                const auto object_width = register_width(*selection_target);
                const auto element_width = slice.element_width;
                std::vector<InstructionIndex> exits;
                if (success) {
                    process_.operations.emplace_back(LoadConstant {
                        *success, unsigned_value(1U, 1U) });
                }
                for (std::size_t elements = 1U;
                     elements * element_width <= object_width; ++elements) {
                    const auto width = elements * element_width;
                    const auto matches = allocate_register(
                        1U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::equal, matches, slice.length,
                        load_constant(unsigned_value(elements,
                                          register_width(slice.length)),
                            frontend::ValueDomain::Bit2) });
                    const auto branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch { });
                    const auto temporary = allocate_register(
                        width, register_domain(*selection_target));
                    auto sized = conversion;
                    sized.target = { InputScanTargetKind::packed_register,
                        temporary, static_cast<std::uint32_t>(width), true };
                    process_.operations.emplace_back(FileScan { count, 0U,
                        *line->local, true, { std::move(sized) }, { },
                        !success, success, true });
                    std::optional<InstructionIndex> skip_insert;
                    if (success) {
                        skip_insert = static_cast<InstructionIndex>(
                            process_.operations.size());
                        process_.operations.emplace_back(Branch { });
                    }
                    const auto insert = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(DynamicPartInsert {
                        *selection_target, *selection_target, temporary,
                        DynamicPartIndex { slice.base,
                            static_cast<std::int64_t>(object_width - 1U), 0,
                            0U, static_cast<std::uint32_t>(width), true,
                            true } });
                    exits.push_back(static_cast<InstructionIndex>(
                        process_.operations.size()));
                    process_.operations.emplace_back(Jump { });
                    if (skip_insert) {
                        process_.operations[*skip_insert] = Branch {
                            *success, insert, exits.back(),
                            UnknownBranchPolicy::when_false };
                    }
                    process_.operations[branch] = Branch { matches,
                        branch + 1U,
                        static_cast<InstructionIndex>(
                            process_.operations.size()),
                        UnknownBranchPolicy::when_false };
                }
                const auto end = static_cast<InstructionIndex>(
                    process_.operations.size());
                for (const auto exit : exits) {
                    process_.operations[exit] = Jump { end };
                }
            } else {
            FileScan scan { count, 0U, *line->local, true,
                { std::move(conversion) }, { }, !success,
                success, true };
            if (time_conversion) {
                scan.time_unit_femtoseconds = hir_time_unit_femtoseconds();
            }
            process_.operations.emplace_back(std::move(scan));
            }
            if (selection_target && selection_scan_target
                && selection_offset) {
                // FileScan writes a full temporary value. Update the
                // selected part only after a successful conversion so
                // GOOD failures preserve the original target.
                const auto update_target = [&] {
                    process_.operations.emplace_back(Insert {
                        *selection_target,
                        *selection_target,
                        *selection_scan_target,
                        *selection_offset,
                    });
                };
                if (success) {
                    const auto branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch {
                        *success,
                        branch + 1U,
                        branch + 2U,
                        UnknownBranchPolicy::when_false,
                    });
                    update_target();
                } else {
                    update_target();
                }
            }
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
                    runtime_source_location(span),
                });
            }
            return true;
        }

        // IEEE 1076-2008 16.4: WRITE of TIME takes a UNIT and WRITE of
        // REAL takes DIGITS as the fifth parameter.
        const bool time_value = hir_vhdl_time_expression(*value);
        const auto value_type_name = [&] {
            const auto subtype = hir_vhdl_expression_subtype(*value);
            if (!subtype) {
                return std::string { };
            }
            auto name = std::string_view { subtype->type_mark.spelling };
            name = simple_name_of(name);
            return std::string { name };
        }();
        const bool real_value = value_type_name == "real"
            || hir_systemverilog_scalar_kind(*value)
                == frontend::SystemVerilogScalarKind::Real
            || hir_vhdl_expression_is_real(*value);
        const auto fifth = actual(time_value ? "unit" : "digits", 4U);
        if (call.procedure_arguments.size() < 2U
            || call.procedure_arguments.size()
                > (time_value || real_value ? 5U : 4U)) {
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
        const auto field_actual = actual("field", 3U);
        bool dynamic_field { };
        if (field_actual) {
            if (const auto width = hir_constant_integer(*field_actual)) {
                if (*width < 0) {
                    // WIDTH is a NATURAL formal. Its subtype check belongs to
                    // the executed call; defer it so a statically false branch
                    // containing -TIME_WIDTH does not reject the whole unit.
                    // The dynamic-field operation performs the full-width
                    // 0..4096 check if this path executes.
                    dynamic_field = true;
                } else if (*width
                    > static_cast<std::int64_t>(maximum_string_bytes)) {
                    return fail("FSIM-ELAB-VHTEXTIO-008",
                        "bounded TextIO field must be in 0..4096");
                } else {
                    minimum_width = static_cast<std::uint32_t>(*width);
                }
            } else {
                dynamic_field = true;
            }
        }
        std::optional<RegisterId> dynamic_field_register;
        const auto lower_dynamic_field = [&] {
            if (!dynamic_field) {
                return true;
            }
            const auto lowered = lower_hir_expression(*field_actual, 64U);
            if (!lowered) {
                return false;
            }
            const auto width = register_width(*lowered);
            if (width > 64U) {
                return false;
            }
            dynamic_field_register = width == 64U
                ? *lowered
                : resize_register(*lowered, 64U,
                      hir_expression_signed(*field_actual));
            return true;
        };
        const auto append_string = [&](const StringRegisterId source) {
            StringMethod operation;
            operation.operation = dynamic_field_register
                ? StringMethodOperator::format_string_dynamic_field
                : StringMethodOperator::format_string;
            operation.source = *line->local;
            operation.argument = source;
            if (dynamic_field_register) {
                operation.first = *dynamic_field_register;
            } else {
                operation.minimum_width = minimum_width;
            }
            operation.left_justify = left_justify;
            process_.operations.emplace_back(operation);
        };
        const auto append_radix_string = [&](const RegisterId value_register,
                                             const std::uint64_t value_width,
                                             const OutputFormat format) {
            if (value_width == 0U
                || value_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            const auto formatted = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant { formatted, std::string { } });
            auto format_operation = hir_format_packed(
                formatted, value_register, value_width, format);
            process_.operations.emplace_back(format_operation);

            // TextIO TO_HSTRING and TO_OSTRING use uppercase digits. The
            // shared SystemVerilog formatter intentionally emits lowercase.
            StringMethod uppercase;
            uppercase.operation = StringMethodOperator::toupper;
            uppercase.source = formatted;
            uppercase.string_destination = formatted;
            process_.operations.emplace_back(uppercase);
            append_string(formatted);
            return true;
        };
        if (name == "hwrite" || name == "owrite") {
            // A direct string literal in this context is a context-dependent
            // BIT_VECTOR actual (IEEE 1076-2008 8.6). Materialize its decoded
            // bit payload explicitly: generic STRING lowering may choose an
            // 8-bit CHARACTER representation for the same syntax.
            std::optional<std::string> literal_bits;
            if (const auto expression
                = specialized_hir_unit_->find_expression(*value);
                expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal
                && expression->vhdl->decoded_string) {
                const auto& decoded = *expression->vhdl->decoded_string;
                if (std::ranges::all_of(decoded, [](const char bit) {
                        return bit == '0' || bit == '1';
                    })) {
                    literal_bits = decoded;
                }
            }
            const bool is_bit_vector = literal_bits.has_value()
                || textio_scalar_class(*value)
                    == TextioScalarClass::bit_vector;
            const auto width = literal_bits
                ? std::optional<std::size_t> { literal_bits->size() }
                : hir_expression_width(*value, hir_process_scope_);
            if (!is_bit_vector || !width || *width == 0U
                || *width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return fail("FSIM-ELAB-VHTEXTIO-009",
                    "TextIO HWRITE and OWRITE require a bounded "
                    "BIT_VECTOR value");
            }
            std::optional<RegisterId> lowered;
            if (literal_bits) {
                const auto destination = allocate_register(
                    *width, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    PackedLogic4::from_msb_string(*literal_bits),
                });
                lowered = destination;
            } else {
                lowered = lower_hir_expression(*value, *width);
            }
            if (!lowered || !lower_dynamic_field()) {
                return false;
            }
            return append_radix_string(*lowered, *width,
                name == "hwrite"
                    ? OutputFormat::hexadecimal
                    : OutputFormat::octal);
        }
        if (hir_expression_is_string(*value, hir_process_scope_)) {
            const auto lowered = lower_hir_string_expression(*value);
            if (!lowered) {
                return false;
            }
            if (!lower_dynamic_field()) {
                return false;
            }
            append_string(*lowered);
            return true;
        }
        if (time_value) {
            // The value is written as an integer multiple of UNIT followed
            // by the unit name; the default unit is NS.
            std::string unit_name { "ns" };
            if (fifth) {
                const auto expression
                    = specialized_hir_unit_->find_expression(*fifth);
                const auto* vhdl_expression
                    = expression ? expression->vhdl : nullptr;
                const bool normalized_time_unit
                    = vhdl_expression != nullptr
                    && vhdl_expression->kind
                        == semantic::vhdl::ExpressionKind::integer_literal
                    && vhdl_expression->nominal_type == "@builtin:time"
                    && vhdl_expression->folded
                    && vhdl_expression->referenced_name
                    && !vhdl_expression->referenced_name->selected
                    && vhdl_expression->referenced_name->overloads.empty();
                if (normalized_time_unit) {
                    const auto& referenced
                        = *vhdl_expression->referenced_name;
                    const auto text = referenced.text();
                    unit_name = std::string { text };
                } else {
                    auto text = expression && expression->vhdl != nullptr
                        ? std::string_view { expression->vhdl->text }
                        : std::string_view { };
                    constexpr auto physical
                        = std::string_view { "@vhdl-physical:" };
                    if (text.starts_with(physical)) {
                        text.remove_prefix(physical.size());
                    }
                    unit_name = std::string { vhdl_simple_name(text) };
                }
                lowercase_ascii(unit_name);
            }
            const auto unit = std::ranges::find(vhdl_time_units, unit_name,
                &std::pair<std::string_view, std::uint64_t>::first);
            if (unit == vhdl_time_units.end()) {
                return fail("FSIM-ELAB-VHTEXTIO-010",
                    "TextIO write of TIME requires a static time unit");
            }
            const auto text = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant { text, std::string { } });
            if (!append_hir_vhdl_time_text(text, *value,
                    static_cast<std::uint32_t>(unit - vhdl_time_units.begin()))) {
                return false;
            }
            if (!lower_dynamic_field()) {
                return false;
            }
            append_string(text);
            return true;
        }
        if (real_value) {
            const auto digits = fifth
                ? hir_constant_integer(*fifth)
                : std::optional<std::int64_t> { 0 };
            if (!digits || *digits < 0 || *digits > 64) {
                return fail("FSIM-ELAB-VHTEXTIO-011",
                    "TextIO write of REAL requires static DIGITS in 0..64");
            }
            {
                // Nonzero DIGITS selects fixed-point notation with that many
                // digits after the point; zero, the default, selects the
                // exponent form (IEEE 1076-2008 16.4).
                const auto lowered = lower_hir_expression(*value, 64U);
                if (!lowered) {
                    return false;
                }
                if (!lower_dynamic_field()) {
                    return false;
                }
                StringMethod operation;
                operation.operation = dynamic_field_register
                    ? StringMethodOperator::format_packed_dynamic_field
                    : StringMethodOperator::format_packed;
                operation.source = *line->local;
                operation.first = *lowered;
                if (dynamic_field_register) {
                    operation.second = *dynamic_field_register;
                } else {
                    operation.second = allocate_register(
                        32U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        operation.second, unsigned_value(64U, 32U) });
                }
                operation.format = *digits > 0
                    ? OutputFormat::real_fixed
                    : OutputFormat::real_scientific;
                operation.scalar_kind
                    = frontend::SystemVerilogScalarKind::Real;
                operation.precision = *digits > 0
                    ? static_cast<std::uint32_t>(*digits)
                    : 6U;
                if (!dynamic_field_register) {
                    operation.minimum_width = minimum_width;
                }
                operation.left_justify = left_justify;
                process_.operations.emplace_back(operation);
                return true;
            }
        }
        // A BIT_VECTOR slice with run-time bounds has no fixed width: its
        // characters are those of the prefix at the slice's positions
        // (IEEE 1076-2019 8.5, 16.4).
        if (textio_scalar_class(*value) == TextioScalarClass::bit_vector
            && !hir_expression_width(*value, hir_process_scope_)) {
            const auto text = lower_hir_vhdl_bit_slice_text(*value);
            if (!text || !lower_dynamic_field()) {
                return false;
            }
            append_string(*text);
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
        // A CHARACTER literal appends that character (IEEE 1076-2008 5.7,
        // 16.4).
        if (const auto expression
            = specialized_hir_unit_->find_expression(*value);
            value_class == TextioScalarClass::character && expression
            && expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal
            && expression->vhdl->text.size() == 3U) {
            if (!lower_dynamic_field()) {
                return false;
            }
            const auto text = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                text, std::string(1U, expression->vhdl->text[1]) });
            append_string(text);
            return true;
        }
        if (value_class == TextioScalarClass::integer
            || value_class == TextioScalarClass::bit) {
            const auto lowered = lower_hir_expression(*value, *width);
            if (!lowered) {
                return false;
            }
            if (!lower_dynamic_field()) {
                return false;
            }
            StringMethod operation;
            operation.operation = dynamic_field_register
                ? StringMethodOperator::format_packed_dynamic_field
                : StringMethodOperator::format_packed;
            operation.source = *line->local;
            operation.first = *lowered;
            if (dynamic_field_register) {
                operation.second = *dynamic_field_register;
            } else {
                operation.second = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    operation.second, unsigned_value(*width, 32U) });
            }
            operation.format
                = value_class == TextioScalarClass::integer
                ? OutputFormat::decimal
                : OutputFormat::binary;
            operation.signed_decimal
                = value_class == TextioScalarClass::integer;
            if (!dynamic_field_register) {
                operation.minimum_width = minimum_width;
            }
            operation.left_justify = left_justify;
            process_.operations.emplace_back(operation);
            return true;
        }
        if (value_class == TextioScalarClass::boolean) {
            const auto condition = lower_hir_expression(*value, 1U);
            if (!condition) {
                return false;
            }
            if (!lower_dynamic_field()) {
                return false;
            }
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *condition, 0U, 0U, UnknownBranchPolicy::error });
            const auto true_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto true_text = allocate_string_register();
            // The string representation of an identifier value is in
            // lower case (IEEE 1076-2008 5.7), as 'IMAGE gives it.
            process_.operations.emplace_back(LoadStringConstant {
                true_text, "true" });
            append_string(true_text);
            const auto skip_false = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { });
            const auto false_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto false_text = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                false_text, "false" });
            append_string(false_text);
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *condition, true_start, false_start,
                UnknownBranchPolicy::error };
            process_.operations[skip_false] = Jump { end };
            return true;
        }
        // STD.TEXTIO declares WRITE for the predefined scalar and vector
        // types only; an enumeration type declared in the design has none
        // (IEEE 1076-2008 16.4).
        const auto user_enumeration = [&] {
            const auto subtype = hir_vhdl_expression_subtype(*value);
            // Nor does it for the STANDARD enumerations SEVERITY_LEVEL,
            // FILE_OPEN_KIND and FILE_OPEN_STATUS.
            if (subtype) {
                const auto mark = lowercase_ascii_copy(
                    simple_name_of(subtype->type_mark.spelling));
                if (mark == "severity_level" || mark == "file_open_kind"
                    || mark == "file_open_status") {
                    return true;
                }
            }
            auto type_id = subtype ? subtype->type_mark.target
                                   : semantic::TypeId { };
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid() && visited.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return false;
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::enumeration) {
                    const auto name = vhdl_simple_name(type->vhdl->name);
                    return name != "std_ulogic" && name != "std_logic"
                        && name != "bit" && name != "boolean"
                        && name != "character";
                }
                if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias) {
                    return false;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            return false;
        }();
        // Other scalar and array values: WRITE appends the value's image
        // without character-literal quotes (IEEE 1076-2008 16.4), as
        // TO_STRING produces it.
        if (user_enumeration) {
            return fail("FSIM-ELAB-VHTEXTIO-009",
                "STD.TEXTIO declares no WRITE for a value of this "
                "enumeration type");
        }
        if (const auto image = lower_hir_vhdl_runtime_image(*value,
                std::nullopt, OutputFormat::binary, false)) {
            if (!lower_dynamic_field()) {
                return false;
            }
            append_string(*image);
            return true;
        }
        return fail("FSIM-ELAB-VHTEXTIO-009",
            "bounded TextIO write supports integer, boolean, bit, and "
            "string values");
    }

    // VHDL-2008 implicit WRITE(F, VALUE) of a STRING to a TEXT file such as
    // OUTPUT appends the characters without a line terminator (5.5.2).
    if (const auto target = actual("f", 0U), text = actual("value", 1U);
        name == "write" && target && text
        && call.procedure_arguments.size() == 2U
        && hir_expression_is_string(*text, hir_process_scope_)) {
        std::optional<RegisterId> handle;
        if (standard_output(*target)) {
            handle = allocate_register(32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                LoadConstant { *handle, unsigned_value(1U, 32U) });
        } else if (const auto text_file = file_actual(0U);
            text_file && standard_text_file(text_file->first)) {
            handle = *text_file->second.local;
        }
        if (handle) {
            const auto lowered = lower_hir_string_expression(*text);
            if (!lowered) {
                return false;
            }
            process_.operations.emplace_back(FileWriteString {
                *handle, *lowered, { }, { }, false, false });
            return true;
        }
    }
    const auto direct_file = file_actual(0U);
    const auto value = actual("value", 1U);
    const auto length = name == "read" ? actual("length", 2U) : std::nullopt;
    if (!direct_file || !value
        || (call.procedure_arguments.size() != 2U
            && !(length && call.procedure_arguments.size() == 3U))) {
        return fail("FSIM-ELAB-VHFILE-010",
            "direct VHDL file read/write requires a file object and one "
            "value");
    }
    const auto element_subtype = file_element_subtype(
        direct_file->first);
    const auto result = element_subtype
        ? lower_hir_vhdl_file_image(name == "write",
              *direct_file->second.local, *element_subtype, *value, length, span)
        : HirVhdlFileImageResult::unsupported;
    if (result == HirVhdlFileImageResult::unsupported) {
        return fail("FSIM-ELAB-VHFILE-011",
            "direct VHDL file I/O supports scalar, record and array "
            "elements with text images");
    }
    return result == HirVhdlFileImageResult::lowered;
}

std::optional<Lowerer::HirVhdlSlicePositions>
Lowerer::lower_hir_vhdl_slice_positions(const semantic::ExpressionId slice_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(slice_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::slice
        || expression->vhdl->operands.size() != 3U
        || (expression->vhdl->text != "to"
            && expression->vhdl->text != "downto")) {
        return std::nullopt;
    }
    const auto& slice = *expression->vhdl;
    const bool descending = slice.text == "downto";
    const auto prefix_subtype
        = hir_vhdl_expression_subtype(slice.operands.front());
    if (!prefix_subtype) {
        return std::nullopt;
    }
    const auto range = std::ranges::find_if(prefix_subtype->constraints,
        [](const semantic::vhdl::RangeConstraint& constraint) {
            return constraint.kind == semantic::vhdl::RangeKind::array_index;
        });
    if (range == prefix_subtype->constraints.end()
        || range->descending != descending) {
        return std::nullopt;
    }
    const auto base_left = hir_range_left(*range);
    if (!base_left) {
        return std::nullopt;
    }
    auto first = lower_hir_expression(slice.operands[1], 32U);
    auto last = lower_hir_expression(slice.operands[2], 32U);
    if (!first || !last) {
        return std::nullopt;
    }
    if (register_width(*first) != 32U) {
        first = resize_register(*first, 32U, true);
    }
    if (register_width(*last) != 32U) {
        last = resize_register(*last, 32U, true);
    }
    const auto base
        = load_integer(*base_left, 32U, frontend::ValueDomain::Integer);
    const auto position = [&](const RegisterId bound) {
        const auto result
            = allocate_register(32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::subtract, result,
            descending ? base : bound, descending ? bound : base });
        return result;
    };
    return HirVhdlSlicePositions { position(*first), position(*last) };
}

std::optional<StringRegisterId> Lowerer::lower_hir_vhdl_bit_slice_text(
    const semantic::ExpressionId slice_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(slice_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::slice
        || expression->vhdl->operands.size() != 3U) {
        return std::nullopt;
    }
    const auto prefix = expression->vhdl->operands.front();
    const auto width = hir_expression_width(prefix, hir_process_scope_);
    const auto domain = hir_expression_domain(prefix, hir_process_scope_);
    if (!width || *width == 0U || *width > maximum_string_bytes || !domain
        || *domain != frontend::ValueDomain::Bit2) {
        return std::nullopt;
    }
    const auto positions = lower_hir_vhdl_slice_positions(slice_id);
    const auto packed
        = positions ? lower_hir_expression(prefix, *width) : std::nullopt;
    if (!packed) {
        return std::nullopt;
    }
    const auto characters = allocate_string_register();
    process_.operations.emplace_back(
        LoadStringConstant { characters, std::string { } });
    process_.operations.emplace_back(
        hir_format_packed(characters, *packed, *width, OutputFormat::binary));
    StringMethod method;
    method.operation = StringMethodOperator::substr;
    method.source = characters;
    method.string_destination = allocate_string_register();
    method.first = positions->first;
    method.second = positions->last;
    process_.operations.emplace_back(method);
    return method.string_destination;
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
                    ? frontend::systemverilog_scalar_kind(
                          declaration->systemverilog->type
                              ->target.spelling)
                    : frontend::SystemVerilogScalarKind::None;
            };
        const auto scalar_format_matches = [&]
            (const semantic::ExpressionId expression,
             const semantic::sv::OutputFormat format) {
                const auto kind = expression_scalar_kind(expression);
                const auto real_kind
                    = frontend::is_real_scalar_kind(kind);
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
                        pending_prefix = std::move(prefix) + hir_scope_hierarchy();
                        return;
                    }
                    if (output.format
                        == semantic::sv::OutputFormat::library) {
                        pending_prefix = std::move(prefix) + hir_library_cell();
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
                    value.minimum_width = hir_systemverilog_decimal_width(
                        *output.value, value.format,
                        output.suppress_leading_zero || output.left_justify,
                        output.minimum_width);
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
                if (output.format == semantic::sv::OutputFormat::hierarchy
                    || output.format == semantic::sv::OutputFormat::library) {
                    process_.operations.emplace_back(FileWriteLiteral {
                        *handle,
                        output.prefix
                            + (output.format
                                    == semantic::sv::OutputFormat::library
                                ? hir_library_cell()
                                : hir_scope_hierarchy())
                            + std::move(suffix),
                        newline,
                    });
                    return true;
                }
                if (!output.value) {
                    return false;
                }
                const bool pattern
                    = output.format == semantic::sv::OutputFormat::pattern;
                // `%p` writes an unpacked value as an assignment pattern,
                // a string in quotes and an enumeration value by name
                // (IEEE 1800-2017 21.2.1.7).
                if (pattern) {
                    const auto text = lower_hir_pattern_text(*output.value);
                    if (text.failed) {
                        return false;
                    }
                    if (text.text) {
                        const auto quote = text.quoted ? "\"" : "";
                        process_.operations.emplace_back(FileWriteString {
                            *handle, *text.text, output.prefix + quote,
                            quote + std::move(suffix), newline,
                        });
                        return true;
                    }
                }
                // A string variable printed with a radix format prints its
                // text, as `$display` does.
                const auto value_record
                    = specialized_hir_unit_->find_expression(*output.value);
                const bool string_variable = value_record
                    && value_record->systemverilog != nullptr
                    && value_record->systemverilog->kind
                        != semantic::sv::ExpressionKind::string_literal;
                if ((output.format == semantic::sv::OutputFormat::string
                        || output.format
                            == semantic::sv::OutputFormat::decimal
                        || (string_variable
                            && (output.format
                                    == semantic::sv::OutputFormat::binary
                                || output.format
                                    == semantic::sv::OutputFormat::octal
                                || output.format
                                    == semantic::sv::OutputFormat::
                                        hexadecimal)))
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
                auto kind = pattern
                    ? hir_systemverilog_scalar_kind(*output.value)
                    : expression_scalar_kind(*output.value);
                const auto format = pattern
                        && (frontend::is_real_scalar_kind(kind))
                    ? runtime::simir::OutputFormat::real_general
                    : runtime_output_format(output.format);
                const bool suppress_leading_zero
                    = output.suppress_leading_zero
                    || (pattern && output.minimum_width == 0U);
                const bool real_conversion
                    = (format == runtime::simir::OutputFormat::real_scientific
                        || format == runtime::simir::OutputFormat::real_fixed
                        || format == runtime::simir::OutputFormat::real_general)
                    && kind == frontend::SystemVerilogScalarKind::None;
                if (real_conversion) {
                    kind = frontend::SystemVerilogScalarKind::Real;
                }
                auto width = hir_expression_width(
                    *output.value, hir_process_scope_).value_or(32U);
                auto value = lower_hir_expression(*output.value, width,
                    real_conversion ? frontend::SystemVerilogScalarKind::None
                                    : kind);
                if (!value
                    || width > std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                if (real_conversion) {
                    value = convert_hir_integral_to_real(*value,
                        hir_expression_signed(*output.value),
                        frontend::SystemVerilogScalarKind::Real);
                    width = 64U;
                }
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
                    suppress_leading_zero,
                    hir_systemverilog_decimal_width(*output.value, format,
                        suppress_leading_zero || output.left_justify,
                        output.minimum_width),
                    output.left_justify,
                    output.zero_pad,
                    kind,
                    output.precision,
                    format == runtime::simir::OutputFormat::time
                        ? hir_time_unit_femtoseconds()
                        : 0U,
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
                source.output_precision,
            },
            source.output_suffix,
            source.output_newline);
    }
    if (source.kind != semantic::sv::StatementKind::memory_transfer
        || !source.value || !source.target) {
        return false;
    }
    const auto path = lower_hir_filename(*source.value);
    // The memory is a module's array or a subprogram's local or formal one
    // (IEEE 1800-2017 21.4).
    const auto binding = hir_container_object_binding(*source.target);
    if (!path || !binding || binding->type == nullptr
        || (!source.memory_write && binding->read_only)
        || (!binding->local
            && static_cast<std::size_t>(binding->object)
                >= design_.container_object_info_.size())) {
        report("FSIM-ELAB-SVMEMORY-003",
            "memory transfer requires a writable fixed-array object",
            hir_source_span(source.source));
        return true;
    }
    // A dynamic array, queue or integral-indexed associative array of
    // packed elements is a memory too (IEEE 1800-2017 21.4.1, 21.5.3).
    const auto& type = *binding->type;
    if (!type.fixed
        && (type.element_kind != ContainerElementKind::Packed
            || (type.associative
                && (type.string_indices || type.index_width > 64U)))) {
        report("FSIM-ELAB-SVMEMORY-003",
            "$readmem*/$writemem* requires an array of packed elements with "
            "integral indices",
            hir_source_span(source.source));
        return true;
    }
    const auto target = binding->local
        ? *binding->local
        : allocate_container_register(type);
    if (!binding->local) {
        process_.operations.emplace_back(ReadContainerObject {
            target, binding->object });
    }
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
    if (!source.memory_write && !binding->local) {
        process_.operations.emplace_back(WriteContainerObject {
            binding->object, target, std::nullopt });
    }
    return true;
}

Lowerer::HirPatternText Lowerer::lower_hir_pattern_text(
    const semantic::ExpressionId value)
{
    if (hir_static_container_expression_type(value)) {
        const auto container = lower_hir_static_container_value(value);
        if (!container) {
            return { .failed = true, .text = std::nullopt };
        }
        const auto text = allocate_string_register();
        process_.operations.emplace_back(
            FormatContainerPattern { text, container->value });
        return { .failed = false, .text = text };
    }
    if (hir_expression_is_string(value, hir_process_scope_)) {
        const auto text = lower_hir_string_expression(value);
        if (!text) {
            return { .failed = true, .text = std::nullopt };
        }
        return { .failed = false, .text = *text, .quoted = true };
    }
    if (const auto name
        = lower_hir_systemverilog_enumeration_value_name(value)) {
        return { .failed = false, .text = *name };
    }
    return { };
}


bool Lowerer::hir_filename_operand(const semantic::ExpressionId expression,
    const semantic::ScopeId scope) const
{
    if (hir_expression_is_string(expression, scope)) {
        return true;
    }
    // An integral variable may hold the name (IEEE 1800-2017 21.3.1); a
    // numeric literal is not a filename.
    if (const auto literal = specialized_hir_unit_ != nullptr
            ? specialized_hir_unit_->find_expression(expression)
            : std::nullopt;
        literal && literal->systemverilog != nullptr
        && (literal->systemverilog->kind
                == semantic::sv::ExpressionKind::integer_literal
            || literal->systemverilog->kind
                == semantic::sv::ExpressionKind::logic_literal)) {
        return false;
    }
    const auto width = hir_expression_width(expression, scope);
    const auto domain = hir_expression_domain(expression, scope);
    return width && *width != 0U && domain
        && (*domain == frontend::ValueDomain::Bit2
            || *domain == frontend::ValueDomain::Logic4);
}

std::optional<StringRegisterId> Lowerer::lower_hir_filename(
    const semantic::ExpressionId expression)
{
    if (hir_expression_is_string(expression, hir_process_scope_)) {
        return lower_hir_string_expression(expression);
    }
    const auto width = hir_expression_width(expression, hir_process_scope_);
    if (!width || *width == 0U) {
        return std::nullopt;
    }
    const auto value = lower_hir_expression(expression, *width);
    if (!value) {
        return std::nullopt;
    }
    // The characters of an integral file name; leading zero bytes are not
    // part of the name (IEEE 1800-2017 11.10).
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant { destination, { } });
    auto operation = hir_format_packed(destination, *value,
        register_width(*value), runtime::simir::OutputFormat::string);
    operation.suppress_leading_zero = true;
    process_.operations.emplace_back(operation);
    return destination;
}

bool Lowerer::hir_text_operand(
    const semantic::ExpressionId expression,
    const semantic::ScopeId process_scope) const
{
    if (hir_expression_is_string(expression, process_scope)) {
        return true;
    }
    const auto width = hir_expression_width(expression, process_scope);
    return width && *width != 0U
        && !frontend::is_real_scalar_kind(
            hir_systemverilog_scalar_kind(expression));
}

} // namespace fsim::elaboration

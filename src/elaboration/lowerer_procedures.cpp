// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    bool writable_procedure_actual(const Expression& expression)
    {
        if (expression.kind == ExpressionKind::Identifier) {
            return true;
        }
        return (expression.kind == ExpressionKind::Index
                   && expression.operands.size() == 2)
            || (expression.kind == ExpressionKind::Slice
                && expression.operands.size() == 3);
    }

} // namespace

bool Lowerer::lower_vhdl_simulator_procedure_call(
    const Statement& statement)
{
    const auto api = frontend::vhdl_simulator_api(statement.procedure_name);
    if (api == frontend::VhdlSimulatorApi::none) {
        return false;
    }
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
        report(
            "FSIM-ELAB-VHENV-001",
            "the STD.ENV simulator API requires VHDL-2019",
            statement.span);
        return true;
    }
    if (api == frontend::VhdlSimulatorApi::set_psl_cover_assert) {
        if (statement.procedure_arguments.size() > 1U
            || std::ranges::any_of(
                statement.procedure_arguments,
                [](const auto& association) {
                    return association.formal
                        && *association.formal != "enable";
                })) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETPSLCOVERASSERT accepts at most one BOOLEAN ENABLE actual",
                statement.span);
            return true;
        }
        std::optional<RegisterId> enable;
        if (statement.procedure_arguments.empty()) {
            enable = allocate_register(1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LoadConstant {
                *enable, PackedLogic4::from_aval_bval(1U, 1U, 0U) });
        } else {
            const auto& actual = statement.procedure_arguments.front().value;
            enable = lower_expression(actual, 1U);
            if (!enable) {
                return true;
            }
            if (register_width(*enable) != 1U
                || register_domain(*enable)
                    != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV SETPSLCOVERASSERT ENABLE must be BOOLEAN",
                    actual.span);
                return true;
            }
        }
        process_.operations.emplace_back(VhdlPslApi {
            VhdlPslApiKind::set_cover_assert, std::nullopt, enable });
        return true;
    }
    if (api == frontend::VhdlSimulatorApi::clear_psl_state) {
        if (!statement.procedure_arguments.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV CLEARPSLSTATE does not accept arguments",
                statement.span);
            return true;
        }
        process_.operations.emplace_back(VhdlPslApi {
            VhdlPslApiKind::clear_state, std::nullopt, std::nullopt });
        return true;
    }
    const auto assert_source = SourceLocation {
        statement.span.source_name.str(),
        static_cast<std::uint32_t>(statement.span.begin.line),
        static_cast<std::uint32_t>(statement.span.begin.column) };
    const auto severity_type = [] {
        frontend::Type type;
        type.spelling = "severity_level";
        type.domain = frontend::ValueDomain::Bit2;
        type.packed_range = frontend::PackedRange { 1, 0, true };
        type.enumeration_literals = {
            "note", "warning", "error", "failure" };
        return type;
    }();
    const auto boolean_type = [] {
        frontend::Type type;
        type.spelling = "boolean";
        type.domain = frontend::ValueDomain::Boolean;
        type.packed_range = frontend::PackedRange { 0, 0, true };
        type.nominal_type = "@builtin:boolean";
        type.vhdl_type_declaration = type.nominal_type;
        return type;
    }();
    const auto constant = [&](const std::size_t width,
                              const frontend::ValueDomain domain,
                              const std::uint64_t value) {
        const auto result = allocate_register(width, domain);
        process_.operations.emplace_back(LoadConstant {
            result, PackedLogic4::from_aval_bval(width, value, 0U) });
        return result;
    };
    if (api == frontend::VhdlSimulatorApi::clear_vhdl_assert) {
        if (!statement.procedure_arguments.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV CLEARVHDLASSERT does not accept arguments",
                statement.span);
            return true;
        }
        process_.operations.emplace_back(VhdlAssertApi {
            VhdlAssertApiKind::clear, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            assert_source });
        return true;
    }
    if (api == frontend::VhdlSimulatorApi::set_vhdl_assert_enable) {
        if (statement.procedure_arguments.size() > 2U) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETVHDLASSERTENABLE has no matching standardized association profile",
                statement.span);
            return true;
        }
        const Expression* level_actual { };
        const Expression* enable_actual { };
        std::size_t positional { };
        bool valid { true };
        for (const auto& association : statement.procedure_arguments) {
            if (association.formal) {
                if (*association.formal == "level" && level_actual == nullptr) {
                    level_actual = &association.value;
                } else if (*association.formal == "enable"
                    && enable_actual == nullptr) {
                    enable_actual = &association.value;
                } else {
                    valid = false;
                }
                continue;
            }
            if (statement.procedure_arguments.size() == 1U) {
                if (vhdl_expression_matches_type(
                        association.value, severity_type)) {
                    level_actual = &association.value;
                } else {
                    enable_actual = &association.value;
                }
            } else if (positional++ == 0U && level_actual == nullptr) {
                level_actual = &association.value;
            } else if (enable_actual == nullptr) {
                enable_actual = &association.value;
            } else {
                valid = false;
            }
        }
        if (!valid) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETVHDLASSERTENABLE has no matching standardized association profile",
                statement.span);
            return true;
        }
        std::optional<RegisterId> level;
        if (level_actual != nullptr) {
            level = lower_expression(*level_actual, 2U, &severity_type);
            if (!level || register_width(*level) != 2U) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV SETVHDLASSERTENABLE LEVEL must be SEVERITY_LEVEL",
                    level_actual->span);
                return true;
            }
        }
        std::optional<RegisterId> enable;
        if (enable_actual != nullptr) {
            enable = lower_expression(*enable_actual, 1U, &boolean_type);
            if (!enable || register_width(*enable) != 1U
                || register_domain(*enable)
                    != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV SETVHDLASSERTENABLE ENABLE must be BOOLEAN",
                    enable_actual->span);
                return true;
            }
        } else {
            enable = constant(1U, frontend::ValueDomain::Boolean, 1U);
        }
        process_.operations.emplace_back(VhdlAssertApi {
            VhdlAssertApiKind::set_enable, std::nullopt, std::nullopt,
            level, enable, std::nullopt, std::nullopt, assert_source });
        return true;
    }
    if (api == frontend::VhdlSimulatorApi::set_vhdl_assert_format) {
        const auto count = statement.procedure_arguments.size();
        std::array<const Expression*, 3> actuals { nullptr, nullptr, nullptr };
        static constexpr std::array names {
            std::string_view { "level" }, std::string_view { "format" },
            std::string_view { "valid" } };
        std::size_t positional { };
        bool valid = count == 2U || count == 3U;
        for (const auto& association : statement.procedure_arguments) {
            std::size_t formal = positional;
            if (!association.formal) {
                ++positional;
            } else {
                const auto found = std::ranges::find(names, *association.formal);
                if (found == names.end()) {
                    valid = false;
                    continue;
                }
                formal = static_cast<std::size_t>(
                    std::distance(names.begin(), found));
            }
            if (formal >= actuals.size() || actuals[formal] != nullptr) {
                valid = false;
            } else {
                actuals[formal] = &association.value;
            }
        }
        valid = valid && actuals[0] != nullptr && actuals[1] != nullptr
            && (count == 2U ? actuals[2] == nullptr : actuals[2] != nullptr);
        if (!valid) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETVHDLASSERTFORMAT has no matching standardized association profile",
                statement.span);
            return true;
        }
        const auto level = lower_expression(*actuals[0], 2U, &severity_type);
        const auto format = lower_string_expression(*actuals[1]);
        if (!level || register_width(*level) != 2U) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV SETVHDLASSERTFORMAT LEVEL must be SEVERITY_LEVEL",
                actuals[0]->span);
            return true;
        }
        if (!format) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV SETVHDLASSERTFORMAT FORMAT must be STRING-compatible",
                actuals[1]->span);
            return true;
        }
        std::optional<RegisterId> format_valid;
        if (actuals[2] != nullptr) {
            const auto local = actuals[2]->kind == ExpressionKind::Identifier
                ? locals_.find(actuals[2]->text) : locals_.end();
            const auto* type = actuals[2]->kind == ExpressionKind::Identifier
                ? object_type(actuals[2]->text) : nullptr;
            if (local == locals_.end() || type == nullptr
                || type->domain != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV SETVHDLASSERTFORMAT VALID must be a writable BOOLEAN variable",
                    actuals[2]->span);
                return true;
            }
            format_valid = local->second;
        }
        process_.operations.emplace_back(VhdlAssertApi {
            VhdlAssertApiKind::set_format, std::nullopt, std::nullopt,
            *level, std::nullopt, *format, format_valid, assert_source });
        return true;
    }
    if (api == frontend::VhdlSimulatorApi::set_vhdl_read_severity) {
        if (statement.procedure_arguments.size() > 1U
            || std::ranges::any_of(statement.procedure_arguments,
                [](const auto& association) {
                    return association.formal
                        && *association.formal != "level";
                })) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETVHDLREADSEVERITY accepts at most one SEVERITY_LEVEL actual",
                statement.span);
            return true;
        }
        auto level = constant(
            2U, frontend::ValueDomain::Bit2,
            static_cast<std::uint64_t>(AssertionSeverity::failure));
        if (!statement.procedure_arguments.empty()) {
            const auto& actual = statement.procedure_arguments.front().value;
            const auto lowered = lower_expression(actual, 2U, &severity_type);
            if (!lowered || register_width(*lowered) != 2U) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV SETVHDLREADSEVERITY LEVEL must be SEVERITY_LEVEL",
                    actual.span);
                return true;
            }
            level = *lowered;
        }
        process_.operations.emplace_back(VhdlAssertApi {
            VhdlAssertApiKind::set_read_severity, std::nullopt,
            std::nullopt, level, std::nullopt, std::nullopt,
            std::nullopt, assert_source });
        return true;
    }
    const bool directory_procedure = api
            == frontend::VhdlSimulatorApi::dir_open
        || api == frontend::VhdlSimulatorApi::dir_close
        || api == frontend::VhdlSimulatorApi::dir_workingdir
        || api == frontend::VhdlSimulatorApi::dir_createdir
        || api == frontend::VhdlSimulatorApi::dir_deletedir
        || api == frontend::VhdlSimulatorApi::dir_deletefile;
    if (directory_procedure) {
        std::vector<std::string_view> formals;
        if (api == frontend::VhdlSimulatorApi::dir_open) {
            formals = { "dir", "path", "status" };
        } else if (api == frontend::VhdlSimulatorApi::dir_close) {
            formals = { "dir" };
        } else if (api == frontend::VhdlSimulatorApi::dir_createdir) {
            formals = statement.procedure_arguments.size() == 3U
                ? std::vector<std::string_view> { "path", "parents", "status" }
                : std::vector<std::string_view> { "path", "status" };
        } else if (api == frontend::VhdlSimulatorApi::dir_deletedir) {
            formals = statement.procedure_arguments.size() == 3U
                ? std::vector<std::string_view> { "path", "recursive", "status" }
                : std::vector<std::string_view> { "path", "status" };
        } else {
            formals = { "path", "status" };
        }
        bool valid = statement.procedure_arguments.size() == formals.size();
        std::vector<const Expression*> actuals(formals.size(), nullptr);
        std::size_t positional = 0U;
        for (const auto& association : statement.procedure_arguments) {
            std::size_t formal = positional;
            if (!association.formal) {
                ++positional;
            } else {
                const auto found = std::ranges::find(
                    formals, *association.formal);
                if (found == formals.end()) {
                    valid = false;
                    continue;
                }
                formal = static_cast<std::size_t>(
                    std::distance(formals.begin(), found));
            }
            if (formal >= actuals.size() || actuals[formal] != nullptr) {
                valid = false;
            } else {
                actuals[formal] = &association.value;
            }
        }
        valid = valid && std::ranges::all_of(
            actuals, [](const Expression* value) { return value != nullptr; });
        if (!valid) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV directory procedure has no matching standardized association profile",
                statement.span);
            return true;
        }
        std::optional<ContainerRegisterId> directory;
        std::optional<StringRegisterId> path;
        std::optional<RegisterId> option;
        std::optional<RegisterId> status;
        std::size_t next = 0U;
        if (api == frontend::VhdlSimulatorApi::dir_open
            || api == frontend::VhdlSimulatorApi::dir_close) {
            const auto& value = *actuals[next++];
            const auto found = value.kind == ExpressionKind::Identifier
                ? container_locals_.find(value.text) : container_locals_.end();
            const auto* type = value.kind == ExpressionKind::Identifier
                ? object_type(value.text) : nullptr;
            if (found == container_locals_.end() || type == nullptr
                || !frontend::is_vhdl_environment_directory(*type)) {
                report(
                    "FSIM-ELAB-VHENV-006",
                    "STD.ENV directory DIR must be a DIRECTORY variable",
                    value.span);
                return true;
            }
            directory = found->second;
        }
        if (api != frontend::VhdlSimulatorApi::dir_close) {
            path = lower_string_expression(*actuals[next++]);
            if (!path) {
                report(
                    "FSIM-ELAB-VHENV-006",
                    "STD.ENV directory PATH must be STRING-compatible",
                    actuals[next - 1U]->span);
                return true;
            }
        }
        if ((api == frontend::VhdlSimulatorApi::dir_createdir
                || api == frontend::VhdlSimulatorApi::dir_deletedir)
            && actuals.size() == 3U) {
            option = lower_expression(*actuals[next++], 1U);
            if (!option) {
                return true;
            }
        }
        if (api != frontend::VhdlSimulatorApi::dir_close) {
            const auto& value = *actuals[next];
            const auto found = value.kind == ExpressionKind::Identifier
                ? locals_.find(value.text) : locals_.end();
            const auto* type = value.kind == ExpressionKind::Identifier
                ? object_type(value.text) : nullptr;
            const auto status_kind = api
                    == frontend::VhdlSimulatorApi::dir_createdir
                ? frontend::VhdlSimulatorApi::dir_create_status
                : api == frontend::VhdlSimulatorApi::dir_deletedir
                ? frontend::VhdlSimulatorApi::dir_delete_status
                : api == frontend::VhdlSimulatorApi::dir_deletefile
                ? frontend::VhdlSimulatorApi::file_delete_status
                : frontend::VhdlSimulatorApi::dir_open_status;
            const auto expected =
                frontend::vhdl_environment_directory_status_type(status_kind);
            if (found == locals_.end() || type == nullptr
                || type->nominal_type != expected.nominal_type
                || register_width(found->second) != 3U) {
                report(
                    "FSIM-ELAB-VHENV-006",
                    "STD.ENV directory STATUS must be a writable matching status variable",
                    value.span);
                return true;
            }
            status = found->second;
        }
        using Kind = VhdlEnvironmentDirectoryKind;
        const auto kind = api == frontend::VhdlSimulatorApi::dir_open
            ? Kind::open
            : api == frontend::VhdlSimulatorApi::dir_close
            ? Kind::close
            : api == frontend::VhdlSimulatorApi::dir_workingdir
            ? Kind::set_working_directory
            : api == frontend::VhdlSimulatorApi::dir_createdir
            ? Kind::create_directory
            : api == frontend::VhdlSimulatorApi::dir_deletedir
            ? Kind::delete_directory : Kind::delete_file;
        process_.operations.emplace_back(VhdlEnvironmentDirectory {
            kind, status, std::nullopt, directory, path, option });
        return true;
    }
    if (api == frontend::VhdlSimulatorApi::resolution_limit) {
        report(
            "FSIM-ELAB-VHENV-002",
            "STD.ENV RESOLUTION_LIMIT is a function and cannot be called as a procedure",
            statement.span);
        return true;
    }
    if (api != frontend::VhdlSimulatorApi::stop
        && api != frontend::VhdlSimulatorApi::finish) {
        report(
            "FSIM-ELAB-VHENV-002",
            "STD.ENV functions, constants, and types cannot be called as procedures",
            statement.span);
        return true;
    }
    if (statement.procedure_arguments.size() > 1U
        || std::ranges::any_of(
            statement.procedure_arguments,
            [](const auto& association) {
                return association.formal
                    && *association.formal != "status";
            })) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV STOP and FINISH accept at most one INTEGER STATUS actual",
            statement.span);
        return true;
    }
    std::optional<RegisterId> status;
    if (!statement.procedure_arguments.empty()) {
        const auto& actual = statement.procedure_arguments.front().value;
        if (!is_integer_expression(actual)) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV simulator status must be an INTEGER expression",
                actual.span);
            return true;
        }
        const auto width = static_cast<std::size_t>(
            frontend::vhdl_predefined_integer_storage_width(vhdl_standard_));
        status = lower_expression(actual, width);
        if (!status) {
            return true;
        }
        if (register_width(*status) != width) {
            *status = resize_register(*status, width, true);
        }
    }
    if (api == frontend::VhdlSimulatorApi::stop) {
        process_.operations.emplace_back(Pause { status });
    } else {
        process_.operations.emplace_back(Stop { status });
    }
    return true;
}

bool Lowerer::lower_vhdl_file_procedure_call(
    const Statement& statement)
{
    const bool open = statement.procedure_name == "file_open";
    const bool close = statement.procedure_name == "file_close";
    const bool read = statement.procedure_name == "read";
    const bool write = statement.procedure_name == "write";
    if (!open && !close && !read && !write) {
        return false;
    }
    const auto positional = [&] {
        std::vector<const Expression*> result;
        for (const auto& argument : statement.procedure_arguments) {
            if (!argument.formal) {
                result.push_back(&argument.value);
            }
        }
        return result;
    }();
    const auto actual = [&](const std::string_view formal,
                            const std::size_t index)
        -> const Expression* {
        const auto named = std::ranges::find_if(
            statement.procedure_arguments,
            [&](const auto& argument) {
                return argument.formal && *argument.formal == formal;
            });
        if (named != statement.procedure_arguments.end()) {
            return &named->value;
        }
        return index < positional.size() ? positional[index] : nullptr;
    };
    bool status_form = false;
    if (open) {
        status_form = statement.procedure_arguments.size() == 4
            || std::ranges::any_of(
                statement.procedure_arguments,
                [](const auto& argument) {
                    return argument.formal
                        && *argument.formal == "status";
                });
        if (!status_form
            && statement.procedure_arguments.size() == 3
            && !positional.empty()) {
            const auto* first_type = positional.front()->kind
                    == ExpressionKind::Identifier
                ? object_type(positional.front()->text)
                : nullptr;
            status_form = first_type != nullptr
                && !first_type->vhdl_file;
        }
    }
    const auto* file = actual(
        "f", status_form ? 1U : 0U);
    if ((read || write)
        && (file == nullptr
            || file->kind != ExpressionKind::Identifier
            || object_type(file->text) == nullptr
            || !object_type(file->text)->vhdl_file)) {
        return false;
    }
    if (file == nullptr
        || file->kind != ExpressionKind::Identifier) {
        report(
            "FSIM-ELAB-VHFILE-005",
            "VHDL file operation requires a whole file object",
            file == nullptr ? statement.span : file->span);
        return true;
    }
    const auto local = locals_.find(file->text);
    const auto* type = object_type(file->text);
    if (local == locals_.end() || type == nullptr
        || !type->vhdl_file) {
        report(
            "FSIM-ELAB-VHFILE-005",
            "unknown VHDL file object '" + file->text + "'",
            file->span);
        return true;
    }
    if (close) {
        if (statement.procedure_arguments.size() != 1) {
            report(
                "FSIM-ELAB-VHFILE-006",
                "file_close requires exactly one file object",
                statement.span);
            return true;
        }
        process_.operations.emplace_back(
            FileClose { local->second, true, false });
        return true;
    }
    if (read || write) {
        const auto* value = actual("value", 1);
        const auto& element = type->vhdl_file->element_types.front();
        if (statement.procedure_arguments.size() != 2
            || value == nullptr) {
            report(
                "FSIM-ELAB-VHFILE-010",
                "direct VHDL file read/write requires a file object "
                "and one value",
                statement.span);
            return true;
        }
        if (element.domain != frontend::ValueDomain::Integer) {
            report(
                "FSIM-ELAB-VHFILE-011",
                "bounded direct VHDL file I/O requires an integer "
                "element subtype",
                statement.span);
            return true;
        }
        if (write) {
            auto source = lower_expression(*value, 32, &element);
            if (!source) {
                return true;
            }
            FileWriteFormatted operation;
            operation.handle = local->second;
            operation.source = *source;
            operation.width = 32;
            operation.format = OutputFormat::decimal;
            operation.newline = true;
            operation.signed_decimal = true;
            process_.operations.emplace_back(std::move(operation));
            return true;
        }
        const auto target = value->kind == ExpressionKind::Identifier
            ? locals_.find(value->text)
            : locals_.end();
        const auto* target_type = value->kind == ExpressionKind::Identifier
            ? object_type(value->text)
            : nullptr;
        if (target == locals_.end() || target_type == nullptr
            || target_type->domain != frontend::ValueDomain::Integer) {
            report(
                "FSIM-ELAB-VHFILE-012",
                "direct VHDL file read target must be a writable "
                "integer variable",
                value->span);
            return true;
        }
        InputScanConversion conversion;
        conversion.format = InputScanFormat::decimal;
        conversion.target = InputScanTarget {
            InputScanTargetKind::packed_register,
            target->second,
            32,
            true
        };
        const auto count = allocate_register(
            32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(FileScan {
            count,
            local->second,
            0,
            false,
            { std::move(conversion) },
            { },
            true });
        return true;
    }
    const auto* path = actual(
        "external_name", status_form ? 2U : 1U);
    const auto* kind = actual(
        "open_kind", status_form ? 3U : 2U);
    const auto minimum = status_form ? 3U : 2U;
    const auto maximum = status_form ? 4U : 3U;
    if (statement.procedure_arguments.size() < minimum
        || statement.procedure_arguments.size() > maximum
        || path == nullptr || !is_string_expression(*path)) {
        report(
            "FSIM-ELAB-VHFILE-007",
            "file_open requires a file object, string logical name, "
            "and optional file_open_kind",
            statement.span);
        return true;
    }
    const auto kind_name = kind == nullptr
        ? std::string_view { "read_mode" }
        : std::string_view { kind->text };
    const auto mode_text = kind_name == "read_mode" ? std::string_view { "r" }
        : kind_name == "write_mode"                 ? std::string_view { "w" }
        : kind_name == "append_mode"                ? std::string_view { "a" }
                                                    : std::string_view { };
    if (mode_text.empty()) {
        report(
            "FSIM-ELAB-VHFILE-004",
            "VHDL file open kind must be read_mode, write_mode, "
            "or append_mode",
            kind->span);
        return true;
    }
    std::optional<RegisterId> status;
    if (status_form) {
        const auto* target = actual("status", 0);
        const auto target_local = target != nullptr
                && target->kind == ExpressionKind::Identifier
            ? locals_.find(target->text)
            : locals_.end();
        const auto* target_type = target != nullptr
                && target->kind == ExpressionKind::Identifier
            ? object_type(target->text)
            : nullptr;
        if (target == nullptr || target_local == locals_.end()
            || target_type == nullptr
            || target_type->enumeration_literals
                != std::vector<std::string> {
                    "open_ok", "status_error", "name_error", "mode_error" }) {
            report(
                "FSIM-ELAB-VHFILE-008",
                "file_open status actual must be a writable "
                "file_open_status object",
                target == nullptr ? statement.span : target->span);
            return true;
        }
        status = target_local->second;
    }
    const auto path_register = lower_string_expression(*path);
    const auto mode_register = allocate_string_register();
    process_.operations.emplace_back(
        LoadStringConstant { mode_register, std::string { mode_text } });
    if (path_register) {
        process_.operations.emplace_back(FileOpen {
            local->second, *path_register, mode_register, status, true });
    }
    return true;
}

void Lowerer::initialize_procedure_support()
{
    vhdl_procedure_specializations_.clear();
    procedure_frames_.clear();
    procedure_indices_.clear();
    vhdl_procedure_specialization_indices_.clear();
    pending_procedures_.clear();
    procedure_dependencies_.clear();
    procedure_suspending_.clear();
    process_procedure_dependencies_.clear();
    function_procedure_dependencies_.clear();
    function_procedure_dependencies_.resize(
        function_frames_.size());
    active_procedure_.reset();
    procedure_return_jumps_.clear();
    procedure_call_stack_ = { };
    procedure_support_initialized_ = false;
    if (procedures_.empty()) {
        return;
    }
    if (procedures_.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-VHPROC-019",
            "too many visible VHDL procedures for the SimIR call stack",
            procedures_.front().span);
        return;
    }

    procedure_frames_.reserve(procedures_.size());
    for (const auto& procedure : procedures_) {
        auto& overloads = procedure_indices_[procedure.name];
        const bool duplicate_profile = std::ranges::any_of(
            overloads,
            [&](const std::size_t candidate_index) {
                const auto& candidate = *procedure_frames_[candidate_index].source;
                const bool imported_distinct_declarations = !candidate.visibility_owner.empty()
                    && !procedure.visibility_owner.empty()
                    && candidate.visibility_owner
                        != procedure.visibility_owner;
                const bool mapped_distinct_declarations =
                    !candidate.specialization_identity.empty()
                    && !procedure.specialization_identity.empty()
                    && candidate.specialization_identity
                        != procedure.specialization_identity;
                if (candidate.arguments.size()
                        != procedure.arguments.size()
                    || imported_distinct_declarations
                    || mapped_distinct_declarations) {
                    return false;
                }
                for (std::size_t argument = 0;
                    argument < procedure.arguments.size();
                    ++argument) {
                    const auto& left = candidate.arguments[argument];
                    const auto& right = procedure.arguments[argument];
                    if (!frontend::vhdl_parameter_type_profiles_match(
                            left.type, right.type)) {
                        return false;
                    }
                }
                return true;
            });
        if (duplicate_profile) {
            report(
                "FSIM-ELAB-VHOVER-006",
                "duplicate VHDL procedure profile '"
                    + procedure.name + "'",
                procedure.span);
            continue;
        }
        const auto index = procedure_frames_.size();
        ProcedureFrame frame;
        frame.source = &procedure;
        frame.invocation_identity = next_callable_invocation_identity_++;
        procedure_frames_.push_back(std::move(frame));
        overloads.push_back(index);
    }
    for (auto& [name, overloads] : procedure_indices_) {
        (void)name;
        std::ranges::stable_sort(
            overloads,
            [&](const std::size_t left, const std::size_t right) {
                const auto& lhs = *procedure_frames_[left].source;
                const auto& rhs = *procedure_frames_[right].source;
                return std::tie(
                           lhs.visibility_owner,
                           lhs.specialization_identity,
                           lhs.span.source_name.str(),
                           lhs.span.begin.offset,
                           lhs.span.end.offset)
                    < std::tie(
                           rhs.visibility_owner,
                           rhs.specialization_identity,
                           rhs.span.source_name.str(),
                           rhs.span.begin.offset,
                           rhs.span.end.offset);
            });
    }
    procedure_dependencies_.resize(procedure_frames_.size());
    procedure_suspending_.reserve(procedure_frames_.size());
    for (const auto& frame : procedure_frames_) {
        procedure_suspending_.push_back(
            contains_explicit_wait(frame.source->statements));
    }
    if (procedure_frames_.empty()) {
        return;
    }
    for (const auto& frame : procedure_frames_) {
        const auto& procedure = *frame.source;
        for (const auto& argument : procedure.arguments) {
            if (argument.default_value
                && !vhdl_expression_matches_type(
                    *argument.default_value, argument.type)) {
                report(
                    "FSIM-ELAB-VHLEGAL-008",
                    "default for VHDL procedure formal '"
                        + argument.name
                        + "' does not match its subtype",
                    argument.default_value->span);
            }
        }
    }

    procedure_call_stack_ = { };
    procedure_support_initialized_ = true;
}

void Lowerer::lower_procedure_call(const Statement& statement)
{
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_simulator_procedure_call(statement)) {
        return;
    }
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_vital_delay_call(statement)) {
        return;
    }
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_vital_state_table_call(statement)) {
        return;
    }
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_vital_procedure_call(statement)) {
        return;
    }
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_textio_procedure_call(statement)) {
        return;
    }
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_file_procedure_call(statement)) {
        return;
    }
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_protected_procedure_call(statement)) {
        return;
    }
    if (language_ == frontend::Language::Vhdl2008
        && lower_vhdl_access_deallocation(statement)) {
        return;
    }
    if (!procedure_support_initialized_) {
        report(
            "FSIM-ELAB-VHPROC-014",
            "unknown VHDL procedure '" + statement.procedure_name + "'",
            statement.span);
        return;
    }
    const auto selected = select_procedure_overload(statement);
    if (!selected.named) {
        report(
            "FSIM-ELAB-VHPROC-014",
            "unknown VHDL procedure '" + statement.procedure_name + "'",
            statement.span);
        return;
    }
    if (!selected.index) {
        return;
    }
    auto procedure_index = *selected.index;
    const auto& declared_procedure =
        *procedure_frames_[procedure_index].source;

    std::vector<const frontend::Expression*> actuals(
        declared_procedure.arguments.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    for (const auto& association :
        statement.procedure_arguments) {
        std::optional<std::size_t> index;
        if (association.formal) {
            saw_named = true;
            const auto formal = std::ranges::find_if(
                declared_procedure.arguments,
                [&](const auto& candidate) {
                    return candidate.name == *association.formal;
                });
            if (formal == declared_procedure.arguments.end()) {
                report(
                    "FSIM-ELAB-VHPROC-015",
                    "procedure '" + declared_procedure.name
                        + "' has no formal parameter '"
                        + *association.formal + "'",
                    association.span);
                continue;
            }
            index = static_cast<std::size_t>(
                std::distance(
                    declared_procedure.arguments.begin(), formal));
        } else {
            if (saw_named) {
                report(
                    "FSIM-ELAB-VHPROC-016",
                    "a positional procedure actual cannot follow a "
                    "named actual",
                    association.span);
            }
            if (next_positional >= declared_procedure.arguments.size()) {
                report(
                    "FSIM-ELAB-VHPROC-017",
                    "too many actual parameters for procedure '"
                        + declared_procedure.name + "'",
                    association.span);
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index] != nullptr) {
            report(
                "FSIM-ELAB-VHPROC-015",
                "duplicate actual for procedure formal '"
                    + declared_procedure.arguments[*index].name + "'",
                association.span);
            continue;
        }
        actuals[*index] = &association.value;
    }
    for (std::size_t index = 0;
        index < actuals.size(); ++index) {
        if (actuals[index] == nullptr) {
            if (declared_procedure.arguments[index].default_value) {
                actuals[index] =
                    &*declared_procedure.arguments[index].default_value;
            } else {
                report(
                    "FSIM-ELAB-VHPROC-017",
                    "procedure '" + declared_procedure.name
                        + "' requires an actual for formal '"
                        + declared_procedure.arguments[index].name + "'",
                    statement.span);
            }
        }
    }
    if (std::ranges::any_of(
            actuals,
            [](const auto* actual) {
                return actual == nullptr;
            })) {
        return;
    }

    if (std::ranges::any_of(
            declared_procedure.arguments,
            [](const auto& argument) {
                return argument.type.vhdl_unspecified != nullptr;
            })) {
        auto specialized = declared_procedure;
        std::string specialization_identity =
            std::to_string(procedure_index);
        for (std::size_t index = 0;
             index < specialized.arguments.size(); ++index) {
            auto& argument = specialized.arguments[index];
            if (!argument.type.vhdl_unspecified) {
                continue;
            }
            const auto actual_type =
                vhdl_expression_type(*actuals[index]);
            if (!actual_type) {
                report(
                    "FSIM-ELAB-VHUNSPEC-001",
                    "procedure call does not determine one unique legal "
                    "actual type for unspecified formal '" + argument.name
                        + "'",
                    actuals[index]->span);
                return;
            }
            argument.type = *actual_type;
            specialization_identity += "|" +
                frontend::vhdl_inferred_type_identity(*actual_type);
        }
        if (const auto found =
                vhdl_procedure_specialization_indices_.find(
                    specialization_identity);
            found != vhdl_procedure_specialization_indices_.end()) {
            procedure_index = found->second;
        } else {
            specialized.specialization_identity = specialization_identity;
            vhdl_procedure_specializations_.push_back(
                std::move(specialized));
            ProcedureFrame specialized_frame;
            specialized_frame.source =
                &vhdl_procedure_specializations_.back();
            specialized_frame.invocation_identity =
                next_callable_invocation_identity_++;
            procedure_index = procedure_frames_.size();
            procedure_frames_.push_back(std::move(specialized_frame));
            procedure_dependencies_.emplace_back();
            procedure_suspending_.push_back(contains_explicit_wait(
                vhdl_procedure_specializations_.back().statements));
            vhdl_procedure_specialization_indices_.emplace(
                std::move(specialization_identity), procedure_index);
        }
    }

    auto& frame = procedure_frames_[procedure_index];
    const auto& procedure = *frame.source;

    if (!frame.allocated) {
        frame.arguments.reserve(procedure.arguments.size());
        for (const auto& argument : procedure.arguments) {
            const auto width = argument.type.width();
            if (!width || *width == 0) {
                report(
                    "FSIM-ELAB-VHPROC-020",
                    "procedure argument '" + argument.name
                        + "' must have a concrete nonempty executable width",
                    argument.span);
                return;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
        }
        frame.invocation_packed = frame.arguments;
        frame.allocated = true;
    }

    std::vector<Expression> copy_out_targets(
        procedure.arguments.size());
    std::vector<RegisterId> actual_values(
        procedure.arguments.size());
    for (std::size_t index = 0;
        index < procedure.arguments.size(); ++index) {
        const auto& formal = procedure.arguments[index];
        const auto& actual = *actuals[index];
        const auto width = static_cast<std::size_t>(*formal.type.width());
        const bool writable = writable_procedure_actual(actual);
        if ((formal.object_class
                    == frontend::InterfaceObjectClass::Variable
                || formal.object_class
                    == frontend::InterfaceObjectClass::File
                || formal.direction
                    != frontend::PortDirection::Input)
            && !writable) {
            report(
                "FSIM-ELAB-VHPROC-018",
                "variable-class, output, and inout procedure formal '"
                    + formal.name
                    + "' requires a writable signal or variable actual",
                actual.span);
            return;
        }
        if (formal.direction != frontend::PortDirection::Input
            || formal.object_class
                == frontend::InterfaceObjectClass::File) {
            auto target = capture_callable_copy_out_target(
                actual,
                "@procedure_target_" + std::to_string(procedure_index)
                    + "_" + std::to_string(index)
                    + "_" + std::to_string(process_.operations.size()));
            if (!target) {
                return;
            }
            copy_out_targets[index] = std::move(*target);
        }
        actual_values[index] = allocate_register(
            width, formal.type.domain);
        if (formal.direction == frontend::PortDirection::Output) {
            process_.operations.emplace_back(LoadConstant {
                actual_values[index],
                default_packed_value(formal.type, width) });
            continue;
        }
        auto value = lower_expression(
            actual, width, &formal.type);
        if (!value) {
            return;
        }
        if (register_width(*value) != width) {
            *value = resize_register(
                *value, width, is_signed_expression(actual));
        }
        process_.operations.emplace_back(
            CopyRegister { actual_values[index], *value });
    }

    const auto push_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(CallableFramePush {
        frame.invocation_identity,
        frame.invocation_packed,
        { },
        { },
        true });
    if (!frame.invocation_layout_finalized) {
        frame.invocation_push_sites.push_back(push_site);
    }
    for (std::size_t index = 0;
        index < procedure.arguments.size(); ++index) {
        process_.operations.emplace_back(CopyRegister {
            frame.arguments[index], actual_values[index] });
    }

    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        procedure_call_stack_ });
    if (frame.target) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target = *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_procedures_.push_back(procedure_index);
    }
    if (active_procedure_) {
        procedure_dependencies_[*active_procedure_].insert(
            procedure_index);
    } else if (active_function_) {
        function_procedure_dependencies_[*active_function_].insert(
            procedure_index);
    } else {
        process_procedure_dependencies_.insert(procedure_index);
    }

    std::vector<RegisterId> preserved_values;
    for (std::size_t index = 0;
        index < procedure.arguments.size(); ++index) {
        const auto& formal = procedure.arguments[index];
        if (formal.direction == frontend::PortDirection::Input
            && formal.object_class
                != frontend::InterfaceObjectClass::File) {
            continue;
        }
        process_.operations.emplace_back(CopyRegister {
            actual_values[index], frame.arguments[index] });
        preserved_values.push_back(actual_values[index]);
    }
    process_.operations.emplace_back(CallableFramePop {
        frame.invocation_identity,
        preserved_values,
        { },
        { } });

    for (std::size_t index = 0;
        index < procedure.arguments.size(); ++index) {
        const auto& formal = procedure.arguments[index];
        if (formal.direction == frontend::PortDirection::Input
            && formal.object_class
                != frontend::InterfaceObjectClass::File) {
            continue;
        }
        if (formal.object_class
            == frontend::InterfaceObjectClass::File) {
            const auto actual_local = copy_out_targets[index].kind
                    == ExpressionKind::Identifier
                ? locals_.find(copy_out_targets[index].text)
                : locals_.end();
            if (actual_local != locals_.end()) {
                process_.operations.emplace_back(CopyRegister {
                    actual_local->second, actual_values[index] });
            }
            continue;
        }
        const auto temporary = "@procedure_copyout_" + std::to_string(procedure_index)
            + "_" + std::to_string(index);
        locals_.insert_or_assign(
            temporary, actual_values[index]);
        local_signed_.insert_or_assign(
            temporary, formal.type.is_signed);
        local_ranges_.insert_or_assign(
            temporary, formal.type.packed_range);
        local_integer_ranges_.insert_or_assign(
            temporary, formal.type.integer_range);
        local_members_.insert_or_assign(
            temporary, formal.type.packed_members);
        local_types_.insert_or_assign(temporary, &formal.type);

        Statement copy_out;
        copy_out.kind = StatementKind::Assignment;
        copy_out.assignment_kind = AssignmentKind::Blocking;
        copy_out.target = copy_out_targets[index];
        copy_out.value = Expression {
            ExpressionKind::Identifier,
            temporary,
            { },
            statement.span
        };
        copy_out.span = statement.span;
        lower_assignment(copy_out);
    }
}

void Lowerer::lower_procedure_return(
    const Statement& statement)
{
    if (!active_procedure_) {
        report(
            "FSIM-ELAB-VHPROC-021",
            "procedure return statement is outside an executable "
            "procedure",
            statement.span);
        return;
    }
    if (statement.value.valid()) {
        report(
            "FSIM-ELAB-VHPROC-021",
            "VHDL procedure return cannot carry a value",
            statement.span);
    }
    procedure_return_jumps_.push_back(
        static_cast<InstructionIndex>(
            process_.operations.size()));
    process_.operations.emplace_back(Jump { });
}

void Lowerer::lower_procedure_body(
    const std::size_t procedure_index)
{
    auto& frame = procedure_frames_[procedure_index];
    if (frame.lowered || !frame.allocated) {
        return;
    }
    frame.target = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto call_site : frame.call_sites) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target = *frame.target;
    }
    frame.call_sites.clear();
    frame.lowered = true;

    auto saved_locals = std::move(locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    auto saved_loop_controls = std::move(loop_controls_);
    auto saved_block_controls = std::move(block_controls_);
    auto saved_named_block_controls = std::move(named_block_controls_);
    auto saved_named_fork_controls = std::move(named_fork_controls_);
    auto saved_return_jumps = std::move(procedure_return_jumps_);
    auto saved_file_handles = std::move(procedure_file_handles_);
    const auto saved_active = active_procedure_;
    locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = { frame.source->name };
    loop_controls_.clear();
    block_controls_.clear();
    named_block_controls_.clear();
    named_fork_controls_.clear();
    procedure_return_jumps_.clear();
    procedure_file_handles_.clear();
    active_procedure_ = procedure_index;

    const auto bind =
        [&](const std::string& name,
            const frontend::Type& type,
            const RegisterId register_id) {
            locals_.insert_or_assign(name, register_id);
            local_signed_.insert_or_assign(name, type.is_signed);
            local_ranges_.insert_or_assign(name, type.packed_range);
            local_integer_ranges_.insert_or_assign(
                name, type.integer_range);
            local_members_.insert_or_assign(
                name, type.packed_members);
            local_types_.insert_or_assign(name, &type);
        };
    for (std::size_t index = 0;
        index < frame.source->arguments.size(); ++index) {
        const auto& argument = frame.source->arguments[index];
        bind(
            argument.name,
            argument.type,
            frame.arguments[index]);
        auto debug_name = scoped_local_name(argument.name);
        if (!debug_local_names_.emplace(debug_name).second) {
            debug_name += "@"
                + std::to_string(argument.span.begin.line)
                + ":" + std::to_string(argument.span.begin.column);
            debug_local_names_.emplace(debug_name);
        }
        process_.debug_locals.push_back(DebugLocal {
            std::move(debug_name),
            argument.type.spelling,
            frame.arguments[index],
            static_cast<std::size_t>(*argument.type.width()),
            SourceLocation {
                argument.span.source_name.str(),
                static_cast<std::uint32_t>(
                    argument.span.begin.line),
                static_cast<std::uint32_t>(
                    argument.span.begin.column) },
            { },
            { },
            value_kind(argument.type.domain),
            argument.type.enumeration_literals,
            argument.type.systemverilog_scalar });
        if (argument.type.integer_range) {
            const auto [lower, upper] = integer_bounds(argument.type.integer_range);
            process_.debug_locals.back().integer_lower = lower;
            process_.debug_locals.back().integer_upper = upper;
        }
    }
    const auto packed_begin = next_register_;
    initialize_variables(frame.source->variables);
    lower_statements(frame.source->statements);
    for (auto id = packed_begin; id < next_register_; ++id) {
        frame.invocation_packed.push_back(id);
    }
    const auto epilogue = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : procedure_return_jumps_) {
        process_.operations[jump] = Jump { epilogue };
    }
    for (const auto handle : procedure_file_handles_) {
        process_.operations.emplace_back(
            FileClose { handle, true, true });
    }
    emit_vhdl_access_scope_cleanup(frame.source->variables);
    process_.operations.emplace_back(
        Return { procedure_call_stack_ });
    for (const auto push_site : frame.invocation_push_sites) {
        process_.operations[push_site] = CallableFramePush {
            frame.invocation_identity,
            frame.invocation_packed,
            { },
            { },
            true
        };
    }
    frame.invocation_push_sites.clear();
    frame.invocation_layout_finalized = true;

    active_procedure_ = saved_active;
    procedure_return_jumps_ = std::move(saved_return_jumps);
    procedure_file_handles_ = std::move(saved_file_handles);
    loop_controls_ = std::move(saved_loop_controls);
    block_controls_ = std::move(saved_block_controls);
    named_block_controls_ = std::move(saved_named_block_controls);
    named_fork_controls_ = std::move(saved_named_fork_controls);
    local_scope_ = std::move(saved_scope);
    local_types_ = std::move(saved_types);
    local_members_ = std::move(saved_members);
    local_integer_ranges_ = std::move(saved_integer_ranges);
    local_ranges_ = std::move(saved_ranges);
    local_signed_ = std::move(saved_signed);
    locals_ = std::move(saved_locals);
}

bool Lowerer::procedure_dependencies_suspend(
    const std::unordered_set<std::size_t>& roots) const
{
    std::vector<bool> visited(procedure_frames_.size(), false);
    const auto visit = [&](const auto& self,
                           const std::size_t index) -> bool {
        if (index >= procedure_frames_.size() || visited[index]) {
            return false;
        }
        visited[index] = true;
        if (procedure_suspending_[index]) {
            return true;
        }
        return std::ranges::any_of(
            procedure_dependencies_[index],
            [&](const std::size_t dependency) {
                return self(self, dependency);
            });
    };
    return std::ranges::any_of(
        roots,
        [&](const std::size_t root) {
            return visit(visit, root);
        });
}

void Lowerer::lower_pending_procedures()
{
    while (!pending_procedures_.empty()) {
        const auto procedure = pending_procedures_.front();
        pending_procedures_.pop_front();
        procedure_frames_[procedure].queued = false;
        lower_procedure_body(procedure);
    }
}

} // namespace fsim::elaboration

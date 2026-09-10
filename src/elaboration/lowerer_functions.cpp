// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

const frontend::FunctionDeclaration* Lowerer::visible_function(
    const std::string_view name) const
{
    const auto found = function_indices_.find(std::string { name });
    return found == function_indices_.end()
            || found->second.size() != 1
        ? nullptr
        : function_frames_[found->second.front()].source;
}

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_simulator_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    const auto api = frontend::vhdl_simulator_api(expression.text);
    if (api == frontend::VhdlSimulatorApi::none) {
        return ExpressionAttempt { };
    }
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
        report(
            "FSIM-ELAB-VHENV-001",
            "the STD.ENV simulator API requires VHDL-2019",
            expression.span);
        return std::nullopt;
    }
    if (api == frontend::VhdlSimulatorApi::stop
        || api == frontend::VhdlSimulatorApi::finish
        || api == frontend::VhdlSimulatorApi::set_psl_cover_assert
        || api == frontend::VhdlSimulatorApi::clear_psl_state) {
        report(
            "FSIM-ELAB-VHENV-002",
            "this STD.ENV declaration is a procedure and cannot be used as a value",
            expression.span);
        return std::nullopt;
    }
    const bool psl_query
        = api == frontend::VhdlSimulatorApi::psl_assert_failed
        || api == frontend::VhdlSimulatorApi::psl_is_covered
        || api == frontend::VhdlSimulatorApi::get_psl_cover_assert
        || api == frontend::VhdlSimulatorApi::psl_is_assert_covered;
    if (psl_query) {
        if (!expression.operands.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV PSL query functions do not accept arguments",
                expression.span);
            return std::nullopt;
        }
        frontend::Type boolean;
        boolean.domain = frontend::ValueDomain::Boolean;
        boolean.spelling = "boolean";
        boolean.packed_range = frontend::PackedRange { 0, 0, true };
        boolean.nominal_type = "@builtin:boolean";
        boolean.vhdl_type_declaration = boolean.nominal_type;
        if (expected_type != nullptr
            && !vhdl_callable_type_matches(boolean, *expected_type)) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV PSL query result is incompatible with its context",
                expression.span);
            return std::nullopt;
        }
        using Kind = VhdlPslApiKind;
        const auto kind = api
                == frontend::VhdlSimulatorApi::psl_assert_failed
            ? Kind::assert_failed
            : api == frontend::VhdlSimulatorApi::psl_is_covered
            ? Kind::is_covered
            : api == frontend::VhdlSimulatorApi::get_psl_cover_assert
            ? Kind::get_cover_assert : Kind::is_assert_covered;
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(
            VhdlPslApi { kind, destination, std::nullopt });
        return destination;
    }
    const bool assert_query
        = api == frontend::VhdlSimulatorApi::is_vhdl_assert_failed
        || api == frontend::VhdlSimulatorApi::get_vhdl_assert_count
        || api == frontend::VhdlSimulatorApi::get_vhdl_assert_enable
        || api == frontend::VhdlSimulatorApi::get_vhdl_read_severity;
    if (assert_query) {
        const bool accepts_level
            = api != frontend::VhdlSimulatorApi::get_vhdl_read_severity;
        const bool optional_level
            = api != frontend::VhdlSimulatorApi::get_vhdl_assert_format;
        const bool names_valid = expression.call_argument_names.empty()
            || (expression.call_argument_names.size()
                    == expression.operands.size()
                && std::ranges::all_of(
                    expression.call_argument_names,
                    [](const std::string& name) {
                        return name.empty() || name == "level";
                    }));
        if (!names_valid || expression.operands.size() > 1U
            || (!accepts_level && !expression.operands.empty())
            || (!optional_level && expression.operands.empty())) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV assertion query has no matching standardized association profile",
                expression.span);
            return std::nullopt;
        }
        std::optional<RegisterId> level;
        if (!expression.operands.empty()) {
            frontend::Type severity;
            severity.spelling = "severity_level";
            severity.domain = frontend::ValueDomain::Bit2;
            severity.packed_range = frontend::PackedRange { 1, 0, true };
            severity.enumeration_literals = {
                "note", "warning", "error", "failure" };
            level = lower_expression(expression.operands.front(), 2U, &severity);
            if (!level || register_width(*level) != 2U) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV assertion LEVEL must be SEVERITY_LEVEL",
                    expression.operands.front().span);
                return std::nullopt;
            }
        } else if (api == frontend::VhdlSimulatorApi::get_vhdl_assert_enable) {
            level = allocate_register(2U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                *level, PackedLogic4::from_aval_bval(2U, 0U, 0U) });
        }
        frontend::Type result_type;
        std::size_t width { 1U };
        auto domain = frontend::ValueDomain::Boolean;
        auto kind = VhdlAssertApiKind::is_failed;
        if (api == frontend::VhdlSimulatorApi::get_vhdl_assert_count) {
            result_type.spelling = "natural";
            result_type.domain = frontend::ValueDomain::Integer;
            result_type.is_signed = true;
            result_type.packed_range = frontend::PackedRange { 63, 0, true };
            result_type.integer_range = frontend::IntegerRange {
                0, std::numeric_limits<std::int64_t>::max(), false };
            result_type.nominal_type = "@builtin:natural";
            result_type.vhdl_type_declaration = result_type.nominal_type;
            width = 64U;
            domain = frontend::ValueDomain::Integer;
            kind = VhdlAssertApiKind::get_count;
        } else if (api
            == frontend::VhdlSimulatorApi::get_vhdl_read_severity) {
            result_type.spelling = "severity_level";
            result_type.domain = frontend::ValueDomain::Bit2;
            result_type.packed_range = frontend::PackedRange { 1, 0, true };
            result_type.enumeration_literals = {
                "note", "warning", "error", "failure" };
            width = 2U;
            domain = frontend::ValueDomain::Bit2;
            kind = VhdlAssertApiKind::get_read_severity;
        } else {
            result_type.spelling = "boolean";
            result_type.domain = frontend::ValueDomain::Boolean;
            result_type.packed_range = frontend::PackedRange { 0, 0, true };
            result_type.nominal_type = "@builtin:boolean";
            result_type.vhdl_type_declaration = result_type.nominal_type;
            kind = api == frontend::VhdlSimulatorApi::get_vhdl_assert_enable
                ? VhdlAssertApiKind::get_enable
                : VhdlAssertApiKind::is_failed;
        }
        if (expected_type != nullptr
            && !vhdl_callable_type_matches(result_type, *expected_type)) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV assertion query result is incompatible with its context",
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(width, domain);
        process_.operations.emplace_back(VhdlAssertApi {
            kind,
            destination,
            std::nullopt,
            level,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            SourceLocation {
                expression.span.source_name.str(),
                static_cast<std::uint32_t>(expression.span.begin.line),
                static_cast<std::uint32_t>(expression.span.begin.column) } });
        return destination;
    }
    if (api == frontend::VhdlSimulatorApi::dayofweek
        || api == frontend::VhdlSimulatorApi::time_record
        || api == frontend::VhdlSimulatorApi::directory_items
        || api == frontend::VhdlSimulatorApi::directory
        || api == frontend::VhdlSimulatorApi::call_path_element
        || api == frontend::VhdlSimulatorApi::call_path_vector
        || api == frontend::VhdlSimulatorApi::call_path_vector_ptr
        || api == frontend::VhdlSimulatorApi::dir_open_status
        || api == frontend::VhdlSimulatorApi::dir_create_status
        || api == frontend::VhdlSimulatorApi::dir_delete_status
        || api == frontend::VhdlSimulatorApi::file_delete_status) {
        report(
            "FSIM-ELAB-VHENV-002",
            "STD.ENV DAYOFWEEK and TIME_RECORD are types, not values",
            expression.span);
        return std::nullopt;
    }
    const bool directory_function = api == frontend::VhdlSimulatorApi::dir_open
        || api == frontend::VhdlSimulatorApi::dir_itemexists
        || api == frontend::VhdlSimulatorApi::dir_itemisdir
        || api == frontend::VhdlSimulatorApi::dir_itemisfile
        || api == frontend::VhdlSimulatorApi::dir_workingdir
        || api == frontend::VhdlSimulatorApi::dir_createdir
        || api == frontend::VhdlSimulatorApi::dir_deletedir
        || api == frontend::VhdlSimulatorApi::dir_deletefile;
    if (api == frontend::VhdlSimulatorApi::dir_close
        || api == frontend::VhdlSimulatorApi::dir_separator) {
        report(
            "FSIM-ELAB-VHENV-002",
            "this STD.ENV directory declaration cannot be used as a packed function",
            expression.span);
        return std::nullopt;
    }
    if (api == frontend::VhdlSimulatorApi::getenv
        || api == frontend::VhdlSimulatorApi::vhdl_version
        || api == frontend::VhdlSimulatorApi::tool_type
        || api == frontend::VhdlSimulatorApi::tool_vendor
        || api == frontend::VhdlSimulatorApi::tool_name
        || api == frontend::VhdlSimulatorApi::tool_edition
        || api == frontend::VhdlSimulatorApi::tool_version
        || api == frontend::VhdlSimulatorApi::file_name
        || api == frontend::VhdlSimulatorApi::file_path) {
        report(
            "FSIM-ELAB-VHENV-004",
            "STD.ENV environment information requires a STRING-compatible context",
            expression.span);
        return std::nullopt;
    }
    if (api == frontend::VhdlSimulatorApi::get_call_path) {
        report(
            "FSIM-ELAB-VHENV-004",
            "STD.ENV GET_CALL_PATH requires a CALL_PATH_VECTOR_PTR-compatible context",
            expression.span);
        return std::nullopt;
    }
    if (api == frontend::VhdlSimulatorApi::file_line) {
        if (!expression.operands.empty()) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV FILE_LINE does not accept arguments",
                expression.span);
            return std::nullopt;
        }
        frontend::Type positive;
        positive.domain = frontend::ValueDomain::Integer;
        positive.spelling = "positive";
        positive.is_signed = true;
        positive.packed_range = frontend::PackedRange { 63, 0, true };
        positive.integer_range = frontend::IntegerRange {
            1, std::numeric_limits<std::int64_t>::max(), false };
        positive.nominal_type = "@builtin:positive";
        positive.vhdl_type_declaration = positive.nominal_type;
        if (expected_type != nullptr
            && !vhdl_callable_type_matches(positive, *expected_type)) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV FILE_LINE result is incompatible with its context",
                expression.span);
            return std::nullopt;
        }
        const auto width = std::max<std::size_t>(expected_width, 64U);
        const auto destination = allocate_register(
            width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4::from_aval_bval(
                width,
                static_cast<std::uint64_t>(expression.span.begin.line),
                0U) });
        return destination;
    }
    if (directory_function) {
        std::vector<std::string_view> formals;
        if (api == frontend::VhdlSimulatorApi::dir_open) {
            formals = { "dir", "path" };
        } else if (api == frontend::VhdlSimulatorApi::dir_createdir) {
            formals = { "path", "parents" };
        } else if (api == frontend::VhdlSimulatorApi::dir_deletedir) {
            formals = { "path", "recursive" };
        } else {
            formals = { "path" };
        }
        std::vector<const Expression*> actuals(formals.size(), nullptr);
        std::size_t positional = 0U;
        bool valid = expression.operands.size() <= formals.size();
        valid = valid
            && (expression.call_argument_names.empty()
                || expression.call_argument_names.size()
                    == expression.operands.size());
        for (std::size_t index = 0; index < expression.operands.size(); ++index) {
            const auto name = expression.call_argument_names.empty()
                    || index >= expression.call_argument_names.size()
                ? std::string_view { }
                : std::string_view { expression.call_argument_names[index] };
            std::size_t formal = positional;
            if (name.empty()) {
                ++positional;
            } else {
                const auto found = std::ranges::find(formals, name);
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
                actuals[formal] = &expression.operands[index];
            }
        }
        const bool noarg_working = api
                == frontend::VhdlSimulatorApi::dir_workingdir
            && expression.operands.empty();
        if (noarg_working) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV DIR_WORKINGDIR without arguments requires a STRING-compatible context",
                expression.span);
            return std::nullopt;
        }
        if (!valid || actuals.empty() || actuals[0] == nullptr
            || (api == frontend::VhdlSimulatorApi::dir_open
                && actuals[1] == nullptr)) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV directory function has no matching standardized association profile",
                expression.span);
            return std::nullopt;
        }
        std::optional<ContainerRegisterId> directory;
        std::size_t path_index = 0U;
        if (api == frontend::VhdlSimulatorApi::dir_open) {
            const auto& value = *actuals[0];
            const auto found = value.kind == ExpressionKind::Identifier
                ? container_locals_.find(value.text) : container_locals_.end();
            const auto* type = value.kind == ExpressionKind::Identifier
                ? object_type(value.text) : nullptr;
            if (found == container_locals_.end() || type == nullptr
                || !frontend::is_vhdl_environment_directory(*type)) {
                report(
                    "FSIM-ELAB-VHENV-006",
                    "STD.ENV DIR_OPEN DIR must be a writable DIRECTORY variable",
                    value.span);
                return std::nullopt;
            }
            directory = found->second;
            path_index = 1U;
        }
        const auto path = lower_string_expression(*actuals[path_index]);
        if (!path) {
            report(
                "FSIM-ELAB-VHENV-006",
                "STD.ENV directory PATH must be STRING-compatible",
                actuals[path_index]->span);
            return std::nullopt;
        }
        std::optional<RegisterId> option;
        if (actuals.size() > path_index + 1U
            && actuals[path_index + 1U] != nullptr) {
            option = lower_expression(*actuals[path_index + 1U], 1U);
            if (!option) {
                return std::nullopt;
            }
        }
        const bool boolean_result = api
                == frontend::VhdlSimulatorApi::dir_itemexists
            || api == frontend::VhdlSimulatorApi::dir_itemisdir
            || api == frontend::VhdlSimulatorApi::dir_itemisfile;
        frontend::Type result_type;
        if (boolean_result) {
            result_type.domain = frontend::ValueDomain::Boolean;
            result_type.spelling = "boolean";
            result_type.packed_range = frontend::PackedRange { 0, 0, true };
            result_type.nominal_type = "@builtin:boolean";
            result_type.vhdl_type_declaration = result_type.nominal_type;
        } else {
            const auto status_kind = api == frontend::VhdlSimulatorApi::dir_createdir
                ? frontend::VhdlSimulatorApi::dir_create_status
                : api == frontend::VhdlSimulatorApi::dir_deletedir
                ? frontend::VhdlSimulatorApi::dir_delete_status
                : api == frontend::VhdlSimulatorApi::dir_deletefile
                ? frontend::VhdlSimulatorApi::file_delete_status
                : frontend::VhdlSimulatorApi::dir_open_status;
            result_type = frontend::vhdl_environment_directory_status_type(
                status_kind);
        }
        if (expected_type != nullptr
            && !vhdl_callable_type_matches(result_type, *expected_type)) {
            report(
                "FSIM-ELAB-VHENV-004",
                "STD.ENV directory result is incompatible with its context",
                expression.span);
            return std::nullopt;
        }
        using Kind = VhdlEnvironmentDirectoryKind;
        const auto kind = api == frontend::VhdlSimulatorApi::dir_open
            ? Kind::open
            : api == frontend::VhdlSimulatorApi::dir_itemexists
            ? Kind::item_exists
            : api == frontend::VhdlSimulatorApi::dir_itemisdir
            ? Kind::item_is_directory
            : api == frontend::VhdlSimulatorApi::dir_itemisfile
            ? Kind::item_is_file
            : api == frontend::VhdlSimulatorApi::dir_workingdir
            ? Kind::set_working_directory
            : api == frontend::VhdlSimulatorApi::dir_createdir
            ? Kind::create_directory
            : api == frontend::VhdlSimulatorApi::dir_deletedir
            ? Kind::delete_directory : Kind::delete_file;
        const auto destination = allocate_register(
            boolean_result ? 1U : 3U, result_type.domain);
        process_.operations.emplace_back(VhdlEnvironmentDirectory {
            kind, destination, std::nullopt, directory, path, option });
        return destination;
    }
    if (api == frontend::VhdlSimulatorApi::to_string) {
        report(
            "FSIM-ELAB-VHENV-004",
            "STD.ENV TO_STRING requires a STRING-compatible context",
            expression.span);
        return std::nullopt;
    }
    if (expression.kind != ExpressionKind::Identifier
        && expression.kind != ExpressionKind::Call) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV data/time function has malformed executable HIR",
            expression.span);
        return std::nullopt;
    }

    frontend::Type real_type;
    real_type.domain = frontend::ValueDomain::Bit2;
    real_type.spelling = "real";
    real_type.systemverilog_scalar =
        frontend::SystemVerilogScalarKind::Real;
    real_type.is_signed = true;
    real_type.packed_range = frontend::PackedRange { 63, 0, true };
    real_type.nominal_type = "@builtin:real";
    real_type.vhdl_type_declaration = real_type.nominal_type;
    frontend::Type time_type;
    time_type.domain = frontend::ValueDomain::Integer;
    time_type.spelling = "time";
    time_type.is_signed = true;
    time_type.packed_range = frontend::PackedRange { 63, 0, true };
    time_type.integer_range = frontend::IntegerRange {
        0, std::numeric_limits<std::int64_t>::max(), false };
    time_type.nominal_type = "@builtin:time";
    time_type.vhdl_type_declaration = time_type.nominal_type;
    const auto record_type = frontend::vhdl_environment_time_record_type();
    const auto context_matches = [&](const frontend::Type& result) {
        if (expected_type == nullptr
            || vhdl_callable_type_matches(result, *expected_type)) {
            return true;
        }
        report(
            "FSIM-ELAB-VHENV-004",
            "STD.ENV data/time result is incompatible with its context",
            expression.span);
        return false;
    };
    const auto one_actual = [&](const std::string_view first_name,
                                const std::string_view alternate_name = {})
        -> const Expression* {
        if (expression.operands.size() != 1U) {
            return nullptr;
        }
        if (!expression.call_argument_names.empty()) {
            const auto& name = expression.call_argument_names.front();
            if (!name.empty() && name != first_name
                && (alternate_name.empty() || name != alternate_name)) {
                return nullptr;
            }
        }
        return &expression.operands.front();
    };
    if (api == frontend::VhdlSimulatorApi::resolution_limit) {
        if (!expression.operands.empty() || !context_matches(time_type)) {
            if (!expression.operands.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV RESOLUTION_LIMIT does not accept arguments",
                    expression.span);
            }
            return std::nullopt;
        }
        const auto width = std::max<std::size_t>(expected_width, 64U);
        const auto destination = allocate_register(
            width, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            destination,
            PackedLogic4::from_aval_bval(width, 1U, 0U) });
        return destination;
    }

    using Kind = VhdlEnvironmentTimeKind;
    Kind kind { Kind::current_local };
    const frontend::Type* result_type = nullptr;
    const Expression* first_expression = nullptr;
    const frontend::Type* first_type = nullptr;
    if (api == frontend::VhdlSimulatorApi::localtime
        || api == frontend::VhdlSimulatorApi::gmtime) {
        result_type = &record_type;
        if (expression.operands.empty()) {
            kind = api == frontend::VhdlSimulatorApi::localtime
                ? Kind::current_local : Kind::current_utc;
        } else {
            first_expression = one_actual("timer", "trec");
            const auto actual_type = first_expression
                ? vhdl_expression_type(*first_expression)
                : std::nullopt;
            if (actual_type
                && actual_type->systemverilog_scalar
                    == frontend::SystemVerilogScalarKind::Real) {
                first_type = &real_type;
                kind = api == frontend::VhdlSimulatorApi::localtime
                    ? Kind::local_from_epoch : Kind::utc_from_epoch;
            } else if (actual_type
                && frontend::is_vhdl_environment_time_record(*actual_type)) {
                first_type = &record_type;
                kind = api == frontend::VhdlSimulatorApi::localtime
                    ? Kind::local_from_utc_record
                    : Kind::utc_from_local_record;
            } else {
                first_expression = nullptr;
            }
        }
    } else if (api == frontend::VhdlSimulatorApi::epoch) {
        result_type = &real_type;
        if (expression.operands.empty()) {
            kind = Kind::current_epoch;
        } else {
            first_expression = one_actual("trec");
            first_type = &record_type;
            kind = Kind::epoch_from_local;
            const auto actual_type = first_expression
                ? vhdl_expression_type(*first_expression)
                : std::nullopt;
            if (!actual_type
                || !frontend::is_vhdl_environment_time_record(*actual_type)) {
                first_expression = nullptr;
            }
        }
    } else if (api == frontend::VhdlSimulatorApi::time_to_seconds) {
        result_type = &real_type;
        first_expression = one_actual("time_val");
        first_type = &time_type;
        kind = Kind::time_to_seconds;
        const auto actual_type = first_expression
            ? vhdl_expression_type(*first_expression)
            : std::nullopt;
        if (!actual_type
                || (actual_type->spelling != "time"
                    && actual_type->nominal_type != "@builtin:time")) {
            first_expression = nullptr;
        }
    } else if (api == frontend::VhdlSimulatorApi::seconds_to_time) {
        result_type = &time_type;
        first_expression = one_actual("real_val");
        first_type = &real_type;
        kind = Kind::seconds_to_time;
        const auto actual_type = first_expression
            ? vhdl_expression_type(*first_expression)
            : std::nullopt;
        if (!actual_type
            || actual_type->systemverilog_scalar
                != frontend::SystemVerilogScalarKind::Real) {
            first_expression = nullptr;
        }
    }
    if (result_type == nullptr
        || (!expression.operands.empty() && first_expression == nullptr)) {
        report(
            "FSIM-ELAB-VHENV-003",
            "STD.ENV data/time function '" + expression.text
                + "' has no matching standardized overload",
            expression.span);
        return std::nullopt;
    }
    if (!context_matches(*result_type)) {
        return std::nullopt;
    }
    std::optional<RegisterId> first;
    if (first_expression != nullptr) {
        first = lower_expression(
            *first_expression,
            static_cast<std::size_t>(first_type->width().value_or(0U)),
            first_type);
        if (!first) {
            return std::nullopt;
        }
    }
    const auto destination = allocate_register(
        static_cast<std::size_t>(result_type->width().value_or(0U)),
        result_type->domain);
    process_.operations.emplace_back(VhdlEnvironmentTime {
        kind, destination, first, std::nullopt });
    return destination;
}

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_environment_binary_expression(
    const Expression& expression,
    const frontend::Type* expected_type)
{
    if (expression.kind != ExpressionKind::Binary
        || expression.operands.size() != 2U
        || (expression.text != "+" && expression.text != "-")) {
        return ExpressionAttempt { };
    }
    const auto left_type = vhdl_expression_type(expression.operands[0]);
    const auto right_type = vhdl_expression_type(expression.operands[1]);
    const bool left_record = left_type
        && frontend::is_vhdl_environment_time_record(*left_type);
    const bool right_record = right_type
        && frontend::is_vhdl_environment_time_record(*right_type);
    if (!left_record && !right_record) {
        return ExpressionAttempt { };
    }
    const bool left_real = left_type
        && left_type->systemverilog_scalar
            == frontend::SystemVerilogScalarKind::Real;
    const bool right_real = right_type
        && right_type->systemverilog_scalar
            == frontend::SystemVerilogScalarKind::Real;
    const bool difference = expression.text == "-"
        && left_record && right_record;
    if (!difference
        && !((left_record && right_real)
            || (left_real && right_record))) {
        report(
            "FSIM-ELAB-VHENV-005",
            "STD.ENV TIME_RECORD arithmetic requires TIME_RECORD/REAL or TIME_RECORD/TIME_RECORD subtraction",
            expression.span);
        return std::nullopt;
    }
    auto real_type = left_real ? *left_type
        : right_real ? *right_type
        : frontend::Type { };
    if (real_type.systemverilog_scalar
        == frontend::SystemVerilogScalarKind::None) {
        real_type.domain = frontend::ValueDomain::Bit2;
        real_type.spelling = "real";
        real_type.systemverilog_scalar =
            frontend::SystemVerilogScalarKind::Real;
        real_type.is_signed = true;
        real_type.packed_range = frontend::PackedRange { 63, 0, true };
        real_type.nominal_type = "@builtin:real";
        real_type.vhdl_type_declaration = real_type.nominal_type;
    }
    const auto record_type = frontend::vhdl_environment_time_record_type();
    const auto& result_type = difference ? real_type : record_type;
    if (expected_type != nullptr
        && !vhdl_callable_type_matches(result_type, *expected_type)) {
        report(
            "FSIM-ELAB-VHENV-004",
            "STD.ENV TIME_RECORD arithmetic result is incompatible with its context",
            expression.span);
        return std::nullopt;
    }

    const Expression* first_expression = nullptr;
    const Expression* second_expression = nullptr;
    const frontend::Type* first_type = nullptr;
    const frontend::Type* second_type = nullptr;
    auto kind = VhdlEnvironmentTimeKind::add_seconds;
    if (difference) {
        first_expression = &expression.operands[0];
        second_expression = &expression.operands[1];
        first_type = &record_type;
        second_type = &record_type;
        kind = VhdlEnvironmentTimeKind::difference_seconds;
    } else if (left_record) {
        first_expression = &expression.operands[0];
        second_expression = &expression.operands[1];
        first_type = &record_type;
        second_type = &real_type;
        kind = expression.text == "+"
            ? VhdlEnvironmentTimeKind::add_seconds
            : VhdlEnvironmentTimeKind::subtract_seconds;
    } else {
        first_expression = &expression.operands[1];
        second_expression = &expression.operands[0];
        first_type = &record_type;
        second_type = &real_type;
        kind = expression.text == "+"
            ? VhdlEnvironmentTimeKind::add_seconds
            : VhdlEnvironmentTimeKind::reverse_subtract_seconds;
    }
    const auto first = lower_expression(
        *first_expression,
        static_cast<std::size_t>(first_type->width().value_or(0U)),
        first_type);
    const auto second = lower_expression(
        *second_expression,
        static_cast<std::size_t>(second_type->width().value_or(0U)),
        second_type);
    if (!first || !second) {
        return std::nullopt;
    }
    const auto destination = allocate_register(
        static_cast<std::size_t>(result_type.width().value_or(0U)),
        result_type.domain);
    process_.operations.emplace_back(VhdlEnvironmentTime {
        kind, destination, *first, *second });
    return destination;
}

void Lowerer::initialize_function_support()
{
    vhdl_function_specializations_.clear();
    function_frames_.clear();
    function_indices_.clear();
    vhdl_function_specialization_indices_.clear();
    pending_functions_.clear();
    function_dependencies_.clear();
    active_function_.reset();
    function_return_jumps_.clear();
    function_call_stack_ = { };
    function_support_initialized_ = false;
    if (functions_.empty()) {
        return;
    }
    if (functions_.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVFUNC-001",
            "too many visible functions for the SimIR call stack",
            functions_.front().span);
        return;
    }

    function_frames_.reserve(functions_.size());
    for (const auto& function : functions_) {
        auto& overloads = function_indices_[function.name];
        if (function.language
                != frontend::Language::Vhdl2008
            && !overloads.empty()) {
            report(
                "FSIM-ELAB-SVFUNC-002",
                "ambiguous visible function '" + function.name + "'",
                function.span);
            continue;
        }
        const bool duplicate_vhdl_profile = function.language == frontend::Language::Vhdl2008
            && std::ranges::any_of(
                overloads,
                [&](const std::size_t candidate_index) {
                    const auto& candidate = *function_frames_[candidate_index].source;
                    const bool imported_distinct_declarations = !candidate.visibility_owner.empty()
                        && !function.visibility_owner.empty()
                        && candidate.visibility_owner
                            != function.visibility_owner;
                    const bool mapped_distinct_declarations =
                        !candidate.specialization_identity.empty()
                        && !function.specialization_identity.empty()
                        && candidate.specialization_identity
                            != function.specialization_identity;
                    if (candidate.arguments.size()
                            != function.arguments.size()
                        || imported_distinct_declarations
                        || mapped_distinct_declarations
                        || !frontend::vhdl_base_type_profiles_match(
                            candidate.return_type,
                            function.return_type)) {
                        return false;
                    }
                    for (std::size_t argument = 0;
                        argument < function.arguments.size();
                        ++argument) {
                        if (!frontend::vhdl_parameter_type_profiles_match(
                                candidate.arguments[argument].type,
                                function.arguments[argument].type)) {
                            return false;
                        }
                    }
                    return true;
                });
        if (duplicate_vhdl_profile) {
            report(
                "FSIM-ELAB-VHOVER-003",
                "duplicate VHDL function profile '"
                    + function.name + "'",
                function.span);
            continue;
        }
        const auto index = function_frames_.size();
        FunctionFrame frame;
        frame.source = &function;
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (!function.automatic) {
            frame.static_variables = allocate_static_callable_variables(
                function.variables,
                function.statements,
                function.name);
        }
        function_frames_.push_back(std::move(frame));
        overloads.push_back(index);
    }
    for (auto& [name, overloads] : function_indices_) {
        (void)name;
        std::ranges::stable_sort(
            overloads,
            [&](const std::size_t left, const std::size_t right) {
                const auto& lhs = *function_frames_[left].source;
                const auto& rhs = *function_frames_[right].source;
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
    function_dependencies_.resize(function_frames_.size());
    if (function_frames_.empty()) {
        return;
    }

    for (const auto& frame : function_frames_) {
        const auto& function = *frame.source;
        if (function.language
            != frontend::Language::Vhdl2008) {
            continue;
        }
        for (const auto& argument : function.arguments) {
            if (argument.default_value
                && !vhdl_expression_matches_type(
                    *argument.default_value, argument.type)) {
                report(
                    "FSIM-ELAB-VHLEGAL-007",
                    "default for VHDL function formal '"
                        + argument.name
                        + "' does not match its subtype",
                    argument.default_value->span);
            }
        }
        if (contains_explicit_wait(function.statements)) {
            report(
                "FSIM-ELAB-VHLEGAL-009",
                "VHDL function '" + function.name
                    + "' contains a wait statement",
                function.span);
        }
        if (!function.pure) {
            continue;
        }
        std::set<std::string> dependencies;
        collect_statement_identifiers(
            function.statements, dependencies);
        for (const auto& argument : function.arguments) {
            dependencies.erase(argument.name);
        }
        for (const auto& variable : function.variables) {
            dependencies.erase(variable.name);
        }
        for (const auto& constant : function.constants) {
            dependencies.erase(constant.name);
        }
        for (const auto& alias : function.signal_aliases) {
            dependencies.erase(alias.name);
        }
        const auto erase_statement_locals =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> void {
              for (const auto& statement : statements) {
                  if (!statement.loop_variable.empty()) {
                      dependencies.erase(statement.loop_variable);
                  }
                  for (const auto& declaration :
                       statement.declarations) {
                      dependencies.erase(declaration.name);
                  }
                  self(self, statement.statements);
                  self(self, statement.else_statements);
                  for (const auto& alternative :
                       statement.case_alternatives) {
                      self(self, alternative.statements);
                  }
              }
            };
        erase_statement_locals(
            erase_statement_locals, function.statements);
        const auto signal = std::ranges::find_if(
            dependencies,
            [&](const auto& name) {
                return signals_.contains(name);
            });
        if (signal != dependencies.end()) {
            report(
                "FSIM-ELAB-VHLEGAL-005",
                "pure VHDL function '" + function.name
                    + "' reads signal '" + *signal + "'",
                function.span);
        }
        const auto calls_procedure =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> bool {
            for (const auto& statement : statements) {
                if (statement.kind == StatementKind::ProcedureCall
                    || self(self, statement.statements)
                    || self(self, statement.else_statements)) {
                    return true;
                }
                for (const auto& alternative :
                    statement.case_alternatives) {
                    if (self(self, alternative.statements)) {
                        return true;
                    }
                }
            }
            return false;
        };
        if (calls_procedure(
                calls_procedure, function.statements)) {
            report(
                "FSIM-ELAB-VHLEGAL-006",
                "pure VHDL function '" + function.name
                    + "' calls a procedure",
                function.span);
        }
    }

    function_support_initialized_ = true;
}

} // namespace fsim::elaboration

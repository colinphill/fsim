// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "fsim/version.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
namespace {

    struct VhdlDirectoryStringSelection {
        std::string object;
        const Expression* index { };
    };

    struct VhdlCallPathStringSelection {
        const Expression* container { };
        const Expression* index { };
        std::uint32_t member { };
    };

    [[nodiscard]] std::optional<VhdlCallPathStringSelection>
    vhdl_call_path_string_selection(const Expression& expression)
    {
        const Expression* selected = &expression;
        if (selected->kind == ExpressionKind::Call
            && (selected->text == "@vhdl-member:all"
                || selected->text == "@vhdl-dereference")
            && selected->operands.size() == 1U) {
            selected = &selected->operands.front();
        }
        constexpr std::string_view prefix { "@vhdl-member:" };
        if (selected->kind != ExpressionKind::Call
            || !selected->text.starts_with(prefix)
            || selected->operands.size() != 1U) {
            return std::nullopt;
        }
        const auto name = std::string_view { selected->text }.substr(
            prefix.size());
        const auto member = name == "name" ? std::optional<std::uint32_t> { 0U }
            : name == "file_name" ? std::optional<std::uint32_t> { 1U }
            : name == "file_path" ? std::optional<std::uint32_t> { 2U }
            : std::nullopt;
        const auto& indexed = selected->operands.front();
        if (!member || indexed.kind != ExpressionKind::Index
            || indexed.operands.size() != 2U) {
            return std::nullopt;
        }
        return VhdlCallPathStringSelection {
            &indexed.operands.front(), &indexed.operands[1], *member };
    }

    [[nodiscard]] std::optional<VhdlDirectoryStringSelection>
    vhdl_directory_string_selection(const Expression& expression)
    {
        constexpr std::string_view name_suffix { ".name" };
        constexpr std::string_view items_suffix { ".items" };
        if (expression.kind == ExpressionKind::Call
            && expression.text == "@vhdl-dereference"
            && expression.operands.size() == 1U
            && expression.operands.front().kind
                == ExpressionKind::Identifier
            && expression.operands.front().text.ends_with(name_suffix)) {
            auto object = expression.operands.front().text;
            object.resize(object.size() - name_suffix.size());
            return VhdlDirectoryStringSelection {
                std::move(object), nullptr };
        }
        if (expression.kind != ExpressionKind::Call
            || expression.text != "@vhdl-member:all"
            || expression.operands.size() != 1U) {
            return std::nullopt;
        }
        const auto& selection = expression.operands.front();
        if (selection.kind != ExpressionKind::Index
            || selection.operands.size() != 2U) {
            return std::nullopt;
        }
        const auto& items = selection.operands.front();
        if (items.kind != ExpressionKind::Call
            || items.text != "@vhdl-dereference"
            || items.operands.size() != 1U
            || items.operands.front().kind != ExpressionKind::Identifier
            || !items.operands.front().text.ends_with(items_suffix)) {
            return std::nullopt;
        }
        auto object = items.operands.front().text;
        object.resize(object.size() - items_suffix.size());
        return VhdlDirectoryStringSelection {
            std::move(object), &selection.operands[1] };
    }

    [[nodiscard]] const Expression* vhdl_environment_getenv_call(
        const Expression& expression)
    {
        if (frontend::vhdl_simulator_api(expression.text)
            == frontend::VhdlSimulatorApi::getenv) {
            return &expression;
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "@vhdl-member:all"
                || expression.text == "@vhdl-dereference")
            && expression.operands.size() == 1U
            && frontend::vhdl_simulator_api(
                expression.operands.front().text)
                == frontend::VhdlSimulatorApi::getenv) {
            return &expression.operands.front();
        }
        return nullptr;
    }

    [[nodiscard]] std::string systemverilog_type_name(
        const frontend::Type& type);

    [[nodiscard]] std::string range_text(
        const frontend::PackedRange& range)
    {
        return "[" + std::to_string(range.left) + ":"
            + std::to_string(range.right) + "]";
    }

    [[nodiscard]] std::string expression_range_text(
        const frontend::PackedRangeExpression& range)
    {
        return "[" + range.left.text + ":" + range.right.text + "]";
    }

    [[nodiscard]] std::string systemverilog_base_type_name(
        const frontend::Type& type)
    {
        if (type.systemverilog_virtual_interface) {
            auto result = std::string { "virtual " }
                + type.systemverilog_interface_type;
            if (!type.systemverilog_interface_modport.empty()) {
                result += "." + type.systemverilog_interface_modport;
            }
            return result;
        }
        if (!type.systemverilog_class_name.empty()) {
            return type.systemverilog_class_name;
        }
        if (!type.named_type.empty()) {
            return type.named_type;
        }
        if (!type.enumeration_literals.empty()) {
            return "enum";
        }
        switch (type.packed_aggregate) {
        case frontend::PackedAggregateKind::Struct:
            return "struct packed";
        case frontend::PackedAggregateKind::Union:
            return "union packed";
        case frontend::PackedAggregateKind::UnpackedStruct:
            return "struct";
        case frontend::PackedAggregateKind::TaggedUnion:
            return "union tagged";
        case frontend::PackedAggregateKind::UnpackedUnion:
            return "union";
        case frontend::PackedAggregateKind::None:
            break;
        }
        switch (type.systemverilog_scalar) {
        case frontend::SystemVerilogScalarKind::ShortReal:
            return "shortreal";
        case frontend::SystemVerilogScalarKind::Real:
            return "real";
        case frontend::SystemVerilogScalarKind::Realtime:
            return "realtime";
        case frontend::SystemVerilogScalarKind::Time:
            return "time";
        case frontend::SystemVerilogScalarKind::Chandle:
            return "chandle";
        case frontend::SystemVerilogScalarKind::None:
            break;
        }
        if (!type.spelling.empty() && type.spelling != "implicit") {
            return type.spelling;
        }
        switch (type.domain) {
        case frontend::ValueDomain::Bit2:
            return "bit";
        case frontend::ValueDomain::Logic4:
        case frontend::ValueDomain::Logic9:
            return "logic";
        case frontend::ValueDomain::Integer:
            return "int";
        case frontend::ValueDomain::Boolean:
            return "bit";
        case frontend::ValueDomain::String:
            return "string";
        case frontend::ValueDomain::Unknown:
            break;
        }
        return { };
    }

    [[nodiscard]] std::string systemverilog_type_name(
        const frontend::Type& type)
    {
        auto result = systemverilog_base_type_name(type);
        if (result.empty()) {
            return result;
        }
        const bool named = !type.named_type.empty();
        const bool atomic = result == "byte" || result == "shortint"
            || result == "int" || result == "longint"
            || result == "integer" || result == "time"
            || result == "shortreal" || result == "real"
            || result == "realtime" || result == "chandle"
            || result == "process" || result == "string";
        if (!named
            && (result == "bit" || result == "logic" || result == "reg")
            && type.is_signed) {
            result += " signed";
        }
        if (!named && !atomic) {
            if (!type.systemverilog_packed_dimensions.empty()) {
                for (const auto& dimension :
                    type.systemverilog_packed_dimensions) {
                    result += expression_range_text(dimension);
                }
            } else if (type.packed_range) {
                result += range_text(*type.packed_range);
            }
        }
        if (!type.systemverilog_container) {
            return result;
        }
        const auto& container = *type.systemverilog_container;
        switch (container.kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
            result += "[]";
            break;
        case frontend::SystemVerilogContainerKind::Queue:
            result += "[$";
            if (container.queue_maximum) {
                result += ":" + container.queue_maximum->text;
            }
            result += "]";
            break;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
            result += "[";
            if (container.associative_index_type) {
                result += systemverilog_type_name(
                    *container.associative_index_type);
            } else {
                result += "*";
            }
            result += "]";
            break;
        case frontend::SystemVerilogContainerKind::StaticArray:
            if (!container.static_range_expressions.empty()) {
                for (const auto& dimension :
                    container.static_range_expressions) {
                    result += expression_range_text(dimension);
                }
            } else if (container.static_range) {
                result += range_text(*container.static_range);
            }
            break;
        }
        return result;
    }

} // namespace

bool Lowerer::is_string_expression(
    const Expression& expression) const
{
    if (language_ == frontend::Language::Vhdl2008
        && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019
        && (expression.kind == ExpressionKind::Call
            || expression.kind == ExpressionKind::Identifier)) {
        const auto separator = expression.text.find_last_of('.');
        if (separator != std::string::npos) {
            const auto receiver = expression.text.substr(0U, separator);
            const auto method = std::string_view { expression.text }.substr(
                separator + 1U);
            const auto* type = object_type(receiver);
            const auto type_name = type == nullptr ? std::string { }
                : !type->vhdl_type_declaration.empty()
                    ? type->vhdl_type_declaration : type->spelling;
            const bool mirror = type != nullptr
                && (type_name.find("value_mirror") != std::string::npos
                    || type_name.find("subtype_mirror") != std::string::npos);
            if (mirror && (method == "simple_name" || method == "image"
                    || method == "unit_name" || method == "element_name"
                    || method == "get_file_logical_name")) {
                return true;
            }
        }
    }
    if (language_ == frontend::Language::Vhdl2008
        && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
        if (vhdl_call_path_string_selection(expression)) {
            return true;
        }
        if (const auto selected = vhdl_directory_string_selection(expression);
            selected && container_locals_.contains(selected->object)
            && object_type(selected->object) != nullptr
            && frontend::is_vhdl_environment_directory(
                *object_type(selected->object))) {
            return true;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "@vhdl-dereference"
            && expression.operands.size() == 1U
            && expression.operands.front().kind == ExpressionKind::Identifier
            && string_locals_.contains(expression.operands.front().text)) {
            const auto* type = object_type(expression.operands.front().text);
            const auto separator = type == nullptr
                ? std::string::npos : type->spelling.find_last_of('.');
            if (type != nullptr && type->domain == frontend::ValueDomain::String
                && type->spelling.substr(separator == std::string::npos
                        ? 0U : separator + 1U) == "line") {
                return true;
            }
        }
        const auto api = frontend::vhdl_simulator_api(expression.text);
        const bool environment_string
            = vhdl_environment_getenv_call(expression) != nullptr
            || api == frontend::VhdlSimulatorApi::vhdl_version
            || api == frontend::VhdlSimulatorApi::tool_type
            || api == frontend::VhdlSimulatorApi::tool_vendor
            || api == frontend::VhdlSimulatorApi::tool_name
            || api == frontend::VhdlSimulatorApi::tool_edition
            || api == frontend::VhdlSimulatorApi::tool_version
            || api == frontend::VhdlSimulatorApi::file_name
            || api == frontend::VhdlSimulatorApi::file_path
            || api == frontend::VhdlSimulatorApi::file_line
            || api == frontend::VhdlSimulatorApi::get_vhdl_assert_format;
        if ((api == frontend::VhdlSimulatorApi::dir_separator
                && (expression.kind == ExpressionKind::Identifier
                    || (expression.kind == ExpressionKind::Call
                        && expression.operands.empty())))
            || (api == frontend::VhdlSimulatorApi::dir_workingdir
                && ((expression.kind == ExpressionKind::Call
                        && expression.operands.empty())
                    || expression.kind == ExpressionKind::Identifier))
            || environment_string) {
            return true;
        }
    }
    if (expression.kind == ExpressionKind::StringLiteral) {
        return true;
    }
    if (expression.kind == ExpressionKind::Identifier) {
        return string_locals_.contains(expression.text)
            || string_objects_.contains(expression.text)
            || (object_type(expression.text) != nullptr
                && object_type(expression.text)->domain
                    == frontend::ValueDomain::String);
    }
    if (expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2
        && container_expression_type(expression) == nullptr) {
        const auto* type = container_expression_type(expression.operands.front());
        return type != nullptr && type->systemverilog_container
            && type->systemverilog_container->element_types.size() == 1
            && type->systemverilog_container->element_types.front().domain
            == frontend::ValueDomain::String;
    }
    if (expression.kind == ExpressionKind::Concatenation
        || (expression.kind == ExpressionKind::Binary
            && expression.text == "&")) {
        return !expression.operands.empty()
            && std::ranges::all_of(
                expression.operands,
                [&](const Expression& operand) {
                    return is_string_expression(operand);
                });
    }
    if (expression.kind == ExpressionKind::Call) {
        if (language_ == frontend::Language::SystemVerilog2017
            && expression.text == "$typename") {
            return true;
        }
        if (expression.text == ".get_randstate"
            && expression.operands.size() == 1U
            && expression.operands.front().kind == ExpressionKind::Identifier) {
            const auto* receiver_type = object_type(
                expression.operands.front().text);
            if (receiver_type != nullptr
                && receiver_type->spelling == "process") {
                return true;
            }
        }
        if (language_ == frontend::Language::Vhdl2008) {
            const auto separator = expression.text.find_last_of('.');
            const auto name = std::string_view { expression.text }.substr(
                separator == std::string::npos ? 0 : separator + 1);
            if (name == "to_string" || name == "to_bstring"
                || name == "to_binary_string" || name == "to_ostring"
                || name == "to_octal_string" || name == "to_hstring"
                || name == "to_hex_string") {
                return true;
            }
        }
        if (expression.text == "$sformatf") {
            return true;
        }
        if (!expression.operands.empty()
            && (expression.text == ".toupper"
                || expression.text == ".tolower"
                || expression.text == ".substr")) {
            return is_string_expression(expression.operands.front());
        }
        const auto* function = visible_function(expression.text);
        return function != nullptr
            && function->return_type.domain
            == frontend::ValueDomain::String;
    }
    return false;
}

std::optional<StringRegisterId>
Lowerer::lower_string_expression(
    const Expression& expression)
{
    if (language_ == frontend::Language::Vhdl2008
        && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019
        && is_string_expression(expression)) {
        if (auto reflected = lower_vhdl_reflection_string_expression(expression)) {
            return reflected;
        }
    }
    if (language_ == frontend::Language::Vhdl2008
        && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
        if (const auto selected = vhdl_call_path_string_selection(expression);
            selected) {
            const auto source_type = vhdl_expression_type(*selected->container);
            if (!source_type
                || source_type->nominal_type
                    != "@builtin:std.env.call_path_vector") {
                report(
                    "FSIM-ELAB-VHENV-007",
                    "STD.ENV call-path field selection requires a CALL_PATH_VECTOR value",
                    expression.span);
                return std::nullopt;
            }
            auto index = lower_expression(*selected->index, 32U);
            const auto source = lower_container_expression(
                *selected->container);
            if (!index || !source) {
                return std::nullopt;
            }
            if (register_width(*index) != 32U) {
                *index = resize_register(*index, 32U, true);
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(ContainerStringRead {
                destination, *source, *index, true, false, false,
                { selected->member } });
            return destination;
        }
        if (const auto selected = vhdl_directory_string_selection(expression);
            selected) {
            const auto local = container_locals_.find(selected->object);
            const auto* type = object_type(selected->object);
            if (local == container_locals_.end() || type == nullptr
                || !frontend::is_vhdl_environment_directory(*type)) {
                report(
                    "FSIM-ELAB-VHENV-007",
                    "STD.ENV DIRECTORY selection requires a DIRECTORY variable",
                    expression.span);
                return std::nullopt;
            }
            auto index = allocate_register(32U, frontend::ValueDomain::Integer);
            if (selected->index == nullptr) {
                process_.operations.emplace_back(LoadConstant {
                    index, integer_value(0) });
            } else {
                auto source = lower_expression(*selected->index, 32U);
                if (!source) {
                    return std::nullopt;
                }
                if (register_width(*source) != 32U) {
                    *source = resize_register(*source, 32U, true);
                }
                const auto one = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(LoadConstant {
                    one, integer_value(1) });
                process_.operations.emplace_back(IntegerBinary {
                    IntegerBinaryOperator::add, index, *source, one });
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(ContainerStringRead {
                destination, local->second, index, true, false, false, { } });
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "@vhdl-dereference"
            && expression.operands.size() == 1U
            && expression.operands.front().kind == ExpressionKind::Identifier) {
            const auto& object = expression.operands.front();
            const auto local = string_locals_.find(object.text);
            const auto* type = object_type(object.text);
            const auto separator = type == nullptr
                ? std::string::npos : type->spelling.find_last_of('.');
            if (local != string_locals_.end() && type != nullptr
                && type->domain == frontend::ValueDomain::String
                && type->spelling.substr(separator == std::string::npos
                        ? 0U : separator + 1U) == "line") {
                return local->second;
            }
        }
        const auto api = frontend::vhdl_simulator_api(expression.text);
        if (api == frontend::VhdlSimulatorApi::get_vhdl_assert_format) {
            const bool names_valid = expression.call_argument_names.empty()
                || (expression.call_argument_names.size()
                        == expression.operands.size()
                    && expression.call_argument_names.size() == 1U
                    && (expression.call_argument_names.front().empty()
                        || expression.call_argument_names.front()
                            == "level"));
            if (expression.kind != ExpressionKind::Call
                || expression.operands.size() != 1U || !names_valid) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV GETVHDLASSERTFORMAT requires one SEVERITY_LEVEL actual",
                    expression.span);
                return std::nullopt;
            }
            frontend::Type severity;
            severity.spelling = "severity_level";
            severity.domain = frontend::ValueDomain::Bit2;
            severity.packed_range = frontend::PackedRange { 1, 0, true };
            severity.enumeration_literals = {
                "note", "warning", "error", "failure" };
            const auto level = lower_expression(
                expression.operands.front(), 2U, &severity);
            if (!level || register_width(*level) != 2U) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV GETVHDLASSERTFORMAT LEVEL must be SEVERITY_LEVEL",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(VhdlAssertApi {
                VhdlAssertApiKind::get_format,
                std::nullopt,
                destination,
                *level,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                SourceLocation {
                    expression.span.source_name.str(),
                    static_cast<std::uint32_t>(expression.span.begin.line),
                    static_cast<std::uint32_t>(expression.span.begin.column) } });
            return destination;
        }
        const bool environment_identity
            = api == frontend::VhdlSimulatorApi::vhdl_version
            || api == frontend::VhdlSimulatorApi::tool_type
            || api == frontend::VhdlSimulatorApi::tool_vendor
            || api == frontend::VhdlSimulatorApi::tool_name
            || api == frontend::VhdlSimulatorApi::tool_edition
            || api == frontend::VhdlSimulatorApi::tool_version;
        if (const auto* getenv = vhdl_environment_getenv_call(expression)) {
            const bool names_valid = getenv->call_argument_names.empty()
                || (getenv->call_argument_names.size() == 1U
                    && (getenv->call_argument_names.front().empty()
                        || getenv->call_argument_names.front() == "name"));
            if (getenv->kind != ExpressionKind::Call
                || getenv->operands.size() != 1U || !names_valid) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV GETENV requires one STRING NAME actual",
                    expression.span);
                return std::nullopt;
            }
            const auto name = lower_string_expression(
                getenv->operands.front());
            if (!name) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV GETENV NAME must be STRING-compatible",
                    getenv->operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(VhdlEnvironmentGetenv {
                destination, *name });
            return destination;
        }
        if (environment_identity) {
            if ((expression.kind != ExpressionKind::Identifier
                    && expression.kind != ExpressionKind::Call)
                || !expression.operands.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV environment identity functions do not accept arguments",
                    expression.span);
                return std::nullopt;
            }
            const auto value = api == frontend::VhdlSimulatorApi::vhdl_version
                ? std::string { "2019" }
                : api == frontend::VhdlSimulatorApi::tool_type
                ? std::string { "SIMULATION" }
                : api == frontend::VhdlSimulatorApi::tool_vendor
                ? std::string { "fsim project" }
                : api == frontend::VhdlSimulatorApi::tool_name
                ? std::string { "fsim" }
                : api == frontend::VhdlSimulatorApi::tool_edition
                ? std::string { "community" }
                : std::string { fsim::version };
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant { destination, value });
            return destination;
        }
        const bool source_identity
            = api == frontend::VhdlSimulatorApi::file_name
            || api == frontend::VhdlSimulatorApi::file_path
            || api == frontend::VhdlSimulatorApi::file_line;
        if (source_identity) {
            if ((expression.kind != ExpressionKind::Identifier
                    && expression.kind != ExpressionKind::Call)
                || !expression.operands.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV source-location functions do not accept arguments",
                    expression.span);
                return std::nullopt;
            }
            const auto source_path = std::filesystem::path {
                expression.span.source_name.str() }.lexically_normal();
            const auto value = api == frontend::VhdlSimulatorApi::file_name
                ? source_path.filename().generic_string()
                : api == frontend::VhdlSimulatorApi::file_path
                ? source_path.generic_string()
                : std::to_string(expression.span.begin.line);
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant { destination, value });
            return destination;
        }
        if (api == frontend::VhdlSimulatorApi::to_string
            && !expression.operands.empty()) {
            const auto unwrap_call_path = [](const Expression& value)
                -> const Expression* {
                if (frontend::vhdl_simulator_api(value.text)
                    == frontend::VhdlSimulatorApi::get_call_path) {
                    return &value;
                }
                if (value.kind == ExpressionKind::Call
                    && value.text == "@vhdl-dereference"
                    && value.operands.size() == 1U
                    && frontend::vhdl_simulator_api(
                           value.operands.front().text)
                        == frontend::VhdlSimulatorApi::get_call_path) {
                    return &value.operands.front();
                }
                return nullptr;
            };
            std::array<const Expression*, 2> actuals { nullptr, nullptr };
            bool valid = expression.operands.size() <= 2U
                && (expression.call_argument_names.empty()
                        || expression.call_argument_names.size()
                            == expression.operands.size());
            std::size_t positional = 0U;
            for (std::size_t index = 0;
                valid && index < expression.operands.size(); ++index) {
                const auto name = expression.call_argument_names.empty()
                    ? std::string_view { }
                    : std::string_view {
                          expression.call_argument_names[index] };
                std::size_t formal = positional;
                if (name.empty()) {
                    ++positional;
                } else if (name == "call_path") {
                    formal = 0U;
                } else if (name == "separator") {
                    formal = 1U;
                } else {
                    valid = false;
                    continue;
                }
                if (formal >= actuals.size()
                    || actuals[formal] != nullptr) {
                    valid = false;
                } else {
                    actuals[formal] = &expression.operands[index];
                }
            }
            const auto* call_path = valid && actuals[0] != nullptr
                ? unwrap_call_path(*actuals[0]) : nullptr;
            const auto call_path_type = valid && actuals[0] != nullptr
                ? vhdl_expression_type(*actuals[0]) : std::nullopt;
            const bool call_path_value = call_path_type
                && frontend::is_vhdl_environment_call_path_type(
                    *call_path_type);
            if (call_path != nullptr || call_path_value) {
                const bool element_value = call_path_type
                    && call_path_type->nominal_type
                        == "@builtin:std.env.call_path_element";
                valid = call_path == nullptr
                    || (call_path->operands.empty()
                    && (call_path->kind == ExpressionKind::Identifier
                        || call_path->kind == ExpressionKind::Call));
                if (!valid || actuals[0] == nullptr
                    || (element_value && actuals[1] != nullptr)) {
                    report(
                        "FSIM-ELAB-VHENV-003",
                        "STD.ENV TO_STRING call-path overload has no matching standardized association profile",
                        expression.span);
                    return std::nullopt;
                }
                std::optional<ContainerRegisterId> source_value;
                std::optional<RegisterId> source_index;
                if (element_value
                    && actuals[0]->kind == ExpressionKind::Index
                    && actuals[0]->operands.size() == 2U) {
                    source_value = lower_container_expression(
                        actuals[0]->operands.front());
                    source_index = lower_expression(
                        actuals[0]->operands[1], 32U);
                    if (source_index
                        && register_width(*source_index) != 32U) {
                        *source_index = resize_register(
                            *source_index, 32U, true);
                    }
                    if (!source_value || !source_index) {
                        return std::nullopt;
                    }
                } else if (call_path == nullptr) {
                    source_value = lower_container_expression(*actuals[0]);
                    if (!source_value) {
                        return std::nullopt;
                    }
                }
                std::optional<StringRegisterId> separator_value;
                if (actuals[1] != nullptr) {
                    separator_value = lower_string_expression(*actuals[1]);
                } else {
                    separator_value = allocate_string_register();
                    process_.operations.emplace_back(LoadStringConstant {
                        *separator_value, "\n" });
                }
                if (!separator_value) {
                    report(
                        "FSIM-ELAB-VHENV-004",
                        "STD.ENV TO_STRING SEPARATOR must be STRING-compatible",
                        actuals[1]->span);
                    return std::nullopt;
                }
                const auto destination = allocate_string_register();
                process_.operations.emplace_back(VhdlEnvironmentCallPath {
                    destination,
                    *separator_value,
                    source_value,
                    source_index,
                    SourceLocation {
                        expression.span.source_name.str(),
                        static_cast<std::uint32_t>(
                            expression.span.begin.line),
                        static_cast<std::uint32_t>(
                            expression.span.begin.column) },
                    debug_scope_name() });
                return destination;
            }
        }
        const bool separator = api
                == frontend::VhdlSimulatorApi::dir_separator
            && (expression.kind == ExpressionKind::Identifier
                || (expression.kind == ExpressionKind::Call
                    && expression.operands.empty()));
        const bool working = api
                == frontend::VhdlSimulatorApi::dir_workingdir
            && ((expression.kind == ExpressionKind::Call
                    && expression.operands.empty())
                || expression.kind == ExpressionKind::Identifier);
        if (separator || working) {
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(VhdlEnvironmentDirectory {
                separator
                    ? VhdlEnvironmentDirectoryKind::separator
                    : VhdlEnvironmentDirectoryKind::get_working_directory,
                std::nullopt, destination, std::nullopt,
                std::nullopt, std::nullopt });
            return destination;
        }
    }
    if (expression.kind == ExpressionKind::StringLiteral) {
        if (!expression.decoded_string) {
            report(
                "FSIM-ELAB-SVSTRING-006",
                "string literal has no decoded byte value",
                expression.span);
            return std::nullopt;
        }
        if (expression.decoded_string->size()
            > maximum_string_bytes) {
            report(
                "FSIM-ELAB-SVSTRING-007",
                "string literal exceeds the 4096-byte limit",
                expression.span);
            return std::nullopt;
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(
            LoadStringConstant {
                destination, *expression.decoded_string });
        return destination;
    }
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto local = string_locals_.find(expression.text);
            local != string_locals_.end()) {
            return local->second;
        }
        if (const auto object = string_objects_.find(expression.text);
            object != string_objects_.end()) {
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(
                ReadStringObject { destination, object->second });
            return destination;
        }
        report(
            "FSIM-ELAB-SVSTRING-008",
            "unknown string object '" + expression.text + "'",
            expression.span);
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2
        && is_string_expression(expression)) {
        const auto* type = container_expression_type(expression.operands.front());
        if (type == nullptr || !type->systemverilog_container) {
            return std::nullopt;
        }
        const auto rank = type->systemverilog_container->static_range_expressions.size();
        std::size_t selected_dimensions { };
        for (const Expression* selected = &expression;
            selected->kind == ExpressionKind::Index
            && selected->operands.size() == 2;
            selected = &selected->operands.front()) {
            ++selected_dimensions;
        }
        if (rank > 1 && selected_dimensions == rank) {
            const Expression* base = &expression;
            while (base->kind == ExpressionKind::Index
                && base->operands.size() == 2) {
                base = &base->operands.front();
            }
            const auto source = lower_container_expression(*base);
            const auto index = lower_multidimensional_index(expression, *type);
            if (!source || !index) {
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(ContainerStringRead {
                destination, *source, *index, true, true, false, { } });
            return destination;
        }
        const auto runtime_type = container_expression_runtime_type(expression.operands.front());
        const auto source = lower_container_expression(expression.operands.front());
        if (!runtime_type || !source
            || runtime_type->element_kind
                != ContainerElementKind::String) {
            return std::nullopt;
        }
        const auto index_width = runtime_type->associative
            ? static_cast<std::size_t>(runtime_type->index_width)
            : runtime_type->fixed
            ? std::size_t { 32 }
            : infer_width(expression.operands[1]).value_or(32U);
        const bool string_index = runtime_type->associative
            && runtime_type->string_indices;
        auto index = string_index
            ? lower_string_expression(expression.operands[1])
            : lower_expression(
                  expression.operands[1], index_width,
                  runtime_type->associative
                      ? type->systemverilog_container
                            ->associative_index_type.get()
                      : nullptr);
        if (!index) {
            return std::nullopt;
        }
        if (runtime_type->associative
            && !string_index
            && register_width(*index) != runtime_type->index_width) {
            *index = resize_register(
                *index, runtime_type->index_width,
                runtime_type->signed_indices);
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(ContainerStringRead {
            destination, *source, *index,
            runtime_type->associative
                ? runtime_type->signed_indices
                : runtime_type->fixed
                    || is_signed_expression(expression.operands[1]),
            false,
            string_index,
            { } });
        return destination;
    }
    if (expression.kind == ExpressionKind::Concatenation
        || (expression.kind == ExpressionKind::Binary
            && expression.text == "&")) {
        if (expression.operands.empty()) {
            report(
                "FSIM-ELAB-SVSTRING-009",
                "string concatenation requires at least one operand",
                expression.span);
            return std::nullopt;
        }
        std::vector<StringRegisterId> operands;
        operands.reserve(expression.operands.size());
        for (const auto& operand : expression.operands) {
            const auto value = lower_string_expression(operand);
            if (!value) {
                report(
                    "FSIM-ELAB-SVSTRING-010",
                    "string concatenation requires string operands",
                    operand.span);
                return std::nullopt;
            }
            operands.push_back(*value);
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(
            ConcatenateStrings {
                destination, std::move(operands) });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call) {
        if (language_ == frontend::Language::Vhdl2008
            && frontend::vhdl_simulator_api(expression.text)
                == frontend::VhdlSimulatorApi::to_string) {
            if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
                || expression.operands.empty()
                || expression.operands.size() > 2U) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV TO_STRING requires VHDL-2019, one TIME_RECORD, and an optional FRAC_DIGITS",
                    expression.span);
                return std::nullopt;
            }
            std::array<const Expression*, 2> actuals { nullptr, nullptr };
            std::size_t positional = 0U;
            bool valid = true;
            for (std::size_t index = 0;
                index < expression.operands.size(); ++index) {
                const auto name = expression.call_argument_names.empty()
                    ? std::string_view { }
                    : std::string_view {
                          expression.call_argument_names[index] };
                std::size_t formal = 0U;
                if (name.empty()) {
                    formal = positional++;
                } else if (name == "trec") {
                    formal = 0U;
                } else if (name == "frac_digits") {
                    formal = 1U;
                } else {
                    valid = false;
                    continue;
                }
                if (formal >= actuals.size() || actuals[formal] != nullptr) {
                    valid = false;
                    continue;
                }
                actuals[formal] = &expression.operands[index];
            }
            if (!valid || actuals[0] == nullptr) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV TO_STRING has no matching standardized association profile",
                    expression.span);
                return std::nullopt;
            }
            const auto record_type =
                frontend::vhdl_environment_time_record_type();
            const auto actual_type = vhdl_expression_type(*actuals[0]);
            if (!actual_type
                || !frontend::is_vhdl_environment_time_record(
                    *actual_type)) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV TO_STRING TREC must have type TIME_RECORD",
                    actuals[0]->span);
                return std::nullopt;
            }
            const auto record = lower_expression(
                *actuals[0], 515U, &record_type);
            auto integer_type = frontend::vhdl_predefined_integer_type(
                frontend::VhdlStandard::Vhdl2019, "integer");
            integer_type.integer_range = frontend::IntegerRange {
                0, 6, false };
            std::optional<RegisterId> fractional_digits;
            if (actuals[1] != nullptr) {
                fractional_digits = lower_expression(
                    *actuals[1], 64U, &integer_type);
            } else {
                fractional_digits = allocate_register(
                    64U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(LoadConstant {
                    *fractional_digits,
                    PackedLogic4::from_aval_bval(64U, 0U, 0U) });
            }
            if (!record || !fractional_digits) {
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(
                VhdlEnvironmentTimeToString {
                    destination, *record, *fractional_digits });
            return destination;
        }
        if (expression.text == "$typename") {
            if (language_ != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1U) {
                report(
                    "FSIM-ELAB-SVTYPENAME-001",
                    "$typename requires exactly one SystemVerilog expression or type",
                    expression.span);
                return std::nullopt;
            }
            const auto& operand = expression.operands.front();
            const auto* type_mark = operand.kind == ExpressionKind::Identifier
                ? visible_type_mark(operand.text)
                : nullptr;
            const auto* type = type_mark != nullptr
                ? type_mark
                : systemverilog_expression_type(operand);
            std::string text;
            if (type_mark != nullptr) {
                text = operand.text;
            } else if (type != nullptr && !type->nominal_type.empty()) {
                const auto nominal = std::ranges::find_if(
                    visible_type_marks_,
                    [&](const auto& entry) {
                        return entry.second != nullptr
                            && entry.second->nominal_type
                            == type->nominal_type;
                    });
                text = nominal != visible_type_marks_.end()
                    ? nominal->first
                    : systemverilog_type_name(*type);
            } else if (type != nullptr) {
                text = systemverilog_type_name(*type);
            } else if (is_string_expression(operand)) {
                text = "string";
            } else {
                switch (operand.systemverilog_scalar_kind) {
                case frontend::SystemVerilogScalarKind::ShortReal:
                    text = "shortreal";
                    break;
                case frontend::SystemVerilogScalarKind::Real:
                    text = "real";
                    break;
                case frontend::SystemVerilogScalarKind::Realtime:
                    text = "realtime";
                    break;
                case frontend::SystemVerilogScalarKind::Time:
                    text = "time";
                    break;
                case frontend::SystemVerilogScalarKind::Chandle:
                    text = "chandle";
                    break;
                case frontend::SystemVerilogScalarKind::None:
                    break;
                }
                if (text.empty()
                    && operand.kind == ExpressionKind::IntegerLiteral) {
                    text = "int";
                } else if (text.empty()
                    && operand.kind == ExpressionKind::StringLiteral) {
                    text = "string";
                } else if (text.empty()) {
                    const auto width = infer_width(operand);
                    if (width && *width != 0U) {
                        text = "logic";
                        if (is_signed_expression(operand)) {
                            text += " signed";
                        }
                        if (*width != 1U) {
                            text += "[" + std::to_string(*width - 1U)
                                + ":0]";
                        }
                    }
                }
            }
            if (text.empty() || text.size() > maximum_string_bytes) {
                report(
                    "FSIM-ELAB-SVTYPENAME-001",
                    "$typename argument has no bounded statically known SystemVerilog type name",
                    operand.span);
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant { destination, std::move(text) });
            return destination;
        }
        if (expression.text == ".get_randstate"
            && expression.operands.size() == 1U
            && expression.operands.front().kind == ExpressionKind::Identifier
            && object_type(expression.operands.front().text) != nullptr
            && object_type(expression.operands.front().text)->spelling
                == "process") {
            const auto& receiver = expression.operands.front();
            const auto source = lower_expression(
                receiver, 64, object_type(receiver.text));
            if (!source || register_width(*source) != 64U) {
                report(
                    "FSIM-ELAB-SVPROCESS-001",
                    "process random-state receiver has no 64-bit handle value",
                    receiver.span);
                return std::nullopt;
            }
            const auto destination = allocate_string_register();
            process_.operations.emplace_back(
                ProcessGetRandState { destination, *source });
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008) {
            const auto separator = expression.text.find_last_of('.');
            const auto name = std::string_view { expression.text }.substr(
                separator == std::string::npos ? 0 : separator + 1);
            const bool binary = name == "to_string" || name == "to_bstring"
                || name == "to_binary_string";
            const bool octal = name == "to_ostring" || name == "to_octal_string";
            const bool hexadecimal = name == "to_hstring" || name == "to_hex_string";
            if (binary || octal || hexadecimal) {
                if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2008) {
                    report(
                        "FSIM-ELAB-VHSTD-001",
                        "predefined IEEE function '"
                            + std::string { name }
                            + "' requires VHDL-2008, but this process uses "
                              "VHDL-"
                            + std::string { frontend::to_string(
                                vhdl_standard_) },
                        expression.span);
                    return std::nullopt;
                }
                if (expression.operands.size() != 1) {
                    report(
                        "FSIM-ELAB-VHLOGIC-001",
                        std::string { name } + " requires exactly one packed value",
                        expression.span);
                    return std::nullopt;
                }
                const auto width = infer_width(expression.operands.front());
                const auto value = width
                    ? literal_value(
                          expression.operands.front(),
                          *width,
                          frontend::Language::Vhdl2008)
                    : std::nullopt;
                if (!width || *width == 0 || !value) {
                    report(
                        "FSIM-ELAB-VHLOGIC-003",
                        std::string { name }
                            + " requires a nonempty static packed value",
                        expression.operands.front().span);
                    return std::nullopt;
                }
                const auto group = octal ? 3U : 4U;
                const auto output_bytes = binary
                    ? *width
                    : (*width + group - 1U) / group;
                if (output_bytes > maximum_string_bytes) {
                    report(
                        "FSIM-ELAB-VHLOGIC-003",
                        std::string { name }
                            + " result exceeds the bounded runtime string storage limit",
                        expression.operands.front().span);
                    return std::nullopt;
                }
                std::string text;
                if (binary) {
                    text = value->value.to_msb_string();
                } else {
                    const auto digits = (*width + group - 1U) / group;
                    text.assign(digits, '0');
                    for (std::size_t digit = 0; digit < digits; ++digit) {
                        unsigned numeric = 0;
                        for (std::size_t bit = 0; bit < group; ++bit) {
                            const auto offset = digit * group + bit;
                            if (offset >= *width) {
                                continue;
                            }
                            const auto state = value->value.is_logic9()
                                ? runtime::to_logic4(value->value.get_logic9(offset))
                                : value->value.get(offset);
                            if (state != runtime::Logic4::zero
                                && state != runtime::Logic4::one) {
                                report(
                                    "FSIM-ELAB-VHLOGIC-003",
                                    std::string { name }
                                        + " static octal/hex profile requires only 0/1 states",
                                    expression.operands.front().span);
                                return std::nullopt;
                            }
                            if (state == runtime::Logic4::one) {
                                numeric |= 1U << bit;
                            }
                        }
                        constexpr std::string_view digits_text { "0123456789ABCDEF" };
                        text[digits - digit - 1U] = digits_text[numeric];
                    }
                }
                const auto destination = allocate_string_register();
                process_.operations.emplace_back(
                    LoadStringConstant { destination, std::move(text) });
                return destination;
            }
        }
        if (expression.text == "$sformatf") {
            return lower_string_format(
                expression.operands, 0U, expression.text, expression.span);
        }
        if ((expression.text == ".toupper"
                || expression.text == ".tolower"
                || expression.text == ".substr")
            && !expression.operands.empty()
            && is_string_expression(expression.operands.front())) {
            const auto expected = expression.text == ".substr" ? 3U : 1U;
            if (expression.operands.size() != expected) {
                report(
                    "FSIM-ELAB-SVSTRING-018",
                    "runtime string method '" + expression.text
                        + "' has an incompatible argument count",
                    expression.span);
                return std::nullopt;
            }
            const auto source = lower_string_expression(expression.operands.front());
            if (!source) {
                return std::nullopt;
            }
            StringMethod method;
            method.operation = expression.text == ".toupper"
                ? StringMethodOperator::toupper
                : expression.text == ".tolower"
                ? StringMethodOperator::tolower
                : StringMethodOperator::substr;
            method.source = *source;
            method.string_destination = allocate_string_register();
            if (expression.text == ".substr") {
                auto first = lower_expression(expression.operands[1], 32);
                auto second = lower_expression(expression.operands[2], 32);
                if (!first || !second) {
                    report(
                        "FSIM-ELAB-SVSTRING-018",
                        "substr indices must be signed 32-bit integral values",
                        expression.span);
                    return std::nullopt;
                }
                if (register_width(*first) != 32) {
                    *first = resize_register(
                        *first, 32,
                        is_signed_expression(expression.operands[1]));
                }
                if (register_width(*second) != 32) {
                    *second = resize_register(
                        *second, 32,
                        is_signed_expression(expression.operands[2]));
                }
                method.first = *first;
                method.second = *second;
            }
            process_.operations.emplace_back(method);
            return method.string_destination;
        }
        const auto selected = select_function_overload(
            expression, nullptr, FunctionResultKind::String);
        if (!function_support_initialized_ || !selected.named) {
            report(
                "FSIM-ELAB-SVSTRING-011",
                "unknown runtime string function '"
                    + expression.text + "'",
                expression.span);
            return std::nullopt;
        }
        if (!selected.index) {
            return std::nullopt;
        }
        const auto function_index = *selected.index;
        auto& frame = function_frames_[function_index];
        const auto& function = *frame.source;
        if (function.return_type.domain
            != frontend::ValueDomain::String) {
            report(
                "FSIM-ELAB-SVSTRING-011",
                "string function call has an incompatible result or argument "
                "count",
                expression.span);
            return std::nullopt;
        }
        const auto actuals = bind_function_actuals(expression, function);
        if (!actuals
            || !validate_function_reference_actuals(function, *actuals)) {
            return std::nullopt;
        }
        if (!frame.allocated) {
            frame.result_is_string = true;
            frame.string_result = allocate_string_register();
            frame.arguments.reserve(function.arguments.size());
            frame.string_arguments.reserve(function.arguments.size());
            frame.container_arguments.reserve(function.arguments.size());
            frame.argument_is_string.reserve(function.arguments.size());
            frame.argument_is_container.reserve(function.arguments.size());
            for (const auto& argument : function.arguments) {
                if (argument.type.systemverilog_container) {
                    const auto type = container_type(argument.type, argument.span);
                    if (!type) {
                        return std::nullopt;
                    }
                    frame.arguments.push_back({ });
                    frame.string_arguments.push_back({ });
                    frame.container_arguments.push_back(
                        allocate_container_register(*type));
                    frame.argument_is_string.push_back(false);
                    frame.argument_is_container.push_back(true);
                    continue;
                }
                if (argument.type.domain
                    == frontend::ValueDomain::String) {
                    frame.arguments.push_back({ });
                    frame.string_arguments.push_back(
                        allocate_string_register());
                    frame.container_arguments.push_back({ });
                    frame.argument_is_string.push_back(true);
                    frame.argument_is_container.push_back(false);
                    continue;
                }
                const auto width = argument.type.width();
                if (!width || *width == 0) {
                    report(
                        "FSIM-ELAB-SVFUNC-004",
                        "function argument '" + argument.name
                            + "' must be a string or have a positive executable "
                              "packed width",
                        argument.span);
                    return std::nullopt;
                }
                frame.arguments.push_back(
                    allocate_register(*width, argument.type.domain));
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back({ });
                frame.argument_is_string.push_back(false);
                frame.argument_is_container.push_back(false);
            }
            if (function.automatic) {
                frame.invocation_strings.push_back(frame.string_result);
                for (std::size_t index = 0;
                    index < function.arguments.size(); ++index) {
                    if (frame.argument_is_container[index]) {
                        frame.invocation_containers.push_back(
                            frame.container_arguments[index]);
                    } else if (frame.argument_is_string[index]) {
                        frame.invocation_strings.push_back(
                            frame.string_arguments[index]);
                    } else {
                        frame.invocation_packed.push_back(frame.arguments[index]);
                    }
                }
            }
            frame.allocated = true;
        }
        std::vector<Expression> copy_out_targets(function.arguments.size());
        std::vector<RegisterId> packed_actuals(function.arguments.size());
        std::vector<StringRegisterId> string_actuals(function.arguments.size());
        std::vector<ContainerRegisterId> container_actuals(
            function.arguments.size());
        for (std::size_t index = 0;
            index < function.arguments.size(); ++index) {
            const auto& formal = function.arguments[index];
            if (formal.direction != frontend::PortDirection::Input) {
                auto target = capture_callable_copy_out_target(
                    *(*actuals)[index],
                    "@function_target_" + std::to_string(function_index)
                        + "_" + std::to_string(index)
                        + "_" + std::to_string(process_.operations.size()));
                if (!target) {
                    return std::nullopt;
                }
                copy_out_targets[index] = std::move(*target);
            }
            if (formal.direction == frontend::PortDirection::Output) {
                if (frame.argument_is_container[index]
                    || frame.argument_is_string[index]) {
                    report(
                        "FSIM-ELAB-SVFUNC-011",
                        "function output/inout/ref formals require a bounded packed "
                        "integral type",
                        formal.span);
                    return std::nullopt;
                }
                packed_actuals[index] = allocate_register(
                    static_cast<std::size_t>(*formal.type.width()),
                    formal.type.domain);
                continue;
            }
            if (frame.argument_is_container[index]) {
                const auto formal_type = container_type(
                    formal.type, formal.span);
                if (!formal_type) {
                    return std::nullopt;
                }
                const auto actual = formal_type->fixed
                    ? lower_static_container_assignment_value(
                          *(*actuals)[index], *formal_type)
                    : lower_container_expression(*(*actuals)[index]);
                if (!actual) {
                    return std::nullopt;
                }
                if (process_.container_register_types.at(*actual)
                    != *formal_type) {
                    report(
                        "FSIM-ELAB-SVFUNC-009",
                        "function container arguments require an exactly "
                        "compatible kind and profile",
                        (*actuals)[index]->span);
                    return std::nullopt;
                }
                container_actuals[index] = allocate_container_register(*formal_type);
                process_.operations.emplace_back(CopyContainerRegister {
                    container_actuals[index], *actual });
                continue;
            }
            if (frame.argument_is_string[index]) {
                const auto actual = lower_string_expression(*(*actuals)[index]);
                if (!actual) {
                    return std::nullopt;
                }
                string_actuals[index] = allocate_string_register();
                process_.operations.emplace_back(CopyStringRegister {
                    string_actuals[index], *actual });
                continue;
            }
            const auto width = static_cast<std::size_t>(*formal.type.width());
            auto actual = lower_expression(
                *(*actuals)[index], width, &formal.type);
            if (!actual) {
                return std::nullopt;
            }
            if (register_width(*actual) != width) {
                *actual = resize_register(
                    *actual,
                    width,
                    is_signed_expression(*(*actuals)[index]));
            }
            packed_actuals[index] = allocate_register(width, formal.type.domain);
            process_.operations.emplace_back(
                CopyRegister { packed_actuals[index], *actual });
        }
        if (function.automatic) {
            const auto push_site = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(CallableFramePush {
                frame.invocation_identity,
                frame.invocation_packed,
                frame.invocation_strings,
                frame.invocation_containers,
                true });
            if (!frame.invocation_layout_finalized) {
                frame.invocation_push_sites.push_back(push_site);
            }
        }
        for (std::size_t index = 0;
            index < function.arguments.size(); ++index) {
            const auto& formal = function.arguments[index];
            if (formal.direction == frontend::PortDirection::Output) {
                const auto width = static_cast<std::size_t>(
                    *formal.type.width());
                process_.operations.emplace_back(LoadConstant {
                    frame.arguments[index],
                    default_packed_value(formal.type, width) });
            } else if (frame.argument_is_container[index]) {
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index], container_actuals[index] });
            } else if (frame.argument_is_string[index]) {
                process_.operations.emplace_back(CopyStringRegister {
                    frame.string_arguments[index], string_actuals[index] });
            } else {
                process_.operations.emplace_back(CopyRegister {
                    frame.arguments[index], packed_actuals[index] });
            }
        }
        process_.operations.emplace_back(
            LoadStringConstant { frame.string_result, { } });
        const auto call_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(
            Call {
                frame.target.value_or(0),
                static_cast<InstructionIndex>(call_site + 1U),
                function_call_stack_ });
        if (frame.target) {
            fsim::runtime::simir::operation_get<Call>(
                process_.operations[call_site])
                .target = *frame.target;
        } else {
            frame.call_sites.push_back(call_site);
        }
        if (!frame.queued && !frame.lowered) {
            frame.queued = true;
            pending_functions_.push_back(function_index);
        }
        if (active_function_) {
            function_dependencies_[*active_function_].insert(
                function_index);
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(
            CopyStringRegister {
                destination, frame.string_result });
        for (std::size_t index = 0;
            index < function.arguments.size(); ++index) {
            if (function.arguments[index].direction
                == frontend::PortDirection::Input) {
                continue;
            }
            if (frame.argument_is_container[index]) {
                process_.operations.emplace_back(CopyContainerRegister {
                    container_actuals[index], frame.container_arguments[index] });
            } else if (frame.argument_is_string[index]) {
                process_.operations.emplace_back(CopyStringRegister {
                    string_actuals[index], frame.string_arguments[index] });
            } else {
                process_.operations.emplace_back(CopyRegister {
                    packed_actuals[index], frame.arguments[index] });
            }
        }
        if (function.automatic) {
            std::vector<RegisterId> preserve_packed;
            std::vector<StringRegisterId> preserve_strings { destination };
            std::vector<ContainerRegisterId> preserve_containers;
            for (std::size_t index = 0;
                index < function.arguments.size(); ++index) {
                if (function.arguments[index].direction
                    == frontend::PortDirection::Input) {
                    continue;
                }
                if (frame.argument_is_container[index]) {
                    preserve_containers.push_back(container_actuals[index]);
                } else if (frame.argument_is_string[index]) {
                    preserve_strings.push_back(string_actuals[index]);
                } else {
                    preserve_packed.push_back(packed_actuals[index]);
                }
            }
            process_.operations.emplace_back(CallableFramePop {
                frame.invocation_identity,
                std::move(preserve_packed),
                std::move(preserve_strings),
                std::move(preserve_containers) });
        }
        for (std::size_t index = 0;
            index < function.arguments.size(); ++index) {
            const auto& formal = function.arguments[index];
            if (formal.direction == frontend::PortDirection::Input) {
                continue;
            }
            lower_callable_copy_out(
                copy_out_targets[index],
                formal.type,
                packed_actuals[index],
                string_actuals[index],
                container_actuals[index],
                frame.argument_is_string[index],
                frame.argument_is_container[index],
                "@function_copyout_" + std::to_string(function_index)
                    + "_" + std::to_string(index));
        }
        return destination;
    }
    report(
        "FSIM-ELAB-SVSTRING-010",
        "expression does not produce a runtime string value",
        expression.span);
    return std::nullopt;
}

bool Lowerer::lower_string_method_statement(
    const Statement& statement)
{
    const auto& call = statement.value;
    if (call.kind != ExpressionKind::Call
        || (call.text != ".putc" && call.text != ".itoa"
            && call.text != ".hextoa" && call.text != ".octtoa"
            && call.text != ".bintoa" && call.text != ".realtoa")) {
        return false;
    }
    if (language_ != frontend::Language::SystemVerilog2017
        || call.operands.empty()
        || call.operands.front().kind != ExpressionKind::Identifier
        || !is_string_expression(call.operands.front())) {
        report(
            "FSIM-ELAB-SVSTRING-018",
            "mutating string methods require a direct writable string object",
            call.span);
        return true;
    }
    if (const auto object = string_objects_.find(call.operands.front().text);
        object != string_objects_.end()
        && read_only_string_objects_.contains(object->second)) {
        report(
            "FSIM-ELAB-SVPORT-011",
            "an input mutable string port is read-only",
            call.operands.front().span);
        return true;
    }
    const auto expected = call.text == ".putc" ? 3U : 2U;
    if (call.operands.size() != expected) {
        report(
            "FSIM-ELAB-SVSTRING-018",
            "runtime string method '" + call.text
                + "' has an incompatible argument count",
            call.span);
        return true;
    }
    const auto target = lower_string_expression(call.operands.front());
    const auto first_width = call.text == ".realtoa" ? 64U : 32U;
    auto first = lower_expression(call.operands[1], first_width);
    if (!target || !first) {
        report(
            "FSIM-ELAB-SVSTRING-018",
            call.text == ".realtoa"
                ? "realtoa argument must be a 64-bit real value"
                : "mutating string method argument must be a 32-bit integral value",
            call.operands[1].span);
        return true;
    }
    if (register_width(*first) != first_width) {
        *first = resize_register(
            *first, first_width, is_signed_expression(call.operands[1]));
    }
    StringMethod method;
    method.source = *target;
    method.first = *first;
    if (call.text == ".putc") {
        auto character = lower_expression(call.operands[2], 32);
        if (!character) {
            report(
                "FSIM-ELAB-SVSTRING-018",
                "putc character must be a 32-bit integral byte value",
                call.operands[2].span);
            return true;
        }
        if (register_width(*character) != 32) {
            *character = resize_register(
                *character, 32,
                is_signed_expression(call.operands[2]));
        }
        method.operation = StringMethodOperator::putc;
        method.second = *character;
    } else {
        method.operation = call.text == ".itoa"
            ? StringMethodOperator::itoa
            : call.text == ".hextoa"
            ? StringMethodOperator::hextoa
            : call.text == ".octtoa"
            ? StringMethodOperator::octtoa
            : call.text == ".bintoa"
            ? StringMethodOperator::bintoa
            : StringMethodOperator::realtoa;
    }
    process_.operations.emplace_back(method);
    if (const auto object = string_objects_.find(call.operands.front().text);
        object != string_objects_.end()) {
        process_.operations.emplace_back(
            WriteStringObject { object->second, *target });
    }
    return true;
}

} // namespace fsim::elaboration

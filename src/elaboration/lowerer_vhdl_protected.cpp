// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    struct ProtectedName {
        std::string object;
        std::string method;
    };

    std::optional<ProtectedName> protected_name(
        const std::string_view selected)
    {
        const auto separator = selected.find_last_of('.');
        if (separator == std::string_view::npos
            || separator == 0 || separator + 1 >= selected.size()) {
            return std::nullopt;
        }
        return ProtectedName {
            std::string { selected.substr(0, separator) },
            std::string { selected.substr(separator + 1) }
        };
    }

    bool contains_call(
        const std::vector<frontend::Statement>& statements)
    {
        for (const auto& statement : statements) {
            if (statement.kind == frontend::StatementKind::ProcedureCall
                || contains_call(statement.statements)
                || contains_call(statement.else_statements)) {
                return true;
            }
            for (const auto& alternative : statement.case_alternatives) {
                if (contains_call(alternative.statements)) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::string reflection_simple_name(
        const frontend::Type& type)
    {
        auto name = !type.vhdl_type_declaration.empty()
            ? type.vhdl_type_declaration
            : !type.nominal_type.empty() ? type.nominal_type : type.spelling;
        const auto separator = name.find_last_of(".:@");
        if (separator != std::string::npos) {
            name.erase(0U, separator + 1U);
        }
        return name;
    }

    [[nodiscard]] VhdlReflectionType reflection_type(
        const frontend::Type& source, const std::uint64_t offset = 0U)
    {
        VhdlReflectionType result;
        result.simple_name = reflection_simple_name(source);
        result.signed_value = source.is_signed;
        result.lsb_offset = offset;
        if (const auto width = source.width(); width
            && *width <= std::numeric_limits<std::uint32_t>::max()) {
            result.packed_width = static_cast<std::uint32_t>(*width);
        }
        if (!source.enumeration_literals.empty()) {
            result.type_class = VhdlReflectionClass::enumeration;
            result.names = source.enumeration_literals;
            const auto range = source.enumeration_range.value_or(
                frontend::EnumerationRange { 0,
                    static_cast<std::int64_t>(source.enumeration_literals.size()) - 1,
                    false });
            result.ranges.push_back(
                { range.left, range.right, !range.descending });
        } else if (source.vhdl_physical) {
            result.type_class = VhdlReflectionClass::physical;
            const auto range = source.vhdl_physical->resolved_range
                .value_or(frontend::IntegerRange {
                    std::numeric_limits<std::int64_t>::min(),
                    std::numeric_limits<std::int64_t>::max(), false });
            result.ranges.push_back(
                { range.left, range.right, !range.descending });
            for (const auto& unit : source.vhdl_physical->units) {
                result.names.push_back(unit.name);
                result.scales.push_back(static_cast<std::uint64_t>(
                    unit.scale_factor.value_or(1)));
            }
        } else if (!source.packed_members.empty()) {
            result.type_class = VhdlReflectionClass::record;
            for (const auto& member : source.packed_members) {
                result.names.push_back(member.name);
                frontend::Type member_type;
                if (!member.nested_types.empty()) {
                    member_type = member.nested_types.front();
                } else {
                    member_type.domain = member.domain;
                    member_type.spelling = member.spelling;
                    member_type.packed_range = member.packed_range;
                    member_type.is_signed = member.is_signed;
                }
                result.children.push_back(
                    reflection_type(member_type, member.lsb_offset));
            }
        } else if (source.vhdl_array) {
            result.type_class = VhdlReflectionClass::array;
            for (const auto& dimension : source.vhdl_array->dimensions) {
                if (dimension.range) {
                    result.ranges.push_back({ dimension.range->left,
                        dimension.range->right,
                        !dimension.range->descending });
                }
            }
            if (!source.vhdl_array->element_types.empty()) {
                result.children.push_back(
                    reflection_type(source.vhdl_array->element_types.front()));
            }
        } else if (source.vhdl_access) {
            result.type_class = VhdlReflectionClass::access;
            result.packed_width = source.vhdl_access->handle_width;
            if (!source.vhdl_access->designated_types.empty()) {
                result.children.push_back(reflection_type(
                    source.vhdl_access->designated_types.front()));
            }
        } else if (source.vhdl_file) {
            result.type_class = VhdlReflectionClass::file;
            result.packed_width = 32U;
            if (!source.vhdl_file->element_types.empty()) {
                result.children.push_back(
                    reflection_type(source.vhdl_file->element_types.front()));
            }
        } else if (source.vhdl_protected) {
            result.type_class = VhdlReflectionClass::protected_type;
            result.packed_width = 0U;
        } else if (source.spelling == "real"
            || source.systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Real) {
            result.type_class = VhdlReflectionClass::floating;
            result.packed_width = 64U;
        } else {
            result.type_class = VhdlReflectionClass::integer;
            const auto range = source.integer_range.value_or(
                frontend::IntegerRange {
                    std::numeric_limits<std::int64_t>::min(),
                    std::numeric_limits<std::int64_t>::max(), false });
            result.ranges.push_back(
                { range.left, range.right, !range.descending });
        }
        return result;
    }

    [[nodiscard]] bool reflection_mirror_type(const frontend::Type* type)
    {
        if (type == nullptr) {
            return false;
        }
        const auto name = reflection_simple_name(*type);
        return name == "value_mirror" || name == "subtype_mirror"
            || name.ends_with("_value_mirror")
            || name.ends_with("_subtype_mirror");
    }

    [[nodiscard]] std::optional<VhdlReflectionClass>
    reflection_conversion_class(const std::string_view method)
    {
        if (method == "to_enumeration") return VhdlReflectionClass::enumeration;
        if (method == "to_integer") return VhdlReflectionClass::integer;
        if (method == "to_floating") return VhdlReflectionClass::floating;
        if (method == "to_physical") return VhdlReflectionClass::physical;
        if (method == "to_record") return VhdlReflectionClass::record;
        if (method == "to_array") return VhdlReflectionClass::array;
        if (method == "to_access") return VhdlReflectionClass::access;
        if (method == "to_file") return VhdlReflectionClass::file;
        if (method == "to_protected") return VhdlReflectionClass::protected_type;
        return std::nullopt;
    }

    [[nodiscard]] SourceLocation reflection_location(
        const frontend::SourceSpan& span)
    {
        return SourceLocation { span.source_name.str(),
            static_cast<std::uint32_t>(span.begin.line),
            static_cast<std::uint32_t>(span.begin.column) };
    }

} // namespace

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_reflection_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
        return ExpressionAttempt { };
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "'reflect"
        && expression.operands.size() == 1U
        && expression.operands.front().kind == ExpressionKind::Identifier) {
        const auto& prefix = expression.operands.front();
        const auto* object = object_type(prefix.text);
        const auto* type_mark = visible_type_mark(prefix.text);
        const bool value_mirror = object != nullptr;
        const auto* reflected = value_mirror ? object : type_mark;
        if (reflected == nullptr || expected_width != 32U
            || !reflection_mirror_type(expected_type)) {
            report("FSIM-ELAB-VHREFLECT-001",
                "'reflect requires a resolved VHDL type or object and a matching reflection mirror context",
                expression.span);
            return ExpressionAttempt { std::nullopt };
        }
        auto descriptor = reflection_type(*reflected);
        std::optional<RegisterId> source;
        if (value_mirror && descriptor.packed_width != 0U) {
            source = lower_expression(prefix, descriptor.packed_width, reflected);
            if (!source) return ExpressionAttempt { std::nullopt };
        }
        std::optional<ContainerRegisterId> access_heap;
        if (value_mirror && reflected->vhdl_access) {
            const auto* heap = vhdl_access_heap(*reflected, expression.span);
            if (heap == nullptr) {
                return ExpressionAttempt { std::nullopt };
            }
            access_heap = heap->objects;
        }
        const auto destination = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        VhdlReflectionApi operation;
        operation.kind = value_mirror
            ? VhdlReflectionApiKind::create_value
            : VhdlReflectionApiKind::create_subtype;
        operation.destination = destination;
        operation.source = source;
        operation.access_heap = access_heap;
        operation.result_width = 32U;
        operation.type = std::move(descriptor);
        operation.source_location = reflection_location(expression.span);
        process_.operations.emplace_back(std::move(operation));
        return destination;
    }

    if ((expression.kind != ExpressionKind::Call
            && expression.kind != ExpressionKind::Identifier)
        || expression.text.starts_with("@")) {
        return ExpressionAttempt { };
    }
    const auto selected = protected_name(expression.text);
    if (!selected || !reflection_mirror_type(object_type(selected->object))) {
        return ExpressionAttempt { };
    }
    if (expected_width == 0U
        || expected_width > std::numeric_limits<std::uint32_t>::max()) {
        report("FSIM-ELAB-VHREFLECT-002",
            "reflection method result has no bounded packed representation",
            expression.span);
        return ExpressionAttempt { std::nullopt };
    }
    const auto receiver = lower_expression(Expression {
        ExpressionKind::Identifier, selected->object, { }, expression.span },
        32U, object_type(selected->object));
    if (!receiver) return ExpressionAttempt { std::nullopt };

    auto kind = VhdlReflectionApiKind::get_type_class;
    VhdlReflectionType conversion;
    const auto* receiver_type = object_type(selected->object);
    const auto receiver_kind = receiver_type == nullptr
        ? std::string { } : reflection_simple_name(*receiver_type);
    if (selected->method == "get_type_class"
        || selected->method == "get_value_class") {
        kind = VhdlReflectionApiKind::get_type_class;
    } else if (selected->method == "get_subtype_mirror") {
        kind = VhdlReflectionApiKind::get_subtype_mirror;
    } else if (selected->method == "to_subtype_mirror"
        || selected->method == "to_value_mirror") {
        kind = VhdlReflectionApiKind::convert_generic;
    } else if (const auto target = reflection_conversion_class(selected->method)) {
        kind = VhdlReflectionApiKind::convert;
        conversion.type_class = *target;
    } else if (selected->method == "enumeration_literal") kind = VhdlReflectionApiKind::enumeration_literal;
    else if (selected->method == "pos") kind = VhdlReflectionApiKind::pos;
    else if (selected->method == "left") kind = VhdlReflectionApiKind::left;
    else if (selected->method == "right") kind = VhdlReflectionApiKind::right;
    else if (selected->method == "low") kind = VhdlReflectionApiKind::low;
    else if (selected->method == "high") kind = VhdlReflectionApiKind::high;
    else if (selected->method == "length") kind = VhdlReflectionApiKind::length;
    else if (selected->method == "ascending") kind = VhdlReflectionApiKind::ascending;
    else if (selected->method == "value") kind = VhdlReflectionApiKind::value;
    else if (selected->method == "units_length") kind = VhdlReflectionApiKind::units_length;
    else if (selected->method == "unit_index") kind = VhdlReflectionApiKind::unit_index;
    else if (selected->method == "scale") kind = VhdlReflectionApiKind::scale;
    else if (selected->method == "element_index") kind = VhdlReflectionApiKind::record_element_index;
    else if (selected->method == "element_subtype") {
        kind = receiver_kind.starts_with("array_")
            ? VhdlReflectionApiKind::array_element_subtype
            : VhdlReflectionApiKind::record_element_subtype;
    } else if (selected->method == "get") {
        kind = receiver_kind.starts_with("access_")
            ? VhdlReflectionApiKind::access_get
            : VhdlReflectionApiKind::aggregate_get;
    }
    else if (selected->method == "dimensions") kind = VhdlReflectionApiKind::dimensions;
    else if (selected->method == "index_subtype") kind = VhdlReflectionApiKind::array_index_subtype;
    else if (selected->method == "designated_subtype") kind = VhdlReflectionApiKind::designated_subtype;
    else if (selected->method == "is_null") kind = VhdlReflectionApiKind::is_null;
    else if (selected->method == "get_file_open_kind") kind = VhdlReflectionApiKind::file_open_kind;
    else {
        return ExpressionAttempt { };
    }

    VhdlReflectionApi operation;
    operation.kind = kind;
    operation.destination = allocate_register(
        expected_width,
        expected_type != nullptr ? expected_type->domain
                                 : frontend::ValueDomain::Bit2);
    operation.receiver = *receiver;
    operation.result_width = static_cast<std::uint32_t>(expected_width);
    operation.type = std::move(conversion);
    operation.source_location = reflection_location(expression.span);
    for (const auto& operand : expression.operands) {
        if (is_string_expression(operand)) {
            if (operation.string_argument) {
                report("FSIM-ELAB-VHREFLECT-003",
                    "reflection method accepts at most one string selector",
                    operand.span);
                return ExpressionAttempt { std::nullopt };
            }
            operation.string_argument = lower_string_expression(operand);
            if (!operation.string_argument) return ExpressionAttempt { std::nullopt };
        } else {
            const auto operand_type = vhdl_expression_type(operand);
            const auto operand_width = operand_type
                ? operand_type->width() : std::optional<std::uint64_t> { };
            const bool index_vector = operand_type
                && operand_type->vhdl_array
                && !operand_type->vhdl_array->element_types.empty()
                && operand_type->vhdl_array->element_types.front().domain
                    == frontend::ValueDomain::Integer
                && operand_width && *operand_width > 64U
                && *operand_width % 64U == 0U;
            if (index_vector) {
                const auto count = *operand_width / 64U;
                if (count > 32U
                    || *operand_width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report("FSIM-ELAB-VHREFLECT-003",
                        "reflection index vector exceeds 32 dimensions",
                        operand.span);
                    return ExpressionAttempt { std::nullopt };
                }
                const auto packed_indices = lower_expression(
                    operand, static_cast<std::size_t>(*operand_width),
                    &*operand_type);
                if (!packed_indices) {
                    return ExpressionAttempt { std::nullopt };
                }
                for (std::uint64_t index = 0U; index < count; ++index) {
                    const auto argument = allocate_register(
                        64U, frontend::ValueDomain::Integer);
                    process_.operations.emplace_back(Extract { argument,
                        *packed_indices,
                        static_cast<std::uint32_t>(
                            (count - index - 1U) * 64U), 64U });
                    operation.arguments.push_back(argument);
                }
                continue;
            }
            auto argument = lower_expression(operand, 64U);
            if (!argument) return ExpressionAttempt { std::nullopt };
            if (register_width(*argument) != 64U) {
                *argument = resize_register(*argument, 64U, true);
            }
            operation.arguments.push_back(*argument);
        }
    }
    const auto destination = *operation.destination;
    process_.operations.emplace_back(std::move(operation));
    return destination;
}

std::optional<StringRegisterId>
Lowerer::lower_vhdl_reflection_string_expression(
    const Expression& expression)
{
    if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
        || (expression.kind != ExpressionKind::Call
            && expression.kind != ExpressionKind::Identifier)) {
        return std::nullopt;
    }
    const auto selected = protected_name(expression.text);
    if (!selected || !reflection_mirror_type(object_type(selected->object))) {
        return std::nullopt;
    }
    auto kind = VhdlReflectionApiKind::simple_name;
    if (selected->method == "simple_name") kind = VhdlReflectionApiKind::simple_name;
    else if (selected->method == "image") kind = VhdlReflectionApiKind::image;
    else if (selected->method == "unit_name") kind = VhdlReflectionApiKind::unit_name;
    else if (selected->method == "element_name") kind = VhdlReflectionApiKind::record_element_name;
    else if (selected->method == "get_file_logical_name") kind = VhdlReflectionApiKind::file_logical_name;
    else return std::nullopt;

    const auto receiver = lower_expression(Expression {
        ExpressionKind::Identifier, selected->object, { }, expression.span },
        32U, object_type(selected->object));
    if (!receiver) return std::nullopt;
    VhdlReflectionApi operation;
    operation.kind = kind;
    operation.string_destination = allocate_string_register();
    operation.receiver = *receiver;
    operation.source_location = reflection_location(expression.span);
    for (const auto& operand : expression.operands) {
        auto argument = lower_expression(operand, 64U);
        if (!argument) return std::nullopt;
        if (register_width(*argument) != 64U) {
            *argument = resize_register(*argument, 64U, true);
        }
        operation.arguments.push_back(*argument);
    }
    const auto destination = *operation.string_destination;
    process_.operations.emplace_back(std::move(operation));
    return destination;
}

Lowerer::ExpressionAttempt
Lowerer::lower_vhdl_protected_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    auto reflection = lower_vhdl_reflection_expression(
        expression, expected_width, expected_type);
    if (reflection.handled) {
        return reflection;
    }
    if ((expression.kind != ExpressionKind::Call
            && expression.kind != ExpressionKind::Identifier)
        || expression.text.starts_with("@")) {
        return ExpressionAttempt { };
    }
    const auto selected = protected_name(expression.text);
    if (!selected) {
        return ExpressionAttempt { };
    }
    const auto type_found = visible_types_.find(selected->object);
    if (type_found == visible_types_.end()
        || type_found->second == nullptr
        || !type_found->second->vhdl_protected) {
        return ExpressionAttempt { };
    }
    const auto& info = *type_found->second->vhdl_protected;
    std::vector<const frontend::FunctionDeclaration*> matches;
    for (const auto& function : info.functions) {
        if (function.name != selected->method
            || function.arguments.size()
                != expression.operands.size()) {
            continue;
        }
        bool compatible = true;
        for (std::size_t index = 0;
            index < function.arguments.size(); ++index) {
            if (!vhdl_expression_matches_type(
                    expression.operands[index],
                    function.arguments[index].type)) {
                compatible = false;
                break;
            }
        }
        if (compatible) {
            matches.push_back(&function);
        }
    }
    if (matches.size() != 1) {
        report(
            matches.empty()
                ? "FSIM-ELAB-VHPROTECTED-012"
                : "FSIM-ELAB-VHPROTECTED-013",
            matches.empty()
                ? "protected function call '" + expression.text
                    + "' matches no public profile"
                : "protected function call '" + expression.text
                    + "' is ambiguous",
            expression.span);
        return ExpressionAttempt { std::nullopt };
    }
    const auto& function = *matches.front();
    if (active_function_
        && function_frames_.at(*active_function_).source != nullptr
        && function_frames_.at(*active_function_).source->pure
        && !function.pure) {
        report(
            "FSIM-ELAB-VHPROTECTED-022",
            "pure VHDL function '"
                + function_frames_.at(*active_function_).source->name
                + "' cannot call impure protected function '"
                + expression.text + "'",
            expression.span);
        return ExpressionAttempt { std::nullopt };
    }
    if (active_vhdl_protected_method_) {
        report(
            "FSIM-ELAB-VHPROTECTED-014",
            "re-entry into a protected method is outside the bounded "
            "execution policy",
            expression.span);
        return ExpressionAttempt { std::nullopt };
    }
    if (function.statements.size() != 1
        || function.statements.front().kind != StatementKind::Return
        || !function.statements.front().value.valid()) {
        report(
            "FSIM-ELAB-VHPROTECTED-015",
            "a bounded protected function body must contain one direct "
            "value-return statement",
            function.span);
        return ExpressionAttempt { std::nullopt };
    }
    const auto result_width = function.return_type.width();
    if (!result_width || *result_width == 0
        || *result_width != expected_width
        || (expected_type != nullptr
            && !vhdl_callable_type_matches(
                *expected_type, function.return_type))) {
        report(
            "FSIM-ELAB-VHPROTECTED-016",
            "protected function result is incompatible with its context",
            expression.span);
        return ExpressionAttempt { std::nullopt };
    }

    std::vector<RegisterId> arguments;
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        const auto width = formal.type.width();
        if (!width || *width == 0) {
            return ExpressionAttempt { std::nullopt };
        }
        const auto actual = lower_expression(
            expression.operands[index], *width, &formal.type);
        if (!actual) {
            return ExpressionAttempt { std::nullopt };
        }
        arguments.push_back(*actual);
    }

    auto saved_locals = std::move(locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = { selected->object, selected->method };
    active_vhdl_protected_method_ = true;
    const auto bind = [&](const std::string& name,
                          const frontend::Type& member_type,
                          const RegisterId value) {
        locals_.insert_or_assign(name, value);
        local_signed_.insert_or_assign(name, member_type.is_signed);
        local_ranges_.insert_or_assign(name, member_type.packed_range);
        local_integer_ranges_.insert_or_assign(
            name, member_type.integer_range);
        local_members_.insert_or_assign(
            name, member_type.packed_members);
        local_types_.insert_or_assign(name, &member_type);
    };
    const auto zero = allocate_register(
        32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(
        LoadConstant { zero, unsigned_value(0, 32) });
    bool storage_ok = true;
    for (const auto& member : info.variables) {
        const auto storage = container_objects_.find(
            selected->object + "." + member.name);
        const auto width = member.type.width();
        if (storage == container_objects_.end()
            || !width || *width == 0
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-VHPROTECTED-017",
                "protected private storage for '" + selected->object
                    + "." + member.name + "' is unavailable",
                expression.span);
            storage_ok = false;
            break;
        }
        const auto container = allocate_container_register(
            design_.container_object_info_.at(storage->second).type);
        const auto value = allocate_register(*width, member.type.domain);
        process_.operations.emplace_back(
            ReadContainerObject { container, storage->second });
        process_.operations.emplace_back(
            ContainerRead { value, container, zero, true, false });
        bind(member.name, member.type, value);
    }
    for (std::size_t index = 0;
        storage_ok && index < function.arguments.size(); ++index) {
        bind(
            function.arguments[index].name,
            function.arguments[index].type,
            arguments[index]);
    }
    std::optional<RegisterId> result;
    if (storage_ok) {
        result = lower_expression(
            function.statements.front().value,
            *result_width,
            &function.return_type);
    }
    active_vhdl_protected_method_ = false;
    locals_ = std::move(saved_locals);
    local_signed_ = std::move(saved_signed);
    local_ranges_ = std::move(saved_ranges);
    local_integer_ranges_ = std::move(saved_integer_ranges);
    local_members_ = std::move(saved_members);
    local_types_ = std::move(saved_types);
    local_scope_ = std::move(saved_scope);
    return ExpressionAttempt { result };
}

bool Lowerer::lower_vhdl_protected_procedure_call(
    const Statement& statement)
{
    const auto selected = protected_name(statement.procedure_name);
    if (!selected) {
        return false;
    }
    const auto type_found = visible_types_.find(selected->object);
    if (type_found == visible_types_.end()
        || type_found->second == nullptr
        || !type_found->second->vhdl_protected) {
        return false;
    }
    const auto& info = *type_found->second->vhdl_protected;
    std::vector<const frontend::ProcedureDeclaration*> matches;
    for (const auto& procedure : info.procedures) {
        if (procedure.name != selected->method
            || procedure.arguments.size()
                != statement.procedure_arguments.size()) {
            continue;
        }
        bool compatible = true;
        for (std::size_t index = 0;
            index < procedure.arguments.size(); ++index) {
            if (statement.procedure_arguments[index].formal
                || procedure.arguments[index].direction
                    != frontend::PortDirection::Input
                || !vhdl_expression_matches_type(
                    statement.procedure_arguments[index].value,
                    procedure.arguments[index].type)) {
                compatible = false;
                break;
            }
        }
        if (compatible) {
            matches.push_back(&procedure);
        }
    }
    if (matches.size() != 1) {
        report(
            matches.empty()
                ? "FSIM-ELAB-VHPROTECTED-018"
                : "FSIM-ELAB-VHPROTECTED-019",
            matches.empty()
                ? "protected procedure call '" + statement.procedure_name
                    + "' matches no supported public input profile"
                : "protected procedure call '" + statement.procedure_name
                    + "' is ambiguous",
            statement.span);
        return true;
    }
    const auto& procedure = *matches.front();
    if (active_vhdl_protected_method_) {
        report(
            "FSIM-ELAB-VHPROTECTED-014",
            "re-entry into a protected method is outside the bounded "
            "execution policy",
            statement.span);
        return true;
    }
    if (contains_explicit_wait(procedure.statements)) {
        report(
            "FSIM-ELAB-VHPROTECTED-020",
            "a protected method cannot suspend",
            procedure.span);
        return true;
    }
    if (contains_call(procedure.statements)) {
        report(
            "FSIM-ELAB-VHPROTECTED-021",
            "nested protected procedure calls are outside the bounded "
            "non-reentrant policy",
            procedure.span);
        return true;
    }

    std::vector<RegisterId> arguments;
    for (std::size_t index = 0;
        index < procedure.arguments.size(); ++index) {
        const auto& formal = procedure.arguments[index];
        const auto width = formal.type.width();
        if (!width || *width == 0) {
            return true;
        }
        const auto actual = lower_expression(
            statement.procedure_arguments[index].value,
            *width,
            &formal.type);
        if (!actual) {
            return true;
        }
        arguments.push_back(*actual);
    }

    auto saved_locals = std::move(locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = { selected->object, selected->method };
    active_vhdl_protected_method_ = true;
    const auto bind = [&](const std::string& name,
                          const frontend::Type& member_type,
                          const RegisterId value) {
        locals_.insert_or_assign(name, value);
        local_signed_.insert_or_assign(name, member_type.is_signed);
        local_ranges_.insert_or_assign(name, member_type.packed_range);
        local_integer_ranges_.insert_or_assign(
            name, member_type.integer_range);
        local_members_.insert_or_assign(
            name, member_type.packed_members);
        local_types_.insert_or_assign(name, &member_type);
    };
    const auto zero = allocate_register(
        32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(
        LoadConstant { zero, unsigned_value(0, 32) });
    struct MemberStorage {
        ContainerObjectId object { };
        ContainerRegisterId container { };
        RegisterId value { };
    };
    std::vector<MemberStorage> storage;
    bool storage_ok = true;
    for (const auto& member : info.variables) {
        const auto found = container_objects_.find(
            selected->object + "." + member.name);
        const auto width = member.type.width();
        if (found == container_objects_.end()
            || !width || *width == 0
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-VHPROTECTED-017",
                "protected private storage for '" + selected->object
                    + "." + member.name + "' is unavailable",
                statement.span);
            storage_ok = false;
            break;
        }
        const auto container = allocate_container_register(
            design_.container_object_info_.at(found->second).type);
        const auto value = allocate_register(*width, member.type.domain);
        process_.operations.emplace_back(
            ReadContainerObject { container, found->second });
        process_.operations.emplace_back(
            ContainerRead { value, container, zero, true, false });
        bind(member.name, member.type, value);
        storage.push_back({ found->second, container, value });
    }
    for (std::size_t index = 0;
        storage_ok && index < procedure.arguments.size(); ++index) {
        bind(
            procedure.arguments[index].name,
            procedure.arguments[index].type,
            arguments[index]);
    }
    if (storage_ok) {
        lower_statements(procedure.statements);
        for (const auto& member : storage) {
            process_.operations.emplace_back(
                ContainerWrite {
                    member.container, zero, member.value, true, false });
            process_.operations.emplace_back(
                WriteContainerObject {
                    member.object, member.container, std::nullopt });
        }
    }
    active_vhdl_protected_method_ = false;
    locals_ = std::move(saved_locals);
    local_signed_ = std::move(saved_signed);
    local_ranges_ = std::move(saved_ranges);
    local_integer_ranges_ = std::move(saved_integer_ranges);
    local_members_ = std::move(saved_members);
    local_types_ = std::move(saved_types);
    local_scope_ = std::move(saved_scope);
    return true;
}

} // namespace fsim::elaboration

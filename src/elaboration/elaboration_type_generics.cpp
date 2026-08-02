// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <sstream>

namespace fsim::elaboration::elaboration_detail {
namespace {

frontend::Type builtin_vhdl_type(const std::string_view name) {
    frontend::Type type;
    type.spelling = std::string{name};
    if (name == "bit" || name == "bit_vector") {
        type.domain = frontend::ValueDomain::Bit2;
    } else if (
        name == "std_logic" || name == "std_logic_vector"
        || name == "std_ulogic" || name == "std_ulogic_vector"
        || name == "signed" || name == "unsigned"
        || name == "ufixed" || name == "sfixed"
        || name == "unresolved_ufixed"
        || name == "unresolved_sfixed"
        || name == "float" || name == "unresolved_float"
        || name == "u_float") {
        type.domain = frontend::ValueDomain::Logic9;
        type.is_signed = name == "signed" || name == "sfixed"
            || name == "unresolved_sfixed";
    } else if (name == "boolean") {
        type.domain = frontend::ValueDomain::Boolean;
    } else if (
        name == "integer" || name == "natural"
        || name == "positive") {
        type.domain = frontend::ValueDomain::Integer;
        type.is_signed = true;
        constexpr auto first =
            std::int64_t{std::numeric_limits<std::int32_t>::min()};
        constexpr auto last =
            std::int64_t{std::numeric_limits<std::int32_t>::max()};
        type.integer_range = frontend::IntegerRange{
            name == "integer" ? first : name == "natural" ? 0 : 1,
            last,
            false};
    }
    return type;
}

std::string expression_identity(
    const frontend::Expression& expression) {
    std::ostringstream output;
    output << static_cast<unsigned>(expression.kind)
           << ':' << expression.text;
    for (const auto& operand : expression.operands) {
        output << '(' << expression_identity(operand) << ')';
    }
    return output.str();
}

std::string canonical_type_identity(const frontend::Type& type) {
    std::ostringstream output;
    output << "vhdl-type-v1;domain="
           << static_cast<unsigned>(type.domain)
           << ";spelling=" << type.spelling
           << ";nominal=" << type.nominal_type
           << ";signed=" << (type.is_signed ? 1 : 0);
    if (type.packed_range) {
        output << ";packed=" << type.packed_range->left << ':'
               << type.packed_range->right << ':'
               << (type.packed_range->descending ? 1 : 0);
    }
    if (type.integer_range) {
        output << ";integer=" << type.integer_range->left << ':'
               << type.integer_range->right << ':'
               << (type.integer_range->descending ? 1 : 0);
    }
    if (type.enumeration_range) {
        output << ";enum-range=" << type.enumeration_range->left << ':'
               << type.enumeration_range->right << ':'
               << (type.enumeration_range->descending ? 1 : 0);
    }
    for (const auto& literal : type.enumeration_literals) {
        output << ";literal=" << literal;
    }
    output << ";aggregate="
           << static_cast<unsigned>(type.packed_aggregate);
    for (const auto& member : type.packed_members) {
        output << ";member=" << member.name << ':'
               << static_cast<unsigned>(member.domain) << ':'
               << member.spelling << ':'
               << (member.is_signed ? 1 : 0) << ':'
               << member.lsb_offset;
        if (member.packed_range) {
            output << ':' << member.packed_range->left << ':'
                   << member.packed_range->right << ':'
                   << (member.packed_range->descending ? 1 : 0);
        }
        for (const auto& nested : member.nested_types) {
            output << ";nested={"
                   << canonical_type_identity(nested) << '}';
        }
    }
    if (type.vhdl_array) {
        output << ";array=" << type.vhdl_array->index_subtype << ':'
               << type.vhdl_array->element_spelling << ':'
               << type.vhdl_array->element_named_type << ':'
               << static_cast<unsigned>(
                      type.vhdl_array->element_domain)
               << ':' << (type.vhdl_array->unconstrained ? 1 : 0);
        if (type.vhdl_array->index_base_range) {
            output << ':' << type.vhdl_array->index_base_range->left
                   << ':' << type.vhdl_array->index_base_range->right
                   << ':'
                   << (type.vhdl_array->index_base_range->descending
                           ? 1
                           : 0);
        }
        for (const auto& dimension :
             type.vhdl_array->dimensions) {
            output << ";dimension="
                   << dimension.index_subtype << ':'
                   << (dimension.unconstrained ? 1 : 0);
            if (dimension.index_base_range) {
                output << ':'
                       << dimension.index_base_range->left << ':'
                       << dimension.index_base_range->right << ':'
                       << (dimension.index_base_range->descending
                               ? 1
                               : 0);
            }
            if (dimension.constraint) {
                output << ":constraint:"
                       << expression_identity(
                              dimension.constraint->left)
                       << ':'
                       << expression_identity(
                              dimension.constraint->right)
                       << ':'
                       << (dimension.constraint->descending
                               ? 1
                               : 0);
            }
            if (dimension.range) {
                output << ":range:"
                       << dimension.range->left << ':'
                       << dimension.range->right << ':'
                       << (dimension.range->descending ? 1 : 0)
                       << ":null:" << (dimension.null ? 1 : 0)
                       << ":stride:" << dimension.stride;
            }
        }
        if (type.vhdl_array->flat_width) {
            output << ";flat-width="
                   << *type.vhdl_array->flat_width;
        }
        for (const auto& element :
             type.vhdl_array->element_types) {
            output << ";element={"
                   << canonical_type_identity(element) << '}';
        }
    }
    if (type.vhdl_access) {
        const auto& access = *type.vhdl_access;
        output << ";access=" << access.handle_width << ':'
               << access.maximum_objects << ':'
               << (access.nullable ? 1 : 0) << ':'
               << (access.owns_designated_object ? 1 : 0) << ':'
               << (access.simulation_lifetime ? 1 : 0);
        for (const auto& designated : access.designated_types) {
            output << ";designated={"
                   << canonical_type_identity(designated) << '}';
        }
    }
    if (type.vhdl_physical) {
        const auto& physical = *type.vhdl_physical;
        output << ";physical";
        if (physical.range) {
            output << "-range="
                   << expression_identity(physical.range->left) << ':'
                   << expression_identity(physical.range->right) << ':'
                   << (physical.range->descending ? 1 : 0);
        }
        if (physical.resolved_range) {
            output << "-resolved=" << physical.resolved_range->left
                   << ':' << physical.resolved_range->right << ':'
                   << (physical.resolved_range->descending ? 1 : 0);
        }
        for (const auto& physical_unit : physical.units) {
            output << ";physical-unit=" << physical_unit.name;
            if (physical_unit.scale) {
                output << ':'
                       << expression_identity(*physical_unit.scale);
            }
            if (physical_unit.scale_factor) {
                output << ":factor="
                       << *physical_unit.scale_factor;
            }
        }
    }
    if (type.vhdl_protected) {
        const auto& protected_info = *type.vhdl_protected;
        output << ";protected=" << (protected_info.has_body ? 1 : 0)
               << ':' << (protected_info.body_conformant ? 1 : 0)
               << ':' << protected_info.storage_width;
        for (std::size_t index = 0;
             index < protected_info.variables.size(); ++index) {
            output << ";protected-member="
                   << protected_info.variables[index].name << ':'
                   << (index < protected_info.variable_offsets.size()
                           ? protected_info.variable_offsets[index]
                           : 0)
                   << "={"
                   << canonical_type_identity(
                          protected_info.variables[index].type)
                   << '}';
        }
        for (const auto& function : protected_info.functions) {
            output << ";protected-function=" << function.name
                   << "->{"
                   << canonical_type_identity(function.return_type)
                   << '}';
            for (const auto& argument : function.arguments) {
                output << ":arg={"
                       << canonical_type_identity(argument.type)
                       << '}';
            }
        }
        for (const auto& procedure : protected_info.procedures) {
            output << ";protected-procedure=" << procedure.name;
            for (const auto& argument : procedure.arguments) {
                output << ":arg={"
                       << canonical_type_identity(argument.type)
                       << '}';
            }
        }
    }
    for (const auto& constraint :
         type.vhdl_array_constraints) {
        output << ";array-constraint="
               << expression_identity(constraint.left) << ':'
               << expression_identity(constraint.right) << ':'
               << (constraint.descending ? 1 : 0);
    }
    return output.str();
}

bool conforming_function_type(
    const frontend::Type& formal,
    const frontend::Type& actual) {
    if (!formal.named_type.empty()
        || !actual.named_type.empty()) {
        return formal.named_type == actual.named_type
            && formal.spelling == actual.spelling;
    }
    return formal.domain == actual.domain
        && formal.is_signed == actual.is_signed
        && formal.width() == actual.width()
        && formal.nominal_type == actual.nominal_type;
}

bool conforming_function_profile(
    const frontend::InterfaceFunctionProfile& formal,
    const frontend::FunctionDeclaration& actual) {
    if (!conforming_function_type(
            formal.return_type, actual.return_type)
        || formal.arguments.size()
            != actual.arguments.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < formal.arguments.size(); ++index) {
        if (actual.arguments[index].direction
                != frontend::PortDirection::Input
            || !conforming_function_type(
                formal.arguments[index].type,
                actual.arguments[index].type)) {
            return false;
        }
    }
    return true;
}

bool conforming_procedure_profile(
    const frontend::InterfaceProcedureProfile& formal,
    const frontend::ProcedureDeclaration& actual) {
    if (formal.arguments.size() != actual.arguments.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < formal.arguments.size(); ++index) {
        const auto& formal_argument = formal.arguments[index];
        const auto& actual_argument = actual.arguments[index];
        if (formal_argument.direction != actual_argument.direction
            || formal_argument.object_class
                != actual_argument.object_class
            || !conforming_function_type(
                formal_argument.type, actual_argument.type)) {
            return false;
        }
    }
    return true;
}

bool expression_calls_function(
    const frontend::Expression& expression,
    const std::string_view name) {
    if (expression.kind == frontend::ExpressionKind::Call
        && expression.text == name) {
        return true;
    }
    for (const auto& operand : expression.operands) {
        if (expression_calls_function(operand, name)) {
            return true;
        }
    }
    for (const auto& association :
         expression.aggregate_choice_expressions) {
        for (const auto& choice : association) {
            if (expression_calls_function(choice, name)) {
                return true;
            }
        }
    }
    return false;
}

bool statements_call_function(
    const std::vector<frontend::Statement>& statements,
    const std::string_view name) {
    for (const auto& statement : statements) {
        if (expression_calls_function(statement.target, name)
            || expression_calls_function(statement.value, name)
            || expression_calls_function(statement.condition, name)
            || expression_calls_function(
                statement.loop_initial, name)
            || expression_calls_function(
                statement.loop_limit, name)
            || statements_call_function(
                statement.statements, name)
            || statements_call_function(
                statement.else_statements, name)) {
            return true;
        }
        for (const auto& argument :
             statement.task_arguments) {
            if (expression_calls_function(argument, name)) {
                return true;
            }
        }
        for (const auto& association :
             statement.procedure_arguments) {
            if (expression_calls_function(
                    association.value, name)) {
                return true;
            }
        }
        for (const auto& declaration :
             statement.declarations) {
            if (declaration.initializer
                && expression_calls_function(
                    *declaration.initializer, name)) {
                return true;
            }
        }
        for (const auto& alternative :
             statement.case_alternatives) {
            for (const auto& choice :
                 alternative.choices) {
                if (expression_calls_function(choice, name)) {
                    return true;
                }
            }
            if (statements_call_function(
                    alternative.statements, name)) {
                return true;
            }
        }
    }
    return false;
}

bool statements_call_procedure(
    const std::vector<frontend::Statement>& statements,
    const std::string_view name) {
    for (const auto& statement : statements) {
        if ((statement.kind
                 == frontend::StatementKind::ProcedureCall
             && statement.procedure_name == name)
            || statements_call_procedure(
                statement.statements, name)
            || statements_call_procedure(
                statement.else_statements, name)) {
            return true;
        }
        for (const auto& alternative :
             statement.case_alternatives) {
            if (statements_call_procedure(
                    alternative.statements, name)) {
                return true;
            }
        }
    }
    return false;
}

bool procedure_has_timing_or_signal_update(
    const std::vector<frontend::Statement>& statements) {
    for (const auto& statement : statements) {
        if (statement.kind == frontend::StatementKind::Delay
            || statement.kind
                == frontend::StatementKind::WaitOn
            || statement.kind
                == frontend::StatementKind::WaitUntil
            || (statement.kind
                    == frontend::StatementKind::Assignment
                && statement.assignment_kind
                    != frontend::AssignmentKind::Blocking)
            || procedure_has_timing_or_signal_update(
                statement.statements)
            || procedure_has_timing_or_signal_update(
                statement.else_statements)) {
            return true;
        }
        for (const auto& alternative :
             statement.case_alternatives) {
            if (procedure_has_timing_or_signal_update(
                    alternative.statements)) {
                return true;
            }
        }
    }
    return false;
}

void append_function_dependency_closure(
    DesignUnit& unit,
    const frontend::FunctionDeclaration& root,
    const std::vector<frontend::FunctionDeclaration>& visible) {
    std::vector<const frontend::FunctionDeclaration*> pending{
        &root};
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        const auto* caller = pending.back();
        pending.pop_back();
        if (!visited.insert(caller->name).second) {
            continue;
        }
        for (const auto& dependency :
             caller->source_dependencies) {
            if (std::ranges::find(
                    unit.source_dependencies, dependency)
                == unit.source_dependencies.end()) {
                unit.source_dependencies.push_back(dependency);
            }
        }
        for (const auto& candidate : visible) {
            const bool called =
                std::ranges::any_of(
                    caller->variables,
                    [&](const frontend::VariableDeclaration& variable) {
                      return variable.initializer
                          && expression_calls_function(
                              *variable.initializer,
                              candidate.name);
                    })
                || statements_call_function(
                    caller->statements, candidate.name);
            if (!called) {
                continue;
            }
            if (std::ranges::none_of(
                    unit.functions,
                    [&](const auto& existing) {
                      return existing.name == candidate.name;
                    })) {
                unit.functions.push_back(candidate);
            }
            const auto dependency = std::string{
                frontend::physical_source(candidate.span)};
            if (!dependency.empty()
                && std::ranges::find(
                       unit.source_dependencies, dependency)
                    == unit.source_dependencies.end()) {
                unit.source_dependencies.push_back(
                    dependency);
            }
            pending.push_back(&candidate);
        }
    }
}

void append_procedure_dependency_closure(
    DesignUnit& unit,
    const frontend::ProcedureDeclaration& root,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const std::vector<frontend::ProcedureDeclaration>& procedures) {
    std::vector<const frontend::ProcedureDeclaration*> pending{
        &root};
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        const auto* caller = pending.back();
        pending.pop_back();
        if (!visited.insert(caller->name).second) {
            continue;
        }
        for (const auto& dependency :
             caller->source_dependencies) {
            if (std::ranges::find(
                    unit.source_dependencies, dependency)
                == unit.source_dependencies.end()) {
                unit.source_dependencies.push_back(dependency);
            }
        }
        for (const auto& function : functions) {
            const bool called =
                std::ranges::any_of(
                    caller->variables,
                    [&](const frontend::VariableDeclaration& variable) {
                      return variable.initializer
                          && expression_calls_function(
                              *variable.initializer,
                              function.name);
                    })
                || statements_call_function(
                    caller->statements, function.name);
            if (!called) {
                continue;
            }
            if (std::ranges::none_of(
                    unit.functions,
                    [&](const auto& existing) {
                      return existing.name == function.name;
                    })) {
                unit.functions.push_back(function);
                append_function_dependency_closure(
                    unit, function, functions);
            }
            const auto dependency = std::string{
                frontend::physical_source(function.span)};
            if (!dependency.empty()
                && std::ranges::find(
                       unit.source_dependencies, dependency)
                    == unit.source_dependencies.end()) {
                unit.source_dependencies.push_back(
                    dependency);
            }
        }
        for (const auto& procedure : procedures) {
            if (!statements_call_procedure(
                    caller->statements, procedure.name)) {
                continue;
            }
            if (std::ranges::none_of(
                    unit.procedures,
                    [&](const auto& existing) {
                      return existing.name == procedure.name;
                    })) {
                unit.procedures.push_back(procedure);
            }
            const auto dependency = std::string{
                frontend::physical_source(procedure.span)};
            if (!dependency.empty()
                && std::ranges::find(
                       unit.source_dependencies, dependency)
                    == unit.source_dependencies.end()) {
                unit.source_dependencies.push_back(
                    dependency);
            }
            pending.push_back(&procedure);
        }
    }
}

std::string canonical_function_identity(
    const frontend::FunctionDeclaration& function) {
    std::ostringstream output;
    output << "vhdl-function-v1;name=" << function.name
           << ";pure=" << (function.pure ? 1 : 0)
           << ";source="
           << frontend::physical_source(function.span)
           << ";offset=" << function.span.begin.offset
           << ";return="
           << canonical_type_identity(function.return_type);
    for (const auto& argument : function.arguments) {
        output << ";argument="
               << canonical_type_identity(argument.type);
    }
    if (!function.specialization_identity.empty()) {
        output << ";generic-instance="
               << function.specialization_identity;
    }
    return output.str();
}

std::string canonical_procedure_identity(
    const frontend::ProcedureDeclaration& procedure) {
    std::ostringstream output;
    output << "vhdl-procedure-v1;name=" << procedure.name
           << ";source="
           << frontend::physical_source(procedure.span)
           << ";offset=" << procedure.span.begin.offset;
    for (const auto& argument : procedure.arguments) {
        output << ";argument="
               << static_cast<unsigned>(argument.object_class)
               << ':' << static_cast<unsigned>(argument.direction)
               << ':' << canonical_type_identity(argument.type);
    }
    if (!procedure.specialization_identity.empty()) {
        output << ";generic-instance="
               << procedure.specialization_identity;
    }
    return output.str();
}

void resolve_interface_profile_types(
    frontend::InterfaceFunctionProfile& profile,
    const DesignUnit& unit) {
    const auto resolve =
        [&](frontend::Type& type) {
            if (type.named_type.empty()) {
                return;
            }
            const auto alias = std::ranges::find_if(
                unit.type_aliases,
                [&](const frontend::TypeAliasDeclaration& candidate) {
                    return candidate.name == type.named_type;
                });
            if (alias != unit.type_aliases.end()) {
                type = alias->type;
            }
        };
    resolve(profile.return_type);
    for (auto& argument : profile.arguments) {
        resolve(argument.type);
    }
}

void resolve_interface_profile_types(
    frontend::InterfaceProcedureProfile& profile,
    const DesignUnit& unit) {
    for (auto& argument : profile.arguments) {
        if (argument.type.named_type.empty()) {
            continue;
        }
        const auto alias = std::ranges::find_if(
            unit.type_aliases,
            [&](const frontend::TypeAliasDeclaration& candidate) {
                return candidate.name == argument.type.named_type;
            });
        if (alias != unit.type_aliases.end()) {
            argument.type = alias->type;
        }
    }
}
std::optional<frontend::Type> resolve_type_mark(
    const frontend::Expression& expression,
    const NamedTypeEnvironment& parent_types) {
    if (expression.kind != frontend::ExpressionKind::Identifier
        || !expression.operands.empty()) {
        return std::nullopt;
    }
    auto builtin = builtin_vhdl_type(expression.text);
    if (builtin.domain != frontend::ValueDomain::Unknown) {
        return builtin;
    }
    const auto found = parent_types.find(expression.text);
    if (found == parent_types.end()
        || found->second.interface_formal) {
        return std::nullopt;
    }
    return found->second.type;
}

std::string simple_type_name(const std::string_view spelling) {
    const auto separator = spelling.find_last_of('.');
    return std::string{
        spelling.substr(
            separator == std::string_view::npos
                ? 0
                : separator + 1)};
}

bool is_packed_array_type(const frontend::Type& type) {
    const auto name = simple_type_name(type.spelling);
    return type.vhdl_array.has_value()
        || name == "bit_vector"
        || name == "std_logic_vector"
        || name == "std_ulogic_vector"
        || name == "signed"
        || name == "unsigned"
        || name == "ufixed" || name == "sfixed"
        || name == "unresolved_ufixed"
        || name == "unresolved_sfixed"
        || name == "float" || name == "unresolved_float"
        || name == "u_float";
}

frontend::SourceSpan constraint_span(const frontend::Type& type) {
    if (!type.vhdl_array_constraints.empty()) {
        return type.vhdl_array_constraints.front().span;
    }
    if (type.discrete_range_expression) {
        return type.discrete_range_expression->span;
    }
    if (type.integer_range_expression) {
        return type.integer_range_expression->span;
    }
    if (type.packed_range_expression) {
        return type.packed_range_expression->span;
    }
    return type.named_type_span;
}

std::optional<frontend::Type> apply_derived_constraints(
    frontend::Type base,
    const frontend::Type& derived,
    std::vector<Diagnostic>& diagnostics) {
    if (derived.integer_range_expression) {
        if (base.domain != frontend::ValueDomain::Integer) {
            diagnostics.push_back({
                "FSIM-ELAB-VHSUBTYPE-001",
                "a derived VHDL range constraint requires an "
                "integer-family base subtype",
                constraint_span(derived)});
            return std::nullopt;
        }
        base.integer_base_range = base.integer_range;
        base.integer_base_range_expression =
            base.integer_range_expression;
        base.integer_range = derived.integer_range;
        base.integer_range_expression =
            derived.integer_range_expression;
    }
    if (derived.discrete_range_expression) {
        const auto& range = *derived.discrete_range_expression;
        if (base.domain == frontend::ValueDomain::Integer) {
            base.integer_base_range = base.integer_range;
            base.integer_base_range_expression =
                base.integer_range_expression;
            base.integer_range.reset();
            base.integer_range_expression =
                frontend::IntegerRangeExpression{
                    range.left,
                    range.right,
                    range.span,
                    range.descending};
        } else if (!base.enumeration_literals.empty()) {
            base.enumeration_base_range =
                base.enumeration_range;
            base.enumeration_base_range_expression =
                base.enumeration_range_expression;
            base.enumeration_range.reset();
            base.enumeration_range_expression = range;
        } else {
            diagnostics.push_back({
                "FSIM-ELAB-VHSUBTYPE-001",
                "a derived VHDL discrete range constraint requires an "
                "integer-family or enumeration base subtype",
                constraint_span(derived)});
            return std::nullopt;
        }
    }
    if (base.vhdl_array
        && !derived.vhdl_array_constraints.empty()) {
        auto& array = *base.vhdl_array;
        if (derived.vhdl_array_constraints.size()
            != array.dimensions.size()) {
            diagnostics.push_back({
                "FSIM-ELAB-VHARRAY-008",
                "a VHDL array subtype constraint has "
                    + std::to_string(
                        derived.vhdl_array_constraints.size())
                    + " dimensions but its base type has "
                    + std::to_string(array.dimensions.size()),
                constraint_span(derived)});
            return std::nullopt;
        }
        for (std::size_t index = 0;
             index < array.dimensions.size(); ++index) {
            auto& dimension = array.dimensions[index];
            if (!dimension.unconstrained
                || dimension.constraint
                || dimension.range) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHSUBTYPE-004",
                    "a constrained VHDL array dimension cannot be "
                    "constrained again",
                    derived.vhdl_array_constraints[index].span});
                return std::nullopt;
            }
            dimension.constraint =
                derived.vhdl_array_constraints[index];
            dimension.range.reset();
            dimension.null = false;
            dimension.stride = 0;
            dimension.unconstrained = false;
        }
        array.unconstrained = false;
        array.flat_width.reset();
        base.packed_range.reset();
        base.packed_range_expression.reset();
        if (array.dimensions.size() == 1) {
            const auto& constraint =
                derived.vhdl_array_constraints.front();
            base.packed_range_expression =
                frontend::PackedRangeExpression{
                    constraint.left,
                    constraint.right,
                    constraint.span,
                    constraint.descending};
        }
        base.vhdl_array_constraints.clear();
    } else if (derived.packed_range_expression) {
        if (!is_packed_array_type(base)
            || !base.packed_members.empty()) {
            diagnostics.push_back({
                "FSIM-ELAB-VHSUBTYPE-003",
                "a derived VHDL packed index constraint requires an "
                "unconstrained one-dimensional packed-array base",
                constraint_span(derived)});
            return std::nullopt;
        }
        if (base.packed_range
            || base.packed_range_expression) {
            diagnostics.push_back({
                "FSIM-ELAB-VHSUBTYPE-004",
                "a constrained VHDL packed-array subtype cannot be "
                "constrained again",
                constraint_span(derived)});
            return std::nullopt;
        }
        base.packed_range = derived.packed_range;
        base.packed_range_expression =
            derived.packed_range_expression;
        if (base.vhdl_array) {
            base.vhdl_array->unconstrained = false;
        }
    }
    return base;
}

std::optional<frontend::Type> resolve_subtype_indication(
    const frontend::ParameterOverride& actual,
    const NamedTypeEnvironment& parent_types,
    std::vector<Diagnostic>& diagnostics) {
    if (!actual.type_value) {
        if (actual.value.kind
                == frontend::ExpressionKind::Identifier
            && actual.value.operands.empty()) {
            return resolve_type_mark(actual.value, parent_types);
        }
        if (actual.value.kind != frontend::ExpressionKind::Slice
            || actual.value.operands.size() != 3
            || actual.value.operands.front().kind
                != frontend::ExpressionKind::Identifier
            || !actual.value.operands.front().operands.empty()
            || (actual.value.text != "to"
                && actual.value.text != "downto")) {
            return std::nullopt;
        }
        const auto& mark = actual.value.operands.front();
        auto base = resolve_type_mark(mark, parent_types);
        if (!base) {
            return std::nullopt;
        }
        frontend::Type derived;
        derived.spelling = mark.text;
        derived.named_type = mark.text;
        derived.named_type_span = mark.span;
        derived.packed_range_expression =
            frontend::PackedRangeExpression{
                actual.value.operands[1],
                actual.value.operands[2],
                actual.value.span,
                actual.value.text == "downto"};
        if (base->vhdl_array) {
            derived.vhdl_array_constraints.push_back(
                frontend::DiscreteRangeExpression{
                    actual.value.operands[1],
                    actual.value.operands[2],
                    actual.value.span,
                    actual.value.text == "downto"});
        }
        return apply_derived_constraints(
            std::move(*base), derived, diagnostics);
    }

    const auto& parsed = *actual.type_value;
    if (parsed.named_type.empty()) {
        return parsed;
    }
    frontend::Expression mark{
        frontend::ExpressionKind::Identifier,
        parsed.named_type,
        {},
        parsed.named_type_span};
    auto base = resolve_type_mark(mark, parent_types);
    if (!base) {
        return std::nullopt;
    }
    return apply_derived_constraints(
        std::move(*base), parsed, diagnostics);
}

} // namespace

NamedTypeEnvironment local_vhdl_type_environment(
    const DesignUnit& unit) {
    NamedTypeEnvironment result;
    for (const auto& alias : unit.type_aliases) {
        result.insert_or_assign(
            alias.name,
            NamedTypeBinding{
                alias.type,
                (unit.library.empty() ? std::string{"work"} : unit.library)
                    + "." + unit.name,
                false});
    }
    return result;
}

InterfaceTypeSpecialization specialize_vhdl_interface_types(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const ConstantDomainEnvironment& parent_domains,
    const NamedTypeEnvironment& parent_types,
    const std::vector<frontend::FunctionDeclaration>& parent_functions,
    const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
    InterfaceTypeSpecialization result;
    result.unit = source;
    const bool has_interface_formals = std::ranges::any_of(
        source.parameters,
        [](const auto& parameter) {
            return !parameter.local
                && parameter.kind
                    != frontend::ParameterKind::Value;
        });
    if (!has_interface_formals) {
        for (const auto& actual : overrides) {
            if (actual.type_value) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENTYPE-005",
                    "a value generic cannot receive a subtype-indication "
                    "actual",
                    actual.span});
            } else {
                result.value_overrides.push_back(actual);
            }
        }
        return result;
    }
    result.applied = true;

    std::vector<const frontend::ParameterDeclaration*> formals;
    for (const auto& parameter : source.parameters) {
        if (!parameter.local) {
            formals.push_back(&parameter);
        }
    }
    std::vector<std::optional<frontend::ParameterOverride>> actuals(
        formals.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    for (const auto& actual : overrides) {
        std::optional<std::size_t> index;
        if (actual.name) {
            saw_named = true;
            const auto found = std::ranges::find_if(
                formals,
                [&](const auto* formal) {
                    return parameter_name_matches(
                        formal->name,
                        *actual.name,
                        source.language,
                        association_language);
                });
            if (found == formals.end()) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENERIC-001",
                    "unknown generic actual '" + *actual.name + "'",
                    actual.span});
                continue;
            }
            index = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        } else {
            if (saw_named) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENERIC-003",
                    "a positional generic actual cannot follow a named "
                    "actual",
                    actual.span});
            }
            if (next_positional >= formals.size()) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENERIC-001",
                    "too many positional generic actuals",
                    actual.span});
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index]) {
            diagnostics.push_back({
                "FSIM-ELAB-GENERIC-002",
                "duplicate generic actual for '"
                    + formals[*index]->name + "'",
                actual.span});
        } else {
            actuals[*index] = actual;
        }
    }

    for (std::size_t index = 0; index < formals.size(); ++index) {
        const auto& formal = *formals[index];
        if (actuals[index] && actuals[index]->default_box) {
            const bool has_default = [&]() {
                switch (formal.kind) {
                case frontend::ParameterKind::Value:
                    return formal.default_value.valid();
                case frontend::ParameterKind::Function:
                    return formal.function_profile
                        && (formal.function_profile->default_name
                            || formal.function_profile->default_box);
                case frontend::ParameterKind::Procedure:
                    return formal.procedure_profile
                        && (formal.procedure_profile->default_name
                            || formal.procedure_profile->default_box);
                case frontend::ParameterKind::Type:
                case frontend::ParameterKind::Package:
                    return false;
                }
                return false;
            }();
            if (!has_default) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENERIC-001",
                    "generic '" + formal.name
                        + "' has no default selected by open",
                    actuals[index]->span});
            }
            actuals[index].reset();
        }
        if (formal.kind == frontend::ParameterKind::Value) {
            if (actuals[index]) {
                if (actuals[index]->type_value) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GENTYPE-005",
                        "value generic '" + formal.name
                            + "' cannot receive a subtype-indication "
                              "actual",
                        actuals[index]->span});
                } else {
                    auto value_actual = std::move(*actuals[index]);
                    value_actual.name = formal.name;
                    result.value_overrides.push_back(
                        std::move(value_actual));
                }
            }
            continue;
        }
        if (formal.kind
            == frontend::ParameterKind::Function) {
            if (!formal.function_profile) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-001",
                    "interface function generic '" + formal.name
                        + "' has no retained profile",
                    formal.span});
                continue;
            }
            if (association_language
                != frontend::Language::Vhdl2008) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-002",
                    "interface function generic '" + formal.name
                        + "' requires a same-language VHDL function "
                          "actual",
                    actuals[index]
                            ? actuals[index]->span
                            : formal.span});
                continue;
            }

            auto profile = *formal.function_profile;
            resolve_interface_profile_types(
                profile, result.unit);
            std::optional<std::string> actual_name;
            frontend::SourceSpan actual_span = formal.span;
            if (actuals[index]) {
                actual_span = actuals[index]->span;
                if (actuals[index]->type_value
                    || actuals[index]->value.kind
                        != frontend::ExpressionKind::Identifier
                    || !actuals[index]->value.operands.empty()) {
                    diagnostics.push_back({
                        "FSIM-ELAB-VHFUNC-003",
                        "actual for interface function generic '"
                            + formal.name
                            + "' must be a visible function name",
                        actuals[index]->span});
                    continue;
                }
                actual_name = actuals[index]->value.text;
            } else if (profile.default_name) {
                actual_name = *profile.default_name;
            } else if (!profile.default_box) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-004",
                    "interface function generic '" + formal.name
                        + "' requires an actual function",
                    formal.span});
                continue;
            }

            std::vector<const frontend::FunctionDeclaration*>
                matches;
            for (const auto& candidate : parent_functions) {
                if (actual_name
                    && candidate.name != *actual_name) {
                    continue;
                }
                if (conforming_function_profile(
                        profile, candidate)) {
                    matches.push_back(&candidate);
                }
            }
            if (matches.empty()) {
                diagnostics.push_back({
                    actual_name
                        ? "FSIM-ELAB-VHFUNC-005"
                        : "FSIM-ELAB-VHFUNC-006",
                    actual_name
                        ? "function '" + *actual_name
                            + "' is not visible with a conforming "
                              "profile for interface function generic '"
                            + formal.name + "'"
                        : "no unique directly visible function has a "
                          "conforming profile for box-default interface "
                          "function generic '" + formal.name + "'",
                    actual_span});
                continue;
            }
            if (matches.size() != 1) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-006",
                    "function actual for interface function generic '"
                        + formal.name
                        + "' is ambiguous among conforming visible "
                          "functions",
                    actual_span});
                continue;
            }
            const auto& selected = *matches.front();
            if (selected.language
                != frontend::Language::Vhdl2008) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-002",
                    "function actual '" + selected.name
                        + "' is not a VHDL function",
                    selected.span});
                continue;
            }
            if (!selected.defined) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-007",
                    "function actual '" + selected.name
                        + "' has no executable body",
                    selected.span});
                continue;
            }
            if (!selected.pure) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-008",
                    "function actual '" + selected.name
                        + "' must be pure in the bounded VHDL subset",
                    selected.span});
                continue;
            }
            const bool recursive_initializer =
                std::ranges::any_of(
                    selected.variables,
                    [&](const frontend::VariableDeclaration& variable) {
                        return variable.initializer
                            && expression_calls_function(
                                *variable.initializer,
                                selected.name);
                    });
            if (recursive_initializer
                || statements_call_function(
                    selected.statements, selected.name)) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVFUNC-006",
                    "recursive function call graph involving '"
                        + selected.name + "' is not supported",
                    selected.span});
                continue;
            }
            if (std::ranges::any_of(
                    result.unit.functions,
                    [&](const frontend::FunctionDeclaration& existing) {
                        return existing.name == formal.name;
                    })) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHFUNC-009",
                    "interface function binding '" + formal.name
                        + "' conflicts with a child-local function",
                    formal.span});
                continue;
            }
            auto bound = selected;
            bound.name = formal.name;
            result.unit.functions.push_back(std::move(bound));
            append_function_dependency_closure(
                result.unit, selected, parent_functions);
            result.values.emplace_back(
                formal.name,
                canonical_function_identity(selected));
            const auto dependency =
                std::string{
                    frontend::physical_source(selected.span)};
            if (!dependency.empty()
                && std::ranges::find(
                       result.unit.source_dependencies,
                       dependency)
                    == result.unit.source_dependencies.end()) {
                result.unit.source_dependencies.push_back(
                    dependency);
            }
            continue;
        }
        if (formal.kind
            == frontend::ParameterKind::Procedure) {
            if (!formal.procedure_profile) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-001",
                    "interface procedure generic '" + formal.name
                        + "' has no retained profile",
                    formal.span});
                continue;
            }
            if (association_language
                != frontend::Language::Vhdl2008) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-002",
                    "interface procedure generic '" + formal.name
                        + "' requires a same-language VHDL procedure "
                          "actual",
                    actuals[index]
                            ? actuals[index]->span
                            : formal.span});
                continue;
            }

            auto profile = *formal.procedure_profile;
            resolve_interface_profile_types(
                profile, result.unit);
            std::optional<std::string> actual_name;
            frontend::SourceSpan actual_span = formal.span;
            if (actuals[index]) {
                actual_span = actuals[index]->span;
                if (actuals[index]->type_value
                    || actuals[index]->value.kind
                        != frontend::ExpressionKind::Identifier
                    || !actuals[index]->value.operands.empty()) {
                    diagnostics.push_back({
                        "FSIM-ELAB-VHPROC-003",
                        "actual for interface procedure generic '"
                            + formal.name
                            + "' must be a visible procedure name",
                        actuals[index]->span});
                    continue;
                }
                actual_name = actuals[index]->value.text;
            } else if (profile.default_name) {
                actual_name = *profile.default_name;
            } else if (!profile.default_box) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-004",
                    "interface procedure generic '" + formal.name
                        + "' requires an actual procedure",
                    formal.span});
                continue;
            }

            std::vector<const frontend::ProcedureDeclaration*>
                matches;
            for (const auto& candidate : parent_procedures) {
                if (actual_name
                    && candidate.name != *actual_name) {
                    continue;
                }
                if (conforming_procedure_profile(
                        profile, candidate)) {
                    matches.push_back(&candidate);
                }
            }
            if (matches.empty()) {
                const bool names_function =
                    actual_name
                    && std::ranges::any_of(
                        parent_functions,
                        [&](const auto& function) {
                            return function.name == *actual_name;
                        });
                const bool generated_or_scoped =
                    actual_name
                    && actual_name->find('.')
                        != std::string::npos;
                diagnostics.push_back({
                    names_function
                        ? "FSIM-ELAB-VHPROC-008"
                        : generated_or_scoped
                            ? "FSIM-ELAB-VHPROC-012"
                            : actual_name
                                ? "FSIM-ELAB-VHPROC-005"
                                : "FSIM-ELAB-VHPROC-006",
                    names_function
                        ? "subprogram actual '" + *actual_name
                            + "' is a function rather than a procedure"
                        : generated_or_scoped
                            ? "generated or scoped procedure actual '"
                                + *actual_name
                                + "' is not supported by bounded "
                                  "interface procedure generics"
                        : actual_name
                            ? "procedure '" + *actual_name
                                + "' is not visible with a conforming "
                                  "mode, class, and type profile for "
                                  "interface procedure generic '"
                                + formal.name + "'"
                            : "no unique directly visible procedure has "
                              "a conforming profile for box-default "
                              "interface procedure generic '"
                                + formal.name + "'",
                    actual_span});
                continue;
            }
            if (matches.size() != 1) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-006",
                    "procedure actual for interface procedure generic '"
                        + formal.name
                        + "' is ambiguous among conforming visible "
                          "procedures",
                    actual_span});
                continue;
            }
            const auto& selected = *matches.front();
            if (selected.language
                != frontend::Language::Vhdl2008) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-002",
                    "procedure actual '" + selected.name
                        + "' is not a VHDL procedure",
                    selected.span});
                continue;
            }
            if (!selected.defined) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-007",
                    "procedure actual '" + selected.name
                        + "' has no executable body",
                    selected.span});
                continue;
            }
            if (procedure_has_timing_or_signal_update(
                    selected.statements)) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-009",
                    "procedure actual '" + selected.name
                        + "' must be time-free and may update only "
                          "variables and formals",
                    selected.span});
                continue;
            }
            if (statements_call_procedure(
                    selected.statements, selected.name)) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-010",
                    "recursive procedure call graph involving '"
                        + selected.name + "' is not supported",
                    selected.span});
                continue;
            }
            if (std::ranges::any_of(
                    result.unit.procedures,
                    [&](const frontend::ProcedureDeclaration& existing) {
                        return existing.name == formal.name;
                    })
                || std::ranges::any_of(
                    result.unit.functions,
                    [&](const frontend::FunctionDeclaration& existing) {
                        return existing.name == formal.name;
                    })) {
                diagnostics.push_back({
                    "FSIM-ELAB-VHPROC-011",
                    "interface procedure binding '" + formal.name
                        + "' conflicts with a child-local subprogram",
                    formal.span});
                continue;
            }
            auto bound = selected;
            bound.name = formal.name;
            result.unit.procedures.push_back(std::move(bound));
            append_procedure_dependency_closure(
                result.unit,
                selected,
                parent_functions,
                parent_procedures);
            result.values.emplace_back(
                formal.name,
                canonical_procedure_identity(selected));
            const auto dependency =
                std::string{
                    frontend::physical_source(selected.span)};
            if (!dependency.empty()
                && std::ranges::find(
                       result.unit.source_dependencies,
                       dependency)
                    == result.unit.source_dependencies.end()) {
                result.unit.source_dependencies.push_back(
                    dependency);
            }
            continue;
        }
        if (formal.kind
            == frontend::ParameterKind::Package) {
            diagnostics.push_back({
                "FSIM-ELAB-VHPKG-001",
                "nested interface package generics are outside the "
                "bounded package-template subset",
                formal.span});
            continue;
        }
        if (!actuals[index]) {
            diagnostics.push_back({
                "FSIM-ELAB-GENTYPE-001",
                "interface type generic '" + formal.name
                    + "' requires an actual subtype indication",
                formal.span});
            continue;
        }
        if (association_language
            != frontend::Language::Vhdl2008) {
            diagnostics.push_back({
                "FSIM-ELAB-GENTYPE-004",
                "interface type generic '" + formal.name
                    + "' requires a same-language VHDL subtype-indication "
                      "actual",
                actuals[index]->span});
            continue;
        }
        const auto diagnostic_count = diagnostics.size();
        auto actual_type =
            resolve_subtype_indication(
                *actuals[index], parent_types, diagnostics);
        if (!actual_type
            && diagnostics.size() == diagnostic_count) {
            const auto& expression = actuals[index]->value;
            const bool syntactic_subtype =
                actuals[index]->type_value.has_value()
                || (expression.kind
                        == frontend::ExpressionKind::Identifier
                    && expression.operands.empty())
                || (expression.kind
                        == frontend::ExpressionKind::Slice
                    && expression.operands.size() == 3
                    && expression.operands.front().kind
                        == frontend::ExpressionKind::Identifier);
            diagnostics.push_back({
                syntactic_subtype
                    ? "FSIM-ELAB-GENTYPE-003"
                    : "FSIM-ELAB-GENTYPE-002",
                syntactic_subtype
                    ? "VHDL type mark in subtype indication for '"
                        + formal.name
                        + "' is not visible at this generic association"
                    : "actual for interface type generic '"
                        + formal.name
                        + "' must be a subtype indication",
                actuals[index]->span});
        }
        if (!actual_type) {
            continue;
        }
        const auto fold_diagnostic_count = diagnostics.size();
        substitute_parameters(
            *actual_type,
            parent_environment,
            parent_domains,
            diagnostics,
            frontend::Language::Vhdl2008);
        if (diagnostics.size() != fold_diagnostic_count) {
            continue;
        }
        result.unit.type_aliases.push_back(
            frontend::TypeAliasDeclaration{
                formal.name,
                *actual_type,
                formal.span,
                {},
                frontend::TypeDeclarationKind::Alias});
        result.values.emplace_back(
            formal.name,
            canonical_type_identity(*actual_type));
    }

    std::erase_if(
        result.unit.parameters,
        [](const auto& parameter) {
            return parameter.kind
                != frontend::ParameterKind::Value;
        });
    return result;
}

} // namespace fsim::elaboration::elaboration_detail

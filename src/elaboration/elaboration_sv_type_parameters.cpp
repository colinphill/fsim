// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <sstream>

namespace fsim::elaboration::elaboration_detail {
namespace {

void append_expression_identity(
    std::ostringstream& output,
    const frontend::Expression& expression) {
    output << '{' << static_cast<unsigned>(expression.kind)
           << ':' << expression.text
           << ':' << expression.nominal_type;
    for (const auto& operand : expression.operands) {
        append_expression_identity(output, operand);
    }
    output << '}';
}

std::string canonical_type_identity(const frontend::Type& type) {
    std::ostringstream output;
    output << "sv-type-v3;domain="
           << static_cast<unsigned>(type.domain)
           << ";spelling=" << type.spelling
           << ";scalar="
           << static_cast<unsigned>(type.systemverilog_scalar)
           << ";net=" << type.systemverilog_net_type
           << ";resolution="
           << type.systemverilog_resolution_function
           << ";nominal=" << type.nominal_type
           << ";named=" << type.named_type
           << ";signed=" << (type.is_signed ? 1 : 0)
           << ";aggregate="
           << static_cast<unsigned>(type.packed_aggregate);
    if (type.packed_range) {
        output << ";packed=" << type.packed_range->left << ':'
               << type.packed_range->right << ':'
               << (type.packed_range->descending ? 1 : 0);
    }
    if (type.packed_range_expression) {
        output << ";packed-expression=";
        append_expression_identity(
            output, type.packed_range_expression->left);
        append_expression_identity(
            output, type.packed_range_expression->right);
        output << ':';
        if (type.packed_range_expression->descending) {
            output << (*type.packed_range_expression->descending ? 1 : 0);
        } else {
            output << '?';
        }
    }
    for (const auto& dimension :
        type.systemverilog_packed_dimensions) {
        output << ";packed-dimension=";
        append_expression_identity(output, dimension.left);
        append_expression_identity(output, dimension.right);
        output << ':';
        if (dimension.descending) {
            output << (*dimension.descending ? 1 : 0);
        } else {
            output << '?';
        }
    }
    if (type.enumeration_range) {
        output << ";enum-range=" << type.enumeration_range->left
               << ':' << type.enumeration_range->right << ':'
               << (type.enumeration_range->descending ? 1 : 0);
    }
    for (const auto& literal : type.enumeration_literals) {
        output << ";literal=" << literal;
    }
    for (const auto& value :
        type.systemverilog_enumeration_values) {
        output << ";enum-value=";
        append_expression_identity(output, value);
    }
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
    if (type.systemverilog_container) {
        const auto& container = *type.systemverilog_container;
        output << ";container="
               << static_cast<unsigned>(container.kind);
        if (container.queue_maximum) {
            output << ";queue-maximum=";
            append_expression_identity(
                output, *container.queue_maximum);
        }
        if (container.associative_index_type) {
            output << ";index-type={"
                   << canonical_type_identity(
                          *container.associative_index_type)
                   << '}';
        }
        if (container.static_range) {
            output << ";static=" << container.static_range->left
                   << ':' << container.static_range->right << ':'
                   << (container.static_range->descending ? 1 : 0);
        }
        for (const auto& dimension :
             container.static_range_expressions) {
            output << ";dimension=";
            append_expression_identity(output, dimension.left);
            append_expression_identity(output, dimension.right);
            output << ':';
            if (dimension.descending) {
                output << (*dimension.descending ? 1 : 0);
            } else {
                output << '?';
            }
        }
        for (const auto& element : container.element_types) {
            output << ";element={"
                   << canonical_type_identity(element) << '}';
        }
    }
    output << ";virtual-interface="
           << (type.systemverilog_virtual_interface ? 1 : 0)
           << ':' << type.systemverilog_interface_type
           << ':' << type.systemverilog_interface_modport
           << ";class=" << type.systemverilog_class_name
           << ':' << type.systemverilog_class_declaration;
    for (const auto& actual :
        type.systemverilog_class_parameter_actuals) {
        output << ";class-actual="
               << (actual.name ? *actual.name : std::string { })
               << ':';
        if (actual.type_actual) {
            output << "type={"
                   << canonical_type_identity(*actual.type_actual)
                   << '}';
        } else {
            output << "value=";
            append_expression_identity(output, actual.value);
        }
    }
    return output.str();
}

bool supported_type_actual(const frontend::Type& type) {
    const auto width = type.width();
    return type.domain != frontend::ValueDomain::Unknown
        && type.domain != frontend::ValueDomain::Boolean
        && width && *width > 0
        && !type.vhdl_array;
}

std::optional<frontend::Type> resolve_type(
    frontend::Type type,
    const NamedTypeEnvironment& environment) {
    if (type.named_type.empty()) {
        return (supported_type_actual(type)
                || (type.domain != frontend::ValueDomain::Unknown
                    && type.packed_range_expression))
            ? std::optional<frontend::Type>{std::move(type)}
            : std::nullopt;
    }
    const auto found = environment.find(type.named_type);
    if (found == environment.end()
        || found->second.interface_formal) {
        return std::nullopt;
    }
    return found->second.type;
}

std::optional<frontend::Type> resolve_expression_type(
    const frontend::ParameterOverride& actual,
    const NamedTypeEnvironment& environment) {
    if (actual.type_value) {
        return resolve_type(*actual.type_value, environment);
    }
    if (actual.value.kind != frontend::ExpressionKind::Identifier
        || !actual.value.operands.empty()) {
        return std::nullopt;
    }
    frontend::Type type;
    type.spelling = actual.value.text;
    type.named_type = actual.value.text;
    type.named_type_span = actual.value.span;
    return resolve_type(std::move(type), environment);
}

} // namespace

std::string systemverilog_type_identity(const frontend::Type& type)
{
    return canonical_type_identity(type);
}

std::optional<std::string> systemverilog_type_parameter_identity(
    const frontend::Type& type) {
    if (!supported_type_actual(type)) {
        return std::nullopt;
    }
    return canonical_type_identity(type);
}

NamedTypeEnvironment local_systemverilog_type_environment(
    const DesignUnit& unit) {
    NamedTypeEnvironment result;
    const auto owner =
        (unit.library.empty() ? std::string{"work"} : unit.library)
        + "." + unit.name;
    for (const auto& alias : unit.type_aliases) {
        result.insert_or_assign(
            alias.name,
            NamedTypeBinding{alias.type, owner, false});
    }
    return result;
}

InterfaceTypeSpecialization specialize_systemverilog_type_parameters(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const NamedTypeEnvironment& parent_types,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
    InterfaceTypeSpecialization result;
    result.unit = source;
    const bool has_type_formals = std::ranges::any_of(
        source.parameters,
        [](const auto& parameter) {
            return parameter.kind == frontend::ParameterKind::Type;
        });
    if (!has_type_formals) {
        for (const auto& actual : overrides) {
            if (actual.type_value) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVTYPEPARAM-002",
                    "a value parameter cannot receive a data-type actual",
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
    for (const auto& actual : overrides) {
        std::optional<std::size_t> index;
        if (actual.name) {
            const auto found = std::ranges::find_if(
                formals,
                [&](const auto* formal) {
                    return formal->name == *actual.name;
                });
            if (found == formals.end()) {
                diagnostics.push_back({
                    "FSIM-ELAB-PARAM-001",
                    "unknown parameter actual '" + *actual.name + "'",
                    actual.span});
                continue;
            }
            index = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        } else {
            if (next_positional >= formals.size()) {
                diagnostics.push_back({
                    "FSIM-ELAB-PARAM-001",
                    "too many positional parameter actuals",
                    actual.span});
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index]) {
            diagnostics.push_back({
                "FSIM-ELAB-PARAM-002",
                "duplicate parameter actual for '"
                    + formals[*index]->name + "'",
                actual.span});
        } else {
            actuals[*index] = actual;
        }
    }

    auto local_types = local_systemverilog_type_environment(source);
    for (std::size_t index = 0; index < formals.size(); ++index) {
        const auto& formal = *formals[index];
        if (formal.kind == frontend::ParameterKind::Value) {
            if (actuals[index]) {
                if (actuals[index]->type_value) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVTYPEPARAM-002",
                        "value parameter '" + formal.name
                            + "' cannot receive a data-type actual",
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
        if (association_language
            != frontend::Language::SystemVerilog2017) {
            diagnostics.push_back({
                "FSIM-ELAB-SVTYPEPARAM-004",
                "type parameter '" + formal.name
                    + "' requires a same-language SystemVerilog "
                      "data-type actual",
                actuals[index] ? actuals[index]->span : formal.span});
            continue;
        }

        std::optional<frontend::Type> actual_type;
        if (actuals[index]) {
            actual_type =
                resolve_expression_type(*actuals[index], parent_types);
            if (actuals[index]->type_value
                && actual_type
                && actual_type->packed_range_expression) {
                ConstantDomainEnvironment domains;
                substitute_parameters(
                    *actual_type,
                    parent_environment,
                    domains,
                    diagnostics,
                    frontend::Language::SystemVerilog2017);
            }
        } else if (formal.default_type) {
            actual_type =
                resolve_type(*formal.default_type, local_types);
        } else {
            diagnostics.push_back({
                "FSIM-ELAB-SVTYPEPARAM-001",
                "type parameter '" + formal.name
                    + "' requires a data-type actual",
                formal.span});
            continue;
        }
        if (!actual_type
            || (!supported_type_actual(*actual_type)
                && !actual_type->packed_range_expression)) {
            diagnostics.push_back({
                "FSIM-ELAB-SVTYPEPARAM-003",
                "data type for type parameter '" + formal.name
                    + "' is not visible or is outside the bounded packed "
                      "integral subset",
                actuals[index] ? actuals[index]->span : formal.span});
            continue;
        }
        result.unit.type_aliases.push_back(
            frontend::TypeAliasDeclaration {
                formal.name,
                *actual_type,
                formal.span,
                { },
                frontend::TypeDeclarationKind::
                    SystemVerilogTypedef,
                { } });
        result.values.emplace_back(
            formal.name,
            supported_type_actual(*actual_type)
                ? canonical_type_identity(*actual_type)
                : std::string{});
        local_types.insert_or_assign(
            formal.name,
            NamedTypeBinding{
                *actual_type,
                source.name,
                false});
    }
    for (const auto& parameter : source.parameters) {
        if (!parameter.local
            || parameter.kind != frontend::ParameterKind::Type) {
            continue;
        }
        const auto actual_type =
            parameter.default_type
                ? resolve_type(*parameter.default_type, local_types)
                : std::optional<frontend::Type>{};
        if (!actual_type
            || (!supported_type_actual(*actual_type)
                && !actual_type->packed_range_expression)) {
            diagnostics.push_back({
                parameter.default_type
                    ? "FSIM-ELAB-SVTYPEPARAM-003"
                    : "FSIM-ELAB-SVTYPEPARAM-001",
                "local type parameter '" + parameter.name
                    + (parameter.default_type
                           ? "' has an invisible or unsupported default"
                           : "' requires a default data type"),
                parameter.span});
            continue;
        }
        result.unit.type_aliases.push_back(
            frontend::TypeAliasDeclaration {
                parameter.name,
                *actual_type,
                parameter.span,
                { },
                frontend::TypeDeclarationKind::
                    SystemVerilogTypedef,
                { } });
        result.values.emplace_back(
            parameter.name,
            supported_type_actual(*actual_type)
                ? canonical_type_identity(*actual_type)
                : std::string{});
        local_types.insert_or_assign(
            parameter.name,
            NamedTypeBinding{
                *actual_type,
                source.name,
                false});
    }

    std::erase_if(
        result.unit.parameters,
        [](const auto& parameter) {
            return parameter.kind == frontend::ParameterKind::Type;
        });
    return result;
}

InterfaceTypeSpecialization specialize_systemverilog_type_parameters(
    DesignUnit&& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const NamedTypeEnvironment& parent_types,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics)
{
    const bool has_type_formals = std::ranges::any_of(
        source.parameters,
        [](const auto& parameter) {
            return parameter.kind == frontend::ParameterKind::Type;
        });
    if (has_type_formals) {
        return specialize_systemverilog_type_parameters(
            static_cast<const DesignUnit&>(source),
            overrides,
            parent_environment,
            parent_types,
            association_language,
            diagnostics);
    }

    InterfaceTypeSpecialization result;
    result.unit = std::move(source);
    for (const auto& actual : overrides) {
        if (actual.type_value) {
            diagnostics.push_back({
                "FSIM-ELAB-SVTYPEPARAM-002",
                "a value parameter cannot receive a data-type actual",
                actual.span});
        } else {
            result.value_overrides.push_back(actual);
        }
    }
    return result;
}

} // namespace fsim::elaboration::elaboration_detail

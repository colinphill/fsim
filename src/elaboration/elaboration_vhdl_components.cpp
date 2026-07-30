// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <numeric>
#include <sstream>

namespace fsim::elaboration {
using namespace elaboration_detail;

namespace {

std::string normalized_library(const DesignUnit& unit) {
    return unit.library.empty() ? "work" : unit.library;
}

std::string expression_profile(
    const frontend::Expression& expression,
    const std::unordered_map<std::string, std::string>& names) {
    std::ostringstream output;
    output << static_cast<int>(expression.kind) << ':';
    if (expression.kind == frontend::ExpressionKind::Identifier) {
        const auto found = names.find(expression.text);
        output << (found == names.end() ? expression.text : found->second);
    } else {
        output << expression.text;
    }
    for (const auto& operand : expression.operands) {
        output << '(' << expression_profile(operand, names) << ')';
    }
    return output.str();
}

std::string type_profile(
    const frontend::Type& type,
    const std::unordered_map<std::string, std::string>& names) {
    std::ostringstream output;
    output << static_cast<int>(type.domain)
           << ";spelling=" << type.spelling
           << ";named=" << type.named_type
           << ";nominal=" << type.nominal_type
           << ";signed=" << type.is_signed;
    if (type.packed_range) {
        output << ";packed=" << type.packed_range->left << ':'
               << type.packed_range->right << ':'
               << type.packed_range->descending;
    } else if (type.packed_range_expression) {
        output << ";packed-expr="
               << expression_profile(
                      type.packed_range_expression->left, names)
               << ':'
               << expression_profile(
                      type.packed_range_expression->right, names)
               << ':'
               << type.packed_range_expression->descending.value_or(true);
    }
    if (type.integer_range) {
        output << ";integer=" << type.integer_range->left << ':'
               << type.integer_range->right << ':'
               << type.integer_range->descending;
    } else if (type.integer_range_expression) {
        output << ";integer-expr="
               << expression_profile(
                      type.integer_range_expression->left, names)
               << ':'
               << expression_profile(
                      type.integer_range_expression->right, names)
               << ':'
               << type.integer_range_expression->descending;
    }
    if (type.integer_base_range) {
        output << ";integer-base="
               << type.integer_base_range->left << ':'
               << type.integer_base_range->right << ':'
               << type.integer_base_range->descending;
    } else if (type.integer_base_range_expression) {
        output << ";integer-base-expr="
               << expression_profile(
                      type.integer_base_range_expression->left, names)
               << ':'
               << expression_profile(
                      type.integer_base_range_expression->right, names)
               << ':'
               << type.integer_base_range_expression->descending;
    }
    for (const auto& literal : type.enumeration_literals) {
        output << ";literal=" << literal;
    }
    if (type.enumeration_range) {
        output << ";enum=" << type.enumeration_range->left << ':'
               << type.enumeration_range->right << ':'
               << type.enumeration_range->descending;
    } else if (type.enumeration_range_expression) {
        output << ";enum-expr="
               << expression_profile(
                      type.enumeration_range_expression->left, names)
               << ':'
               << expression_profile(
                      type.enumeration_range_expression->right, names)
               << ':'
               << type.enumeration_range_expression->descending;
    }
    if (type.enumeration_base_range) {
        output << ";enum-base="
               << type.enumeration_base_range->left << ':'
               << type.enumeration_base_range->right << ':'
               << type.enumeration_base_range->descending;
    } else if (type.enumeration_base_range_expression) {
        output << ";enum-base-expr="
               << expression_profile(
                      type.enumeration_base_range_expression->left,
                      names)
               << ':'
               << expression_profile(
                      type.enumeration_base_range_expression->right,
                      names)
               << ':'
               << type.enumeration_base_range_expression->descending;
    }
    output << ";aggregate="
           << static_cast<int>(type.packed_aggregate);
    for (const auto& member : type.packed_members) {
        output << ";member=" << member.name << ':'
               << static_cast<int>(member.domain) << ':'
               << member.spelling << ':' << member.is_signed
               << ':' << member.lsb_offset;
        if (member.packed_range) {
            output << ':' << member.packed_range->left << ':'
                   << member.packed_range->right << ':'
                   << member.packed_range->descending;
        } else if (member.packed_range_expression) {
            output << ":expr:"
                   << expression_profile(
                          member.packed_range_expression->left, names)
                   << ':'
                   << expression_profile(
                          member.packed_range_expression->right, names)
                   << ':'
                   << member.packed_range_expression
                          ->descending.value_or(true);
        }
    }
    if (type.vhdl_array) {
        output << ";array=" << type.vhdl_array->index_subtype
               << ':' << type.vhdl_array->element_spelling
               << ':' << type.vhdl_array->element_named_type
               << ':'
               << static_cast<int>(
                      type.vhdl_array->element_domain)
               << ':' << type.vhdl_array->unconstrained;
        if (type.vhdl_array->index_base_range) {
            output << ':'
                   << type.vhdl_array->index_base_range->left
                   << ':'
                   << type.vhdl_array->index_base_range->right
                   << ':'
                   << type.vhdl_array
                          ->index_base_range->descending;
        }
    }
    return output.str();
}

std::string type_provenance_profile(
    const frontend::Type& type) {
    return type.vhdl_type_declaration.empty()
        ? std::string{"<builtin>"}
        : type.vhdl_type_declaration;
}

std::unordered_map<std::string, std::string>
generic_placeholders(
    const std::span<
        const frontend::ParameterDeclaration> generics) {
    std::unordered_map<std::string, std::string> result;
    for (std::size_t index = 0; index < generics.size(); ++index) {
        result.emplace(
            generics[index].name,
            "@generic" + std::to_string(index));
    }
    return result;
}

std::size_t component_region_rank(
    const frontend::VhdlComponentDeclaration& declaration) {
    switch (declaration.region) {
    case frontend::VhdlComponentDeclarationRegion::Block:
    case frontend::VhdlComponentDeclarationRegion::Generate:
        return 3;
    case frontend::VhdlComponentDeclarationRegion::Architecture:
        return 2;
    case frontend::VhdlComponentDeclarationRegion::Entity:
        return 1;
    case frontend::VhdlComponentDeclarationRegion::Package:
        return 0;
    }
    return 0;
}

std::string_view instance_scope(
    const std::string_view name) {
    const auto separator = name.find_last_of('.');
    return separator == std::string_view::npos
        ? std::string_view{}
        : name.substr(0, separator);
}

bool scope_is_visible(
    const std::string_view declaration_scope,
    const std::string_view selected_scope) {
    return declaration_scope.empty()
        || selected_scope == declaration_scope
        || (selected_scope.starts_with(declaration_scope)
            && selected_scope.size() > declaration_scope.size()
            && selected_scope[declaration_scope.size()] == '.');
}

template <typename Formal, typename Actual, typename Name, typename Default>
bool association_shape_matches(
    const std::vector<Formal>& formals,
    const std::vector<Actual>& actuals,
    Name actual_name,
    Default has_default) {
    std::vector<bool> bound(formals.size());
    std::size_t positional = 0;
    bool saw_named = false;
    for (const auto& actual : actuals) {
        std::size_t index = formals.size();
        const auto name = actual_name(actual);
        if (name) {
            saw_named = true;
            const auto found = std::ranges::find_if(
                formals,
                [&](const auto& formal) {
                  return formal.name == *name;
                });
            if (found != formals.end()) {
                index = static_cast<std::size_t>(
                    std::distance(formals.begin(), found));
            }
        } else {
            if (saw_named) {
                return false;
            }
            while (positional < formals.size()
                   && bound[positional]) {
                ++positional;
            }
            index = positional++;
        }
        if (index >= formals.size() || bound[index]) {
            return false;
        }
        bound[index] = true;
    }
    for (std::size_t index = 0; index < formals.size(); ++index) {
        if (!bound[index] && !has_default(formals[index])) {
            return false;
        }
    }
    return true;
}

bool known_type_matches(
    const frontend::Type& formal,
    const frontend::Type& actual,
    const frontend::PortDirection direction) {
    if (formal.domain != actual.domain
        || formal.is_signed != actual.is_signed) {
        return false;
    }
    if (!formal.named_type.empty()
        || !actual.named_type.empty()) {
        return formal.named_type == actual.named_type
            && formal.spelling == actual.spelling;
    }
    const bool formal_nominal =
        !formal.packed_members.empty()
        || !formal.enumeration_literals.empty()
        || formal.vhdl_array.has_value();
    const bool actual_nominal =
        !actual.packed_members.empty()
        || !actual.enumeration_literals.empty()
        || actual.vhdl_array.has_value();
    if (formal_nominal != actual_nominal
        || (formal_nominal
            && formal.nominal_type != actual.nominal_type)) {
        return false;
    }
    if (formal.width() && actual.width()
        && formal.width() != actual.width()) {
        return false;
    }
    if (formal.packed_range
        && actual.packed_range
        && (formal.packed_range->left != actual.packed_range->left
            || formal.packed_range->right
                != actual.packed_range->right
            || formal.packed_range->descending
                != actual.packed_range->descending)) {
        return false;
    }
    const auto directional_bounds_match =
        [&](const auto formal_bounds,
            const auto actual_bounds) {
          const auto formal_low =
              std::min(formal_bounds.first, formal_bounds.second);
          const auto formal_high =
              std::max(formal_bounds.first, formal_bounds.second);
          const auto actual_low =
              std::min(actual_bounds.first, actual_bounds.second);
          const auto actual_high =
              std::max(actual_bounds.first, actual_bounds.second);
          if (direction == frontend::PortDirection::Input) {
              return formal_low <= actual_low
                  && formal_high >= actual_high;
          }
          if (direction == frontend::PortDirection::Output) {
              return actual_low <= formal_low
                  && actual_high >= formal_high;
          }
          return formal_low == actual_low
              && formal_high == actual_high;
        };
    if (formal.integer_range && actual.integer_range
        && !directional_bounds_match(
            std::pair{
                formal.integer_range->left,
                formal.integer_range->right},
            std::pair{
                actual.integer_range->left,
                actual.integer_range->right})) {
        return false;
    }
    if (formal.enumeration_range
        && actual.enumeration_range
        && (!directional_bounds_match(
                std::pair{
                    formal.enumeration_range->left,
                    formal.enumeration_range->right},
                std::pair{
                    actual.enumeration_range->left,
                    actual.enumeration_range->right})
            || formal.enumeration_range->descending
                != actual.enumeration_range->descending)) {
        return false;
    }
    return true;
}

bool component_actual_profile_matches(
    const frontend::VhdlComponentDeclaration& declaration,
    const frontend::Instance& instance,
    const DesignUnit& unit) {
    if (!association_shape_matches(
            declaration.generics,
            instance.parameter_overrides,
            [](const auto& actual) {
              return actual.name;
            },
            [](const auto& formal) {
              return formal.default_value.valid();
            })
        || !association_shape_matches(
            declaration.ports,
            instance.connections,
            [](const auto& actual) {
              return actual.port;
            },
            [](const auto& formal) {
              return formal.default_value.has_value();
            })) {
        return false;
    }

    std::size_t positional = 0;
    for (const auto& connection : instance.connections) {
        const frontend::VhdlComponentPort* formal = nullptr;
        if (connection.port) {
            const auto found = std::ranges::find_if(
                declaration.ports,
                [&](const auto& candidate) {
                  return candidate.name == *connection.port;
                });
            if (found != declaration.ports.end()) {
                formal = &*found;
            }
        } else if (positional < declaration.ports.size()) {
            formal = &declaration.ports[positional++];
        }
        if (formal == nullptr
            || connection.value.kind
                != frontend::ExpressionKind::Identifier) {
            continue;
        }
        const auto signal = std::ranges::find_if(
            unit.signals,
            [&](const auto& candidate) {
              return candidate.name == connection.value.text;
            });
        const auto port = std::ranges::find_if(
            unit.ports,
            [&](const auto& candidate) {
              return candidate.name == connection.value.text;
            });
        const frontend::Type* actual_type = nullptr;
        if (signal != unit.signals.end()) {
            actual_type = &signal->type;
        } else if (port != unit.ports.end()) {
            actual_type = &port->type;
        }
        if (actual_type != nullptr
            && !known_type_matches(
                formal->type,
                *actual_type,
                formal->direction)) {
            return false;
        }
    }
    return true;
}

bool component_declaration_matches_entity(
    const frontend::VhdlComponentDeclaration& declaration,
    const DesignUnit& entity) {
    if (declaration.generics.size()
            != entity.parameters.size()
        || declaration.ports.size() != entity.ports.size()) {
        return false;
    }
    const auto component_names =
        generic_placeholders(declaration.generics);
    const auto entity_names =
        generic_placeholders(entity.parameters);
    for (std::size_t index = 0;
         index < declaration.generics.size();
         ++index) {
        if (declaration.generics[index].kind
                != entity.parameters[index].kind
            || type_profile(
                   declaration.generics[index].type,
                   component_names)
                != type_profile(
                   entity.parameters[index].type,
                   entity_names)) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < declaration.ports.size();
         ++index) {
        if (declaration.ports[index].direction
                != entity.ports[index].direction
            || type_profile(
                   declaration.ports[index].type,
                   component_names)
                != type_profile(
                   entity.ports[index].type,
                   entity_names)) {
            return false;
        }
    }
    return true;
}

std::string component_identity(
    const frontend::VhdlComponentDeclaration& declaration,
    const DesignUnit& target) {
    const auto names = generic_placeholders(declaration.generics);
    std::ostringstream output;
    output << "vhdl-component-binding-v3;name="
           << declaration.name
           << ";region="
           << static_cast<int>(declaration.region)
           << ";scope=" << declaration.scope_path
           << ";owner=" << declaration.owner_library
           << '.' << declaration.owner_name
           << ";order=" << declaration.declaration_order
           << ";target=" << unit_identity(target);
    for (const auto& generic : declaration.generics) {
        output << ";generic=" << generic.name
               << ':' << static_cast<int>(generic.kind)
               << ':' << type_profile(generic.type, names)
               << ":type-source="
               << type_provenance_profile(generic.type)
               << ":default=";
        if (generic.default_value.valid()) {
            output << expression_profile(
                generic.default_value, names);
        } else {
            output << "<required>";
        }
    }
    for (const auto& port : declaration.ports) {
        output << ";port=" << port.name
               << ':' << static_cast<int>(port.direction)
               << ':' << type_profile(port.type, names)
               << ":type-source="
               << type_provenance_profile(port.type)
               << ":default=";
        if (port.default_value) {
            output << expression_profile(*port.default_value, names);
        } else {
            output << "<required>";
        }
    }
    return output.str();
}

template <typename Formal, typename Actual, typename Name>
bool normalize_associations(
    const std::span<const Formal> formals,
    const std::span<const Actual> actuals,
    std::vector<Actual>& normalized,
    const std::string& path,
    const std::string_view object,
    const std::string_view code,
    Name actual_name,
    std::vector<Diagnostic>& diagnostics) {
    std::vector<bool> bound(formals.size());
    std::size_t positional = 0;
    bool saw_named = false;
    bool valid = true;
    for (auto actual : actuals) {
        std::size_t index = formals.size();
        const auto name = actual_name(actual);
        if (name) {
            saw_named = true;
            const auto found = std::ranges::find_if(
                formals,
                [&](const auto& formal) {
                  return formal.name == *name;
                });
            if (found != formals.end()) {
                index = static_cast<std::size_t>(
                    std::distance(formals.begin(), found));
            }
        } else {
            if (saw_named) {
                diagnostics.push_back({
                    std::string{code},
                    "a positional component " + std::string{object}
                        + " actual cannot follow a named actual on '"
                        + path + "'",
                    actual.span});
                valid = false;
            }
            while (positional < formals.size() && bound[positional]) {
                ++positional;
            }
            index = positional++;
        }
        if (index >= formals.size()) {
            diagnostics.push_back({
                std::string{code},
                name
                    ? "unknown component " + std::string{object}
                        + " formal '" + *name + "' on '" + path + "'"
                    : "too many positional component "
                        + std::string{object} + " actuals on '" + path + "'",
                actual.span});
            valid = false;
            continue;
        }
        if (bound[index]) {
            diagnostics.push_back({
                std::string{code},
                "component " + std::string{object} + " formal '"
                    + formals[index].name
                    + "' is associated more than once on '" + path + "'",
                actual.span});
            valid = false;
            continue;
        }
        bound[index] = true;
        if constexpr (
            std::is_same_v<Actual, frontend::ParameterOverride>) {
            actual.name = formals[index].name;
        } else {
            actual.port = formals[index].name;
        }
        normalized.push_back(std::move(actual));
    }
    for (std::size_t index = 0; index < formals.size(); ++index) {
        const bool has_default = [&]() {
          if constexpr (
              std::is_same_v<Formal,
                             frontend::ParameterDeclaration>) {
            return formals[index].default_value.valid();
          } else {
            return formals[index].default_value.has_value();
          }
        }();
        if (!bound[index] && !has_default) {
            diagnostics.push_back({
                std::string{code},
                "required component " + std::string{object}
                    + " formal '" + formals[index].name
                    + "' is not associated on '" + path + "'",
                formals[index].span});
            valid = false;
        }
    }
    return valid;
}

std::vector<std::size_t> generic_mapping(
    const frontend::VhdlComponentDeclaration& component,
    const DesignUnit& entity,
    const frontend::VhdlComponentConfiguration* rule,
    std::vector<Diagnostic>& diagnostics,
    bool& valid) {
    std::vector<std::size_t> result(component.generics.size());
    std::iota(result.begin(), result.end(), std::size_t{0});
    if (rule == nullptr) {
        return result;
    }
    for (const auto& association : rule->binding.generic_map) {
        if (!association.name
            || association.value.kind
                != frontend::ExpressionKind::Identifier) {
            continue;
        }
        const auto component_formal = std::ranges::find_if(
            component.generics,
            [&](const auto& formal) {
              return formal.name == association.value.text;
            });
        const auto entity_formal = std::ranges::find_if(
            entity.parameters,
            [&](const auto& formal) {
              return formal.name == *association.name;
            });
        if (component_formal == component.generics.end()
            || entity_formal == entity.parameters.end()) {
            diagnostics.push_back({
                "FSIM-ELAB-VHCOMP-010",
                "configuration generic binding names an unknown component "
                "or entity formal",
                association.span});
            valid = false;
            continue;
        }
        result[static_cast<std::size_t>(
            std::distance(
                component.generics.begin(), component_formal))] =
            static_cast<std::size_t>(
                std::distance(entity.parameters.begin(), entity_formal));
    }
    return result;
}

std::vector<std::size_t> port_mapping(
    const frontend::VhdlComponentDeclaration& component,
    const DesignUnit& entity,
    const frontend::VhdlComponentConfiguration* rule,
    std::vector<Diagnostic>& diagnostics,
    bool& valid) {
    std::vector<std::size_t> result(component.ports.size());
    std::iota(result.begin(), result.end(), std::size_t{0});
    if (rule == nullptr) {
        return result;
    }
    for (const auto& association : rule->binding.port_map) {
        if (!association.port
            || association.value.kind
                != frontend::ExpressionKind::Identifier) {
            continue;
        }
        const auto component_formal = std::ranges::find_if(
            component.ports,
            [&](const auto& formal) {
              return formal.name == association.value.text;
            });
        const auto entity_formal = std::ranges::find_if(
            entity.ports,
            [&](const auto& formal) {
              return formal.name == *association.port;
            });
        if (component_formal == component.ports.end()
            || entity_formal == entity.ports.end()) {
            diagnostics.push_back({
                "FSIM-ELAB-VHCOMP-010",
                "configuration port binding names an unknown component "
                "or entity formal",
                association.span});
            valid = false;
            continue;
        }
        result[static_cast<std::size_t>(
            std::distance(component.ports.begin(), component_formal))] =
            static_cast<std::size_t>(
                std::distance(entity.ports.begin(), entity_formal));
    }
    return result;
}

}  // namespace

const DesignUnit&
HierarchyBuilder::resolved_vhdl_entity_interface(
    const DesignUnit& entity) {
    if (const auto cached =
            resolved_vhdl_entity_interfaces_.find(&entity);
        cached != resolved_vhdl_entity_interfaces_.end()) {
        return cached->second;
    }

    auto visibility = entity;
    visibility.type_aliases.clear();
    visibility.vhdl_component_declarations.clear();
    visibility.signals.clear();
    visibility.processes.clear();
    visibility.instances.clear();
    visibility.generate_regions.clear();
    visibility.concurrent_statements.clear();
    visibility.functions.clear();
    visibility.tasks.clear();
    visibility.procedures.clear();
    visibility.generic_function_templates.clear();
    visibility.generic_procedure_templates.clear();
    visibility.generic_function_instances.clear();
    visibility.generic_procedure_instances.clear();

    std::vector<frontend::VhdlContextItem> expanded_context;
    std::vector<const DesignUnit*> context_stack;
    const auto owner_library =
        entity.library.empty()
            ? std::string{"work"}
            : entity.library;
    expand_vhdl_context_references(
        visibility,
        entity.vhdl_context,
        expanded_context,
        context_stack,
        owner_library);
    std::vector<const DesignUnit*> import_stack;
    NamedTypeEnvironment type_environment;
    import_vhdl_package_constants(
        visibility,
        expanded_context,
        import_stack,
        type_environment);
    import_qualified_vhdl_package_constants(
        visibility, import_stack);
    import_qualified_vhdl_package_types(
        visibility, type_environment, import_stack);
    for (const auto& parameter : entity.parameters) {
        if (parameter.kind != frontend::ParameterKind::Type) {
            continue;
        }
        type_environment.insert_or_assign(
            parameter.name,
            NamedTypeBinding{
                {},
                owner_library + "." + entity.name,
                true});
    }

    DesignUnit resolved;
    resolved.kind = entity.kind;
    resolved.language = entity.language;
    resolved.library = entity.library;
    resolved.name = entity.name;
    resolved.primary_name = entity.primary_name;
    resolved.parameters = entity.parameters;
    resolved.ports = entity.ports;
    resolved.span = entity.span;
    resolved.source_dependencies =
        std::move(visibility.source_dependencies);
    resolve_named_types(resolved, type_environment, true);
    return resolved_vhdl_entity_interfaces_
        .emplace(&entity, std::move(resolved))
        .first->second;
}

HierarchyBuilder::ConfiguredVhdlInstance
HierarchyBuilder::bind_vhdl_component_instance(
    const DesignUnit& unit,
    const frontend::Instance& instance,
    const std::string& path,
    const ConstantEnvironment& parent_environment) {
    ConfiguredVhdlInstance result;
    result.instance = instance;
    if (unit.language != frontend::Language::Vhdl2008
        || !instance.vhdl_component_instance) {
        return result;
    }

    const auto scope = instance_scope(instance.name);
    std::vector<
        const frontend::VhdlComponentDeclaration*> visible;
    std::size_t best_scope = 0;
    std::size_t best_region = 0;
    bool have_candidate = false;
    for (const auto& declaration :
         unit.vhdl_component_declarations) {
        if (declaration.name != instance.unit_name
            || !scope_is_visible(
                declaration.scope_path, scope)) {
            continue;
        }
        const auto declaration_scope =
            declaration.scope_path.size();
        const auto declaration_region =
            component_region_rank(declaration);
        if (!have_candidate
            || declaration_scope > best_scope
            || (declaration_scope == best_scope
                && declaration_region > best_region)) {
            visible.clear();
            best_scope = declaration_scope;
            best_region = declaration_region;
            have_candidate = true;
        }
        if (declaration_scope == best_scope
            && declaration_region == best_region) {
            visible.push_back(&declaration);
        }
    }
    if (visible.empty()) {
        report(
            "FSIM-ELAB-VHCOMP-001",
            "component instance '" + path + "' has no visible declaration '"
                + instance.unit_name + "'",
            instance.span);
        result.valid = false;
        return result;
    }

    std::vector<
        const frontend::VhdlComponentDeclaration*> matching;
    const auto parent_library = normalized_library(unit);
    const DesignUnit* default_entity_profile = nullptr;
    const DesignUnit* default_entity_declaration = nullptr;
    std::size_t default_entity_count = 0;
    for (const auto& candidate : parsed_.units) {
        if (candidate.kind == frontend::UnitKind::VhdlEntity
            && candidate.name == instance.unit_name
            && normalized_library(candidate)
                == parent_library) {
            default_entity_declaration = &candidate;
            ++default_entity_count;
        }
    }
    if (default_entity_count == 1) {
        default_entity_profile =
            &resolved_vhdl_entity_interface(
                *default_entity_declaration);
    }
    std::ranges::copy_if(
        visible,
        std::back_inserter(matching),
        [&](const auto* declaration) {
          return component_actual_profile_matches(
                     *declaration, instance, unit)
              && (default_entity_profile == nullptr
                  || component_declaration_matches_entity(
                      *declaration,
                      *default_entity_profile));
        });
    if (matching.empty() && visible.size() == 1) {
        matching = visible;
    }
    if (matching.empty()) {
        report(
            "FSIM-ELAB-VHCOMP-012",
            "component instance '" + path
                + "' does not match any visible overload '"
                + instance.unit_name + "'",
            instance.span);
        result.valid = false;
        return result;
    }
    if (matching.size() != 1) {
        report(
            "FSIM-ELAB-VHCOMP-002",
            "component instance '" + path
                + "' ambiguously matches "
                + std::to_string(matching.size())
                + " visible declarations '"
                + instance.unit_name + "'",
            instance.span);
        result.valid = false;
        return result;
    }
    const auto& component = *matching.front();
    if (std::ranges::any_of(
            component.ports,
            [](const auto& port) {
              return port.default_value.has_value()
                  && (!port.type.packed_members.empty()
                      || !port.type.enumeration_literals.empty()
                      || port.type.vhdl_array.has_value());
            })) {
        report(
            "FSIM-ELAB-VHCOMP-013",
            "component '" + component.name
                + "' uses an unsupported composite port default",
            component.span);
        result.valid = false;
        return result;
    }

    frontend::Instance normalized = instance;
    normalized.parameter_overrides.clear();
    normalized.connections.clear();
    result.valid &=
        normalize_associations<
            frontend::ParameterDeclaration,
            frontend::ParameterOverride>(
            component.generics,
            instance.parameter_overrides,
            normalized.parameter_overrides,
            path,
            "generic",
            "FSIM-ELAB-VHCOMP-008",
            [](const auto& actual) {
              return actual.name;
            },
            diagnostics_);
    result.valid &=
        normalize_associations<
            frontend::VhdlComponentPort,
            frontend::PortConnection>(
            component.ports,
            instance.connections,
            normalized.connections,
            path,
            "port",
            "FSIM-ELAB-VHCOMP-009",
            [](const auto& actual) {
              return actual.port;
            },
            diagnostics_);
    if (!result.valid) {
        return result;
    }

    DesignUnit component_profile;
    component_profile.kind = frontend::UnitKind::VhdlEntity;
    component_profile.language = frontend::Language::Vhdl2008;
    component_profile.library = unit.library;
    component_profile.name = component.name;
    component_profile.parameters = component.generics;
    component_profile.span = component.span;
    const auto diagnostic_count = diagnostics_.size();
    const auto specialized_component = specialize_unit(
        component_profile,
        normalized.parameter_overrides,
        parent_environment,
        frontend::Language::Vhdl2008,
        diagnostics_);
    if (diagnostics_.size() != diagnostic_count) {
        result.valid = false;
        return result;
    }

    result = configure_vhdl_component_instance(
        unit, normalized, path);
    if (!result.valid) {
        return result;
    }

    if (!result.applied) {
        const auto library = normalized_library(unit);
        const DesignUnit* entity = nullptr;
        std::size_t entities = 0;
        for (const auto& candidate : parsed_.units) {
            if (candidate.kind == frontend::UnitKind::VhdlEntity
                && candidate.name == component.name
                && normalized_library(candidate) == library) {
                entity = &candidate;
                ++entities;
            }
        }
        if (entities == 0) {
            const bool other_language =
                std::ranges::any_of(
                    parsed_.units,
                    [&](const auto& candidate) {
                      return candidate.language
                                 != frontend::Language::Vhdl2008
                          && candidate.name == component.name;
                    });
            report(
                other_language
                    ? "FSIM-ELAB-VHCOMP-011"
                    : "FSIM-ELAB-VHCOMP-003",
                other_language
                    ? "component '" + component.name
                        + "' on '" + path
                        + "' requires an explicit cross-language binding"
                    : "component '" + component.name
                        + "' on '" + path
                        + "' has no same-library default entity",
                component.span);
            result.valid = false;
            return result;
        }
        if (entities != 1) {
            report(
                "FSIM-ELAB-VHCOMP-005",
                "component '" + component.name
                    + "' has an ambiguous same-library entity",
                component.span);
            result.valid = false;
            return result;
        }
        const DesignUnit* architecture = nullptr;
        for (const auto& candidate : parsed_.units) {
            if (candidate.kind
                    == frontend::UnitKind::VhdlArchitecture
                && candidate.primary_name == entity->name
                && normalized_library(candidate) == library) {
                architecture = &candidate;
            }
        }
        if (architecture == nullptr) {
            report(
                "FSIM-ELAB-VHCOMP-003",
                "component '" + component.name
                    + "' has no same-library default architecture",
                component.span);
            result.valid = false;
            return result;
        }
        result.target = *architecture;
        result.applied = true;
    }

    if (!result.target) {
        result.valid = false;
        return result;
    }
    const auto target_library =
        normalized_library(*result.target);
    const auto target_entity_count =
        std::ranges::count_if(
            parsed_.units,
            [&](const auto& candidate) {
              return candidate.kind
                      == frontend::UnitKind::VhdlEntity
                  && candidate.name
                      == result.target->primary_name
                  && normalized_library(candidate)
                      == target_library;
            });
    const auto* entity = find_vhdl_entity(parsed_, *result.target);
    if (entity == nullptr || target_entity_count != 1) {
        report(
            "FSIM-ELAB-VHCOMP-005",
            "bound component target '" + unit_identity(*result.target)
                + "' has no unique entity interface",
            component.span);
        result.valid = false;
        return result;
    }
    entity = &resolved_vhdl_entity_interface(*entity);

    bool profile_valid = true;
    if (component.generics.size() != entity->parameters.size()) {
        report(
            "FSIM-ELAB-VHCOMP-006",
            "component '" + component.name
                + "' generic count does not match entity '"
                + entity->name + "'",
            component.span);
        profile_valid = false;
    }
    if (component.ports.size() != entity->ports.size()) {
        report(
            "FSIM-ELAB-VHCOMP-007",
            "component '" + component.name
                + "' port count does not match entity '"
                + entity->name + "'",
            component.span);
        profile_valid = false;
    }
    if (!profile_valid) {
        result.valid = false;
        return result;
    }

    const auto generic_map = generic_mapping(
        component,
        *entity,
        result.configuration_rule,
        diagnostics_,
        profile_valid);
    const auto port_map = port_mapping(
        component,
        *entity,
        result.configuration_rule,
        diagnostics_,
        profile_valid);
    const auto mapping_is_unique =
        [&](const std::span<const std::size_t> mapping,
            const std::string_view object) {
          std::unordered_set<std::size_t> targets;
          for (const auto target : mapping) {
              if (!targets.insert(target).second) {
                  report(
                      "FSIM-ELAB-VHCOMP-010",
                      "configuration maps multiple component "
                          + std::string{object}
                          + " formals to one entity formal on '"
                          + path + "'",
                      component.span);
                  return false;
              }
          }
          return true;
        };
    profile_valid &=
        mapping_is_unique(generic_map, "generic");
    profile_valid &=
        mapping_is_unique(port_map, "port");
    const auto component_names =
        generic_placeholders(component.generics);
    const auto entity_names =
        generic_placeholders(entity->parameters);
    for (std::size_t index = 0;
         index < component.generics.size(); ++index) {
        const auto target_index = generic_map[index];
        if (target_index >= entity->parameters.size()) {
            profile_valid = false;
            continue;
        }
        const auto& component_generic = component.generics[index];
        const auto& entity_generic = entity->parameters[target_index];
        if (component_generic.kind != frontend::ParameterKind::Value
            || entity_generic.kind != frontend::ParameterKind::Value
            || type_profile(component_generic.type, component_names)
                != type_profile(entity_generic.type, entity_names)) {
            report(
                "FSIM-ELAB-VHCOMP-006",
                "component generic '" + component_generic.name
                    + "' is incompatible with entity generic '"
                    + entity_generic.name + "'",
                component_generic.span);
            profile_valid = false;
        }
    }
    for (std::size_t index = 0;
         index < component.ports.size(); ++index) {
        const auto target_index = port_map[index];
        if (target_index >= entity->ports.size()) {
            profile_valid = false;
            continue;
        }
        const auto& component_port = component.ports[index];
        const auto& entity_port = entity->ports[target_index];
        if (component_port.direction != entity_port.direction
            || type_profile(component_port.type, component_names)
                != type_profile(entity_port.type, entity_names)) {
            report(
                "FSIM-ELAB-VHCOMP-007",
                "component port '" + component_port.name
                    + "' is incompatible with entity port '"
                    + entity_port.name + "'",
                component_port.span);
            profile_valid = false;
        }
    }
    if (!profile_valid) {
        result.valid = false;
        return result;
    }

    std::unordered_set<std::string> explicit_generic_targets;
    std::unordered_set<std::string> explicit_port_targets;
    if (result.configuration_rule != nullptr) {
        for (const auto& actual :
             result.configuration_rule->binding.generic_map) {
            if (actual.name) {
                explicit_generic_targets.insert(*actual.name);
            }
        }
        for (const auto& actual :
             result.configuration_rule->binding.port_map) {
            if (actual.port) {
                explicit_port_targets.insert(*actual.port);
            }
        }
    }

    for (const auto& generic : component.generics) {
        if (!specialized_component.environment.contains(generic.name)) {
            report(
                "FSIM-ELAB-VHCOMP-006",
                "component generic '" + generic.name
                    + "' has no materialized value on '" + path + "'",
                generic.span);
            result.valid = false;
        }
    }
    if (!result.valid) {
        return result;
    }
    const auto literal_actual =
        [&](const std::size_t component_index,
            const std::string& target_name,
            const frontend::SourceSpan& span) {
          const auto value = specialized_component.environment.find(
              component.generics[component_index].name);
          return frontend::ParameterOverride{
              target_name,
              frontend::Expression{
                  frontend::ExpressionKind::IntegerLiteral,
                  std::to_string(value->second),
                  {},
                  span},
              span};
        };
    for (auto& actual : result.instance.parameter_overrides) {
        if (!actual.name) {
            continue;
        }
        std::optional<std::size_t> component_index;
        if (result.configuration_rule != nullptr) {
            const auto binding = std::ranges::find_if(
                result.configuration_rule->binding.generic_map,
                [&](const auto& candidate) {
                  return candidate.name == actual.name
                      && candidate.value.kind
                          == frontend::ExpressionKind::Identifier;
                });
            if (binding
                != result.configuration_rule->binding.generic_map.end()) {
                const auto formal = std::ranges::find_if(
                    component.generics,
                    [&](const auto& candidate) {
                      return candidate.name == binding->value.text;
                    });
                if (formal != component.generics.end()) {
                    component_index = static_cast<std::size_t>(
                        std::distance(
                            component.generics.begin(), formal));
                }
            }
        }
        if (!component_index
            && !explicit_generic_targets.contains(*actual.name)) {
            const auto formal = std::ranges::find_if(
                component.generics,
                [&](const auto& candidate) {
                  return candidate.name == *actual.name;
                });
            if (formal != component.generics.end()) {
                component_index = static_cast<std::size_t>(
                    std::distance(
                        component.generics.begin(), formal));
            }
        }
        if (component_index) {
            const auto target_name =
                entity->parameters[
                    generic_map[*component_index]].name;
            actual = literal_actual(
                *component_index, target_name, actual.span);
        }
    }
    for (std::size_t index = 0;
         index < component.generics.size(); ++index) {
        const auto& target_name =
            entity->parameters[generic_map[index]].name;
        if (std::ranges::none_of(
                result.instance.parameter_overrides,
                [&](const auto& actual) {
                  return actual.name == target_name;
                })) {
            result.instance.parameter_overrides.push_back(
                literal_actual(
                    index,
                    target_name,
                    component.generics[index].span));
        }
    }

    for (auto& actual : result.instance.parameter_overrides) {
        if (!actual.name
            || explicit_generic_targets.contains(*actual.name)) {
            continue;
        }
        const auto found = std::ranges::find_if(
            component.generics,
            [&](const auto& formal) {
              return formal.name == *actual.name;
            });
        if (found != component.generics.end()) {
            const auto index = static_cast<std::size_t>(
                std::distance(component.generics.begin(), found));
            actual.name =
                entity->parameters[generic_map[index]].name;
        }
    }
    for (auto& actual : result.instance.connections) {
        if (!actual.port
            || explicit_port_targets.contains(*actual.port)) {
            continue;
        }
        const auto found = std::ranges::find_if(
            component.ports,
            [&](const auto& formal) {
              return formal.name == *actual.port;
            });
        if (found != component.ports.end()) {
            const auto index = static_cast<std::size_t>(
                std::distance(component.ports.begin(), found));
            actual.port = entity->ports[port_map[index]].name;
        }
    }
    const auto unique_associations =
        [&](const auto& actuals,
            const auto name,
            const std::string_view object) {
          std::unordered_set<std::string> names;
          for (const auto& actual : actuals) {
              const auto selected = name(actual);
              if (selected && !names.insert(*selected).second) {
                  report(
                      "FSIM-ELAB-VHCOMP-010",
                      "component binding produces more than one "
                          + std::string{object} + " actual for '"
                          + *selected + "' on '" + path + "'",
                      actual.span);
                  return false;
              }
          }
          return true;
        };
    result.valid &=
        unique_associations(
            result.instance.parameter_overrides,
            [](const auto& actual) {
              return actual.name;
            },
            "generic");
    result.valid &=
        unique_associations(
            result.instance.connections,
            [](const auto& actual) {
              return actual.port;
            },
            "port");
    if (!result.valid) {
        return result;
    }

    const auto component_source = std::string{
        frontend::physical_source(component.span)};
    if (!component_source.empty()
        && component_source
            != frontend::physical_source(result.target->span)
        && std::ranges::find(
               result.target->source_dependencies,
               component_source)
            == result.target->source_dependencies.end()) {
        result.target->source_dependencies.push_back(
            component_source);
    }
    std::unordered_set<std::string> type_declarations;
    const auto collect_type_declaration =
        [&](const frontend::Type& type) {
          if (!type.vhdl_type_declaration.empty()) {
              type_declarations.insert(
                  type.vhdl_type_declaration);
          }
        };
    for (const auto& generic : component.generics) {
        collect_type_declaration(generic.type);
    }
    for (const auto& port : component.ports) {
        collect_type_declaration(port.type);
    }
    const auto append_type_dependency =
        [&](const std::string& dependency) {
          if (!dependency.empty()
              && dependency
                  != frontend::physical_source(
                         result.target->span)
              && std::ranges::find(
                     result.target->source_dependencies,
                     dependency)
                  == result.target->source_dependencies.end()) {
              result.target->source_dependencies.push_back(
                  dependency);
          }
        };
    for (const auto& source_unit : parsed_.units) {
        for (const auto& alias : source_unit.type_aliases) {
            if (type_declarations.contains(
                    alias.type.vhdl_type_declaration)) {
                append_type_dependency(std::string{
                    frontend::physical_source(alias.span)});
            }
        }
    }
    if (result.configuration_rule != nullptr) {
        const auto configuration_source = std::string{
            frontend::physical_source(
                result.configuration_rule->span)};
        if (!configuration_source.empty()
            && configuration_source
                != frontend::physical_source(result.target->span)
            && std::ranges::find(
                   result.target->source_dependencies,
                   configuration_source)
                == result.target->source_dependencies.end()) {
            result.target->source_dependencies.push_back(
                configuration_source);
        }
    }
    result.component_name = component.name;
    result.component_identity =
        component_identity(component, *result.target);
    if (!result.configuration_identity.empty()) {
        result.component_identity +=
            ";configuration=" + result.configuration_identity;
    }
    return result;
}

}  // namespace fsim::elaboration

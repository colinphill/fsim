// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "fsim/frontend/class_inheritance.hpp"
#include "fsim/frontend/class_resolution.hpp"
#include "fsim/support/sha256.hpp"
#include "hierarchy_port_support.hpp"
namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;
namespace {

constexpr std::size_t specialization_cache_limit = 4U;
constexpr std::size_t maximum_vhdl_mode_view_endpoints = 65'536U;

} // namespace

frontend::Type mode_view_member_type(
    const frontend::PackedMember& member)
{
    if (!member.nested_types.empty()) {
        return member.nested_types.front();
    }
    frontend::Type result;
    result.domain = member.domain;
    result.spelling = member.spelling;
    result.packed_range = member.packed_range;
    result.is_signed = member.is_signed;
    return result;
}

bool materialize_vhdl_mode_view_endpoints(
    const frontend::Type& root_type,
    const frontend::VhdlModeViewIndication& view,
    const runtime::simir::SignalId signal,
    const std::size_t storage_width,
    const std::string& formal,
    const std::string& actual,
    std::vector<VhdlModeViewElementBinding>& output,
    std::string& error)
{
    const auto append_member = [](const std::string& base,
                                   const std::string_view member) {
        return base + "." + std::string { member };
    };
    const auto append_indices = [](const std::string& base,
                                    const std::vector<std::int64_t>& indices) {
        std::string result = base + "(";
        for (std::size_t index = 0; index < indices.size(); ++index) {
            if (index != 0U) {
                result += ',';
            }
            result += std::to_string(indices[index]);
        }
        result += ')';
        return result;
    };
    const auto checked_add = [](const std::uint64_t left,
                                 const std::uint64_t right,
                                 std::uint64_t& result) {
        if (right > std::numeric_limits<std::uint64_t>::max() - left) {
            return false;
        }
        result = left + right;
        return true;
    };

    std::function<bool(const frontend::Type&, std::string_view,
        std::uint64_t, const std::string&, const std::string&,
        frontend::PortDirection, const frontend::SourceSpan&)> descend;
    std::function<bool(const frontend::Type&, std::string_view,
        std::uint64_t, const std::string&, const std::string&,
        frontend::PortDirection, const frontend::SourceSpan&)> expand_array;

    expand_array = [&](const frontend::Type& type,
                       const std::string_view remainder,
                       const std::uint64_t base_offset,
                       const std::string& formal_base,
                       const std::string& actual_base,
                       const frontend::PortDirection direction,
                       const frontend::SourceSpan& source) {
        if (!type.vhdl_array || type.vhdl_array->element_types.size() != 1U
            || !type.vhdl_array->flat_width) {
            error = "array-view endpoint requires one concrete array element subtype";
            return false;
        }
        const auto& array = *type.vhdl_array;
        if (array.dimensions.empty()) {
            error = "array-view endpoint requires at least one concrete dimension";
            return false;
        }
        if (std::ranges::any_of(array.dimensions, [](const auto& dimension) {
                return !dimension.range || dimension.unconstrained
                    || (!dimension.null && dimension.stride == 0U);
            })) {
            error = "array-view endpoint has an unconstrained or incomplete layout";
            return false;
        }
        if (std::ranges::any_of(array.dimensions,
                [](const auto& dimension) { return dimension.null; })) {
            return true;
        }

        std::vector<std::int64_t> indices(array.dimensions.size());
        std::function<bool(std::size_t, std::uint64_t)> visit;
        visit = [&](const std::size_t dimension_index,
                    const std::uint64_t offset) {
            if (dimension_index == array.dimensions.size()) {
                return descend(
                    array.element_types.front(), remainder, offset,
                    append_indices(formal_base, indices),
                    append_indices(actual_base, indices), direction, source);
            }
            const auto& dimension = array.dimensions[dimension_index];
            const auto& range = *dimension.range;
            const auto unsigned_left = static_cast<std::uint64_t>(range.left);
            const auto unsigned_right = static_cast<std::uint64_t>(range.right);
            const auto distance = range.left >= range.right
                ? unsigned_left - unsigned_right
                : unsigned_right - unsigned_left;
            if (distance == std::numeric_limits<std::uint64_t>::max()
                || distance + 1U
                    > maximum_vhdl_mode_view_endpoints - output.size()) {
                error = "array-view endpoint count exceeds the bounded limit";
                return false;
            }
            const auto count = distance + 1U;
            for (std::uint64_t ordinal = 0; ordinal < count; ++ordinal) {
                indices[dimension_index] = range.descending
                    ? range.left - static_cast<std::int64_t>(ordinal)
                    : range.left + static_cast<std::int64_t>(ordinal);
                const auto packed_distance = count - ordinal - 1U;
                if (dimension.stride != 0U
                    && packed_distance
                        > std::numeric_limits<std::uint64_t>::max()
                            / dimension.stride) {
                    error = "array-view endpoint offset overflows packed storage";
                    return false;
                }
                std::uint64_t element_offset { };
                if (!checked_add(offset,
                        packed_distance * dimension.stride,
                        element_offset)
                    || !visit(dimension_index + 1U, element_offset)) {
                    return false;
                }
            }
            return true;
        };
        return visit(0U, base_offset);
    };

    descend = [&](const frontend::Type& type,
                  const std::string_view path,
                  const std::uint64_t base_offset,
                  const std::string& formal_base,
                  const std::string& actual_base,
                  const frontend::PortDirection direction,
                  const frontend::SourceSpan& source) {
        if (path.empty()) {
            const auto width = type.width();
            if (!width || *width == 0U || base_offset > storage_width
                || *width > storage_width - base_offset) {
                error = "view endpoint lies outside its associated packed signal";
                return false;
            }
            if (output.size() >= maximum_vhdl_mode_view_endpoints) {
                error = "view endpoint count exceeds the bounded limit";
                return false;
            }
            output.push_back({
                formal_base, actual_base, direction, source, signal,
                base_offset, *width });
            return true;
        }
        const auto separator = path.find('.');
        const auto component = path.substr(0, separator);
        const auto remainder = separator == std::string_view::npos
            ? std::string_view { }
            : path.substr(separator + 1U);
        if (component == "(<>)") {
            return expand_array(
                type, remainder, base_offset, formal_base, actual_base,
                direction, source);
        }
        const bool selects_array = component.ends_with("(<>)");
        const auto member_name = selects_array
            ? component.substr(0, component.size() - 4U)
            : component;
        const auto member = std::ranges::find(
            type.packed_members, member_name,
            &frontend::PackedMember::name);
        if (member == type.packed_members.end()) {
            error = "view endpoint member '" + std::string { member_name }
                + "' is absent from its associated record";
            return false;
        }
        std::uint64_t member_offset { };
        if (!checked_add(base_offset, member->lsb_offset, member_offset)) {
            error = "view endpoint member offset overflows packed storage";
            return false;
        }
        auto member_type = mode_view_member_type(*member);
        const auto formal_member = append_member(formal_base, member_name);
        const auto actual_member = append_member(actual_base, member_name);
        if (selects_array) {
            return expand_array(
                member_type, remainder, member_offset,
                formal_member, actual_member, direction, source);
        }
        return descend(
            member_type, remainder, member_offset,
            formal_member, actual_member, direction, source);
    };

    for (const auto& element : view.elements) {
        if (!descend(root_type, element.path, 0U, formal, actual,
                element.direction, element.span)) {
            output.clear();
            return false;
        }
    }
    return true;
}

namespace {

void append_specialization_key_component(
    std::string& key,
    const std::string_view component)
{
    key += std::to_string(component.size());
    key += ':';
    key += component;
}

void append_specialization_expression(
    std::string& key,
    const frontend::Expression& expression)
{
    append_specialization_key_component(key,
        std::to_string(static_cast<unsigned>(expression.kind)));
    append_specialization_key_component(key, expression.text);
    append_specialization_key_component(key, expression.nominal_type);
    for (const auto& operand : expression.operands) {
        append_specialization_expression(key, operand);
    }
    for (const auto& choices :
        expression.aggregate_choice_expressions) {
        for (const auto& choice : choices) {
            append_specialization_expression(key, choice);
        }
    }
}

template <typename Environment, typename Identity>
void append_sorted_environment(
    std::string& key,
    const Environment& environment,
    Identity identity)
{
    std::vector<const typename Environment::value_type*> entries;
    entries.reserve(environment.size());
    for (const auto& entry : environment) {
        entries.push_back(&entry);
    }
    std::ranges::sort(entries, {}, [](const auto* entry) {
        return entry->first;
    });
    for (const auto* entry : entries) {
        append_specialization_key_component(key, entry->first);
        append_specialization_key_component(key, identity(entry->second));
    }
}

std::string specialization_cache_key(
    const frontend::DesignUnit& selected,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const SystemVerilogConstantEnvironment& parent_integral_environment,
    const ConstantDomainEnvironment& parent_domains,
    const NamedTypeEnvironment& parent_types,
    const std::vector<frontend::FunctionDeclaration>& parent_functions,
    const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
    const PackageEnvironment& parent_packages,
    const frontend::Language association_language)
{
    std::string key;
    append_specialization_key_component(key,
        unit_identity(selected));
    append_specialization_key_component(key,
        frontend::physical_source(selected.span));
    append_specialization_key_component(key,
        std::to_string(selected.span.begin.offset));
    append_specialization_key_component(key,
        std::to_string(selected.span.end.offset));
    append_specialization_key_component(key,
        std::to_string(static_cast<unsigned>(selected.kind)));
    append_specialization_key_component(key,
        std::to_string(static_cast<unsigned>(selected.language)));
    append_specialization_key_component(key,
        std::to_string(static_cast<unsigned>(association_language)));
    for (const auto& override : overrides) {
        append_specialization_key_component(key,
            override.name.value_or(std::string { }));
        append_specialization_key_component(key,
            std::to_string(override.default_box));
        append_specialization_expression(key, override.value);
        if (override.type_value) {
            append_specialization_key_component(key,
                vhdl_type_identity(*override.type_value));
        }
    }
    append_sorted_environment(key, parent_environment,
        [](const auto value) { return std::to_string(value); });
    append_sorted_environment(key, parent_integral_environment,
        [](const auto& value) { return value.canonical(); });
    append_sorted_environment(key, parent_domains,
        [](const auto& domain) {
            std::string identity =
                std::to_string(static_cast<unsigned>(domain.domain))
                + ':' + std::to_string(domain.vhdl_enumeration)
                + ':' + domain.nominal_type;
            if (domain.vhdl_composite_value) {
                append_specialization_expression(
                    identity, *domain.vhdl_composite_value);
            }
            return identity;
        });
    append_sorted_environment(key, parent_types,
        [](const auto& binding) {
            return vhdl_type_identity(binding.type);
        });
    for (const auto& function : parent_functions) {
        append_specialization_key_component(key, function.name);
        append_specialization_key_component(
            key, function.specialization_identity);
        append_specialization_key_component(
            key, function.visibility_owner);
        append_specialization_key_component(key,
            frontend::physical_source(function.span));
        append_specialization_key_component(key,
            std::to_string(function.span.begin.offset));
        append_specialization_key_component(key,
            std::to_string(function.span.end.offset));
        append_specialization_key_component(key,
            vhdl_type_identity(function.return_type));
        for (const auto& argument : function.arguments) {
            append_specialization_key_component(key,
                vhdl_type_identity(argument.type));
        }
    }
    for (const auto& procedure : parent_procedures) {
        append_specialization_key_component(key, procedure.name);
        append_specialization_key_component(
            key, procedure.specialization_identity);
        append_specialization_key_component(
            key, procedure.visibility_owner);
        append_specialization_key_component(key,
            frontend::physical_source(procedure.span));
        append_specialization_key_component(key,
            std::to_string(procedure.span.begin.offset));
        append_specialization_key_component(key,
            std::to_string(procedure.span.end.offset));
        for (const auto& argument : procedure.arguments) {
            append_specialization_key_component(key,
                vhdl_type_identity(argument.type));
        }
    }
    std::vector<const PackageEnvironment::value_type*> packages;
    packages.reserve(parent_packages.size());
    for (const auto& package : parent_packages) {
        packages.push_back(&package);
    }
    std::ranges::sort(packages, {}, [](const auto* package) {
        return package->first;
    });
    for (const auto* package : packages) {
        append_specialization_key_component(key, package->first);
        append_specialization_key_component(
            key, package->second.template_name);
        for (const auto& [name, value] :
            package->second.identity_values) {
            append_specialization_key_component(key, name);
            append_specialization_key_component(key, value);
        }
        append_sorted_environment(key, package->second.environment,
            [](const auto value) { return std::to_string(value); });
    }
    return support::Sha256::hex(support::Sha256::digest(key));
}

} // namespace

DesignUnit HierarchyBuilder::effective_unit(
    const DesignUnit& selected,
    const DesignUnit* entity_override)
{
    auto result = selected;
    if (selected.kind
            == frontend::UnitKind::VerilogModule
        || selected.kind
            == frontend::UnitKind::SystemVerilogProgram) {
        std::vector<const DesignUnit*> import_stack;
        NamedTypeEnvironment type_environment;
        import_systemverilog_package_items(
            result, import_stack, type_environment);
        import_qualified_systemverilog_package_items(
            result, import_stack, type_environment);
        for (const auto& parameter : result.parameters) {
            if (parameter.kind
                != frontend::ParameterKind::Type) {
                continue;
            }
            type_environment.insert_or_assign(
                parameter.name,
                NamedTypeBinding {
                    { },
                    (result.library.empty()
                            ? std::string { "work" }
                            : result.library)
                        + "." + result.name,
                    true });
        }
        resolve_named_types(
            result, type_environment);
        for (const auto& [name, binding] : type_environment) {
            if (binding.interface_formal
                || std::any_of(
                    result.type_aliases.begin(),
                    result.type_aliases.end(),
                    [&](const auto& alias) {
                        return alias.name == name;
                    })) {
                continue;
            }
            result.type_aliases.push_back(
                frontend::TypeAliasDeclaration {
                    name,
                    binding.type,
                    result.span,
                    { },
                    frontend::TypeDeclarationKind::
                        SystemVerilogTypedef,
                    { },
                    { },
                    { },
                    false });
        }
        return result;
    }
    if (selected.kind
        != frontend::UnitKind::VhdlArchitecture) {
        return result;
    }
    const auto* entity = entity_override != nullptr
        ? entity_override
        : find_vhdl_entity(parsed_, selected);
    if (entity == nullptr) {
        return result;
    }
    std::vector<frontend::ParameterDeclaration>
        architecture_constants;
    for (auto& parameter : result.parameters) {
        if (parameter.local) {
            architecture_constants.push_back(
                std::move(parameter));
        }
    }
    result.parameters = entity->parameters;
    result.parameters.insert(
        result.parameters.end(),
        std::make_move_iterator(
            architecture_constants.begin()),
        std::make_move_iterator(
            architecture_constants.end()));
    result.ports = entity->ports;
    std::vector<frontend::VhdlComponentDeclaration>
        entity_components = entity->vhdl_component_declarations;
    for (auto& component : entity_components) {
        component.region = frontend::VhdlComponentDeclarationRegion::Entity;
        component.owner_library = entity->library.empty()
            ? std::string { "work" }
            : entity->library;
        component.owner_name = entity->name;
    }
    result.vhdl_component_declarations.insert(
        result.vhdl_component_declarations.begin(),
        std::make_move_iterator(
            entity_components.begin()),
        std::make_move_iterator(
            entity_components.end()));
    result.package_instances.insert(
        result.package_instances.begin(),
        entity->package_instances.begin(),
        entity->package_instances.end());
    result.generic_function_templates.insert(
        result.generic_function_templates.begin(),
        entity->generic_function_templates.begin(),
        entity->generic_function_templates.end());
    result.generic_procedure_templates.insert(
        result.generic_procedure_templates.begin(),
        entity->generic_procedure_templates.begin(),
        entity->generic_procedure_templates.end());
    result.generic_function_instances.insert(
        result.generic_function_instances.begin(),
        entity->generic_function_instances.begin(),
        entity->generic_function_instances.end());
    result.generic_procedure_instances.insert(
        result.generic_procedure_instances.begin(),
        entity->generic_procedure_instances.begin(),
        entity->generic_procedure_instances.end());
    for (const auto& function : entity->functions) {
        if (function.name.find('.') == std::string::npos
            || std::ranges::any_of(
                result.functions,
                [&](const auto& existing) {
                    return existing.name == function.name;
                })) {
            continue;
        }
        result.functions.push_back(function);
    }
    for (const auto& procedure : entity->procedures) {
        if (procedure.name.find('.') == std::string::npos
            || std::ranges::any_of(
                result.procedures,
                [&](const auto& existing) {
                    return existing.name == procedure.name;
                })) {
            continue;
        }
        result.procedures.push_back(procedure);
    }
    for (const auto& dependency :
        entity->source_dependencies) {
        if (std::ranges::find(
                result.source_dependencies, dependency)
            == result.source_dependencies.end()) {
            result.source_dependencies.push_back(dependency);
        }
    }
    for (const auto& generic : result.parameters) {
        if (std::any_of(
                result.signals.begin(),
                result.signals.end(),
                [&](const frontend::SignalDeclaration& signal) {
                    return signal.name == generic.name;
                })) {
            report(
                "FSIM-ELAB-GENERIC-009",
                "architecture object '" + generic.name
                    + "' conflicts with an entity generic",
                generic.span);
        }
    }
    std::vector<frontend::VhdlContextItem> context = entity->vhdl_context;
    context.insert(
        context.end(),
        selected.vhdl_context.begin(),
        selected.vhdl_context.end());
    std::vector<frontend::VhdlContextItem> expanded_context;
    std::vector<const DesignUnit*> context_stack;
    const auto unit_library = result.library.empty()
        ? std::string { "work" }
        : result.library;
    expand_vhdl_context_references(
        result,
        context,
        expanded_context,
        context_stack,
        unit_library);
    std::vector<const DesignUnit*> import_stack;
    NamedTypeEnvironment type_environment;
    import_vhdl_package_constants(
        result,
        expanded_context,
        import_stack,
        type_environment);
    import_qualified_vhdl_package_constants(
        result, import_stack);
    for (const auto& alias : entity->type_aliases) {
        if (alias.name.find('.') == std::string::npos) {
            continue;
        }
        type_environment.insert_or_assign(
            alias.name,
            NamedTypeBinding {
                alias.type,
                (entity->library.empty()
                        ? std::string { "work" }
                        : entity->library)
                    + "." + entity->name });
    }
    import_qualified_vhdl_package_types(
        result, type_environment, import_stack);
    for (const auto& generic : result.parameters) {
        if (generic.kind
            != frontend::ParameterKind::Type) {
            continue;
        }
        type_environment.insert_or_assign(
            generic.name,
            NamedTypeBinding {
                { },
                (entity->library.empty()
                        ? std::string { "work" }
                        : entity->library)
                    + "." + entity->name,
                true });
    }
    // Entity interfaces have their own declarative region. Resolve them
    // without exposing architecture-local type declarations, then merge
    // the typed ports back into the architecture specialization.
    // Generic and port clauses precede the entity declarative part in
    // VHDL. Resolve the interface without entity-local type declarations,
    // then resolve those declarations separately for architecture
    // visibility.
    auto effective_interface = *entity;
    effective_interface.type_aliases.clear();
    // A mode view is a declaration rather than a named type, so it is not
    // carried by type_environment. Rebuild only the entity interface's own
    // package visibility and retain those declarations while keeping
    // entity-local declarations hidden from the preceding port clause.
    DesignUnit interface_visibility;
    interface_visibility.language = entity->language;
    interface_visibility.library = entity->library;
    interface_visibility.name = entity->name;
    interface_visibility.span = entity->span;
    std::vector<frontend::VhdlContextItem> interface_context;
    std::vector<const DesignUnit*> interface_context_stack;
    expand_vhdl_context_references(
        interface_visibility,
        entity->vhdl_context,
        interface_context,
        interface_context_stack,
        unit_library);
    std::vector<const DesignUnit*> interface_import_stack;
    NamedTypeEnvironment interface_imported_types;
    import_vhdl_package_constants(
        interface_visibility,
        interface_context,
        interface_import_stack,
        interface_imported_types);
    for (auto& declaration : interface_visibility.type_aliases) {
        if (declaration.declaration_kind
            == frontend::TypeDeclarationKind::VhdlModeView) {
            effective_interface.type_aliases.push_back(
                std::move(declaration));
        }
    }
    resolve_named_types(
        effective_interface, type_environment, true);
    for (const auto& generic :
        effective_interface.parameters) {
        if (generic.kind
            == frontend::ParameterKind::Type) {
            continue;
        }
        const auto resolved = std::find_if(
            result.parameters.begin(),
            result.parameters.end(),
            [&](const auto& candidate) {
                return candidate.name == generic.name
                    && candidate.span.source_name
                    == generic.span.source_name
                    && candidate.span.begin.offset
                    == generic.span.begin.offset;
            });
        if (resolved != result.parameters.end()) {
            if (generic.kind
                == frontend::ParameterKind::Function) {
                resolved->function_profile = generic.function_profile;
            } else if (
                generic.kind
                == frontend::ParameterKind::Procedure) {
                resolved->procedure_profile = generic.procedure_profile;
            } else if (
                generic.kind
                == frontend::ParameterKind::Package) {
                resolved->package_profile = generic.package_profile;
            } else {
                resolved->type = generic.type;
            }
        }
        if (generic.kind
                == frontend::ParameterKind::Function
            || generic.kind
                == frontend::ParameterKind::Procedure
            || generic.kind
                == frontend::ParameterKind::Package) {
            continue;
        }
        validate_vhdl_generic_type(generic);
    }
    result.ports = std::move(effective_interface.ports);
    auto effective_entity_declarations = *entity;
    effective_entity_declarations.parameters.clear();
    effective_entity_declarations.ports.clear();
    resolve_named_types(
        effective_entity_declarations,
        type_environment,
        true,
        false);
    for (const auto& alias :
        effective_entity_declarations.type_aliases) {
        type_environment.insert_or_assign(
            alias.name,
            NamedTypeBinding {
                alias.type,
                (entity->library.empty()
                        ? std::string { "work" }
                        : entity->library)
                    + "." + entity->name });
    }
    std::erase_if(
        result.vhdl_component_declarations,
        [](const auto& component) {
            return component.region
                == frontend::VhdlComponentDeclarationRegion::
                    Entity;
        });
    auto resolved_entity_components = std::move(
        effective_entity_declarations
            .vhdl_component_declarations);
    for (auto& component : resolved_entity_components) {
        component.region = frontend::VhdlComponentDeclarationRegion::Entity;
        component.owner_library = entity->library.empty()
            ? std::string { "work" }
            : entity->library;
        component.owner_name = entity->name;
    }
    result.vhdl_component_declarations.insert(
        result.vhdl_component_declarations.begin(),
        std::make_move_iterator(
            resolved_entity_components.begin()),
        std::make_move_iterator(
            resolved_entity_components.end()));
    resolve_named_types(
        result, type_environment, true, false);
    for (const auto& [name, binding] : type_environment) {
        if (binding.interface_formal
            || std::any_of(
                result.type_aliases.begin(),
                result.type_aliases.end(),
                [&](const auto& alias) {
                    return alias.name == name;
                })) {
            continue;
        }
        result.type_aliases.push_back(
            frontend::TypeAliasDeclaration {
                name,
                binding.type,
                result.span,
                { },
                frontend::TypeDeclarationKind::Alias,
                { },
                { },
                { },
                false });
    }
    for (auto& component :
        result.vhdl_component_declarations) {
        if (component.owner_library.empty()) {
            component.owner_library = result.library.empty()
                ? std::string { "work" }
                : result.library;
        }
        if (component.owner_name.empty()) {
            component.owner_name = component.region
                    == frontend::
                        VhdlComponentDeclarationRegion::
                            Entity
                ? entity->name
                : result.name;
        }
    }
    return result;
}
SpecializedUnit HierarchyBuilder::specialize_selected_unit(
    const DesignUnit& selected,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const SystemVerilogConstantEnvironment&
        parent_integral_environment,
    const ConstantDomainEnvironment& parent_domains,
    const NamedTypeEnvironment& parent_types,
    const std::vector<frontend::FunctionDeclaration>& parent_functions,
    const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
    const PackageEnvironment& parent_packages,
    const frontend::Language association_language)
{
    const auto diagnostics_before = diagnostics_.size();
    auto normalized_overrides = overrides;
    if (selected.language
        == frontend::Language::SystemVerilog2017) {
        const auto annotate_nominal_actual =
            [&](auto&& self, frontend::Expression& expression)
            -> void {
            if (expression.kind
                == frontend::ExpressionKind::Identifier) {
                const auto found = parent_domains.find(expression.text);
                if (found != parent_domains.end()
                    && !found->second.nominal_type.empty()) {
                    expression.nominal_type = found->second.nominal_type;
                }
            }
            for (auto& operand : expression.operands) {
                self(self, operand);
            }
            for (auto& choices :
                expression.aggregate_choice_expressions) {
                for (auto& choice : choices) {
                    self(self, choice);
                }
            }
        };
        for (auto& override : normalized_overrides) {
            annotate_nominal_actual(
                annotate_nominal_actual, override.value);
        }
    }
    if (association_language
            == frontend::Language::SystemVerilog2017
        || association_language
            == frontend::Language::Verilog2005) {
        for (auto& override : normalized_overrides) {
            std::string error;
            const auto value =
                evaluate_systemverilog_constant_function_expression(
                    override.value,
                    parent_integral_environment,
                    parent_environment,
                    parent_functions,
                    error);
            if (value) {
                override.value = value->expression(
                    override.value.span);
            }
        }
    }
    if (association_language == frontend::Language::Vhdl2008) {
        for (auto& override : normalized_overrides) {
            std::string error;
            const auto value =
                evaluate_systemverilog_constant_function_expression(
                    override.value,
                    parent_integral_environment,
                    parent_environment,
                    parent_functions,
                    error);
            if (value) {
                override.value = value->expression(
                    override.value.span);
            }
        }
    }
    const auto cache_key = specialization_cache_key(
        selected,
        normalized_overrides,
        parent_environment,
        parent_integral_environment,
        parent_domains,
        parent_types,
        parent_functions,
        parent_procedures,
        parent_packages,
        association_language);
    if (const auto cached = specialized_unit_cache_.find(cache_key);
        cached != specialized_unit_cache_.end()) {
        const auto retained = std::ranges::find(
            cached_specialization_lru_, cache_key);
        if (retained != cached_specialization_lru_.end()) {
            std::rotate(
                retained,
                retained + 1,
                cached_specialization_lru_.end());
        }
        return cached->second;
    }
    PackageEnvironment interface_packages;
    std::vector<std::pair<std::string, std::string>>
        package_identities;
    std::optional<DesignUnit> selected_override;
    std::optional<DesignUnit> entity_override;
    if (selected.language
        == frontend::Language::Vhdl2008) {
        if (selected.kind
            == frontend::UnitKind::VhdlArchitecture) {
            if (const auto* entity = find_vhdl_entity(parsed_, selected);
                entity != nullptr) {
                entity_override = *entity;
                if (std::ranges::any_of(
                        entity_override->parameters,
                        [](const auto& parameter) {
                            return parameter.kind
                                == frontend::ParameterKind::
                                    Package;
                        })) {
                    bind_vhdl_interface_packages(
                        *entity_override,
                        normalized_overrides,
                        parent_packages,
                        parent_environment,
                        parent_domains,
                        parent_types,
                        parent_functions,
                        parent_procedures,
                        association_language,
                        interface_packages,
                        package_identities);
                }
            }
        } else {
            selected_override = selected;
            if (std::ranges::any_of(
                    selected_override->parameters,
                    [](const auto& parameter) {
                        return parameter.kind
                            == frontend::ParameterKind::Package;
                    })) {
                bind_vhdl_interface_packages(
                    *selected_override,
                    normalized_overrides,
                    parent_packages,
                    parent_environment,
                    parent_domains,
                    parent_types,
                    parent_functions,
                    parent_procedures,
                    association_language,
                    interface_packages,
                    package_identities);
            }
        }
    }
    auto effective = effective_unit(
        selected_override ? *selected_override : selected,
        entity_override ? &*entity_override : nullptr);
    if (effective.language
        == frontend::Language::SystemVerilog2017) {
        expand_systemverilog_lets(effective, diagnostics_);
    }
    auto type_specialized = selected.language
            == frontend::Language::SystemVerilog2017
        ? specialize_systemverilog_type_parameters(
              std::move(effective),
              normalized_overrides,
              parent_environment,
              parent_types,
              association_language,
              diagnostics_)
        : specialize_vhdl_interface_types(
              std::move(effective),
              normalized_overrides,
              parent_environment,
              parent_domains,
              parent_types,
              parent_functions,
              parent_procedures,
              association_language,
              diagnostics_);
    if (type_specialized.applied) {
        resolve_named_types(
            type_specialized.unit,
            { },
            selected.language
                == frontend::Language::Vhdl2008);
    }
    auto specialized = specialize_unit(
        type_specialized.unit,
        type_specialized.value_overrides,
        parent_environment,
        association_language,
        diagnostics_,
        false,
        parent_integral_environment,
        parent_functions);
    // specialize_unit owns a complete copy of the transformed unit.  Keep the
    // independently returned type/value metadata, but release the now-dead AST
    // before local-package and generate expansion grow the final unit.
    type_specialized.unit = { };
    if (selected.language
            == frontend::Language::SystemVerilog2017
        && type_specialized.applied) {
        for (auto& [name, identity] :
            type_specialized.values) {
            const auto alias = std::ranges::find_if(
                specialized.unit.type_aliases,
                [&](const auto& candidate) {
                    return candidate.name == name;
                });
            const auto resolved = alias
                    == specialized.unit.type_aliases.end()
                ? std::optional<std::string> { }
                : systemverilog_type_parameter_identity(
                      alias->type);
            if (!resolved) {
                report(
                    "FSIM-ELAB-SVTYPEPARAM-003",
                    "specialized data type for type parameter '"
                        + name
                        + "' is outside the bounded 1-64-bit "
                          "packed integral subset",
                    alias
                            == specialized.unit.type_aliases.end()
                        ? selected.span
                        : alias->span);
                continue;
            }
            identity = *resolved;
        }
    }
    if (specialized.identity_values.empty()) {
        specialized.identity_values = specialized.values;
    }
    if (!type_specialized.values.empty()) {
        const auto value_values = std::move(specialized.values);
        const auto value_identities = std::move(specialized.identity_values);
        std::unordered_set<std::string> consumed_values;
        std::unordered_set<std::string> consumed_types;
        const auto append_named =
            [](auto& destination,
                const auto& source_values,
                const std::string_view name) {
                const auto found = std::ranges::find_if(
                    source_values,
                    [&](const auto& value) {
                        return value.first == name;
                    });
                if (found != source_values.end()) {
                    destination.push_back(*found);
                    return true;
                }
                return false;
            };
        for (const auto& parameter : effective.parameters) {
            if (parameter.kind
                != frontend::ParameterKind::Value) {
                if (append_named(
                        specialized.values,
                        type_specialized.values,
                        parameter.name)) {
                    (void)append_named(
                        specialized.identity_values,
                        type_specialized.values,
                        parameter.name);
                    consumed_types.insert(parameter.name);
                }
            } else if (append_named(
                           specialized.values,
                           value_values,
                           parameter.name)) {
                (void)append_named(
                    specialized.identity_values,
                    value_identities,
                    parameter.name);
                consumed_values.insert(parameter.name);
            }
        }
        for (const auto& value : type_specialized.values) {
            if (!consumed_types.contains(value.first)) {
                specialized.values.push_back(value);
                specialized.identity_values.push_back(value);
            }
        }
        for (const auto& value : value_values) {
            if (consumed_values.contains(value.first)) {
                continue;
            }
            specialized.values.push_back(value);
            (void)append_named(
                specialized.identity_values,
                value_identities,
                value.first);
        }
    }
    for (const auto& identity : package_identities) {
        specialized.values.push_back(identity);
        specialized.identity_values.push_back(identity);
    }
    specialized.packages = interface_packages;
    materialize_vhdl_local_declarations(specialized);
    instantiate_vhdl_local_packages(
        specialized, interface_packages);
    instantiate_vhdl_generic_subprograms(
        specialized);
    materialize_vhdl_local_declarations(specialized);
    instantiate_vhdl_local_packages(
        specialized, interface_packages);
    const auto source_class_count =
        specialized.unit.systemverilog_classes.size();
    expand_vhdl_block_generates(specialized);
    if (specialized.unit.language
            == frontend::Language::SystemVerilog2017
        && specialized.unit.systemverilog_classes.size()
            != source_class_count) {
        frontend::ParsedDesign expanded_design;
        expanded_design.systemverilog_classes =
            parsed_.systemverilog_classes;
        expanded_design.systemverilog_class_method_definitions =
            parsed_.systemverilog_class_method_definitions;
        for (const auto& candidate : parsed_.units) {
            if (candidate.kind
                == frontend::UnitKind::SystemVerilogPackage) {
                expanded_design.units.push_back(candidate);
            }
        }
        expanded_design.units.push_back(std::move(specialized.unit));
        std::vector<frontend::Diagnostic> class_diagnostics;
        const bool classes_resolved =
            frontend::resolve_systemverilog_classes(
                expanded_design, class_diagnostics);
        if (classes_resolved) {
            (void)frontend::validate_systemverilog_class_inheritance(
                expanded_design, class_diagnostics);
        }
        specialized.unit = std::move(expanded_design.units.back());
        for (auto& diagnostic : class_diagnostics) {
            diagnostics_.push_back({
                std::move(diagnostic.code),
                std::move(diagnostic.message),
                std::move(diagnostic.span)});
        }
    }
    if (diagnostics_.size() == diagnostics_before) {
        // A one-use specialization only bloats the retained frontend model.
        // Admit entries on reuse and keep a small working set for every HDL;
        // the cache is an elaboration accelerator, not design state.
        const bool admit = !seen_specialization_keys_.insert(
            cache_key).second;
        if (admit) {
            const auto inserted = specialized_unit_cache_.try_emplace(
                cache_key, specialized).second;
            if (inserted) {
                cached_specialization_lru_.push_back(cache_key);
                while (cached_specialization_lru_.size()
                    > specialization_cache_limit) {
                    specialized_unit_cache_.erase(
                        cached_specialization_lru_.front());
                    cached_specialization_lru_.erase(
                        cached_specialization_lru_.begin());
                }
            }
        }
    }
    return specialized;
}
std::optional<SignalId> HierarchyBuilder::add_owned_signal(
    const frontend::SignalDeclaration& declaration,
    const std::string_view path,
    SignalMap& local)
{
    if (const auto existing = local.find(declaration.name);
        existing != local.end()) {
        return existing->second;
    }
    if (declaration.type.domain == frontend::ValueDomain::Unknown
        && declaration.type.systemverilog_scalar
            == frontend::SystemVerilogScalarKind::None
        && declaration.type.systemverilog_class_declaration.empty()) {
        report(
            "FSIM-ELAB-TYPE-001",
            "signal '" + declaration.name
                + "' has a type that the packed simulation runtime "
                  "cannot represent",
            declaration.span);
        return std::nullopt;
    }
    if (declaration.type.vhdl_array
        && !declaration.type.width()) {
        report(
            "FSIM-ELAB-VHARRAY-005",
            "VHDL array object '" + declaration.name
                + "' requires a concrete index constraint",
            declaration.span);
        return std::nullopt;
    }
    const auto separator = declaration.type.spelling.find_last_of('.');
    const auto simple_type_name = declaration.type.spelling.substr(
        separator == std::string::npos
            ? 0
            : separator + 1);
    if (!declaration.type.packed_range
        && !declaration.type.packed_range_expression
        && !(declaration.type.vhdl_array
            && declaration.type.vhdl_array->flat_width)
        && (simple_type_name == "bit_vector"
            || simple_type_name == "std_logic_vector"
            || simple_type_name == "std_ulogic_vector")) {
        report(
            "FSIM-ELAB-VHARRAY-005",
            "VHDL array object '" + declaration.name
                + "' requires a concrete non-null index constraint",
            declaration.span);
        return std::nullopt;
    }
    const auto width = declaration.type.width().value_or(1);
    const bool null_vhdl_array
        = (declaration.type.vhdl_array
              && declaration.type.vhdl_array->flat_width
              && *declaration.type.vhdl_array->flat_width == 0)
        || (declaration.type.packed_range
            && declaration.type.packed_range->width() == 0
            && (simple_type_name == "bit_vector"
                || simple_type_name == "std_logic_vector"
                || simple_type_name == "std_ulogic_vector"));
    if ((!null_vhdl_array && width == 0)
        || width > std::numeric_limits<std::size_t>::max()) {
        report(
            "FSIM-ELAB-010",
            "signal '" + declaration.name + "' has an invalid width",
            declaration.span);
        return std::nullopt;
    }
    if (design_.signals_.size()
        > std::numeric_limits<SignalId>::max()) {
        report(
            "FSIM-ELAB-011",
            "the design has too many signals for dense 32-bit IDs",
            declaration.span);
        return std::nullopt;
    }
    const auto id = static_cast<SignalId>(design_.signals_.size());
    const auto full_name = std::string(path) + "." + declaration.name;
    local.emplace(declaration.name, id);
    local.emplace(full_name, id);
    design_.signal_by_name_.emplace(full_name, id);
    if (design_.roots_.size() == 1 && path == active_root_) {
        design_.signal_by_name_.emplace(declaration.name, id);
    }
    design_.signal_info_.push_back({ id,
        full_name,
        static_cast<std::size_t>(width),
        declaration.type.spelling,
        declaration.type.domain,
        declaration.type.systemverilog_scalar,
        declaration.type.systemverilog_net_type,
        declaration.type.is_signed,
        declaration.type.packed_range,
        declaration.type.vhdl_array
            ? std::make_shared<frontend::VhdlArrayInfo>(
                  *declaration.type.vhdl_array)
            : nullptr,
        declaration.type.vhdl_access
            ? std::make_shared<frontend::VhdlAccessInfo>(
                  *declaration.type.vhdl_access)
            : nullptr,
        declaration.type.vhdl_physical
            ? std::make_shared<frontend::VhdlPhysicalInfo>(
                  *declaration.type.vhdl_physical)
            : nullptr,
        declaration.type.packed_members,
        declaration.type.integer_range,
        declaration.type.nominal_type,
        declaration.type.enumeration_literals,
        declaration.type.enumeration_range,
        declaration.is_port,
        declaration.direction,
        declaration.span,
        { } });
    if (!declaration.type.vhdl_resolution_function.empty()) {
        resolver_by_signal_.insert_or_assign(
            id, declaration.type.vhdl_resolution_function);
    } else if (!declaration.type.systemverilog_resolution_function.empty()) {
        resolver_by_signal_.insert_or_assign(
            id, declaration.type.systemverilog_resolution_function);
    }
    auto initial = declaration.type.systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None
            && declaration.type.systemverilog_scalar
                != frontend::SystemVerilogScalarKind::Time
        ? Logic4::zero
        : Logic4::x;
    if (declaration.type.spelling == "event") {
        initial = Logic4::zero;
    } else if (declaration.type.spelling == "tri0") {
        initial = Logic4::zero;
    } else if (declaration.type.spelling == "tri1") {
        initial = Logic4::one;
    } else if (declaration.type.spelling == "supply0") {
        initial = Logic4::zero;
    } else if (declaration.type.spelling == "supply1") {
        initial = Logic4::one;
    } else if (is_two_state_domain(declaration.type.domain)) {
        initial = Logic4::zero;
    } else if (!declaration.type.systemverilog_net_type.empty()) {
        initial = Logic4::z;
    } else if (
        declaration.type.domain == frontend::ValueDomain::Logic4
        && (declaration.type.spelling == "wire"
            || declaration.type.spelling == "tri"
            || declaration.type.spelling == "wand"
            || declaration.type.spelling == "triand"
            || declaration.type.spelling == "wor"
            || declaration.type.spelling == "trior"
            || declaration.type.spelling == "trireg"
            || declaration.type.spelling == "uwire")) {
        initial = Logic4::z;
    }
    auto initial_value = PackedLogic4(static_cast<std::size_t>(width), initial);
    if (!declaration.type.packed_members.empty()
        || !declaration.type.enumeration_literals.empty()
        || declaration.type.domain
            == frontend::ValueDomain::Logic9
        || declaration.type.domain
            == frontend::ValueDomain::Integer) {
        initial_value = default_packed_value(
            declaration.type, static_cast<std::size_t>(width));
    }
    if (!declaration.is_port && declaration.default_value) {
        std::string initializer_error;
        const auto initialized = static_vhdl_value(
            *declaration.default_value,
            declaration.type,
            initializer_error);
        if (!initialized
            || initialized->width()
                != static_cast<std::size_t>(width)) {
            report(
                "FSIM-ELAB-VHINIT-001",
                "initial value for VHDL signal '" + declaration.name
                    + "' is not a statically foldable value compatible "
                      "with its subtype: "
                    + initializer_error,
                declaration.default_value->span);
        } else {
            initial_value = *initialized;
        }
    }
    runtime::simir::Signal signal {
        full_name,
        std::move(initial_value),
        ResolutionKind::none,
        value_kind(declaration.type.domain),
        std::nullopt,
        { StrengthRank::pull, StrengthRank::pull },
        std::nullopt,
        std::nullopt,
        declaration.type.systemverilog_scalar
    };
    signal.event_variable = declaration.type.spelling == "event";
    if (declaration.type.spelling == "tri0"
        || declaration.type.spelling == "tri1") {
        signal.implicit_driver = declaration.type.spelling == "tri0"
            ? Logic4::zero
            : Logic4::one;
    } else if (declaration.type.spelling == "supply0"
        || declaration.type.spelling == "supply1") {
        signal.implicit_driver = declaration.type.spelling == "supply0"
            ? Logic4::zero
            : Logic4::one;
        signal.implicit_drive_strength = {
            StrengthRank::supply, StrengthRank::supply
        };
    }
    if (declaration.type.spelling == "trireg") {
        const auto rank = [](const frontend::VerilogStrength strength) {
            using Frontend = frontend::VerilogStrength;
            switch (strength) {
            case Frontend::Small:
                return StrengthRank::small;
            case Frontend::Large:
                return StrengthRank::large;
            case Frontend::Medium:
                return StrengthRank::medium;
            default:
                return StrengthRank::medium;
            }
        };
        signal.charge_strength = declaration.charge_strength
            ? rank(declaration.charge_strength->rank)
            : StrengthRank::medium;
        if (declaration.charge_decay) {
            signal.charge_decay = declaration.charge_decay->magnitude;
        }
    }
    design_.signals_.push_back(std::move(signal));
    return id;
}
const Binding* HierarchyBuilder::binding_for(const std::string& path)
{
    const auto found = bindings_.find(path);
    if (found == bindings_.end()) {
        return nullptr;
    }
    used_bindings_.insert(path);
    return found->second;
}

std::vector<UnitResolutionCandidate>
HierarchyBuilder::resolution_candidates(
    const std::string_view library,
    const std::string_view name) const
{
    auto result = resolve_unit_candidates(parsed_, library, name);
    for (const auto& factory : systemc_candidates_) {
        if (factory.library == library && factory.name == name) {
            result.push_back({ nullptr,
                factory.target,
                "systemc:" + factory.library + "." + factory.name });
        }
    }
    std::stable_sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            return left.identity < right.identity;
        });
    return result;
}

std::vector<UnitResolutionCandidate>
HierarchyBuilder::resolution_candidates(
    const std::span<const std::string> libraries,
    const std::string_view name,
    std::vector<std::string>& unavailable_libraries) const
{
    std::vector<UnitResolutionCandidate> result;
    for (std::size_t index = 0; index < libraries.size(); ++index) {
        const auto& library = libraries[index];
        if (!has_logical_library(
                parsed_, systemc_candidates_,
                systemc_libraries_, library)) {
            if (index != 0) {
                unavailable_libraries.push_back(library);
            }
            continue;
        }
        auto candidates = resolution_candidates(library, name);
        result.insert(
            result.end(),
            std::make_move_iterator(candidates.begin()),
            std::make_move_iterator(candidates.end()));
    }
    std::stable_sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            return left.identity < right.identity;
        });
    return result;
}

std::optional<UnitResolutionCandidate>
HierarchyBuilder::inferred_target(
    const std::string_view library,
    const std::string_view name,
    const std::string& path,
    const frontend::SourceSpan source)
{
    const auto scope = effective_search_scope(
        library, search_libraries_);
    std::vector<std::string> unavailable_libraries;
    const auto candidates = resolution_candidates(
        scope, name, unavailable_libraries);
    const auto formatted_scope = [&] {
        std::string result;
        for (const auto& entry : scope) {
            if (!result.empty()) {
                result += ", ";
            }
            result += entry;
        }
        return result;
    }();
    if (!unavailable_libraries.empty()) {
        std::string unavailable;
        for (const auto& entry : unavailable_libraries) {
            if (!unavailable.empty()) {
                unavailable += ", ";
            }
            unavailable += entry;
        }
        report(
            "FSIM-ELAB-BIND-059",
            "instance '" + path
                + "' queried unavailable logical "
                  "library/libraries ["
                + unavailable
                + "] while resolving unit '" + std::string { name }
                + "' in search scope [" + formatted_scope + "]",
            source);
        return std::nullopt;
    }
    if (candidates.empty()) {
        report(
            "FSIM-ELAB-BIND-012",
            "instance '" + path + "' names unit '" + std::string { name }
                + "', which was not found across VHDL, Verilog, "
                  "SystemVerilog, or SystemC in search scope ["
                + formatted_scope + "]; candidates: <none>",
            source);
        return std::nullopt;
    }
    if (candidates.size() != 1) {
        report(
            "FSIM-ELAB-BIND-017",
            "instance '" + path + "' names ambiguous unit '"
                + std::string { name } + "' in search scope ["
                + formatted_scope + "]; candidates: "
                + format_resolution_candidates(candidates),
            source);
        return std::nullopt;
    }
    return candidates.front();
}

const DesignUnit* HierarchyBuilder::bound_target(
    const frontend::Instance& instance,
    const DesignUnit& parent,
    const std::string& path,
    const Binding* binding)
{
    if (binding == nullptr || !binding->target.has_value()) {
        if (instance.unit_name.find_first_of(".(")
            != std::string::npos) {
            const auto* target = choose_same_language_instance(
                parsed_, parent, instance.unit_name);
            if (target == nullptr) {
                report(
                    "FSIM-ELAB-BIND-012",
                    "instance '" + path + "' names explicit unit '"
                        + instance.unit_name
                        + "', which was not found",
                    instance.span);
            }
            return target;
        }
        const auto library = parent.library.empty() ? std::string { "work" }
                                                    : parent.library;
        const auto selected = inferred_target(
            library, instance.unit_name, path, instance.span);
        return selected ? selected->unit : nullptr;
    }
    const auto target = parse_target(*binding->target);
    if (!target) {
        report(
            "FSIM-ELAB-BIND-013",
            "malformed binding target '" + *binding->target + "'",
            instance.span);
        return nullptr;
    }
    if (target->language == "systemc") {
        report(
            "FSIM-ELAB-BIND-014",
            "SystemC factory hierarchy is not executable in this slice",
            instance.span);
        return nullptr;
    }
    if (target->language == "vhdl" && !target->architecture) {
        report(
            "FSIM-ELAB-BIND-016",
            "an explicit VHDL binding target must name an architecture, "
            "for example vhdl:work.entity(rtl)",
            instance.span);
        return nullptr;
    }
    const auto* selected = choose_bound_unit(parsed_, *target);
    if (selected == nullptr) {
        report(
            "FSIM-ELAB-BIND-015",
            "binding target '" + *binding->target + "' was not found",
            instance.span);
    }
    return selected;
}

} // namespace fsim::elaboration

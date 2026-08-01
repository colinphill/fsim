// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

    void HierarchyBuilder::resolve_named_types(
        DesignUnit& unit,
        const NamedTypeEnvironment& imported_types,
        const bool vhdl,
        const bool resolve_ports) {
        std::unordered_map<std::string, std::size_t> local_types;
        const auto owner =
            (unit.library.empty() ? std::string{"work"} : unit.library)
            + "." + unit.name;
        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            auto& alias = unit.type_aliases[index];
            if (!vhdl
                && alias.type.nominal_type.empty()
                && (alias.type.packed_aggregate
                        != frontend::PackedAggregateKind::None
                    || !alias.enum_literals.empty())) {
                alias.type.nominal_type =
                    "sv:" + owner + "." + alias.name;
            }
            local_types.emplace(
                alias.name, index);
        }
        std::vector<unsigned char> states(
            unit.type_aliases.size(), 0);
        std::unordered_map<std::string, frontend::Type*>
            generated_types;
        std::unordered_set<std::string> active_interface_type_formals;
        std::unordered_set<std::string> active_interface_package_formals;
        std::function<bool(frontend::Type&)> resolve_type;
        std::function<bool(std::size_t)> resolve_alias;
        const auto simple_type_name =
            [](const std::string_view spelling) {
                const auto separator =
                    spelling.find_last_of('.');
                return std::string{
                    spelling.substr(
                        separator == std::string_view::npos
                            ? 0
                            : separator + 1)};
            };
        const auto is_packed_array_type =
            [&](const frontend::Type& type) {
                const auto name =
                    simple_type_name(type.spelling);
                return type.vhdl_array.has_value()
                    || name == "bit_vector"
                    || name == "std_logic_vector"
                    || name == "std_ulogic_vector"
                    || name == "signed"
                    || name == "unsigned";
            };
        const auto constraint_span =
            [](const frontend::Type& type) {
                if (type.discrete_range_expression) {
                    return type.discrete_range_expression->span;
                }
                if (type.enumeration_range_expression) {
                    return type.enumeration_range_expression->span;
                }
                if (type.integer_range_expression) {
                    return type.integer_range_expression->span;
                }
                if (type.packed_range_expression) {
                    return type.packed_range_expression->span;
                }
                return type.named_type_span;
            };
        const auto validate_direct_constraints =
            [&](const frontend::Type& type) {
                bool valid = true;
                if (type.discrete_range_expression) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-001",
                        "a VHDL discrete range constraint requires a "
                        "named integer-family or enumeration base subtype",
                        constraint_span(type));
                    valid = false;
                }
                if (type.integer_range_expression
                    && type.domain
                        != frontend::ValueDomain::Integer) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-001",
                        "a VHDL range constraint requires an "
                        "integer-family base subtype",
                        constraint_span(type));
                    valid = false;
                }
                if (type.packed_range_expression
                    && (!is_packed_array_type(type)
                        || !type.packed_members.empty())) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-003",
                        "a VHDL packed index constraint requires an "
                        "unconstrained one-dimensional packed-array base",
                        constraint_span(type));
                    valid = false;
                }
                return valid;
            };
        const auto apply_derived_constraints =
            [&](frontend::Type base,
                const frontend::Type& derived)
                -> std::optional<frontend::Type> {
                if (!derived.vhdl_type_declaration.empty()) {
                    base.vhdl_type_declaration =
                        derived.vhdl_type_declaration;
                }
                if (!derived.vhdl_resolution_function.empty()) {
                    base.vhdl_resolution_function =
                        derived.vhdl_resolution_function;
                }
                const bool has_integer_constraint =
                    derived.integer_range_expression.has_value();
                const bool has_discrete_constraint =
                    derived.discrete_range_expression.has_value();
                const bool has_packed_constraint =
                    derived.packed_range_expression.has_value();
                if (has_integer_constraint) {
                    if (base.domain
                        != frontend::ValueDomain::Integer) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-001",
                            "a derived VHDL range constraint requires an "
                            "integer-family base subtype",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.integer_base_range =
                        base.integer_range;
                    base.integer_base_range_expression =
                        base.integer_range_expression;
                    base.integer_range =
                        derived.integer_range;
                    base.integer_range_expression =
                        derived.integer_range_expression;
                }
                if (has_discrete_constraint) {
                    const auto& range =
                        *derived.discrete_range_expression;
                    if (base.domain
                        == frontend::ValueDomain::Integer) {
                        base.integer_base_range =
                            base.integer_range;
                        base.integer_base_range_expression =
                            base.integer_range_expression;
                        base.integer_range.reset();
                        base.integer_range_expression =
                            frontend::IntegerRangeExpression{
                                range.left,
                                range.right,
                                range.span,
                                range.descending};
                    } else if (
                        !base.enumeration_literals.empty()) {
                        base.enumeration_base_range =
                            base.enumeration_range;
                        base.enumeration_base_range_expression =
                            base.enumeration_range_expression;
                        base.enumeration_range.reset();
                        base.enumeration_range_expression =
                            range;
                    } else {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-001",
                            "a derived VHDL discrete range constraint "
                            "requires an integer-family or enumeration "
                            "base subtype",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.discrete_range_expression.reset();
                }
                if (has_packed_constraint) {
                    if (!is_packed_array_type(base)
                        || !base.packed_members.empty()) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-003",
                            "a derived VHDL packed index constraint "
                            "requires an unconstrained one-dimensional "
                            "packed-array base",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    if (base.packed_range
                        || base.packed_range_expression) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-004",
                            "a constrained VHDL packed-array subtype cannot "
                            "be constrained again",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.packed_range =
                        derived.packed_range;
                    base.packed_range_expression =
                        derived.packed_range_expression;
                    if (base.vhdl_array) {
                        base.vhdl_array->unconstrained = false;
                    }
                }
                return base;
            };
        resolve_alias = [&](const std::size_t index) {
            if (states[index] == 2) {
                return true;
            }
            if (states[index] == 3) {
                return false;
            }
            if (states[index] == 1) {
                report(
                    vhdl
                        ? "FSIM-ELAB-VHTYPE-002"
                        : "FSIM-ELAB-SVTYPE-003",
                    std::string{
                        vhdl
                            ? "cyclic VHDL type declaration involving '"
                            : "cyclic SystemVerilog typedef involving '"}
                        + unit.type_aliases[index].name + "'",
                    unit.type_aliases[index].span);
                return false;
            }
            states[index] = 1;
            const bool resolved =
                resolve_type(unit.type_aliases[index].type);
            states[index] = resolved ? 2 : 3;
            return resolved;
        };
        resolve_type = [&](frontend::Type& type) {
            for (auto& member : type.packed_members) {
                if (member.nested_types.empty()) {
                    continue;
                }
                auto& nested = member.nested_types.front();
                if (!resolve_type(nested)) {
                    return false;
                }
                member.domain = nested.domain;
                member.spelling = nested.spelling;
                member.packed_range = nested.packed_range;
                member.packed_range_expression =
                    nested.packed_range_expression;
                member.is_signed = nested.is_signed;
            }
            if (type.systemverilog_container
                && type.systemverilog_container
                       ->associative_index_type
                && !resolve_type(
                    *type.systemverilog_container
                         ->associative_index_type)) {
                return false;
            }
            if (type.vhdl_array
                && !type.vhdl_array->element_named_type.empty()) {
                frontend::Type element;
                element.spelling =
                    type.vhdl_array->element_spelling;
                element.named_type =
                    type.vhdl_array->element_named_type;
                element.named_type_span =
                    type.vhdl_array->element_span;
                if (!resolve_type(element)) {
                    return false;
                }
                const auto width = element.width();
                if (!width || *width != 1
                    || element.vhdl_array
                    || !element.packed_members.empty()
                    || !element.enumeration_literals.empty()
                    || element.domain
                        == frontend::ValueDomain::Integer
                    || element.domain
                        == frontend::ValueDomain::Unknown) {
                    report(
                        "FSIM-ELAB-VHARRAY-001",
                        "VHDL array type '" + type.spelling
                            + "' requires a resolved scalar bit, Boolean, "
                              "std_logic, or std_ulogic element subtype",
                        type.vhdl_array->element_span);
                    return false;
                }
                type.vhdl_array->element_domain =
                    element.domain;
                type.vhdl_array->element_spelling =
                    element.spelling;
                type.vhdl_array->element_named_type.clear();
                type.domain = element.domain;
                // Resolve packed array elements independently.
                if (!element.vhdl_resolution_function.empty()) {
                    type.vhdl_resolution_function = element.vhdl_resolution_function;
                }
            }
            if (type.named_type.empty()) {
                return !vhdl
                    || validate_direct_constraints(type);
            }
            const auto name = type.named_type;
            const auto use_span = type.named_type_span;
            const auto derived = type;
            std::optional<frontend::Type> base;
            if (name.find("::") == std::string::npos) {
                if (const auto generated = generated_types.find(name);
                    generated != generated_types.end()) {
                    base = *generated->second;
                }
                if (const auto local = local_types.find(name);
                    !base && local != local_types.end()) {
                    if (!resolve_alias(local->second)) {
                        return false;
                    }
                    base =
                        unit.type_aliases[local->second].type;
                }
            }
            if (!base) {
                const auto imported =
                    imported_types.find(name);
                if (imported == imported_types.end()) {
                    const auto separator = name.find('.');
                    const auto deferred_component_type =
                        vhdl
                        && (active_interface_type_formals.contains(name)
                            || (separator != std::string::npos
                                && active_interface_package_formals.contains(
                                    name.substr(0, separator))));
                    if (deferred_component_type) {
                        return true;
                    }
                    const auto deferred_package_type =
                        vhdl
                        && separator != std::string::npos
                        && std::ranges::any_of(
                            unit.package_instances,
                            [&](const auto& instance) {
                                return instance.name
                                    == name.substr(0, separator);
                            });
                    if (deferred_package_type) {
                        return true;
                    }
                    report(
                        vhdl
                            ? "FSIM-ELAB-VHTYPE-001"
                            : "FSIM-ELAB-SVTYPE-001",
                        std::string{
                            vhdl
                                ? "VHDL type '"
                                : "SystemVerilog type alias '"}
                            + name + "' is not visible in this unit",
                        use_span);
                    return false;
                }
                if (imported->second.interface_formal) {
                    return true;
                }
                base = imported->second.type;
            }
            if (!vhdl) {
                const auto container =
                    type.systemverilog_container;
                type = std::move(*base);
                if (container) {
                    type.systemverilog_container = container;
                }
                return true;
            }
            auto constrained =
                apply_derived_constraints(
                    std::move(*base), derived);
            if (!constrained) {
                return false;
            }
            type = std::move(*constrained);
            return true;
        };
        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            (void)resolve_alias(index);
        }
        const auto resolve_declaration =
            [&](auto& declaration) {
                (void)resolve_type(declaration.type);
            };
        const auto resolve_component =
            [&](frontend::VhdlComponentDeclaration& component) {
                active_interface_type_formals.clear();
                active_interface_package_formals.clear();
                for (auto& generic : component.generics) {
                    switch (generic.kind) {
                    case frontend::ParameterKind::Type:
                        active_interface_type_formals.insert(
                            generic.name);
                        break;
                    case frontend::ParameterKind::Function:
                        if (generic.function_profile) {
                            (void)resolve_type(
                                generic.function_profile->return_type);
                            for (auto& argument :
                                 generic.function_profile->arguments) {
                                (void)resolve_type(argument.type);
                            }
                        }
                        break;
                    case frontend::ParameterKind::Procedure:
                        if (generic.procedure_profile) {
                            for (auto& argument :
                                 generic.procedure_profile->arguments) {
                                (void)resolve_type(argument.type);
                            }
                        }
                        break;
                    case frontend::ParameterKind::Package:
                        if (generic.package_profile) {
                            for (auto& actual :
                                 generic.package_profile->generic_map) {
                                if (actual.type_value) {
                                    (void)resolve_type(*actual.type_value);
                                }
                            }
                        }
                        active_interface_package_formals.insert(
                            generic.name);
                        break;
                    case frontend::ParameterKind::Value:
                        (void)resolve_type(generic.type);
                        break;
                    }
                }
                for (auto& port : component.ports) {
                    (void)resolve_type(port.type);
                }
                active_interface_type_formals.clear();
                active_interface_package_formals.clear();
            };
        std::function<void(std::vector<Statement>&)>
            resolve_statements;
        resolve_statements =
            [&](std::vector<Statement>& statements) {
                for (auto& statement : statements) {
                    for (auto& declaration :
                         statement.declarations) {
                        resolve_declaration(declaration);
                    }
                    resolve_statements(statement.statements);
                    resolve_statements(
                        statement.else_statements);
                    for (auto& alternative :
                         statement.case_alternatives) {
                        resolve_statements(
                            alternative.statements);
                    }
                }
            };
        std::function<void(frontend::GenerateBody&)>
            resolve_generate_body;
        std::function<void(
            std::vector<frontend::GenerateRegion>&)>
            resolve_generate_regions;
        resolve_generate_body =
            [&](frontend::GenerateBody& body) {
                struct PriorGeneratedType {
                    std::string name;
                    frontend::Type* type{};
                };
                std::vector<PriorGeneratedType> prior_types;
                for (auto& alias : body.type_aliases) {
                    (void)resolve_type(alias.type);
                    const auto prior = generated_types.find(alias.name);
                    prior_types.push_back({
                        alias.name,
                        prior == generated_types.end()
                            ? nullptr
                            : prior->second});
                    generated_types[alias.name] = &alias.type;
                }
                for (auto& constant : body.constants) {
                    resolve_declaration(constant);
                }
                for (auto& signal : body.signals) {
                    resolve_declaration(signal);
                }
                for (auto& alias : body.signal_aliases) {
                    (void)resolve_type(alias.type);
                }
                for (auto& component :
                     body.vhdl_component_declarations) {
                    resolve_component(component);
                }
                for (auto& process : body.processes) {
                    for (auto& variable : process.variables) {
                        resolve_declaration(variable);
                    }
                    resolve_statements(process.statements);
                }
                for (auto& function : body.functions) {
                    (void)resolve_type(function.return_type);
                    for (auto& argument : function.arguments) {
                        (void)resolve_type(argument.type);
                    }
                    for (auto& variable : function.variables) {
                        resolve_declaration(variable);
                    }
                    resolve_statements(function.statements);
                }
                for (auto& task : body.tasks) {
                    for (auto& argument : task.arguments) {
                        (void)resolve_type(argument.type);
                    }
                    for (auto& variable : task.variables) {
                        resolve_declaration(variable);
                    }
                    resolve_statements(task.statements);
                }
                resolve_generate_regions(
                    body.generate_regions);
                for (auto prior = prior_types.rbegin();
                     prior != prior_types.rend(); ++prior) {
                    if (prior->type == nullptr) {
                        generated_types.erase(prior->name);
                    } else {
                        generated_types[prior->name] = prior->type;
                    }
                }
            };
        resolve_generate_regions =
            [&](std::vector<frontend::GenerateRegion>&
                    regions) {
                for (auto& region : regions) {
                    for (auto& generic : region.block_generics) {
                        resolve_declaration(generic);
                    }
                    for (auto& port : region.block_ports) {
                        resolve_declaration(port);
                    }
                    resolve_generate_body(region.then_body);
                    resolve_generate_body(region.else_body);
                    for (auto& alternative :
                         region.alternatives) {
                        resolve_generate_body(
                            alternative.body);
                    }
                }
            };
        for (auto& parameter : unit.parameters) {
            if (parameter.kind
                == frontend::ParameterKind::Type) {
                if (parameter.default_type) {
                    (void)resolve_type(*parameter.default_type);
                }
            } else if (
                parameter.kind
                    == frontend::ParameterKind::Function) {
                if (parameter.function_profile) {
                    (void)resolve_type(
                        parameter.function_profile->return_type);
                    for (auto& argument :
                         parameter.function_profile->arguments) {
                        (void)resolve_type(argument.type);
                    }
                }
            } else if (
                parameter.kind
                    == frontend::ParameterKind::Procedure) {
                if (parameter.procedure_profile) {
                    for (auto& argument :
                         parameter.procedure_profile->arguments) {
                        (void)resolve_type(argument.type);
                    }
                }
            } else {
                resolve_declaration(parameter);
            }
        }
        if (resolve_ports) {
            for (auto& port : unit.ports) {
                resolve_declaration(port);
            }
        }
        for (auto& signal : unit.signals) {
            resolve_declaration(signal);
        }
        for (auto& variable : unit.variables) {
            resolve_declaration(variable);
        }
        for (auto& component :
             unit.vhdl_component_declarations) {
            resolve_component(component);
        }
        for (auto& function : unit.functions) {
            (void)resolve_type(function.return_type);
            for (auto& argument : function.arguments) {
                (void)resolve_type(argument.type);
            }
            for (auto& variable : function.variables) {
                resolve_declaration(variable);
            }
            resolve_statements(function.statements);
        }
        for (auto& task : unit.tasks) {
            for (auto& argument : task.arguments) {
                (void)resolve_type(argument.type);
            }
            for (auto& variable : task.variables) {
                resolve_declaration(variable);
            }
            resolve_statements(task.statements);
        }
        for (auto& procedure : unit.procedures) {
            for (auto& argument : procedure.arguments) {
                (void)resolve_type(argument.type);
            }
            for (auto& variable : procedure.variables) {
                resolve_declaration(variable);
            }
            resolve_statements(procedure.statements);
        }
        for (auto& process : unit.processes) {
            for (auto& variable : process.variables) {
                resolve_declaration(variable);
            }
            resolve_statements(process.statements);
        }
        resolve_generate_regions(unit.generate_regions);
    }
    DesignUnit HierarchyBuilder::effective_unit(
        const DesignUnit& selected,
        const DesignUnit* entity_override) {
        auto result = selected;
        if (selected.kind
            == frontend::UnitKind::VerilogModule) {
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
                    NamedTypeBinding{
                        {},
                        (result.library.empty()
                             ? std::string{"work"}
                             : result.library)
                            + "." + result.name,
                        true});
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
                    frontend::TypeAliasDeclaration{
                        name,
                        binding.type,
                        result.span,
                        {},
                        frontend::TypeDeclarationKind::
                            SystemVerilogTypedef});
            }
            return result;
        }
        if (selected.kind
            != frontend::UnitKind::VhdlArchitecture) {
            return result;
        }
        const auto* entity =
            entity_override != nullptr
                ? entity_override
                : find_vhdl_entity(parsed_, selected);
        if (entity == nullptr) {
            return result;
        }
        result.parameters = entity->parameters;
        result.ports = entity->ports;
        std::vector<frontend::VhdlComponentDeclaration>
            entity_components =
                entity->vhdl_component_declarations;
        for (auto& component : entity_components) {
            component.region =
                frontend::VhdlComponentDeclarationRegion::Entity;
            component.owner_library =
                entity->library.empty()
                    ? std::string{"work"}
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
        std::vector<frontend::VhdlContextItem> context =
            entity->vhdl_context;
        context.insert(
            context.end(),
            selected.vhdl_context.begin(),
            selected.vhdl_context.end());
        std::vector<frontend::VhdlContextItem> expanded_context;
        std::vector<const DesignUnit*> context_stack;
        const auto unit_library =
            result.library.empty()
                ? std::string{"work"}
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
                NamedTypeBinding{
                    alias.type,
                    (entity->library.empty()
                         ? std::string{"work"}
                         : entity->library)
                        + "." + entity->name});
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
                NamedTypeBinding{
                    {},
                    (entity->library.empty()
                         ? std::string{"work"}
                         : entity->library)
                        + "." + entity->name,
                    true});
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
                    resolved->function_profile =
                        generic.function_profile;
                } else if (
                    generic.kind
                        == frontend::ParameterKind::Procedure) {
                    resolved->procedure_profile =
                        generic.procedure_profile;
                } else if (
                    generic.kind
                        == frontend::ParameterKind::Package) {
                    resolved->package_profile =
                        generic.package_profile;
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
                NamedTypeBinding{
                    alias.type,
                    (entity->library.empty()
                         ? std::string{"work"}
                         : entity->library)
                        + "." + entity->name});
        }
        std::erase_if(
            result.vhdl_component_declarations,
            [](const auto& component) {
              return component.region
                  == frontend::VhdlComponentDeclarationRegion::
                      Entity;
            });
        auto resolved_entity_components =
            std::move(
                effective_entity_declarations
                    .vhdl_component_declarations);
        for (auto& component : resolved_entity_components) {
            component.region =
                frontend::VhdlComponentDeclarationRegion::Entity;
            component.owner_library =
                entity->library.empty()
                    ? std::string{"work"}
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
                frontend::TypeAliasDeclaration{
                    name,
                    binding.type,
                    result.span,
                    {},
                    frontend::TypeDeclarationKind::Alias});
        }
        for (auto& component :
             result.vhdl_component_declarations) {
            if (component.owner_library.empty()) {
                component.owner_library =
                    result.library.empty()
                        ? std::string{"work"}
                        : result.library;
            }
            if (component.owner_name.empty()) {
                component.owner_name =
                    component.region
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
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>& parent_functions,
        const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
        const PackageEnvironment& parent_packages,
        const frontend::Language association_language) {
        auto normalized_overrides = overrides;
        PackageEnvironment interface_packages;
        std::vector<std::pair<std::string, std::string>>
            package_identities;
        std::optional<DesignUnit> selected_override;
        std::optional<DesignUnit> entity_override;
        if (selected.language
            == frontend::Language::Vhdl2008) {
            if (selected.kind
                == frontend::UnitKind::VhdlArchitecture) {
                if (const auto* entity =
                        find_vhdl_entity(parsed_, selected);
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
        auto type_specialized =
            selected.language
                    == frontend::Language::SystemVerilog2017
                ? specialize_systemverilog_type_parameters(
                      effective,
                      normalized_overrides,
                      parent_environment,
                      parent_types,
                      association_language,
                      diagnostics_)
                : specialize_vhdl_interface_types(
                      effective,
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
                {},
                selected.language
                    == frontend::Language::Vhdl2008);
        }
        auto specialized = specialize_unit(
            type_specialized.unit,
            type_specialized.value_overrides,
            parent_environment,
            association_language,
            diagnostics_);
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
                const auto resolved =
                    alias
                        == specialized.unit.type_aliases.end()
                    ? std::optional<std::string>{}
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
            const auto value_identities =
                std::move(specialized.identity_values);
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
        instantiate_vhdl_local_packages(
            specialized, interface_packages);
        instantiate_vhdl_generic_subprograms(
            specialized);
        return specialized;
    }

    std::optional<SignalId> HierarchyBuilder::add_owned_signal(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        SignalMap& local) {
        if (const auto existing = local.find(declaration.name);
            existing != local.end()) {
            return existing->second;
        }
        if (declaration.type.domain == frontend::ValueDomain::Unknown) {
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
                    + "' requires a concrete non-null index constraint",
                declaration.span);
            return std::nullopt;
        }
        const auto separator =
            declaration.type.spelling.find_last_of('.');
        const auto simple_type_name =
            declaration.type.spelling.substr(
                separator == std::string::npos
                    ? 0
                    : separator + 1);
        if (!declaration.type.packed_range
            && !declaration.type.packed_range_expression
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
        if (width == 0 || width > std::numeric_limits<std::size_t>::max()) {
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
        const auto full_name =
            std::string(path) + "." + declaration.name;
        local.emplace(declaration.name, id);
        local.emplace(full_name, id);
        design_.signal_by_name_.emplace(full_name, id);
        if (path == design_.top_) {
            design_.signal_by_name_.emplace(declaration.name, id);
        }
        design_.signal_info_.push_back({
            id,
            full_name,
            static_cast<std::size_t>(width),
            declaration.type.spelling,
            declaration.type.domain,
            declaration.type.is_signed,
            declaration.type.packed_range,
            declaration.type.vhdl_array,
            declaration.type.packed_members,
            declaration.type.integer_range,
            declaration.type.nominal_type,
            declaration.type.enumeration_literals,
            declaration.type.enumeration_range,
            declaration.is_port,
            declaration.direction,
            declaration.span});
        if (!declaration.type.vhdl_resolution_function.empty()) {
            resolver_by_signal_.insert_or_assign(
                id, declaration.type.vhdl_resolution_function);
        }
        auto initial = Logic4::x;
        if (declaration.type.spelling == "event") {
            initial = Logic4::zero;
        } else if (declaration.type.spelling == "tri0") {
            initial = Logic4::zero;
        } else if (declaration.type.spelling == "tri1") {
            initial = Logic4::one;
        } else if (is_two_state_domain(declaration.type.domain)) {
            initial = Logic4::zero;
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
        auto initial_value =
            PackedLogic4(static_cast<std::size_t>(width), initial);
        if (!declaration.type.packed_members.empty()
            || !declaration.type.enumeration_literals.empty()
            || declaration.type.domain
                == frontend::ValueDomain::Logic9
            || declaration.type.domain
                == frontend::ValueDomain::Integer) {
            initial_value = default_packed_value(
                declaration.type, static_cast<std::size_t>(width));
        }
        design_.signals_.push_back(
            {
                full_name,
                std::move(initial_value),
                ResolutionKind::none,
                value_kind(declaration.type.domain)});
        return id;
    }
    const Binding* HierarchyBuilder::binding_for(const std::string& path) {
        const auto found = bindings_.find(path);
        if (found == bindings_.end()) {
            return nullptr;
        }
        used_bindings_.insert(path);
        return found->second;
    }
    const DesignUnit* HierarchyBuilder::bound_target(
        const frontend::Instance& instance,
        const DesignUnit& parent,
        const std::string& path,
        const Binding* binding) {
        if (binding == nullptr) {
            const auto* target = choose_same_language_instance(
                parsed_, parent, instance.unit_name);
            if (target == nullptr) {
                report(
                    "FSIM-ELAB-BIND-012",
                    "instance '" + path + "' names unit '"
                        + instance.unit_name
                        + "', which was not found in the same language; "
                          "an explicit cross-language binding is required",
                    instance.span);
            }
            return target;
        }
        const auto target = parse_target(binding->target);
        if (!target) {
            report(
                "FSIM-ELAB-BIND-013",
                "malformed binding target '" + binding->target + "'",
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
                "binding target '" + binding->target + "' was not found",
                instance.span);
        }
        return selected;
    }

    HierarchyBuilder::PortAliases HierarchyBuilder::connect_ports(
        const frontend::Instance& instance,
        const std::vector<frontend::SignalDeclaration>& ports,
        const std::string& path,
        const SignalMap& parent_signals,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>&
            parent_read_only_strings,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>&
            parent_read_only_containers,
        const Binding* binding,
        const bool cross_language,
        const bool require_input_connections,
        DesignUnit* dependency_owner) {
        PortAliases result;
        auto& aliases = result.signals;
        auto& string_aliases = result.strings;
        auto& container_aliases = result.containers;
        std::vector<bool> connected(ports.size());
        std::size_t positional = 0;
        for (const auto& connection : instance.connections) {
            std::size_t port_index = ports.size();
            if (connection.port) {
                const auto found = std::find_if(
                    ports.begin(), ports.end(),
                    [&](const frontend::SignalDeclaration& port) {
                        return port.name == *connection.port;
                    });
                if (found != ports.end()) {
                    port_index = static_cast<std::size_t>(
                        std::distance(ports.begin(), found));
                }
            } else {
                while (positional < ports.size() && connected[positional]) {
                    ++positional;
                }
                port_index = positional++;
            }
            if (port_index >= ports.size()) {
                report(
                    "FSIM-ELAB-BIND-025",
                    connection.port
                        ? "unknown port '" + *connection.port
                            + "' on instance '" + path + "'"
                        : "too many positional connections on instance '"
                            + path + "'",
                    connection.span);
                continue;
            }
            if (connected[port_index]) {
                report(
                    "FSIM-ELAB-BIND-026",
                    "port '" + ports[port_index].name
                        + "' is connected more than once on instance '"
                        + path + "'",
                    connection.span);
                continue;
            }
            connected[port_index] = true;
            const auto& port = ports[port_index];
            if (!port.interface_type.empty()
                || port.type.spelling == "interface") {
                std::string actual_name;
                if (connection.value.kind
                    == frontend::ExpressionKind::Identifier) {
                  actual_name = connection.value.text;
                } else if (
                    connection.value.kind
                        == frontend::ExpressionKind::Index
                    && connection.value.operands.size() == 2
                    && connection.value.operands[0].kind
                        == frontend::ExpressionKind::Identifier
                    && connection.value.operands[1].kind
                        == frontend::ExpressionKind::IntegerLiteral) {
                  actual_name = connection.value.operands[0].text
                      + "[" + connection.value.operands[1].text + "]";
                }
                if (actual_name.empty()) {
                  report(
                        "FSIM-ELAB-SVIFACE-001",
                        "interface port '" + path + "." + port.name
                            + "' requires a whole interface-instance actual",
                        connection.value.span);
                    continue;
                }
                const auto separator = path.rfind('.');
                const auto parent_path = separator == std::string::npos
                    ? std::string{}
                    : path.substr(0, separator);
                auto actual_path = parent_path.empty()
                    ? actual_name
                    : parent_path + "." + actual_name;
                auto actual_interface =
                    systemverilog_interface_instances_.find(actual_path);
                auto lexical_path = parent_path;
                while (actual_interface
                           == systemverilog_interface_instances_.end()
                       && lexical_path.find('.') != std::string::npos) {
                  lexical_path.resize(lexical_path.rfind('.'));
                  actual_path = lexical_path + "." + actual_name;
                  actual_interface =
                      systemverilog_interface_instances_.find(actual_path);
                }
                if (actual_interface
                    == systemverilog_interface_instances_.end()) {
                    report(
                        "FSIM-ELAB-SVIFACE-002",
                        "interface actual '" + actual_path
                            + "' must name an earlier interface instance",
                        connection.value.span);
                    continue;
                }
                const auto interface_unit = actual_interface->second;
                if (dependency_owner != nullptr) {
                  const auto source = std::string{
                      frontend::physical_source(interface_unit.span)};
                  if (!source.empty()
                      && std::ranges::find(
                          dependency_owner->source_dependencies,
                          source)
                          == dependency_owner->source_dependencies.end()) {
                    dependency_owner->source_dependencies.push_back(source);
                  }
                }
                if (!port.interface_type.empty()
                    && port.interface_type != interface_unit.name) {
                    report(
                        "FSIM-ELAB-SVIFACE-003",
                        "interface port '" + path + "." + port.name
                            + "' requires type '" + port.interface_type
                            + "' but actual '" + actual_path + "' has type '"
                            + interface_unit.name + "'",
                        connection.span);
                    continue;
                }
                systemverilog_interface_instances_.insert_or_assign(
                    path + "." + port.name, interface_unit);
                systemverilog_interface_port_paths_.insert(
                    path + "." + port.name);
                const bool forwarded_interface_port =
                    systemverilog_interface_port_paths_.contains(
                        actual_path);
                const frontend::SystemVerilogModport* modport = nullptr;
                if (!port.modport.empty()) {
                    const auto found = std::ranges::find_if(
                        interface_unit.systemverilog_modports,
                        [&](const frontend::SystemVerilogModport& candidate) {
                          return candidate.name == port.modport;
                        });
                    if (found
                        == interface_unit.systemverilog_modports.end()) {
                      report(
                          "FSIM-ELAB-SVIFACE-004",
                          "interface type '" + interface_unit.name
                              + "' has no modport '" + port.modport + "'",
                          port.span);
                      continue;
                    }
                    modport = &*found;
                }
                const auto connect_member =
                    [&](const std::string& member,
                        const frontend::PortDirection direction,
                        const frontend::SourceSpan& member_span) {
                      const auto signal_name = actual_path + "." + member;
                      const auto signal =
                          design_.signal_by_name_.find(signal_name);
                      if (signal == design_.signal_by_name_.end()) {
                        report(
                            "FSIM-ELAB-SVIFACE-005",
                            "interface member signal '" + signal_name
                                + "' was not elaborated",
                            member_span);
                        return;
                      }
                      const auto local_name = port.name + "." + member;
                      const auto qualified_name = path + "." + local_name;
                      aliases.emplace(local_name, signal->second);
                      aliases.emplace(qualified_name, signal->second);
                      const auto member_path =
                          actual_path + "." + member;
                      const bool inherited_read_only =
                          systemverilog_read_only_interface_member_paths_
                              .contains(member_path);
                      if (direction
                              == frontend::PortDirection::Input
                          || inherited_read_only) {
                        result.read_only_signals.insert(signal->second);
                        systemverilog_read_only_interface_member_paths_
                            .insert(qualified_name);
                      }
                      design_.signal_by_name_.emplace(
                          qualified_name, signal->second);
                      if (!forwarded_interface_port
                          && (direction
                                  == frontend::PortDirection::Output
                          || direction == frontend::PortDirection::Inout
                          || direction == frontend::PortDirection::Ref
                          || direction
                              == frontend::PortDirection::Buffer)) {
                        note_boundary_driver(
                            signal->second,
                            binding,
                            qualified_name,
                            connection.span,
                            false);
                      }
                    };
                const auto connect_callable =
                    [&](const std::string& member,
                        const bool function,
                        const bool imported,
                        const frontend::SourceSpan& member_span) {
                      if (dependency_owner == nullptr) {
                        report(
                            "FSIM-ELAB-SVIFACE-007",
                            "interface callable '" + member
                                + "' has no same-language module owner",
                            member_span);
                        return;
                      }
                      if (!imported) {
                        const bool supplied = function
                            ? std::ranges::any_of(
                                  dependency_owner->functions,
                                  [&](const auto& candidate) {
                                    return candidate.name == member;
                                  })
                            : std::ranges::any_of(
                                  dependency_owner->tasks,
                                  [&](const auto& candidate) {
                                    return candidate.name == member;
                                  });
                        if (!supplied) {
                          report(
                              "FSIM-ELAB-SVIFACE-009",
                              "modport export '" + member
                                  + "' has no matching module callable",
                              member_span);
                        }
                        return;
                      }
                      const auto qualified = port.name + "." + member;
                      if (function) {
                        const auto found = std::ranges::find_if(
                            interface_unit.functions,
                            [&](const auto& candidate) {
                              return candidate.name == member;
                            });
                        if (found == interface_unit.functions.end()) {
                          report(
                              "FSIM-ELAB-SVIFACE-007",
                              "interface function '" + member
                                  + "' was not retained",
                              member_span);
                          return;
                        }
                        if (std::ranges::any_of(
                                dependency_owner->functions,
                                [&](const auto& candidate) {
                                  return candidate.name == qualified;
                                })) {
                          report(
                              "FSIM-ELAB-SVIFACE-008",
                              "interface callable '" + qualified
                                  + "' is visible more than once",
                              member_span);
                          return;
                        }
                        auto callable = *found;
                        qualify_interface_callable(
                            callable, port.name, interface_unit);
                        dependency_owner->functions.push_back(
                            std::move(callable));
                        return;
                      }
                      const auto found = std::ranges::find_if(
                          interface_unit.tasks,
                          [&](const auto& candidate) {
                            return candidate.name == member;
                          });
                      if (found == interface_unit.tasks.end()) {
                        report(
                            "FSIM-ELAB-SVIFACE-007",
                            "interface task '" + member
                                + "' was not retained",
                            member_span);
                        return;
                      }
                      if (std::ranges::any_of(
                              dependency_owner->tasks,
                              [&](const auto& candidate) {
                                return candidate.name == qualified;
                              })) {
                        report(
                            "FSIM-ELAB-SVIFACE-008",
                            "interface callable '" + qualified
                                + "' is visible more than once",
                            member_span);
                        return;
                      }
                      auto callable = *found;
                      qualify_interface_callable(
                          callable, port.name, interface_unit);
                      dependency_owner->tasks.push_back(
                          std::move(callable));
                    };
                if (modport != nullptr) {
                  for (const auto& member : modport->members) {
                    using Kind =
                        frontend::SystemVerilogModportMemberKind;
                    if (member.kind == Kind::Signal) {
                      connect_member(
                          member.name, member.direction, member.span);
                    } else {
                      connect_callable(
                          member.name,
                          member.kind == Kind::FunctionImport
                              || member.kind == Kind::FunctionExport,
                          member.kind == Kind::FunctionImport
                              || member.kind == Kind::TaskImport,
                          member.span);
                    }
                  }
                } else {
                  for (const auto& member : interface_unit.signals) {
                    connect_member(
                        member.name,
                        frontend::PortDirection::Unknown,
                        member.span);
                  }
                  for (const auto& function : interface_unit.functions) {
                    connect_callable(
                        function.name, true, true, function.span);
                  }
                  for (const auto& task : interface_unit.tasks) {
                    connect_callable(
                        task.name, false, true, task.span);
                  }
                }
                continue;
            }
            if (connection.kind
                    == frontend::PortActualKind::Open) {
                if (port.direction
                    == frontend::PortDirection::Input) {
                    if (!cross_language
                        && dependency_owner != nullptr
                        && port.default_value) {
                        auto default_connection = connection;
                        default_connection.kind =
                            frontend::PortActualKind::Default;
                        default_connection.value = *port.default_value;
                        (void)connect_vhdl_expression_port(
                            port,
                            default_connection,
                            path,
                            parent_signals,
                            result,
                            *dependency_owner);
                        continue;
                    }
                    report(
                        "FSIM-ELAB-BIND-027",
                        "input port '" + path + "." + port.name
                            + "' cannot be open without a component "
                              "default",
                        connection.span);
                }
                continue;
            }
            if (connection.kind
                    == frontend::PortActualKind::Default) {
                if (port.type.systemverilog_container) {
                    report(
                        "FSIM-ELAB-SVPORT-003",
                        "container input ports do not support default "
                        "connection values",
                        connection.span);
                    continue;
                }
                if (port.direction
                    != frontend::PortDirection::Input) {
                    report(
                        "FSIM-ELAB-VHCOMP-013",
                        "only an input component port can materialize "
                        "a default on '" + path + "." + port.name + "'",
                        connection.span);
                    continue;
                }
                const auto signal =
                    add_owned_signal(port, path, aliases);
                if (!signal) {
                    continue;
                }
                const auto width =
                    static_cast<std::size_t>(
                        port.type.width().value_or(1));
                std::string default_error;
                auto lowered = static_vhdl_value(
                    connection.value,
                    port.type,
                    default_error);
                if (!lowered
                    || lowered->width() != width) {
                    report(
                        "FSIM-ELAB-VHCOMP-013",
                        "component input default for '" + path + "."
                            + port.name
                            + "' is not a statically foldable value "
                              "compatible with the selected port type: "
                            + default_error,
                        connection.value.span);
                    continue;
                }
                design_.signals_.at(*signal).initial_value =
                    std::move(*lowered);
                continue;
            }
            if (port.type.systemverilog_container) {
                const auto object =
                    connect_container_port(
                        port,
                        connection,
                        path,
                        parent_containers,
                        parent_read_only_containers,
                        cross_language);
                if (object) {
                    container_aliases.emplace(
                        port.name, *object);
                    container_aliases.emplace(
                        path + "." + port.name, *object);
                    design_.container_by_name_.emplace(
                        path + "." + port.name, *object);
                }
                continue;
            }
            if (port.type.domain
                == frontend::ValueDomain::String) {
                const auto object = connect_string_port(
                    port,
                    connection,
                    path,
                    parent_strings,
                    parent_read_only_strings,
                    cross_language);
                if (object) {
                    string_aliases.emplace(port.name, *object);
                    string_aliases.emplace(
                        path + "." + port.name, *object);
                    design_.string_by_name_.emplace(
                        path + "." + port.name, *object);
                    design_.string_object_info_.push_back(
                        StringObjectInfo{
                            *object,
                            path + "." + port.name,
                            port.span,
                            true,
                            port.direction});
                    if (port.direction
                        == frontend::PortDirection::Input) {
                      result.read_only_strings.insert(*object);
                    }
                }
                continue;
            }
            if (connection.value.kind != frontend::ExpressionKind::Identifier) {
                if (!cross_language
                    && dependency_owner != nullptr
                    && dependency_owner->language
                        == frontend::Language::Vhdl2008
                    && connect_vhdl_expression_port(
                        port,
                        connection,
                        path,
                        parent_signals,
                        result,
                        *dependency_owner)) {
                    continue;
                }
                report(
                    "FSIM-ELAB-BIND-027",
                    "boundary connection actuals must be whole signals",
                    connection.value.span);
                continue;
            }
            const auto actual = parent_signals.find(connection.value.text);
            if (actual == parent_signals.end()) {
                report(
                    "FSIM-ELAB-BIND-028",
                    "unknown connection signal '" + connection.value.text
                        + "' on instance '" + path + "'",
                    connection.value.span);
                continue;
            }
            const auto& actual_info = design_.signal_info_.at(actual->second);
            validate_boundary_type(
                port,
                actual_info,
                path,
                connection.span,
                cross_language);
            if (cross_language
                && port.direction == frontend::PortDirection::Inout) {
                if (binding == nullptr || !binding->resolver) {
                    report(
                        "FSIM-ELAB-BIND-030",
                        "cross-language inout '" + path + "." + port.name
                            + "' requires resolver = \"std_logic\" or "
                              "\"sv_wire\"",
                        connection.span);
                }
            }
            aliases.emplace(port.name, actual->second);
            aliases.emplace(path + "." + port.name, actual->second);
            design_.signal_by_name_.emplace(
                path + "." + port.name, actual->second);
            if (port.direction == frontend::PortDirection::Output
                || port.direction == frontend::PortDirection::Inout
                || port.direction == frontend::PortDirection::Buffer) {
                note_boundary_driver(
                    actual->second,
                    binding,
                    path,
                    connection.span,
                    cross_language);
            }
        }
        if (instance.unconnected_drive
            != frontend::VerilogUnconnectedDrive::None) {
            for (std::size_t port_index = 0;
                 port_index < ports.size(); ++port_index) {
                const auto& port = ports[port_index];
                if (connected[port_index]
                    || port.direction
                        != frontend::PortDirection::Input
                    || port.type.domain
                        == frontend::ValueDomain::String
                    || port.type.systemverilog_container) {
                    continue;
                }
                auto pulled = port;
                pulled.type.spelling =
                    instance.unconnected_drive
                            == frontend::VerilogUnconnectedDrive::Pull0
                        ? "tri0"
                        : "tri1";
                (void)add_owned_signal(pulled, path, aliases);
            }
        }
        if (require_input_connections
            || std::ranges::any_of(
                ports,
                [](const auto& port) {
                  return (port.type.systemverilog_container
                          || port.type.domain
                              == frontend::ValueDomain::String)
                      && port.direction
                          == frontend::PortDirection::Input;
                })) {
            for (std::size_t port_index = 0;
                 port_index < ports.size(); ++port_index) {
                if (!connected[port_index]
                    && ports[port_index].direction
                        == frontend::PortDirection::Input) {
                    if (!cross_language
                        && dependency_owner != nullptr
                        && ports[port_index].default_value) {
                        frontend::PortConnection default_connection;
                        default_connection.port =
                            ports[port_index].name;
                        default_connection.value =
                            *ports[port_index].default_value;
                        default_connection.kind =
                            frontend::PortActualKind::Default;
                        default_connection.span =
                            ports[port_index].span;
                        (void)connect_vhdl_expression_port(
                            ports[port_index],
                            default_connection,
                            path,
                            parent_signals,
                            result,
                            *dependency_owner);
                        continue;
                    }
                    report(
                        ports[port_index].type.domain
                                    == frontend::ValueDomain::String
                            ? "FSIM-ELAB-SVPORT-010"
                        : ports[port_index].type.systemverilog_container
                            ? "FSIM-ELAB-SVPORT-006"
                            : "FSIM-ELAB-BIND-027",
                        std::string{
                            ports[port_index].type.domain
                                    == frontend::ValueDomain::String
                                ? "required mutable string input port '"
                            : ports[port_index].type
                                      .systemverilog_container
                                ? "required container input port '"
                                : "required VHDL input port '"}
                            + path + "." + ports[port_index].name
                            + "' is not associated",
                        ports[port_index].span);
                }
            }
        }
        return result;
    }
} // namespace fsim::elaboration

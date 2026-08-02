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
        const auto resolve_interface_parameter =
            [&](frontend::ParameterDeclaration& generic) {
              switch (generic.kind) {
              case frontend::ParameterKind::Type:
                  if (generic.default_type) {
                      (void)resolve_type(*generic.default_type);
                  }
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
            };
        const auto resolve_component =
            [&](frontend::VhdlComponentDeclaration& component) {
                active_interface_type_formals.clear();
                active_interface_package_formals.clear();
                for (auto& generic : component.generics) {
                    resolve_interface_parameter(generic);
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
                enum class DeclarationKind {
                    Type,
                    Constant,
                    Signal,
                    SignalAlias,
                    Component,
                    Function,
                    Task,
                    Procedure,
                    GenericFunction,
                    GenericProcedure,
                };
                struct OrderedDeclaration {
                    std::size_t offset{};
                    DeclarationKind kind{};
                    std::size_t index{};
                };
                std::vector<PriorGeneratedType> prior_types;
                std::vector<OrderedDeclaration> declarations;
                const auto append_declarations =
                    [&](const auto& source,
                        const DeclarationKind kind) {
                      for (std::size_t index = 0;
                           index < source.size(); ++index) {
                          declarations.push_back({
                              source[index].span.begin.offset,
                              kind,
                              index});
                      }
                    };
                append_declarations(
                    body.type_aliases, DeclarationKind::Type);
                append_declarations(
                    body.constants, DeclarationKind::Constant);
                append_declarations(
                    body.signals, DeclarationKind::Signal);
                append_declarations(
                    body.signal_aliases,
                    DeclarationKind::SignalAlias);
                append_declarations(
                    body.vhdl_component_declarations,
                    DeclarationKind::Component);
                append_declarations(
                    body.functions, DeclarationKind::Function);
                append_declarations(
                    body.tasks, DeclarationKind::Task);
                append_declarations(
                    body.procedures, DeclarationKind::Procedure);
                append_declarations(
                    body.generic_function_templates,
                    DeclarationKind::GenericFunction);
                append_declarations(
                    body.generic_procedure_templates,
                    DeclarationKind::GenericProcedure);
                std::ranges::stable_sort(
                    declarations, {}, &OrderedDeclaration::offset);
                for (const auto& declaration : declarations) {
                    switch (declaration.kind) {
                    case DeclarationKind::Type: {
                        auto& alias =
                            body.type_aliases[declaration.index];
                        (void)resolve_type(alias.type);
                        const auto prior =
                            generated_types.find(alias.name);
                        prior_types.push_back({
                            alias.name,
                            prior == generated_types.end()
                                ? nullptr
                                : prior->second});
                        generated_types[alias.name] = &alias.type;
                        break;
                    }
                    case DeclarationKind::Constant:
                        resolve_declaration(
                            body.constants[declaration.index]);
                        break;
                    case DeclarationKind::Signal:
                        resolve_declaration(
                            body.signals[declaration.index]);
                        break;
                    case DeclarationKind::SignalAlias:
                        (void)resolve_type(
                            body.signal_aliases[declaration.index].type);
                        break;
                    case DeclarationKind::Component:
                        resolve_component(
                            body.vhdl_component_declarations[
                                declaration.index]);
                        break;
                    case DeclarationKind::Function: {
                        auto& function =
                            body.functions[declaration.index];
                        (void)resolve_type(function.return_type);
                        for (auto& argument : function.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        for (auto& variable : function.variables) {
                            resolve_declaration(variable);
                        }
                        resolve_statements(function.statements);
                        break;
                    }
                    case DeclarationKind::Task: {
                        auto& task = body.tasks[declaration.index];
                        for (auto& argument : task.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        for (auto& variable : task.variables) {
                            resolve_declaration(variable);
                        }
                        resolve_statements(task.statements);
                        break;
                    }
                    case DeclarationKind::Procedure: {
                        auto& procedure =
                            body.procedures[declaration.index];
                        for (auto& argument : procedure.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        for (auto& variable : procedure.variables) {
                            resolve_declaration(variable);
                        }
                        resolve_statements(procedure.statements);
                        break;
                    }
                    case DeclarationKind::GenericFunction: {
                        auto& generic =
                            body.generic_function_templates[
                                declaration.index];
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        for (auto& parameter : generic.generic_parameters) {
                            resolve_interface_parameter(parameter);
                        }
                        auto& function = generic.function;
                        (void)resolve_type(function.return_type);
                        for (auto& argument : function.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        for (auto& variable : function.variables) {
                            resolve_declaration(variable);
                        }
                        resolve_statements(function.statements);
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        break;
                    }
                    case DeclarationKind::GenericProcedure: {
                        auto& generic =
                            body.generic_procedure_templates[
                                declaration.index];
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        for (auto& parameter : generic.generic_parameters) {
                            resolve_interface_parameter(parameter);
                        }
                        auto& procedure = generic.procedure;
                        for (auto& argument : procedure.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        for (auto& variable : procedure.variables) {
                            resolve_declaration(variable);
                        }
                        resolve_statements(procedure.statements);
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        break;
                    }
                    }
                }
                for (auto& process : body.processes) {
                    for (auto& variable : process.variables) {
                        resolve_declaration(variable);
                    }
                    resolve_statements(process.statements);
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
                    if (std::ranges::any_of(
                            region.block_generics,
                            [](const auto& generic) {
                              return generic.kind
                                  != frontend::ParameterKind::Value;
                            })) {
                        continue;
                    }
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

}  // namespace fsim::elaboration

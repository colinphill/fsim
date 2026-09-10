// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

using SystemVerilogReferenceMap =
    std::unordered_map<std::string, frontend::SourceSpan>;

void collect_systemverilog_references(
    const frontend::Expression& expression,
    SystemVerilogReferenceMap& references) {
  if ((expression.kind == frontend::ExpressionKind::Identifier
       || expression.kind == frontend::ExpressionKind::Call)
      && !expression.text.empty()
      && !expression.text.starts_with("@sv-")
      && expression.text.find('.') == std::string::npos
      && expression.text.find("::") == std::string::npos) {
    references.try_emplace(expression.text, expression.span);
  }
  for (const auto& association :
       expression.aggregate_choice_expressions) {
    for (const auto& choice : association) {
      collect_systemverilog_references(choice, references);
    }
  }
  for (const auto& operand : expression.operands) {
    collect_systemverilog_references(operand, references);
  }
}

void collect_systemverilog_references(
    const frontend::Type& type,
    SystemVerilogReferenceMap& references) {
  if (!type.named_type.empty()
      && type.named_type.find('.') == std::string::npos
      && type.named_type.find("::") == std::string::npos) {
    references.try_emplace(type.named_type, type.named_type_span);
  }
  if (!type.nominal_type.empty()) {
    const auto separator = type.nominal_type.rfind('.');
    references.try_emplace(
        separator == std::string::npos
            ? type.nominal_type
            : type.nominal_type.substr(separator + 1U),
        type.named_type_span);
  }
  const auto collect_range = [&](const auto& range) {
    if (range) {
      collect_systemverilog_references(range->left, references);
      collect_systemverilog_references(range->right, references);
    }
  };
  collect_range(type.packed_range_expression);
  collect_range(type.integer_range_expression);
  collect_range(type.integer_base_range_expression);
  collect_range(type.discrete_range_expression);
  collect_range(type.enumeration_range_expression);
  collect_range(type.enumeration_base_range_expression);
  for (const auto& dimension : type.systemverilog_packed_dimensions) {
    collect_systemverilog_references(dimension.left, references);
    collect_systemverilog_references(dimension.right, references);
  }
  for (const auto& member : type.packed_members) {
    collect_range(member.packed_range_expression);
    for (const auto& nested : member.nested_types) {
      collect_systemverilog_references(nested, references);
    }
  }
  if (type.systemverilog_container) {
    if (type.systemverilog_container->queue_maximum) {
      collect_systemverilog_references(
          *type.systemverilog_container->queue_maximum, references);
    }
    for (const auto& range :
         type.systemverilog_container->static_range_expressions) {
      collect_systemverilog_references(range.left, references);
      collect_systemverilog_references(range.right, references);
    }
    if (type.systemverilog_container->associative_index_type) {
      collect_systemverilog_references(
          *type.systemverilog_container->associative_index_type,
          references);
    }
    for (const auto& element :
         type.systemverilog_container->element_types) {
      collect_systemverilog_references(element, references);
    }
  }
}

void collect_systemverilog_type_references_at(
    const frontend::Type& type,
    const frontend::SourceSpan& occurrence,
    SystemVerilogReferenceMap& references) {
  SystemVerilogReferenceMap type_references;
  collect_systemverilog_references(type, type_references);
  for (const auto& [name, span] : type_references) {
    (void)span;
    references.insert_or_assign(name, occurrence);
  }
}

void collect_systemverilog_references(
    const std::vector<frontend::Statement>& statements,
    SystemVerilogReferenceMap& references) {
  for (const auto& statement : statements) {
    collect_systemverilog_references(statement.target, references);
    collect_systemverilog_references(statement.value, references);
    collect_systemverilog_references(statement.condition, references);
    collect_systemverilog_references(statement.vhdl_guard, references);
    collect_systemverilog_references(statement.loop_initial, references);
    collect_systemverilog_references(statement.loop_limit, references);
    collect_systemverilog_references(
        statement.loop_update_target, references);
    if (statement.kind == frontend::StatementKind::TaskCall
        && !statement.task_name.empty()
        && !statement.task_name.starts_with("@sv-")
        && statement.task_name.find("::") == std::string::npos) {
      references.try_emplace(statement.task_name, statement.span);
    }
    for (const auto& argument : statement.task_arguments) {
      collect_systemverilog_references(argument, references);
    }
    for (const auto& sensitivity : statement.sensitivities) {
      collect_systemverilog_references(
          sensitivity.expression, references);
    }
    for (const auto& association : statement.procedure_arguments) {
      collect_systemverilog_references(association.value, references);
    }
    for (const auto& declaration : statement.declarations) {
      collect_systemverilog_type_references_at(
          declaration.type, declaration.span, references);
      if (declaration.initializer) {
        collect_systemverilog_references(
            *declaration.initializer, references);
      }
    }
    for (const auto& alternative : statement.case_alternatives) {
      for (const auto& choice : alternative.choices) {
        collect_systemverilog_references(choice, references);
      }
      collect_systemverilog_references(
          alternative.statements, references);
    }
    collect_systemverilog_references(statement.statements, references);
    collect_systemverilog_references(
        statement.else_statements, references);
    collect_systemverilog_references(
        statement.loop_updates, references);
  }
}

template <typename Region>
void collect_systemverilog_region_references(
    const Region& region,
    SystemVerilogReferenceMap& references) {
  if constexpr (requires { region.return_type; }) {
    collect_systemverilog_type_references_at(
        region.return_type, region.span, references);
  }
  if constexpr (requires { region.arguments; }) {
    for (const auto& argument : region.arguments) {
      collect_systemverilog_type_references_at(
          argument.type, argument.span, references);
      if (argument.default_value) {
        collect_systemverilog_references(
            *argument.default_value, references);
      }
    }
  }
  for (const auto& variable : region.variables) {
    collect_systemverilog_type_references_at(
        variable.type, variable.span, references);
    if (variable.initializer) {
      collect_systemverilog_references(
          *variable.initializer, references);
    }
  }
  collect_systemverilog_references(region.statements, references);
}

void collect_systemverilog_references(
    const std::vector<frontend::GenerateRegion>& generates,
    SystemVerilogReferenceMap& references);

void collect_systemverilog_references(
    const frontend::GenerateBody& body,
    SystemVerilogReferenceMap& references) {
  for (const auto& alias : body.type_aliases) {
    collect_systemverilog_type_references_at(
        alias.type, alias.span, references);
  }
  for (const auto& constant : body.constants) {
    collect_systemverilog_type_references_at(
        constant.type, constant.span, references);
    collect_systemverilog_references(
        constant.default_value, references);
  }
  for (const auto& signal : body.signals) {
    collect_systemverilog_type_references_at(
        signal.type, signal.span, references);
  }
  for (const auto& variable : body.variables) {
    collect_systemverilog_type_references_at(
        variable.type, variable.span, references);
    if (variable.initializer) {
      collect_systemverilog_references(
          *variable.initializer, references);
    }
  }
  for (const auto& function : body.functions) {
    collect_systemverilog_region_references(function, references);
  }
  for (const auto& task : body.tasks) {
    collect_systemverilog_region_references(task, references);
  }
  for (const auto& process : body.processes) {
    collect_systemverilog_region_references(process, references);
  }
  collect_systemverilog_references(
      body.concurrent_statements, references);
  for (const auto& instance : body.instances) {
    for (const auto& override : instance.parameter_overrides) {
      collect_systemverilog_references(override.value, references);
    }
    for (const auto& connection : instance.connections) {
      collect_systemverilog_references(connection.value, references);
    }
  }
  collect_systemverilog_references(body.generate_regions, references);
}

void collect_systemverilog_references(
    const std::vector<frontend::GenerateRegion>& generates,
    SystemVerilogReferenceMap& references) {
  for (const auto& generate : generates) {
    collect_systemverilog_references(generate.initial, references);
    collect_systemverilog_references(generate.condition, references);
    collect_systemverilog_references(generate.iteration, references);
    collect_systemverilog_references(generate.then_body, references);
    collect_systemverilog_references(generate.else_body, references);
    for (const auto& alternative : generate.alternatives) {
      for (const auto& choice : alternative.choices) {
        collect_systemverilog_references(choice.left, references);
        if (choice.right) {
          collect_systemverilog_references(*choice.right, references);
        }
      }
      collect_systemverilog_references(
          alternative.body, references);
    }
  }
}

SystemVerilogReferenceMap systemverilog_references(
    const frontend::DesignUnit& unit) {
  SystemVerilogReferenceMap references;
  for (const auto& alias : unit.type_aliases) {
    collect_systemverilog_type_references_at(
        alias.type, alias.span, references);
  }
  for (const auto& parameter : unit.parameters) {
    collect_systemverilog_type_references_at(
        parameter.type, parameter.span, references);
    if (parameter.default_type) {
      collect_systemverilog_type_references_at(
          *parameter.default_type, parameter.span, references);
    }
    collect_systemverilog_references(
        parameter.default_value, references);
  }
  for (const auto& port : unit.ports) {
    collect_systemverilog_type_references_at(
        port.type, port.span, references);
  }
  for (const auto& signal : unit.signals) {
    collect_systemverilog_type_references_at(
        signal.type, signal.span, references);
  }
  for (const auto& variable : unit.variables) {
    collect_systemverilog_type_references_at(
        variable.type, variable.span, references);
    if (variable.initializer) {
      collect_systemverilog_references(
          *variable.initializer, references);
    }
  }
  for (const auto& function : unit.functions) {
    collect_systemverilog_region_references(function, references);
  }
  for (const auto& task : unit.tasks) {
    collect_systemverilog_region_references(task, references);
  }
  for (const auto& process : unit.processes) {
    collect_systemverilog_region_references(process, references);
  }
  collect_systemverilog_references(
      unit.concurrent_statements, references);
  for (const auto& instance : unit.instances) {
    for (const auto& override : instance.parameter_overrides) {
      collect_systemverilog_references(override.value, references);
    }
    for (const auto& connection : instance.connections) {
      collect_systemverilog_references(connection.value, references);
    }
  }
  collect_systemverilog_references(unit.generate_regions, references);
  return references;
}

} // namespace

    void HierarchyBuilder::import_qualified_vhdl_package_constants(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack) {
        auto identifiers = qualified_identifiers(unit);
        std::unordered_set<std::string> local_objects;
        std::unordered_set<std::string> local_qualified_items;
        for (const auto& parameter : unit.parameters) {
            if (parameter.name.find('.') != std::string::npos) {
                local_qualified_items.insert(parameter.name);
            }
        }
        for (const auto& alias : unit.type_aliases) {
            if (alias.name.find('.') != std::string::npos) {
                local_qualified_items.insert(alias.name);
            }
        }
        for (const auto& function : unit.functions) {
            if (function.name.find('.') != std::string::npos) {
                local_qualified_items.insert(function.name);
            }
        }
        for (const auto& procedure : unit.procedures) {
            if (procedure.name.find('.') != std::string::npos) {
                local_qualified_items.insert(procedure.name);
            }
        }
        for (const auto& port : unit.ports) {
            local_objects.emplace(port.name);
        }
        for (const auto& signal : unit.signals) {
            local_objects.emplace(signal.name);
        }
        for (const auto& variable : unit.variables) {
            local_objects.emplace(variable.name);
        }
        for (const auto& process : unit.processes) {
            for (const auto& variable : process.variables) {
                local_objects.emplace(variable.name);
            }
        }
        std::vector<std::string> ordered;
        ordered.reserve(identifiers.size());
        for (const auto& [identifier, span] : identifiers) {
            (void)span;
            ordered.push_back(identifier);
        }
        std::sort(ordered.begin(), ordered.end());
        const auto owner_library =
            unit.library.empty()
                ? std::string{"work"}
                : unit.library;
        std::vector<frontend::ParameterDeclaration> imports;
        std::unordered_set<std::string> dependencies;
        for (const auto& identifier : ordered) {
            const auto& reference_span =
                identifiers.at(identifier);
            if (frontend::vhdl_simulator_api(identifier)
                != frontend::VhdlSimulatorApi::none) {
                continue;
            }
            if (local_qualified_items.contains(identifier)) {
                continue;
            }
            const auto parts = selected_name_parts(identifier);
            if (!parts.empty()
                && std::ranges::any_of(
                    unit.package_instances,
                    [&](const auto& instance) {
                        return instance.name == parts.front();
                    })) {
                continue;
            }
            if (!parts.empty()
                && local_objects.contains(parts.front())) {
                // A selected record element has the same lexical shape as
                // package.constant. Local object declarations take
                // precedence in VHDL name resolution.
                continue;
            }
            if (parts.size() != 2 && parts.size() != 3) {
                report(
                    "FSIM-ELAB-PKG-008",
                    "a selected package constant must be "
                    "package.constant or library.package.constant",
                    reference_span);
                continue;
            }
            const auto package_name =
                parts[parts.size() - 2];
            const auto constant_name = parts.back();
            const auto requested_library =
                parts.size() == 2
                    ? owner_library
                    : parts.front() == "work"
                        ? owner_library
                        : parts.front();
            const auto package = std::find_if(
                parsed_.units.begin(),
                parsed_.units.end(),
                [&](const DesignUnit& candidate) {
                    const auto candidate_library =
                        candidate.library.empty()
                            ? std::string_view{"work"}
                            : std::string_view{
                                  candidate.library};
                    return candidate.kind
                            == frontend::UnitKind::VhdlPackage
                        && candidate.primary_name.empty()
                        && candidate.name == package_name
                        && candidate_library
                            == requested_library;
                });
            if (package == parsed_.units.end()) {
                report(
                    "FSIM-ELAB-PKG-009",
                    "VHDL package '"
                        + requested_library + "."
                        + package_name + "' was not found",
                    reference_span);
                continue;
            }
            auto specialized_package =
                specialize_vhdl_package(
                    *package, import_stack, reference_span);
            if (!specialized_package) {
                continue;
            }
            const bool exported_function =
                std::ranges::any_of(
                    package->functions,
                    [&](const auto& candidate) {
                      return candidate.name == constant_name;
                    })
                && std::ranges::any_of(
                    specialized_package->unit.functions,
                    [&](const auto& candidate) {
                      return candidate.name == constant_name
                          && candidate.defined;
                    });
            const bool exported_procedure =
                std::ranges::any_of(
                    package->procedures,
                    [&](const auto& candidate) {
                      return candidate.name == constant_name;
                    })
                && std::ranges::any_of(
                    specialized_package->unit.procedures,
                    [&](const auto& candidate) {
                      return candidate.name == constant_name
                          && candidate.defined;
                    });
            if (exported_function || exported_procedure) {
                const auto separator = identifier.rfind('.');
                const auto prefix = identifier.substr(0, separator);
                const PackageBinding binding{
                    requested_library + "." + package_name,
                    specialized_package->unit,
                    specialized_package->environment,
                    specialized_package->values,
                    specialized_package->identity_values};
                materialize_vhdl_package_binding(
                    unit, prefix, binding);
                continue;
            }
            const auto declaration = std::find_if(
                package->parameters.begin(),
                package->parameters.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            if (declaration == package->parameters.end()) {
                const bool enumeration_literal =
                    std::any_of(
                        specialized_package->unit.type_aliases.begin(),
                        specialized_package->unit.type_aliases.end(),
                        [&](const auto& alias) {
                            return std::find(
                                       alias.type
                                           .enumeration_literals.begin(),
                                       alias.type
                                           .enumeration_literals.end(),
                                       constant_name)
                                != alias.type
                                       .enumeration_literals.end();
                        });
                const bool type_mark = std::any_of(
                        specialized_package->unit.type_aliases.begin(),
                        specialized_package->unit.type_aliases.end(),
                        [&](const auto& alias) { return alias.name == constant_name; });
                const bool standard_item = std::ranges::find(
                    package->standard_package_declarations, constant_name)
                    != package->standard_package_declarations.end();
                if (!enumeration_literal && !type_mark && !standard_item) {
                    report(
                        "FSIM-ELAB-PKG-010",
                        "VHDL package '" + requested_library
                            + "." + package_name
                            + "' has no exported item '"
                            + constant_name + "'",
                        reference_span);
                }
                if (enumeration_literal || type_mark || standard_item) {
                    const auto package_source = std::string{
                        frontend::physical_source(package->span)};
                    if (dependencies.insert(package_source).second) {
                        unit.source_dependencies.push_back(
                            package_source);
                    }
                    for (const auto& dependency :
                         specialized_package->unit
                             .source_dependencies) {
                        if (dependencies.insert(dependency).second) {
                            unit.source_dependencies.push_back(
                                dependency);
                        }
                    }
                }
                continue;
            }
            const auto value =
                specialized_package->environment.find(
                    constant_name);
            if (value
                == specialized_package->environment.end()) {
                continue;
            }
            const auto specialized_declaration =
                std::find_if(
                    specialized_package->unit.parameters.begin(),
                    specialized_package->unit.parameters.end(),
                    [&](const auto& candidate) {
                        return candidate.name
                                == declaration->name
                            && candidate.span.source_name
                                == declaration->span.source_name
                            && candidate.span.begin.offset
                                == declaration->span.begin.offset;
                    });
            const auto& imported_type =
                specialized_declaration
                        == specialized_package->unit.parameters.end()
                    ? declaration->type
                    : specialized_declaration->type;
            imports.push_back({
                identifier,
                imported_type,
                constant_expression(
                    value->second,
                    declaration->span,
                    imported_type.domain,
                    frontend::Language::Vhdl2008,
                    !imported_type.enumeration_literals.empty()),
                true,
                declaration->span});
            const auto package_source = std::string{
                frontend::physical_source(package->span)};
            if (dependencies.insert(package_source).second) {
                unit.source_dependencies.push_back(
                    package_source);
            }
            for (const auto& dependency :
                 specialized_package->unit.source_dependencies) {
                if (dependencies.insert(dependency).second) {
                    unit.source_dependencies.push_back(
                        dependency);
                }
            }
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
    }



    void HierarchyBuilder::import_qualified_vhdl_package_types(
        DesignUnit& unit,
        NamedTypeEnvironment& imported_types,
        std::vector<const DesignUnit*>& import_stack) {
        std::unordered_map<
            std::string, frontend::SourceSpan> referenced_types;
        visit_declared_types(
            unit,
            [&](const frontend::Type& type) {
                if (!type.named_type.empty()
                    && type.named_type.find('.')
                        != std::string::npos) {
                    referenced_types.try_emplace(
                        type.named_type,
                        type.named_type_span);
                }
                if (type.vhdl_array
                    && !type.vhdl_array->element_named_type.empty()
                    && type.vhdl_array->element_named_type.find('.')
                        != std::string::npos) {
                    referenced_types.try_emplace(
                        type.vhdl_array->element_named_type,
                        type.vhdl_array->element_span);
                }
            },
            true);
        const auto owner_library =
            unit.library.empty()
                ? std::string{"work"}
                : unit.library;
        const auto expression_identifiers =
            qualified_identifiers(unit);
        for (const auto& [name, span] :
             expression_identifiers) {
            const auto parts = selected_name_parts(name);
            if (!parts.empty()
                && std::ranges::any_of(
                    unit.package_instances,
                    [&](const auto& instance) {
                        return instance.name == parts.front();
                    })) {
                continue;
            }
            if (parts.size() != 2 && parts.size() != 3) {
                continue;
            }
            const auto package_name =
                parts[parts.size() - 2];
            const auto type_name = parts.back();
            const auto requested_library =
                parts.size() == 2
                    ? owner_library
                    : parts.front() == "work"
                        ? owner_library
                        : parts.front();
            const auto package = std::find_if(
                parsed_.units.begin(),
                parsed_.units.end(),
                [&](const DesignUnit& candidate) {
                    const auto candidate_library =
                        candidate.library.empty()
                            ? std::string_view{"work"}
                            : std::string_view{
                                  candidate.library};
                    return candidate.kind
                            == frontend::UnitKind::VhdlPackage
                        && candidate.primary_name.empty()
                        && candidate.name == package_name
                        && candidate_library
                            == requested_library;
                });
            if (package == parsed_.units.end()
                || std::none_of(
                    package->type_aliases.begin(),
                    package->type_aliases.end(),
                    [&](const auto& alias) {
                        return alias.name == type_name;
                    })) {
                continue;
            }
            referenced_types.try_emplace(name, span);
        }
        std::vector<std::string> ordered;
        ordered.reserve(referenced_types.size());
        for (const auto& [name, span] : referenced_types) {
            (void)span;
            ordered.push_back(name);
        }
        std::sort(ordered.begin(), ordered.end());
        for (const auto& name : ordered) {
            const auto& reference_span =
                referenced_types.at(name);
            if (imported_types.contains(name)
                || std::ranges::any_of(
                    unit.type_aliases,
                    [&](const auto& alias) {
                        return alias.name == name;
                    })) {
                continue;
            }
            const auto parts = selected_name_parts(name);
            if (!parts.empty()
                && std::ranges::any_of(
                    unit.package_instances,
                    [&](const auto& instance) {
                        return instance.name == parts.front();
                    })) {
                continue;
            }
            if (parts.size() != 2 && parts.size() != 3) {
                report(
                    "FSIM-ELAB-VHTYPE-004",
                    "a selected VHDL type must be "
                    "package.type or library.package.type",
                    reference_span);
                continue;
            }
            const auto package_name =
                parts[parts.size() - 2];
            const auto type_name = parts.back();
            const auto requested_library =
                parts.size() == 2
                    ? owner_library
                    : parts.front() == "work"
                        ? owner_library
                        : parts.front();
            const auto package = std::find_if(
                parsed_.units.begin(),
                parsed_.units.end(),
                [&](const DesignUnit& candidate) {
                    const auto candidate_library =
                        candidate.library.empty()
                            ? std::string_view{"work"}
                            : std::string_view{
                                  candidate.library};
                    return candidate.kind
                            == frontend::UnitKind::VhdlPackage
                        && candidate.primary_name.empty()
                        && candidate.name == package_name
                        && candidate_library
                            == requested_library;
                });
            if (package == parsed_.units.end()) {
                report(
                    "FSIM-ELAB-PKG-009",
                    "VHDL package '" + requested_library
                        + "." + package_name
                        + "' was not found",
                    reference_span);
                continue;
            }
            auto specialized_package =
                specialize_vhdl_package(
                    *package, import_stack, reference_span);
            if (!specialized_package) {
                continue;
            }
            const auto alias = std::find_if(
                specialized_package->unit.type_aliases.begin(),
                specialized_package->unit.type_aliases.end(),
                [&](const auto& candidate) {
                    return candidate.name == type_name;
                });
            if (alias
                == specialized_package->unit.type_aliases.end()) {
                report(
                    "FSIM-ELAB-VHTYPE-004",
                    "VHDL package '" + requested_library
                        + "." + package_name
                        + "' has no type '" + type_name + "'",
                    reference_span);
                continue;
            }
            imported_types.insert_or_assign(
                name,
                NamedTypeBinding{
                    alias->type,
                    requested_library + "."
                        + package_name});
            const auto append_dependency =
                [&](const std::string& dependency) {
                    if (std::find(
                            unit.source_dependencies.begin(),
                            unit.source_dependencies.end(),
                            dependency)
                        == unit.source_dependencies.end()) {
                        unit.source_dependencies.push_back(
                            dependency);
                    }
                };
            append_dependency(std::string{
                frontend::physical_source(package->span)});
            for (const auto& dependency :
                 specialized_package->unit.source_dependencies) {
                append_dependency(dependency);
            }
        }
    }



    std::optional<SpecializedUnit>
    HierarchyBuilder::specialize_systemverilog_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span) {
        if (std::find(
                import_stack.begin(),
                import_stack.end(),
                &package)
            != import_stack.end()) {
            std::string cycle;
            for (const auto* imported : import_stack) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += imported->name;
            }
            cycle += " -> " + package.name;
            report(
                "FSIM-ELAB-SVPKG-004",
                "cyclic SystemVerilog package visibility: "
                    + cycle,
                reference_span);
            return std::nullopt;
        }
        import_stack.push_back(&package);
        auto effective_package = package;
        NamedTypeEnvironment type_environment;
        import_systemverilog_package_items(
            effective_package, import_stack, type_environment);
        import_qualified_systemverilog_package_items(
            effective_package, import_stack, type_environment);
        validate_systemverilog_exports(
            package, effective_package);
        for (const auto& parameter :
             effective_package.parameters) {
            if (parameter.kind
                != frontend::ParameterKind::Type) {
                continue;
            }
            type_environment.insert_or_assign(
                parameter.name,
                NamedTypeBinding{
                    {},
                    effective_package.name,
                    true});
        }
        resolve_named_types(
            effective_package, type_environment);
        auto type_specialized =
            specialize_systemverilog_type_parameters(
                effective_package,
                {},
                {},
                {},
                frontend::Language::SystemVerilog2017,
                diagnostics_);
        if (type_specialized.applied) {
            resolve_named_types(
                type_specialized.unit, {}, false);
        }
        auto specialized = specialize_unit(
            type_specialized.unit,
            type_specialized.value_overrides,
            {},
            frontend::Language::SystemVerilog2017,
            diagnostics_);
        import_stack.pop_back();
        return specialized;
    }



    const DesignUnit* HierarchyBuilder::find_systemverilog_package(
        const DesignUnit& owner,
        const std::string_view name) const {
        const auto owner_library =
            owner.library.empty()
                ? std::string_view{"work"}
                : std::string_view{owner.library};
        const auto found = std::find_if(
            parsed_.units.begin(),
            parsed_.units.end(),
            [&](const DesignUnit& candidate) {
                const auto candidate_library =
                    candidate.library.empty()
                        ? std::string_view{"work"}
                        : std::string_view{candidate.library};
                return candidate.kind
                        == frontend::UnitKind::
                            SystemVerilogPackage
                    && candidate.name == name
                    && candidate_library == owner_library;
            });
        return found == parsed_.units.end()
            ? nullptr
            : &*found;
    }



    void HierarchyBuilder::append_package_dependencies(
        DesignUnit& unit,
        const DesignUnit& package,
        const SpecializedUnit& specialized) {
        const auto append = [&](const std::string& dependency) {
            if (std::find(
                    unit.source_dependencies.begin(),
                    unit.source_dependencies.end(),
                    dependency)
                == unit.source_dependencies.end()) {
                unit.source_dependencies.push_back(dependency);
            }
        };
        append(std::string{frontend::physical_source(package.span)});
        for (const auto& dependency :
             specialized.unit.source_dependencies) {
            append(dependency);
        }
    }



    void HierarchyBuilder::import_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& type_environment) {
        std::vector<frontend::ParameterDeclaration> imports;
        std::vector<frontend::FunctionDeclaration>
            function_imports;
        std::vector<frontend::TaskDeclaration> task_imports;
        std::vector<frontend::TypeAliasDeclaration> type_imports;
        std::vector<frontend::SystemVerilogLetDeclaration> let_imports;
        std::vector<frontend::SystemVerilogClassDeclaration> class_imports;
        std::unordered_map<std::string, std::string> owners;
        const auto declaration_identity = [](const auto& declaration) {
            return declaration.span.source_name + ":"
                + std::to_string(declaration.span.begin.offset);
        };
        const auto referenced_names =
            systemverilog_references(unit);
        std::unordered_set<std::string> local_names;
        const auto remember_local_names = [&](const auto& declarations) {
            for (const auto& declaration : declarations) {
                local_names.insert(declaration.name);
            }
        };
        remember_local_names(unit.parameters);
        remember_local_names(unit.type_aliases);
        remember_local_names(unit.ports);
        for (const auto& signal : unit.signals) {
            const bool implicit_reference =
                !signal.is_port
                && signal.direction == frontend::PortDirection::Unknown
                && signal.type.named_type.empty()
                && signal.type.spelling != "event"
                && signal.span.end.offset >= signal.span.begin.offset
                && signal.span.end.offset - signal.span.begin.offset
                    == signal.name.size();
            if (!implicit_reference) {
                local_names.insert(signal.name);
            }
        }
        remember_local_names(unit.variables);
        remember_local_names(unit.functions);
        remember_local_names(unit.tasks);
        remember_local_names(unit.systemverilog_lets);
        remember_local_names(unit.systemverilog_classes);
        for (const auto& import_item :
             unit.systemverilog_imports) {
            if (!import_item.name.empty()
                && local_names.contains(import_item.name)) {
                report(
                    "FSIM-ELAB-SVPKG-009",
                    "explicit SystemVerilog package import '"
                        + import_item.package + "::" + import_item.name
                        + "' conflicts with a declaration in the same scope",
                    import_item.span);
                continue;
            }
            const auto* package =
                find_systemverilog_package(
                    unit, import_item.package);
            if (package == nullptr) {
                report(
                    "FSIM-ELAB-SVPKG-001",
                    "SystemVerilog package '"
                        + import_item.package + "' was not found",
                    import_item.span);
                continue;
            }
            auto specialized_package =
                specialize_systemverilog_package(
                    *package, import_stack, import_item.span);
            if (!specialized_package) {
                continue;
            }
            const bool wildcard = import_item.name.empty();
            bool found_selected = wildcard;
            const auto reexported_from_import =
                [&](const std::string_view name) {
                    return unit.kind
                            == frontend::UnitKind::SystemVerilogPackage
                        && std::ranges::any_of(
                            unit.systemverilog_exports,
                            [&](const frontend::SystemVerilogExport& item) {
                                const bool package_matches =
                                    item.package == "*"
                                    || item.package == import_item.package;
                                const bool name_matches =
                                    item.name.empty() || item.name == name;
                                return package_matches && name_matches;
                        });
                };
            std::unordered_set<std::string> required_wildcard_names;
            for (const auto& [name, span] : referenced_names) {
                if (span.source_name != import_item.span.source_name
                    || span.begin.offset > import_item.span.end.offset) {
                    required_wildcard_names.insert(name);
                }
            }
            bool expanded_required_names = true;
            while (expanded_required_names) {
                expanded_required_names = false;
                const auto include_dependencies =
                    [&](const frontend::Type& type,
                        const frontend::Expression* value = nullptr) {
                        SystemVerilogReferenceMap dependencies;
                        collect_systemverilog_references(type, dependencies);
                        if (value != nullptr) {
                            collect_systemverilog_references(
                                *value, dependencies);
                        }
                        for (const auto& [name, span] : dependencies) {
                            (void)span;
                            expanded_required_names =
                                required_wildcard_names.insert(name).second
                                || expanded_required_names;
                        }
                    };
                for (const auto& declaration :
                     specialized_package->unit.parameters) {
                    if (required_wildcard_names.contains(
                            declaration.name)) {
                        include_dependencies(
                            declaration.type,
                            &declaration.default_value);
                    }
                }
                for (const auto& alias :
                     specialized_package->unit.type_aliases) {
                    if (required_wildcard_names.contains(alias.name)) {
                        include_dependencies(alias.type);
                    }
                }
            }
            const auto wildcard_reference_visible =
                [&](const std::string_view name) {
                    if (!wildcard || reexported_from_import(name)) {
                        return true;
                    }
                    return required_wildcard_names.contains(
                        std::string{name});
                };
            const auto directly_declared =
                [](const auto& candidate, const auto& declarations) {
                    return std::ranges::any_of(
                        declarations,
                        [&](const auto& declaration) {
                            return declaration.name == candidate.name
                                && declaration.span.source_name
                                    == candidate.span.source_name
                                && declaration.span.begin.offset
                                    == candidate.span.begin.offset;
                        });
                };
            const auto explicitly_exported =
                [&](const std::string_view name) {
                    return std::ranges::any_of(
                        package->systemverilog_exports,
                        [&](const frontend::SystemVerilogExport& item) {
                            if (!item.name.empty()
                                && item.name != name) {
                                return false;
                            }
                            return std::ranges::any_of(
                                package->systemverilog_imports,
                                [&](const frontend::SystemVerilogImport& imported) {
                                    const bool package_matches =
                                        item.package == "*"
                                        || imported.package == item.package;
                                    const bool name_matches =
                                        imported.name.empty()
                                        || imported.name == name;
                                    return package_matches && name_matches;
                                });
                        });
                };
            for (const auto& declaration :
                 specialized_package->unit.parameters) {
                const bool public_item = directly_declared(
                    declaration, package->parameters)
                    || explicitly_exported(declaration.name);
                if (!public_item) {
                    continue;
                }
                if (!wildcard_reference_visible(declaration.name)) {
                    continue;
                }
                if (wildcard
                    && local_names.contains(declaration.name)) {
                    continue;
                }
                if (!wildcard
                    && declaration.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto& imported_type = declaration.type;
                frontend::Expression imported_value;
                if (imported_type.spelling == "string") {
                    const auto value =
                        specialized_package->string_environment.find(
                            declaration.name);
                    if (value
                        == specialized_package
                               ->string_environment.end()) {
                        continue;
                    }
                    imported_value =
                        value->second.expression(declaration.span);
                } else if (const auto value =
                               specialized_package->scalar_environment.find(
                                   declaration.name);
                           value != specialized_package
                                        ->scalar_environment.end()) {
                    imported_value =
                        value->second.expression(declaration.span);
                } else if (const auto integral_constant =
                               specialized_package->integral_environment.find(
                                   declaration.name);
                           integral_constant != specialized_package
                                        ->integral_environment.end()) {
                    imported_value =
                        integral_constant->second.expression(declaration.span);
                } else {
                    const auto integral_value =
                        specialized_package->environment.find(
                            declaration.name);
                    if (integral_value
                        == specialized_package->environment.end()) {
                        continue;
                    }
                    imported_value = constant_expression(
                        integral_value->second,
                        declaration.span,
                        imported_type.domain,
                        frontend::Language::
                            SystemVerilog2017);
                }
                const auto [owner, inserted] =
                    owners.emplace(
                        declaration.name,
                        declaration_identity(declaration));
                if (!inserted
                    && owner->second
                        != declaration_identity(declaration)) {
                    report(
                        "FSIM-ELAB-SVPKG-003",
                        "SystemVerilog package constant '"
                            + declaration.name
                            + "' is imported from multiple "
                              "packages",
                        import_item.span);
                    continue;
                }
                if (std::any_of(
                        imports.begin(),
                        imports.end(),
                        [&](const auto& existing) {
                            return existing.name
                                == declaration.name;
                        })) {
                    continue;
                }
                imports.push_back({
                    declaration.name,
                    imported_type,
                    std::move(imported_value),
                    true,
                    declaration.span});
            }
            for (const auto& alias :
                 specialized_package->unit.type_aliases) {
                const bool public_item = directly_declared(
                    alias, package->type_aliases)
                    || directly_declared(
                        alias, package->parameters)
                    || explicitly_exported(alias.name);
                if (!public_item) {
                    continue;
                }
                if (!wildcard_reference_visible(alias.name)) {
                    continue;
                }
                if (wildcard && local_names.contains(alias.name)) {
                    continue;
                }
                if (!wildcard
                    && alias.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [owner, owner_inserted] =
                    owners.emplace(
                        alias.name, declaration_identity(alias));
                const bool conflicting_owner = !owner_inserted
                    && owner->second != declaration_identity(alias);
                const auto [existing, inserted] =
                    type_environment.emplace(
                        alias.name,
                        NamedTypeBinding{
                            alias.type,
                            package->name});
                if (conflicting_owner) {
                    report(
                        "FSIM-ELAB-SVTYPE-002",
                        "SystemVerilog type '" + alias.name
                            + "' is imported from multiple "
                              "packages",
                        import_item.span);
                }
                (void)existing;
                (void)inserted;
                if (reexported_from_import(alias.name)
                    && std::ranges::none_of(
                        type_imports,
                        [&](const auto& existing_alias) {
                          return existing_alias.name == alias.name;
                        })) {
                  type_imports.push_back(alias);
                }
            }
            for (const auto& declaration :
                 specialized_package->unit.systemverilog_classes) {
                const bool public_item = directly_declared(
                    declaration, package->systemverilog_classes)
                    || explicitly_exported(declaration.name);
                if (!public_item) {
                    continue;
                }
                if (!wildcard_reference_visible(declaration.name)) {
                    continue;
                }
                if (wildcard
                    && local_names.contains(declaration.name)) {
                    continue;
                }
                if (!wildcard
                    && declaration.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [owner, inserted] = owners.emplace(
                    declaration.name,
                    declaration_identity(declaration));
                if (!inserted
                    && owner->second
                        != declaration_identity(declaration)) {
                    report(
                        "FSIM-ELAB-SVTYPE-002",
                        "SystemVerilog class '" + declaration.name
                            + "' is imported from multiple packages",
                        import_item.span);
                    continue;
                }
                if (reexported_from_import(declaration.name)
                    && std::ranges::none_of(
                        class_imports,
                        [&](const auto& existing) {
                          return existing.name == declaration.name;
                        })) {
                  class_imports.push_back(declaration);
                }
            }
            for (const auto& declaration :
                specialized_package->unit.systemverilog_lets) {
                const bool public_item = directly_declared(
                                             declaration, package->systemverilog_lets)
                    || explicitly_exported(declaration.name);
                if (!public_item) {
                    continue;
                }
                if (!wildcard_reference_visible(declaration.name)) {
                    continue;
                }
                if (wildcard
                    && local_names.contains(declaration.name)) {
                    continue;
                }
                if (!wildcard
                    && declaration.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [owner, inserted] = owners.emplace(
                    declaration.name,
                    declaration_identity(declaration));
                if (!inserted
                    && owner->second
                        != declaration_identity(declaration)) {
                    report(
                        "FSIM-ELAB-SVLET-006",
                        "SystemVerilog let declaration '"
                            + declaration.name
                            + "' is imported from multiple packages",
                        import_item.span);
                    continue;
                }
                if (std::ranges::none_of(
                        let_imports,
                        [&](const auto& existing) {
                            return existing.name == declaration.name;
                        })) {
                    let_imports.push_back(declaration);
                }
            }
            for (const auto& function :
                 specialized_package->unit.functions) {
                const bool public_item = directly_declared(
                    function, package->functions)
                    || explicitly_exported(function.name);
                if (!public_item) {
                    continue;
                }
                if (!wildcard_reference_visible(function.name)) {
                    continue;
                }
                if (wildcard && local_names.contains(function.name)) {
                    continue;
                }
                if (!wildcard
                    && function.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [owner, inserted] =
                    owners.emplace(
                        function.name,
                        declaration_identity(function));
                if (!inserted
                    && owner->second
                        != declaration_identity(function)) {
                    report(
                        "FSIM-ELAB-SVFUNC-007",
                        "SystemVerilog function '"
                            + function.name
                            + "' is imported from multiple packages",
                        import_item.span);
                    continue;
                }
                if (std::ranges::none_of(
                        function_imports,
                        [&](const auto& existing) {
                            return existing.name == function.name;
                        })) {
                    function_imports.push_back(function);
                }
            }
            for (const auto& task :
                 specialized_package->unit.tasks) {
                const bool public_item = directly_declared(
                    task, package->tasks)
                    || explicitly_exported(task.name);
                if (!public_item) {
                    continue;
                }
                if (!wildcard_reference_visible(task.name)) {
                    continue;
                }
                if (wildcard && local_names.contains(task.name)) {
                    continue;
                }
                if (!wildcard
                    && task.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [owner, inserted] =
                    owners.emplace(
                        task.name,
                        declaration_identity(task));
                if (!inserted
                    && owner->second
                        != declaration_identity(task)) {
                    report(
                        "FSIM-ELAB-SVTASK-009",
                        "SystemVerilog task '"
                            + task.name
                            + "' is imported from multiple packages",
                        import_item.span);
                    continue;
                }
                if (std::ranges::none_of(
                        task_imports,
                        [&](const auto& existing) {
                            return existing.name == task.name;
                        })) {
                    task_imports.push_back(task);
                }
            }
            if (!found_selected) {
                report(
                    "FSIM-ELAB-SVPKG-002",
                    "SystemVerilog package '"
                        + package->name
                        + "' has no exported item '"
                        + import_item.name + "'",
                    import_item.span);
            }
            append_package_dependencies(
                unit, *package, *specialized_package);
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
        type_imports.insert(
            type_imports.end(),
            std::make_move_iterator(unit.type_aliases.begin()),
            std::make_move_iterator(unit.type_aliases.end()));
        unit.type_aliases = std::move(type_imports);
        let_imports.insert(
            let_imports.end(),
            std::make_move_iterator(unit.systemverilog_lets.begin()),
            std::make_move_iterator(unit.systemverilog_lets.end()));
        unit.systemverilog_lets = std::move(let_imports);
        class_imports.insert(
            class_imports.end(),
            std::make_move_iterator(unit.systemverilog_classes.begin()),
            std::make_move_iterator(unit.systemverilog_classes.end()));
        unit.systemverilog_classes = std::move(class_imports);
        function_imports.insert(
            function_imports.end(),
            std::make_move_iterator(unit.functions.begin()),
            std::make_move_iterator(unit.functions.end()));
        unit.functions = std::move(function_imports);
        task_imports.insert(
            task_imports.end(),
            std::make_move_iterator(unit.tasks.begin()),
            std::make_move_iterator(unit.tasks.end()));
        unit.tasks = std::move(task_imports);
    }



} // namespace fsim::elaboration

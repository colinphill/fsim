// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <sstream>

namespace fsim::elaboration {
using namespace elaboration_detail;

namespace {

void rename_package_expression(
    frontend::Expression& expression,
    const std::unordered_map<std::string, std::string>& names) {
    if (expression.kind == frontend::ExpressionKind::Identifier
        || expression.kind == frontend::ExpressionKind::Call) {
        const auto found = names.find(expression.text);
        if (found != names.end()) {
            expression.text = found->second;
        }
    }
    for (auto& choices :
         expression.aggregate_choice_expressions) {
        for (auto& choice : choices) {
            rename_package_expression(choice, names);
        }
    }
    for (auto& operand : expression.operands) {
        rename_package_expression(operand, names);
    }
}

void rename_package_statements(
    std::vector<frontend::Statement>& statements,
    const std::unordered_map<std::string, std::string>& names) {
    for (auto& statement : statements) {
        rename_package_expression(statement.target, names);
        rename_package_expression(statement.value, names);
        rename_package_expression(statement.condition, names);
        rename_package_expression(statement.loop_initial, names);
        rename_package_expression(statement.loop_limit, names);
        for (auto& element : statement.vhdl_waveform) {
            rename_package_expression(element.value, names);
        }
        for (auto& argument : statement.task_arguments) {
            rename_package_expression(argument, names);
        }
        for (auto& association :
             statement.procedure_arguments) {
            rename_package_expression(association.value, names);
        }
        for (auto& output : statement.output_values) {
            rename_package_expression(output.value, names);
        }
        if (const auto found =
                names.find(statement.procedure_name);
            found != names.end()) {
            statement.procedure_name = found->second;
        }
        rename_package_statements(statement.statements, names);
        rename_package_statements(
            statement.else_statements, names);
        for (auto& alternative :
             statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                rename_package_expression(choice, names);
            }
            rename_package_statements(
                alternative.statements, names);
        }
        for (auto& declaration : statement.declarations) {
            if (declaration.initializer) {
                rename_package_expression(
                    *declaration.initializer, names);
            }
        }
    }
}

std::string package_binding_identity(
    const PackageBinding& binding) {
    std::ostringstream output;
    output << "vhdl-package-v1;template="
           << binding.template_name;
    const auto& values =
        binding.identity_values.empty()
            ? binding.values
            : binding.identity_values;
    for (const auto& [name, value] : values) {
        output << ';' << name << '=' << value;
    }
    return output.str();
}

ConstantDomainEnvironment package_domains(
    const DesignUnit& unit,
    const ConstantEnvironment& environment) {
    ConstantDomainEnvironment result;
    for (const auto& parameter : unit.parameters) {
        if (parameter.kind != frontend::ParameterKind::Value
            || !environment.contains(parameter.name)) {
            continue;
        }
        result.insert_or_assign(
            parameter.name,
            ConstantTypeInfo{
                parameter.type.domain,
                !parameter.type.enumeration_literals.empty(),
                parameter.type.nominal_type});
    }
    return result;
}

void substitute_package_bound_items(
    DesignUnit& unit,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& parameter : unit.parameters) {
        substitute_parameters(
            parameter.type,
            environment,
            domains,
            diagnostics,
            frontend::Language::Vhdl2008);
        substitute_parameters(
            parameter.default_value,
            environment,
            domains,
            frontend::Language::Vhdl2008);
    }
    for (auto& alias : unit.type_aliases) {
        substitute_parameters(
            alias.type,
            environment,
            domains,
            diagnostics,
            frontend::Language::Vhdl2008);
    }
    for (auto& signal : unit.signals) {
        substitute_parameters(
            signal.type,
            environment,
            domains,
            diagnostics,
            frontend::Language::Vhdl2008);
    }
    for (auto& function : unit.functions) {
        substitute_parameters(
            function.return_type,
            environment,
            domains,
            diagnostics,
            frontend::Language::Vhdl2008);
        for (auto& argument : function.arguments) {
            substitute_parameters(
                argument.type,
                environment,
                domains,
                diagnostics,
                frontend::Language::Vhdl2008);
        }
        for (auto& variable : function.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                frontend::Language::Vhdl2008);
        }
        substitute_parameters(
            function.statements,
            environment,
            domains,
            diagnostics,
            frontend::Language::Vhdl2008);
    }
    for (auto& procedure : unit.procedures) {
        for (auto& argument : procedure.arguments) {
            substitute_parameters(
                argument.type,
                environment,
                domains,
                diagnostics,
                frontend::Language::Vhdl2008);
            if (argument.default_value) {
                substitute_parameters(
                    *argument.default_value,
                    environment,
                    domains,
                    frontend::Language::Vhdl2008);
            }
        }
        for (auto& variable : procedure.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                frontend::Language::Vhdl2008);
        }
        substitute_parameters(
            procedure.statements,
            environment,
            domains,
            diagnostics,
            frontend::Language::Vhdl2008);
    }
    substitute_parameters(
        unit.concurrent_statements,
        environment,
        domains,
        diagnostics,
        frontend::Language::Vhdl2008);
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                frontend::Language::Vhdl2008);
        }
        substitute_parameters(
            process.statements,
            environment,
            domains,
            diagnostics,
            frontend::Language::Vhdl2008);
    }
    substitute_parameters(
        unit.instances,
        environment,
        domains,
        frontend::Language::Vhdl2008);
}

std::optional<std::string> canonical_package_template(
    const DesignUnit& owner,
    const std::string_view selected_name) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= selected_name.size()) {
        const auto separator = selected_name.find('.', begin);
        parts.emplace_back(
            selected_name.substr(
                begin,
                separator == std::string_view::npos
                    ? selected_name.size() - begin
                    : separator - begin));
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1;
    }
    if (parts.empty() || parts.size() > 2) {
        return std::nullopt;
    }
    const auto owner_library =
        owner.library.empty()
            ? std::string{"work"}
            : owner.library;
    const auto library =
        parts.size() == 1 || parts.front() == "work"
            ? owner_library
            : parts.front();
    return library + "." + parts.back();
}

} // namespace

void HierarchyBuilder::materialize_vhdl_package_binding(
    DesignUnit& unit,
    const std::string_view prefix,
    const PackageBinding& binding) {
    const auto qualified =
        [&](const std::string_view name) {
          return std::string{prefix} + "." + std::string{name};
        };
    std::unordered_map<std::string, std::string> subprogram_names;
    for (const auto& function : binding.unit.functions) {
        subprogram_names.emplace(
            function.name, qualified(function.name));
    }
    for (const auto& procedure : binding.unit.procedures) {
        subprogram_names.emplace(
            procedure.name, qualified(procedure.name));
    }

    for (const auto& parameter : binding.unit.parameters) {
        if (!parameter.local
            || parameter.kind
                != frontend::ParameterKind::Value) {
            continue;
        }
        const auto name = qualified(parameter.name);
        if (std::ranges::any_of(
                unit.parameters,
                [&](const auto& existing) {
                    return existing.name == name;
                })) {
            continue;
        }
        auto imported = parameter;
        imported.name = name;
        if (const auto value =
                binding.environment.find(parameter.name);
            value != binding.environment.end()) {
            imported.default_value = constant_expression(
                value->second,
                parameter.span,
                parameter.type.domain,
                frontend::Language::Vhdl2008,
                !parameter.type.enumeration_literals.empty());
        }
        unit.parameters.push_back(std::move(imported));
    }
    for (const auto& alias : binding.unit.type_aliases) {
        const auto name = qualified(alias.name);
        if (std::ranges::any_of(
                unit.type_aliases,
                [&](const auto& existing) {
                    return existing.name == name;
                })) {
            continue;
        }
        auto imported = alias;
        imported.name = name;
        unit.type_aliases.push_back(std::move(imported));
    }
    for (const auto& function : binding.unit.functions) {
        const auto name = qualified(function.name);
        if (std::ranges::any_of(
                unit.functions,
                [&](const auto& existing) {
                    return existing.name == name
                        && existing.span.source_name
                            == function.span.source_name
                        && existing.span.begin.offset
                            == function.span.begin.offset;
                })) {
            continue;
        }
        auto imported = function;
        imported.name = name;
        imported.visibility_owner = std::string{prefix};
        for (auto& variable : imported.variables) {
            if (variable.initializer) {
                rename_package_expression(
                    *variable.initializer, subprogram_names);
            }
        }
        rename_package_statements(
            imported.statements, subprogram_names);
        unit.functions.push_back(std::move(imported));
    }
    for (const auto& procedure : binding.unit.procedures) {
        const auto name = qualified(procedure.name);
        if (std::ranges::any_of(
                unit.procedures,
                [&](const auto& existing) {
                    return existing.name == name
                        && existing.span.source_name
                            == procedure.span.source_name
                        && existing.span.begin.offset
                            == procedure.span.begin.offset;
                })) {
            continue;
        }
        auto imported = procedure;
        imported.name = name;
        imported.visibility_owner = std::string{prefix};
        for (auto& variable : imported.variables) {
            if (variable.initializer) {
                rename_package_expression(
                    *variable.initializer, subprogram_names);
            }
        }
        rename_package_statements(
            imported.statements, subprogram_names);
        unit.procedures.push_back(std::move(imported));
    }

    const auto append_dependency =
        [&](const std::string& dependency) {
          if (!dependency.empty()
              && std::ranges::find(
                     unit.source_dependencies, dependency)
                  == unit.source_dependencies.end()) {
              unit.source_dependencies.push_back(dependency);
          }
        };
    append_dependency(
        std::string{
            frontend::physical_source(binding.unit.span)});
    for (const auto& dependency :
         binding.unit.source_dependencies) {
        append_dependency(dependency);
    }
}

void HierarchyBuilder::bind_vhdl_interface_packages(
    DesignUnit& unit,
    std::vector<frontend::ParameterOverride>& overrides,
    const PackageEnvironment& parent_packages,
    const ConstantEnvironment& parent_environment,
    const ConstantDomainEnvironment& parent_domains,
    const NamedTypeEnvironment& parent_types,
    const std::vector<frontend::FunctionDeclaration>& parent_functions,
    const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
    const frontend::Language association_language,
    PackageEnvironment& bindings,
    std::vector<std::pair<std::string, std::string>>& identity_values) {
    std::vector<const frontend::ParameterDeclaration*> formals;
    for (const auto& parameter : unit.parameters) {
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
                        unit.language,
                        association_language);
                });
            if (found == formals.end()) {
                report(
                    "FSIM-ELAB-GENERIC-001",
                    "unknown generic actual '" + *actual.name + "'",
                    actual.span);
                continue;
            }
            index = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        } else {
            if (saw_named) {
                report(
                    "FSIM-ELAB-GENERIC-003",
                    "a positional generic actual cannot follow a named "
                    "actual",
                    actual.span);
            }
            if (next_positional >= formals.size()) {
                report(
                    "FSIM-ELAB-GENERIC-001",
                    "too many positional generic actuals",
                    actual.span);
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index]) {
            report(
                "FSIM-ELAB-GENERIC-002",
                "duplicate generic actual for '"
                    + formals[*index]->name + "'",
                actual.span);
        } else {
            actuals[*index] = actual;
        }
    }

    std::vector<frontend::ParameterOverride> remaining;
    for (std::size_t index = 0; index < formals.size(); ++index) {
        if (formals[index]->kind
                == frontend::ParameterKind::Package
            || !actuals[index]) {
            continue;
        }
        auto actual = *actuals[index];
        actual.name = formals[index]->name;
        remaining.push_back(std::move(actual));
    }

    auto compatibility_unit = unit;
    std::erase_if(
        compatibility_unit.parameters,
        [](const auto& parameter) {
            return parameter.kind
                == frontend::ParameterKind::Package;
        });
    compatibility_unit.ports.clear();
    compatibility_unit.signals.clear();
    compatibility_unit.concurrent_statements.clear();
    compatibility_unit.processes.clear();
    compatibility_unit.instances.clear();
    compatibility_unit.generate_regions.clear();
    compatibility_unit.package_instances.clear();
    auto compatibility_types =
        specialize_vhdl_interface_types(
            compatibility_unit,
            remaining,
            parent_environment,
            parent_domains,
            parent_types,
            parent_functions,
            parent_procedures,
            association_language,
            diagnostics_);
    auto compatibility =
        specialize_unit(
            compatibility_types.unit,
            compatibility_types.value_overrides,
            parent_environment,
            association_language,
            diagnostics_);
    const auto compatibility_named_types =
        local_vhdl_type_environment(compatibility.unit);
    std::unordered_map<std::string, std::string>
        compatibility_identities;
    for (const auto& [name, identity] :
         compatibility_types.values) {
        compatibility_identities.insert_or_assign(
            name, identity);
    }
    const auto& compatibility_value_identities =
        compatibility.identity_values.empty()
            ? compatibility.values
            : compatibility.identity_values;
    for (const auto& [name, identity] :
         compatibility_value_identities) {
        compatibility_identities.insert_or_assign(
            name, identity);
    }

    for (std::size_t index = 0; index < formals.size(); ++index) {
        const auto& formal = *formals[index];
        if (formal.kind != frontend::ParameterKind::Package) {
            continue;
        }
        if (!formal.package_profile) {
            report(
                "FSIM-ELAB-VHPKG-001",
                "interface package generic '" + formal.name
                    + "' has no retained package profile",
                formal.span);
            continue;
        }
        if (association_language
            != frontend::Language::Vhdl2008) {
            report(
                "FSIM-ELAB-VHPKG-002",
                "interface package generic '" + formal.name
                    + "' requires a same-language VHDL package "
                    "instance actual",
                actuals[index] ? actuals[index]->span : formal.span);
            continue;
        }
        if (!actuals[index]) {
            report(
                "FSIM-ELAB-VHPKG-004",
                "interface package generic '" + formal.name
                    + "' requires a package instance actual",
                formal.span);
            continue;
        }
        const auto& actual = *actuals[index];
        if (actual.default_box || actual.type_value
            || actual.value.kind
                != frontend::ExpressionKind::Identifier
            || !actual.value.operands.empty()) {
            report(
                "FSIM-ELAB-VHPKG-003",
                "actual for interface package generic '"
                    + formal.name
                    + "' must be a visible package instance name",
                actual.span);
            continue;
        }
        const auto selected =
            parent_packages.find(actual.value.text);
        if (selected == parent_packages.end()) {
            const auto actual_parts =
                canonical_package_template(
                    unit, actual.value.text);
            const bool unspecialized_template =
                actual_parts
                && std::ranges::any_of(
                    parsed_.units,
                    [&](const DesignUnit& candidate) {
                        const auto candidate_library =
                            candidate.library.empty()
                                ? std::string{"work"}
                                : candidate.library;
                        return candidate.kind
                                == frontend::UnitKind::VhdlPackage
                            && candidate.primary_name.empty()
                            && candidate_library + "."
                                   + candidate.name
                                == *actual_parts
                            && std::ranges::any_of(
                                candidate.parameters,
                                [](const auto& parameter) {
                                    return !parameter.local;
                                });
                    });
            const bool wrong_kind =
                parent_types.contains(actual.value.text)
                || parent_environment.contains(actual.value.text)
                || std::ranges::any_of(
                    parent_functions,
                    [&](const auto& candidate) {
                        return candidate.name == actual.value.text;
                    })
                || std::ranges::any_of(
                    parent_procedures,
                    [&](const auto& candidate) {
                        return candidate.name == actual.value.text;
                    });
            const bool scoped =
                actual.value.text.find('.') != std::string::npos;
            report(
                unspecialized_template
                    ? "FSIM-ELAB-VHPKG-014"
                    : wrong_kind
                    ? "FSIM-ELAB-VHPKG-006"
                    : scoped
                        ? "FSIM-ELAB-VHPKG-011"
                        : "FSIM-ELAB-VHPKG-005",
                unspecialized_template
                    ? "generic package template '"
                        + actual.value.text
                        + "' is unspecialized and cannot be used as an "
                        "interface package actual"
                    : wrong_kind
                    ? "generic actual '" + actual.value.text
                        + "' is not a package instance"
                    : scoped
                        ? "generated or scoped package actual '"
                            + actual.value.text
                            + "' is outside the bounded interface "
                            "package subset"
                        : "package instance actual '"
                            + actual.value.text
                            + "' is not directly visible",
                actual.span);
            continue;
        }
        const auto& profile = *formal.package_profile;
        const auto required_template =
            canonical_package_template(
                unit, profile.template_name);
        if (!required_template) {
            report(
                "FSIM-ELAB-VHPKG-009",
                "interface package template name '"
                    + profile.template_name
                    + "' is outside the bounded direct-library form",
                profile.span);
            continue;
        }
        if (selected->second.template_name
            != *required_template) {
            report(
                "FSIM-ELAB-VHPKG-007",
                "package instance actual '" + actual.value.text
                    + "' specializes '" + selected->second.template_name
                    + "' rather than required template '"
                    + *required_template + "'",
                actual.span);
            continue;
        }
        if (!profile.generic_map_box) {
            const auto separator =
                required_template->find('.');
            const auto required_library =
                required_template->substr(0, separator);
            const auto required_name =
                required_template->substr(separator + 1);
            const auto package = std::ranges::find_if(
                parsed_.units,
                [&](const DesignUnit& candidate) {
                    const auto candidate_library =
                        candidate.library.empty()
                            ? std::string{"work"}
                            : candidate.library;
                    return candidate.kind
                            == frontend::UnitKind::VhdlPackage
                        && candidate.primary_name.empty()
                        && candidate.name == required_name
                        && candidate_library == required_library;
                });
            if (package == parsed_.units.end()) {
                report(
                    "FSIM-ELAB-VHPKG-009",
                    "generic package template '"
                        + *required_template + "' was not found",
                    profile.span);
                continue;
            }
            auto expected_actuals = profile.generic_map;
            std::erase_if(
                expected_actuals,
                [](const auto& expected) {
                    return expected.default_box;
                });
            std::vector<const DesignUnit*> import_stack;
            const auto diagnostic_count = diagnostics_.size();
            auto expected = specialize_vhdl_package(
                *package,
                import_stack,
                profile.span,
                expected_actuals,
                compatibility.environment,
                package_domains(
                    compatibility.unit,
                    compatibility.environment),
                compatibility_named_types,
                compatibility.unit.functions,
                compatibility.unit.procedures);
            if (!expected
                || diagnostics_.size() != diagnostic_count) {
                continue;
            }
            std::vector<const frontend::ParameterDeclaration*>
                template_formals;
            for (const auto& parameter : package->parameters) {
                if (!parameter.local) {
                    template_formals.push_back(&parameter);
                }
            }
            std::size_t positional = 0;
            for (const auto& association :
                 profile.generic_map) {
                const frontend::ParameterDeclaration*
                    template_formal = nullptr;
                if (association.name) {
                    const auto found = std::ranges::find_if(
                        template_formals,
                        [&](const auto* candidate) {
                            return candidate->name
                                == *association.name;
                        });
                    if (found != template_formals.end()) {
                        template_formal = *found;
                    }
                } else if (positional < template_formals.size()) {
                    template_formal =
                        template_formals[positional++];
                }
                if (template_formal == nullptr
                    || association.default_box
                    || association.type_value
                    || association.value.kind
                        != frontend::ExpressionKind::Identifier
                    || !association.value.operands.empty()) {
                    continue;
                }
                const auto forwarded =
                    compatibility_identities.find(
                        association.value.text);
                if (forwarded
                    == compatibility_identities.end()) {
                    continue;
                }
                const auto replace_identity =
                    [&](auto& values) {
                      const auto found = std::ranges::find_if(
                          values,
                          [&](const auto& value) {
                              return value.first
                                  == template_formal->name;
                          });
                      if (found != values.end()) {
                          found->second = forwarded->second;
                      }
                    };
                replace_identity(expected->values);
                replace_identity(expected->identity_values);
            }
            const PackageBinding expected_binding{
                *required_template,
                expected->unit,
                expected->environment,
                expected->values,
                expected->identity_values};
            if (package_binding_identity(expected_binding)
                != package_binding_identity(selected->second)) {
                report(
                    "FSIM-ELAB-VHPKG-008",
                    "package instance actual '" + actual.value.text
                        + "' does not conform to the interface package "
                        "generic map for '" + formal.name + "'",
                    actual.span);
                continue;
            }
        }
        const auto formal_name = formal.name;
        bindings.insert_or_assign(
            formal_name, selected->second);
        materialize_vhdl_package_binding(
            unit, formal_name, selected->second);
        identity_values.emplace_back(
            formal_name,
            package_binding_identity(selected->second));
    }
    std::erase_if(
        unit.parameters,
        [](const auto& parameter) {
            return parameter.kind
                == frontend::ParameterKind::Package;
        });
    overrides = std::move(remaining);
}

void HierarchyBuilder::instantiate_vhdl_local_packages(
    SpecializedUnit& specialized,
    const PackageEnvironment& inherited_packages) {
    if (specialized.unit.language
        != frontend::Language::Vhdl2008) {
        return;
    }
    specialized.packages.insert(
        inherited_packages.begin(), inherited_packages.end());
    auto domains =
        package_domains(
            specialized.unit, specialized.environment);
    for (const auto& instance :
         specialized.unit.package_instances) {
        const auto parts =
            selected_name_parts(instance.template_name);
        if (parts.empty() || parts.size() > 2) {
            report(
                "FSIM-ELAB-VHPKG-009",
                "generic package template name '"
                    + instance.template_name
                    + "' is outside the bounded direct-library form",
                instance.span);
            continue;
        }
        const auto owner_library =
            specialized.unit.library.empty()
                ? std::string{"work"}
                : specialized.unit.library;
        const auto template_library =
            parts.size() == 1 || parts.front() == "work"
                ? owner_library
                : parts.front();
        const auto template_name = parts.back();
        std::vector<const DesignUnit*> matches;
        for (const auto& candidate : parsed_.units) {
            const auto candidate_library =
                candidate.library.empty()
                    ? std::string{"work"}
                    : candidate.library;
            if (candidate.kind
                    == frontend::UnitKind::VhdlPackage
                && candidate.primary_name.empty()
                && candidate.name == template_name
                && candidate_library == template_library) {
                matches.push_back(&candidate);
            }
        }
        if (matches.empty()) {
            report(
                "FSIM-ELAB-VHPKG-009",
                "generic package template '"
                    + template_library + "." + template_name
                    + "' was not found",
                instance.span);
            continue;
        }
        if (matches.size() != 1) {
            report(
                "FSIM-ELAB-VHPKG-012",
                "generic package template '"
                    + template_library + "." + template_name
                    + "' is ambiguous",
                instance.span);
            continue;
        }
        if (std::ranges::none_of(
                matches.front()->parameters,
                [](const auto& parameter) {
                    return !parameter.local;
                })) {
            report(
                "FSIM-ELAB-VHPKG-013",
                "package '" + template_library + "."
                    + template_name
                    + "' is not a generic package template",
                instance.span);
            continue;
        }
        auto actuals = instance.generic_map;
        std::erase_if(
            actuals,
            [](const auto& actual) {
                return actual.default_box;
            });
        std::vector<const DesignUnit*> import_stack;
        const auto diagnostic_count = diagnostics_.size();
        auto selected = specialize_vhdl_package(
            *matches.front(),
            import_stack,
            instance.span,
            actuals,
            specialized.environment,
            domains,
            local_vhdl_type_environment(specialized.unit),
            specialized.unit.functions,
            specialized.unit.procedures);
        if (!selected
            || diagnostics_.size() != diagnostic_count) {
            continue;
        }
        if (std::ranges::any_of(
                selected->unit.functions,
                [](const auto& function) {
                    return !function.defined;
                })
            || std::ranges::any_of(
                selected->unit.procedures,
                [](const auto& procedure) {
                    return !procedure.defined;
                })) {
            report(
                "FSIM-ELAB-VHPKG-010",
                "package instance '" + instance.name
                    + "' has an incomplete required subprogram body",
                instance.span);
            continue;
        }
        PackageBinding binding{
            template_library + "." + template_name,
            std::move(selected->unit),
            std::move(selected->environment),
            std::move(selected->values),
            std::move(selected->identity_values)};
        if (specialized.packages.contains(instance.name)) {
            report(
                "FSIM-ELAB-VHPKG-012",
                "package instance name '" + instance.name
                    + "' is ambiguous in this declarative region",
                instance.span);
            continue;
        }
        materialize_vhdl_package_binding(
            specialized.unit, instance.name, binding);
        for (const auto& parameter :
             binding.unit.parameters) {
            if (!parameter.local
                || parameter.kind
                    != frontend::ParameterKind::Value) {
                continue;
            }
            const auto value =
                binding.environment.find(parameter.name);
            if (value == binding.environment.end()) {
                continue;
            }
            const auto qualified_name =
                instance.name + "." + parameter.name;
            specialized.environment.insert_or_assign(
                qualified_name, value->second);
            domains.insert_or_assign(
                qualified_name,
                ConstantTypeInfo{
                    parameter.type.domain,
                    !parameter.type
                         .enumeration_literals.empty(),
                    parameter.type.nominal_type});
        }
        const auto identity =
            package_binding_identity(binding);
        specialized.values.emplace_back(
            instance.name, identity);
        specialized.identity_values.emplace_back(
            instance.name, identity);
        specialized.packages.emplace(
            instance.name, std::move(binding));
    }
    specialized.unit.package_instances.clear();
    resolve_named_types(
        specialized.unit, {}, true, false);
    substitute_package_bound_items(
        specialized.unit,
        specialized.environment,
        domains,
        diagnostics_);
}

} // namespace fsim::elaboration

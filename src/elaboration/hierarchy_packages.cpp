// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

HierarchyBuilder::HierarchyBuilder(
        const frontend::ParsedDesign& parsed,
        ElaboratedDesign& design,
        std::vector<Diagnostic>& diagnostics,
        const std::span<const Binding> bindings,
        const std::span<const SystemCInstanceDescription>
            systemc_instances,
        SystemCFactoryProvider* systemc_provider)
        : parsed_(parsed),
          design_(design),
          diagnostics_(diagnostics),
          systemc_provider_(systemc_provider) {
        for (const auto& binding : bindings) {
            if (!bindings_.emplace(binding.instance, &binding).second) {
                report(
                    "FSIM-ELAB-BIND-010",
                    "duplicate binding for instance '" + binding.instance + "'",
                    {});
            }
        }
        for (const auto& instance : systemc_instances) {
            if (!systemc_instances_
                     .emplace(instance.path, &instance)
                     .second) {
                report(
                    "FSIM-ELAB-BIND-032",
                    "duplicate constructed SystemC instance path '"
                        + instance.path + "'",
                    {});
            }
        }
    }



    void HierarchyBuilder::build(const DesignUnit& root) {
        const DesignUnit* selected = &root;
        std::optional<DesignUnit> configured_root;
        std::string configuration_identity;
        if (root.kind
            == frontend::UnitKind::VhdlConfiguration) {
            selected =
                select_vhdl_configuration_root(root);
            if (selected == nullptr) {
                finish();
                return;
            }
            active_vhdl_configuration_ = &root;
            vhdl_configurations_by_path_[design_.top_] = &root;
            configured_root = *selected;
            const auto configuration_source = std::string{
                frontend::physical_source(root.span)};
            if (!configuration_source.empty()
                && std::ranges::find(
                       configured_root->source_dependencies,
                       configuration_source)
                    == configured_root
                           ->source_dependencies.end()) {
                configured_root->source_dependencies.push_back(
                    configuration_source);
            }
            selected = &*configured_root;
            configuration_identity =
                vhdl_configuration_identity(root);
        }
        auto specialized = specialize_selected_unit(
            *selected, {}, {}, {}, {}, {}, {}, {},
            selected->language);
        if (!configuration_identity.empty()) {
            specialized.identity_values.emplace_back(
                "__configuration",
                std::move(configuration_identity));
        }
        instantiate(
            specialized.unit,
            design_.top_,
            {},
            {},
            std::move(specialized.environment),
            std::move(specialized.values),
            std::move(specialized.identity_values),
            std::move(specialized.packages));
        active_vhdl_configuration_ = nullptr;
        vhdl_configurations_by_path_.clear();
        finish();
    }



    void HierarchyBuilder::build(const SystemCInstanceDescription& root) {
        instantiate_systemc(root, root.path, {}, {});
        finish();
    }



    std::vector<std::string> HierarchyBuilder::selected_name_parts(
        const std::string_view name) {
        std::vector<std::string> result;
        std::size_t begin = 0;
        while (begin <= name.size()) {
            const auto separator = name.find('.', begin);
            result.emplace_back(
                name.substr(
                    begin,
                    separator == std::string_view::npos
                        ? name.size() - begin
                        : separator - begin));
            if (separator == std::string_view::npos) {
                break;
            }
            begin = separator + 1;
        }
        return result;
    }



    void HierarchyBuilder::expand_vhdl_context_references(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem> context,
        std::vector<frontend::VhdlContextItem>& expanded,
        std::vector<const DesignUnit*>& context_stack,
        const std::string_view visibility_library) {
        for (const auto& item : context) {
            if (item.kind
                != frontend::VhdlContextItemKind::
                    ContextReference) {
                auto visible_item = item;
                if (visible_item.kind
                    == frontend::VhdlContextItemKind::UseClause) {
                    for (auto& selected_name :
                         visible_item.selected_names) {
                        const auto parts =
                            selected_name_parts(selected_name);
                        if (!parts.empty()
                            && parts.front() == "work") {
                            selected_name =
                                std::string{visibility_library}
                                + selected_name.substr(4);
                        }
                    }
                }
                expanded.push_back(std::move(visible_item));
                continue;
            }
            for (const auto& selected_name : item.selected_names) {
                const auto parts =
                    selected_name_parts(selected_name);
                if (parts.size() != 2) {
                    report(
                        "FSIM-ELAB-CTX-001",
                        "bounded context references require "
                        "library.context",
                        item.span);
                    continue;
                }
                const auto requested_library =
                    parts[0] == "work"
                        ? std::string{visibility_library}
                        : parts[0];
                const auto context_unit = std::find_if(
                    parsed_.units.begin(),
                    parsed_.units.end(),
                    [&](const DesignUnit& candidate) {
                        const auto candidate_library =
                            candidate.library.empty()
                                ? std::string_view{"work"}
                                : std::string_view{
                                      candidate.library};
                        return candidate.kind
                                == frontend::UnitKind::VhdlContext
                            && candidate.name == parts[1]
                            && candidate_library
                                == requested_library;
                    });
                if (context_unit == parsed_.units.end()) {
                    if (parts[0] == "ieee"
                        || parts[0] == "std") {
                        continue;
                    }
                    report(
                        "FSIM-ELAB-CTX-002",
                        "VHDL context '" + parts[0] + "."
                            + parts[1] + "' was not found",
                        item.span);
                    continue;
                }
                const auto context_owner =
                    (context_unit->library.empty()
                         ? std::string{"work"}
                         : context_unit->library)
                    + "." + context_unit->name;
                if (std::find(
                        context_stack.begin(),
                        context_stack.end(),
                        &*context_unit)
                    != context_stack.end()) {
                    std::string cycle;
                    for (const auto* referenced : context_stack) {
                        if (!cycle.empty()) {
                            cycle += " -> ";
                        }
                        cycle +=
                            (referenced->library.empty()
                                 ? std::string{"work"}
                                 : referenced->library)
                            + "." + referenced->name;
                    }
                    cycle += " -> " + context_owner;
                    report(
                        "FSIM-ELAB-CTX-003",
                        "cyclic VHDL context visibility: " + cycle,
                        item.span);
                    continue;
                }
                const auto context_source = std::string{
                    frontend::physical_source(context_unit->span)};
                if (std::find(
                        unit.source_dependencies.begin(),
                        unit.source_dependencies.end(),
                        context_source)
                    == unit.source_dependencies.end()) {
                    unit.source_dependencies.push_back(
                        context_source);
                }
                context_stack.push_back(&*context_unit);
                const auto nested_library =
                    context_unit->library.empty()
                        ? std::string{"work"}
                        : context_unit->library;
                expand_vhdl_context_references(
                    unit,
                    context_unit->vhdl_context,
                    expanded,
                    context_stack,
                    nested_library);
                context_stack.pop_back();
            }
        }
    }



    void HierarchyBuilder::import_vhdl_package_constants(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem>
            context,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& imported_types) {
        std::vector<frontend::ParameterDeclaration> imports;
        std::vector<frontend::FunctionDeclaration>
            function_imports;
        std::vector<frontend::ProcedureDeclaration>
            procedure_imports;
        std::vector<frontend::GenericFunctionTemplate>
            generic_function_imports;
        std::vector<frontend::GenericProcedureTemplate>
            generic_procedure_imports;
        std::vector<frontend::VhdlComponentDeclaration>
            component_imports;
        std::unordered_map<std::string, std::string> bare_owners;
        std::unordered_set<std::string> dependencies;
        const auto owner_library =
            unit.library.empty()
                ? std::string{"work"}
                : unit.library;
        for (const auto& item : context) {
            if (item.kind
                != frontend::VhdlContextItemKind::UseClause) {
                continue;
            }
            for (const auto& selected_name : item.selected_names) {
                const auto parts =
                    selected_name_parts(selected_name);
                if (parts.size() != 3) {
                    report(
                        "FSIM-ELAB-PKG-001",
                        "bounded package imports require "
                        "library.package.all or "
                        "library.package.constant",
                        item.span);
                    continue;
                }
                const auto requested_library =
                    parts[0] == "work"
                        ? owner_library
                        : parts[0];
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
                            && candidate.name == parts[1]
                            && candidate_library
                                == requested_library;
                    });
                if (package == parsed_.units.end()) {
                    if (parts[0] == "ieee"
                        || parts[0] == "std") {
                        continue;
                    }
                    report(
                        "FSIM-ELAB-PKG-002",
                        "VHDL package '" + parts[0] + "."
                            + parts[1] + "' was not found",
                        item.span);
                    continue;
                }
                const auto package_owner =
                    (package->library.empty()
                         ? std::string{"work"}
                         : package->library)
                    + "." + package->name;
                auto specialized_package =
                    specialize_vhdl_package(
                        *package, import_stack, item.span);
                if (!specialized_package) {
                    continue;
                }
                auto& specialized = *specialized_package;
                const bool import_all = parts[2] == "all";
                bool found_selected = import_all;
                for (const auto& declaration :
                     package->parameters) {
                    if (!import_all
                        && declaration.name != parts[2]) {
                        continue;
                    }
                    found_selected = true;
                    const auto value =
                        specialized.environment.find(
                            declaration.name);
                    if (value
                        == specialized.environment.end()) {
                        continue;
                    }
                    const auto specialized_declaration =
                        std::find_if(
                            specialized.unit.parameters.begin(),
                            specialized.unit.parameters.end(),
                            [&](const auto& candidate) {
                                return candidate.name
                                        == declaration.name
                                    && candidate.span.source_name
                                        == declaration.span.source_name
                                    && candidate.span.begin.offset
                                        == declaration.span.begin.offset;
                            });
                    const auto& imported_type =
                        specialized_declaration
                                == specialized.unit.parameters.end()
                            ? declaration.type
                            : specialized_declaration->type;
                    const auto add_alias =
                        [&](std::string alias) {
                          const auto [owner, inserted] =
                              bare_owners.emplace(
                                  alias, package_owner);
                          if (!inserted
                              && owner->second != package_owner) {
                              report(
                                  "FSIM-ELAB-PKG-004",
                                  "VHDL package constant '"
                                      + alias
                                      + "' is directly visible "
                                      "from multiple packages",
                                  item.span);
                              return;
                          }
                          if (std::any_of(
                                  imports.begin(),
                                  imports.end(),
                                  [&](const auto& existing) {
                                      return existing.name
                                          == alias;
                                  })) {
                              return;
                          }
                          imports.push_back({
                              std::move(alias),
                              imported_type,
                              constant_expression(
                                  value->second,
                                  declaration.span,
                                  imported_type.domain,
                                  frontend::Language::Vhdl2008,
                                  !imported_type
                                       .enumeration_literals.empty()),
                              true,
                              declaration.span});
                        };
                    add_alias(declaration.name);
                }
                for (const auto& declared_alias :
                     package->type_aliases) {
                    const auto resolved_alias =
                        std::ranges::find_if(
                            specialized.unit.type_aliases,
                            [&](const auto& candidate) {
                              return candidate.name
                                      == declared_alias.name
                                  && candidate.span.source_name
                                      == declared_alias.span.source_name
                                  && candidate.span.begin.offset
                                      == declared_alias.span.begin.offset;
                            });
                    const auto& alias =
                        resolved_alias
                                == specialized.unit.type_aliases.end()
                            ? declared_alias
                            : *resolved_alias;
                    if (!import_all
                        && alias.name != parts[2]) {
                        continue;
                    }
                    found_selected = true;
                    const auto existing =
                        imported_types.find(alias.name);
                    if (existing != imported_types.end()) {
                        if (existing->second.owner
                            != package_owner) {
                            report(
                                "FSIM-ELAB-VHTYPE-003",
                                "VHDL type '" + alias.name
                                    + "' is directly visible from "
                                      "multiple packages",
                                item.span);
                        }
                        continue;
                    }
                    imported_types.emplace(
                        alias.name,
                        NamedTypeBinding{
                            alias.type, package_owner});
                }
                for (const auto& function :
                     specialized.unit.functions) {
                    const bool exported =
                        std::ranges::any_of(
                            package->functions,
                            [&](const auto& declaration) {
                                return declaration.name
                                    == function.name;
                            })
                        || std::ranges::any_of(
                            package->generic_function_instances,
                            [&](const auto& declaration) {
                              return declaration.name
                                  == function.name;
                            });
                    if (!exported || !function.defined
                        || (!import_all
                            && function.name != parts[2])) {
                        continue;
                    }
                    found_selected = true;
                    const auto [owner, inserted] =
                        bare_owners.emplace(
                            function.name, package_owner);
                    if (!inserted
                        && owner->second != package_owner) {
                        report(
                            "FSIM-ELAB-VHFUNC-010",
                            "VHDL function '" + function.name
                                + "' is directly visible from multiple "
                                  "packages",
                            item.span);
                        continue;
                    }
                    if (std::ranges::none_of(
                            function_imports,
                            [&](const auto& existing) {
                                return existing.name
                                    == function.name;
                            })) {
                        function_imports.push_back(function);
                    }
                }
                for (const auto& procedure :
                     specialized.unit.procedures) {
                    const bool exported =
                        std::ranges::any_of(
                            package->procedures,
                            [&](const auto& declaration) {
                                return declaration.name
                                    == procedure.name;
                            })
                        || std::ranges::any_of(
                            package->generic_procedure_instances,
                            [&](const auto& declaration) {
                              return declaration.name
                                  == procedure.name;
                            });
                    if (!exported || !procedure.defined
                        || (!import_all
                            && procedure.name != parts[2])) {
                        continue;
                    }
                    found_selected = true;
                    const auto [owner, inserted] =
                        bare_owners.emplace(
                            procedure.name, package_owner);
                    if (!inserted
                        && owner->second != package_owner) {
                        report(
                            "FSIM-ELAB-VHPROC-013",
                            "VHDL procedure '" + procedure.name
                                + "' is directly visible from multiple "
                                  "packages",
                            item.span);
                        continue;
                    }
                    if (std::ranges::none_of(
                            procedure_imports,
                            [&](const auto& existing) {
                                return existing.name
                                    == procedure.name;
                            })) {
                        procedure_imports.push_back(procedure);
                    }
                }
                for (const auto& generic :
                     specialized.unit
                         .generic_function_templates) {
                    if (!import_all
                        && generic.function.name != parts[2]) {
                        continue;
                    }
                    found_selected = true;
                    const auto [owner, inserted] =
                        bare_owners.emplace(
                            generic.function.name,
                            package_owner);
                    if (!inserted
                        && owner->second != package_owner) {
                        report(
                            "FSIM-ELAB-VHGSUB-002",
                            "generic function template '"
                                + generic.function.name
                                + "' is directly visible from "
                                  "multiple packages",
                            item.span);
                        continue;
                    }
                    if (std::ranges::none_of(
                            generic_function_imports,
                            [&](const auto& existing) {
                              return existing.function.name
                                  == generic.function.name;
                            })) {
                        generic_function_imports.push_back(
                            generic);
                    }
                }
                for (const auto& generic :
                     specialized.unit
                         .generic_procedure_templates) {
                    if (!import_all
                        && generic.procedure.name != parts[2]) {
                        continue;
                    }
                    found_selected = true;
                    const auto [owner, inserted] =
                        bare_owners.emplace(
                            generic.procedure.name,
                            package_owner);
                    if (!inserted
                        && owner->second != package_owner) {
                        report(
                            "FSIM-ELAB-VHGSUB-002",
                            "generic procedure template '"
                                + generic.procedure.name
                                + "' is directly visible from "
                                  "multiple packages",
                            item.span);
                        continue;
                    }
                    if (std::ranges::none_of(
                            generic_procedure_imports,
                            [&](const auto& existing) {
                              return existing.procedure.name
                                  == generic.procedure.name;
                            })) {
                        generic_procedure_imports.push_back(
                            generic);
                    }
                }
                for (const auto& declared_component :
                     package->vhdl_component_declarations) {
                    const auto resolved_component =
                        std::ranges::find_if(
                            specialized.unit
                                .vhdl_component_declarations,
                            [&](const auto& candidate) {
                              return candidate.name
                                      == declared_component.name
                                  && candidate.span.source_name
                                      == declared_component
                                             .span.source_name
                                  && candidate.span.begin.offset
                                      == declared_component
                                             .span.begin.offset;
                            });
                    auto component =
                        resolved_component
                                == specialized.unit
                                       .vhdl_component_declarations.end()
                            ? declared_component
                            : *resolved_component;
                    if (!import_all
                        && component.name != parts[2]) {
                        continue;
                    }
                    found_selected = true;
                    component.region =
                        frontend::VhdlComponentDeclarationRegion::
                            Package;
                    component.owner_library =
                        requested_library;
                    component.owner_name = package->name;
                    if (std::ranges::none_of(
                            component_imports,
                            [&](const auto& existing) {
                              return existing.owner_library
                                          == component.owner_library
                                  && existing.owner_name
                                          == component.owner_name
                                  && existing.name == component.name
                                  && existing.span.source_name
                                          == component.span.source_name
                                  && existing.span.begin.offset
                                          == component.span.begin.offset;
                            })) {
                        component_imports.push_back(
                            std::move(component));
                    }
                }
                if (!found_selected) {
                    report(
                        "FSIM-ELAB-PKG-003",
                        "VHDL package '" + parts[0] + "."
                            + parts[1]
                            + "' has no exported item '" + parts[2]
                            + "'",
                        item.span);
                }
                const auto package_source = std::string{
                    frontend::physical_source(package->span)};
                if (dependencies.insert(package_source).second) {
                    unit.source_dependencies.push_back(
                        package_source);
                }
                for (const auto& dependency :
                     specialized.unit.source_dependencies) {
                    if (dependencies.insert(dependency).second) {
                        unit.source_dependencies.push_back(
                            dependency);
                    }
                }
            }
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
        function_imports.insert(
            function_imports.end(),
            std::make_move_iterator(unit.functions.begin()),
            std::make_move_iterator(unit.functions.end()));
        unit.functions = std::move(function_imports);
        procedure_imports.insert(
            procedure_imports.end(),
            std::make_move_iterator(unit.procedures.begin()),
            std::make_move_iterator(unit.procedures.end()));
        unit.procedures = std::move(procedure_imports);
        generic_function_imports.insert(
            generic_function_imports.end(),
            std::make_move_iterator(
                unit.generic_function_templates.begin()),
            std::make_move_iterator(
                unit.generic_function_templates.end()));
        unit.generic_function_templates =
            std::move(generic_function_imports);
        generic_procedure_imports.insert(
            generic_procedure_imports.end(),
            std::make_move_iterator(
                unit.generic_procedure_templates.begin()),
            std::make_move_iterator(
                unit.generic_procedure_templates.end()));
        unit.generic_procedure_templates =
            std::move(generic_procedure_imports);
        component_imports.insert(
            component_imports.end(),
            std::make_move_iterator(
                unit.vhdl_component_declarations.begin()),
            std::make_move_iterator(
                unit.vhdl_component_declarations.end()));
        unit.vhdl_component_declarations =
            std::move(component_imports);
    }



    std::optional<SpecializedUnit> HierarchyBuilder::specialize_vhdl_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span,
        const std::vector<frontend::ParameterOverride>& overrides,
        const ConstantEnvironment& parent_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions,
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures) {
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
                cycle +=
                    (imported->library.empty()
                         ? std::string{"work"}
                         : imported->library)
                    + "." + imported->name;
            }
            cycle += " -> "
                + (package.library.empty()
                       ? std::string{"work"}
                       : package.library)
                + "." + package.name;
            report(
                "FSIM-ELAB-PKG-007",
                "cyclic VHDL package visibility: " + cycle,
                reference_span);
            return std::nullopt;
        }
        import_stack.push_back(&package);
        auto effective_package = package;
        const auto package_library =
            package.library.empty()
                ? std::string_view{"work"}
                : std::string_view{package.library};
        const auto package_body = std::ranges::find_if(
            parsed_.units,
            [&](const DesignUnit& candidate) {
                const auto candidate_library =
                    candidate.library.empty()
                        ? std::string_view{"work"}
                        : std::string_view{candidate.library};
                return candidate.kind
                        == frontend::UnitKind::VhdlPackage
                    && candidate.primary_name == package.name
                    && candidate.name == package.name
                    && candidate_library == package_library;
            });
        if (package_body != parsed_.units.end()) {
            effective_package.parameters.insert(
                effective_package.parameters.end(),
                package_body->parameters.begin(),
                package_body->parameters.end());
            effective_package.type_aliases.insert(
                effective_package.type_aliases.end(),
                package_body->type_aliases.begin(),
                package_body->type_aliases.end());
            effective_package.generic_function_templates.insert(
                effective_package.generic_function_templates.end(),
                package_body->generic_function_templates.begin(),
                package_body->generic_function_templates.end());
            effective_package.generic_procedure_templates.insert(
                effective_package.generic_procedure_templates.end(),
                package_body->generic_procedure_templates.begin(),
                package_body->generic_procedure_templates.end());
            effective_package.generic_function_instances.insert(
                effective_package.generic_function_instances.end(),
                package_body->generic_function_instances.begin(),
                package_body->generic_function_instances.end());
            effective_package.generic_procedure_instances.insert(
                effective_package.generic_procedure_instances.end(),
                package_body->generic_procedure_instances.begin(),
                package_body->generic_procedure_instances.end());
            const auto conforming_declaration =
                [](const frontend::FunctionDeclaration& declaration,
                   const frontend::FunctionDeclaration& body) {
                  if (declaration.name != body.name
                      || declaration.arguments.size()
                          != body.arguments.size()
                      || declaration.return_type.spelling
                          != body.return_type.spelling
                      || declaration.return_type.domain
                          != body.return_type.domain) {
                      return false;
                  }
                  for (std::size_t index = 0;
                       index < declaration.arguments.size();
                       ++index) {
                      if (declaration.arguments[index].direction
                              != body.arguments[index].direction
                          || declaration.arguments[index].type.spelling
                              != body.arguments[index].type.spelling
                          || declaration.arguments[index].type.domain
                              != body.arguments[index].type.domain) {
                          return false;
                      }
                  }
                  return true;
                };
            for (const auto& body_function :
                 package_body->functions) {
                if (!body_function.defined) {
                    continue;
                }
                const auto declaration =
                    std::ranges::find_if(
                        effective_package.functions,
                        [&](const auto& candidate) {
                            return candidate.name
                                    == body_function.name
                                && !candidate.defined
                                && conforming_declaration(
                                    candidate, body_function);
                        });
                if (declaration
                    != effective_package.functions.end()) {
                    *declaration = body_function;
                } else {
                    effective_package.functions.push_back(
                        body_function);
                }
            }
            const auto conforming_procedure_declaration =
                [](const frontend::ProcedureDeclaration& declaration,
                   const frontend::ProcedureDeclaration& body) {
                  if (declaration.name != body.name
                      || declaration.arguments.size()
                          != body.arguments.size()) {
                      return false;
                  }
                  for (std::size_t index = 0;
                       index < declaration.arguments.size(); ++index) {
                      const auto& left = declaration.arguments[index];
                      const auto& right = body.arguments[index];
                      if (left.type.spelling != right.type.spelling
                          || left.type.domain != right.type.domain
                          || left.direction != right.direction
                          || left.object_class
                              != right.object_class) {
                          return false;
                      }
                  }
                  return true;
                };
            for (const auto& body_procedure :
                 package_body->procedures) {
                if (!body_procedure.defined) {
                    continue;
                }
                const auto declaration =
                    std::ranges::find_if(
                        effective_package.procedures,
                        [&](const auto& candidate) {
                            return candidate.name
                                    == body_procedure.name
                                && !candidate.defined
                                && conforming_procedure_declaration(
                                    candidate, body_procedure);
                        });
                if (declaration
                    != effective_package.procedures.end()) {
                    *declaration = body_procedure;
                } else {
                    effective_package.procedures.push_back(
                        body_procedure);
                }
            }
            effective_package.vhdl_context.insert(
                effective_package.vhdl_context.end(),
                package_body->vhdl_context.begin(),
                package_body->vhdl_context.end());
            const auto body_source = std::string{
                frontend::physical_source(package_body->span)};
            if (!body_source.empty()
                && body_source
                    != frontend::physical_source(package.span)
                && std::ranges::find(
                       effective_package.source_dependencies,
                       body_source)
                    == effective_package
                           .source_dependencies.end()) {
                effective_package.source_dependencies.push_back(
                    body_source);
            }
        }
        std::vector<frontend::VhdlContextItem>
            expanded_package_context;
        std::vector<const DesignUnit*> context_stack;
        const auto effective_package_library =
            effective_package.library.empty()
                ? std::string{"work"}
                : effective_package.library;
        expand_vhdl_context_references(
            effective_package,
            package.vhdl_context,
            expanded_package_context,
            context_stack,
            effective_package_library);
        NamedTypeEnvironment type_environment;
        import_vhdl_package_constants(
            effective_package,
            expanded_package_context,
            import_stack,
            type_environment);
        import_qualified_vhdl_package_constants(
            effective_package, import_stack);
        import_qualified_vhdl_package_types(
            effective_package, type_environment, import_stack);
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
                    effective_package_library + "."
                        + effective_package.name,
                    true});
        }
        resolve_named_types(
            effective_package, type_environment, true);
        for (const auto& [name, binding] : type_environment) {
            if (binding.type.enumeration_literals.empty()
                || std::any_of(
                    effective_package.type_aliases.begin(),
                    effective_package.type_aliases.end(),
                    [&](const auto& alias) {
                        return alias.name == name;
                    })) {
                continue;
            }
            effective_package.type_aliases.push_back(
                frontend::TypeAliasDeclaration{
                    name,
                    binding.type,
                    effective_package.span,
                    {},
                    frontend::TypeDeclarationKind::Alias});
        }
        auto type_specialized =
            specialize_vhdl_interface_types(
                effective_package,
                overrides,
                parent_environment,
                parent_domains,
                parent_types,
                parent_functions,
                parent_procedures,
                frontend::Language::Vhdl2008,
                diagnostics_);
        if (type_specialized.applied) {
            resolve_named_types(
                type_specialized.unit, {}, true);
        }
        auto specialized = specialize_unit(
            type_specialized.unit,
            type_specialized.value_overrides,
            parent_environment,
            frontend::Language::Vhdl2008,
            diagnostics_);
        if (specialized.identity_values.empty()) {
            specialized.identity_values = specialized.values;
        }
        if (!type_specialized.values.empty()) {
            specialized.values.insert(
                specialized.values.begin(),
                type_specialized.values.begin(),
                type_specialized.values.end());
            specialized.identity_values.insert(
                specialized.identity_values.begin(),
                type_specialized.values.begin(),
                type_specialized.values.end());
        }
        instantiate_vhdl_generic_subprograms(specialized);
        import_stack.pop_back();
        return specialized;
    }



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
                const bool type_mark =
                    std::any_of(
                        specialized_package->unit.type_aliases.begin(),
                        specialized_package->unit.type_aliases.end(),
                        [&](const auto& alias) {
                            return alias.name == constant_name;
                        });
                if (!enumeration_literal && !type_mark) {
                    report(
                        "FSIM-ELAB-PKG-010",
                        "VHDL package '" + requested_library
                            + "." + package_name
                            + "' has no constant or enumeration literal '"
                            + constant_name + "'",
                        reference_span);
                }
                if (enumeration_literal || type_mark) {
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
        std::unordered_map<std::string, std::string> owners;
        for (const auto& import_item :
             unit.systemverilog_imports) {
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
            for (const auto& declaration :
                 package->parameters) {
                if (!wildcard
                    && declaration.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto specialized_declaration =
                    std::find_if(
                        specialized_package->unit.parameters.begin(),
                        specialized_package->unit.parameters.end(),
                        [&](const auto& candidate) {
                            return candidate.name
                                    == declaration.name
                                && candidate.span.source_name
                                    == declaration.span.source_name
                                && candidate.span.begin.offset
                                    == declaration.span.begin.offset;
                        });
                const auto& imported_type =
                    specialized_declaration
                            == specialized_package->unit.parameters.end()
                        ? declaration.type
                        : specialized_declaration->type;
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
                } else {
                    const auto value =
                        specialized_package->environment.find(
                            declaration.name);
                    if (value
                        == specialized_package->environment.end()) {
                        continue;
                    }
                    imported_value = constant_expression(
                        value->second,
                        declaration.span,
                        imported_type.domain,
                        frontend::Language::
                            SystemVerilog2017);
                }
                const auto [owner, inserted] =
                    owners.emplace(
                        declaration.name, package->name);
                if (!inserted
                    && owner->second != package->name) {
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
                if (!wildcard
                    && alias.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [existing, inserted] =
                    type_environment.emplace(
                        alias.name,
                        NamedTypeBinding{
                            alias.type,
                            package->name});
                if (!inserted
                    && existing->second.owner
                        != package->name) {
                    report(
                        "FSIM-ELAB-SVTYPE-002",
                        "SystemVerilog type '" + alias.name
                            + "' is imported from multiple "
                              "packages",
                        import_item.span);
                }
            }
            for (const auto& function :
                 specialized_package->unit.functions) {
                if (!wildcard
                    && function.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [owner, inserted] =
                    owners.emplace(
                        function.name, package->name);
                if (!inserted
                    && owner->second != package->name) {
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
                if (!wildcard
                    && task.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [owner, inserted] =
                    owners.emplace(
                        task.name, package->name);
                if (!inserted
                    && owner->second != package->name) {
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

    void HierarchyBuilder::import_qualified_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& type_environment) {
        auto identifiers = qualified_identifiers(unit);
        std::vector<std::string> ordered;
        for (const auto& [identifier, span] : identifiers) {
            (void)span;
            if (identifier.find("::")
                != std::string::npos) {
                ordered.push_back(identifier);
            }
        }
        std::sort(ordered.begin(), ordered.end());
        std::vector<frontend::ParameterDeclaration> imports;
        std::vector<frontend::FunctionDeclaration>
            function_imports;
        std::vector<frontend::TaskDeclaration> task_imports;
        for (const auto& identifier : ordered) {
            const auto& reference_span =
                identifiers.at(identifier);
            const auto separator = identifier.find("::");
            if (separator == std::string::npos
                || separator == 0
                || identifier.find("::", separator + 2)
                    != std::string::npos
                || separator + 2 >= identifier.size()) {
                report(
                    "FSIM-ELAB-SVPKG-005",
                    "a package-scoped item must be "
                    "package::name",
                    reference_span);
                continue;
            }
            const auto package_name =
                identifier.substr(0, separator);
            const auto constant_name =
                identifier.substr(separator + 2);
            const auto* package =
                find_systemverilog_package(
                    unit, package_name);
            if (package == nullptr) {
                report(
                    "FSIM-ELAB-SVPKG-001",
                    "SystemVerilog package '"
                        + package_name + "' was not found",
                    reference_span);
                continue;
            }
            auto specialized_package =
                specialize_systemverilog_package(
                    *package, import_stack, reference_span);
            if (!specialized_package) {
                continue;
            }
            const auto declaration = std::find_if(
                package->parameters.begin(),
                package->parameters.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            const auto alias = std::find_if(
                specialized_package->unit.type_aliases.begin(),
                specialized_package->unit.type_aliases.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            const auto function = std::find_if(
                specialized_package->unit.functions.begin(),
                specialized_package->unit.functions.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            const auto task = std::find_if(
                specialized_package->unit.tasks.begin(),
                specialized_package->unit.tasks.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            if (declaration == package->parameters.end()
                && alias
                    == specialized_package->unit.type_aliases.end()
                && function
                    == specialized_package->unit.functions.end()
                && task
                    == specialized_package->unit.tasks.end()) {
                report(
                    "FSIM-ELAB-SVPKG-002",
                    "SystemVerilog package '"
                        + package_name
                        + "' has no exported item '"
                        + constant_name + "'",
                    reference_span);
                continue;
            }
            if (alias
                != specialized_package->unit.type_aliases.end()) {
                type_environment.insert_or_assign(
                    identifier,
                    NamedTypeBinding{
                        alias->type,
                        package->name});
                append_package_dependencies(
                    unit, *package, *specialized_package);
                continue;
            }
            if (function
                != specialized_package->unit.functions.end()) {
                auto imported = *function;
                imported.name = identifier;
                function_imports.push_back(std::move(imported));
                append_package_dependencies(
                    unit, *package, *specialized_package);
                continue;
            }
            if (task
                != specialized_package->unit.tasks.end()) {
                auto imported = *task;
                imported.name = identifier;
                task_imports.push_back(std::move(imported));
                append_package_dependencies(
                    unit, *package, *specialized_package);
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
            frontend::Expression imported_value;
            if (imported_type.spelling == "string") {
                const auto value =
                    specialized_package->string_environment.find(
                        constant_name);
                if (value
                    == specialized_package
                           ->string_environment.end()) {
                    continue;
                }
                imported_value =
                    value->second.expression(declaration->span);
            } else {
                const auto value =
                    specialized_package->environment.find(
                        constant_name);
                if (value
                    == specialized_package->environment.end()) {
                    continue;
                }
                imported_value = constant_expression(
                    value->second,
                    declaration->span,
                    imported_type.domain,
                    frontend::Language::
                        SystemVerilog2017);
            }
            imports.push_back({
                identifier,
                imported_type,
                std::move(imported_value),
                true,
                declaration->span});
            append_package_dependencies(
                unit, *package, *specialized_package);
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
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

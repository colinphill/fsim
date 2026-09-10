// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

    void HierarchyBuilder::build(const DesignUnit& root) {
        add_root(root, design_.top_);
        finalize();
    }

    void HierarchyBuilder::add_root(
        const DesignUnit& root,
        std::string path) {
        active_root_ = std::move(path);
        active_systemverilog_root_name_ = root.name;
        if (auto prepared =
                prepared_systemverilog_roots_.find(active_root_);
            prepared != prepared_systemverilog_roots_.end()) {
            auto specialized = std::move(prepared->second);
            prepared_systemverilog_roots_.erase(prepared);
            auto aliases = std::move(
                predeclared_root_signals_.at(active_root_));
            predeclared_root_signals_.erase(active_root_);
            instantiate(
                specialized.unit,
                active_root_,
                std::move(aliases),
                {},
                {},
                {},
                {},
                std::move(specialized.environment),
                std::move(specialized.integral_environment),
                std::move(specialized.values),
                std::move(specialized.identity_values),
                std::move(specialized.packages));
            return;
        }
        const DesignUnit* selected = &root;
        std::optional<DesignUnit> configured_root;
        std::string configuration_identity;
        if (root.kind
            == frontend::UnitKind::VhdlConfiguration) {
            selected =
                select_vhdl_configuration_root(root);
            if (selected == nullptr) {
                return;
            }
            active_vhdl_configuration_ = &root;
            vhdl_configurations_by_path_[active_root_] = &root;
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
        } else if (root.kind
            == frontend::UnitKind::SystemVerilogConfiguration) {
            selected = select_systemverilog_configuration_root(root);
            if (selected == nullptr) {
                return;
            }
            active_systemverilog_configuration_ = &root;
            systemverilog_configurations_by_path_[active_root_] = &root;
            active_systemverilog_root_name_ = selected->name;
            configured_root = *selected;
            const auto configuration_source = std::string {
                frontend::physical_source(root.span)
            };
            if (!configuration_source.empty()
                && std::ranges::find(
                       configured_root->source_dependencies,
                       configuration_source)
                    == configured_root->source_dependencies.end()) {
                configured_root->source_dependencies.push_back(
                    configuration_source);
            }
            selected = &*configured_root;
            configuration_identity = systemverilog_configuration_identity(root);
        }
        auto specialized = specialize_selected_unit(
            *selected, {}, {}, {}, {}, {}, {}, {}, {},
            selected->language);
        if (!configuration_identity.empty()) {
            specialized.identity_values.emplace_back(
                "__configuration",
                std::move(configuration_identity));
        }
        instantiate(
            specialized.unit,
            active_root_,
            {},
            {},
            {},
            {},
            {},
            std::move(specialized.environment),
            std::move(specialized.integral_environment),
            std::move(specialized.values),
            std::move(specialized.identity_values),
            std::move(specialized.packages));
        active_vhdl_configuration_ = nullptr;
        vhdl_configurations_by_path_.clear();
        active_systemverilog_configuration_ = nullptr;
        systemverilog_configurations_by_path_.clear();
    }
    void HierarchyBuilder::build(const SystemCInstanceDescription& root) {
        add_root(root, root.path);
        finalize();
    }

    void HierarchyBuilder::add_root(
        const SystemCInstanceDescription& root,
        std::string path) {
        active_root_ = std::move(path);
        instantiate_systemc(root, active_root_, {}, {});
    }

    void HierarchyBuilder::finalize() {
        finish();
    }

    std::vector<frontend::SystemVerilogClassDeclaration>
    HierarchyBuilder::take_selected_systemverilog_classes() {
        return std::move(selected_systemverilog_classes_);
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
        std::vector<frontend::TypeAliasDeclaration>
            mode_view_imports;
        std::unordered_map<std::string, std::string> mode_view_owners;
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
                    if (alias.declaration_kind
                        == frontend::TypeDeclarationKind::VhdlModeView) {
                        const auto [known, inserted] =
                            mode_view_owners.emplace(
                                alias.name, package_owner);
                        if (!inserted && known->second != package_owner) {
                            report(
                                "FSIM-ELAB-VHVIEW-001",
                                "VHDL mode view '" + alias.name
                                    + "' is directly visible from multiple packages",
                                item.span);
                            continue;
                        }
                        if (std::ranges::none_of(
                                mode_view_imports,
                                [&](const auto& imported) {
                                    return imported.name == alias.name;
                                })) {
                            mode_view_imports.push_back(alias);
                        }
                        continue;
                    }
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
                    if (std::ranges::none_of(
                            function_imports,
                            [&](const auto& existing) {
                                return existing.span.source_name
                                        == function.span.source_name
                                    && existing.span.begin.offset
                                        == function.span.begin.offset;
                            })) {
                        auto imported = function;
                        imported.visibility_owner = package_owner;
                        for (const auto& dependency :
                             specialized.unit.functions) {
                            const bool dependency_exported =
                                std::ranges::any_of(
                                    package->functions,
                                    [&](const auto& declaration) {
                                      return declaration.name
                                          == dependency.name;
                                    })
                                || std::ranges::any_of(
                                    package->generic_function_instances,
                                    [&](const auto& declaration) {
                                      return declaration.name
                                          == dependency.name;
                                    });
                            if (!dependency_exported && dependency.defined) {
                                imported.functions.push_back(dependency);
                            }
                        }
                        function_imports.push_back(std::move(imported));
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
                    if (std::ranges::none_of(
                            procedure_imports,
                            [&](const auto& existing) {
                                return existing.span.source_name
                                        == procedure.span.source_name
                                    && existing.span.begin.offset
                                        == procedure.span.begin.offset;
                            })) {
                        auto imported = procedure;
                        imported.visibility_owner = package_owner;
                        procedure_imports.push_back(std::move(imported));
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
        std::unordered_set<std::string> local_mode_views;
        for (const auto& alias : unit.type_aliases) {
            if (alias.declaration_kind
                == frontend::TypeDeclarationKind::VhdlModeView) {
                local_mode_views.insert(alias.name);
            }
        }
        std::erase_if(
            mode_view_imports,
            [&](const auto& view) {
                return local_mode_views.contains(view.name);
            });
        mode_view_imports.insert(
            mode_view_imports.end(),
            std::make_move_iterator(unit.type_aliases.begin()),
            std::make_move_iterator(unit.type_aliases.end()));
        unit.type_aliases = std::move(mode_view_imports);
        std::unordered_set<std::string> local_function_names;
        for (const auto& function : unit.functions) {
            local_function_names.insert(function.name);
        }
        std::erase_if(
            function_imports,
            [&](const auto& function) {
              return local_function_names.contains(function.name);
            });
        function_imports.insert(
            function_imports.end(),
            std::make_move_iterator(unit.functions.begin()),
            std::make_move_iterator(unit.functions.end()));
        unit.functions = std::move(function_imports);
        std::unordered_set<std::string> local_procedure_names;
        for (const auto& procedure : unit.procedures) {
            local_procedure_names.insert(procedure.name);
        }
        std::erase_if(
            procedure_imports,
            [&](const auto& procedure) {
              return local_procedure_names.contains(procedure.name);
            });
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
            for (const auto& body_parameter : package_body->parameters) {
                if (body_parameter.kind
                    != frontend::ParameterKind::Value) {
                    effective_package.parameters.push_back(body_parameter);
                    continue;
                }
                const auto declaration = std::ranges::find_if(
                    effective_package.parameters,
                    [&](const auto& candidate) {
                      return candidate.kind
                              == frontend::ParameterKind::Value
                          && candidate.name == body_parameter.name;
                    });
                if (declaration == effective_package.parameters.end()) {
                    effective_package.parameters.push_back(body_parameter);
                    continue;
                }
                if (!declaration->vhdl_deferred) {
                    report(
                        "FSIM-ELAB-VHLEGAL-012",
                        "VHDL package body redeclares nondeferred constant '"
                            + body_parameter.name + "'",
                        body_parameter.span);
                    continue;
                }
                declaration->vhdl_completion_span = body_parameter.span;
                if (!frontend::vhdl_subtype_indications_conform(
                        declaration->type, body_parameter.type)) {
                    report(
                        "FSIM-ELAB-VHLEGAL-011",
                        "full declaration of deferred VHDL package constant '"
                            + body_parameter.name
                            + "' does not conform to its subtype indication",
                        body_parameter.span);
                    continue;
                }
                declaration->default_value = body_parameter.default_value;
                declaration->vhdl_deferred = false;
            }
            for (const auto& declaration : effective_package.parameters) {
                if (declaration.kind == frontend::ParameterKind::Value
                    && declaration.vhdl_deferred
                    && !declaration.vhdl_completion_span) {
                    report(
                        "FSIM-ELAB-VHLEGAL-010",
                        "deferred VHDL package constant '" + declaration.name
                            + "' has no full declaration in the package body",
                        declaration.span);
                }
            }
            effective_package.vhdl_attributes.insert(
                effective_package.vhdl_attributes.end(),
                package_body->vhdl_attributes.begin(),
                package_body->vhdl_attributes.end());
            effective_package.vhdl_groups.insert(
                effective_package.vhdl_groups.end(),
                package_body->vhdl_groups.begin(),
                package_body->vhdl_groups.end());
            effective_package.type_aliases.insert(
                effective_package.type_aliases.end(),
                package_body->type_aliases.begin(),
                package_body->type_aliases.end());
            merge_vhdl_protected_types(
                effective_package, *package_body);
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
                      || declaration.pure != body.pure
                      || declaration.arguments.size()
                          != body.arguments.size()
                      || !frontend::vhdl_base_type_profiles_match(
                          declaration.return_type,
                          body.return_type)) {
                      return false;
                  }
                  for (std::size_t index = 0;
                       index < declaration.arguments.size();
                       ++index) {
                      if (declaration.arguments[index].direction
                              != body.arguments[index].direction
                          || declaration.arguments[index].vhdl_file
                              != body.arguments[index].vhdl_file
                          || !frontend::vhdl_parameter_type_profiles_match(
                              declaration.arguments[index].type,
                              body.arguments[index].type)) {
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
                    if (std::ranges::any_of(
                            effective_package.functions,
                            [&](const auto& candidate) {
                              return candidate.name
                                      == body_function.name
                                  && !candidate.defined;
                            })) {
                        report(
                            "FSIM-ELAB-VHLEGAL-001",
                            "VHDL function body '"
                                + body_function.name
                                + "' does not conform to any package "
                                  "declaration with that designator",
                            body_function.span);
                    }
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
                      if (left.direction != right.direction
                          || left.object_class
                              != right.object_class
                          || !frontend::vhdl_parameter_type_profiles_match(
                              left.type, right.type)) {
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
                    if (std::ranges::any_of(
                            effective_package.procedures,
                            [&](const auto& candidate) {
                              return candidate.name
                                      == body_procedure.name
                                  && !candidate.defined;
                            })) {
                        report(
                            "FSIM-ELAB-VHLEGAL-003",
                            "VHDL procedure body '"
                                + body_procedure.name
                                + "' does not conform to any package "
                                  "declaration with that designator",
                            body_procedure.span);
                    }
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
        } else {
            for (const auto& declaration : effective_package.parameters) {
                if (declaration.kind == frontend::ParameterKind::Value
                    && declaration.vhdl_deferred) {
                    report(
                        "FSIM-ELAB-VHLEGAL-010",
                        "deferred VHDL package constant '" + declaration.name
                            + "' has no full declaration in the package body",
                        declaration.span);
                }
            }
        }
        const bool generic_package = std::ranges::any_of(
            package.parameters,
            [](const auto& parameter) {
              return !parameter.local;
            });
        if (!generic_package) {
            for (const auto& function : effective_package.functions) {
                if (!function.defined) {
                    report(
                        "FSIM-ELAB-VHLEGAL-002",
                        "VHDL package function '" + function.name
                            + "' has no conforming body",
                        function.span);
                }
            }
            for (const auto& procedure : effective_package.procedures) {
                if (!procedure.defined) {
                    report(
                        "FSIM-ELAB-VHLEGAL-004",
                        "VHDL package procedure '" + procedure.name
                            + "' has no conforming body",
                        procedure.span);
                }
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
            effective_package.vhdl_context,
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
        const auto append_callable_dependencies =
            [&](auto& callable) {
              for (const auto& dependency :
                   effective_package.source_dependencies) {
                if (std::ranges::find(
                        callable.source_dependencies,
                        dependency)
                    == callable.source_dependencies.end()) {
                  callable.source_dependencies.push_back(dependency);
                }
              }
            };
        for (auto& function : effective_package.functions) {
            if (function.defined) {
                append_callable_dependencies(function);
            }
        }
        for (auto& procedure : effective_package.procedures) {
            if (procedure.defined) {
                append_callable_dependencies(procedure);
            }
        }
        for (auto& generic :
             effective_package.generic_function_templates) {
            append_callable_dependencies(generic.function);
        }
        for (auto& generic :
             effective_package.generic_procedure_templates) {
            append_callable_dependencies(generic.procedure);
        }
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
                frontend::TypeAliasDeclaration {
                    name,
                    binding.type,
                    effective_package.span,
                    { },
                    frontend::TypeDeclarationKind::Alias,
                    { },
                    { },
                    { },
                    false });
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

} // namespace fsim::elaboration

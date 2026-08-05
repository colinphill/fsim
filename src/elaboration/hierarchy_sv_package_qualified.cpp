// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {

void HierarchyBuilder::validate_systemverilog_exports(
    const DesignUnit& declared_package,
    const DesignUnit& effective_package) {
  const auto directly_declared = [](const auto& candidate,
                                    const auto& declarations) {
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
  const auto imported_name_exists = [&](const std::string_view name) {
    return std::ranges::any_of(
               effective_package.parameters,
               [&](const auto& candidate) {
                 return candidate.name == name
                     && !directly_declared(
                         candidate, declared_package.parameters);
               })
        || std::ranges::any_of(
               effective_package.type_aliases,
               [&](const auto& candidate) {
                 return candidate.name == name
                     && !directly_declared(
                         candidate, declared_package.type_aliases)
                     && !directly_declared(
                         candidate, declared_package.parameters);
               })
        || std::ranges::any_of(
               effective_package.functions,
               [&](const auto& candidate) {
                 return candidate.name == name
                     && !directly_declared(
                         candidate, declared_package.functions);
               })
        || std::ranges::any_of(
               effective_package.tasks,
               [&](const auto& candidate) {
                 return candidate.name == name
                     && !directly_declared(
                         candidate, declared_package.tasks);
               });
  };
  for (const auto& exported : declared_package.systemverilog_exports) {
    if (exported.package == "*" && !exported.name.empty()) {
      report(
          "FSIM-ELAB-SVPKG-007",
          "a wildcard export package must use the form *::*",
          exported.span);
      continue;
    }
    const bool backed_by_import = std::ranges::any_of(
        declared_package.systemverilog_imports,
        [&](const frontend::SystemVerilogImport& imported) {
          const bool package_matches = exported.package == "*"
              || imported.package == exported.package;
          const bool name_matches = exported.name.empty()
              || imported.name.empty()
              || imported.name == exported.name;
          return package_matches && name_matches;
        });
    if (!backed_by_import) {
      report(
          "FSIM-ELAB-SVPKG-007",
          "package export '" + exported.package + "::"
              + (exported.name.empty() ? "*" : exported.name)
              + "' is not backed by a matching import",
          exported.span);
      continue;
    }
    if (!exported.name.empty()
        && !imported_name_exists(exported.name)) {
      report(
          "FSIM-ELAB-SVPKG-008",
          "package export '" + exported.package + "::"
              + exported.name
              + "' does not select an imported declaration",
          exported.span);
    }
  }
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
            if (identifier == "std::randomize") continue;
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
                                    return (item.package == "*"
                                            || item.package
                                                == imported.package)
                                        && (imported.name.empty()
                                            || imported.name == name);
                                });
                        });
                };
            const frontend::ParameterDeclaration* declaration = nullptr;
            const auto direct_declaration = std::ranges::find_if(
                package->parameters,
                [&](const auto& candidate) {
                  return candidate.name == constant_name;
                });
            if (direct_declaration != package->parameters.end()) {
              const auto specialized_declaration = std::ranges::find_if(
                  specialized_package->unit.parameters,
                  [&](const auto& candidate) {
                    return directly_declared(
                        candidate, package->parameters)
                        && candidate.name == constant_name;
                  });
              declaration =
                  specialized_declaration
                          == specialized_package->unit.parameters.end()
                      ? &*direct_declaration
                      : &*specialized_declaration;
            } else {
              const auto reexported_declaration = std::ranges::find_if(
                  specialized_package->unit.parameters,
                  [&](const auto& candidate) {
                    return candidate.name == constant_name
                        && explicitly_exported(candidate.name);
                  });
              if (reexported_declaration
                  != specialized_package->unit.parameters.end()) {
                declaration = &*reexported_declaration;
              }
            }
            const auto alias = std::find_if(
                specialized_package->unit.type_aliases.begin(),
                specialized_package->unit.type_aliases.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name
                        && (directly_declared(
                                candidate, package->type_aliases)
                            || directly_declared(
                                candidate, package->parameters)
                            || explicitly_exported(candidate.name));
                });
            const auto function = std::find_if(
                specialized_package->unit.functions.begin(),
                specialized_package->unit.functions.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name
                        && (directly_declared(
                                candidate, package->functions)
                            || explicitly_exported(candidate.name));
                });
            const auto task = std::find_if(
                specialized_package->unit.tasks.begin(),
                specialized_package->unit.tasks.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name
                        && (directly_declared(
                                candidate, package->tasks)
                            || explicitly_exported(candidate.name));
                });
            if (declaration == nullptr
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
            const auto& imported_type = declaration->type;
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

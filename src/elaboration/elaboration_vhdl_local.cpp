// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <type_traits>

namespace fsim::elaboration::elaboration_detail {
namespace {

template <typename Owner>
void collect_local_region(
    const Owner& owner,
    QualifiedIdentifierMap& identifiers) {
    std::unordered_set<std::string> prior_identifiers;
    for (const auto& [name, span] : identifiers) {
        (void)span;
        prior_identifiers.insert(name);
    }
    for (const auto& constant : owner.constants) {
        collect_qualified_identifiers(
            constant.type, identifiers);
        collect_qualified_identifiers(
            constant.default_value, identifiers);
    }
    for (const auto& alias : owner.type_aliases) {
        collect_qualified_identifiers(alias.type, identifiers);
    }
    for (const auto& alias : owner.signal_aliases) {
        collect_qualified_identifiers(alias.type, identifiers);
    }
    for (const auto& variable : owner.variables) {
        collect_qualified_identifiers(variable.type, identifiers);
        if (variable.initializer) {
            collect_qualified_identifiers(
                *variable.initializer, identifiers);
        }
    }
    for (const auto& package : owner.package_instances) {
        for (const auto& actual : package.generic_map) {
            collect_qualified_identifiers(
                actual.value, identifiers);
            if (actual.type_value) {
                collect_qualified_identifiers(
                    *actual.type_value, identifiers);
            }
        }
    }
    for (const auto& function : owner.functions) {
        collect_vhdl_local_qualified_identifiers(
            function, identifiers);
    }
    for (const auto& procedure : owner.procedures) {
        collect_vhdl_local_qualified_identifiers(
            procedure, identifiers);
    }
    collect_qualified_identifiers(owner.statements, identifiers);
    for (const auto& package : owner.package_instances) {
        const auto prefix = package.name + ".";
        std::erase_if(
            identifiers,
            [&](const auto& identifier) {
              return !prior_identifiers.contains(identifier.first)
                  && identifier.first.starts_with(prefix);
            });
    }
}

} // namespace

void collect_vhdl_local_qualified_identifiers(
    const frontend::FunctionDeclaration& function,
    QualifiedIdentifierMap& identifiers) {
    collect_qualified_identifiers(
        function.return_type, identifiers);
    for (const auto& argument : function.arguments) {
        collect_qualified_identifiers(argument.type, identifiers);
        if (argument.default_value) {
            collect_qualified_identifiers(
                *argument.default_value, identifiers);
        }
    }
    collect_local_region(function, identifiers);
}

void collect_vhdl_local_qualified_identifiers(
    const frontend::ProcedureDeclaration& procedure,
    QualifiedIdentifierMap& identifiers) {
    for (const auto& argument : procedure.arguments) {
        collect_qualified_identifiers(argument.type, identifiers);
        if (argument.default_value) {
            collect_qualified_identifiers(
                *argument.default_value, identifiers);
        }
    }
    collect_local_region(procedure, identifiers);
}

void collect_vhdl_local_qualified_identifiers(
    const frontend::Process& process,
    QualifiedIdentifierMap& identifiers) {
    for (const auto& sensitivity : process.sensitivities) {
        collect_qualified_identifiers(
            sensitivity.expression, identifiers);
    }
    collect_local_region(process, identifiers);
}

} // namespace fsim::elaboration::elaboration_detail

namespace fsim::elaboration {
using namespace elaboration_detail;

void HierarchyBuilder::materialize_vhdl_local_declarations(
    SpecializedUnit& specialized) {
    if (specialized.unit.language
        != frontend::Language::Vhdl2008) {
        return;
    }
    auto& unit = specialized.unit;
    std::vector<frontend::FunctionDeclaration> functions;
    std::vector<frontend::ProcedureDeclaration> procedures;

    const auto qualify_parameter =
        [&](frontend::ParameterDeclaration& parameter,
            const GeneratedNameEnvironment& names) {
          qualify_generated_type(parameter.type, names);
          qualify_generated_expression(
              parameter.default_value, names);
    };
    const auto qualify_variable =
        [&](frontend::VariableDeclaration& variable,
            const GeneratedNameEnvironment& names) {
          qualify_generated_type(variable.type, names);
          if (variable.initializer) {
              qualify_generated_expression(
                  *variable.initializer, names);
          }
    };

    const auto materialize =
        [&](auto&& self,
            auto& owner,
            GeneratedNameEnvironment names,
            const std::string& scope) -> void {
          using Owner = std::remove_cvref_t<decltype(owner)>;
          struct LocalDeclaration {
              std::size_t offset{};
              unsigned kind{};
              std::size_t index{};
          };
          std::vector<LocalDeclaration> declarations;
          for (std::size_t index = 0;
               index < owner.package_instances.size(); ++index) {
              declarations.push_back({
                  owner.package_instances[index].span.begin.offset,
                  0,
                  index});
          }
          for (std::size_t index = 0;
               index < owner.signal_aliases.size(); ++index) {
              declarations.push_back({
                  owner.signal_aliases[index].span.begin.offset,
                  1,
                  index});
          }
          for (std::size_t index = 0;
               index < owner.functions.size(); ++index) {
              declarations.push_back({
                  owner.functions[index].span.begin.offset,
                  2,
                  index});
          }
          for (std::size_t index = 0;
               index < owner.procedures.size(); ++index) {
              declarations.push_back({
                  owner.procedures[index].span.begin.offset,
                  3,
                  index});
          }
          std::ranges::stable_sort(
              declarations, {}, &LocalDeclaration::offset);
          for (const auto& declaration : declarations) {
              if (declaration.kind == 0) {
                  auto& package =
                      owner.package_instances[declaration.index];
                  const auto local_name = package.name;
                  package.name = generated_scope(scope, local_name);
                  names[local_name] = package.name;
                  for (auto& actual : package.generic_map) {
                      qualify_generated_expression(
                          actual.value, names);
                      if (actual.type_value) {
                          qualify_generated_type(
                              *actual.type_value, names);
                      }
                  }
                  unit.package_instances.push_back(
                      std::move(package));
              } else if (declaration.kind == 1) {
                  auto& alias =
                      owner.signal_aliases[declaration.index];
                  qualify_generated_type(alias.type, names);
                  if (const auto found = names.find(alias.actual);
                      found != names.end()) {
                      alias.actual = found->second;
                  }
                  names[alias.name] = alias.actual;
              } else if (declaration.kind == 2) {
                  auto& function = owner.functions[declaration.index];
                  const auto local_name = function.name;
                  function.name = generated_scope(scope, local_name);
                  names[local_name] = function.name;
                  self(self, function, names, function.name);
              } else {
                  auto& procedure =
                      owner.procedures[declaration.index];
                  const auto local_name = procedure.name;
                  procedure.name = generated_scope(scope, local_name);
                  names[local_name] = procedure.name;
                  self(self, procedure, names, procedure.name);
              }
          }
          owner.package_instances.clear();

          for (auto& constant : owner.constants) {
              qualify_parameter(constant, names);
          }
          for (auto& alias : owner.type_aliases) {
              qualify_generated_type(alias.type, names);
          }
          for (auto& variable : owner.variables) {
              qualify_variable(variable, names);
          }
          if constexpr (std::is_same_v<
                            Owner,
                            frontend::FunctionDeclaration>) {
              qualify_generated_type(owner.return_type, names);
              for (auto& argument : owner.arguments) {
                  qualify_generated_type(argument.type, names);
                  if (argument.default_value) {
                      qualify_generated_expression(
                          *argument.default_value, names);
                  }
              }
          } else if constexpr (std::is_same_v<
                                   Owner,
                                   frontend::ProcedureDeclaration>) {
              for (auto& argument : owner.arguments) {
                  qualify_generated_type(argument.type, names);
                  if (argument.default_value) {
                      qualify_generated_expression(
                          *argument.default_value, names);
                  }
              }
          } else {
              for (auto& sensitivity : owner.sensitivities) {
                  qualify_generated_expression(
                      sensitivity.expression, names);
              }
          }
          qualify_generated_statements(owner.statements, names);
          owner.signal_aliases.clear();

          for (auto& function : owner.functions) {
              functions.push_back(std::move(function));
          }
          for (auto& procedure : owner.procedures) {
              procedures.push_back(std::move(procedure));
          }
          owner.functions.clear();
          owner.procedures.clear();
        };

    const auto function_count = unit.functions.size();
    for (std::size_t index = 0; index < function_count; ++index) {
        auto& function = unit.functions[index];
        materialize(
            materialize,
            function,
            {},
            function.name + "@"
                + std::to_string(function.span.begin.offset));
    }
    const auto procedure_count = unit.procedures.size();
    for (std::size_t index = 0; index < procedure_count; ++index) {
        auto& procedure = unit.procedures[index];
        materialize(
            materialize,
            procedure,
            {},
            procedure.name + "@"
                + std::to_string(procedure.span.begin.offset));
    }
    for (auto& process : unit.processes) {
        const auto scope =
            (process.name.empty()
                 ? std::string{"process"}
                 : process.name)
            + "@" + std::to_string(process.span.begin.offset);
        materialize(materialize, process, {}, scope);
    }
    unit.functions.insert(
        unit.functions.end(),
        std::make_move_iterator(functions.begin()),
        std::make_move_iterator(functions.end()));
    unit.procedures.insert(
        unit.procedures.end(),
        std::make_move_iterator(procedures.begin()),
        std::make_move_iterator(procedures.end()));
}

} // namespace fsim::elaboration

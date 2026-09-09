// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/class_inheritance.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

namespace {

void diagnose(
    std::vector<Diagnostic>& diagnostics,
    std::string code,
    std::string message,
    const SourceSpan& span) {
  diagnostics.push_back({
      DiagnosticSeverity::Error,
      std::move(code),
      std::move(message),
      span,
      span.expansion_stack});
}

void collect_classes(
    const SystemVerilogClassDeclaration& declaration,
    std::map<std::string, const SystemVerilogClassDeclaration*>& classes) {
  classes.emplace(declaration.canonical_identity, &declaration);
  for (const auto& nested : declaration.nested_classes) {
    collect_classes(nested, classes);
  }
}

[[nodiscard]] std::string type_identity(const Type& type) {
  if (!type.systemverilog_class_declaration.empty()) {
    return "class:" + type.systemverilog_class_declaration;
  }
  if (!type.named_type.empty()) {
    return "named:" + type.named_type;
  }
  std::string identity = std::to_string(static_cast<unsigned>(type.domain));
  identity += type.is_signed ? ":signed" : ":unsigned";
  if (type.packed_range) {
    identity += ':' + std::to_string(type.packed_range->left)
        + ':' + std::to_string(type.packed_range->right);
  }
  return identity;
}

[[nodiscard]] const SystemVerilogClassDeclaration* method_owner(
    const SystemVerilogClassMethod& method,
    const std::map<
        std::string, const SystemVerilogClassDeclaration*>& classes) {
  const SystemVerilogClassDeclaration* owner = nullptr;
  std::size_t owner_identity_size{};
  for (const auto& [identity, declaration] : classes) {
    if (identity.size() <= owner_identity_size
        || !method.canonical_identity.starts_with(identity + "::")) {
      continue;
    }
    owner = declaration;
    owner_identity_size = identity.size();
  }
  return owner;
}

[[nodiscard]] Type resolved_method_type(
    const SystemVerilogClassMethod& method,
    Type result,
    const std::map<
        std::string, const SystemVerilogClassDeclaration*>& classes) {
  const auto* owner = method_owner(method, classes);
  std::set<std::string> expanded;
  while (!result.named_type.empty()
         && expanded.insert(result.named_type).second) {
    auto alias_name = result.named_type;
    const auto separator = alias_name.rfind("::");
    if (separator != std::string::npos) {
      const auto qualifier = alias_name.substr(0, separator);
      for (auto current = owner; current != nullptr;) {
        if (qualifier == current->name
            || qualifier == current->canonical_identity
            || current->canonical_identity.ends_with(
                "::" + qualifier)) {
          alias_name = alias_name.substr(separator + 2U);
          result.named_type = alias_name;
          break;
        }
        if (!current->base
            || current->base->declaration_identity.empty()) {
          break;
        }
        const auto base = classes.find(
            current->base->declaration_identity);
        current = base == classes.end() ? nullptr : base->second;
      }
    }
    const Type* replacement = nullptr;
    if (const auto method_alias = std::ranges::find(
            method.type_aliases,
            alias_name,
            &TypeAliasDeclaration::name);
        method_alias != method.type_aliases.end()) {
      replacement = &method_alias->type;
    } else {
      for (auto current = owner; current != nullptr;) {
        if (const auto class_alias = std::ranges::find(
                current->type_aliases,
                alias_name,
                &TypeAliasDeclaration::name);
            class_alias != current->type_aliases.end()) {
          replacement = &class_alias->type;
          break;
        }
        if (!current->base
            || current->base->declaration_identity.empty()) {
          break;
        }
        const auto base = classes.find(
            current->base->declaration_identity);
        current = base == classes.end() ? nullptr : base->second;
      }
    }
    if (replacement == nullptr) break;
    result = *replacement;
  }
  return result;
}

[[nodiscard]] std::string inheritance_profile(
    const SystemVerilogClassMethod& method,
    const std::map<
        std::string, const SystemVerilogClassDeclaration*>& classes) {
  std::string profile = method.name + ':'
      + std::to_string(static_cast<unsigned>(method.kind)) + '(';
  for (const auto& argument : method.arguments) {
    profile += std::to_string(static_cast<unsigned>(argument.direction));
    profile += ':';
    profile += type_identity(
        resolved_method_type(method, argument.type, classes));
    profile += argument.reference
        ? argument.const_reference
            ? argument.static_reference ? ":const-ref-static;" : ":const-ref;"
            : argument.static_reference ? ":ref-static;" : ":ref;"
        : ":value;";
  }
  profile += ')';
  return profile;
}

[[nodiscard]] bool symbolic_type_parameter(
    const SystemVerilogClassMethod& method,
    const Type& type,
    const std::map<
        std::string, const SystemVerilogClassDeclaration*>& classes) {
  const auto* owner = method_owner(method, classes);
  if (owner == nullptr || type.named_type.empty()) return false;
  auto name = type.named_type;
  if (const auto separator = name.rfind("::");
      separator != std::string::npos) {
    name = name.substr(separator + 2U);
  }
  return std::ranges::any_of(
      owner->parameters,
      [&](const ParameterDeclaration& parameter) {
        return parameter.kind == ParameterKind::Type
            && parameter.name == name;
      });
}

[[nodiscard]] bool compatible_argument_profile(
    const SystemVerilogClassMethod& method,
    const SystemVerilogClassMethod& base_method,
    const std::map<
        std::string, const SystemVerilogClassDeclaration*>& classes) {
  if (method.name != base_method.name
      || method.kind != base_method.kind
      || method.arguments.size() != base_method.arguments.size()) {
    return false;
  }
  for (std::size_t index = 0; index < method.arguments.size(); ++index) {
    const auto& argument = method.arguments[index];
    const auto& base_argument = base_method.arguments[index];
    if (argument.name != base_argument.name
        || argument.direction != base_argument.direction
        || argument.reference != base_argument.reference
        || argument.const_reference != base_argument.const_reference
        || argument.static_reference != base_argument.static_reference) {
      return false;
    }
    const auto argument_type = resolved_method_type(
        method, argument.type, classes);
    const auto base_argument_type = resolved_method_type(
        base_method, base_argument.type, classes);
    if (!systemverilog_types_equivalent(
            argument_type, base_argument_type)
        && !symbolic_type_parameter(method, argument_type, classes)
        && !symbolic_type_parameter(
            base_method, base_argument_type, classes)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool same_result_type(
    const SystemVerilogClassMethod& left,
    const SystemVerilogClassMethod& right,
    const std::map<
        std::string, const SystemVerilogClassDeclaration*>& classes) {
  const auto left_result = resolved_method_type(
      left, left.return_type, classes);
  const auto right_result = resolved_method_type(
      right, right.return_type, classes);
  if (left.kind == SystemVerilogClassMethodKind::Task
      || systemverilog_types_equivalent(left_result, right_result)) {
    return true;
  }
  const auto& derived = left_result.systemverilog_class_declaration;
  const auto& base = right_result.systemverilog_class_declaration;
  if (derived.empty() || base.empty()) return false;
  auto current = classes.find(derived);
  std::set<std::string> visited;
  while (current != classes.end()
         && visited.insert(current->first).second) {
    const auto& parent = current->second->base;
    if (!parent || parent->declaration_identity.empty()) return false;
    if (parent->declaration_identity == base) return true;
    current = classes.find(parent->declaration_identity);
  }
  return false;
}

[[nodiscard]] bool base_constructor_call(const Statement& statement) {
  constexpr std::string_view prefix{"@sv-base-constructor:"};
  return statement.kind == StatementKind::TaskCall
      && statement.task_name.starts_with(prefix);
}

void collect_base_constructor_calls(
    const std::span<const Statement> statements,
    std::vector<const Statement*>& calls) {
  for (const auto& statement : statements) {
    if (base_constructor_call(statement)) calls.push_back(&statement);
    collect_base_constructor_calls(statement.statements, calls);
    collect_base_constructor_calls(statement.else_statements, calls);
    collect_base_constructor_calls(statement.loop_updates, calls);
    for (const auto& alternative : statement.case_alternatives) {
      collect_base_constructor_calls(alternative.statements, calls);
    }
  }
}

[[nodiscard]] const Statement* first_executable_statement(
    const std::span<const Statement> statements) {
  const auto first = std::ranges::find_if(
      statements,
      [](const Statement& statement) {
        return statement.kind != StatementKind::Null;
      });
  return first == statements.end() ? nullptr : &*first;
}

}  // namespace

bool validate_systemverilog_class_inheritance(
    const ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics) {
  const auto initial_diagnostic_count = diagnostics.size();
  std::map<std::string, const SystemVerilogClassDeclaration*> classes;
  for (const auto& unit : design.units) {
    for (const auto& declaration : unit.systemverilog_classes) {
      collect_classes(declaration, classes);
    }
  }
  for (const auto& declaration : design.systemverilog_classes) {
    collect_classes(declaration, classes);
  }

  std::map<std::string, unsigned> visit_state;
  std::function<void(const SystemVerilogClassDeclaration&)> visit;
  visit = [&](const SystemVerilogClassDeclaration& declaration) {
    auto& state = visit_state[declaration.canonical_identity];
    if (state == 2U) return;
    if (state == 1U) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-INHERIT-001",
          "class inheritance cycle reaches '"
              + declaration.canonical_identity + "'",
          declaration.span);
      return;
    }
    state = 1U;
    if (declaration.base
        && !declaration.base->declaration_identity.empty()) {
      if (const auto base = classes.find(
              declaration.base->declaration_identity);
          base != classes.end()) {
        visit(*base->second);
      }
    }
    for (const auto& interface : declaration.extended_interfaces) {
      if (const auto extended = classes.find(
              interface.declaration_identity);
          extended != classes.end()) {
        visit(*extended->second);
      }
    }
    for (const auto& interface : declaration.implemented_interfaces) {
      if (const auto implemented = classes.find(
              interface.declaration_identity);
          implemented != classes.end()) {
        visit(*implemented->second);
      }
    }
    state = 2U;
  };
  for (const auto& [identity, declaration] : classes) {
    (void)identity;
    visit(*declaration);
  }

  for (const auto& [identity, declaration] : classes) {
    (void)identity;
    const auto resolved_class = [&](const SystemVerilogClassBase& relation)
        -> const SystemVerilogClassDeclaration* {
      const auto found = classes.find(relation.declaration_identity);
      return found == classes.end() ? nullptr : found->second;
    };
    if (declaration->is_interface) {
      if (declaration->base) {
        const auto* base = resolved_class(*declaration->base);
        if (base != nullptr && !base->is_interface) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-010",
              "interface class '" + declaration->canonical_identity
                  + "' extends non-interface class '"
                  + base->canonical_identity + "'",
              declaration->base->span);
        }
      }
      for (const auto& relation : declaration->extended_interfaces) {
        const auto* base = resolved_class(relation);
        if (base != nullptr && !base->is_interface) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-010",
              "interface class '" + declaration->canonical_identity
                  + "' extends non-interface class '"
                  + base->canonical_identity + "'",
              relation.span);
        }
      }
      for (const auto& relation : declaration->implemented_interfaces) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-010",
            "interface class '" + declaration->canonical_identity
                + "' cannot use an implements clause for '"
                + relation.name + "'",
            relation.span);
      }
      for (const auto& property : declaration->properties) {
        if (!property.is_parameter) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-011",
              "interface class '" + declaration->canonical_identity
                  + "' contains non-parameter property '"
                  + property.declaration.name + "'",
              property.span);
        }
      }
      for (const auto& method : declaration->methods) {
        if (!method.is_pure
            || method.visibility != SystemVerilogClassVisibility::Public
            || method.is_static || method.is_final || method.is_extern) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-011",
              "interface class method '" + method.canonical_identity
                  + "' must be a public pure virtual prototype",
              method.span);
        }
      }
      if (!declaration->constraints.empty()
          || !declaration->covergroups.empty()
          || !declaration->nested_classes.empty()) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-011",
            "interface class '" + declaration->canonical_identity
                + "' contains a disallowed constraint, covergroup, or nested class",
            declaration->span);
      }
    } else {
      if (declaration->base) {
        const auto* base = resolved_class(*declaration->base);
        if (base != nullptr && base->is_interface) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-010",
              "non-interface class '" + declaration->canonical_identity
                  + "' must implement rather than extend interface class '"
                  + base->canonical_identity + "'",
              declaration->base->span);
        }
      }
      for (const auto& relation : declaration->extended_interfaces) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-010",
            "non-interface class '" + declaration->canonical_identity
                + "' cannot have multiple extends relations",
            relation.span);
      }
    }

    std::set<std::string> local_names;
    std::size_t constructor_count{};
    for (const auto& method : declaration->methods) {
      if (method.kind == SystemVerilogClassMethodKind::Constructor) {
        ++constructor_count;
        if (method.is_static || method.is_virtual || method.is_pure
            || method.is_final
            || method.return_type.spelling != "constructor") {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-014",
              "constructor '" + method.canonical_identity
                  + "' has an explicit result type or an incompatible "
                    "static, virtual, pure, or final qualifier",
              method.span);
        }
      } else if (!local_names.insert(method.name).second) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-002",
            "class '" + declaration->canonical_identity
                + "' contains duplicate method name '" + method.name + "'",
            method.span);
      }

      if (method.lifetime == SystemVerilogClassLifetime::Static) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-015",
            "class method '" + method.canonical_identity
                + "' cannot have static storage lifetime",
            method.span);
      }

      std::vector<const Statement*> base_calls;
      collect_base_constructor_calls(method.statements, base_calls);
      const auto* first_statement = first_executable_statement(
          method.statements);
      if (!base_calls.empty()
          && (method.kind != SystemVerilogClassMethodKind::Constructor
              || base_calls.size() != 1U
              || base_calls.front() != first_statement)) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-016",
            "base-constructor invocation in '" + method.canonical_identity
                + "' must be the constructor's single first executable "
                  "statement",
            base_calls.front()->span);
      }

      if (method.kind == SystemVerilogClassMethodKind::Constructor) continue;
      if (method.is_pure && !declaration->is_virtual) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-003",
            "pure method '" + method.canonical_identity
                + "' requires a virtual class",
            method.span);
      }
      if ((method.is_final && (!method.is_virtual || method.is_pure))
          || (method.is_static && method.is_virtual)) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-004",
            "method '" + method.canonical_identity
                + "' has incompatible static, virtual, pure, or final qualifiers",
            method.span);
      }
    }
    if (constructor_count > 1U) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-INHERIT-014",
          "class '" + declaration->canonical_identity
              + "' declares more than one constructor",
          declaration->span);
    }

    std::vector<const SystemVerilogClassMethod*> inherited;
    std::set<std::string> visited;
    std::function<void(const SystemVerilogClassDeclaration&)> collect;
    collect = [&](const SystemVerilogClassDeclaration& parent) {
      if (!visited.insert(parent.canonical_identity).second) return;
      for (const auto& method : parent.methods) {
        if (method.kind != SystemVerilogClassMethodKind::Constructor
            && method.visibility != SystemVerilogClassVisibility::Local) {
          inherited.push_back(&method);
        }
      }
      if (parent.base && !parent.base->declaration_identity.empty()) {
        if (const auto base = classes.find(
                parent.base->declaration_identity);
            base != classes.end()) {
          collect(*base->second);
        }
      }
      for (const auto& interface : parent.extended_interfaces) {
        if (const auto extended = classes.find(
                interface.declaration_identity);
            extended != classes.end()) {
          collect(*extended->second);
        }
      }
      for (const auto& interface : parent.implemented_interfaces) {
        if (const auto implemented = classes.find(
                interface.declaration_identity);
            implemented != classes.end()) {
          collect(*implemented->second);
        }
      }
    };
    if (declaration->base
        && !declaration->base->declaration_identity.empty()) {
      if (const auto base = classes.find(
              declaration->base->declaration_identity);
          base != classes.end()) {
        collect(*base->second);
      }
    }
    for (const auto& interface : declaration->extended_interfaces) {
      if (const auto extended = classes.find(
              interface.declaration_identity);
          extended != classes.end()) {
        collect(*extended->second);
      }
    }
    for (const auto& interface : declaration->implemented_interfaces) {
      const auto implemented = classes.find(
          interface.declaration_identity);
      if (implemented == classes.end()) continue;
      if (!implemented->second->is_interface) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-005",
            "class '" + interface.declaration_identity
                + "' is not an interface class",
            interface.span);
      }
      collect(*implemented->second);
    }

    std::set<std::string> fulfilled_pure;
    for (const auto& method : declaration->methods) {
      if (method.kind == SystemVerilogClassMethodKind::Constructor) continue;
      for (const auto* base_method : inherited) {
        if (method.name != base_method->name
            || !base_method->is_virtual) {
          continue;
        }
        if (base_method->is_static != method.is_static
            || base_method->visibility != method.visibility
            || !compatible_argument_profile(
                method, *base_method, classes)) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-008",
              "override '" + method.canonical_identity
                  + "' does not match inherited prototype '"
                  + base_method->canonical_identity + "'",
              method.span);
          continue;
        }
        if (!same_result_type(method, *base_method, classes)) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-006",
              "override '" + method.canonical_identity
                  + "' has an incompatible result type '"
                  + type_identity(method.return_type)
                  + "' for inherited result type '"
                  + type_identity(base_method->return_type) + "'",
              method.span);
          continue;
        }
        if (base_method->is_final) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-007",
              "method '" + method.canonical_identity
                  + "' overrides final method '"
                  + base_method->canonical_identity + "'",
              method.span);
        }
        if (!method.is_pure) {
          fulfilled_pure.insert(
              inheritance_profile(*base_method, classes));
        }
      }
    }
    std::map<std::string, std::set<std::string>, std::less<>>
        inherited_interface_methods;
    for (const auto* method : inherited) {
      const auto* owner = method_owner(*method, classes);
      if (owner != nullptr && owner->is_interface) {
        inherited_interface_methods[method->name].insert(
            method->canonical_identity);
      }
    }
    for (const auto& [name, identities] : inherited_interface_methods) {
      if (identities.size() < 2U || local_names.contains(name)) continue;
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-INHERIT-012",
          "class '" + declaration->canonical_identity
              + "' must resolve inherited interface method conflict '"
              + name + "'",
          declaration->span);
    }
    std::map<std::string, std::set<std::string>, std::less<>>
        inherited_interface_declarations;
    std::set<std::string> visited_interface_declarations;
    std::function<void(const SystemVerilogClassDeclaration&)>
        collect_interface_declarations;
    collect_interface_declarations =
        [&](const SystemVerilogClassDeclaration& parent) {
          if (!visited_interface_declarations.insert(
                  parent.canonical_identity).second) {
            return;
          }
          if (parent.is_interface) {
            for (const auto& alias : parent.type_aliases) {
              inherited_interface_declarations[alias.name].insert(
                  parent.canonical_identity + "::type::" + alias.name);
            }
            for (const auto& property : parent.properties) {
              if (property.is_parameter) {
                inherited_interface_declarations[property.declaration.name]
                    .insert(
                        parent.canonical_identity + "::parameter::"
                        + property.declaration.name);
              }
            }
          }
          if (parent.base
              && !parent.base->declaration_identity.empty()) {
            if (const auto base = classes.find(
                    parent.base->declaration_identity);
                base != classes.end()) {
              collect_interface_declarations(*base->second);
            }
          }
          for (const auto& relation : parent.extended_interfaces) {
            if (const auto extended = classes.find(
                    relation.declaration_identity);
                extended != classes.end()) {
              collect_interface_declarations(*extended->second);
            }
          }
        };
    const auto collect_relation_declarations =
        [&](const SystemVerilogClassBase& relation) {
          if (const auto parent = classes.find(
                  relation.declaration_identity);
              parent != classes.end()) {
            collect_interface_declarations(*parent->second);
          }
        };
    if (declaration->base) {
      collect_relation_declarations(*declaration->base);
    }
    for (const auto& relation : declaration->extended_interfaces) {
      collect_relation_declarations(relation);
    }
    for (const auto& relation : declaration->implemented_interfaces) {
      collect_relation_declarations(relation);
    }
    const auto locally_resolves_declaration =
        [&](const std::string_view name) {
          return std::ranges::any_of(
                     declaration->type_aliases,
                     [&](const TypeAliasDeclaration& alias) {
                       return alias.name == name;
                     })
              || std::ranges::any_of(
                  declaration->properties,
                  [&](const SystemVerilogClassProperty& property) {
                    return property.is_parameter
                        && property.declaration.name == name;
                  });
        };
    for (const auto& [name, identities] :
         inherited_interface_declarations) {
      if (identities.size() < 2U
          || locally_resolves_declaration(name)) {
        continue;
      }
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-INHERIT-013",
          "class '" + declaration->canonical_identity
              + "' must resolve inherited interface declaration conflict '"
              + name + "'",
          declaration->span);
    }
    if (!declaration->is_virtual) {
      std::set<std::string> effective_profiles;
      for (const auto* method : inherited) {
        const auto profile = inheritance_profile(*method, classes);
        if (!effective_profiles.insert(profile).second) continue;
        if (method->is_pure && !fulfilled_pure.contains(profile)) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-009",
              "concrete class '" + declaration->canonical_identity
                  + "' does not implement pure method '"
                  + method->canonical_identity + "'",
              declaration->span);
        }
      }
    }
  }

  return std::none_of(
      diagnostics.begin()
          + static_cast<std::ptrdiff_t>(initial_diagnostic_count),
      diagnostics.end(),
      [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
      });
}

}  // namespace fsim::frontend

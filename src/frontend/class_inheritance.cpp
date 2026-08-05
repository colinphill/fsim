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

[[nodiscard]] std::string argument_profile(
    const SystemVerilogClassMethod& method) {
  std::string profile = method.name + ':'
      + std::to_string(static_cast<unsigned>(method.kind)) + '(';
  for (const auto& argument : method.arguments) {
    profile += std::to_string(static_cast<unsigned>(argument.direction));
    profile += ':';
    profile += type_identity(argument.type);
    profile += argument.reference ? ":ref;" : ":value;";
  }
  profile += ')';
  return profile;
}

[[nodiscard]] bool same_result_type(
    const SystemVerilogClassMethod& left,
    const SystemVerilogClassMethod& right,
    const std::map<
        std::string, const SystemVerilogClassDeclaration*>& classes) {
  if (left.kind == SystemVerilogClassMethodKind::Task
      || type_identity(left.return_type) == type_identity(right.return_type)) {
    return true;
  }
  const auto& derived = left.return_type.systemverilog_class_declaration;
  const auto& base = right.return_type.systemverilog_class_declaration;
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
    std::set<std::string> local_profiles;
    for (const auto& method : declaration->methods) {
      if (method.kind == SystemVerilogClassMethodKind::Constructor) {
        continue;
      }
      const auto profile = argument_profile(method);
      if (!local_profiles.insert(profile).second) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-002",
            "class '" + declaration->canonical_identity
                + "' contains duplicate method profile '" + profile + "'",
            method.span);
      }
      if (method.is_pure && !declaration->is_virtual) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-003",
            "pure method '" + method.canonical_identity
                + "' requires a virtual class",
            method.span);
      }
      if (method.is_final && !method.is_virtual) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-INHERIT-004",
            "final method '" + method.canonical_identity
                + "' must be virtual",
            method.span);
      }
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
      const auto profile = argument_profile(method);
      for (const auto* base_method : inherited) {
        if (argument_profile(*base_method) != profile) continue;
        if (!same_result_type(method, *base_method, classes)) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-006",
              "override '" + method.canonical_identity
                  + "' has an incompatible result type",
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
        if (base_method->is_static != method.is_static) {
          diagnose(
              diagnostics,
              "FSIM-SV-CLASS-INHERIT-008",
              "override '" + method.canonical_identity
                  + "' changes static membership",
              method.span);
        }
        if (!method.is_pure) fulfilled_pure.insert(profile);
      }
    }
    if (!declaration->is_virtual) {
      std::set<std::string> effective_profiles;
      for (const auto* method : inherited) {
        const auto profile = argument_profile(*method);
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

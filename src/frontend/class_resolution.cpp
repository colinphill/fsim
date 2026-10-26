// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/class_resolution.hpp"

#include "class_expression_resolution.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

namespace {

// The design whose classes are being resolved; its typedefs name base
// classes (`class c extends t;` with `typedef b t;`).
thread_local const ParsedDesign* resolving_design = nullptr;

struct ClassEntry {
  SystemVerilogClassDeclaration* declaration{};
  const std::vector<SystemVerilogImport>* imports{};
  std::string library;
  std::string compilation_unit_identity;
};

[[nodiscard]] std::string effective_library(const std::string_view library) {
  return library.empty() ? std::string{"work"} : std::string{library};
}

[[nodiscard]] bool ends_with_scope(
    const std::string_view identity,
    const std::string_view selected) {
  return identity == selected
      || (identity.size() > selected.size()
          && identity.ends_with(selected)
          && identity[identity.size() - selected.size() - 1U] == ':');
}

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

void assign_class_identities(
    SystemVerilogClassDeclaration& declaration,
    const std::string_view prefix,
    const std::string_view library,
    const std::string_view compilation_unit_identity,
    const std::vector<SystemVerilogImport>* imports,
    std::vector<ClassEntry>& entries) {
  declaration.library = effective_library(library);
  declaration.compilation_unit_identity = compilation_unit_identity;
  declaration.canonical_identity = std::string{prefix};
  if (!declaration.canonical_identity.empty()) {
    declaration.canonical_identity += "::";
  }
  declaration.canonical_identity += declaration.name;
  for (auto& method : declaration.methods) {
    method.library = declaration.library;
    method.compilation_unit_identity = declaration.compilation_unit_identity;
    method.canonical_identity = declaration.canonical_identity
        + "::" + method.name;
  }
  for (auto& constraint : declaration.constraints) {
    constraint.canonical_identity = declaration.canonical_identity
        + "::" + constraint.name;
  }
  entries.push_back({
      &declaration,
      imports,
      declaration.library,
      declaration.compilation_unit_identity});
  for (auto& nested : declaration.nested_classes) {
    assign_class_identities(
        nested,
        declaration.canonical_identity,
        declaration.library,
        declaration.compilation_unit_identity,
        imports,
        entries);
  }
}

[[nodiscard]] std::vector<ClassEntry*> unique_entries(
    std::vector<ClassEntry*> entries) {
  std::ranges::sort(
      entries,
      {},
      [](const ClassEntry* entry) {
        return entry->declaration->canonical_identity;
      });
  entries.erase(
      std::unique(
          entries.begin(),
          entries.end(),
          [](const ClassEntry* left, const ClassEntry* right) {
            return left->declaration->canonical_identity
                == right->declaration->canonical_identity;
          }),
      entries.end());
  return entries;
}

[[nodiscard]] std::vector<ClassEntry*> resolve_name(
    const std::string_view spelling,
    const ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const bool through_bases = true) {
  const auto matching = [&](const auto& predicate) {
    std::vector<ClassEntry*> result;
    for (auto& entry : entries) {
      if (predicate(entry)) {
        result.push_back(&entry);
      }
    }
    return unique_entries(std::move(result));
  };
  // Package names form one name space across libraries (IEEE 1800-2017
  // 3.13): a package compiled into another library, such as a mapped UVM
  // library, is still found by its name. The owner's library is preferred.
  const auto in_library = [&](const auto& predicate) {
    auto local = matching([&](const ClassEntry& entry) {
      return entry.library == owner.library && predicate(entry);
    });
    return local.empty() ? matching(predicate) : local;
  };
  if (spelling.find("::") != std::string_view::npos) {
    return in_library([&](const ClassEntry& entry) {
      return ends_with_scope(
          entry.declaration->canonical_identity, spelling);
    });
  }

  std::string lexical = owner.declaration->canonical_identity;
  for (;;) {
    auto direct = matching([&](const ClassEntry& entry) {
      return entry.declaration->canonical_identity
          == lexical + "::" + std::string{spelling};
    });
    if (!direct.empty()) {
      return direct;
    }
    const auto separator = lexical.rfind("::");
    if (separator == std::string::npos) {
      break;
    }
    lexical.resize(separator);
  }

  // A nested class inherited from a base class (IEEE 1800-2017 8.13, 8.23).
  std::set<std::string> visited;
  for (const ClassEntry* current = &owner;
       through_bases && current->declaration->base
       && visited.insert(current->declaration->canonical_identity).second;) {
    const auto& base = *current->declaration->base;
    const auto bases = base.declaration_identity.empty()
        ? resolve_name(base.name, *current, entries, false)
        : matching([&](const ClassEntry& entry) {
            return entry.declaration->canonical_identity
                == base.declaration_identity;
          });
    if (bases.size() != 1U) {
      break;
    }
    auto inherited = matching([&](const ClassEntry& entry) {
      return entry.declaration->canonical_identity
          == bases.front()->declaration->canonical_identity + "::"
              + std::string{spelling};
    });
    if (!inherited.empty()) {
      return inherited;
    }
    current = bases.front();
  }

  auto sibling = matching([&](const ClassEntry& entry) {
    const auto& identity = owner.declaration->canonical_identity;
    const auto separator = identity.rfind("::");
    return separator != std::string::npos
        && entry.declaration->canonical_identity
            == identity.substr(0, separator + 2U) + std::string{spelling};
  });
  if (!sibling.empty()) {
    return sibling;
  }

  if (owner.imports != nullptr) {
    std::vector<ClassEntry*> imported;
    for (const auto& import : *owner.imports) {
      if (!import.name.empty() && import.name != spelling) {
        continue;
      }
      auto candidates = in_library([&](const ClassEntry& entry) {
        return ends_with_scope(
            entry.declaration->canonical_identity,
            import.package + "::" + std::string{spelling});
      });
      imported.insert(
          imported.end(), candidates.begin(), candidates.end());
    }
    imported = unique_entries(std::move(imported));
    if (!imported.empty()) {
      return imported;
    }
  }

  // Only the compilation unit's own `$unit` classes; a package class needs
  // an import or `pkg::` (IEEE 1800-2017 26.3, 3.12.1).
  return matching([&](const ClassEntry& entry) {
    return entry.library == owner.library
        && entry.compilation_unit_identity
            == owner.compilation_unit_identity
        && entry.declaration->name == spelling
        && entry.declaration->canonical_identity.starts_with(
            entry.library + "::$unit");
  });
}

void resolve_type(
    Type& type,
    const ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  for (auto& member : type.packed_members) {
    for (auto& nested : member.nested_types) {
      resolve_type(
          nested, owner, entries, shadowed_types, diagnostics);
    }
  }
  if (type.systemverilog_container
      && type.systemverilog_container->associative_index_type) {
    resolve_type(
        *type.systemverilog_container->associative_index_type,
        owner,
        entries,
        shadowed_types,
        diagnostics);
  }
  for (auto& actual : type.systemverilog_class_parameter_actuals) {
    if (actual.type_actual) {
      resolve_type(
          *actual.type_actual,
          owner,
          entries,
          shadowed_types,
          diagnostics);
    }
  }
  if (type.systemverilog_virtual_interface) {
    return;
  }
  if (type.named_type.empty()
      || (type.named_type.find("::") == std::string::npos
          && shadowed_types.contains(type.named_type))) {
    return;
  }
  auto candidates = resolve_name(type.named_type, owner, entries);
  if (candidates.empty()) {
    return;
  }
  if (candidates.size() != 1U) {
    diagnose(
        diagnostics,
        "FSIM-SV-CLASS-004",
        "class handle type '" + type.named_type
            + "' is ambiguous in lexical/import scope",
        type.named_type_span);
    return;
  }
  type.systemverilog_class_name = type.named_type;
  type.systemverilog_class_declaration =
      candidates.front()->declaration->canonical_identity;
}

// Resolves the parameter and type-alias types of one scope; returns the
// names its type parameters and aliases add to `shadowed_types`.
template <typename Parameters, typename Aliases>
std::set<std::string> resolve_declared_types(
    Parameters& parameters,
    Aliases& type_aliases,
    std::set<std::string> shadowed_types,
    ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    std::vector<Diagnostic>& diagnostics) {
  for (const auto& parameter : parameters) {
    if (parameter.kind == ParameterKind::Type) {
      shadowed_types.insert(parameter.name);
    }
  }
  for (const auto& alias : type_aliases) {
    shadowed_types.insert(alias.name);
  }
  for (auto& parameter : parameters) {
    resolve_type(
        parameter.type, owner, entries, shadowed_types, diagnostics);
    if (parameter.default_type) {
      resolve_type(
          *parameter.default_type,
          owner,
          entries,
          shadowed_types,
          diagnostics);
    }
  }
  for (auto& alias : type_aliases) {
    resolve_type(alias.type, owner, entries, shadowed_types, diagnostics);
  }
  return shadowed_types;
}

void resolve_declaration_types(
    ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    std::vector<Diagnostic>& diagnostics) {
  auto& declaration = *owner.declaration;
  const auto shadowed_types = resolve_declared_types(declaration.parameters,
      declaration.type_aliases, { }, owner, entries, diagnostics);
  for (auto& property : declaration.properties) {
    resolve_type(
        property.declaration.type,
        owner,
        entries,
        shadowed_types,
        diagnostics);
  }
  for (auto& method : declaration.methods) {
    auto method_shadowed_types = shadowed_types;
    for (const auto& alias : method.type_aliases) {
      method_shadowed_types.insert(alias.name);
    }
    resolve_type(
        method.return_type,
        owner,
        entries,
        method_shadowed_types,
        diagnostics);
    for (auto& alias : method.type_aliases) {
      resolve_type(
          alias.type,
          owner,
          entries,
          method_shadowed_types,
          diagnostics);
    }
    for (auto& argument : method.arguments) {
      resolve_type(
          argument.type,
          owner,
          entries,
          method_shadowed_types,
          diagnostics);
    }
    for (auto& variable : method.variables) {
      resolve_type(
          variable.type,
          owner,
          entries,
          method_shadowed_types,
          diagnostics);
    }
  }
  if (declaration.base) {
    const bool deferred_type_parameter =
        declaration.base->name.find("::") == std::string::npos
        && shadowed_types.contains(declaration.base->name);
      if (!deferred_type_parameter
          && declaration.base->declaration_identity.empty()) {
      auto candidates =
          resolve_name(declaration.base->name, owner, entries);
      // A typedef of a class or of a specialization names that class and
      // its parameter values (IEEE 1800-2017 8.13, 6.18).
      if (candidates.empty() && resolving_design != nullptr
          && declaration.base->name.find("::") == std::string::npos) {
        const Type* aliased = nullptr;
        for (const auto& unit : resolving_design->units) {
          if (unit.language != Language::SystemVerilog2017) continue;
          const auto prefix =
              effective_library(unit.library) + "::" + unit.name + "::";
          const bool lexical =
              declaration.canonical_identity.starts_with(prefix)
              || (unit.name.starts_with("fsim_unit_")
                  && unit.compilation_unit_identity
                      == owner.compilation_unit_identity);
          if (!lexical) continue;
          for (const auto& alias : unit.type_aliases) {
            if (alias.name == declaration.base->name) {
              aliased = &alias.type;
            }
          }
        }
        if (aliased != nullptr && !aliased->named_type.empty()
            && aliased->named_type != declaration.base->name) {
          candidates = resolve_name(aliased->named_type, owner, entries);
          if (candidates.size() == 1U) {
            declaration.base->name = aliased->named_type;
            if (declaration.base->parameter_actuals.empty()) {
              for (const auto& actual :
                   aliased->systemverilog_class_parameter_actuals) {
                declaration.base->parameter_actuals.push_back(
                    SystemVerilogClassParameterActual{
                        actual.name.value_or(std::string{}),
                        actual.value,
                        actual.type_actual
                            ? std::optional<Type>{*actual.type_actual}
                            : std::nullopt,
                        actual.span});
              }
            }
          }
        }
      }
      if (candidates.empty()) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-002",
            "base class '" + declaration.base->name
                + "' is not visible from '"
                + declaration.canonical_identity + "'",
            declaration.base->span);
      } else if (candidates.size() != 1U) {
        diagnose(
            diagnostics,
            "FSIM-SV-CLASS-003",
            "base class '" + declaration.base->name
                + "' is ambiguous in lexical/import scope",
            declaration.base->span);
      } else {
        declaration.base->declaration_identity =
            candidates.front()->declaration->canonical_identity;
      }
    }
    for (auto& actual : declaration.base->parameter_actuals) {
      if (actual.type_actual) {
        resolve_type(
            *actual.type_actual,
            owner,
            entries,
            shadowed_types,
            diagnostics);
      }
    }
  }
  for (auto& extended : declaration.extended_interfaces) {
    auto candidates = extended.declaration_identity.empty()
        ? resolve_name(extended.name, owner, entries)
        : std::vector<ClassEntry*> {};
    if (!extended.declaration_identity.empty()) {
      // A compiled package environment already selected this identity.
    } else if (candidates.empty()) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-009",
          "extended interface class '" + extended.name
              + "' is not visible from '"
              + declaration.canonical_identity + "'",
          extended.span);
    } else if (candidates.size() != 1U) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-010",
          "extended interface class '" + extended.name
              + "' is ambiguous in lexical/import scope",
          extended.span);
    } else {
      extended.declaration_identity =
          candidates.front()->declaration->canonical_identity;
    }
    for (auto& actual : extended.parameter_actuals) {
      if (actual.type_actual) {
        resolve_type(
            *actual.type_actual,
            owner,
            entries,
            shadowed_types,
            diagnostics);
      }
    }
  }
  for (auto& implemented : declaration.implemented_interfaces) {
    auto candidates = implemented.declaration_identity.empty()
        ? resolve_name(implemented.name, owner, entries)
        : std::vector<ClassEntry*> {};
    if (!implemented.declaration_identity.empty()) {
      // Imported class declarations remain in compiled HIR.
    } else if (candidates.empty()) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-009",
          "implemented interface class '" + implemented.name
              + "' is not visible from '"
              + declaration.canonical_identity + "'",
          implemented.span);
    } else if (candidates.size() != 1U) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-010",
          "implemented interface class '" + implemented.name
              + "' is ambiguous in lexical/import scope",
          implemented.span);
    } else {
      implemented.declaration_identity =
          candidates.front()->declaration->canonical_identity;
    }
    for (auto& actual : implemented.parameter_actuals) {
      if (actual.type_actual) {
        resolve_type(
            *actual.type_actual,
            owner,
            entries,
            shadowed_types,
            diagnostics);
      }
    }
  }
}

void resolve_statement_types(
    Statement& statement,
    const ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  auto local_shadowed_types = shadowed_types;
  for (const auto& alias : statement.type_aliases) {
    local_shadowed_types.insert(alias.name);
  }
  for (auto& alias : statement.type_aliases) {
    resolve_type(
        alias.type,
        owner,
        entries,
        local_shadowed_types,
        diagnostics);
  }
  for (auto& declaration : statement.declarations) {
    resolve_type(
        declaration.type,
        owner,
        entries,
        local_shadowed_types,
        diagnostics);
  }
  for (auto& child : statement.statements) {
    resolve_statement_types(
        child, owner, entries, local_shadowed_types, diagnostics);
  }
  for (auto& child : statement.else_statements) {
    resolve_statement_types(
        child, owner, entries, local_shadowed_types, diagnostics);
  }
  for (auto& alternative : statement.case_alternatives) {
    for (auto& child : alternative.statements) {
      resolve_statement_types(
          child, owner, entries, local_shadowed_types, diagnostics);
    }
  }
}

void resolve_function_types(
    FunctionDeclaration& function,
    const ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& inherited_shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  auto shadowed_types = inherited_shadowed_types;
  for (const auto& alias : function.type_aliases) {
    shadowed_types.insert(alias.name);
  }
  resolve_type(
      function.return_type,
      owner,
      entries,
      shadowed_types,
      diagnostics);
  for (auto& alias : function.type_aliases) {
    resolve_type(
        alias.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& argument : function.arguments) {
    resolve_type(
        argument.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& variable : function.variables) {
    resolve_type(
        variable.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& statement : function.statements) {
    resolve_statement_types(
        statement, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& nested : function.functions) {
    resolve_function_types(
        nested, owner, entries, shadowed_types, diagnostics);
  }
}

void resolve_task_types(
    TaskDeclaration& task,
    const ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& inherited_shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  auto shadowed_types = inherited_shadowed_types;
  for (const auto& alias : task.type_aliases) {
    shadowed_types.insert(alias.name);
  }
  for (auto& alias : task.type_aliases) {
    resolve_type(
        alias.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& argument : task.arguments) {
    resolve_type(
        argument.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& variable : task.variables) {
    resolve_type(
        variable.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& statement : task.statements) {
    resolve_statement_types(
        statement, owner, entries, shadowed_types, diagnostics);
  }
}

void migrate_class_signals(
    std::vector<SignalDeclaration>& signals,
    std::vector<VariableDeclaration>& variables,
    std::vector<Diagnostic>& diagnostics) {
  std::vector<SignalDeclaration> retained;
  retained.reserve(signals.size());
  for (auto& signal : signals) {
    if (signal.direction == PortDirection::Unknown
        && !signal.type.systemverilog_class_declaration.empty()) {
      const bool duplicate = std::ranges::any_of(
          variables, [&](const VariableDeclaration& variable) {
            return variable.name == signal.name;
          });
      if (duplicate) {
        diagnose(
            diagnostics,
            "FSIM-SV-SEM-006",
            "duplicate class object declaration '" + signal.name + "'",
            signal.span);
      } else {
        variables.push_back({
            std::move(signal.name),
            std::move(signal.type),
            std::move(signal.default_value),
            std::move(signal.span)});
      }
    } else {
      retained.push_back(std::move(signal));
    }
  }
  signals = std::move(retained);
}

template <typename Process>
void resolve_process_types(
    Process& process,
    ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    std::set<std::string> shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  for (const auto& alias : process.type_aliases) {
    shadowed_types.insert(alias.name);
  }
  for (auto& alias : process.type_aliases) {
    resolve_type(alias.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& variable : process.variables) {
    resolve_type(variable.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& function : process.functions) {
    resolve_function_types(
        function, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& statement : process.statements) {
    resolve_statement_types(
        statement, owner, entries, shadowed_types, diagnostics);
  }
}

void resolve_generate_body_types(
    GenerateBody& body,
    const std::string& scope_path,
    ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& inherited_shadowed_types,
    std::vector<Diagnostic>& diagnostics);

template <typename Regions>
void resolve_generate_region_types(
    Regions& regions,
    const std::string& scope_path,
    ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  using generate_class_detail::join_scope;
  for (auto& region : regions) {
    resolve_generate_body_types(
        region.then_body, join_scope(scope_path, region.then_scope), owner,
        entries, shadowed_types, diagnostics);
    resolve_generate_body_types(
        region.else_body, join_scope(scope_path, region.else_scope), owner,
        entries, shadowed_types, diagnostics);
    for (auto& alternative : region.alternatives) {
      resolve_generate_body_types(
          alternative.body, join_scope(scope_path, alternative.scope),
          owner, entries, shadowed_types, diagnostics);
    }
  }
}

void resolve_generate_body_types(
    GenerateBody& body,
    const std::string& scope_path,
    ClassEntry& unit_owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& inherited_shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  // Names in a generate block are looked up from the block's scope outward
  // through its enclosing generate blocks to the unit (IEEE 1800-2017
  // 27.3); the block's classes are identified under that scope path.
  SystemVerilogClassDeclaration lexical_owner;
  lexical_owner.canonical_identity = unit_owner.declaration->canonical_identity;
  if (!scope_path.empty()) {
    lexical_owner.canonical_identity += "::" + scope_path;
  }
  ClassEntry owner{
      &lexical_owner,
      unit_owner.imports,
      unit_owner.library,
      unit_owner.compilation_unit_identity};
  const auto shadowed_types
      = resolve_declared_types(body.constants, body.type_aliases,
          inherited_shadowed_types, owner, entries, diagnostics);
  for (auto& signal : body.signals) {
    resolve_type(signal.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& variable : body.variables) {
    resolve_type(variable.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& function : body.functions) {
    resolve_function_types(
        function, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& task : body.tasks) {
    resolve_task_types(task, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& process : body.processes) {
    resolve_process_types(
        process, owner, entries, shadowed_types, diagnostics);
  }
  migrate_class_signals(body.signals, body.variables, diagnostics);
  resolve_generate_region_types(
      body.generate_regions, scope_path, unit_owner, entries,
      shadowed_types, diagnostics);
}

void resolve_unit_types(
    DesignUnit& unit,
    std::vector<ClassEntry>& entries,
    std::vector<Diagnostic>& diagnostics) {
  SystemVerilogClassDeclaration lexical_owner;
  lexical_owner.canonical_identity = effective_library(unit.library)
      + "::" + unit.name;
  ClassEntry owner{
      &lexical_owner,
      &unit.systemverilog_imports,
      effective_library(unit.library),
      unit.compilation_unit_identity};
  const auto shadowed_types = resolve_declared_types(
      unit.parameters, unit.type_aliases, { }, owner, entries, diagnostics);
  for (auto& port : unit.ports) {
    resolve_type(
        port.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& signal : unit.signals) {
    resolve_type(
        signal.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& variable : unit.variables) {
    resolve_type(
        variable.type, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& function : unit.functions) {
    resolve_function_types(
        function, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& task : unit.tasks) {
    resolve_task_types(
        task, owner, entries, shadowed_types, diagnostics);
  }
  for (auto& process : unit.processes) {
    resolve_process_types(
        process, owner, entries, shadowed_types, diagnostics);
  }
  migrate_class_signals(unit.signals, unit.variables, diagnostics);
  resolve_generate_region_types(
      unit.generate_regions, std::string{}, owner, entries, shadowed_types,
      diagnostics);
}

}  // namespace

bool resolve_systemverilog_classes(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics) {
  const auto initial_diagnostic_count = diagnostics.size();
  const struct DesignScope {
    explicit DesignScope(const ParsedDesign& design) {
      resolving_design = &design;
    }
    ~DesignScope() { resolving_design = nullptr; }
    DesignScope(const DesignScope&) = delete;
    DesignScope& operator=(const DesignScope&) = delete;
  } design_scope{design};
  std::vector<ClassEntry> entries;
  // Arm path of each class declared in a generate block.
  std::map<const SystemVerilogClassDeclaration*, std::string>
      generate_branches;
  for (auto& unit : design.units) {
    if (unit.language != Language::SystemVerilog2017) {
      continue;
    }
    const auto library = effective_library(unit.library);
    const auto prefix = library + "::" + unit.name;
    for (auto& declaration : unit.systemverilog_classes) {
      assign_class_identities(
          declaration,
          prefix,
          library,
          declaration.compilation_unit_identity,
          &unit.systemverilog_imports,
          entries);
    }
    // A class declared in a generate block belongs to that block's scope
    // (IEEE 1800-2017 27.3, 8.2).
    for_each_generate_class(
        unit,
        [&](SystemVerilogClassDeclaration& declaration,
            const std::string& scope_path,
            const std::string& branch) {
          const auto first = entries.size();
          assign_class_identities(
              declaration,
              scope_path.empty() ? prefix : prefix + "::" + scope_path,
              library,
              unit.compilation_unit_identity,
              &unit.systemverilog_imports,
              entries);
          for (auto index = first; index < entries.size(); ++index) {
            generate_branches.emplace(entries[index].declaration, branch);
          }
        });
  }
  for (auto& declaration : design.systemverilog_classes) {
    const auto library = effective_library(declaration.library);
    std::string prefix = library + "::$unit";
    if (!declaration.compilation_unit_identity.empty()) {
      prefix += "@" + declaration.compilation_unit_identity;
    }
    assign_class_identities(
        declaration,
        prefix,
        library,
        declaration.compilation_unit_identity,
        &declaration.compilation_unit_imports,
        entries);
  }

  std::map<std::string, std::size_t> identities;
  std::set<std::string> alias_identities;
  for (const auto& unit : design.units) {
    if (unit.language != Language::SystemVerilog2017) {
      continue;
    }
    const auto prefix =
        effective_library(unit.library) + "::" + unit.name;
    for (const auto& alias : unit.type_aliases) {
      alias_identities.insert(prefix + "::" + alias.name);
    }
  }
  for (auto& entry : entries) {
    auto& declaration = *entry.declaration;
    if (declaration.is_forward_declaration
        && !alias_identities.contains(
            declaration.canonical_identity)) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-001",
          "class '" + declaration.canonical_identity
              + "' has no defining declaration",
          declaration.span);
    }
    // Classes under different arms of a generate region are alternatives
    // that never coexist (IEEE 1800-2017 27.5), so the arm path is part of
    // the uniqueness key.
    const auto branch = generate_branches.find(&declaration);
    if (!identities.emplace(
             declaration.canonical_identity + '\n'
                 + (branch == generate_branches.end()
                         ? std::string{} : branch->second),
             1U).second) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-005",
          "duplicate canonical class identity '"
              + declaration.canonical_identity + "'",
          declaration.span);
    }
  }
  for (auto& entry : entries) {
    // Whether a generate block is instantiated depends on parameter values
    // bound at elaboration, and an unselected block is not elaborated
    // (IEEE 1800-2017 27.5), so its classes still gain resolved names but
    // their lookup failures cannot be reported against the design here.
    if (generate_branches.contains(entry.declaration)) {
      std::vector<Diagnostic> deferred;
      resolve_declaration_types(entry, entries, deferred);
      continue;
    }
    resolve_declaration_types(entry, entries, diagnostics);
  }
  for (auto& unit : design.units) {
    if (unit.language == Language::SystemVerilog2017) {
      resolve_unit_types(unit, entries, diagnostics);
    }
  }

  std::set<std::string> linked_definitions;
  std::vector<SystemVerilogClassMethod*> method_definitions;
  method_definitions.reserve(
      design.systemverilog_class_method_definitions.size());
  for (auto& definition :
       design.systemverilog_class_method_definitions) {
    method_definitions.push_back(&definition);
  }
  for (auto& unit : design.units) {
    for (auto& definition :
         unit.systemverilog_class_method_definitions) {
      if (definition.library.empty()) {
        definition.library = unit.library;
      }
      if (definition.compilation_unit_identity.empty()) {
        definition.compilation_unit_identity =
            unit.compilation_unit_identity;
      }
      method_definitions.push_back(&definition);
    }
  }
  for (auto* definition_pointer : method_definitions) {
    auto& definition = *definition_pointer;
    const auto separator = definition.name.rfind("::");
    if (separator == std::string::npos) {
      continue;
    }
    const auto owner_name = definition.name.substr(0, separator);
    const auto method_name = definition.name.substr(separator + 2U);
    std::vector<ClassEntry*> owners;
    for (auto& entry : entries) {
      if (entry.library == effective_library(definition.library)
          && entry.compilation_unit_identity
              == definition.compilation_unit_identity
          && ends_with_scope(
              entry.declaration->canonical_identity, owner_name)) {
        owners.push_back(&entry);
      }
    }
    owners = unique_entries(std::move(owners));
    if (owners.size() != 1U) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-006",
          "out-of-block method owner '" + owner_name
              + "' is missing or ambiguous",
          definition.span);
      continue;
    }
    auto& methods = owners.front()->declaration->methods;
    const auto prototype = std::ranges::find_if(
        methods,
        [&](const SystemVerilogClassMethod& method) {
          return method.name == method_name && method.is_extern
              && method.arguments.size() == definition.arguments.size();
        });
    const auto canonical =
        owners.front()->declaration->canonical_identity + "::" + method_name;
    if (prototype == methods.end()) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-007",
          "out-of-block definition '" + canonical
              + "' has no matching extern prototype",
          definition.span);
      continue;
    }
    if (!linked_definitions.insert(canonical).second) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-008",
          "class method '" + canonical
              + "' has more than one out-of-block definition",
          definition.span);
      continue;
    }
    std::set<std::string> definition_shadowed_types;
    for (const auto& parameter : owners.front()->declaration->parameters) {
      if (parameter.kind == ParameterKind::Type) {
        definition_shadowed_types.insert(parameter.name);
      }
    }
    for (const auto& alias : owners.front()->declaration->type_aliases) {
      definition_shadowed_types.insert(alias.name);
    }
    for (const auto& alias : definition.type_aliases) {
      definition_shadowed_types.insert(alias.name);
    }
    resolve_type(
        definition.return_type,
        *owners.front(),
        entries,
        definition_shadowed_types,
        diagnostics);
    for (auto& alias : definition.type_aliases) {
      resolve_type(
          alias.type,
          *owners.front(),
          entries,
          definition_shadowed_types,
          diagnostics);
    }
    for (auto& argument : definition.arguments) {
      resolve_type(
          argument.type,
          *owners.front(),
          entries,
          definition_shadowed_types,
          diagnostics);
    }
    for (auto& variable : definition.variables) {
      resolve_type(
          variable.type,
          *owners.front(),
          entries,
          definition_shadowed_types,
          diagnostics);
    }
    for (auto& statement : definition.statements) {
      resolve_statement_types(
          statement,
          *owners.front(),
          entries,
          definition_shadowed_types,
          diagnostics);
    }
    const auto visibility = prototype->visibility;
    const auto is_static = prototype->is_static;
    const auto is_virtual = prototype->is_virtual;
    const auto is_final = prototype->is_final;
    *prototype = definition;
    prototype->name = method_name;
    prototype->canonical_identity = canonical;
    prototype->visibility = visibility;
    prototype->is_static = is_static;
    prototype->is_virtual = is_virtual;
    prototype->is_final = is_final;
    prototype->is_extern = false;
    prototype->out_of_block_definition = true;
    prototype->defined = true;
  }
  if (std::none_of(
          diagnostics.begin()
              + static_cast<std::ptrdiff_t>(initial_diagnostic_count),
          diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
          })) {
    (void)resolve_systemverilog_class_expressions(design, diagnostics);
  }
  return std::none_of(
      diagnostics.begin()
          + static_cast<std::ptrdiff_t>(initial_diagnostic_count),
      diagnostics.end(),
      [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
      });
}

std::shared_ptr<const Type> systemverilog_class_scope_owner(
    const Type& selection, std::string declaration_identity) {
  auto owner = selection;
  owner.systemverilog_class_declaration = std::move(declaration_identity);
  return std::make_shared<const Type>(std::move(owner));
}

}  // namespace fsim::frontend

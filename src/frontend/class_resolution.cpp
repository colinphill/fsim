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
    std::vector<ClassEntry>& entries) {
  const auto matching = [&](const auto& predicate) {
    std::vector<ClassEntry*> result;
    for (auto& entry : entries) {
      if (predicate(entry)) {
        result.push_back(&entry);
      }
    }
    return unique_entries(std::move(result));
  };
  if (spelling.find("::") != std::string_view::npos) {
    return matching([&](const ClassEntry& entry) {
      return entry.library == owner.library
          && ends_with_scope(
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
      auto candidates = matching([&](const ClassEntry& entry) {
        return entry.library == owner.library
            && ends_with_scope(
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

  return matching([&](const ClassEntry& entry) {
    return entry.library == owner.library
        && entry.compilation_unit_identity
            == owner.compilation_unit_identity
        && entry.declaration->name == spelling;
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

void resolve_declaration_types(
    ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    std::vector<Diagnostic>& diagnostics) {
  auto& declaration = *owner.declaration;
  std::set<std::string> shadowed_types;
  for (const auto& parameter : declaration.parameters) {
    if (parameter.kind == ParameterKind::Type) {
      shadowed_types.insert(parameter.name);
    }
  }
  for (const auto& alias : declaration.type_aliases) {
    shadowed_types.insert(alias.name);
  }
  for (auto& parameter : declaration.parameters) {
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
  for (auto& alias : declaration.type_aliases) {
    resolve_type(
        alias.type, owner, entries, shadowed_types, diagnostics);
  }
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
    if (!deferred_type_parameter) {
      auto candidates =
          resolve_name(declaration.base->name, owner, entries);
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
    auto candidates = resolve_name(extended.name, owner, entries);
    if (candidates.empty()) {
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
    auto candidates = resolve_name(implemented.name, owner, entries);
    if (candidates.empty()) {
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
            std::nullopt,
            std::move(signal.span)});
      }
    } else {
      retained.push_back(std::move(signal));
    }
  }
  signals = std::move(retained);
}

void resolve_generate_body_types(
    GenerateBody& body,
    ClassEntry& owner,
    std::vector<ClassEntry>& entries,
    const std::set<std::string>& inherited_shadowed_types,
    std::vector<Diagnostic>& diagnostics) {
  auto shadowed_types = inherited_shadowed_types;
  for (const auto& parameter : body.constants) {
    if (parameter.kind == ParameterKind::Type) {
      shadowed_types.insert(parameter.name);
    }
  }
  for (const auto& alias : body.type_aliases) {
    shadowed_types.insert(alias.name);
  }
  for (auto& parameter : body.constants) {
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
  for (auto& alias : body.type_aliases) {
    resolve_type(alias.type, owner, entries, shadowed_types, diagnostics);
  }
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
    auto process_shadowed_types = shadowed_types;
    for (const auto& alias : process.type_aliases) {
      process_shadowed_types.insert(alias.name);
    }
    for (auto& alias : process.type_aliases) {
      resolve_type(
          alias.type,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
    for (auto& variable : process.variables) {
      resolve_type(
          variable.type,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
    for (auto& function : process.functions) {
      resolve_function_types(
          function,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
    for (auto& statement : process.statements) {
      resolve_statement_types(
          statement,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
  }
  migrate_class_signals(body.signals, body.variables, diagnostics);
  for (auto& region : body.generate_regions) {
    resolve_generate_body_types(
        region.then_body,
        owner,
        entries,
        shadowed_types,
        diagnostics);
    resolve_generate_body_types(
        region.else_body,
        owner,
        entries,
        shadowed_types,
        diagnostics);
    for (auto& alternative : region.alternatives) {
      resolve_generate_body_types(
          alternative.body,
          owner,
          entries,
          shadowed_types,
          diagnostics);
    }
  }
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
  std::set<std::string> shadowed_types;
  for (const auto& parameter : unit.parameters) {
    if (parameter.kind == ParameterKind::Type) {
      shadowed_types.insert(parameter.name);
    }
  }
  for (const auto& alias : unit.type_aliases) {
    shadowed_types.insert(alias.name);
  }
  for (auto& parameter : unit.parameters) {
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
  for (auto& alias : unit.type_aliases) {
    resolve_type(
        alias.type, owner, entries, shadowed_types, diagnostics);
  }
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
    auto process_shadowed_types = shadowed_types;
    for (const auto& alias : process.type_aliases) {
      process_shadowed_types.insert(alias.name);
    }
    for (auto& alias : process.type_aliases) {
      resolve_type(
          alias.type,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
    for (auto& variable : process.variables) {
      resolve_type(
          variable.type,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
    for (auto& function : process.functions) {
      resolve_function_types(
          function,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
    for (auto& statement : process.statements) {
      resolve_statement_types(
          statement,
          owner,
          entries,
          process_shadowed_types,
          diagnostics);
    }
  }
  migrate_class_signals(unit.signals, unit.variables, diagnostics);
  for (auto& region : unit.generate_regions) {
    resolve_generate_body_types(
        region.then_body,
        owner,
        entries,
        shadowed_types,
        diagnostics);
    resolve_generate_body_types(
        region.else_body,
        owner,
        entries,
        shadowed_types,
        diagnostics);
    for (auto& alternative : region.alternatives) {
      resolve_generate_body_types(
          alternative.body,
          owner,
          entries,
          shadowed_types,
          diagnostics);
    }
  }
}

}  // namespace

bool resolve_systemverilog_classes(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics) {
  const auto initial_diagnostic_count = diagnostics.size();
  std::vector<ClassEntry> entries;
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
        nullptr,
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
    if (!identities.emplace(
             declaration.canonical_identity, 1U).second) {
      diagnose(
          diagnostics,
          "FSIM-SV-CLASS-005",
          "duplicate canonical class identity '"
              + declaration.canonical_identity + "'",
          declaration.span);
    }
  }
  for (auto& entry : entries) {
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

}  // namespace fsim::frontend

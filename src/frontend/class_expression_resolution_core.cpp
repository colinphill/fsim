// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution_internal.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <utility>

namespace fsim::frontend::class_resolution_detail {


[[nodiscard]] std::string effective_library(const std::string_view library) {
  return library.empty() ? std::string{"work"} : std::string{library};
}

[[nodiscard]] std::optional<std::string> class_identity(const Type& type) {
  if (type.systemverilog_class_declaration.empty()) return std::nullopt;
  return type.systemverilog_class_declaration;
}

[[nodiscard]] Type class_type(
    const SystemVerilogClassDeclaration& declaration) {
  Type type;
  type.spelling = declaration.name;
  type.named_type = declaration.name;
  type.systemverilog_class_name = declaration.name;
  type.systemverilog_class_declaration = declaration.canonical_identity;
  return type;
}

[[nodiscard]] Type chandle_type() {
  Type type;
  type.spelling = "chandle";
  type.domain = ValueDomain::Unknown;
  type.systemverilog_scalar = SystemVerilogScalarKind::Chandle;
  return type;
}

[[nodiscard]] bool chandle(const std::optional<Type>& type) {
  return type
      && !type->systemverilog_container
      && type->systemverilog_scalar == SystemVerilogScalarKind::Chandle;
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

Resolver::Resolver(ParsedDesign& design, std::vector<Diagnostic>& diagnostics)
    : design_(design), diagnostics_(diagnostics) {
  for (const auto& unit : design_.units) {
    const auto prefix = effective_library(unit.library) + "::" + unit.name;
    for (const auto& alias : unit.type_aliases) {
      class_aliases_[prefix + "::" + alias.name] = &alias.type;
    }
  }
  for (auto& declaration : design_.systemverilog_classes) {
    collect(declaration, nullptr);
  }
  for (auto& unit : design_.units) {
    for (auto& declaration : unit.systemverilog_classes) {
      collect(declaration, &unit);
    }
  }
}
void Resolver::run() {
  for (auto& declaration : design_.systemverilog_classes) {
    Scope scope{
        effective_library(declaration.library),
        declaration.compilation_unit_identity,
        declaration.canonical_identity,
        &declaration,
        {}};
    resolve_class(declaration, scope);
  }
  for (auto& unit : design_.units) {
    if (unit.language != Language::SystemVerilog2017) continue;
    Scope scope{
        effective_library(unit.library),
        unit.compilation_unit_identity,
        effective_library(unit.library) + "::" + unit.name,
        nullptr,
        {}};
    add_objects(scope, unit.parameters);
    add_objects(scope, unit.ports);
    add_objects(scope, unit.signals);
    add_objects(scope, unit.variables);
    for (auto& variable : unit.variables) {
      if (variable.initializer) {
        resolve_typed_expression(
            *variable.initializer, scope, variable.type);
      }
    }
    for (auto& declaration : unit.systemverilog_classes) {
      auto class_scope = scope;
      class_scope.lexical_identity = declaration.canonical_identity;
      class_scope.class_owner = &declaration;
      resolve_class(declaration, class_scope);
    }
    for (auto& function : unit.functions) {
      resolve_function(function, scope);
    }
    for (auto& task : unit.tasks) resolve_task(task, scope);
    for (auto& process : unit.processes) {
      auto process_scope = scope;
      add_objects(process_scope, process.variables);
      for (auto& variable : process.variables) {
        if (variable.initializer) {
          resolve_typed_expression(
              *variable.initializer, process_scope, variable.type);
        }
      }
      for (auto& function : process.functions) {
        resolve_function(function, process_scope);
      }
      for (auto& statement : process.statements) {
        resolve_statement(statement, process_scope, std::nullopt);
      }
    }
    for (auto& region : unit.generate_regions) {
      resolve_generate_body(region.then_body, scope);
      resolve_generate_body(region.else_body, scope);
      for (auto& alternative : region.alternatives) {
        resolve_generate_body(alternative.body, scope);
      }
    }
  }
  hydrate_task_bodies();
}
void Resolver::collect(
    SystemVerilogClassDeclaration& declaration,
    const DesignUnit* unit) {
  classes_[declaration.canonical_identity] = &declaration;
  class_units_[declaration.canonical_identity] = unit;
  for (const auto& alias : declaration.type_aliases) {
    class_aliases_[declaration.canonical_identity + "::" + alias.name] =
        &alias.type;
  }
  for (auto& method : declaration.methods) {
    methods_[method.canonical_identity] = &method;
    method_owners_[method.canonical_identity] = &declaration;
  }
  for (auto& nested : declaration.nested_classes) {
    collect(nested, unit);
  }
}
void Resolver::append_lexical_type_aliases(
    Statement& statement,
    const SystemVerilogClassMethod& method,
    const SystemVerilogClassDeclaration& owner) const {
  const auto unit = class_units_.find(owner.canonical_identity);
  if (unit == class_units_.end() || unit->second == nullptr) {
    return;
  }
  std::set<std::string> required;
  const auto collect_type = [&](const auto& self, const Type& type) -> void {
    if (!type.named_type.empty()) {
      required.insert(type.named_type);
    }
    for (const auto& member : type.packed_members) {
      for (const auto& nested : member.nested_types) {
        self(self, nested);
      }
    }
    if (type.systemverilog_container) {
      for (const auto& element :
           type.systemverilog_container->element_types) {
        self(self, element);
      }
      if (type.systemverilog_container->associative_index_type) {
        self(
            self,
            *type.systemverilog_container->associative_index_type);
      }
    }
    for (const auto& actual :
         type.systemverilog_class_parameter_actuals) {
      if (actual.type_actual) {
        self(self, *actual.type_actual);
      }
    }
  };
  const auto collect_statements =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& nested_statement : statements) {
          for (const auto& alias : nested_statement.type_aliases) {
            collect_type(collect_type, alias.type);
          }
          for (const auto& declaration :
               nested_statement.declarations) {
            collect_type(collect_type, declaration.type);
          }
          for (const auto& argument :
               nested_statement.class_method_arguments) {
            collect_type(collect_type, argument.type);
          }
          self(self, nested_statement.loop_updates);
          self(self, nested_statement.statements);
          self(self, nested_statement.else_statements);
          for (const auto& alternative :
               nested_statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  collect_type(collect_type, method.return_type);
  for (const auto& argument : method.arguments) {
    collect_type(collect_type, argument.type);
  }
  for (const auto& variable : method.variables) {
    collect_type(collect_type, variable.type);
  }
  for (const auto& alias : method.type_aliases) {
    collect_type(collect_type, alias.type);
  }
  collect_statements(collect_statements, method.statements);

  std::map<std::string, const TypeAliasDeclaration*, std::less<>> aliases;
  for (const auto& alias : unit->second->type_aliases) {
    aliases.emplace(alias.name, &alias);
  }
  const auto collect_class_aliases =
      [&](const auto& self,
          const SystemVerilogClassDeclaration& declaration,
          const std::string& lexical_name) -> void {
        const auto scoped_name = lexical_name.empty()
            ? declaration.name
            : lexical_name + "::" + declaration.name;
        for (const auto& alias : declaration.type_aliases) {
          aliases.emplace(scoped_name + "::" + alias.name, &alias);
          aliases.emplace(
              declaration.canonical_identity + "::" + alias.name,
              &alias);
        }
        for (const auto& nested : declaration.nested_classes) {
          self(self, nested, scoped_name);
        }
      };
  for (const auto& declaration : unit->second->systemverilog_classes) {
    collect_class_aliases(collect_class_aliases, declaration, {});
  }
  std::set<std::string> processed;
  for (;;) {
    const auto next = std::ranges::find_if(
        required,
        [&](const std::string& name) {
          return !processed.contains(name);
        });
    if (next == required.end()) {
      break;
    }
    processed.insert(*next);
    const auto found = aliases.find(*next);
    if (found != aliases.end()) {
      statement.type_aliases.push_back(*found->second);
      collect_type(collect_type, found->second->type);
    }
  }
}
bool Resolver::hydrate_task_call(
    Statement& statement,
    std::set<std::string>& active,
    std::set<std::string>& expanded) {
  constexpr std::string_view instance_prefix{"@sv-task:"};
  constexpr std::string_view static_prefix{"@sv-static-task:"};
  std::string_view canonical;
  if (statement.kind == StatementKind::TaskCall
      && statement.task_name.starts_with(instance_prefix)) {
    canonical = std::string_view{statement.task_name}.substr(
        instance_prefix.size());
  } else if (
      statement.kind == StatementKind::TaskCall
      && statement.task_name.starts_with(static_prefix)) {
    canonical = std::string_view{statement.task_name}.substr(
        static_prefix.size());
  } else {
    return false;
  }
  const std::string canonical_identity{canonical};
  const auto found = methods_.find(canonical_identity);
  if (found == methods_.end()) {
    return false;
  }
  const auto& method = *found->second;
  statement.class_method_arguments = method.arguments;
  auto call_site_aliases = std::move(statement.type_aliases);
  statement.type_aliases.clear();
  statement.declarations.clear();
  statement.statements.clear();
  if (active.contains(canonical_identity)
      || !expanded.insert(canonical_identity).second) {
    return true;
  }
  active.insert(canonical_identity);
  if (const auto owner = method_owners_.find(canonical_identity);
      owner != method_owners_.end()) {
    append_lexical_type_aliases(
        statement, method, *owner->second);
    for (const auto& parameter : owner->second->parameters) {
      if (parameter.kind == ParameterKind::Type
          && parameter.default_type) {
          statement.type_aliases.push_back(TypeAliasDeclaration {
              parameter.name,
              *parameter.default_type,
              parameter.span,
              { },
              TypeDeclarationKind::SystemVerilogTypedef,
              { },
              { },
              { },
              false });
      }
    }
    statement.type_aliases.insert(
        statement.type_aliases.end(),
        owner->second->type_aliases.begin(),
        owner->second->type_aliases.end());
  }
  statement.type_aliases.insert(
      statement.type_aliases.end(),
      method.type_aliases.begin(),
      method.type_aliases.end());
  statement.type_aliases.insert(
      statement.type_aliases.end(),
      std::make_move_iterator(call_site_aliases.begin()),
      std::make_move_iterator(call_site_aliases.end()));
  statement.declarations = method.variables;
  statement.statements = method.statements;
  hydrate_statements(statement.statements, active, expanded);
  active.erase(canonical_identity);
  return true;
}
void Resolver::hydrate_statements(
    std::vector<Statement>& statements,
    std::set<std::string>& active,
    std::set<std::string>& expanded) {
  for (auto& statement : statements) {
    if (hydrate_task_call(statement, active, expanded)) {
      continue;
    }
    hydrate_statements(statement.loop_updates, active, expanded);
    hydrate_statements(statement.statements, active, expanded);
    hydrate_statements(statement.else_statements, active, expanded);
    for (auto& alternative : statement.case_alternatives) {
      hydrate_statements(alternative.statements, active, expanded);
    }
  }
}
void Resolver::hydrate_task_bodies() {
  for (auto& unit : design_.units) {
    if (unit.language != Language::SystemVerilog2017) {
      continue;
    }
    for (auto& function : unit.functions) {
      std::set<std::string> active;
      std::set<std::string> expanded;
      hydrate_statements(function.statements, active, expanded);
    }
    for (auto& task : unit.tasks) {
      std::set<std::string> active;
      std::set<std::string> expanded;
      hydrate_statements(task.statements, active, expanded);
    }
    for (auto& process : unit.processes) {
      std::set<std::string> active;
      std::set<std::string> expanded;
      hydrate_statements(process.statements, active, expanded);
    }
  }
}
void Resolver::resolve_generate_body(GenerateBody& body, const Scope& inherited) {
  auto scope = inherited;
  add_objects(scope, body.signals);
  add_objects(scope, body.variables);
  for (auto& variable : body.variables) {
    if (variable.initializer) {
      resolve_typed_expression(
          *variable.initializer, scope, variable.type);
    }
  }
  for (auto& function : body.functions) {
    resolve_function(function, scope);
  }
  for (auto& task : body.tasks) resolve_task(task, scope);
  for (auto& process : body.processes) {
    auto process_scope = scope;
    add_objects(process_scope, process.variables);
    for (auto& variable : process.variables) {
      if (variable.initializer) {
        resolve_typed_expression(
            *variable.initializer, process_scope, variable.type);
      }
    }
    for (auto& function : process.functions) {
      resolve_function(function, process_scope);
    }
    for (auto& statement : process.statements) {
      resolve_statement(statement, process_scope, std::nullopt);
    }
  }
  for (auto& statement : body.concurrent_statements) {
    resolve_statement(statement, scope, std::nullopt);
  }
  for (auto& region : body.generate_regions) {
    resolve_generate_body(region.then_body, scope);
    resolve_generate_body(region.else_body, scope);
    for (auto& alternative : region.alternatives) {
      resolve_generate_body(alternative.body, scope);
    }
  }
}
[[nodiscard]] const SystemVerilogClassDeclaration* Resolver::find_class(
    const std::string_view identity) const {
  std::string current{identity};
  std::set<std::string> visited;
  while (visited.insert(current).second) {
    const auto found = classes_.find(current);
    if (found != classes_.end()
        && !found->second->is_forward_declaration) {
      return found->second;
    }
    const auto alias = class_aliases_.find(current);
    if (alias == class_aliases_.end()) {
      return found == classes_.end() ? nullptr : found->second;
    }
    const auto target = class_identity(*alias->second);
    if (!target) {
      return found == classes_.end() ? nullptr : found->second;
    }
    current = *target;
  }
  return nullptr;
}
[[nodiscard]] const SystemVerilogClassDeclaration* Resolver::find_base_class(
    const SystemVerilogClassDeclaration& declaration) const {
  if (!declaration.base) return nullptr;
  if (!declaration.base->declaration_identity.empty()) {
    return find_class(declaration.base->declaration_identity);
  }
  const auto parameter = std::ranges::find(
      declaration.parameters,
      declaration.base->name,
      &ParameterDeclaration::name);
  if (parameter == declaration.parameters.end()
      || parameter->kind != ParameterKind::Type
      || !parameter->default_type) {
    return nullptr;
  }
  const auto identity = class_identity(*parameter->default_type);
  return identity ? find_class(*identity) : nullptr;
}
[[nodiscard]] bool Resolver::has_specialization_dependent_base(
    const std::string_view identity) const {
  std::set<std::string> visited;
  auto current = find_class(identity);
  while (current != nullptr
         && visited.insert(current->canonical_identity).second) {
    if (current->base) {
      const auto parameter = std::ranges::find(
          current->parameters,
          current->base->name,
          &ParameterDeclaration::name);
      if (parameter != current->parameters.end()
          && parameter->kind == ParameterKind::Type) {
        return true;
      }
    }
    current = find_base_class(*current);
  }
  return false;
}
[[nodiscard]] Type Resolver::resolve_alias_type(
    Type type, std::string lexical_identity) const {
  if ((class_identity(type) && !type.systemverilog_container)
      || type.named_type.empty()) {
    return type;
  }
  for (;;) {
    const auto alias = class_aliases_.find(
        lexical_identity + "::" + type.named_type);
    if (alias != class_aliases_.end()) {
      if (type.systemverilog_container) {
        auto& elements = type.systemverilog_container->element_types;
        if (elements.empty()) {
          elements.push_back(*alias->second);
        } else {
          elements.front() = *alias->second;
        }
        return type;
      }
      return *alias->second;
    }
    const auto separator = lexical_identity.rfind("::");
    if (separator == std::string::npos) return type;
    lexical_identity.resize(separator);
  }
}
[[nodiscard]] std::string_view Resolver::trim_type_spelling(
    std::string_view spelling) {
  while (!spelling.empty()
         && std::isspace(static_cast<unsigned char>(spelling.front()))) {
    spelling.remove_prefix(1U);
  }
  while (!spelling.empty()
         && std::isspace(static_cast<unsigned char>(spelling.back()))) {
    spelling.remove_suffix(1U);
  }
  return spelling;
}
[[nodiscard]] Type Resolver::selected_type_actual(
    const std::string_view source, const Scope& scope) const {
  const auto spelling = trim_type_spelling(source);
  if (spelling == "string") {
    return Type{ValueDomain::String, "string", std::nullopt, false};
  }
  if (spelling == "int" || spelling == "integer") {
    return Type{
        ValueDomain::Integer, std::string{spelling},
        PackedRange{31, 0, true}, true};
  }
  if (spelling == "bit" || spelling == "logic") {
    return Type{
        spelling == "bit" ? ValueDomain::Bit2 : ValueDomain::Logic4,
        std::string{spelling}, PackedRange{0, 0, true}, false};
  }
  const auto classes = resolve_class_name(spelling, scope);
  if (classes.size() == 1U) return class_type(*classes.front());
  Type retained;
  retained.spelling = std::string{spelling};
  retained.named_type = retained.spelling;
  return retained;
}
[[nodiscard]] std::vector<std::string_view>
Resolver::selected_parameter_actuals(const std::string_view spelling) {
  const auto hash = spelling.find('#');
  if (hash == std::string_view::npos) return {};
  const auto open = spelling.find('(', hash + 1U);
  if (open == std::string_view::npos) return {};
  std::vector<std::string_view> actuals;
  std::size_t begin = open + 1U;
  std::size_t depth = 1U;
  bool quoted = false;
  bool escaped = false;
  for (std::size_t index = begin; index < spelling.size(); ++index) {
    const auto character = spelling[index];
    if (quoted) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        quoted = false;
      }
      continue;
    }
    if (character == '"') {
      quoted = true;
    } else if (character == '(') {
      ++depth;
    } else if (character == ')') {
      if (--depth == 0U) {
        if (index > begin) {
          actuals.push_back(spelling.substr(begin, index - begin));
        }
        break;
      }
    } else if (character == ',' && depth == 1U) {
      actuals.push_back(spelling.substr(begin, index - begin));
      begin = index + 1U;
    }
  }
  return actuals;
}
[[nodiscard]] std::optional<ClassSelection> Resolver::resolve_class_selection(
    const std::string_view spelling, const Scope& scope) const {
  const auto declarations = resolve_class_name(spelling, scope);
  if (declarations.size() != 1U) return std::nullopt;
  ClassSelection selection{
      declarations.front(), class_type(*declarations.front())};

  const auto hash = spelling.find('#');
  const auto selected = spelling.rfind("::");
  if (selected != std::string_view::npos
      && (hash == std::string_view::npos || selected < hash)) {
    const auto owners = resolve_class_name(spelling.substr(0, selected), scope);
    if (owners.size() == 1U) {
      const auto alias = class_aliases_.find(
          owners.front()->canonical_identity + "::"
          + std::string{spelling.substr(selected + 2U)});
      if (alias != class_aliases_.end()) {
        const auto identity = class_identity(*alias->second);
        if (identity
            && *identity == selection.declaration->canonical_identity) {
          selection.type = *alias->second;
          for (std::size_t index = 0;
               index
                   < selection.type.systemverilog_class_parameter_actuals
                         .size();
               ++index) {
            auto& actual =
                selection.type.systemverilog_class_parameter_actuals[index];
            std::size_t formal_index = index;
            if (actual.name) {
              const auto formal = std::ranges::find(
                  selection.declaration->parameters,
                  *actual.name,
                  &ParameterDeclaration::name);
              if (formal == selection.declaration->parameters.end()) {
                continue;
              }
              formal_index = static_cast<std::size_t>(std::distance(
                  selection.declaration->parameters.begin(), formal));
            }
            if (formal_index >= selection.declaration->parameters.size()) {
              continue;
            }
            const auto& formal =
                selection.declaration->parameters[formal_index];
            if (formal.kind == ParameterKind::Type
                && !actual.type_actual
                && actual.value.valid()) {
              actual.type_actual = std::make_shared<Type>(
                  selected_type_actual(actual.value.text, scope));
            } else if (
                actual.type_actual
                && actual.type_actual->systemverilog_class_declaration
                       .empty()
                && !actual.type_actual->named_type.empty()) {
              *actual.type_actual = selected_type_actual(
                  actual.type_actual->named_type, scope);
            }
          }
          return selection;
        }
      }
    }
  }

  const auto actuals = selected_parameter_actuals(spelling);
  for (std::size_t index = 0;
       index < actuals.size()
       && index < selection.declaration->parameters.size(); ++index) {
    auto source = trim_type_spelling(actuals[index]);
    std::optional<std::string> name;
    if (source.starts_with('.')) {
      const auto open = source.find('(');
      if (open != std::string_view::npos && source.ends_with(')')) {
        name = std::string{
            trim_type_spelling(source.substr(1U, open - 1U))};
        source = trim_type_spelling(
            source.substr(open + 1U, source.size() - open - 2U));
      }
    }
    std::size_t formal_index = index;
    if (name) {
      const auto formal = std::ranges::find(
          selection.declaration->parameters,
          *name,
          &ParameterDeclaration::name);
      if (formal == selection.declaration->parameters.end()) continue;
      formal_index = static_cast<std::size_t>(std::distance(
          selection.declaration->parameters.begin(), formal));
    }
    const auto& formal = selection.declaration->parameters[formal_index];
    SystemVerilogClassTypeActual retained;
    retained.name = std::move(name);
    if (formal.kind == ParameterKind::Type) {
      retained.type_actual = std::make_shared<Type>(
          selected_type_actual(source, scope));
    } else {
      retained.value = Expression{
          source.starts_with('"')
              ? ExpressionKind::StringLiteral
              : ExpressionKind::IntegerLiteral,
          std::string{source}, {}, formal.span};
    }
    retained.span = formal.span;
    selection.type.systemverilog_class_parameter_actuals.push_back(
        std::move(retained));
  }
  return selection;
}
[[nodiscard]] Type Resolver::specialize_selected_type(
    Type type, const ClassSelection& selection) {
  std::map<std::string, Type, std::less<>> bindings;
  std::vector<bool> assigned(selection.declaration->parameters.size());
  std::size_t next_positional{};
  for (const auto& actual :
       selection.type.systemverilog_class_parameter_actuals) {
    std::size_t formal_index{};
    if (actual.name) {
      const auto formal = std::ranges::find(
          selection.declaration->parameters,
          *actual.name,
          &ParameterDeclaration::name);
      if (formal == selection.declaration->parameters.end()) continue;
      formal_index = static_cast<std::size_t>(std::distance(
          selection.declaration->parameters.begin(), formal));
    } else {
      while (next_positional < assigned.size()
             && assigned[next_positional]) {
        ++next_positional;
      }
      formal_index = next_positional;
    }
    if (formal_index >= selection.declaration->parameters.size()) continue;
    assigned[formal_index] = true;
    const auto& formal = selection.declaration->parameters[formal_index];
    if (formal.kind == ParameterKind::Type && actual.type_actual) {
      bindings.insert_or_assign(formal.name, *actual.type_actual);
    }
  }
  for (std::size_t index = 0;
       index < selection.declaration->parameters.size(); ++index) {
    const auto& formal = selection.declaration->parameters[index];
    if (!assigned[index] && formal.kind == ParameterKind::Type
        && formal.default_type) {
      bindings.insert_or_assign(formal.name, *formal.default_type);
    }
  }
  const auto substitute = [&](const auto& self, Type retained) -> Type {
    const auto outer_container = retained.systemverilog_container;
    if (const auto found = bindings.find(retained.named_type);
        found != bindings.end()) {
      retained = found->second;
      if (outer_container) retained.systemverilog_container = outer_container;
    }
    if (retained.systemverilog_container) {
      for (auto& element :
           retained.systemverilog_container->element_types) {
        element = self(self, std::move(element));
      }
    }
    for (auto& actual : retained.systemverilog_class_parameter_actuals) {
      if (actual.type_actual) {
        actual.type_actual = std::make_shared<Type>(
            self(self, std::move(*actual.type_actual)));
      }
    }
    return retained;
  };
  return substitute(substitute, std::move(type));
}
[[nodiscard]] Type Resolver::canonicalize_selected_type(
    Type type, const Scope& scope) const {
  const auto outer_container = type.systemverilog_container;
  if (type.systemverilog_class_declaration.empty()
      && !type.named_type.empty()) {
    const auto declarations = resolve_class_name(type.named_type, scope);
    if (declarations.size() == 1U) {
      type = class_type(*declarations.front());
      if (outer_container) type.systemverilog_container = outer_container;
    }
  }
  if (type.systemverilog_container) {
    for (auto& element : type.systemverilog_container->element_types) {
      element = canonicalize_selected_type(std::move(element), scope);
    }
  }
  return type;
}
[[nodiscard]] SystemVerilogClassMethod Resolver::specialize_selected_method(
    SystemVerilogClassMethod method,
    const ClassSelection& selection,
    const Scope& scope) const {
  method.return_type = canonicalize_selected_type(
      specialize_selected_type(
          std::move(method.return_type), selection),
      scope);
  for (auto& argument : method.arguments) {
    argument.type = canonicalize_selected_type(
        specialize_selected_type(
            std::move(argument.type), selection),
        scope);
  }
  return method;
}
[[nodiscard]] std::string Resolver::selected_uvm_type_identity(
    const ClassSelection& selection) {
  for (const auto& actual :
       selection.type.systemverilog_class_parameter_actuals) {
    if (!actual.type_actual) continue;
    if (!actual.type_actual->systemverilog_class_declaration.empty()) {
      return "class:"
          + actual.type_actual->systemverilog_class_declaration;
    }
    if (actual.type_actual->domain == ValueDomain::Integer
        && actual.type_actual->width() == 32U) {
      return "packed:int:32";
    }
    if (actual.type_actual->domain == ValueDomain::String) {
      return "string";
    }
    if (const auto width = actual.type_actual->width()) {
      return "packed:"
          + std::to_string(static_cast<unsigned>(
              actual.type_actual->domain))
          + ":" + std::to_string(*width);
    }
  }
  return {};
}
[[nodiscard]] std::string Resolver::selected_uvm_intrinsic(
    const ClassSelection& selection,
    const std::string_view method) {
  const auto type_identity = selected_uvm_type_identity(selection);
  if ((selection.declaration->name == "uvm_object_registry"
       || selection.declaration->name == "uvm_component_registry")
      && method == "create"
      && type_identity.starts_with("class:")) {
    return "@uvm-registry-create:" + type_identity.substr(6U);
  }
  if (selection.declaration->name == "uvm_config_db"
      && (method == "set" || method == "get" || method == "exists")
      && !type_identity.empty()) {
    return "@uvm-config-db-" + std::string{method}
        + ":" + type_identity;
  }
  return {};
}
[[nodiscard]] std::vector<const SystemVerilogClassDeclaration*>
Resolver::resolve_class_name(
    const std::string_view spelling, const Scope& scope) const {
  std::string normalized;
  normalized.reserve(spelling.size());
  for (std::size_t index = 0; index < spelling.size();) {
    if (spelling[index] != '#') {
      normalized.push_back(spelling[index++]);
      continue;
    }
    ++index;
    while (index < spelling.size()
           && std::isspace(
               static_cast<unsigned char>(spelling[index]))) {
      ++index;
    }
    if (index >= spelling.size() || spelling[index] != '(') {
      continue;
    }
    std::size_t depth = 1U;
    ++index;
    while (index < spelling.size() && depth != 0U) {
      if (spelling[index] == '(') {
        ++depth;
      } else if (spelling[index] == ')') {
        --depth;
      }
      ++index;
    }
  }
  std::vector<const SystemVerilogClassDeclaration*> matches;
  const auto append = [&](const auto& predicate) {
    for (const auto& [identity, declaration] : classes_) {
      if (predicate(identity, *declaration)
          && std::ranges::find(matches, declaration) == matches.end()) {
        matches.push_back(declaration);
      }
    }
  };
  if (const auto exact = find_class(normalized)) return {exact};
  if (const auto selected = normalized.rfind("::");
      selected != std::string::npos) {
    const auto owners = resolve_class_name(
        std::string_view{normalized}.substr(0, selected), scope);
    const auto alias_name = normalized.substr(selected + 2U);
    for (const auto* owner : owners) {
      const auto alias = class_aliases_.find(
          owner->canonical_identity + "::" + alias_name);
      if (alias == class_aliases_.end()) {
        continue;
      }
      const auto identity = class_identity(*alias->second);
      if (identity) {
        if (const auto declaration = find_class(*identity)) {
          matches.push_back(declaration);
        }
      }
    }
    if (!matches.empty()) {
      std::ranges::sort(matches, {}, [](const auto* declaration) {
        return declaration->canonical_identity;
      });
      matches.erase(
          std::unique(matches.begin(), matches.end()),
          matches.end());
      return matches;
    }
  }
  std::string lexical = scope.lexical_identity;
  for (;;) {
    if (const auto direct = find_class(
            lexical + "::" + normalized)) {
      return {direct};
    }
    const auto separator = lexical.rfind("::");
    if (separator == std::string::npos) break;
    lexical.resize(separator);
  }
  append([&](const std::string& identity, const auto& declaration) {
    if (declaration.library != scope.library) return false;
    if (normalized.find("::") != std::string::npos) {
      return identity.ends_with(normalized)
          && identity.size() > normalized.size()
          && identity[identity.size() - normalized.size() - 1U] == ':';
    }
    return declaration.name == normalized
        && declaration.compilation_unit_identity
            == scope.compilation_unit_identity;
  });
  std::ranges::sort(matches, {}, [](const auto* declaration) {
    return declaration->canonical_identity;
  });
  return matches;
}
[[nodiscard]] PropertyMatch Resolver::find_property(
    const std::string_view identity,
    const std::string_view name) const {
  std::set<std::string> visited;
  auto current = find_class(identity);
  while (current != nullptr
         && visited.insert(current->canonical_identity).second) {
    const auto property = std::ranges::find(
        current->properties,
        name,
        [](const SystemVerilogClassProperty& candidate) {
          return candidate.declaration.name;
        });
    if (property != current->properties.end()) {
      return {&*property, current};
    }
    current = find_base_class(*current);
  }
  return {};
}
[[nodiscard]] ConstraintMatch Resolver::find_constraint(
    const std::string_view identity,
    const std::string_view name) const {
  std::set<std::string> visited;
  auto current = find_class(identity);
  while (current != nullptr
         && visited.insert(current->canonical_identity).second) {
    const auto constraint = std::ranges::find(
        current->constraints, name,
        &SystemVerilogClassConstraint::name);
    if (constraint != current->constraints.end()) {
      return {&*constraint, current};
    }
    current = find_base_class(*current);
  }
  return {};
}
[[nodiscard]] bool Resolver::can_access(
    const SystemVerilogClassVisibility visibility,
    const SystemVerilogClassDeclaration& owner,
    const Scope& scope) const {
  if (visibility == SystemVerilogClassVisibility::Public) return true;
  if (scope.class_owner == nullptr) return false;
  if (scope.class_owner->canonical_identity == owner.canonical_identity) {
    return true;
  }
  if (visibility == SystemVerilogClassVisibility::Local) return false;
  std::set<std::string> visited;
  auto current = scope.class_owner;
  while (current != nullptr
         && visited.insert(current->canonical_identity).second) {
    if (current->canonical_identity == owner.canonical_identity) return true;
    current = find_base_class(*current);
  }
  return false;
}
void Resolver::retain_result_type(
    Expression& expression, const Type& type) {
  const bool class_handle =
      !type.systemverilog_class_declaration.empty();
  expression.call_result_width = class_handle
      ? 64U
      : type.width().value_or(0);
  expression.call_result_domain = class_handle
      ? ValueDomain::Bit2
      : type.domain;
  expression.call_result_signed = class_handle
      ? false
      : type.is_signed;
  expression.systemverilog_scalar_kind = type.systemverilog_scalar;
}
[[nodiscard]] std::vector<MethodMatch> Resolver::find_methods(
    const std::string_view identity,
    const std::string_view name,
    const SystemVerilogClassMethodKind kind) const {
  std::set<std::string> visited;
  std::vector<MethodMatch> matches;
  auto current = find_class(identity);
  while (current != nullptr
         && visited.insert(current->canonical_identity).second) {
    for (const auto& method : current->methods) {
      if (method.name == name && method.kind == kind) {
        matches.push_back({&method, current});
      }
    }
    if (!matches.empty() || kind == SystemVerilogClassMethodKind::Constructor) {
      break;
    }
    current = find_base_class(*current);
  }
  return matches;
}
[[nodiscard]] std::optional<Type> Resolver::resolve_identifier(
    Expression& expression, const Scope& scope) {
  if (expression.text == "this" && scope.class_owner != nullptr) {
    auto type = class_type(*scope.class_owner);
    expression.nominal_type = type.systemverilog_class_declaration;
    return type;
  }
  if (expression.text == "super" && scope.class_owner != nullptr
      && scope.class_owner->base) {
    if (const auto base = find_base_class(*scope.class_owner)) {
      auto type = class_type(*base);
      expression.nominal_type = type.systemverilog_class_declaration;
      return type;
    }
  }
  if (const auto found = scope.objects.find(expression.text);
      found != scope.objects.end()) {
    const auto type = resolve_alias_type(
        found->second, scope.lexical_identity);
    if (const auto identity = class_identity(type)) {
      expression.nominal_type = *identity;
    }
    expression.systemverilog_scalar_kind =
        type.systemverilog_scalar;
    return type;
  }

  const auto dot = expression.text.rfind('.');
  if (dot != std::string::npos) {
    Expression receiver{
        ExpressionKind::Identifier,
        expression.text.substr(0, dot),
        {},
        expression.span};
    const auto receiver_type = resolve_expression(receiver, scope);
    if (!receiver_type || !class_identity(*receiver_type)) return std::nullopt;
    const auto member = expression.text.substr(dot + 1U);
    const auto match = find_property(*class_identity(*receiver_type), member);
    if (!match.property) {
      const auto method = select_method(
          *class_identity(*receiver_type),
          member,
          SystemVerilogClassMethodKind::Function,
          0U,
          expression.span);
      if (method && method->method) {
        expression.kind = ExpressionKind::Call;
        expression.text = "@sv-method:"
            + method->method->canonical_identity;
        expression.operands = {std::move(receiver)};
        retain_call_profile(expression, *method->method, true);
        const auto type = resolve_alias_type(
            method->method->return_type,
            method->owner->canonical_identity);
        retain_result_type(expression, type);
        if (const auto identity = class_identity(type)) {
          expression.nominal_type = *identity;
        }
        return type;
      }
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-012",
          "class '" + *class_identity(*receiver_type)
              + "' has no visible property '" + member + "'",
          expression.span);
      return std::nullopt;
    }
    expression.kind = ExpressionKind::Call;
    const auto type = resolve_alias_type(
        match.property->declaration.type,
        match.owner->canonical_identity);
    expression.text = (type.systemverilog_container
            ? "@sv-container-property:"
            : "@sv-property:")
        + match.owner->canonical_identity + "::" + member;
    expression.operands = {std::move(receiver)};
    retain_result_type(expression, type);
    if (const auto identity = class_identity(type)) {
      expression.nominal_type = *identity;
    }
    return type;
  }

  const auto selected = expression.text.rfind("::");
  if (selected != std::string::npos) {
    const auto owner_name = expression.text.substr(0, selected);
    const auto member = expression.text.substr(selected + 2U);
    const auto owners = resolve_class_name(owner_name, scope);
    if (owners.size() == 1U) {
      const auto match = find_property(
          owners.front()->canonical_identity, member);
      if (match.property && match.property->is_static) {
        const auto type = resolve_alias_type(
            match.property->declaration.type,
            match.owner->canonical_identity);
        expression.kind = ExpressionKind::Call;
        expression.text =
            (type.systemverilog_container
                 ? "@sv-static-container-property:"
                 : "@sv-static-property:")
            + match.owner->canonical_identity + "::" + member;
        retain_result_type(expression, type);
        if (const auto identity = class_identity(type)) {
          expression.nominal_type = *identity;
        }
        return type;
      }
    }
  }

  if (scope.class_owner != nullptr) {
    const auto match = find_property(
        scope.class_owner->canonical_identity, expression.text);
    if (match.property) {
      const auto type = resolve_alias_type(
          match.property->declaration.type,
          match.owner->canonical_identity);
      Expression receiver{
          ExpressionKind::Identifier, "this", {}, expression.span};
      receiver.nominal_type = scope.class_owner->canonical_identity;
      expression.kind = ExpressionKind::Call;
      expression.text = (match.property->is_static
          ? (type.systemverilog_container
                 ? "@sv-static-container-property:"
                 : "@sv-static-property:")
          : (type.systemverilog_container
                 ? "@sv-container-property:"
                 : "@sv-property:"))
          + match.owner->canonical_identity + "::"
          + match.property->declaration.name;
      if (!match.property->is_static) {
        expression.operands = {std::move(receiver)};
      }
      retain_result_type(expression, type);
      if (const auto identity = class_identity(type)) {
        expression.nominal_type = *identity;
      }
      return type;
    }
  }
  return std::nullopt;
}
} // namespace fsim::frontend::class_resolution_detail

// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

namespace {

struct Scope {
  std::string library;
  std::string compilation_unit_identity;
  std::string lexical_identity;
  const SystemVerilogClassDeclaration* class_owner{};
  std::map<std::string, Type, std::less<>> objects;
};

struct PropertyMatch {
  const SystemVerilogClassProperty* property{};
  const SystemVerilogClassDeclaration* owner{};
};

struct ClassSelection {
  const SystemVerilogClassDeclaration* declaration{};
  Type type;
};

struct ConstraintMatch {
  const SystemVerilogClassConstraint* constraint{};
  const SystemVerilogClassDeclaration* owner{};
};

struct MethodMatch {
  const SystemVerilogClassMethod* method{};
  const SystemVerilogClassDeclaration* owner{};
};

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

class Resolver final {
 public:
  Resolver(ParsedDesign& design, std::vector<Diagnostic>& diagnostics)
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

  void run() {
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

 private:
  void collect(
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

  void append_lexical_type_aliases(
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

  bool hydrate_task_call(
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
                { } });
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

  void hydrate_statements(
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

  void hydrate_task_bodies() {
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

  template <typename Declaration>
  static void add_objects(
      Scope& scope, const std::vector<Declaration>& declarations) {
    for (const auto& declaration : declarations) {
      scope.objects[declaration.name] = declaration.type;
    }
  }

  void resolve_generate_body(GenerateBody& body, const Scope& inherited) {
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

  [[nodiscard]] const SystemVerilogClassDeclaration* find_class(
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

  [[nodiscard]] const SystemVerilogClassDeclaration* find_base_class(
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

  [[nodiscard]] bool has_specialization_dependent_base(
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

  [[nodiscard]] Type resolve_alias_type(
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

  [[nodiscard]] static std::string_view trim_type_spelling(
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

  [[nodiscard]] Type selected_type_actual(
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

  [[nodiscard]] static std::vector<std::string_view>
  selected_parameter_actuals(const std::string_view spelling) {
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

  [[nodiscard]] std::optional<ClassSelection> resolve_class_selection(
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

  [[nodiscard]] static Type specialize_selected_type(
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

  [[nodiscard]] Type canonicalize_selected_type(
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

  [[nodiscard]] SystemVerilogClassMethod specialize_selected_method(
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

  [[nodiscard]] static std::string selected_uvm_type_identity(
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

  [[nodiscard]] static std::string selected_uvm_intrinsic(
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
  resolve_class_name(
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

  [[nodiscard]] PropertyMatch find_property(
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

  [[nodiscard]] ConstraintMatch find_constraint(
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

  [[nodiscard]] bool can_access(
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

  static void retain_result_type(
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

  [[nodiscard]] std::vector<MethodMatch> find_methods(
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

  [[nodiscard]] std::optional<Type> resolve_identifier(
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

  [[nodiscard]] std::optional<MethodMatch> select_method(
      const std::string_view identity,
      const std::string_view name,
      const SystemVerilogClassMethodKind kind,
      const std::size_t actual_count,
      const SourceSpan& span) {
    auto matches = find_methods(identity, name, kind);
    std::erase_if(matches, [&](const MethodMatch& match) {
      const auto required = static_cast<std::size_t>(std::ranges::count_if(
          match.method->arguments,
          [](const FunctionArgument& argument) {
            return !argument.default_value.has_value();
          }));
      return actual_count < required
          || actual_count > match.method->arguments.size();
    });
    if (matches.size() == 1U) return matches.front();
    if (matches.empty()
        && kind != SystemVerilogClassMethodKind::Constructor
        && has_specialization_dependent_base(identity)) {
      return std::nullopt;
    }
    if (matches.empty()
        && kind == SystemVerilogClassMethodKind::Constructor) {
      if (actual_count == 0U) return MethodMatch{nullptr, find_class(identity)};
    }
    diagnose(
        diagnostics_,
        "FSIM-SV-CLASS-013",
        "class '" + std::string{identity} + "' has "
            + (matches.empty() ? "no compatible" : "ambiguous")
            + " method '" + std::string{name} + "'",
        span);
    return std::nullopt;
  }

  static void retain_call_profile(
      Expression& expression,
      const SystemVerilogClassMethod& method,
      const bool has_receiver) {
    expression.call_argument_directions.clear();
    std::vector<bool> associated(method.arguments.size());
    std::size_t next_positional{};
    if (has_receiver) {
      expression.call_argument_directions.push_back(PortDirection::Input);
    }
    for (std::size_t operand = has_receiver ? 1U : 0U;
         operand < expression.operands.size(); ++operand) {
      const auto name = operand < expression.call_argument_names.size()
          ? std::string_view{expression.call_argument_names[operand]}
          : std::string_view{};
      std::size_t formal{};
      if (name.empty()) {
        while (next_positional < associated.size()
               && associated[next_positional]) {
          ++next_positional;
        }
        formal = next_positional;
      } else {
        const auto found = std::ranges::find(
            method.arguments, name, &FunctionArgument::name);
        formal = found == method.arguments.end()
            ? method.arguments.size()
            : static_cast<std::size_t>(
                  std::distance(method.arguments.begin(), found));
      }
      if (formal < associated.size()) {
        associated[formal] = true;
        expression.call_argument_directions.push_back(
            method.arguments[formal].direction);
      } else {
        expression.call_argument_directions.push_back(
            PortDirection::Unknown);
      }
    }
    retain_result_type(expression, method.return_type);
  }

  static void retain_task_profile(
      Statement& statement,
      const SystemVerilogClassMethod& method,
      const SystemVerilogClassDeclaration* owner = nullptr,
      const Type* receiver_type = nullptr) {
    statement.class_method_arguments = method.arguments;
    statement.type_aliases.clear();
    if (owner != nullptr && receiver_type != nullptr) {
      const auto& actuals =
          receiver_type->systemverilog_class_parameter_actuals;
      for (std::size_t actual_index = 0;
           actual_index < actuals.size(); ++actual_index) {
        const auto& actual = actuals[actual_index];
        if (!actual.type_actual) {
          continue;
        }
        std::size_t formal_index = actual_index;
        if (actual.name) {
          const auto found = std::ranges::find(
              owner->parameters,
              *actual.name,
              &ParameterDeclaration::name);
          if (found == owner->parameters.end()) {
            continue;
          }
          formal_index = static_cast<std::size_t>(
              std::distance(owner->parameters.begin(), found));
        }
        if (formal_index >= owner->parameters.size()) {
          continue;
        }
        const auto& formal = owner->parameters[formal_index];
        if (formal.kind != ParameterKind::Type) {
          continue;
        }
        statement.type_aliases.push_back(TypeAliasDeclaration {
            formal.name,
            *actual.type_actual,
            actual.span,
            { },
            TypeDeclarationKind::SystemVerilogTypedef,
            { } });
      }
    }
    statement.declarations.clear();
    statement.statements.clear();
  }

  [[nodiscard]] std::optional<Type> resolve_call(
      Expression& expression,
      const Scope& scope,
      const std::optional<std::string>& expected_class,
      const SystemVerilogScalarKind expected_scalar) {
    if (expression.text == "@sv-null") {
      if (expected_scalar == SystemVerilogScalarKind::Chandle
          || expression.systemverilog_scalar_kind
              == SystemVerilogScalarKind::Chandle) {
        expression.systemverilog_scalar_kind =
            SystemVerilogScalarKind::Chandle;
        return chandle_type();
      }
      auto contextual_class = expected_class;
      if (!contextual_class && !expression.nominal_type.empty()) {
        contextual_class = expression.nominal_type;
      }
      expression.nominal_type = contextual_class.value_or(std::string{});
      if (contextual_class) {
        if (const auto declaration = find_class(*contextual_class)) {
          auto type = class_type(*declaration);
          retain_result_type(expression, type);
          return type;
        }
      }
      return std::nullopt;
    }
    if (expression.text == "@sv-cast:chandle") {
      if (expression.operands.size() != 1U) {
        diagnose(
            diagnostics_, "FSIM-SV-SEM-174",
            "a chandle cast requires exactly one chandle or null operand",
            expression.span);
        return std::nullopt;
      }
      const auto operand = resolve_expression(
          expression.operands.front(), scope, std::nullopt,
          SystemVerilogScalarKind::Chandle);
      if (!chandle(operand)) {
        diagnose(
            diagnostics_, "FSIM-SV-SEM-174",
            "a chandle cast requires a chandle or null operand",
            expression.span);
        return std::nullopt;
      }
      expression.systemverilog_scalar_kind =
          SystemVerilogScalarKind::Chandle;
      return chandle_type();
    }
    if (expression.text == "@sv-cast:void"
        && expression.operands.size() == 1U) {
      resolve_expression(expression.operands.front(), scope);
      expression.systemverilog_scalar_kind =
          SystemVerilogScalarKind::None;
      return Type{};
    }
    if (expression.text.starts_with("@sv-cast:")
        && expression.operands.size() == 1U) {
      const auto operand = resolve_expression(
          expression.operands.front(), scope);
      if (chandle(operand)) {
        diagnose(
            diagnostics_, "FSIM-SV-SEM-174",
            "chandle cannot be cast to a numeric or aggregate type",
            expression.span);
      }
      return std::nullopt;
    }
    if (expression.text == "process::self") {
      for (auto& operand : expression.operands) {
        resolve_expression(operand, scope);
      }
      expression.systemverilog_scalar_kind =
          SystemVerilogScalarKind::Chandle;
      return chandle_type();
    }
    if (expression.text == "@sv-new") {
      if (!expected_class) {
        diagnose(
            diagnostics_,
            "FSIM-SV-CLASS-014",
            "class construction requires a destination class-handle type",
            expression.span);
        return std::nullopt;
      }
      const auto expected_declaration = find_class(*expected_class);
      if (expected_declaration == nullptr) {
        expression.text = "@sv-new:" + *expected_class;
        expression.nominal_type = *expected_class;
        for (auto& operand : expression.operands) {
          resolve_expression(operand, scope);
        }
        Type deferred;
        deferred.spelling = *expected_class;
        deferred.named_type = *expected_class;
        return deferred;
      }
      const auto method = select_method(
          *expected_class,
          "new",
          SystemVerilogClassMethodKind::Constructor,
          expression.operands.size(),
          expression.span);
      if (!method) return std::nullopt;
      expression.text = "@sv-new:" + *expected_class;
      expression.nominal_type = *expected_class;
      for (auto& operand : expression.operands) {
        resolve_expression(operand, scope);
      }
      return class_type(*find_class(*expected_class));
    }
    if (expression.text == "@sv-dollar-cast") {
      std::vector<std::optional<Type>> operand_types;
      operand_types.reserve(expression.operands.size());
      for (auto& operand : expression.operands) {
        operand_types.push_back(resolve_expression(operand, scope));
      }
      if (expression.operands.size() != 2U
          || !expression.operands.front().valid()) {
        diagnose(
            diagnostics_,
            "FSIM-SV-CLASS-015",
            "$cast requires a writable destination and source",
            expression.span);
        return std::nullopt;
      }
      if (operand_types.front()
          && class_identity(*operand_types.front())) {
        expression.text += ":"
            + *class_identity(*operand_types.front());
      }
      return Type{ValueDomain::Logic4, "bit", PackedRange{0, 0, true}, false};
    }
    constexpr std::string_view selected_member_prefix{"@sv-select:"};
    if (expression.text.starts_with(selected_member_prefix)
        && expression.operands.size() == 1U) {
      const auto receiver_type = resolve_expression(
          expression.operands.front(), scope);
      const auto identity = receiver_type
          ? class_identity(*receiver_type) : std::nullopt;
      const auto member = expression.text.substr(selected_member_prefix.size());
      if (!identity) {
        auto receiver = std::move(expression.operands.front());
        receiver.text += "." + std::string{member};
        receiver.span = expression.span;
        expression = std::move(receiver);
        return receiver_type;
      }
      const auto match = find_property(*identity, member);
      if (!match.property) {
        const auto methods = find_methods(
            *identity,
            member,
            SystemVerilogClassMethodKind::Function);
        if (!methods.empty()) {
          const auto method = select_method(
              *identity,
              member,
              SystemVerilogClassMethodKind::Function,
              0U,
              expression.span);
          if (!method || !method->method) return std::nullopt;
          expression.text = "@sv-method:"
              + method->method->canonical_identity;
          retain_call_profile(expression, *method->method, true);
          if (const auto result_identity = class_identity(
                  method->method->return_type)) {
            expression.nominal_type = *result_identity;
          }
          return method->method->return_type;
        }
        diagnose(
            diagnostics_,
            "FSIM-SV-CLASS-012",
            "class '" + *identity + "' has no visible property '"
                + std::string{member} + "'",
            expression.span);
        return std::nullopt;
      }
      expression.text =
          (match.property->declaration.type.systemverilog_container
               ? "@sv-container-property:"
               : "@sv-property:")
          + match.owner->canonical_identity + "::" + std::string{member};
      retain_result_type(expression, match.property->declaration.type);
      if (const auto property_identity = class_identity(
              match.property->declaration.type)) {
        expression.nominal_type = *property_identity;
      }
      return match.property->declaration.type;
    }
    if (expression.text.starts_with("@sv-")) {
      for (auto& operand : expression.operands) {
        resolve_expression(operand, scope);
      }
      if (expression.systemverilog_scalar_kind
          == SystemVerilogScalarKind::Chandle) {
        return chandle_type();
      }
      if (!expression.nominal_type.empty()) {
        if (const auto declaration = find_class(expression.nominal_type)) {
          return class_type(*declaration);
        }
      }
      return std::nullopt;
    }

    if (expression.text.starts_with('.') && !expression.operands.empty()) {
      const auto built_in_mode = expression.text == ".rand_mode"
          || expression.text == ".constraint_mode";
      if (built_in_mode
          && expression.operands.front().kind == ExpressionKind::Identifier) {
        const auto selected = expression.operands.front().text.rfind('.');
        if (selected != std::string::npos) {
          Expression receiver{
              ExpressionKind::Identifier,
              expression.operands.front().text.substr(0, selected),
              {}, expression.operands.front().span};
          const auto receiver_type = resolve_expression(receiver, scope);
          const auto identity = receiver_type
              ? class_identity(*receiver_type) : std::nullopt;
          const auto member = expression.operands.front().text.substr(
              selected + 1U);
          std::string canonical;
          SystemVerilogClassVisibility visibility{
              SystemVerilogClassVisibility::Public};
          const SystemVerilogClassDeclaration* owner{};
          if (identity && expression.text == ".rand_mode") {
            const auto property = find_property(*identity, member);
            if (property.property
                && (property.property->is_rand
                    || property.property->is_randc)) {
              canonical = property.owner->canonical_identity + "::"
                  + property.property->declaration.name;
              visibility = property.property->visibility;
              owner = property.owner;
            }
          } else if (identity) {
            const auto constraint = find_constraint(*identity, member);
            if (constraint.constraint) {
              canonical = constraint.constraint->canonical_identity;
              visibility = constraint.constraint->visibility;
              owner = constraint.owner;
            }
          }
          if (canonical.empty() || expression.operands.size() > 2U) {
            diagnose(
                diagnostics_, "FSIM-SV-CLASS-020",
                "randomization mode call does not select a visible random "
                "property or constraint block",
                expression.span);
            return std::nullopt;
          }
          if (owner != nullptr && !can_access(visibility, *owner, scope)) {
            diagnose(
                diagnostics_, "FSIM-SV-CLASS-021",
                "randomization mode call cannot access a nonpublic member",
                expression.span);
            return std::nullopt;
          }
          expression.operands.front() = std::move(receiver);
          expression.text = "@sv-method:@builtin-"
              + std::string{expression.text == ".rand_mode"
                    ? "rand-mode:" : "constraint-mode:"}
              + canonical;
          expression.call_argument_names.resize(expression.operands.size());
          expression.call_argument_directions.assign(
              expression.operands.size(), PortDirection::Input);
          for (auto& operand : expression.operands | std::views::drop(1)) {
            resolve_expression(operand, scope);
          }
          Type result{
              ValueDomain::Integer, "int",
              PackedRange{31, 0, true}, true};
          retain_result_type(expression, result);
          return result;
        }
      }
      auto receiver_type = resolve_expression(expression.operands.front(), scope);
      constexpr std::string_view container_property_prefix{
          "@sv-container-property:"};
      constexpr std::string_view static_container_property_prefix{
          "@sv-static-container-property:"};
      if (receiver_type && receiver_type->systemverilog_container) {
        const auto method = expression.text.substr(1U);
        const bool instance_property =
            expression.operands.front().kind == ExpressionKind::Call
            && expression.operands.front().text.starts_with(
                container_property_prefix)
            && expression.operands.front().operands.size() == 1U;
        const bool static_property =
            expression.operands.front().kind == ExpressionKind::Call
            && expression.operands.front().text.starts_with(
                static_container_property_prefix);
        if (instance_property || static_property) {
          const auto prefix = instance_property
              ? container_property_prefix
              : static_container_property_prefix;
          const auto property =
              expression.operands.front().text.substr(prefix.size());
          std::vector<Expression> operands;
          if (instance_property) {
          auto receiver = std::move(
              expression.operands.front().operands.front());
          operands.push_back(std::move(receiver));
          }
          for (auto& operand : expression.operands | std::views::drop(1)) {
            resolve_expression(
                operand,
                scope,
                receiver_type->systemverilog_class_declaration);
            operands.push_back(std::move(operand));
          }
          expression.text = "@sv-container-method:" + method + ":"
              + property;
          expression.operands = std::move(operands);
        } else {
          for (auto& operand :
               expression.operands | std::views::drop(1)) {
            resolve_expression(operand, scope);
          }
        }
          if (method == "pop_front" || method == "pop_back") {
            auto element = *receiver_type;
            element.systemverilog_container.reset();
            retain_result_type(expression, element);
            expression.nominal_type =
                element.systemverilog_class_declaration;
            return element;
          }
          if (method == "size" || method == "num"
              || method == "exists" || method == "first"
              || method == "last" || method == "next"
              || method == "prev") {
            Type result{
                ValueDomain::Integer,
                "int",
                PackedRange{31, 0, true},
                true};
            retain_result_type(expression, result);
            return result;
          }
          if (method == "find" || method == "find_index"
              || method == "find_first"
              || method == "find_first_index"
              || method == "find_last"
              || method == "find_last_index"
              || method == "min" || method == "max"
              || method == "unique"
              || method == "unique_index") {
            retain_result_type(expression, *receiver_type);
            return receiver_type;
          }
          return std::nullopt;
      }
      if (!receiver_type || !class_identity(*receiver_type)) {
        for (auto& operand : expression.operands | std::views::drop(1)) {
          resolve_expression(operand, scope);
        }
        return std::nullopt;
      }
      const auto name = expression.text.substr(1U);
      if (name == "randomize") {
        const auto receiver_identity = *class_identity(*receiver_type);
        expression.text = "@sv-method:@builtin-randomize";
        expression.call_argument_names.resize(expression.operands.size());
        expression.call_argument_directions.assign(
            expression.operands.size(), PortDirection::Input);
        for (std::size_t index = 1; index < expression.operands.size();
             ++index) {
          const auto selected_name = expression.operands[index].text;
          const auto selected =
              expression.operands[index].kind == ExpressionKind::Identifier
                  ? find_property(receiver_identity, selected_name)
                  : PropertyMatch{};
          if (!selected.property
              || (!selected.property->is_rand
                  && !selected.property->is_randc)) {
            diagnose(
                diagnostics_,
                "FSIM-SV-CLASS-019",
                "object randomize variable list requires a rand or randc "
                "property of class '" + receiver_identity + "'",
                expression.operands[index].span);
            continue;
          }
          expression.call_argument_names[index] =
              selected.owner->canonical_identity + "::"
              + selected.property->declaration.name;
          expression.operands[index] = Expression{
              ExpressionKind::IntegerLiteral,
              "0",
              {},
              expression.operands[index].span};
        }
        Type result{
            ValueDomain::Integer,
            "int",
            PackedRange{31, 0, true},
            true};
        retain_result_type(expression, result);
        return result;
      }
      const auto match = select_method(
          *class_identity(*receiver_type),
          name,
          SystemVerilogClassMethodKind::Function,
          expression.operands.size() - 1U,
          expression.span);
      if (!match || !match->method) return std::nullopt;
      expression.text =
          (expression.operands.front().text == "super"
               ? "@sv-base-method:"
               : "@sv-method:")
          + match->method->canonical_identity;
      retain_call_profile(expression, *match->method, true);
      for (std::size_t index = 1; index < expression.operands.size(); ++index) {
        const auto formal = index - 1U < match->method->arguments.size()
            ? &match->method->arguments[index - 1U] : nullptr;
        if (formal) {
          resolve_typed_expression(
              expression.operands[index], scope, formal->type);
        } else {
          resolve_expression(expression.operands[index], scope);
        }
      }
      if (const auto identity = class_identity(match->method->return_type)) {
        expression.nominal_type = *identity;
      }
      return match->method->return_type;
    }

    const auto selected = expression.text.rfind("::");
    if (selected != std::string::npos) {
      const auto owner = resolve_class_selection(
          expression.text.substr(0, selected), scope);
      if (owner) {
        const auto name = expression.text.substr(selected + 2U);
        const auto match = select_method(
            owner->declaration->canonical_identity,
            name,
            SystemVerilogClassMethodKind::Function,
            expression.operands.size(),
            expression.span);
        if (match && match->method && match->method->is_static) {
          const auto method = specialize_selected_method(
              *match->method, *owner, scope);
          const auto intrinsic = selected_uvm_intrinsic(*owner, name);
          expression.text = "@sv-static-method:"
              + (intrinsic.empty()
                     ? match->method->canonical_identity
                     : intrinsic);
          retain_call_profile(expression, method, false);
          for (std::size_t index = 0;
               index < expression.operands.size(); ++index) {
            const auto formal = index < method.arguments.size()
                ? &method.arguments[index] : nullptr;
            if (formal) {
              resolve_typed_expression(
                  expression.operands[index], scope, formal->type);
            } else {
              resolve_expression(expression.operands[index], scope);
            }
          }
          if (const auto identity = class_identity(method.return_type)) {
            expression.nominal_type = *identity;
          }
          return method.return_type;
        }
      }
    }

    if (scope.class_owner != nullptr) {
      const auto visible = find_methods(
          scope.class_owner->canonical_identity,
          expression.text,
          SystemVerilogClassMethodKind::Function);
      if (visible.empty()) {
        for (auto& operand : expression.operands) {
          resolve_expression(operand, scope);
        }
        return std::nullopt;
      }
      const auto match = select_method(
          scope.class_owner->canonical_identity,
          expression.text,
          SystemVerilogClassMethodKind::Function,
          expression.operands.size(),
          expression.span);
      if (match && match->method) {
        expression.text = (match->method->is_static
                ? "@sv-static-method:"
                : "@sv-method:")
            + match->method->canonical_identity;
        if (!match->method->is_static) {
          Expression receiver{
              ExpressionKind::Identifier, "this", {}, expression.span};
          receiver.nominal_type = scope.class_owner->canonical_identity;
          expression.operands.insert(
              expression.operands.begin(), std::move(receiver));
          expression.call_argument_names.insert(
              expression.call_argument_names.begin(), std::string{});
        }
        retain_call_profile(
            expression, *match->method, !match->method->is_static);
        const std::size_t first = match->method->is_static ? 0U : 1U;
        for (std::size_t index = first;
             index < expression.operands.size(); ++index) {
          const auto formal_index = index - first;
          const auto formal = formal_index < match->method->arguments.size()
              ? &match->method->arguments[formal_index] : nullptr;
          if (formal) {
            resolve_typed_expression(
                expression.operands[index], scope, formal->type);
          } else {
            resolve_expression(expression.operands[index], scope);
          }
        }
        if (const auto identity = class_identity(match->method->return_type)) {
          expression.nominal_type = *identity;
        }
        return match->method->return_type;
      }
    }

    for (auto& operand : expression.operands) {
      resolve_expression(operand, scope);
    }
    return std::nullopt;
  }

  std::optional<Type> resolve_expression(
      Expression& expression,
      const Scope& scope,
      const std::optional<std::string>& expected_class = std::nullopt,
      const SystemVerilogScalarKind expected_scalar =
          SystemVerilogScalarKind::None) {
    if (!expression.valid()) return std::nullopt;
    if (expression.kind == ExpressionKind::Identifier) {
      return resolve_identifier(expression, scope);
    }
    if (expression.kind == ExpressionKind::Call) {
      return resolve_call(
          expression, scope, expected_class, expected_scalar);
    }
    if (expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2U) {
      auto container = resolve_expression(expression.operands[0], scope);
      resolve_expression(expression.operands[1], scope);
      if (!container || !container->systemverilog_container) {
        return std::nullopt;
      }
      auto element =
          container->systemverilog_container->element_types.empty()
          ? *container
          : container->systemverilog_container->element_types.front();
      if (container->systemverilog_container->element_types.empty()) {
        element.systemverilog_container.reset();
      }
      constexpr std::string_view property_prefix{
          "@sv-container-property:"};
      if (expression.operands[0].kind != ExpressionKind::Call
          || !expression.operands[0].text.starts_with(property_prefix)
          || expression.operands[0].operands.size() != 1U) {
        retain_result_type(expression, element);
        expression.nominal_type =
            element.systemverilog_class_declaration;
        return element;
      }
      auto receiver = std::move(expression.operands[0].operands.front());
      auto index = std::move(expression.operands[1]);
      expression.kind = ExpressionKind::Call;
      expression.text = "@sv-container-index:"
          + expression.operands[0].text.substr(property_prefix.size());
      expression.operands = {std::move(receiver), std::move(index)};
      retain_result_type(expression, element);
      expression.nominal_type = element.systemverilog_class_declaration;
      return element;
    }
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2U) {
      auto left = resolve_expression(expression.operands[0], scope);
      auto right = resolve_expression(
          expression.operands[1], scope,
          left ? class_identity(*left) : std::nullopt,
          chandle(left) ? SystemVerilogScalarKind::Chandle
                         : SystemVerilogScalarKind::None);
      if (!left && right && class_identity(*right)) {
        left = resolve_expression(
            expression.operands[0], scope, class_identity(*right));
      }
      if (!left && chandle(right)) {
        left = resolve_expression(
            expression.operands[0], scope, std::nullopt,
            SystemVerilogScalarKind::Chandle);
      }
      if (chandle(left) || chandle(right)) {
        const bool equality = expression.text == "=="
            || expression.text == "!=" || expression.text == "==="
            || expression.text == "!==";
        if (!chandle(left) || !chandle(right) || !equality) {
          diagnose(
              diagnostics_, "FSIM-SV-SEM-175",
              "chandle only supports equality and inequality with chandle "
              "or null",
              expression.span);
          return std::nullopt;
        }
        expression.systemverilog_scalar_kind =
            SystemVerilogScalarKind::None;
        return Type{
            ValueDomain::Bit2, "bit", PackedRange{0, 0, true}, false};
      }
      return std::nullopt;
    }
    for (auto& operand : expression.operands) {
      resolve_expression(operand, scope);
    }
    for (auto& choices : expression.aggregate_choice_expressions) {
      for (auto& choice : choices) resolve_expression(choice, scope);
    }
    return std::nullopt;
  }

  std::optional<Type> resolve_typed_expression(
      Expression& expression,
      const Scope& scope,
      const Type& expected) {
    if (expected.systemverilog_virtual_interface) {
      // Interface instances are hierarchy objects rather than ordinary
      // chandle-valued expressions. The hierarchy builder validates the
      // concrete interface type, specialization, modport, and selected
      // instance after child interfaces have been elaborated.
      return expected;
    }
    if (expression.text == "@sv-new"
        && (expected.spelling == "mailbox"
            || expected.spelling == "semaphore")) {
      expression.text = "@sv-sync-new:" + expected.spelling;
      expression.systemverilog_scalar_kind =
          SystemVerilogScalarKind::Chandle;
      for (auto& operand : expression.operands) {
        resolve_expression(operand, scope);
      }
      return expected;
    }
    auto expected_identity = class_identity(expected);
    if (!expected_identity && !expected.named_type.empty()) {
      expected_identity = expected.named_type;
    }
    const auto resolved = resolve_expression(
        expression, scope, expected_identity,
        expected.systemverilog_container
            ? SystemVerilogScalarKind::None
            : expected.systemverilog_scalar);
    const bool expected_chandle =
        !expected.systemverilog_container
        && expected.systemverilog_scalar == SystemVerilogScalarKind::Chandle;
    if ((expected_chandle && resolved && !chandle(resolved))
        || (!expected_chandle && chandle(resolved))) {
      diagnose(
          diagnostics_, "FSIM-SV-SEM-174",
          "chandle assignment requires a chandle or null value and a "
          "chandle destination",
          expression.span);
    }
    return resolved;
  }

  void resolve_task_call(Statement& statement, const Scope& scope) {
    const auto dot = statement.task_name.rfind('.');
    if (dot != std::string::npos) {
      Expression receiver{
          ExpressionKind::Identifier,
          statement.task_name.substr(0, dot),
          {},
          statement.span};
      const auto receiver_type = resolve_expression(receiver, scope);
      constexpr std::string_view container_property_prefix{
          "@sv-container-property:"};
      if (receiver_type && receiver_type->systemverilog_container
          && receiver.kind == ExpressionKind::Call
          && receiver.text.starts_with(container_property_prefix)
          && receiver.operands.size() == 1U) {
        const auto name = statement.task_name.substr(dot + 1U);
        if (name == "push_back") {
          statement.task_name = "@sv-container-push-back:"
              + receiver.text.substr(container_property_prefix.size());
          statement.task_arguments.insert(
              statement.task_arguments.begin(),
              std::move(receiver.operands.front()));
          statement.task_argument_names.insert(
              statement.task_argument_names.begin(), std::string{});
          const auto expected = receiver_type->systemverilog_class_declaration;
          for (auto& argument : statement.task_arguments
                   | std::views::drop(1)) {
            resolve_expression(argument, scope, expected);
          }
          return;
        }
      }
      if (receiver_type && class_identity(*receiver_type)) {
        const auto name = statement.task_name.substr(dot + 1U);
        if (name == "srandom" && statement.task_arguments.size() == 1U) {
          statement.task_name = "@sv-object-srandom";
          statement.task_arguments.insert(
              statement.task_arguments.begin(), std::move(receiver));
          statement.task_argument_names.insert(
              statement.task_argument_names.begin(), std::string{});
          resolve_expression(statement.task_arguments.back(), scope);
          return;
        }
        auto kind = SystemVerilogClassMethodKind::Constructor;
        if (name != "new") {
          kind = find_methods(
                     *class_identity(*receiver_type),
                     name,
                     SystemVerilogClassMethodKind::Task)
                         .empty()
              ? SystemVerilogClassMethodKind::Function
              : SystemVerilogClassMethodKind::Task;
        }
        const auto match = select_method(
            *class_identity(*receiver_type),
            name,
            kind,
            statement.task_arguments.size(),
            statement.span);
        if (match) {
          const auto canonical = match->method
              ? match->method->canonical_identity
              : *class_identity(*receiver_type) + "::new";
          statement.task_name = kind == SystemVerilogClassMethodKind::Constructor
              ? "@sv-base-constructor:" + canonical
              : "@sv-task:" + canonical;
          statement.task_arguments.insert(
              statement.task_arguments.begin(), std::move(receiver));
          statement.task_argument_names.insert(
              statement.task_argument_names.begin(), std::string{});
          if (kind != SystemVerilogClassMethodKind::Constructor
              && match->method) {
            retain_task_profile(
                statement,
                *match->method,
                match->owner,
                &*receiver_type);
            for (std::size_t index = 1U;
                 index < statement.task_arguments.size(); ++index) {
              const auto actual_name = index < statement.task_argument_names.size()
                  ? std::string_view{statement.task_argument_names[index]}
                  : std::string_view{};
              auto formal = match->method->arguments.begin()
                  + static_cast<std::ptrdiff_t>(std::min(
                      index - 1U, match->method->arguments.size()));
              if (!actual_name.empty()) {
                formal = std::ranges::find(
                    match->method->arguments,
                    actual_name,
                    &FunctionArgument::name);
              }
              if (formal != match->method->arguments.end()) {
                resolve_typed_expression(
                    statement.task_arguments[index], scope, formal->type);
              } else {
                resolve_expression(statement.task_arguments[index], scope);
              }
            }
          }
          return;
        }
      }
    }
    const auto selected = statement.task_name.rfind("::");
    if (selected != std::string::npos) {
      const auto owner = resolve_class_selection(
          statement.task_name.substr(0, selected), scope);
      if (owner) {
        const auto name = statement.task_name.substr(selected + 2U);
        auto kind = SystemVerilogClassMethodKind::Task;
        auto matches = find_methods(
            owner->declaration->canonical_identity,
            name,
            kind);
        if (matches.empty()) {
          kind = SystemVerilogClassMethodKind::Function;
          matches = find_methods(
              owner->declaration->canonical_identity,
              name,
              kind);
        }
        if (!matches.empty()) {
          const auto match = select_method(
              owner->declaration->canonical_identity,
              name,
              kind,
              statement.task_arguments.size(),
              statement.span);
          if (match && match->method && match->method->is_static) {
            const auto intrinsic = selected_uvm_intrinsic(*owner, name);
            statement.task_name = "@sv-static-task:"
                + (intrinsic.empty()
                       ? match->method->canonical_identity
                       : intrinsic);
            const auto method = specialize_selected_method(
                *match->method, *owner, scope);
            retain_task_profile(
                statement,
                method,
                owner->declaration,
                &owner->type);
            for (std::size_t index = 0;
                 index < statement.task_arguments.size(); ++index) {
              if (index < method.arguments.size()) {
                resolve_typed_expression(
                    statement.task_arguments[index],
                    scope,
                    method.arguments[index].type);
              } else {
                resolve_expression(statement.task_arguments[index], scope);
              }
            }
            return;
          }
        }
      }
    }
    if (scope.class_owner != nullptr) {
      auto kind = SystemVerilogClassMethodKind::Task;
      auto matches = find_methods(
          scope.class_owner->canonical_identity,
          statement.task_name,
          kind);
      if (matches.empty()) {
        kind = SystemVerilogClassMethodKind::Function;
        matches = find_methods(
            scope.class_owner->canonical_identity,
            statement.task_name,
            kind);
      }
      if (!matches.empty()) {
        const auto match = select_method(
            scope.class_owner->canonical_identity,
            statement.task_name,
            kind,
            statement.task_arguments.size(),
            statement.span);
        if (match && match->method) {
          statement.task_name = (match->method->is_static
                  ? "@sv-static-task:"
                  : "@sv-task:")
              + match->method->canonical_identity;
          if (!match->method->is_static) {
            Expression receiver{
                ExpressionKind::Identifier, "this", {}, statement.span};
            receiver.nominal_type = scope.class_owner->canonical_identity;
            statement.task_arguments.insert(
                statement.task_arguments.begin(), std::move(receiver));
            statement.task_argument_names.insert(
                statement.task_argument_names.begin(), std::string{});
          }
          retain_task_profile(statement, *match->method);
          const auto first_actual = match->method->is_static ? 0U : 1U;
          for (std::size_t index = first_actual;
               index < statement.task_arguments.size(); ++index) {
            const auto positional = index - first_actual;
            const auto name = index < statement.task_argument_names.size()
                ? std::string_view{statement.task_argument_names[index]}
                : std::string_view{};
            auto formal = match->method->arguments.begin()
                + static_cast<std::ptrdiff_t>(std::min(
                    positional, match->method->arguments.size()));
            if (!name.empty()) {
              formal = std::ranges::find(
                  match->method->arguments,
                  name,
                  &FunctionArgument::name);
            }
            if (formal != match->method->arguments.end()) {
              resolve_typed_expression(
                  statement.task_arguments[index], scope, formal->type);
            } else {
              resolve_expression(statement.task_arguments[index], scope);
            }
          }
          return;
        }
      }
    }
    for (auto& argument : statement.task_arguments) {
      resolve_expression(argument, scope);
    }
  }

  void resolve_statement(
      Statement& statement,
      const Scope& inherited_scope,
      const std::optional<std::string>& return_class,
      const SystemVerilogScalarKind return_scalar =
          SystemVerilogScalarKind::None) {
    auto scope = inherited_scope;
    for (auto& declaration : statement.declarations) {
      if (declaration.initializer) {
        resolve_typed_expression(
            *declaration.initializer, scope, declaration.type);
      }
      scope.objects[declaration.name] = declaration.type;
    }
    auto target_type = resolve_expression(statement.target, scope);
    const auto expected_class = statement.kind == StatementKind::Return
        ? return_class
        : target_type ? class_identity(*target_type) : std::nullopt;
    const auto expected_scalar = statement.kind == StatementKind::Return
        ? return_scalar
        : target_type && !target_type->systemverilog_container
            ? target_type->systemverilog_scalar
                      : SystemVerilogScalarKind::None;
    const bool typed_assignment = statement.kind == StatementKind::Assignment
        || statement.kind == StatementKind::Force
        || statement.kind == StatementKind::ProceduralAssign
        || statement.kind == StatementKind::Return;
    const auto value_type = (statement.kind == StatementKind::Assignment
                                || statement.kind == StatementKind::ProceduralAssign)
            && target_type
        ? resolve_typed_expression(statement.value, scope, *target_type)
        : resolve_expression(
              statement.value, scope, expected_class, expected_scalar);
    const bool expected_chandle =
        expected_scalar == SystemVerilogScalarKind::Chandle;
    const bool destination_type_known =
        statement.kind == StatementKind::Return || target_type.has_value();
    if (typed_assignment && destination_type_known && statement.value.valid()
        && ((expected_chandle && value_type && !chandle(value_type))
            || (!expected_chandle && chandle(value_type)))) {
      diagnose(
          diagnostics_, "FSIM-SV-SEM-174",
          "chandle assignment requires a chandle or null value and a "
          "chandle destination",
          statement.value.span);
    }
    const auto condition_type = resolve_expression(statement.condition, scope);
    if (chandle(condition_type)) {
      diagnose(
          diagnostics_, "FSIM-SV-SEM-175",
          "chandle does not provide logical truth; compare it with null",
          statement.condition.span);
    }
    resolve_expression(statement.loop_initial, scope);
    resolve_expression(statement.loop_limit, scope);
    resolve_expression(statement.loop_update_target, scope);
    for (auto& update : statement.loop_updates) {
      resolve_statement(update, scope, return_class, return_scalar);
    }
    for (auto& output : statement.output_values) {
      resolve_expression(output.value, scope);
    }
    for (auto& sensitivity : statement.sensitivities) {
      resolve_expression(sensitivity.expression, scope);
    }
    if (statement.kind == StatementKind::TaskCall) {
      resolve_task_call(statement, scope);
    }
    for (auto& child : statement.statements) {
      resolve_statement(child, scope, return_class, return_scalar);
    }
    for (auto& child : statement.else_statements) {
      resolve_statement(child, scope, return_class, return_scalar);
    }
    for (auto& alternative : statement.case_alternatives) {
      for (auto& choice : alternative.choices) {
        resolve_expression(choice, scope);
      }
      for (auto& child : alternative.statements) {
        resolve_statement(child, scope, return_class, return_scalar);
      }
    }
  }

  void resolve_function(FunctionDeclaration& function, Scope scope) {
    add_objects(scope, function.arguments);
    add_objects(scope, function.variables);
    scope.objects[function.name] = function.return_type;
    for (auto& variable : function.variables) {
      if (variable.initializer) {
        resolve_typed_expression(
            *variable.initializer, scope, variable.type);
      }
    }
    const auto return_class = class_identity(function.return_type);
    const auto return_scalar = function.return_type.systemverilog_container
        ? SystemVerilogScalarKind::None
        : function.return_type.systemverilog_scalar;
    for (auto& statement : function.statements) {
      resolve_statement(statement, scope, return_class, return_scalar);
    }
    for (auto& nested : function.functions) resolve_function(nested, scope);
  }

  void resolve_task(TaskDeclaration& task, Scope scope) {
    add_objects(scope, task.arguments);
    add_objects(scope, task.variables);
    for (auto& variable : task.variables) {
      if (variable.initializer) {
        resolve_typed_expression(
            *variable.initializer, scope, variable.type);
      }
    }
    for (auto& statement : task.statements) {
      resolve_statement(statement, scope, std::nullopt);
    }
  }

  void resolve_class(SystemVerilogClassDeclaration& declaration, Scope scope) {
    scope.class_owner = &declaration;
    scope.lexical_identity = declaration.canonical_identity;
    for (auto& property : declaration.properties) {
      if (property.declaration.initializer) {
        resolve_typed_expression(
            *property.declaration.initializer,
            scope,
            property.declaration.type);
      }
    }
    for (auto& method : declaration.methods) {
      auto method_scope = scope;
      add_objects(method_scope, method.arguments);
      add_objects(method_scope, method.variables);
      if (method.kind == SystemVerilogClassMethodKind::Function) {
        method_scope.objects[method.name] = method.return_type;
      }
      for (auto& variable : method.variables) {
        if (variable.initializer) {
          resolve_typed_expression(
              *variable.initializer, method_scope, variable.type);
        }
      }
      const auto return_class = class_identity(method.return_type);
      const auto return_scalar = method.return_type.systemverilog_container
          ? SystemVerilogScalarKind::None
          : method.return_type.systemverilog_scalar;
      for (auto& statement : method.statements) {
        resolve_statement(
            statement, method_scope, return_class, return_scalar);
      }
    }
    for (auto& nested : declaration.nested_classes) {
      auto nested_scope = scope;
      nested_scope.class_owner = &nested;
      nested_scope.lexical_identity = nested.canonical_identity;
      resolve_class(nested, nested_scope);
    }
  }

  ParsedDesign& design_;
  std::vector<Diagnostic>& diagnostics_;
  std::map<std::string, SystemVerilogClassDeclaration*, std::less<>> classes_;
  std::map<
      std::string,
      const DesignUnit*,
      std::less<>> class_units_;
  std::map<std::string, const Type*, std::less<>> class_aliases_;
  std::map<std::string, SystemVerilogClassMethod*, std::less<>> methods_;
  std::map<
      std::string,
      SystemVerilogClassDeclaration*,
      std::less<>> method_owners_;
};

}  // namespace

bool resolve_systemverilog_class_expressions(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics) {
  const auto initial_diagnostic_count = diagnostics.size();
  Resolver{design, diagnostics}.run();
  return std::none_of(
      diagnostics.begin()
          + static_cast<std::ptrdiff_t>(initial_diagnostic_count),
      diagnostics.end(),
      [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
      });
}

}  // namespace fsim::frontend

// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution_internal.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <utility>

namespace fsim::frontend::class_resolution_detail {

bool is_class_property_access(const Expression& expression)
{
  if (expression.kind != ExpressionKind::Call) return false;
  if (expression.text.starts_with("@sv-property:")
      || expression.text.starts_with("@sv-static-property:")) {
    return true;
  }
  return expression.text.starts_with("@sv-select:")
      && expression.operands.size() == 1U
      && is_class_property_access(expression.operands.front());
}


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
  // A virtual interface shares the chandle representation but is not one.
  return type
      && !type->systemverilog_container
      && !type->systemverilog_virtual_interface
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
    // Compilation-unit objects are visible to every unit of the file, and
    // the unit's own declarations hide them (IEEE 1800-2017 3.12.1).
    for (const auto& import : unit.systemverilog_imports) {
      if (!import.package.starts_with("fsim_unit_")) continue;
      for (const auto& candidate : design_.units) {
        if (candidate.kind == UnitKind::SystemVerilogPackage
            && candidate.name == import.package
            && effective_library(candidate.library)
                == effective_library(unit.library)) {
          add_objects(scope, candidate.signals);
          add_objects(scope, candidate.variables);
        }
      }
    }
    add_objects(scope, unit.parameters);
    add_objects(scope, unit.ports);
    for (const auto& instance : unit.instances) {
      scope.instances.emplace(instance.name, instance.unit_name);
    }
    // A specialization's parameter in a module constant, `C#(7)::v`.
    for (auto& parameter : unit.parameters) {
      if (parameter.kind == ParameterKind::Value) {
        substitute_class_parameter_references(parameter.default_value, scope);
      }
    }
    for (auto& instance : unit.instances) {
      for (auto& override_value : instance.parameter_overrides) {
        substitute_class_parameter_references(override_value.value, scope);
      }
    }
    resolve_scoped_types(unit.signals, scope);
    resolve_scoped_types(unit.variables, scope);
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
      // A class member hides an enclosing module object of the same name
      // (IEEE 1800-2017 8.6, 23.9).
      std::erase_if(class_scope.objects, [&](const auto& object) {
        return find_property(declaration.canonical_identity, object.first)
            .property != nullptr;
      });
      resolve_class(declaration, class_scope);
    }
    for (auto& function : unit.functions) {
      resolve_function(function, scope);
    }
    for (auto& task : unit.tasks) resolve_task(task, scope);
    for (auto& process : unit.processes) {
      auto process_scope = scope;
      resolve_scoped_types(process.variables, process_scope);
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
  resolve_scoped_types(body.signals, scope);
  resolve_scoped_types(body.variables, scope);
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
    resolve_scoped_types(process.variables, process_scope);
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
bool Resolver::check_member_visibility(
    const PropertyMatch& match, const Scope& scope,
    const std::string_view member, const SourceSpan& span) {
  if (match.property == nullptr || match.owner == nullptr
      || match.property->visibility == SystemVerilogClassVisibility::Public) {
    return true;
  }
  const auto* current = scope.class_owner;
  bool visible = false;
  if (current != nullptr) {
    if (match.property->visibility == SystemVerilogClassVisibility::Local) {
      visible = current->canonical_identity == match.owner->canonical_identity;
    } else {
      std::set<std::string> visited;
      for (const auto* candidate = current; candidate != nullptr
           && visited.insert(candidate->canonical_identity).second;
           candidate = find_base_class(*candidate)) {
        if (candidate->canonical_identity == match.owner->canonical_identity) {
          visible = true;
          break;
        }
      }
      // A nested class sees its enclosing class's members.
      if (!visible
          && current->canonical_identity.starts_with(
              match.owner->canonical_identity + "::")) {
        visible = true;
      }
    }
    if (!visible
        && match.property->visibility == SystemVerilogClassVisibility::Local
        && current->canonical_identity.starts_with(
            match.owner->canonical_identity + "::")) {
      visible = true;
    }
  }
  if (!visible) {
    diagnose(
        diagnostics_,
        "FSIM-SV-CLASS-028",
        std::string{match.property->visibility
                == SystemVerilogClassVisibility::Local
            ? "local"
            : "protected"}
            + " member '" + std::string{member} + "' of class '"
            + match.owner->canonical_identity + "' is not visible here",
        span);
  }
  return visible;
}

bool Resolver::class_assignable(
    const std::string_view from, const std::string_view to) const {
  std::set<std::string> visited;
  std::vector<const SystemVerilogClassDeclaration*> pending;
  const auto* start = find_class(from);
  const auto* target = find_class(to);
  // Only plain classes named by their own declarations are compared;
  // parameterized classes and typedef names are left to elaboration.
  const auto plain = [](const SystemVerilogClassDeclaration* declaration,
                         std::string_view identity) {
    return declaration != nullptr && declaration->parameters.empty()
        && declaration->canonical_identity == identity;
  };
  if (!plain(start, from) || !plain(target, to)
      || has_specialization_dependent_base(from)) {
    return true;
  }
  pending.push_back(start);
  while (!pending.empty()) {
    const auto* current = pending.back();
    pending.pop_back();
    if (current == nullptr || !current->parameters.empty()) {
      return true;
    }
    if (!visited.insert(current->canonical_identity).second) {
      continue;
    }
    if (current->canonical_identity == to) {
      return true;
    }
    if (current->base) {
      const auto* base = find_base_class(*current);
      // A base that depends on a parameter is not known here.
      if (base == nullptr) {
        return true;
      }
      pending.push_back(base);
    }
    if (!current->implemented_interfaces.empty()
        || !current->extended_interfaces.empty()) {
      return true;
    }
  }
  return false;
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
  // A handle type named by its class needs no alias lookup; a typedef name
  // that also carries the class identity may denote an array of handles.
  const auto names_class = [&] {
    const auto& identity = type.systemverilog_class_declaration;
    return identity == type.named_type
        || identity.ends_with("::" + type.named_type);
  };
  if ((class_identity(type) && !type.systemverilog_container
          && names_class())
      || type.named_type.empty()) {
    return type;
  }
  // A type declared in a base class or an extended interface class is
  // visible in the derived class (IEEE 1800-2017 8.26.3).
  const auto inherited_alias = [&](const std::string& identity)
      -> const Type* {
    std::vector<const SystemVerilogClassDeclaration*> pending;
    std::set<std::string> visited;
    if (const auto* start = find_class(identity)) pending.push_back(start);
    while (!pending.empty()) {
      const auto* current = pending.back();
      pending.pop_back();
      if (!visited.insert(current->canonical_identity).second) continue;
      if (current->canonical_identity != identity) {
        const auto found = class_aliases_.find(
            current->canonical_identity + "::" + type.named_type);
        if (found != class_aliases_.end()) return found->second;
      }
      if (const auto* base = find_base_class(*current)) {
        pending.push_back(base);
      }
      for (const auto& extended : current->extended_interfaces) {
        if (const auto* base = find_class(extended.declaration_identity)) {
          pending.push_back(base);
        }
      }
      for (const auto& implemented : current->implemented_interfaces) {
        if (const auto* base = find_class(implemented.declaration_identity)) {
          pending.push_back(base);
        }
      }
    }
    return nullptr;
  };
  for (;;) {
    auto alias = class_aliases_.find(
        lexical_identity + "::" + type.named_type);
    if (alias == class_aliases_.end()) {
      if (const auto* inherited = inherited_alias(lexical_identity)) {
        if (type.systemverilog_container) {
          auto& elements = type.systemverilog_container->element_types;
          if (elements.empty()) {
            elements.push_back(*inherited);
          } else {
            elements.front() = *inherited;
          }
          return type;
        }
        return *inherited;
      }
    }
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
std::optional<Type> Resolver::packed_member_type(
    const Type& type, const std::string_view member) const {
  const Type* structure = &type;
  if (structure->packed_members.empty() && !type.named_type.empty()) {
    const Type* unique = nullptr;
    for (const auto& [key, alias] : class_aliases_) {
      if (!key.ends_with("::" + type.named_type)
          || alias->packed_members.empty()) {
        continue;
      }
      if (unique != nullptr) return std::nullopt;
      unique = alias;
    }
    if (unique == nullptr) return std::nullopt;
    structure = unique;
  }
  const auto found = std::ranges::find(
      structure->packed_members, member, &PackedMember::name);
  if (found == structure->packed_members.end()) return std::nullopt;
  Type result;
  result.domain = found->domain;
  result.spelling = found->spelling;
  result.packed_range = found->packed_range;
  result.is_signed = found->is_signed;
  return result;
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
void Resolver::substitute_class_parameter_references(
    Expression& expression, const Scope& scope, const std::size_t depth) {
  if (depth > 16U) return;
  if (expression.kind == ExpressionKind::Identifier
      && expression.text.find('#') != std::string::npos) {
    const auto selected = expression.text.rfind("::");
    if (selected != std::string::npos) {
      const auto owner_name = expression.text.substr(0, selected);
      const auto owners = resolve_class_name(owner_name, scope);
      if (owners.size() == 1U) {
        if (auto value = class_specialization_parameter_value(*owners.front(),
                owner_name, expression.text.substr(selected + 2U))) {
          const auto span = expression.span;
          expression = std::move(*value);
          expression.span = span;
          substitute_class_parameter_references(expression, scope, depth + 1U);
          return;
        }
      }
    }
  }
  for (auto& operand : expression.operands) {
    substitute_class_parameter_references(operand, scope, depth + 1U);
  }
}
std::optional<Expression> Resolver::class_specialization_parameter_value(
    const SystemVerilogClassDeclaration& owner, const std::string_view spelling,
    const std::string_view member) const {
  const auto trim = [](std::string_view text) {
    while (!text.empty()
           && std::isspace(static_cast<unsigned char>(text.front()))) {
      text.remove_prefix(1U);
    }
    while (!text.empty()
           && std::isspace(static_cast<unsigned char>(text.back()))) {
      text.remove_suffix(1U);
    }
    return text;
  };
  // An actual written as a literal or a name.
  const auto actual_expression = [&](std::string_view text)
      -> std::optional<Expression> {
    text = trim(text);
    if (text.empty()) return std::nullopt;
    const auto identifier = (std::isalpha(static_cast<unsigned char>(
                                 text.front())) != 0
                                || text.front() == '_')
        && std::ranges::all_of(text, [](const char character) {
             return std::isalnum(static_cast<unsigned char>(character)) != 0
                 || character == '_' || character == '$';
           });
    const auto literal = (std::isdigit(static_cast<unsigned char>(
                              text.front())) != 0
                             || text.front() == '\'')
        && std::ranges::all_of(text, [](const char character) {
             return std::isalnum(static_cast<unsigned char>(character)) != 0
                 || character == '_' || character == '\'';
           });
    if (!identifier && !literal) return std::nullopt;
    return Expression{
        identifier ? ExpressionKind::Identifier
            : text.find('\'') != std::string_view::npos
            ? ExpressionKind::LogicLiteral
            : ExpressionKind::IntegerLiteral,
        std::string{text}, {}, {}};
  };
  std::map<std::string, Expression, std::less<>> values;
  const auto actuals = selected_parameter_actuals(spelling);
  std::size_t position{};
  for (const auto& parameter : owner.parameters) {
    if (parameter.local) continue;
    std::optional<std::string_view> text;
    for (const auto actual : actuals) {
      const auto trimmed = trim(actual);
      if (trimmed.starts_with('.')) {
        const auto open = trimmed.find('(');
        if (open != std::string_view::npos && trimmed.ends_with(')')
            && trim(trimmed.substr(1U, open - 1U)) == parameter.name) {
          text = trimmed.substr(open + 1U, trimmed.size() - open - 2U);
        }
      }
    }
    if (!text && position < actuals.size()
        && !trim(actuals[position]).starts_with('.')) {
      text = actuals[position];
    }
    ++position;
    if (text) {
      if (parameter.kind != ParameterKind::Value) continue;
      auto value = actual_expression(*text);
      if (!value) return std::nullopt;
      values.insert_or_assign(parameter.name, std::move(*value));
    }
  }
  const auto selected = std::ranges::find_if(owner.parameters,
      [&](const ParameterDeclaration& parameter) {
        return parameter.kind == ParameterKind::Value
            && parameter.name == member;
      });
  const auto substitute = [&](const auto& self, Expression value,
                              const std::size_t depth)
      -> std::optional<Expression> {
    if (depth > 16U) return std::nullopt;
    if (value.kind == ExpressionKind::Identifier) {
      if (const auto found = values.find(value.text); found != values.end()) {
        return found->second;
      }
      const auto parameter = std::ranges::find_if(owner.parameters,
          [&](const ParameterDeclaration& candidate) {
            return candidate.kind == ParameterKind::Value
                && candidate.name == value.text;
          });
      if (parameter != owner.parameters.end()) {
        return self(self, parameter->default_value, depth + 1U);
      }
      return value;
    }
    for (auto& operand : value.operands) {
      auto replaced = self(self, std::move(operand), depth + 1U);
      if (!replaced) return std::nullopt;
      operand = std::move(*replaced);
    }
    return value;
  };
  if (selected == owner.parameters.end()) {
    // A local parameter of the class body is kept as a constant property.
    const auto property = std::ranges::find_if(owner.properties,
        [&](const SystemVerilogClassProperty& candidate) {
          return candidate.is_parameter
              && candidate.declaration.name == member;
        });
    if (property == owner.properties.end()
        || !property->declaration.initializer) {
      return std::nullopt;
    }
    return substitute(substitute, *property->declaration.initializer, 0U);
  }
  if (const auto found = values.find(selected->name); found != values.end()) {
    return found->second;
  }
  if (!selected->default_value.valid()) return std::nullopt;
  return substitute(substitute, selected->default_value, 0U);
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
[[nodiscard]] std::optional<Type> Resolver::resolve_class_scoped_type(
    const Type& type, const Scope& scope) const {
  // `C::t` or `C #(...)::t`: a type declared in a class, specialized by the
  // selected class actuals (IEEE 1800-2017 8.25.1).
  const auto separator = type.named_type.rfind("::");
  if (separator == std::string::npos || separator == 0U) return std::nullopt;
  const auto owners = resolve_class_name(
      std::string_view{type.named_type}.substr(0U, separator), scope);
  if (owners.size() != 1U) return std::nullopt;
  const auto alias = class_aliases_.find(owners.front()->canonical_identity
      + "::" + type.named_type.substr(separator + 2U));
  if (alias == class_aliases_.end()) {
    // A nested class, possibly inherited from a base (`Derived::Inner`).
    if (find_class(owners.front()->canonical_identity + "::"
            + type.named_type.substr(separator + 2U))) {
      return std::nullopt;
    }
    const auto nested = resolve_class_name(type.named_type, scope);
    if (nested.size() != 1U) return std::nullopt;
    auto resolved = class_type(*nested.front());
    resolved.systemverilog_container = type.systemverilog_container;
    return resolved;
  }
  ClassSelection selection{owners.front(), class_type(*owners.front())};
  selection.type.systemverilog_class_parameter_actuals =
      type.systemverilog_class_parameter_actuals;
  std::size_t next_positional{};
  for (auto& actual : selection.type.systemverilog_class_parameter_actuals) {
    std::size_t formal_index = next_positional++;
    if (actual.name) {
      const auto formal = std::ranges::find(
          selection.declaration->parameters, *actual.name,
          &ParameterDeclaration::name);
      if (formal == selection.declaration->parameters.end()) continue;
      formal_index = static_cast<std::size_t>(std::distance(
          selection.declaration->parameters.begin(), formal));
    }
    if (formal_index >= selection.declaration->parameters.size()) continue;
    const auto& formal = selection.declaration->parameters[formal_index];
    if (formal.kind == ParameterKind::Type && !actual.type_actual
        && actual.value.valid()) {
      actual.type_actual = std::make_shared<Type>(
          selected_type_actual(actual.value.text, scope));
    } else if (actual.type_actual
        && actual.type_actual->systemverilog_class_declaration.empty()
        && !actual.type_actual->named_type.empty()) {
      *actual.type_actual = selected_type_actual(
          actual.type_actual->named_type, scope);
    }
  }
  auto resolved = specialize_selected_type(*alias->second, selection);
  if (type.systemverilog_container && !resolved.systemverilog_container) {
    // `C::t x[string]`: the declared dimensions hold elements of the type.
    auto container = type.systemverilog_container;
    for (auto& element : container->element_types) {
      if (element.named_type == type.named_type) {
        auto actuals = element.systemverilog_class_parameter_actuals;
        element = resolved;
        if (element.systemverilog_class_parameter_actuals.empty()) {
          element.systemverilog_class_parameter_actuals = std::move(actuals);
        }
      }
    }
    resolved.systemverilog_container = std::move(container);
  }
  return resolved;
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
    // `Derived::Inner` names a class or typedef that `Derived` inherits
    // from a base class (IEEE 1800-2017 8.13, 8.23).
    if (matches.empty()) {
      for (const auto* owner : owners) {
        std::set<std::string> visited;
        for (auto base = find_base_class(*owner);
             base != nullptr && matches.empty()
             && visited.insert(base->canonical_identity).second;
             base = find_base_class(*base)) {
          if (const auto nested = find_class(
                  base->canonical_identity + "::" + alias_name)) {
            matches.push_back(nested);
            break;
          }
          const auto alias = class_aliases_.find(
              base->canonical_identity + "::" + alias_name);
          if (alias != class_aliases_.end()) {
            if (const auto identity = class_identity(*alias->second)) {
              if (const auto declaration = find_class(*identity)) {
                matches.push_back(declaration);
              }
            }
          }
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
  // A typedef of a class (`typedef C #(int) K;`) names that class in a
  // class scope resolution `K::f()` (IEEE 1800-2017 8.23, 6.18).
  if (matches.empty() && normalized.find("::") == std::string::npos) {
    const auto library_prefix = scope.library + "::";
    for (const auto& [key, type] : class_aliases_) {
      if (!key.starts_with(library_prefix)
          || !key.ends_with("::" + normalized)) {
        continue;
      }
      if (const auto identity = class_identity(*type)) {
        if (const auto declaration = find_class(*identity);
            declaration != nullptr
            && std::ranges::find(matches, declaration) == matches.end()) {
          matches.push_back(declaration);
        }
      }
    }
  }
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
  if (!matches.empty() || kind == SystemVerilogClassMethodKind::Constructor) {
    return matches;
  }
  // An interface class inherits the prototypes of every interface class it
  // extends, not only the first (IEEE 1800-2017 8.26.3).
  std::vector<const SystemVerilogClassDeclaration*> pending;
  for (const auto& seen : visited) {
    if (const auto* declaration = find_class(seen)) {
      for (const auto& extended : declaration->extended_interfaces) {
        if (const auto* found = find_class(extended.declaration_identity)) {
          pending.push_back(found);
        }
      }
    }
  }
  for (std::size_t index = 0; index < pending.size() && matches.empty();
       ++index) {
    const auto* declaration = pending[index];
    if (!visited.insert(declaration->canonical_identity).second) continue;
    for (const auto& method : declaration->methods) {
      if (method.name == name && method.kind == kind) {
        matches.push_back({&method, declaration});
      }
    }
    if (const auto* base = find_base_class(*declaration)) {
      pending.push_back(base);
    }
    for (const auto& extended : declaration->extended_interfaces) {
      if (const auto* found = find_class(extended.declaration_identity)) {
        pending.push_back(found);
      }
    }
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

  // A class handle may be a variable in a module-instance scope
  // (`m1.c.method()`, IEEE 1800-2017 23.8). Resolve only a path rooted at an
  // exact instance declared by the current unit and require unique unit and
  // declaration matches at every step. This supplies method selection with
  // the declared handle type; runtime reads still use the ordinary
  // hierarchical signal binding.
  const auto first_dot = expression.text.find('.');
  if (first_dot != std::string::npos) {
    std::vector<std::string_view> components;
    std::string_view remaining { expression.text };
    while (true) {
      const auto separator = remaining.find('.');
      components.push_back(remaining.substr(0U, separator));
      if (separator == std::string_view::npos) {
        break;
      }
      remaining.remove_prefix(separator + 1U);
    }
    if (components.size() >= 2U
        && scope.instances.contains(components.front())) {
      const auto select_unit = [&](const std::string_view unit_name,
                                   const std::string_view library)
          -> const DesignUnit* {
        const DesignUnit* selected = nullptr;
        for (const auto& candidate : design_.units) {
          if (candidate.name != unit_name
              || candidate.language != Language::SystemVerilog2017
              || effective_library(candidate.library) != library) {
            continue;
          }
          if (selected != nullptr) {
            return nullptr;
          }
          selected = &candidate;
        }
        return selected;
      };
      const auto root_instance
          = scope.instances.find(components.front());
      const DesignUnit* target = select_unit(
          root_instance->second, scope.library);
      for (std::size_t index = 1U;
           target != nullptr && index + 1U < components.size(); ++index) {
        const Instance* child = nullptr;
        for (const auto& candidate : target->instances) {
          if (candidate.name != components[index]) {
            continue;
          }
          if (child != nullptr) {
            child = nullptr;
            break;
          }
          child = &candidate;
        }
        target = child == nullptr
            ? nullptr
            : select_unit(child->unit_name,
                  effective_library(target->library));
      }
      if (target != nullptr) {
        const VariableDeclaration* variable = nullptr;
        for (const auto& candidate : target->variables) {
          if (candidate.name != components.back()) {
            continue;
          }
          if (variable != nullptr) {
            variable = nullptr;
            break;
          }
          variable = &candidate;
        }
        if (variable != nullptr) {
          Scope declaration_scope = scope;
          declaration_scope.library = effective_library(target->library);
          declaration_scope.compilation_unit_identity
              = target->compilation_unit_identity;
          declaration_scope.lexical_identity
              = declaration_scope.library + "::" + target->name;
          declaration_scope.class_owner = nullptr;
          declaration_scope.objects.clear();
          declaration_scope.instances.clear();
          auto type = resolve_alias_type(
              variable->type, declaration_scope.lexical_identity);
          type = canonicalize_selected_type(
              std::move(type), declaration_scope);
          if (!class_identity(type) && !type.named_type.empty()) {
            const auto declarations = resolve_class_name(
                type.named_type, declaration_scope);
            if (declarations.size() == 1U) {
              auto selected_type = class_type(*declarations.front());
              selected_type.systemverilog_container
                  = type.systemverilog_container;
              type = std::move(selected_type);
            }
          }
          if (class_identity(type) && !type.systemverilog_container) {
            expression.nominal_type = *class_identity(type);
            return type;
          }
        }
      }
    }
  }

  const auto dot = expression.text.rfind('.');
  if (dot != std::string::npos) {
    Expression receiver{
        ExpressionKind::Identifier,
        expression.text.substr(0, dot),
        {},
        expression.span};
    const auto receiver_type = resolve_expression(receiver, scope);
    // A member of a packed structure class property (`c.p.a`, 7.2, 8.5).
    if (receiver_type && !class_identity(*receiver_type)
        && is_class_property_access(receiver)) {
      const auto member = expression.text.substr(dot + 1U);
      auto type = packed_member_type(*receiver_type, member);
      if (!type) return std::nullopt;
      expression.kind = ExpressionKind::Call;
      expression.text = "@sv-select:" + member;
      expression.operands = {std::move(receiver)};
      return type;
    }
    if (!receiver_type || !class_identity(*receiver_type)) return std::nullopt;
    const auto member = expression.text.substr(dot + 1U);
    const auto match = find_property(*class_identity(*receiver_type), member);
    if (match.property
        && !check_member_visibility(match, scope, member, expression.span)) {
      return std::nullopt;
    }
    if (!match.property) {
      // A parameter of the object's class specialization, `obj.P`, denotes
      // its value (IEEE 1800-2017 8.25).
      if (const auto* owner = find_class(*class_identity(*receiver_type))) {
        const auto parameter = std::ranges::find_if(
            owner->parameters, [&](const ParameterDeclaration& candidate) {
              return candidate.kind == ParameterKind::Value
                  && candidate.name == member;
            });
        if (parameter != owner->parameters.end()) {
          // The specialization's actual (positional or named), else the
          // parameter's default.
          const Expression* value = &parameter->default_value;
          const auto& actuals = receiver_type->systemverilog_class_parameter_actuals;
          const auto position = static_cast<std::size_t>(
              parameter - owner->parameters.begin());
          for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto& actual = actuals[index];
            if (actual.type_actual) {
              continue;
            }
            if ((actual.name && *actual.name == member)
                || (!actual.name && index == position)) {
              value = &actual.value;
              break;
            }
          }
          if (value->kind != ExpressionKind::Identifier && !value->text.empty()) {
            const auto span = expression.span;
            expression = *value;
            expression.span = span;
            return resolve_expression(expression, scope);
          }
        }
      }
      // `p.randomize` without parentheses calls the built-in method
      // (IEEE 1800-2017 13.5.5, 18.6.1).
      if (member == "randomize"
          && find_methods(*class_identity(*receiver_type), member,
                 SystemVerilogClassMethodKind::Function)
                 .empty()) {
        expression.kind = ExpressionKind::Call;
        expression.text = ".randomize";
        expression.operands = {std::move(receiver)};
        return resolve_call(expression, scope, std::nullopt,
            SystemVerilogScalarKind::None);
      }
      const auto method = select_method(
          *class_identity(*receiver_type),
          member,
          SystemVerilogClassMethodKind::Function,
          0U,
          expression.span);
      if (method && method->method) {
        expression.kind = ExpressionKind::Call;
        if (method->method->is_static) {
          // A static method called through a handle (8.10).
          expression.text = "@sv-static-method:"
              + method->method->canonical_identity;
          expression.operands.clear();
          retain_call_profile(expression, *method->method, false);
        } else {
          expression.text = "@sv-method:"
              + method->method->canonical_identity;
          expression.operands = {std::move(receiver)};
          retain_call_profile(expression, *method->method, true);
        }
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
    auto type = resolve_alias_type(
        match.property->declaration.type,
        match.owner->canonical_identity);
    // A property declared with a type parameter of the receiver's class
    // has the specialization's type actual (IEEE 1800-2017 8.25).
    if (const auto& named = match.property->declaration.type.named_type;
        !named.empty() && class_identity(*receiver_type)
            && *class_identity(*receiver_type)
                == match.owner->canonical_identity) {
      const auto& parameters = match.owner->parameters;
      const auto formal = std::ranges::find_if(parameters,
          [&](const ParameterDeclaration& parameter) {
            return parameter.kind == ParameterKind::Type
                && parameter.name == named;
          });
      if (formal != parameters.end()) {
        const auto position = static_cast<std::size_t>(
            std::distance(parameters.begin(), formal));
        const auto& actuals =
            receiver_type->systemverilog_class_parameter_actuals;
        for (std::size_t index = 0; index < actuals.size(); ++index) {
          const auto& actual = actuals[index];
          if ((actual.name && *actual.name != named)
              || (!actual.name && index != position)) {
            continue;
          }
          if (actual.type_actual) {
            type = canonicalize_selected_type(*actual.type_actual, scope);
          } else if (actual.value.kind == ExpressionKind::Identifier
              && !actual.value.text.empty()) {
            // A positional actual that names a type is parsed as a name.
            Type named_actual;
            named_actual.named_type = actual.value.text;
            named_actual.spelling = actual.value.text;
            named_actual = canonicalize_selected_type(
                std::move(named_actual), scope);
            if (!named_actual.systemverilog_class_declaration.empty()) {
              type = std::move(named_actual);
            }
          }
          break;
        }
      }
    }
    if (match.property->is_static) {
      // A static property reached through an object handle denotes the
      // class's single storage (IEEE 1800-2017 8.9).
      expression.text = (type.systemverilog_container
              ? "@sv-static-container-property:"
              : "@sv-static-property:")
          + match.owner->canonical_identity + "::" + member;
      expression.operands.clear();
    } else {
      expression.text = (type.systemverilog_container
              ? "@sv-container-property:"
              : "@sv-property:")
          + match.owner->canonical_identity + "::" + member;
      expression.operands = {std::move(receiver)};
    }
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
    // Outside a class, the class scope operator needs a specialization of
    // a parameterized class, `C#(...)::name` (IEEE 1800-2017 8.25.1).
    if (owners.size() == 1U && scope.class_owner == nullptr
        && owner_name.find('#') == std::string::npos
        // A typedef of a specialization already supplies the values.
        && (owner_name == owners.front()->name
            || owner_name.ends_with("::" + owners.front()->name))
        && std::ranges::any_of(
            owners.front()->parameters,
            [](const ParameterDeclaration& parameter) {
              return !parameter.local;
            })) {
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-023",
          "class scope resolution of parameterized class '" + owner_name
              + "' requires a parameter value assignment, as in '"
              + owner_name + "#()::" + member + "'",
          expression.span);
      return std::nullopt;
    }
    // A value parameter or local parameter of a specialization,
    // `C#(7)::v` (IEEE 1800-2017 8.25.1): its value expression with the
    // specialization's parameter values substituted.
    if (owners.size() == 1U && owner_name.find('#') != std::string::npos) {
      if (auto value = class_specialization_parameter_value(
              *owners.front(), owner_name, member)) {
        const auto span = expression.span;
        expression = std::move(*value);
        expression.span = span;
        return resolve_expression(expression, scope);
      }
    }
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
      // A static function with no arguments called without parentheses,
      // `C::f` (IEEE 1800-2017 13.4.5, 8.10).
      if (!match.property
          && !find_methods(owners.front()->canonical_identity, member,
                  SystemVerilogClassMethodKind::Function)
                  .empty()) {
        const auto method = select_method(owners.front()->canonical_identity,
            member, SystemVerilogClassMethodKind::Function, 0U,
            expression.span);
        if (method && method->method && method->method->is_static) {
          expression.kind = ExpressionKind::Call;
          expression.text = "@sv-static-method:"
              + method->method->canonical_identity;
          expression.operands.clear();
          retain_call_profile(expression, *method->method, false);
          const auto type = resolve_alias_type(
              method->method->return_type,
              method->owner->canonical_identity);
          retain_result_type(expression, type);
          if (const auto identity = class_identity(type)) {
            expression.nominal_type = *identity;
          }
          return type;
        }
      }
    }
  }

  if (scope.class_owner != nullptr) {
    const auto match = find_property(
        scope.class_owner->canonical_identity, expression.text);
    if (match.property
        && !check_member_visibility(
            match, scope, expression.text, expression.span)) {
      return std::nullopt;
    }
    if (match.property) {
      const auto type = resolve_alias_type(
          match.property->declaration.type,
          match.owner->canonical_identity);
      // In a method body, an unpacked array, queue or associative array
      // of values stays a name of the property, which the elaborated
      // method reads and writes as a container of the object.
      if (scope.method_body && !match.property->is_static
          && type.systemverilog_container && !class_identity(type)
          && (type.named_type.empty()
              || resolve_class_name(type.named_type, scope).empty())) {
        return type;
      }
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
    // A nested class sees the static properties of its enclosing classes
    // (IEEE 1800-2017 8.23).
    for (std::string_view enclosing = scope.class_owner->canonical_identity;;) {
      const auto separator = enclosing.rfind("::");
      if (separator == std::string_view::npos) break;
      enclosing = enclosing.substr(0U, separator);
      if (find_class(enclosing) == nullptr) break;
      const auto outer = find_property(enclosing, expression.text);
      if (!outer.property) continue;
      if (!outer.property->is_static) break;
      const auto type = resolve_alias_type(
          outer.property->declaration.type, outer.owner->canonical_identity);
      expression.kind = ExpressionKind::Call;
      expression.text =
          (type.systemverilog_container ? "@sv-static-container-property:"
                                        : "@sv-static-property:")
          + outer.owner->canonical_identity + "::"
          + outer.property->declaration.name;
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

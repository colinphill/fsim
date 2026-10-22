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
    for (const auto& import : declaration.compilation_unit_imports) {
      scope.imports.push_back(&import);
    }
    // A class declared in a package sees that package's imports.
    const auto& identity = declaration.canonical_identity;
    const auto library_end = identity.find("::");
    const auto package_end = library_end == std::string::npos
        ? std::string::npos
        : identity.find("::", library_end + 2U);
    if (package_end != std::string::npos) {
      const auto package = std::string_view{identity}.substr(
          library_end + 2U, package_end - library_end - 2U);
      for (const auto& unit : design_.units) {
        if (unit.kind == UnitKind::SystemVerilogPackage
            && unit.name == package
            && effective_library(unit.library) == scope.library) {
          for (const auto& import : unit.systemverilog_imports) {
            scope.imports.push_back(&import);
          }
        }
      }
    }
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
      scope.imports.push_back(&import);
    }
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
        resolve_module_constant(parameter.default_value, scope);
      }
    }
    for (auto& instance : unit.instances) {
      for (auto& override_value : instance.parameter_overrides) {
        resolve_module_constant(override_value.value, scope);
        // A class-scoped type actual (`m #(C#(8)::t) u ();`), including
        // one through a class typedef (`m #(c8_t::t) u ();`) that parses
        // as a scoped name.
        if (!override_value.type_value
            && override_value.value.kind == ExpressionKind::Identifier
            && override_value.value.text.find("::") != std::string::npos) {
          Type named;
          named.named_type = override_value.value.text;
          named.spelling = named.named_type;
          if (auto scoped = resolve_class_scoped_type(named, scope)) {
            override_value.type_value = std::move(*scoped);
          }
        } else if (override_value.type_value) {
          if (auto scoped = resolve_class_scoped_type(
                  *override_value.type_value, scope)) {
            override_value.type_value = std::move(*scoped);
          } else if (auto value = class_scoped_parameter_value(
                         *override_value.type_value, scope)) {
            // `m #(C#(5)::p)` names a parameter of the specialization, a
            // value actual (IEEE 1800-2017 8.25.1).
            override_value.value = std::move(*value);
            override_value.type_value.reset();
            substitute_class_parameter_references(override_value.value, scope);
          }
        }
      }
    }
    // A typedef of a class-scoped type (`typedef C#(8)::t t_t;`) names
    // the type of that class specialization (IEEE 1800-2017 8.25.1).
    resolve_scoped_types(unit.type_aliases, scope);
    resolve_scoped_types(unit.ports, scope);
    resolve_scoped_types(unit.signals, scope);
    resolve_scoped_types(unit.variables, scope);
    // Class constants in packed dimensions (`logic [C#(5)::b-1:0] v;`).
    const auto ranges = [&](auto& declarations) {
      for (auto& declaration : declarations) {
        resolve_range_constants(declaration.type, scope);
      }
    };
    ranges(unit.ports);
    ranges(unit.signals);
    ranges(unit.variables);
    ranges(unit.type_aliases);
    // A variable declared with a forward typedef (`typedef T; T v;`) takes
    // the type the later typedef gives it (IEEE 1800-2017 6.18), which
    // matters when that type is a string.
    const auto resolve_forward_string = [&](auto& objects) {
      for (auto& object : objects) {
        if (object.type.named_type.empty()
            || object.type.spelling == "string"
            || object.type.systemverilog_container) {
          continue;
        }
        const auto alias = std::ranges::find_if(unit.type_aliases,
            [&](const TypeAliasDeclaration& candidate) {
              return candidate.name == object.type.named_type;
            });
        if (alias != unit.type_aliases.end()
            && alias->type.spelling == "string") {
          object.type = alias->type;
        }
      }
    };
    resolve_forward_string(unit.signals);
    resolve_forward_string(unit.variables);
    add_objects(scope, unit.signals);
    add_objects(scope, unit.variables);
    // `$bits(h.p)` of a class handle's property is a constant: the
    // property's type width (IEEE 1800-2017 20.6.2). Resolve the operand
    // of such queries in parameter values once the unit's objects are in
    // scope.
    const auto resolve_bits_operands = [&](const auto& self,
                                           Expression& expression) -> void {
      if (expression.kind == ExpressionKind::Call
          && expression.text == "$bits" && expression.operands.size() == 1U
          && expression.operands.front().kind == ExpressionKind::Identifier
          && expression.operands.front().text.find('.')
              != std::string::npos) {
        (void)resolve_expression(expression.operands.front(), scope);
        return;
      }
      for (auto& operand : expression.operands) self(self, operand);
    };
    for (auto& parameter : unit.parameters) {
      if (parameter.kind == ParameterKind::Value) {
        resolve_bits_operands(resolve_bits_operands, parameter.default_value);
      }
    }
    for (auto& instance : unit.instances) {
      for (auto& override_value : instance.parameter_overrides) {
        if (!override_value.type_value) {
          resolve_bits_operands(resolve_bits_operands, override_value.value);
        }
      }
    }
    // An object of a named conditional generate block is visible as
    // `label.name` (IEEE 1800-2017 27.6).
    for (const auto& region : unit.generate_regions) {
      if (region.kind != GenerateKind::Conditional) continue;
      const auto add_labelled = [&](const std::string& label,
                                    const GenerateBody& body) {
        if (label.empty()) return;
        for (const auto& declaration : body.variables) {
          scope.objects.add(
              label + "." + declaration.name, declaration.type);
        }
      };
      add_labelled(region.then_scope, region.then_body);
      add_labelled(region.else_scope, region.else_body);
    }
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
      class_scope.objects.hide_if([&](const std::string& name) {
        return find_property(declaration.canonical_identity, name)
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
      for (auto& alias : process.type_aliases) {
        if (auto scoped = resolve_class_scoped_type(alias.type, process_scope)) {
          alias.type = std::move(*scoped);
        }
        process_scope.aliases.insert_or_assign(alias.name, &alias.type);
      }
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
    // Continuous assignments can call class methods (`assign c =
    // C#(3)::f(a);`, IEEE 1800-2017 8.10, 10.3), as in generate bodies.
    // Only those that name a class scope are visited; the resolver's
    // recursion is not needed for deep ordinary expressions.
    const auto names_class_scope = [](const Statement& statement) {
      std::vector<const Expression*> pending{
          &statement.target, &statement.value};
      while (!pending.empty()) {
        const auto* expression = pending.back();
        pending.pop_back();
        if ((expression->kind == ExpressionKind::Call
                || expression->kind == ExpressionKind::Identifier)
            && expression->text.find("::") != std::string::npos) {
          return true;
        }
        for (const auto& operand : expression->operands) {
          pending.push_back(&operand);
        }
      }
      return false;
    };
    for (auto& statement : unit.concurrent_statements) {
      if (names_class_scope(statement)) {
        resolve_statement(statement, scope, std::nullopt);
      }
    }
    for (auto& region : unit.generate_regions) {
      resolve_generate_constants(region, scope);
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
  for (auto& constant : body.constants) {
    if (constant.kind == ParameterKind::Value) {
      resolve_module_constant(constant.default_value, scope);
    }
  }
  resolve_scoped_types(body.type_aliases, scope);
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
    for (auto& alias : process.type_aliases) {
      if (auto scoped = resolve_class_scoped_type(alias.type, process_scope)) {
        alias.type = std::move(*scoped);
      }
      process_scope.aliases.insert_or_assign(alias.name, &alias.type);
    }
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
    resolve_generate_constants(region, scope);
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
[[nodiscard]] const SystemVerilogClassDeclaration* Resolver::find_base_class(
    const SystemVerilogClassDeclaration& declaration,
    const Type& receiver) const {
  if (!declaration.base || !declaration.base->declaration_identity.empty()) {
    return find_base_class(declaration);
  }
  // `class C #(type T = D) extends T`: the base is the receiver's actual
  // for T, and only the default without one (IEEE 1800-2017 8.25).
  const Scope scope{
      effective_library(declaration.library),
      declaration.compilation_unit_identity,
      declaration.canonical_identity,
      &declaration,
      {}};
  if (const auto actual = class_type_parameter_actual(
          declaration.base->name, declaration, receiver, scope)) {
    if (const auto identity = class_identity(*actual)) {
      if (const auto* base = find_class(*identity)) return base;
    }
  }
  return find_base_class(declaration);
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
  // A type parameter of a base class names the type the extends clause
  // binds to it, or its default (IEEE 1800-2017 8.13, 8.25).
  const auto inherited_parameter = [&](const std::string& identity)
      -> std::optional<Type> {
    const auto* current = find_class(identity);
    std::set<std::string> visited;
    while (current != nullptr && current->base
           && visited.insert(current->canonical_identity).second) {
      if (std::ranges::any_of(current->parameters,
              [&](const ParameterDeclaration& parameter) {
                return parameter.name == type.named_type;
              })) {
        return std::nullopt;
      }
      const auto* base = find_base_class(*current);
      if (base == nullptr) return std::nullopt;
      const auto formal = std::ranges::find(
          base->parameters, type.named_type, &ParameterDeclaration::name);
      if (formal == base->parameters.end()) {
        current = base;
        continue;
      }
      if (formal->kind != ParameterKind::Type) return std::nullopt;
      const auto index = static_cast<std::size_t>(
          std::distance(base->parameters.begin(), formal));
      const auto& actuals = current->base->parameter_actuals;
      const SystemVerilogClassParameterActual* actual = nullptr;
      for (std::size_t position = 0; position < actuals.size(); ++position) {
        if (actuals[position].name.empty()
                ? position == index
                : actuals[position].name == formal->name) {
          actual = &actuals[position];
          break;
        }
      }
      if (actual == nullptr) {
        return formal->default_type
            ? std::optional{resolve_alias_type(
                  *formal->default_type, base->canonical_identity)}
            : std::nullopt;
      }
      std::optional<Type> bound = actual->type_actual;
      if (!bound && actual->value.kind == ExpressionKind::Identifier) {
        bound = Type{};
        bound->named_type = actual->value.text;
      }
      if (!bound) return std::nullopt;
      if (class_identity(*bound) || bound->named_type.empty()) return bound;
      // A class or typedef named in the derived class's scope.
      const auto scope_end = current->canonical_identity.rfind("::");
      if (scope_end != std::string::npos) {
        if (const auto* named = find_class(
                current->canonical_identity.substr(0, scope_end) + "::"
                + bound->named_type)) {
          return class_type(*named);
        }
      }
      auto resolved = resolve_alias_type(*bound, current->canonical_identity);
      if (!class_identity(resolved)
          && resolved.named_type == bound->named_type) {
        return std::nullopt;
      }
      return resolved;
    }
    return std::nullopt;
  };
  for (;;) {
    auto alias = class_aliases_.find(
        lexical_identity + "::" + type.named_type);
    if (alias == class_aliases_.end()) {
      if (auto inherited = inherited_parameter(lexical_identity)) {
        if (type.systemverilog_container) {
          auto& elements = type.systemverilog_container->element_types;
          if (elements.empty()) {
            elements.push_back(std::move(*inherited));
          } else {
            elements.front() = std::move(*inherited);
          }
          return type;
        }
        return std::move(*inherited);
      }
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
    if (separator == std::string::npos) {
      // A compilation-unit typedef is visible to every unit of its file
      // (IEEE 1800-2017 3.12.1); its key names the unit package.
      const auto prefix = lexical_identity + "::fsim_unit_";
      const auto suffix = "::" + type.named_type;
      const Type* unique = nullptr;
      for (const auto& [key, candidate] : class_aliases_) {
        if (!key.starts_with(prefix) || !key.ends_with(suffix)
            || key.find("::", prefix.size()) != key.size() - suffix.size()) {
          continue;
        }
        if (unique != nullptr) return type;
        unique = candidate;
      }
      // Only a class typedef is resolved here; other compilation-unit
      // types keep their name for the HIR's own lookup.
      if (unique == nullptr || !class_identity(*unique)) return type;
      if (type.systemverilog_container) {
        auto& elements = type.systemverilog_container->element_types;
        if (elements.empty()) {
          elements.push_back(*unique);
        } else {
          elements.front() = *unique;
        }
        return type;
      }
      return *unique;
    }
    lexical_identity.resize(separator);
  }
}
std::optional<Type> Resolver::class_type_parameter_actual(
    const std::string& named, const SystemVerilogClassDeclaration& owner,
    const Type& receiver, const Scope& scope) const {
  const auto& parameters = owner.parameters;
  const auto formal = std::ranges::find_if(parameters,
      [&](const ParameterDeclaration& parameter) {
        return parameter.kind == ParameterKind::Type
            && parameter.name == named;
      });
  const auto receiver_identity = class_identity(receiver);
  if (formal == parameters.end() || !receiver_identity) return std::nullopt;
  const auto position = static_cast<std::size_t>(
      std::distance(parameters.begin(), formal));
  const auto from_actual = [&](const std::optional<Type>& type_actual,
                               const Expression& value)
      -> std::optional<Type> {
    if (type_actual) return canonicalize_selected_type(*type_actual, scope);
    if (value.kind == ExpressionKind::Identifier && !value.text.empty()) {
      Type named_actual;
      named_actual.named_type = value.text;
      named_actual.spelling = value.text;
      named_actual = canonicalize_selected_type(
          std::move(named_actual), scope);
      if (!named_actual.systemverilog_class_declaration.empty()) {
        return named_actual;
      }
    }
    return std::nullopt;
  };
  if (*receiver_identity == owner.canonical_identity) {
    const auto& actuals = receiver.systemverilog_class_parameter_actuals;
    for (std::size_t index = 0; index < actuals.size(); ++index) {
      const auto& actual = actuals[index];
      if ((actual.name && *actual.name != named)
          || (!actual.name && index != position)) {
        continue;
      }
      return from_actual(actual.type_actual
              ? std::optional<Type>{*actual.type_actual} : std::nullopt,
          actual.value);
    }
    if (actuals.empty() && formal->default_type) {
      auto defaulted = canonicalize_selected_type(*formal->default_type, scope);
      if (!defaulted.systemverilog_class_declaration.empty()) {
        return defaulted;
      }
    }
    return std::nullopt;
  }
  const auto* current = find_class(*receiver_identity);
  std::set<std::string> visited;
  while (current != nullptr && current->base
      && visited.insert(current->canonical_identity).second) {
    if (current->base->declaration_identity == owner.canonical_identity) {
      const auto& actuals = current->base->parameter_actuals;
      for (std::size_t index = 0; index < actuals.size(); ++index) {
        const auto& actual = actuals[index];
        if ((!actual.name.empty() && actual.name != named)
            || (actual.name.empty() && index != position)) {
          continue;
        }
        return from_actual(actual.type_actual, actual.value);
      }
      return std::nullopt;
    }
    current = find_base_class(*current);
  }
  return std::nullopt;
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
  // A nested structure member keeps its own members, so a further member
  // can be selected (`c.s.inner.id`, IEEE 1800-2017 7.2).
  if (found->nested_types.size() == 1U
      && !found->nested_types.front().packed_members.empty()) {
    return found->nested_types.front();
  }
  Type result;
  result.domain = found->domain;
  result.spelling = found->spelling;
  result.packed_range = found->packed_range;
  result.is_signed = found->is_signed;
  // A member of a named structure type (`inner_t inner`).
  if (!found->spelling.empty()
      && std::ranges::all_of(found->spelling, [](const char character) {
           return std::isalnum(static_cast<unsigned char>(character)) != 0
               || character == '_' || character == '$';
         })) {
    std::size_t matches{};
    for (const auto& [key, alias] : class_aliases_) {
      if (key.ends_with("::" + found->spelling)
          && !alias->packed_members.empty()) {
        ++matches;
      }
    }
    if (matches == 1U) {
      result.named_type = found->spelling;
    }
  }
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
void Resolver::resolve_class_scope_constants(
    Expression& expression, const Scope& scope, const std::size_t depth) {
  if (depth > 64U) return;
  // $bits of a class-scoped data type is its width (IEEE 1800-2017 20.6.2).
  const auto* type_operand = expression.systemverilog_type_operand.get();
  if (expression.kind == ExpressionKind::Call && expression.text == "$bits"
      && ((type_operand != nullptr
              && type_operand->named_type.find("::") != std::string::npos)
          || (expression.operands.size() == 1U
              && expression.operands.front().kind
                  == ExpressionKind::Identifier
              && expression.operands.front().text.find("::")
                  != std::string::npos))) {
    Type named;
    if (type_operand != nullptr) {
      named = *type_operand;
    } else {
      named.named_type = expression.operands.front().text;
      named.spelling = named.named_type;
      // `C#(6)::t` keeps its actuals (IEEE 1800-2017 8.25.1).
      if (const auto& selection
          = expression.operands.front().systemverilog_class_selection) {
        named.systemverilog_class_parameter_actuals
            = selection->systemverilog_class_parameter_actuals;
      }
    }
    if (const auto scoped = resolve_class_scoped_type(named, scope);
        scoped && !scoped->systemverilog_container) {
      if (const auto width = scoped->width()) {
        const auto span = expression.span;
        expression = Expression{
            ExpressionKind::IntegerLiteral, std::to_string(*width), {}, span};
        return;
      }
    }
  }
  if (expression.kind == ExpressionKind::Identifier
      && expression.text.find('.') == std::string::npos) {
    const auto at = expression.text.rfind("::");
    if (at != std::string::npos && at != 0U) {
      const auto member = expression.text.substr(at + 2U);
      const auto selection = resolve_class_selection(
          std::string_view{expression.text}.substr(0U, at), scope,
          expression.systemverilog_class_selection.get());
      if (selection && selection->declaration != nullptr) {
        const auto& owner = *selection->declaration;
        const auto match = find_property(owner.canonical_identity, member);
        const auto formal = std::ranges::find_if(owner.parameters,
            [&](const ParameterDeclaration& parameter) {
              return parameter.kind == ParameterKind::Value
                  && parameter.name == member;
            });
        if ((match.property != nullptr && match.property->is_parameter)
            || (match.property == nullptr
                && formal != owner.parameters.end())) {
          const auto* declaring =
              match.property != nullptr ? match.owner : &owner;
          const auto type = match.property != nullptr
              ? resolve_alias_type(match.property->declaration.type,
                    declaring->canonical_identity)
              : formal->type;
          expression.kind = ExpressionKind::Call;
          expression.text = "@sv-static-property:"
              + declaring->canonical_identity + "::" + member;
          expression.operands.clear();
          if (!selection->type.systemverilog_class_parameter_actuals.empty()) {
            expression.systemverilog_class_owner =
                std::make_shared<const Type>(selection->type);
          }
          retain_result_type(expression, type);
          return;
        }
      }
    }
  }
  for (auto& operand : expression.operands) {
    resolve_class_scope_constants(operand, scope, depth + 1U);
  }
}
void Resolver::resolve_module_constant(
    Expression& expression, const Scope& scope) {
  substitute_class_parameter_references(expression, scope);
  resolve_class_scope_constants(expression, scope);
}
void Resolver::resolve_generate_constants(
    GenerateRegion& region, const Scope& scope) {
  resolve_module_constant(region.initial, scope);
  resolve_module_constant(region.condition, scope);
  resolve_module_constant(region.iteration, scope);
  for (auto& alternative : region.alternatives) {
    for (auto& choice : alternative.choices) {
      resolve_module_constant(choice.left, scope);
      if (choice.right) resolve_module_constant(*choice.right, scope);
    }
  }
}
void Resolver::resolve_range_constants(Type& type, const Scope& scope) {
  if (type.packed_range_expression) {
    resolve_module_constant(type.packed_range_expression->left, scope);
    resolve_module_constant(type.packed_range_expression->right, scope);
  }
  for (auto& range : type.systemverilog_packed_dimensions) {
    resolve_module_constant(range.left, scope);
    resolve_module_constant(range.right, scope);
  }
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
  // `CFG::member` through `typedef C#(5) CFG;` (IEEE 1800-2017 8.25.1).
  if (expression.kind == ExpressionKind::Identifier
      && expression.text.find('#') == std::string::npos) {
    const auto selected = expression.text.rfind("::");
    if (selected != std::string::npos && selected != 0U
        && expression.text.find("::") == selected) {
      const auto alias_name = expression.text.substr(0, selected);
      const Type* alias{};
      for (std::string lexical = scope.lexical_identity;;) {
        if (const auto found = class_aliases_.find(
                lexical + "::" + alias_name);
            found != class_aliases_.end()) {
          alias = found->second;
          break;
        }
        const auto parent = lexical.rfind("::");
        if (parent == std::string::npos) break;
        lexical.resize(parent);
      }
      const auto spelling = alias != nullptr
          ? class_specialization_spelling(alias->spelling, *alias)
          : std::nullopt;
      if (spelling) {
        const auto owners = resolve_class_name(*spelling, scope);
        if (owners.size() == 1U) {
          if (auto value = class_specialization_parameter_value(
                  *owners.front(), *spelling,
                  expression.text.substr(selected + 2U))) {
            const auto span = expression.span;
            expression = std::move(*value);
            expression.span = span;
            substitute_class_parameter_references(
                expression, scope, depth + 1U);
            return;
          }
        }
      }
    }
  }
  for (auto& operand : expression.operands) {
    substitute_class_parameter_references(operand, scope, depth + 1U);
  }
}
// A class specialization spelled with its positional literal or name
// actuals (`C#(5)`), the form class_specialization_parameter_value reads.
std::optional<std::string> Resolver::class_specialization_spelling(
    const std::string& owner, const Type& type) {
  if (type.systemverilog_class_parameter_actuals.empty()) return std::nullopt;
  std::string spelling = owner + "#(";
  bool first = true;
  for (const auto& actual : type.systemverilog_class_parameter_actuals) {
    if (actual.name || actual.type_actual
        || (actual.value.kind != ExpressionKind::IntegerLiteral
            && actual.value.kind != ExpressionKind::Identifier)
        || !actual.value.operands.empty()) {
      return std::nullopt;
    }
    spelling += (first ? "" : ",") + actual.value.text;
    first = false;
  }
  return spelling + ")";
}
std::optional<Expression> Resolver::class_scoped_parameter_value(
    const Type& type, const Scope& scope) {
  const auto selected = type.named_type.rfind("::");
  if (selected == std::string::npos) return std::nullopt;
  const auto spelling = class_specialization_spelling(
      type.named_type.substr(0, selected), type);
  if (!spelling) return std::nullopt;
  const auto owners = resolve_class_name(*spelling, scope);
  if (owners.size() != 1U) return std::nullopt;
  return class_specialization_parameter_value(
      *owners.front(), *spelling, type.named_type.substr(selected + 2U));
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
    // `Inner#(W)::v`: the specialization's literal actuals replace its
    // parameter names inside a nested specialization's actual list.
    if (value.kind == ExpressionKind::Identifier
        && value.text.find("#(") != std::string::npos) {
      auto& text = value.text;
      for (const auto& [name, actual] : values) {
        if (actual.kind != ExpressionKind::IntegerLiteral) continue;
        for (std::size_t at = text.find(name); at != std::string::npos;
             at = text.find(name, at + 1U)) {
          const auto word = [&](const char character) {
            return std::isalnum(static_cast<unsigned char>(character)) != 0
                || character == '_' || character == '$';
          };
          const auto end = at + name.size();
          if ((at != 0U && word(text[at - 1U]))
              || (end < text.size() && word(text[end]))
              || text.rfind("#(", at) == std::string::npos) {
            continue;
          }
          text.replace(at, name.size(), actual.text);
          at += actual.text.size() - 1U;
        }
      }
      return value;
    }
    // A sibling local parameter already resolved to the class's static
    // property (`@sv-static-property:C::base`) names it again.
    const auto static_prefix = "@sv-static-property:"
        + owner.canonical_identity + "::";
    if (value.kind == ExpressionKind::Call && value.operands.empty()
        && value.text.starts_with(static_prefix)) {
      value = Expression{ExpressionKind::Identifier,
          value.text.substr(static_prefix.size()), {}, value.span};
    }
    // A member of a structure parameter (`c.depth`): its root is the
    // actual's name, or else the value is not substituted here.
    if (value.kind == ExpressionKind::Identifier
        && value.text.find('.') != std::string::npos) {
      const auto dot = value.text.find('.');
      const auto root = std::string_view{value.text}.substr(0U, dot);
      if (const auto found = values.find(root);
          found != values.end()
          && found->second.kind == ExpressionKind::Identifier) {
        value.text = found->second.text + value.text.substr(dot);
        return value;
      }
      const bool class_name = std::ranges::any_of(owner.parameters,
          [&](const ParameterDeclaration& candidate) {
            return candidate.name == root;
          }) || std::ranges::any_of(owner.properties,
          [&](const SystemVerilogClassProperty& candidate) {
            return candidate.is_parameter
                && candidate.declaration.name == root;
          });
      if (class_name) return std::nullopt;
      return value;
    }
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
      // A sibling local parameter of the class body (8.25).
      const auto sibling = std::ranges::find_if(owner.properties,
          [&](const SystemVerilogClassProperty& candidate) {
            return candidate.is_parameter
                && candidate.declaration.name == value.text
                && candidate.declaration.initializer;
          });
      if (sibling != owner.properties.end()) {
        return self(self, *sibling->declaration.initializer, depth + 1U);
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
    const std::string_view spelling, const Scope& scope,
    const Type* parsed) const {
  const auto declarations = resolve_class_name(spelling, scope);
  if (declarations.size() != 1U) return std::nullopt;
  ClassSelection selection{
      declarations.front(), class_type(*declarations.front())};

  // A typedef of a specialization (`C::T`, `P::T` or `T`) supplies its
  // parameter values (IEEE 1800-2017 8.25.1).
  const auto hash = spelling.find('#');
  const auto selected = spelling.rfind("::");
  std::optional<Type> alias_type;
  if (hash == std::string_view::npos && selected == std::string_view::npos) {
    Type named;
    named.named_type = std::string{spelling};
    if (const auto local = scope.aliases.find(spelling);
        local != scope.aliases.end()) {
      if (!local->second->systemverilog_class_parameter_actuals.empty()) {
        alias_type = *local->second;
      }
    } else if (auto resolved = resolve_alias_type(
                   named, scope.lexical_identity);
        !resolved.systemverilog_class_parameter_actuals.empty()) {
      alias_type = std::move(resolved);
    }
  } else if (selected != std::string_view::npos
      && (hash == std::string_view::npos || selected < hash)) {
    const auto owner = spelling.substr(0, selected);
    const auto name = std::string{spelling.substr(selected + 2U)};
    const auto owners = resolve_class_name(owner, scope);
    if (owners.size() == 1U) {
      if (const auto alias = class_aliases_.find(
              owners.front()->canonical_identity + "::" + name);
          alias != class_aliases_.end()) {
        alias_type = *alias->second;
      }
    } else if (owner.find("::") == std::string_view::npos) {
      for (const auto& unit : design_.units) {
        if (unit.name != owner) continue;
        if (const auto alias = class_aliases_.find(
                effective_library(unit.library) + "::" + unit.name + "::"
                + name);
            alias != class_aliases_.end()) {
          alias_type = *alias->second;
          break;
        }
      }
    }
  }
  if (alias_type) {
    const auto identity = class_identity(*alias_type);
    if (identity && *identity == selection.declaration->canonical_identity) {
      selection.type = std::move(*alias_type);
      normalize_selection_actuals(selection, scope);
      return selection;
    }
  }

  // Actuals the parser kept as expressions and types bind as such, also
  // when they name enclosing parameters (`R#(logic [W-1:0])`, `R#(P#(T))`,
  // IEEE 1800-2017 8.25.1).
  if (parsed != nullptr && parsed->spelling == spelling
      && !parsed->systemverilog_class_parameter_actuals.empty()) {
    std::size_t next_positional{};
    for (const auto& actual : parsed->systemverilog_class_parameter_actuals) {
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
      SystemVerilogClassTypeActual retained = actual;
      if (retained.type_actual
          && retained.type_actual->packed_range_expression) {
        // The range binds in the specialization that evaluates it
        // (`R#(logic [W-1:0])` in a class body, IEEE 1800-2017 8.25).
        auto unfolded = *retained.type_actual;
        unfolded.packed_range.reset();
        retained.type_actual = std::make_shared<Type>(std::move(unfolded));
      }
      if (formal.kind != ParameterKind::Type && retained.type_actual) {
        // A name read as a type is a value here.
        retained.value = Expression{ExpressionKind::Identifier,
            retained.type_actual->named_type, {}, actual.span};
        retained.type_actual.reset();
      }
      if (formal.kind != ParameterKind::Type && !retained.value.valid()) {
        return std::nullopt;
      }
      selection.type.systemverilog_class_parameter_actuals.push_back(
          std::move(retained));
    }
    normalize_selection_actuals(selection, scope, true);
    return selection;
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
      // An actual naming a parameter (`C#(W)` in a class body) stays a
      // name, so it binds in the specialization that evaluates it
      // (IEEE 1800-2017 8.25).
      const bool name_actual = !source.empty()
          && (std::isalpha(static_cast<unsigned char>(source.front())) != 0
              || source.front() == '_')
          && std::ranges::all_of(source, [](const char character) {
               return std::isalnum(static_cast<unsigned char>(character)) != 0
                   || character == '_' || character == '$';
             });
      retained.value = Expression{
          source.starts_with('"') ? ExpressionKind::StringLiteral
              : name_actual       ? ExpressionKind::Identifier
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
    Type type, const ClassSelection& selection) const {
  std::map<std::string, Type, std::less<>> bindings;
  std::vector<bool> assigned(selection.declaration->parameters.size());
  std::vector<const Expression*> value_actuals(
      selection.declaration->parameters.size());
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
    } else if (formal.kind != ParameterKind::Type
               && actual.value.kind != ExpressionKind::Invalid) {
      value_actuals[formal_index] = &actual.value;
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
  // Value parameters size the selected type's ranges (`C#(8)::t` with
  // `typedef logic [W-1:0] t`).
  std::vector<std::pair<std::string, const Expression*>> values;
  bool any_value_actual{};
  for (std::size_t index = 0;
       index < selection.declaration->parameters.size(); ++index) {
    const auto& formal = selection.declaration->parameters[index];
    if (formal.kind == ParameterKind::Type) continue;
    any_value_actual = any_value_actual || value_actuals[index] != nullptr;
    values.emplace_back(formal.name, value_actuals[index] != nullptr
        ? value_actuals[index]
        : formal.default_value.kind != ExpressionKind::Invalid
        ? &formal.default_value
        : nullptr);
  }
  // Class local parameters (`localparam int n = W * 2`) follow the
  // parameters they are computed from (IEEE 1800-2017 8.25, 6.20.4); with
  // no actual, from the parameters' defaults.
  const bool any_default = std::ranges::any_of(values,
      [](const auto& value) { return value.second != nullptr; });
  if (any_value_actual || any_default) {
    for (const auto& property : selection.declaration->properties) {
      if (property.is_parameter && property.declaration.initializer
          && std::ranges::none_of(values, [&](const auto& value) {
               return value.first == property.declaration.name;
             })) {
        values.emplace_back(property.declaration.name,
            &*property.declaration.initializer);
      }
    }
  }
  // Type parameters bound here also type the selected type's members
  // (`typedef struct packed {T a;} t`), with their own value-parameter
  // ranges (`localparam type T = logic [W-1:0]`).
  std::map<std::string, Type> type_bindings;
  for (const auto& [name, bound] : bindings) {
    type_bindings.emplace(name, any_value_actual || any_default
        ? specialize_selected_class_type(bound, values, {})
        : bound);
  }
  if (any_value_actual || any_default || !type_bindings.empty()) {
    type = specialize_selected_class_type(
        std::move(type), values, type_bindings);
  }
  // Substitution is simultaneous: a bound type names the outer scope's
  // parameters, so it is not substituted again (`T` bound to `pool#(T)`).
  std::set<std::string> replacing;
  const auto substitute = [&](const auto& self, Type retained) -> Type {
    const auto outer_container = retained.systemverilog_container;
    std::optional<std::string> replaced;
    if (const auto found = bindings.find(retained.named_type);
        found != bindings.end() && !replacing.contains(found->first)) {
      replaced = found->first;
      replacing.insert(*replaced);
      retained = found->second;
      if (outer_container) retained.systemverilog_container = outer_container;
    }
    if (!replaced) {
      // `T::Name`: a type declared by the class that type parameter T is
      // bound to (IEEE 1800-2017 8.25.1).
      const auto cut = retained.named_type.find("::");
      if (cut != std::string::npos && cut != 0U) {
        const auto found = bindings.find(retained.named_type.substr(0U, cut));
        if (found != bindings.end() && !replacing.contains(found->first)) {
          if (const auto owner = class_identity(found->second)) {
            if (const auto alias = class_aliases_.find(
                    *owner + "::" + retained.named_type.substr(cut + 2U));
                alias != class_aliases_.end()) {
              retained = *alias->second;
              if (outer_container) {
                retained.systemverilog_container = outer_container;
              }
            }
          }
        }
      }
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
            self(self, *actual.type_actual));
      } else if (actual.value.kind == ExpressionKind::Identifier) {
        // A type parameter named as an actual (`item#(T)`, parsed as an
        // expression) is the type it is bound to.
        if (const auto found = bindings.find(actual.value.text);
            found != bindings.end() && !replacing.contains(found->first)) {
          actual.type_actual = std::make_shared<Type>(found->second);
          actual.value = {};
        }
      }
    }
    if (replaced) {
      replacing.erase(*replaced);
    }
    return retained;
  };
  return substitute(substitute, std::move(type));
}
void Resolver::resolve_class_scoped_members(
    Type& type, const Scope& scope) const {
  bool changed{};
  for (auto& member : type.packed_members) {
    for (auto& nested : member.nested_types) {
      if (auto scoped = resolve_class_scoped_type(nested, scope)) {
        nested = std::move(*scoped);
        member.domain = nested.domain;
        member.is_signed = nested.is_signed;
        changed = true;
      } else if (!nested.packed_members.empty()) {
        const auto before = nested.width();
        resolve_class_scoped_members(nested, scope);
        changed = changed || nested.width() != before;
      }
    }
  }
  if (!changed || type.packed_members.empty()
      || !type.systemverilog_packed_dimensions.empty()) {
    return;
  }
  const bool is_union = type.packed_aggregate == PackedAggregateKind::Union;
  if (!is_union && type.packed_aggregate != PackedAggregateKind::Struct) {
    return;
  }
  std::uint64_t total{};
  for (const auto& member : type.packed_members) {
    const auto width = member.width();
    if (!width || *width == 0U) return;
    total = is_union ? std::max(total, *width) : total + *width;
  }
  if (!is_union) {
    auto offset = total;
    for (auto& member : type.packed_members) {
      offset -= *member.width();
      member.lsb_offset = offset;
    }
  }
  type.packed_range = PackedRange{
      static_cast<std::int64_t>(total - 1U), 0, true};
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
// A type parameter's actual named as an expression (`C#(D)`) denotes the
// named type (IEEE 1800-2017 8.25).
void Resolver::normalize_selection_actuals(
    ClassSelection& selection, const Scope& scope, const bool parsed) const {
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
    } else if (actual.type_actual) {
      auto resolved = *actual.type_actual;
      if (resolved.systemverilog_class_declaration.empty()
          && !resolved.named_type.empty()) {
        resolved = selected_type_actual(actual.type_actual->named_type, scope);
      } else if (parsed && resolved.named_type.empty()
          && !resolved.packed_range_expression
          && resolved.systemverilog_packed_dimensions.empty()
          && !resolved.systemverilog_container
          && (resolved.spelling == "string"
              || (systemverilog_integral_type_descriptor(resolved.spelling)
                  && resolved.is_signed
                      == systemverilog_integral_type_descriptor(
                             resolved.spelling)->default_signed
                  && (!resolved.packed_range
                      || systemverilog_integral_type_descriptor(
                             resolved.spelling)->fixed_width)))) {
        // A built-in type keyword names its predefined type.
        resolved = selected_type_actual(resolved.spelling, scope);
      }
      // A class specialization as the actual (`R#(P#(T))`) keeps its own
      // actuals, normalized in turn (IEEE 1800-2017 8.25).
      if (const auto* nested = resolved.systemverilog_class_declaration
                  .empty()
              ? nullptr
              : find_class(resolved.systemverilog_class_declaration);
          nested != nullptr
          && !actual.type_actual->systemverilog_class_parameter_actuals
                  .empty()) {
        ClassSelection inner{nested, std::move(resolved)};
        inner.type.systemverilog_class_parameter_actuals =
            actual.type_actual->systemverilog_class_parameter_actuals;
        normalize_selection_actuals(inner, scope, parsed);
        resolved = std::move(inner.type);
      }
      actual.type_actual = std::make_shared<Type>(std::move(resolved));
    }
  }
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
    // A typedef a base class declares (IEEE 1800-2017 8.13), seen in the
    // base specialization the extends clause selects.
    ClassSelection inherited{owners.front(), class_type(*owners.front())};
    inherited.type.systemverilog_class_parameter_actuals =
        type.systemverilog_class_parameter_actuals;
    normalize_selection_actuals(inherited, scope);
    std::set<std::string> visited;
    while (inherited.declaration->base
           && visited.insert(inherited.declaration->canonical_identity)
                  .second) {
      const auto* base = find_base_class(*inherited.declaration);
      if (base == nullptr) break;
      auto base_type = class_type(*base);
      for (const auto& actual :
           inherited.declaration->base->parameter_actuals) {
        SystemVerilogClassTypeActual retained;
        if (!actual.name.empty()) retained.name = actual.name;
        retained.value = actual.value;
        if (actual.type_actual) {
          retained.type_actual = std::make_shared<Type>(*actual.type_actual);
        }
        retained.span = actual.span;
        base_type.systemverilog_class_parameter_actuals.push_back(
            std::move(retained));
      }
      ClassSelection next{base,
          specialize_selected_type(std::move(base_type), inherited)};
      const auto base_alias = class_aliases_.find(base->canonical_identity
          + "::" + type.named_type.substr(separator + 2U));
      if (base_alias != class_aliases_.end()) {
        return specialize_class_alias(
            type, scope, std::move(next), *base_alias->second);
      }
      inherited = std::move(next);
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
  return specialize_class_alias(type, scope, std::move(selection),
      *alias->second);
}
// A package typedef's actuals name that package's parameters (IEEE
// 1800-2017 26.3), wherever the typedef is used.
void Resolver::anchor_alias_actuals(
    std::vector<SystemVerilogClassTypeActual>& actuals,
    const std::string_view declaring) const {
  const auto cut = declaring.rfind("::");
  const auto package_name = cut == std::string_view::npos
      ? declaring : declaring.substr(cut + 2U);
  const auto library = cut == std::string_view::npos
      ? std::string_view{} : declaring.substr(0U, cut);
  const auto unit = std::ranges::find_if(design_.units,
      [&](const DesignUnit& candidate) {
        return candidate.kind == UnitKind::SystemVerilogPackage
            && candidate.name == package_name
            && (library.empty()
                || effective_library(candidate.library) == library);
      });
  if (unit == design_.units.end()) return;
  const auto qualify = [&](const auto& self, Expression& expression) -> void {
    if (expression.kind == ExpressionKind::Identifier) {
      if (expression.text.find("::") != std::string::npos) return;
      const auto root = std::string_view{expression.text}.substr(
          0U, expression.text.find('.'));
      if (std::ranges::any_of(unit->parameters,
              [&](const ParameterDeclaration& parameter) {
                return parameter.kind == ParameterKind::Value
                    && parameter.name == root;
              })) {
        expression.text = unit->name + "::" + expression.text;
      }
      return;
    }
    for (auto& operand : expression.operands) self(self, operand);
  };
  for (auto& actual : actuals) {
    if (!actual.type_actual) qualify(qualify, actual.value);
  }
}
[[nodiscard]] Type Resolver::specialize_class_alias(
    const Type& type, const Scope& scope, ClassSelection selection,
    const Type& alias_type) const {
  const auto separator = type.named_type.rfind("::");
  // `typedef C#(8) c8_t; c8_t::t`: the typedef's specialization actuals.
  if (selection.type.systemverilog_class_parameter_actuals.empty()) {
    const auto owner_name = type.named_type.substr(0U, separator);
    std::string lexical = scope.lexical_identity;
    for (;;) {
      const auto owner_alias = class_aliases_.find(
          lexical + "::" + owner_name);
      if (owner_alias != class_aliases_.end()) {
        selection.type.systemverilog_class_parameter_actuals =
            owner_alias->second->systemverilog_class_parameter_actuals;
        anchor_alias_actuals(
            selection.type.systemverilog_class_parameter_actuals,
            std::string_view{owner_alias->first}.substr(
                0U, owner_alias->first.rfind("::")));
        break;
      }
      const auto parent = lexical.rfind("::");
      if (parent == std::string::npos) break;
      lexical.resize(parent);
    }
  }
  normalize_selection_actuals(selection, scope);
  auto resolved = specialize_selected_type(alias_type, selection);
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
  // A typedef of a class declared in an enclosing block.
  if (const auto alias = scope.aliases.find(normalized);
      alias != scope.aliases.end()) {
    if (const auto identity = class_identity(*alias->second)) {
      if (const auto declaration = find_class(*identity)) return {declaration};
    }
  }
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
  // A nested class inherited from a base class (IEEE 1800-2017 8.13, 8.23).
  if (normalized.find("::") == std::string::npos) {
    for (lexical = scope.lexical_identity;;) {
      if (const auto* owner = find_class(lexical)) {
        std::set<std::string> visited;
        for (auto base = find_base_class(*owner);
             base != nullptr
             && visited.insert(base->canonical_identity).second;
             base = find_base_class(*base)) {
          if (const auto nested = find_class(
                  base->canonical_identity + "::" + normalized)) {
            return {nested};
          }
        }
      }
      const auto separator = lexical.rfind("::");
      if (separator == std::string::npos) break;
      lexical.resize(separator);
    }
  }
  // A class made visible by a package import (IEEE 1800-2017 26.3).
  if (normalized.find("::") == std::string::npos) {
    append([&](const std::string& identity, const auto& declaration) {
      return declaration.name == normalized
          && std::ranges::any_of(scope.imports, [&](const auto* import) {
               return (import->name.empty() || import->name == normalized)
                   && identity.ends_with(
                       "::" + import->package + "::" + normalized);
             });
    });
    if (!matches.empty()) return matches;
  }
  append([&](const std::string& identity, const auto& declaration) {
    if (declaration.library != scope.library) return false;
    if (normalized.find("::") != std::string::npos) {
      return identity.ends_with(normalized)
          && identity.size() > normalized.size()
          && identity[identity.size() - normalized.size() - 1U] == ':';
    }
    // Otherwise only the compilation unit's own `$unit` classes are
    // visible; a package class needs an import or `pkg::` (IEEE 1800-2017
    // 26.3, 3.12.1).
    return declaration.name == normalized
        && declaration.compilation_unit_identity
            == scope.compilation_unit_identity
        && identity.starts_with(scope.library + "::$unit");
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
    const std::string_view name,
    const Type* receiver) const {
  std::set<std::string> visited;
  auto current = find_class(identity);
  Type self;
  if (receiver == nullptr) {
    self.systemverilog_class_declaration = std::string{identity};
    receiver = &self;
  }
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
    current = find_base_class(*current, *receiver);
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
    const SystemVerilogClassMethodKind kind,
    const Type* receiver) const {
  std::set<std::string> visited;
  std::vector<MethodMatch> matches;
  auto current = find_class(identity);
  Type self;
  if (receiver == nullptr) {
    self.systemverilog_class_declaration = std::string{identity};
    receiver = &self;
  }
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
    current = find_base_class(*current, *receiver);
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
      // `super` is the base specialization named in `extends B #(...)`
      // (IEEE 1800-2017 8.15, 8.25).
      for (const auto& actual : scope.class_owner->base->parameter_actuals) {
        SystemVerilogClassTypeActual converted;
        if (!actual.name.empty()) converted.name = actual.name;
        converted.value = actual.value;
        if (actual.type_actual) {
          converted.type_actual = std::make_shared<Type>(*actual.type_actual);
        }
        converted.span = actual.span;
        type.systemverilog_class_parameter_actuals.push_back(
            std::move(converted));
      }
      expression.nominal_type = type.systemverilog_class_declaration;
      return type;
    }
  }
  if (const auto* found = scope.objects.find(expression.text);
      found != nullptr) {
    const auto type = resolve_alias_type(
        *found, scope.lexical_identity);
    if (const auto identity = class_identity(type)) {
      expression.nominal_type = *identity;
    }
    expression.systemverilog_scalar_kind =
        type.systemverilog_scalar;
    return type;
  }
  // A bare container property inherited from a base class (`s = new[3];`
  // in a derived method) is the object's own member (IEEE 1800-2017 8.13).
  if (scope.class_owner != nullptr && scope.method_body
      && expression.text.find('.') == std::string::npos
      && expression.text.find("::") == std::string::npos) {
    const auto inherited = find_property(
        scope.class_owner->canonical_identity, expression.text);
    if (inherited.property != nullptr && inherited.owner != nullptr
        && inherited.owner != scope.class_owner
        && !inherited.property->is_static
        && inherited.property->declaration.type.systemverilog_container) {
      expression.text = "this." + expression.text;
      return resolve_identifier(expression, scope);
    }
  }

  // `v.rand_mode` and `c.constraint_mode` without parentheses query the
  // mode (IEEE 1800-2017 13.5.5, 18.8, 18.9).
  if (const auto mode_dot = expression.text.rfind('.');
      mode_dot != std::string::npos && mode_dot != 0U
      && (expression.text.ends_with(".rand_mode")
          || expression.text.ends_with(".constraint_mode"))) {
    Expression call{ExpressionKind::Call,
        expression.text.substr(mode_dot), {}, expression.span};
    call.operands.push_back(Expression{ExpressionKind::Identifier,
        expression.text.substr(0, mode_dot), {}, expression.span});
    expression = std::move(call);
    return resolve_call(expression, scope, std::nullopt,
        SystemVerilogScalarKind::None);
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
    auto receiver_type = resolve_expression(receiver, scope);
    // A class handle declared in a package (`P::c.x`, 26.3).
    if (!receiver_type) {
      const auto separator = receiver.text.find("::");
      if (separator != std::string::npos
          && receiver.text.find("::", separator + 2U) == std::string::npos
          && receiver.text.find('.') == std::string::npos) {
        const auto package_name = receiver.text.substr(0, separator);
        const auto object_name = receiver.text.substr(separator + 2U);
        for (const auto& candidate : design_.units) {
          if (candidate.kind != UnitKind::SystemVerilogPackage
              || candidate.name != package_name) {
            continue;
          }
          const auto find_type = [&](const auto& objects) -> const Type* {
            const auto found = std::ranges::find_if(objects,
                [&](const auto& object) { return object.name == object_name; });
            return found == objects.end() ? nullptr : &found->type;
          };
          const auto* object_type = find_type(candidate.variables);
          if (object_type == nullptr) {
            object_type = find_type(candidate.signals);
          }
          if (object_type != nullptr && !object_type->named_type.empty()) {
            Scope package_scope = scope;
            package_scope.lexical_identity
                = effective_library(candidate.library) + "::" + candidate.name;
            const auto declarations = resolve_class_name(
                object_type->named_type, package_scope);
            if (declarations.size() == 1U) {
              receiver_type = class_type(*declarations.front());
            }
          }
        }
      }
    }
    // `vif.f` names a call of the interface's function `f` without
    // arguments (IEEE 1800-2017 13.5.5, 25.9).
    if (receiver_type && receiver_type->systemverilog_virtual_interface) {
      const auto member = expression.text.substr(dot + 1U);
      auto interface_name = receiver_type->named_type.empty()
          ? receiver_type->spelling : receiver_type->named_type;
      interface_name = interface_name.substr(0, interface_name.find('.'));
      const auto function = std::ranges::any_of(design_.units,
          [&](const DesignUnit& unit) {
            return unit.kind == UnitKind::SystemVerilogInterface
                && unit.name == interface_name
                && std::ranges::any_of(unit.functions,
                       [&](const FunctionDeclaration& candidate) {
                         return candidate.name == member;
                       });
          });
      if (function) {
        Expression call{ExpressionKind::Call, "." + member, {},
            expression.span};
        call.operands.push_back(Expression{ExpressionKind::Identifier,
            expression.text.substr(0, dot), {}, expression.span});
        expression = std::move(call);
        return resolve_expression(expression, scope);
      }
    }
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
    const auto match = find_property(*class_identity(*receiver_type), member, &*receiver_type);
    if (match.property
        && !check_member_visibility(match, scope, member, expression.span)) {
      return std::nullopt;
    }
    // A local parameter of the object's class with a literal value,
    // `obj.X`, is that constant (IEEE 1800-2017 8.25, 6.20.4).
    if (match.property && match.property->is_parameter
        && match.property->declaration.initializer
        && match.property->declaration.type.enumeration_literals.empty()
        && (match.property->declaration.initializer->kind
                == ExpressionKind::IntegerLiteral
            || match.property->declaration.initializer->kind
                == ExpressionKind::LogicLiteral)) {
      const auto span = expression.span;
      expression = *match.property->declaration.initializer;
      expression.span = span;
      return match.property->declaration.type;
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
                 SystemVerilogClassMethodKind::Function, &*receiver_type)
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
          expression.span, &*receiver_type);
      if (method && method->method) {
        expression.kind = ExpressionKind::Call;
        if (method->method->is_static) {
          // A static method called through a handle (8.10), in the
          // handle's class specialization (8.25).
          expression.text = "@sv-static-method:"
              + method->method->canonical_identity;
          if (!receiver_type->systemverilog_class_parameter_actuals.empty()) {
            expression.systemverilog_class_owner =
                std::make_shared<const Type>(*receiver_type);
          }
          expression.operands.clear();
          retain_call_profile(expression, *method->method, false);
        } else {
          expression.text = "@sv-method:"
              + method->method->canonical_identity;
          expression.operands = {std::move(receiver)};
          retain_call_profile(expression, *method->method, true);
        }
        auto type = resolve_alias_type(
            method->method->return_type,
            method->owner->canonical_identity);
        if (const auto& named = method->method->return_type.named_type;
            !named.empty() && class_identity(*receiver_type)
            && !method->method->return_type.systemverilog_container) {
          if (auto actual = class_type_parameter_actual(
                  named, *method->owner, *receiver_type, scope)) {
            type = std::move(*actual);
          }
        }
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
    bool type_actual_found = false;
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
            type_actual_found = true;
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
              type_actual_found = true;
            }
          }
          break;
        }
      }
    }
    // A type parameter of a base class takes the actual of the derived
    // class's `extends Base #(T)` clause, or its default (IEEE 1800-2017
    // 8.25, 8.13).
    if (const auto& named = match.property->declaration.type.named_type;
        !type_actual_found && !named.empty()
        && !match.property->declaration.type.systemverilog_container) {
      if (auto actual = class_type_parameter_actual(
              named, *match.owner, *receiver_type, scope)) {
        type = std::move(*actual);
      }
    }
    if (match.property->is_static) {
      // A static property reached through an object handle denotes the
      // class's single storage (IEEE 1800-2017 8.9), the one of the
      // handle's specialization (8.25).
      expression.text = (type.systemverilog_container
              ? "@sv-static-container-property:"
              : "@sv-static-property:")
          + match.owner->canonical_identity + "::" + member;
      if (!receiver_type->systemverilog_class_parameter_actuals.empty()) {
        expression.systemverilog_class_owner =
            std::make_shared<const Type>(*receiver_type);
      }
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
    // A typedef of a specialization, `typedef C#(8) C8; C8::v`, names the
    // same parameter values.
    auto specialization_spelling = owner_name;
    if (owners.size() == 1U && owner_name.find('#') == std::string::npos
        && owner_name.find("::") == std::string::npos) {
      Type named;
      named.named_type = owner_name;
      const auto alias = resolve_alias_type(named, scope.lexical_identity);
      if (const auto spelling = class_specialization_spelling(
              owners.front()->name, alias)) {
        specialization_spelling = *spelling;
      }
    } else if (owners.size() == 1U
        && owner_name.find('#') == std::string::npos) {
      // A package typedef of a specialization, `P::C8::v`.
      const auto package_separator = owner_name.find("::");
      const auto package = owner_name.substr(0U, package_separator);
      const auto alias_name = owner_name.substr(package_separator + 2U);
      const Type* package_alias = nullptr;
      if (alias_name.find("::") == std::string::npos) {
        for (const auto& unit : design_.units) {
          if (unit.name != package) continue;
          const auto alias = class_aliases_.find(effective_library(unit.library)
              + "::" + unit.name + "::" + alias_name);
          if (alias != class_aliases_.end()) {
            package_alias = alias->second;
            break;
          }
        }
      }
      if (package_alias != nullptr) {
        if (const auto spelling = class_specialization_spelling(
                owners.front()->name, *package_alias)) {
          specialization_spelling = *spelling;
        }
      }
    }
    if (owners.size() == 1U
        && specialization_spelling.find('#') != std::string::npos) {
      if (auto value = class_specialization_parameter_value(
              *owners.front(), specialization_spelling, member)) {
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
        // The selected specialization owns the storage (IEEE 1800-2017
        // 8.25.1).
        if (const auto selection = resolve_class_selection(owner_name, scope,
                expression.systemverilog_class_selection.get());
            selection
            && !selection->type.systemverilog_class_parameter_actuals.empty()) {
          expression.systemverilog_class_owner =
              std::make_shared<const Type>(selection->type);
        }
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
      auto type = resolve_alias_type(
          match.property->declaration.type,
          match.owner->canonical_identity);
      // A property inherited from a base and typed by the base's type
      // parameter has the type the extends clause binds (IEEE 1800-2017
      // 8.13, 8.25).
      if (match.owner != scope.class_owner && !class_identity(type)
          && !type.named_type.empty()
          && std::ranges::any_of(match.owner->parameters,
              [&](const ParameterDeclaration& parameter) {
                return parameter.kind == ParameterKind::Type
                    && parameter.name == type.named_type;
              })) {
        auto bound = resolve_alias_type(match.property->declaration.type,
            scope.class_owner->canonical_identity);
        if (class_identity(bound) || bound.named_type != type.named_type) {
          type = std::move(bound);
        }
      }
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

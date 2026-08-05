// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution.hpp"

#include <algorithm>
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
    for (auto& declaration : design_.systemverilog_classes) {
      collect(declaration);
    }
    for (auto& unit : design_.units) {
      for (auto& declaration : unit.systemverilog_classes) {
        collect(declaration);
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
  }

 private:
  void collect(SystemVerilogClassDeclaration& declaration) {
    classes_[declaration.canonical_identity] = &declaration;
    for (auto& nested : declaration.nested_classes) collect(nested);
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
    const auto found = classes_.find(identity);
    return found == classes_.end() ? nullptr : found->second;
  }

  [[nodiscard]] std::vector<const SystemVerilogClassDeclaration*>
  resolve_class_name(
      const std::string_view spelling, const Scope& scope) const {
    std::vector<const SystemVerilogClassDeclaration*> matches;
    const auto append = [&](const auto& predicate) {
      for (const auto& [identity, declaration] : classes_) {
        if (predicate(identity, *declaration)
            && std::ranges::find(matches, declaration) == matches.end()) {
          matches.push_back(declaration);
        }
      }
    };
    if (const auto exact = find_class(spelling)) return {exact};
    std::string lexical = scope.lexical_identity;
    for (;;) {
      if (const auto direct = find_class(
              lexical + "::" + std::string{spelling})) {
        return {direct};
      }
      const auto separator = lexical.rfind("::");
      if (separator == std::string::npos) break;
      lexical.resize(separator);
    }
    append([&](const std::string& identity, const auto& declaration) {
      if (declaration.library != scope.library) return false;
      if (spelling.find("::") != std::string_view::npos) {
        return identity.ends_with(spelling)
            && identity.size() > spelling.size()
            && identity[identity.size() - spelling.size() - 1U] == ':';
      }
      return declaration.name == spelling
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
      current = current->base
          ? find_class(current->base->declaration_identity)
          : nullptr;
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
      current = current->base
          ? find_class(current->base->declaration_identity)
          : nullptr;
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
      current = current->base
          ? find_class(current->base->declaration_identity)
          : nullptr;
    }
    return false;
  }

  static void retain_result_type(
      Expression& expression, const Type& type) {
    expression.call_result_width = type.width().value_or(0);
    expression.call_result_domain = type.domain;
    expression.call_result_signed = type.is_signed;
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
      current = current->base
          ? find_class(current->base->declaration_identity)
          : nullptr;
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
      if (const auto base = find_class(
              scope.class_owner->base->declaration_identity)) {
        auto type = class_type(*base);
        expression.nominal_type = type.systemverilog_class_declaration;
        return type;
      }
    }
    if (const auto found = scope.objects.find(expression.text);
        found != scope.objects.end()) {
      if (const auto identity = class_identity(found->second)) {
        expression.nominal_type = *identity;
      }
      expression.systemverilog_scalar_kind =
          found->second.systemverilog_scalar;
      return found->second;
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
        diagnose(
            diagnostics_,
            "FSIM-SV-CLASS-012",
            "class '" + *class_identity(*receiver_type)
                + "' has no visible property '" + member + "'",
            expression.span);
        return std::nullopt;
      }
      expression.kind = ExpressionKind::Call;
      expression.text = (match.property->declaration.type.systemverilog_container
              ? "@sv-container-property:"
              : "@sv-property:")
          + match.owner->canonical_identity + "::" + member;
      expression.operands = {std::move(receiver)};
      retain_result_type(expression, match.property->declaration.type);
      if (const auto identity = class_identity(match.property->declaration.type)) {
        expression.nominal_type = *identity;
      }
      return match.property->declaration.type;
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
          expression.kind = ExpressionKind::Call;
          expression.text =
              (match.property->declaration.type.systemverilog_container
                   ? "@sv-static-container-property:"
                   : "@sv-static-property:")
              + match.owner->canonical_identity + "::" + member;
          retain_result_type(expression, match.property->declaration.type);
          if (const auto identity = class_identity(
                  match.property->declaration.type)) {
            expression.nominal_type = *identity;
          }
          return match.property->declaration.type;
        }
      }
    }

    if (scope.class_owner != nullptr) {
      const auto match = find_property(
          scope.class_owner->canonical_identity, expression.text);
      if (match.property) {
        Expression receiver{
            ExpressionKind::Identifier, "this", {}, expression.span};
        receiver.nominal_type = scope.class_owner->canonical_identity;
        expression.kind = ExpressionKind::Call;
        expression.text = (match.property->is_static
            ? (match.property->declaration.type.systemverilog_container
                   ? "@sv-static-container-property:"
                   : "@sv-static-property:")
            : (match.property->declaration.type.systemverilog_container
                   ? "@sv-container-property:"
                   : "@sv-property:"))
            + match.owner->canonical_identity + "::"
            + match.property->declaration.name;
        if (!match.property->is_static) {
          expression.operands = {std::move(receiver)};
        }
        retain_result_type(expression, match.property->declaration.type);
        if (const auto identity = class_identity(match.property->declaration.type)) {
          expression.nominal_type = *identity;
        }
        return match.property->declaration.type;
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
      const SystemVerilogClassMethod& method) {
    statement.class_method_arguments = method.arguments;
    statement.declarations = method.variables;
    statement.statements = method.statements;
  }

  [[nodiscard]] std::optional<Type> resolve_call(
      Expression& expression,
      const Scope& scope,
      const std::optional<std::string>& expected_class,
      const SystemVerilogScalarKind expected_scalar) {
    if (expression.text == "@sv-null") {
      if (expected_scalar == SystemVerilogScalarKind::Chandle) {
        expression.systemverilog_scalar_kind =
            SystemVerilogScalarKind::Chandle;
        return chandle_type();
      }
      expression.nominal_type = expected_class.value_or(std::string{});
      if (expected_class) {
        if (const auto declaration = find_class(*expected_class)) {
          return class_type(*declaration);
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
    if (expression.text == "@sv-new") {
      if (!expected_class || find_class(*expected_class) == nullptr) {
        diagnose(
            diagnostics_,
            "FSIM-SV-CLASS-014",
            "class construction requires a destination class-handle type",
            expression.span);
        return std::nullopt;
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
      for (auto& operand : expression.operands) {
        resolve_expression(operand, scope);
      }
      if (expression.operands.size() != 2U
          || expression.operands.front().nominal_type.empty()) {
        diagnose(
            diagnostics_,
            "FSIM-SV-CLASS-015",
            "$cast requires a writable class-handle destination and source",
            expression.span);
        return std::nullopt;
      }
      expression.text += ":" + expression.operands.front().nominal_type;
      return Type{ValueDomain::Logic4, "bit", PackedRange{0, 0, true}, false};
    }
    if (expression.text.starts_with("@sv-")) {
      for (auto& operand : expression.operands) {
        resolve_expression(operand, scope);
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
      if (receiver_type && receiver_type->systemverilog_container
          && expression.operands.front().kind == ExpressionKind::Call
          && expression.operands.front().text.starts_with(
              container_property_prefix)
          && expression.operands.front().operands.size() == 1U) {
        const auto method = expression.text.substr(1U);
        if (method == "push_back" || method == "pop_front"
            || method == "size") {
          const auto property = expression.operands.front().text.substr(
              container_property_prefix.size());
          auto receiver = std::move(
              expression.operands.front().operands.front());
          std::vector<Expression> operands;
          operands.push_back(std::move(receiver));
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
          if (method == "pop_front") {
            auto element = *receiver_type;
            element.systemverilog_container.reset();
            retain_result_type(expression, element);
            expression.nominal_type =
                element.systemverilog_class_declaration;
            return element;
          }
          if (method == "size") {
            Type result{
                ValueDomain::Integer,
                "int",
                PackedRange{31, 0, true},
                true};
            retain_result_type(expression, result);
            return result;
          }
          return std::nullopt;
        }
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
      const auto owners = resolve_class_name(
          expression.text.substr(0, selected), scope);
      if (owners.size() == 1U) {
        const auto name = expression.text.substr(selected + 2U);
        const auto match = select_method(
            owners.front()->canonical_identity,
            name,
            SystemVerilogClassMethodKind::Function,
            expression.operands.size(),
            expression.span);
        if (match && match->method && match->method->is_static) {
          expression.text = "@sv-static-method:"
              + match->method->canonical_identity;
          retain_call_profile(expression, *match->method, false);
          for (std::size_t index = 0;
               index < expression.operands.size(); ++index) {
            const auto formal = index < match->method->arguments.size()
                ? &match->method->arguments[index] : nullptr;
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
      if (!container || !container->systemverilog_container
          || container->systemverilog_class_declaration.empty()) {
        return std::nullopt;
      }
      constexpr std::string_view property_prefix{
          "@sv-container-property:"};
      if (expression.operands[0].kind != ExpressionKind::Call
          || !expression.operands[0].text.starts_with(property_prefix)
          || expression.operands[0].operands.size() != 1U) {
        return std::nullopt;
      }
      auto element = *container;
      element.systemverilog_container.reset();
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
    const auto resolved = resolve_expression(
        expression, scope, class_identity(expected),
        expected.systemverilog_scalar);
    const bool expected_chandle =
        expected.systemverilog_scalar == SystemVerilogScalarKind::Chandle;
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
        const auto kind = name == "new"
            ? SystemVerilogClassMethodKind::Constructor
            : SystemVerilogClassMethodKind::Task;
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
          if (kind == SystemVerilogClassMethodKind::Task && match->method) {
            retain_task_profile(statement, *match->method);
          }
          return;
        }
      }
    }
    const auto selected = statement.task_name.rfind("::");
    if (selected != std::string::npos) {
      const auto owners = resolve_class_name(
          statement.task_name.substr(0, selected), scope);
      if (owners.size() == 1U) {
        const auto name = statement.task_name.substr(selected + 2U);
        const auto matches = find_methods(
            owners.front()->canonical_identity,
            name,
            SystemVerilogClassMethodKind::Task);
        if (!matches.empty()) {
          const auto match = select_method(
              owners.front()->canonical_identity,
              name,
              SystemVerilogClassMethodKind::Task,
              statement.task_arguments.size(),
              statement.span);
          if (match && match->method && match->method->is_static) {
            statement.task_name = "@sv-static-task:"
                + match->method->canonical_identity;
            retain_task_profile(statement, *match->method);
            for (auto& argument : statement.task_arguments) {
              resolve_expression(argument, scope);
            }
            return;
          }
        }
      }
    }
    if (scope.class_owner != nullptr) {
      const auto matches = find_methods(
          scope.class_owner->canonical_identity,
          statement.task_name,
          SystemVerilogClassMethodKind::Task);
      if (!matches.empty()) {
        const auto match = select_method(
            scope.class_owner->canonical_identity,
            statement.task_name,
            SystemVerilogClassMethodKind::Task,
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
        : target_type ? target_type->systemverilog_scalar
                      : SystemVerilogScalarKind::None;
    const auto value_type = resolve_expression(
        statement.value, scope, expected_class, expected_scalar);
    const bool typed_assignment = statement.kind == StatementKind::Assignment
        || statement.kind == StatementKind::Force
        || statement.kind == StatementKind::Return;
    const bool expected_chandle =
        expected_scalar == SystemVerilogScalarKind::Chandle;
    if (typed_assignment && statement.value.valid()
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
    for (auto& variable : function.variables) {
      if (variable.initializer) {
        resolve_typed_expression(
            *variable.initializer, scope, variable.type);
      }
    }
    const auto return_class = class_identity(function.return_type);
    const auto return_scalar = function.return_type.systemverilog_scalar;
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
      for (auto& variable : method.variables) {
        if (variable.initializer) {
          resolve_typed_expression(
              *variable.initializer, method_scope, variable.type);
        }
      }
      const auto return_class = class_identity(method.return_type);
      const auto return_scalar = method.return_type.systemverilog_scalar;
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

// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution_internal.hpp"
#include "fsim/support/uvm_identity.hpp"
#include <array>

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::frontend::class_resolution_detail {

[[nodiscard]] std::optional<MethodMatch> Resolver::select_method(
    const std::string_view identity,
    const std::string_view name,
    const SystemVerilogClassMethodKind kind,
    const std::size_t actual_count,
    const SourceSpan& span,
    const Type* receiver) {
  auto matches = find_methods(identity, name, kind, receiver);
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
void Resolver::retain_call_profile(
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
void Resolver::retain_task_profile(
    Statement& statement,
    const SystemVerilogClassMethod& method,
    const SystemVerilogClassDeclaration* owner,
    const Type* receiver_type) {
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
          { },
          { },
          { },
          false });
    }
  }
  statement.declarations.clear();
  statement.statements.clear();
}
[[nodiscard]] std::optional<Type> Resolver::resolve_call(
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
  if (expression.text == "@sv-size-cast:expression"
      && expression.operands.size() == 2U) {
    resolve_expression(expression.operands.front(), scope);
    const auto operand = resolve_expression(
        expression.operands.back(), scope);
    if (chandle(operand)) {
      diagnose(
          diagnostics_, "FSIM-SV-SEM-174",
          "chandle cannot be cast to a numeric or aggregate type",
          expression.span);
    }
    return std::nullopt;
  }
  if (expression.text.starts_with("@sv-cast:")
      && expression.operands.size() == 1U) {
    // A static cast of a class handle to a class it may be assigned to is
    // that handle (IEEE 1800-2017 6.24.1, 8.15).
    const auto classes = resolve_class_name(
        std::string_view{expression.text}.substr(
            std::string_view{"@sv-cast:"}.size()),
        scope);
    if (classes.size() == 1U) {
      const auto target = class_type(*classes.front());
      auto operand_type = resolve_expression(
          expression.operands.front(), scope);
      const auto operand_class = operand_type
          ? class_identity(*operand_type) : std::nullopt;
      const auto target_class = class_identity(target);
      const bool null_operand
          = expression.operands.front().text == "@sv-null";
      if (target_class
          && (null_operand
              || (operand_class
                  && class_assignable(*operand_class, *target_class)))) {
        auto operand = std::move(expression.operands.front());
        expression = std::move(operand);
        if (null_operand) {
          expression.nominal_type = *target_class;
          return target;
        }
        return operand_type;
      }
    }
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
  if (expression.text == "process::self"
      || expression.text == "std::process::self") {
    for (auto& operand : expression.operands) {
      resolve_expression(operand, scope);
    }
    expression.systemverilog_scalar_kind =
        SystemVerilogScalarKind::Chandle;
    return chandle_type();
  }
  if (expression.text == "@sv-shallow-copy"
      && expression.operands.size() == 1U) {
    auto type = resolve_expression(expression.operands.front(), scope);
    if (!type || !class_identity(*type)) {
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-027",
          "a shallow copy requires a class handle operand",
          expression.span);
      return std::nullopt;
    }
    expression.nominal_type = *class_identity(*type);
    return type;
  }
  constexpr std::string_view typed_new{"@sv-new-typed:"};
  if (expression.text.starts_with(typed_new)) {
    const auto name = expression.text.substr(typed_new.size());
    const auto owners = resolve_class_name(name, scope);
    if (owners.size() != 1U) {
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-026",
          "typed constructor '" + name + "::new' does not name a class",
          expression.span);
      return std::nullopt;
    }
    const auto& constructed = owners.front()->canonical_identity;
    // The constructed class must be assignable to the destination
    // (IEEE 1800-2017 8.8).
    if (expected_class && *expected_class != constructed
        && !class_assignable(constructed, *expected_class)) {
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-025",
          "a handle of class '" + constructed
              + "' cannot be assigned to a variable of class '"
              + *expected_class + "'",
          expression.span);
      return std::nullopt;
    }
    expression.text = "@sv-new";
    return resolve_expression(
        expression, scope, constructed,
        SystemVerilogScalarKind::None);
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
    // An abstract class cannot be constructed (IEEE 1800-2017 8.21).
    if (expected_declaration->is_virtual || expected_declaration->is_interface) {
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-024",
          "class '" + *expected_class + "' is virtual and cannot be "
              "constructed with new",
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
    // A member of a packed structure class property stays a selection.
    if (!identity && receiver_type
        && is_class_property_access(expression.operands.front())) {
      return packed_member_type(*receiver_type, member);
    }
    // A member of a value element of an object's array (`obj.v[i].m`) is
    // a member path on the indexed property (`index.m`).
    constexpr std::string_view element_prefix{"@sv-container-index:"};
    if (!identity
        && expression.operands.front().kind == ExpressionKind::Call
        && expression.operands.front().text.starts_with(element_prefix)
        && expression.operands.front().operands.size() == 2U) {
      auto element = std::move(expression.operands.front());
      Expression property{ExpressionKind::Call,
          "@sv-container-property:"
              + element.text.substr(element_prefix.size()),
          {std::move(element.operands[0])}, element.span};
      Expression selected{ExpressionKind::Index,
          "index." + std::string{member},
          {std::move(property), std::move(element.operands[1])},
          expression.span};
      selected.systemverilog_source_order
          = expression.systemverilog_source_order;
      expression = std::move(selected);
      return resolve_expression(
          expression, scope, expected_class, expected_scalar);
    }
    if (!identity) {
      auto receiver = std::move(expression.operands.front());
      receiver.text += "." + std::string{member};
      receiver.span = expression.span;
      expression = std::move(receiver);
      return receiver_type;
    }
    const auto match = find_property(*identity, member);
    if (match.property
        && !check_member_visibility(match, scope, member, expression.span)) {
      return std::nullopt;
    }
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
        if (method->method->is_static) {
          expression.text = "@sv-static-method:"
              + method->method->canonical_identity;
          expression.operands.clear();
          expression.call_argument_names.clear();
          retain_call_profile(expression, *method->method, false);
        } else {
          expression.text = "@sv-method:"
              + method->method->canonical_identity;
          retain_call_profile(expression, *method->method, true);
        }
        if (const auto result_identity = class_identity(
                method->method->return_type)) {
          expression.nominal_type = *result_identity;
        }
        return method->method->return_type;
      }
      // `p.randomize` without parentheses calls the built-in method
      // (IEEE 1800-2017 13.5.5, 18.6.1).
      if (member == "randomize") {
        expression.text = ".randomize";
        return resolve_call(expression, scope, expected_class, expected_scalar);
      }
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-012",
          "class '" + *identity + "' has no visible property '"
              + std::string{member} + "'",
          expression.span);
      return std::nullopt;
    }
    // A static property selected through any object expression is the
    // class's one variable (IEEE 1800-2017 8.9).
    if (match.property->is_static) {
      const auto type = resolve_alias_type(
          match.property->declaration.type,
          match.owner->canonical_identity);
      expression.kind = ExpressionKind::Call;
      expression.text =
          (type.systemverilog_container
               ? "@sv-static-container-property:"
               : "@sv-static-property:")
          + match.owner->canonical_identity + "::" + std::string{member};
      expression.operands.clear();
      retain_result_type(expression, type);
      if (const auto result_identity = class_identity(type)) {
        expression.nominal_type = *result_identity;
      }
      return type;
    }
    expression.text =
        (match.property->declaration.type.systemverilog_container
             ? "@sv-container-property:"
             : "@sv-property:")
        + match.owner->canonical_identity + "::" + std::string{member};
    auto property_type = match.property->declaration.type;
    // A container of class handles selected through another object
    // (`m[i].arr[j] = new`) names its element class in the owner's scope.
    if (property_type.systemverilog_container
        && !class_identity(property_type)) {
      auto& elements = property_type.systemverilog_container->element_types;
      const auto named = elements.empty() ? property_type.named_type
                                          : elements.front().named_type;
      if (!named.empty()
          && (elements.empty() || !class_identity(elements.front()))) {
        Scope owner_scope = scope;
        owner_scope.lexical_identity = match.owner->canonical_identity;
        const auto declarations = resolve_class_name(named, owner_scope);
        if (declarations.size() == 1U) {
          const auto element_class = class_type(*declarations.front());
          property_type.systemverilog_class_declaration
              = element_class.systemverilog_class_declaration;
          property_type.systemverilog_class_name
              = element_class.systemverilog_class_name;
          if (!elements.empty() && !elements.front().systemverilog_container) {
            elements.front().systemverilog_class_declaration
                = element_class.systemverilog_class_declaration;
            elements.front().systemverilog_class_name
                = element_class.systemverilog_class_name;
          }
        }
      }
    }
    retain_result_type(expression, property_type);
    if (const auto property_identity = class_identity(property_type)) {
      expression.nominal_type = *property_identity;
    }
    return property_type;
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
    const bool implicit_member = built_in_mode
        && expression.operands.front().kind == ExpressionKind::Identifier
        && this_mode_member(
            expression.operands.front().text + expression.text, scope);
    if (implicit_member) {
      expression.operands.front().text =
          "this." + expression.operands.front().text;
    }
    // `obj.rand_mode(b)` and `obj.constraint_mode(b)` set the mode of every
    // random variable or constraint of the object (IEEE 1800-2017 18.8,
    // 18.9).
    // A dotted receiver is an object only when its last member is a class
    // handle property; otherwise it names a property or constraint below.
    const auto object_receiver = [&](const Expression& receiver_name) {
      const auto& text = receiver_name.text;
      const auto member_dot = text.rfind('.');
      if (member_dot == std::string::npos) return true;
      Expression prefix{ExpressionKind::Identifier,
          text.substr(0, member_dot), {}, receiver_name.span};
      const auto prefix_type = resolve_expression(prefix, scope);
      const auto prefix_identity = prefix_type
          ? class_identity(*prefix_type) : std::nullopt;
      if (!prefix_identity) return false;
      const auto property = find_property(
          *prefix_identity, text.substr(member_dot + 1U));
      return property.property
          && class_identity(property.property->declaration.type)
          && !property.property->declaration.type.systemverilog_container;
    };
    if (built_in_mode && expression.operands.size() == 2U
        && !implicit_member
        && expression.operands.front().kind == ExpressionKind::Identifier
        && object_receiver(expression.operands.front())) {
      auto receiver = expression.operands.front();
      const auto receiver_type = resolve_expression(receiver, scope);
      if (receiver_type && class_identity(*receiver_type)
          && !receiver_type->systemverilog_container) {
        expression.operands.front() = std::move(receiver);
        expression.text = "@sv-method:@builtin-"
            + std::string{expression.text == ".rand_mode"
                  ? "rand-mode:" : "constraint-mode:"}
            + "@object";
        expression.call_argument_names.resize(expression.operands.size());
        expression.call_argument_directions.assign(
            expression.operands.size(), PortDirection::Input);
        resolve_expression(expression.operands.back(), scope);
        Type result{
            ValueDomain::Integer, "int", PackedRange{31, 0, true}, true};
        retain_result_type(expression, result);
        return result;
      }
    }
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
    auto& receiver_expression = expression.operands.front();
    auto receiver_type = resolve_expression(receiver_expression, scope);
    // A function-returned class handle is a valid method-call receiver. The
    // ordinary class resolver does not otherwise need to resolve compilation-
    // unit functions, so recover the narrow no-argument design-unit profile
    // here before class method selection. Keep the call expression intact: HIR
    // lowering must evaluate it once as the receiver.
    if (!receiver_type && scope.class_owner == nullptr
        && receiver_expression.kind == ExpressionKind::Call
        && receiver_expression.operands.empty()
        && receiver_expression.text.find("::") == std::string::npos) {
      const DesignUnit* owner{};
      bool ambiguous_owner{};
      for (const auto& unit : design_.units) {
        if (unit.language != Language::SystemVerilog2017
            || effective_library(unit.library) + "::" + unit.name
                != scope.lexical_identity) {
          continue;
        }
        if (owner != nullptr) {
          ambiguous_owner = true;
          break;
        }
        owner = &unit;
      }
      if (owner != nullptr && !ambiguous_owner) {
        const FunctionDeclaration* callable{};
        bool ambiguous_callable{};
        for (const auto& function : owner->functions) {
          if (function.name != receiver_expression.text || !function.defined
              || !function.arguments.empty()) {
            continue;
          }
          if (callable != nullptr) {
            ambiguous_callable = true;
            break;
          }
          callable = &function;
        }
        if (callable != nullptr && !ambiguous_callable) {
          auto result = resolve_alias_type(
              callable->return_type, scope.lexical_identity);
          if (const auto identity = class_identity(result);
              identity && !result.systemverilog_container
              && find_class(*identity) != nullptr) {
            retain_result_type(receiver_expression, result);
            receiver_expression.nominal_type = *identity;
            receiver_type = std::move(result);
          }
        }
      }
    }
    constexpr std::string_view container_property_prefix{
        "@sv-container-property:"};
    if (receiver_type && receiver_type->systemverilog_container) {
      const auto method = expression.text.substr(1U);
      const bool instance_property =
          expression.operands.front().kind == ExpressionKind::Call
          && expression.operands.front().text.starts_with(
              container_property_prefix)
          && expression.operands.front().operands.size() == 1U;
      // A static property's methods apply to the class's one container
      // object, which the container method lowering binds directly
      // (IEEE 1800-2017 8.9, 7.10).
      if (instance_property) {
        const auto property = expression.operands.front().text.substr(
            container_property_prefix.size());
        std::vector<Expression> operands;
        auto receiver = std::move(
            expression.operands.front().operands.front());
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
      } else {
        // A with-clause's iterator (`item` by default) hides a class
        // property of the same name (IEEE 1800-2017 7.12).
        auto with_scope = scope;
        const auto iterator_name = expression.operands.size() == 3U
                && expression.operands[1].kind == ExpressionKind::Identifier
            ? expression.operands[1].text
            : std::string{"item"};
        // A class-handle element always binds the iterator, so that
        // `x.m` and `x.f()` select through it (7.12.4).
        const bool class_element
            = !receiver_type->systemverilog_class_declaration.empty();
        if (expression.operands.size() >= 2U
            && (class_element
                || (scope.class_owner != nullptr
                    && find_property(scope.class_owner->canonical_identity,
                           iterator_name).property
                    && !scope.objects.contains(iterator_name)))) {
          auto element = *receiver_type;
          element.systemverilog_container.reset();
          with_scope.objects.assign(iterator_name, std::move(element));
        }
        for (auto& operand :
             expression.operands | std::views::drop(1)) {
          resolve_expression(operand, with_scope);
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
      const auto null_argument = [](const Expression& operand) {
        return operand.kind == ExpressionKind::Call
            && operand.text == "@sv-null";
      };
      const auto has_null = std::ranges::any_of(
          expression.operands | std::views::drop(1), null_argument);
      const auto checker_call = expression.operands.size() == 2U
          && null_argument(expression.operands[1]);
      if (has_null && !checker_call) {
        diagnose(
            diagnostics_,
            "FSIM-SV-CLASS-022",
            "the null randomize checker argument must be the only "
            "variable-list item",
            expression.span);
      }
      for (std::size_t index = 1; index < expression.operands.size();
           ++index) {
        if (null_argument(expression.operands[index])) {
          if (checker_call) {
            expression.call_argument_names[index] = "@randomize-null";
          }
          expression.operands[index] = Expression{
              ExpressionKind::IntegerLiteral,
              "0",
              {},
              expression.operands[index].span};
          continue;
        }
        const auto selected_name = expression.operands[index].text;
        const auto selected =
            expression.operands[index].kind == ExpressionKind::Identifier
                ? find_property(receiver_identity, selected_name)
                : PropertyMatch{};
        // Any property may be listed; it is random for this call (IEEE
        // 1800-2017 18.11).
        if (!selected.property || selected.property->is_static) {
          diagnose(
              diagnostics_,
              "FSIM-SV-CLASS-019",
              "object randomize variable list requires a non-static "
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
      // A name in the inline constraint that is not a member of the
      // randomized object, or is `local::`-qualified, denotes the calling
      // scope's object (IEEE 1800-2017 18.7, 18.7.1).
      // An array method's with-clause iterator (`item` by default) is
      // bound in its with expression (IEEE 1800-2017 7.12).
      std::vector<std::string> bound_iterators;
      const auto resolve_caller_names = [&](const auto& self,
                                            Expression& node) -> void {
        static const std::set<std::string_view> iterator_methods{
            ".find", ".find_index", ".find_first", ".find_first_index",
            ".find_last", ".find_last_index", ".min", ".max", ".unique",
            ".unique_index", ".sort", ".rsort", ".sum", ".product", ".and",
            ".or", ".xor", ".map"};
        if (node.kind != ExpressionKind::Identifier
            && iterator_methods.contains(node.text)
            && node.operands.size() >= 2U) {
          self(self, node.operands.front());
          bound_iterators.push_back(
              node.operands.size() == 3U
                      && node.operands[1].kind == ExpressionKind::Identifier
                  ? node.operands[1].text
                  : std::string{"item"});
          for (auto& operand : node.operands | std::views::drop(1)) {
            self(self, operand);
          }
          bound_iterators.pop_back();
          return;
        }
        if (node.kind == ExpressionKind::Identifier) {
          constexpr std::string_view local_prefix{"local::"};
          // The first name of a dotted path (`item.addr`) selects the
          // scope that holds it.
          const auto head = [](const std::string_view text) {
            return std::string{text.substr(0, text.find('.'))};
          };
          if (node.text.starts_with(local_prefix)) {
            // Outside a class the lowerer binds `local::` names itself.
            const auto name = node.text.substr(local_prefix.size());
            if (scope.class_owner != nullptr
                && !scope.objects.contains(head(name))
                && find_property(scope.class_owner->canonical_identity,
                       head(name)).property) {
              node.text = name;
              resolve_expression(node, scope);
            }
            return;
          }
          if (node.text.find("::") == std::string::npos
              && std::ranges::find(bound_iterators, head(node.text))
                  == bound_iterators.end()
              && !find_property(receiver_identity, head(node.text)).property
              && scope.class_owner != nullptr
              && !scope.objects.contains(head(node.text))
              && find_property(scope.class_owner->canonical_identity,
                     head(node.text)).property) {
            resolve_expression(node, scope);
          }
          return;
        }
        for (auto& operand : node.operands) self(self, operand);
      };
      for (auto& choices : expression.aggregate_choice_expressions) {
        for (auto& choice : choices) {
          resolve_caller_names(resolve_caller_names, choice);
        }
      }
      Type result{
          ValueDomain::Integer,
          "int",
          PackedRange{31, 0, true},
          true};
      retain_result_type(expression, result);
      return result;
    }
    // `obj.get_randstate()` returns the object's random number generator
    // state (IEEE 1800-2017 18.13.4).
    if (name == "get_randstate" && expression.operands.size() == 1U) {
      expression.text = "@sv-method:@builtin-get-randstate";
      expression.call_argument_names.resize(expression.operands.size());
      expression.call_argument_directions.assign(
          expression.operands.size(), PortDirection::Input);
      Type result{ValueDomain::String, "string", std::nullopt, false};
      retain_result_type(expression, result);
      return result;
    }
    const auto match = select_method(
        *class_identity(*receiver_type),
        name,
        SystemVerilogClassMethodKind::Function,
        expression.operands.size() - 1U,
        expression.span, &*receiver_type);
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
    // A static method called through a handle runs without the handle
    // (IEEE 1800-2017 8.10).
    if (match->method->is_static && expression.operands.front().text != "super") {
      expression.text = "@sv-static-method:" + match->method->canonical_identity;
      // The handle's class specialization selects the method's (8.25).
      if (!receiver_type->systemverilog_class_parameter_actuals.empty()) {
        expression.systemverilog_class_owner =
            std::make_shared<const Type>(*receiver_type);
      }
      expression.operands.erase(expression.operands.begin());
      if (!expression.call_argument_names.empty()) {
        expression.call_argument_names.erase(
            expression.call_argument_names.begin());
      }
      retain_call_profile(expression, *match->method, false);
    }
    // The result type as the receiver's specialization sees it
    // (`T get()` of `C#(D)` returns a D, IEEE 1800-2017 8.25).
    auto return_type = match->method->return_type;
    if (const auto* receiver_class = find_class(*class_identity(*receiver_type));
        receiver_class != nullptr && !receiver_class->parameters.empty()
        && match->method->canonical_identity.starts_with(
            receiver_class->canonical_identity + "::")) {
      ClassSelection selection{receiver_class, *receiver_type};
      normalize_selection_actuals(selection, scope);
      return_type = specialize_selected_method(
          *match->method, selection, scope).return_type;
    }
    if (const auto identity = class_identity(return_type)) {
      expression.nominal_type = *identity;
    }
    return return_type;
  }

  const auto selected = expression.text.rfind("::");
  if (selected != std::string::npos) {
    const auto owner = resolve_class_selection(
        expression.text.substr(0, selected), scope,
        expression.systemverilog_class_selection.get());
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
        expression.systemverilog_class_owner =
            std::make_shared<const Type>(owner->type);
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

  // Outside a class an unqualified randomize call is the scope randomize
  // function, `[std::] randomize` (IEEE 1800-2017 18.12).
  if (scope.class_owner == nullptr && expression.text == "randomize") {
    expression.text = "std::randomize";
  }
  // An unqualified randomize call in a class method randomizes the object
  // itself (IEEE 1800-2017 18.6.1, 18.7); an unqualified get_randstate
  // reads the object's own generator state (18.13.4).
  if (scope.class_owner != nullptr && scope.method_body
      && (expression.text == "randomize"
          || (expression.text == "get_randstate"
              && expression.operands.empty()
              && find_methods(scope.class_owner->canonical_identity,
                     "get_randstate", SystemVerilogClassMethodKind::Function)
                  .empty()))) {
    Expression receiver{
        ExpressionKind::Identifier, "this", {}, expression.span};
    receiver.nominal_type = scope.class_owner->canonical_identity;
    expression.operands.insert(
        expression.operands.begin(), std::move(receiver));
    if (!expression.call_argument_names.empty()) {
      expression.call_argument_names.insert(
          expression.call_argument_names.begin(), std::string{});
    }
    expression.text = "." + expression.text;
    return resolve_call(
        expression, scope, expected_class, expected_scalar);
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
      resolve_scope_inline_constraints(expression, scope);
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
  resolve_scope_inline_constraints(expression, scope);
  return std::nullopt;
}
// The with-clause of a scope call (`std::randomize(x) with {...}`) names
// the calling scope (IEEE 1800-2017 18.12).
void Resolver::resolve_scope_inline_constraints(
    Expression& expression, const Scope& scope) {
  for (std::size_t index = 0;
       index < expression.aggregate_choices.size()
       && index < expression.aggregate_choice_expressions.size();
       ++index) {
    if (expression.aggregate_choices[index] != "@sv-inline-constraint") {
      continue;
    }
    for (auto& choice : expression.aggregate_choice_expressions[index]) {
      resolve_expression(choice, scope);
    }
  }
}
std::optional<Type> Resolver::resolve_expression(
    Expression& expression,
    const Scope& scope,
    const std::optional<std::string>& expected_class,
    const SystemVerilogScalarKind expected_scalar) {
  if (!expression.valid()) return std::nullopt;
  if (expression.kind == ExpressionKind::Identifier) {
    return resolve_identifier(expression, scope);
  }
  if (expression.kind == ExpressionKind::Call) {
    return resolve_call(
        expression, scope, expected_class, expected_scalar);
  }
  // `a[i].m` parses as an index node whose text carries the member path
  // (`index.m`); a member of a class-handle element is a member selection
  // of that object (IEEE 1800-2017 8.22).
  if (expression.kind == ExpressionKind::Index
      && expression.operands.size() == 2U
      && expression.text.starts_with("index.")) {
    Expression element{ExpressionKind::Index, "index",
        {expression.operands[0], expression.operands[1]}, expression.span};
    const auto element_type = resolve_expression(element, scope);
    if (element_type && class_identity(*element_type)
        && !element_type->systemverilog_container) {
      Expression selected = std::move(element);
      std::string_view path{expression.text};
      path.remove_prefix(std::string_view{"index."}.size());
      while (!path.empty()) {
        const auto dot = path.find('.');
        const auto member = path.substr(0, dot);
        Expression select{ExpressionKind::Call,
            "@sv-select:" + std::string{member}, {}, expression.span};
        select.operands.push_back(std::move(selected));
        selected = std::move(select);
        path = dot == std::string_view::npos ? std::string_view{}
                                             : path.substr(dot + 1U);
      }
      selected.systemverilog_source_order
          = expression.systemverilog_source_order;
      expression = std::move(selected);
      return resolve_expression(
          expression, scope, expected_class, expected_scalar);
    }
    // A built-in array method without arguments may omit its parentheses
    // (IEEE 1800-2017 7.12, 13.4.1): `a[i].size` is `a[i].size()`.
    constexpr std::array<std::string_view, 18> argumentless_methods{
        "size", "num", "delete", "pop_front", "pop_back", "sum", "product",
        "and", "or", "xor", "min", "max", "unique", "unique_index",
        "reverse", "sort", "rsort", "shuffle"};
    const auto member = std::string_view{expression.text}.substr(
        std::string_view{"index."}.size());
    if (element_type && element_type->systemverilog_container
        && member.find('.') == std::string_view::npos
        && std::ranges::find(argumentless_methods, member)
            != argumentless_methods.end()) {
      Expression call{ExpressionKind::Call, "." + std::string{member},
          {std::move(element)}, expression.span};
      call.systemverilog_source_order = expression.systemverilog_source_order;
      expression = std::move(call);
      return resolve_expression(
          expression, scope, expected_class, expected_scalar);
    }
  }
  if (expression.kind == ExpressionKind::Index
      && expression.operands.size() == 2U) {
    auto container = resolve_expression(expression.operands[0], scope);
    resolve_expression(expression.operands[1], scope);
    // An object of a typedef'd array (`typedef C ca_t[2]; ca_t a;`) has
    // the typedef's dimensions.
    if (container && !container->systemverilog_container
        && container->systemverilog_class_declaration.empty()
        && !container->named_type.empty()) {
      if (const auto alias = container_alias(
              scope.lexical_identity, container->named_type)) {
        container = *alias;
      }
    }
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
    // An index selects one dimension of a multidimensional static array;
    // the element keeps the remaining dimensions.
    if (const auto& source = *container->systemverilog_container;
        source.kind == SystemVerilogContainerKind::StaticArray
        && source.static_range_expressions.size() > 1U) {
      element = *container;
      auto& remaining = *element.systemverilog_container;
      remaining.static_range_expressions.erase(
          remaining.static_range_expressions.begin());
    }
    constexpr std::string_view indexed_property_prefix {
        "@sv-container-index:" };
    auto& base_expression = expression.operands.front();
    if (base_expression.kind == ExpressionKind::Call
        && base_expression.text.starts_with(indexed_property_prefix)
        && base_expression.operands.size() >= 2U
        && container->systemverilog_container->kind
            == SystemVerilogContainerKind::StaticArray
        && !container->systemverilog_container->element_types.empty()
        && !container->systemverilog_container->element_types.front()
                .systemverilog_class_declaration.empty()) {
      auto text = std::move(base_expression.text);
      auto operands = std::move(base_expression.operands);
      operands.push_back(std::move(expression.operands[1]));
      expression.kind = ExpressionKind::Call;
      expression.text = std::move(text);
      expression.operands = std::move(operands);
      retain_result_type(expression, element);
      expression.nominal_type = element.systemverilog_class_declaration;
      return element;
    }
    constexpr std::string_view property_prefix{
        "@sv-container-property:"};
    // `obj.v[i].m` keeps its member path (`index.m`): the elaborator
    // walks it as a value path rooted at the property.
    if (expression.operands[0].kind != ExpressionKind::Call
        || !expression.operands[0].text.starts_with(property_prefix)
        || expression.operands[0].operands.size() != 1U
        || expression.text != "index") {
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
    // `p.status` and `p.status()` of a process handle are its state
    // enumeration, not the handle (IEEE 1800-2017 9.7).
    const auto process_status = [](const Expression& operand) {
      return (operand.kind == ExpressionKind::Identifier
                 || operand.kind == ExpressionKind::Call
                 || operand.kind == ExpressionKind::Index)
          && (operand.text.ends_with(".status")
              || operand.text.ends_with("::status"));
    };
    if (process_status(expression.operands[0])) {
      left = Type{
          ValueDomain::Integer, "int", PackedRange{31, 0, true}, true};
    }
    if (process_status(expression.operands[1])) {
      right = Type{
          ValueDomain::Integer, "int", PackedRange{31, 0, true}, true};
    }
    if (chandle(left) || chandle(right)) {
      const bool equality = expression.text == "=="
          || expression.text == "!=" || expression.text == "==="
          || expression.text == "!==";
      // An operand whose type is not resolved here (a function call) may
      // be a chandle too (IEEE 1800-2017 6.14).
      if ((left && !chandle(left)) || (right && !chandle(right))
          || !equality) {
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
  for (std::size_t index = 0;
       index < expression.aggregate_choice_expressions.size(); ++index) {
    // A pattern key naming a property that is not a parameter is no
    // constant index, so it names a structure member or a type (IEEE
    // 1800-2017 10.9.1, 10.9.2) and stays a name.
    const bool key = index < expression.aggregate_choices.size()
        && expression.aggregate_choices[index] == "@key";
    for (auto& choice : expression.aggregate_choice_expressions[index]) {
      if (key && choice.kind == ExpressionKind::Identifier
          && scope.class_owner != nullptr
          && !scope.objects.contains(choice.text)) {
        const auto match = find_property(
            scope.class_owner->canonical_identity, choice.text);
        if (match.property != nullptr && !match.property->is_parameter) {
          continue;
        }
      }
      resolve_expression(choice, scope);
    }
  }
  return std::nullopt;
}
const Type* Resolver::container_alias(
    std::string lexical, const std::string& named_type) const {
  for (;;) {
    const auto alias = class_aliases_.find(lexical + "::" + named_type);
    if (alias != class_aliases_.end()) {
      return alias->second->systemverilog_container ? alias->second
                                                    : nullptr;
    }
    const auto parent = lexical.rfind("::");
    if (parent == std::string::npos) break;
    lexical.resize(parent);
  }
  // A compilation-unit typedef: the one alias of that name with array
  // dimensions.
  const Type* unique{};
  bool ambiguous{};
  const auto suffix = "::" + named_type;
  for (const auto& [key, type] : class_aliases_) {
    if (key.ends_with(suffix) && type->systemverilog_container) {
      ambiguous = ambiguous || unique != nullptr;
      unique = type;
    }
  }
  return ambiguous ? nullptr : unique;
}

std::optional<Type> Resolver::resolve_typed_expression(
    Expression& expression,
    const Scope& scope,
    const Type& expected) {
  if (expected.systemverilog_virtual_interface) {
    // Interface instances are hierarchy objects rather than ordinary
    // chandle-valued expressions. The hierarchy builder validates the
    // concrete interface type, specialization, modport, and selected
    // instance after child interfaces have been elaborated.
    // `vif.mp` selects a modport of the interface a handle names; the
    // handle is unchanged (IEEE 1800-2017 25.9).
    if (expression.kind == ExpressionKind::Identifier) {
      const auto dot = expression.text.rfind('.');
      auto interface_name = expected.named_type.empty()
          ? expected.spelling : expected.named_type;
      interface_name = interface_name.substr(0, interface_name.find('.'));
      if (dot != std::string::npos && dot != 0U) {
        const auto member = expression.text.substr(dot + 1U);
        const auto modport = std::ranges::any_of(design_.units,
            [&](const DesignUnit& unit) {
              return unit.kind == UnitKind::SystemVerilogInterface
                  && unit.name == interface_name
                  && std::ranges::any_of(unit.systemverilog_modports,
                         [&](const SystemVerilogModport& candidate) {
                           return candidate.name == member;
                         });
            });
        if (modport) {
          expression.text.resize(dot);
        }
      }
      // A handle held by a class object (`c.vif`) is read like any other
      // property (8.4).
      const auto root = std::string_view { expression.text }.substr(
          0, expression.text.find('.'));
      if (expression.text.find('.') != std::string::npos
          && scope.objects.contains(root)) {
        (void)resolve_expression(expression, scope);
      }
    }
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
    // A typedef of a class names that class (IEEE 1800-2017 6.18).
    expected_identity = class_identity(
        resolve_alias_type(expected, scope.lexical_identity));
  }
  if (!expected_identity && !expected.named_type.empty()) {
    expected_identity = expected.named_type;
  }
  const auto resolved = resolve_expression(
      expression, scope, expected_identity,
      expected.systemverilog_container
          ? SystemVerilogScalarKind::None
          : expected.systemverilog_scalar);
  const bool expected_chandle = chandle(expected);
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
bool Resolver::this_mode_member(
    const std::string_view call, const Scope& scope) const {
  const auto dot = call.find('.');
  if (scope.class_owner == nullptr || !scope.method_body
      || dot == std::string_view::npos || dot != call.rfind('.')) {
    return false;
  }
  const auto member = call.substr(0, dot);
  const auto method = call.substr(dot + 1U);
  if (scope.objects.contains(member)) return false;
  const auto& owner = scope.class_owner->canonical_identity;
  if (method == "constraint_mode") {
    return find_constraint(owner, member).constraint != nullptr;
  }
  if (method != "rand_mode") return false;
  const auto property = find_property(owner, member);
  // A random class handle member names the handle itself (18.8).
  return property.property
      && (property.property->is_rand || property.property->is_randc);
}

void Resolver::resolve_task_call(Statement& statement, const Scope& scope) {
  // In a method, `c.constraint_mode(0);` and `v.rand_mode(0);` select a
  // member of the object itself (IEEE 1800-2017 18.8, 18.9); the call
  // conversion below binds it.
  const bool implicit_member = this_mode_member(statement.task_name, scope);
  // An unqualified `rand_mode(b);` or `constraint_mode(b);` in a method
  // applies to the object itself.
  if (scope.class_owner != nullptr && scope.method_body
      && statement.task_arguments.size() == 1U
      && (statement.task_name == "rand_mode"
          || statement.task_name == "constraint_mode")) {
    statement.task_name = "this." + statement.task_name;
  }
  // An unqualified `srandom(seed);` or `set_randstate(state);` in a method
  // seeds the object's own generator (IEEE 1800-2017 18.13.3, 18.13.5).
  if (scope.class_owner != nullptr && scope.method_body
      && statement.task_arguments.size() == 1U
      && (statement.task_name == "srandom"
          || statement.task_name == "set_randstate")
      && find_methods(scope.class_owner->canonical_identity,
             statement.task_name, SystemVerilogClassMethodKind::Function)
          .empty()
      && find_methods(scope.class_owner->canonical_identity,
             statement.task_name, SystemVerilogClassMethodKind::Task)
          .empty()) {
    statement.task_name = "this." + statement.task_name;
  }
  const auto dot = statement.task_name.rfind('.');
  // `obj.c.constraint_mode(0);` or `obj.v.rand_mode(0);` selects a
  // constraint block or random property, not an object (18.8, 18.9).
  if (dot != std::string::npos
      && (statement.task_name.find('.') < dot || implicit_member)
      && (statement.task_name.ends_with(".constraint_mode")
          || statement.task_name.ends_with(".rand_mode"))) {
    Expression call{ExpressionKind::Call,
        statement.task_name.substr(dot), {}, statement.span};
    call.operands.push_back(Expression{ExpressionKind::Identifier,
        statement.task_name.substr(0, dot), {}, statement.span});
    for (auto& argument : statement.task_arguments) {
      call.operands.push_back(std::move(argument));
    }
    resolve_expression(call, scope);
    statement.kind = StatementKind::ContainerMethod;
    statement.value = std::move(call);
    statement.task_name.clear();
    statement.task_arguments.clear();
    statement.task_argument_names.clear();
    return;
  }
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
    if (receiver_type && !class_identity(*receiver_type)
        && statement.task_arguments.empty()) {
      // A built-in array method without arguments may omit its parentheses
      // (IEEE 1800-2017 7.10.2, 7.12, 13.4.1): `q.delete;`, `a.sort;`.
      static constexpr std::array argument_free_methods{
          std::string_view{"delete"}, std::string_view{"reverse"},
          std::string_view{"sort"}, std::string_view{"rsort"},
          std::string_view{"shuffle"}, std::string_view{"pop_front"},
          std::string_view{"pop_back"}};
      const auto name = std::string_view{statement.task_name}.substr(
          dot + 1U);
      if (std::ranges::find(argument_free_methods, name)
          != argument_free_methods.end()) {
        Expression call{ExpressionKind::Call, "." + std::string{name}, {},
            statement.span};
        call.operands.push_back(Expression{ExpressionKind::Identifier,
            statement.task_name.substr(0, dot), {}, statement.span});
        resolve_expression(call, scope);
        statement.kind = StatementKind::ContainerMethod;
        statement.value = std::move(call);
        statement.task_name.clear();
        statement.task_argument_names.clear();
        return;
      }
    }
    if (receiver_type && class_identity(*receiver_type)) {
      const auto name = statement.task_name.substr(dot + 1U);
      // `obj.set_randstate(state)` (IEEE 1800-2017 18.13.5).
      if (name == "set_randstate" && statement.task_arguments.size() == 1U) {
        statement.task_name = "@sv-object-set-randstate";
        statement.task_arguments.insert(
            statement.task_arguments.begin(), std::move(receiver));
        statement.task_argument_names.insert(
            statement.task_argument_names.begin(), std::string{});
        resolve_expression(statement.task_arguments.back(), scope);
        return;
      }
      if (name == "srandom" && statement.task_arguments.size() == 1U) {
        statement.task_name = "@sv-object-srandom";
        statement.task_arguments.insert(
            statement.task_arguments.begin(), std::move(receiver));
        statement.task_argument_names.insert(
            statement.task_argument_names.begin(), std::string{});
        resolve_expression(statement.task_arguments.back(), scope);
        return;
      }
      // The built-in pre_randomize and post_randomize do nothing unless a
      // class overrides them (IEEE 1800-2017 18.6.2).
      if ((name == "pre_randomize" || name == "post_randomize")
          && statement.task_arguments.empty()
          && find_methods(*class_identity(*receiver_type), name,
                 SystemVerilogClassMethodKind::Function, &*receiver_type)
                 .empty()
          && find_methods(*class_identity(*receiver_type), name,
                 SystemVerilogClassMethodKind::Task, &*receiver_type)
                 .empty()) {
        statement.kind = StatementKind::Null;
        statement.task_name.clear();
        statement.task_arguments.clear();
        statement.task_argument_names.clear();
        return;
      }
      auto kind = SystemVerilogClassMethodKind::Constructor;
      if (name != "new") {
        kind = find_methods(
                   *class_identity(*receiver_type),
                   name,
                   SystemVerilogClassMethodKind::Task, &*receiver_type)
                       .empty()
            ? SystemVerilogClassMethodKind::Function
            : SystemVerilogClassMethodKind::Task;
      }
      // The built-in randomization methods are functions too (IEEE
      // 1800-2017 18.6.1, 18.8, 18.9, 18.13).
      const bool builtin_function = name == "randomize"
          || name == "rand_mode" || name == "constraint_mode"
          || name == "get_randstate" || name == "set_randstate";
      if (kind == SystemVerilogClassMethodKind::Function
          && (builtin_function
              || (scope.class_owner == nullptr
                  && !find_methods(*class_identity(*receiver_type), name,
                      SystemVerilogClassMethodKind::Function, &*receiver_type)
                      .empty()))) {
        // A function method called as a statement discards its result
        // (IEEE 1800-2017 13.4.1); outside class methods it lowers as an
        // expression statement. Class method bodies keep the task form,
        // which their executor runs directly.
        // The parser's member-call form: `.name` with the receiver first.
        Expression call{
            ExpressionKind::Call, "." + std::string{name}, {}, statement.span};
        call.operands.push_back(Expression{
            ExpressionKind::Identifier,
            statement.task_name.substr(0, dot),
            {},
            statement.span});
        for (auto& argument : statement.task_arguments) {
          call.operands.push_back(std::move(argument));
        }
        if (!statement.task_argument_names.empty()) {
          call.call_argument_names.push_back(std::string{});
          for (const auto& argument_name : statement.task_argument_names) {
            call.call_argument_names.push_back(argument_name);
          }
        }
        resolve_expression(call, scope);
        statement.kind = StatementKind::ContainerMethod;
        statement.value = std::move(call);
        statement.task_name.clear();
        statement.task_arguments.clear();
        statement.task_argument_names.clear();
        return;
      }
      const auto match = select_method(
          *class_identity(*receiver_type),
          name,
          kind,
          statement.task_arguments.size(),
          statement.span, &*receiver_type);
      // A static task called through a handle runs without an object
      // (IEEE 1800-2017 8.10); the handle only names the class.
      if (match && match->method && match->method->is_static
          && kind != SystemVerilogClassMethodKind::Constructor) {
        statement.task_name = "@sv-static-task:"
            + match->method->canonical_identity;
        retain_task_profile(
            statement,
            *match->method,
            match->owner,
            &*receiver_type);
        for (std::size_t index = 0;
             index < statement.task_arguments.size(); ++index) {
          if (index < match->method->arguments.size()) {
            resolve_typed_expression(statement.task_arguments[index],
                scope, match->method->arguments[index].type);
          } else {
            resolve_expression(statement.task_arguments[index], scope);
          }
        }
        return;
      }
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
        if (kind == SystemVerilogClassMethodKind::Constructor
            && !match->method) {
          // The actuals of `super.new(...)` are expressions of the derived
          // constructor (IEEE 1800-2017 8.17).
          for (std::size_t index = 1U;
               index < statement.task_arguments.size(); ++index) {
            resolve_expression(statement.task_arguments[index], scope);
          }
        }
        if (match->method) {
          if (kind != SystemVerilogClassMethodKind::Constructor) {
            retain_task_profile(
                statement,
                *match->method,
                match->owner,
                &*receiver_type);
          }
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
        if (match && match->method && match->method->is_static
            && kind == SystemVerilogClassMethodKind::Function
            && scope.class_owner == nullptr
            && selected_uvm_intrinsic(*owner, name).empty()
            && !support::is_uvm_library_identity(
                match->method->canonical_identity)) {
          // A static function called as a statement discards its result
          // (IEEE 1800-2017 13.4.1) and lowers as an expression statement;
          // native UVM methods keep their task form.
          Expression call{
              ExpressionKind::Call, statement.task_name, {}, statement.span};
          for (auto& argument : statement.task_arguments) {
            call.operands.push_back(std::move(argument));
          }
          call.call_argument_names = statement.task_argument_names;
          resolve_expression(call, scope);
          statement.kind = StatementKind::ContainerMethod;
          statement.value = std::move(call);
          statement.task_name.clear();
          statement.task_arguments.clear();
          statement.task_argument_names.clear();
          return;
        }
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
void Resolver::resolve_statement(
    Statement& statement,
    const Scope& inherited_scope,
    const std::optional<std::string>& return_class,
    const SystemVerilogScalarKind return_scalar) {
  // Only a statement that declares objects needs its own scope copy.
  std::optional<Scope> declared_scope;
  if (!statement.type_aliases.empty()) {
    declared_scope = inherited_scope;
    for (auto& alias : statement.type_aliases) {
      if (auto scoped = resolve_class_scoped_type(alias.type, *declared_scope)) {
        alias.type = std::move(*scoped);
      }
      declared_scope->aliases.insert_or_assign(alias.name, &alias.type);
    }
  }
  if (!statement.declarations.empty()) {
    if (!declared_scope) declared_scope = inherited_scope;
    for (auto& declaration : statement.declarations) {
      if (auto scoped = resolve_class_scoped_type(
              declaration.type, *declared_scope)) {
        declaration.type = std::move(*scoped);
      }
      if (declaration.initializer) {
        resolve_typed_expression(
            *declaration.initializer, *declared_scope, declaration.type);
      }
      declared_scope->objects.assign(declaration.name, declaration.type);
    }
  }
  const Scope& scope = declared_scope ? *declared_scope : inherited_scope;
  // `$cast(dest, value);` discards the call's result (IEEE 1800-2017
  // 6.24.2); a class destination resolves to the checked class cast.
  if (statement.kind == StatementKind::TaskCall
      && statement.task_name == "$cast"
      && statement.task_arguments.size() == 2U) {
    Expression call{ExpressionKind::Call, "@sv-dollar-cast",
        {std::move(statement.task_arguments[0]),
         std::move(statement.task_arguments[1])},
        statement.span};
    statement.kind = StatementKind::ContainerMethod;
    statement.value = std::move(call);
    statement.task_name.clear();
    statement.task_arguments.clear();
    statement.task_argument_names.clear();
  }
  // A task of an element of a module or interface instance array,
  // `u[0].t(x)`, is a hierarchical task call (IEEE 1800-2017 23.8).
  if (statement.kind == StatementKind::ContainerMethod
      && statement.value.kind == ExpressionKind::Call
      && statement.value.text.starts_with('.')
      && !statement.value.operands.empty()
      && statement.value.operands.front().kind == ExpressionKind::Index) {
    std::string suffix;
    const Expression* root = &statement.value.operands.front();
    bool constant_path = true;
    while (root->kind == ExpressionKind::Index && root->operands.size() == 2U) {
      if (root->operands[1].kind != ExpressionKind::IntegerLiteral
          || root->operands[1].text.find_first_not_of("0123456789")
              != std::string::npos) {
        constant_path = false;
        break;
      }
      suffix = "[" + root->operands[1].text + "]" + suffix;
      root = &root->operands.front();
    }
    const auto instance = constant_path
            && root->kind == ExpressionKind::Identifier
        ? scope.instances.find(root->text)
        : scope.instances.end();
    const auto name = statement.value.text.substr(1U);
    const auto target_unit = instance != scope.instances.end()
        ? std::ranges::find(design_.units, instance->second, &DesignUnit::name)
        : design_.units.end();
    if (target_unit != design_.units.end()
        && std::ranges::any_of(target_unit->tasks,
            [&](const auto& task) { return task.name == name; })) {
      statement.kind = StatementKind::TaskCall;
      statement.task_name = root->text + suffix + "." + name;
      statement.task_arguments.clear();
      statement.task_argument_names.clear();
      for (std::size_t index = 1U; index < statement.value.operands.size();
           ++index) {
        statement.task_arguments.push_back(
            std::move(statement.value.operands[index]));
        statement.task_argument_names.push_back(
            index < statement.value.call_argument_names.size()
                ? statement.value.call_argument_names[index]
                : std::string{});
      }
      statement.value = Expression{};
      return;
    }
  }
  // A task called through a selected receiver, `items[i].run(x)`, parses as
  // a method call statement (IEEE 1800-2017 8.4, 13.3).
  if (statement.kind == StatementKind::ContainerMethod
      && statement.value.kind == ExpressionKind::Call
      && statement.value.text.starts_with('.')
      && !statement.value.operands.empty()
      && statement.value.operands.front().kind
          != ExpressionKind::Identifier) {
    auto receiver = statement.value.operands.front();
    const auto receiver_type = resolve_expression(receiver, scope);
    const auto identity = receiver_type
        ? class_identity(*receiver_type) : std::nullopt;
    const auto name = statement.value.text.substr(1U);
    // `items[i].srandom(seed);` seeds that object's generator
    // (IEEE 1800-2017 18.13.3, 18.13.5).
    if (identity && statement.value.operands.size() == 2U
        && (name == "srandom" || name == "set_randstate")
        && find_methods(*identity, name, SystemVerilogClassMethodKind::Function)
               .empty()
        && find_methods(*identity, name, SystemVerilogClassMethodKind::Task)
               .empty()) {
      statement.kind = StatementKind::TaskCall;
      statement.task_name = name == "srandom" ? "@sv-object-srandom"
                                              : "@sv-object-set-randstate";
      statement.task_arguments.clear();
      statement.task_argument_names.clear();
      statement.task_arguments.push_back(std::move(receiver));
      statement.task_arguments.push_back(
          std::move(statement.value.operands[1]));
      statement.task_argument_names.assign(2U, std::string{});
      resolve_expression(statement.task_arguments.back(), scope);
      statement.value = Expression{};
      return;
    }
    if (identity
        && !find_methods(*identity, name, SystemVerilogClassMethodKind::Task)
                .empty()) {
      const auto match = select_method(
          *identity, name, SystemVerilogClassMethodKind::Task,
          statement.value.operands.size() - 1U, statement.span);
      if (!match || !match->method) {
        return;
      }
      statement.kind = StatementKind::TaskCall;
      statement.task_name = "@sv-task:" + match->method->canonical_identity;
      statement.task_arguments.clear();
      statement.task_argument_names.clear();
      statement.task_arguments.push_back(std::move(receiver));
      statement.task_argument_names.emplace_back();
      for (std::size_t index = 1U; index < statement.value.operands.size();
           ++index) {
        statement.task_arguments.push_back(
            std::move(statement.value.operands[index]));
        statement.task_argument_names.push_back(
            index < statement.value.call_argument_names.size()
                ? statement.value.call_argument_names[index]
                : std::string{});
      }
      statement.value = Expression{};
      retain_task_profile(
          statement, *match->method, match->owner, &*receiver_type);
      for (std::size_t index = 1U; index < statement.task_arguments.size();
           ++index) {
        const auto& actual_name = statement.task_argument_names[index];
        auto formal = match->method->arguments.begin()
            + static_cast<std::ptrdiff_t>(std::min(
                index - 1U, match->method->arguments.size()));
        if (!actual_name.empty()) {
          formal = std::ranges::find(
              match->method->arguments, actual_name,
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
  // A class handle receives only a handle of its own class or of a class
  // derived from it, or one implementing it (IEEE 1800-2017 8.15). Only
  // plain variables have classes known independently of specialization.
  if (statement.kind == StatementKind::Assignment && target_type && value_type
      && statement.value.kind == ExpressionKind::Identifier
      && statement.target.kind == ExpressionKind::Identifier
      && statement.target.text.find('.') == std::string::npos
      && !target_type->systemverilog_container
      && !value_type->systemverilog_container) {
    const auto target_class = class_identity(*target_type);
    const auto value_class = class_identity(*value_type);
    if (target_class && value_class && *target_class != *value_class
        && !class_assignable(*value_class, *target_class)) {
      diagnose(
          diagnostics_,
          "FSIM-SV-CLASS-025",
          "a handle of class '" + *value_class
              + "' cannot be assigned to a variable of class '"
              + *target_class + "'",
          statement.span);
    }
  }
  const bool expected_chandle =
      expected_scalar == SystemVerilogScalarKind::Chandle;
  const bool destination_type_known =
      statement.kind == StatementKind::Return || target_type.has_value();
  const bool virtual_interface =
      (value_type && value_type->systemverilog_virtual_interface)
      || (target_type && target_type->systemverilog_virtual_interface);
  if (typed_assignment && destination_type_known && statement.value.valid()
      && !virtual_interface
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
    // A chandle is true when it is not null (IEEE 1800-2017 6.14).
    const auto span = statement.condition.span;
    Expression handle = std::move(statement.condition);
    statement.condition = Expression{
        ExpressionKind::Binary, "!=",
        {std::move(handle),
         Expression{ExpressionKind::Call, "@sv-null", {}, span}},
        span};
    resolve_expression(statement.condition, scope);
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
    resolve_member_sensitivity(sensitivity, scope);
    resolve_expression(sensitivity.expression, scope);
  }
  if (statement.kind == StatementKind::Report) {
    for (auto& argument : statement.task_arguments) {
      resolve_expression(argument, scope);
    }
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
void Resolver::resolve_function(FunctionDeclaration& function, Scope scope) {
  // A default argument is resolved where the subroutine is declared
  // (IEEE 1800-2017 13.5.3).
  for (auto& argument : function.arguments) {
    if (argument.default_value) {
      resolve_typed_expression(*argument.default_value, scope, argument.type);
    }
  }
  add_objects(scope, function.arguments);
  add_objects(scope, function.variables);
  scope.objects.assign(function.name, function.return_type);
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
void Resolver::resolve_task(TaskDeclaration& task, Scope scope) {
  // A default argument is resolved where the subroutine is declared
  // (IEEE 1800-2017 13.5.3).
  for (auto& argument : task.arguments) {
    if (argument.default_value) {
      resolve_typed_expression(*argument.default_value, scope, argument.type);
    }
  }
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
void Resolver::resolve_class(SystemVerilogClassDeclaration& declaration, Scope scope) {
  scope.class_owner = &declaration;
  scope.lexical_identity = declaration.canonical_identity;
  // A parameter default naming another specialization's constant
  // (`parameter int v = C#(7)::b`, IEEE 1800-2017 8.25.1).
  for (auto& parameter : declaration.parameters) {
    if (parameter.kind == ParameterKind::Value
        && parameter.default_value.valid()) {
      resolve_module_constant(parameter.default_value, scope);
    }
  }
  for (auto& property : declaration.properties) {
    if (property.declaration.initializer) {
      resolve_typed_expression(
          *property.declaration.initializer,
          scope,
          property.declaration.type);
    }
  }
  // A formal, result or local typed by a typedef of a base class or an
  // extended interface class takes that type (IEEE 1800-2017 8.26.3).
  const auto inherit_type = [&](Type& type) {
    if (type.named_type.empty()
        || type.named_type.find("::") != std::string::npos) {
      return;
    }
    for (std::string lexical = declaration.canonical_identity;;) {
      if (class_aliases_.contains(lexical + "::" + type.named_type)) return;
      const auto separator = lexical.rfind("::");
      if (separator == std::string::npos) break;
      lexical.resize(separator);
    }
    auto resolved = resolve_alias_type(type, declaration.canonical_identity);
    if (resolved.named_type != type.named_type
        || resolved.spelling != type.spelling) {
      type = std::move(resolved);
    }
  };
  for (auto& method : declaration.methods) {
    for (auto& argument : method.arguments) inherit_type(argument.type);
    inherit_type(method.return_type);
    for (auto& variable : method.variables) inherit_type(variable.type);
  }
  for (auto& property : declaration.properties) {
    if (!property.declaration.type.systemverilog_container) {
      inherit_type(property.declaration.type);
    }
    // A property typed by an array typedef declared outside the class
    // (`typedef int ia_t[]; class C; ia_t arr;`) has its dimensions.
    auto& type = property.declaration.type;
    if (!type.systemverilog_container && !type.named_type.empty()
        && type.systemverilog_class_declaration.empty()) {
      if (const auto alias = container_alias(
              declaration.canonical_identity, type.named_type)) {
        type = *alias;
      }
    }
  }
  // A property of an enumeration typedef keeps the enumeration's literals,
  // which bound its random values (IEEE 1800-2017 18.4).
  for (auto& property : declaration.properties) {
    auto& type = property.declaration.type;
    if (type.named_type.empty() || !type.enumeration_literals.empty()
        || type.systemverilog_container) {
      continue;
    }
    auto resolved = resolve_alias_type(
        type, declaration.canonical_identity);
    // A compilation-unit typedef is held by the unit's package.
    if (resolved.enumeration_literals.empty()
        && type.named_type.find("::") == std::string::npos) {
      const Type* found{};
      std::size_t matches{};
      for (const auto& unit : design_.units) {
        if (unit.kind != UnitKind::SystemVerilogPackage
            || !unit.name.starts_with("fsim_unit_")) {
          continue;
        }
        for (const auto& alias : unit.type_aliases) {
          if (alias.name == type.named_type
              && !alias.type.enumeration_literals.empty()) {
            found = &alias.type;
            ++matches;
          }
        }
      }
      if (matches == 1U) resolved = *found;
    }
    if (!resolved.enumeration_literals.empty()
        && resolved.enumeration_literals.size()
            == resolved.systemverilog_enumeration_values.size()) {
      type.enumeration_literals = resolved.enumeration_literals;
      type.systemverilog_enumeration_values
          = resolved.systemverilog_enumeration_values;
    }
  }
  for (auto& method : declaration.methods) {
    auto method_scope = scope;
    method_scope.method_body = true;
    add_objects(method_scope, method.arguments);
    // A formal typed by an array typedef of class handles keeps the array
    // (`typedef C list_t[string]; function f(ref list_t l)`).
    for (const auto& argument : method.arguments) {
      if (!argument.type.named_type.empty()
          && argument.type.named_type
              != argument.type.systemverilog_class_name) {
        auto resolved = resolve_alias_type(
            argument.type, declaration.canonical_identity);
        if (resolved.systemverilog_container) {
          method_scope.objects.assign(argument.name, std::move(resolved));
        }
      }
    }
    add_objects(method_scope, method.variables);
    if (method.kind == SystemVerilogClassMethodKind::Function) {
      method_scope.objects.assign(method.name, method.return_type);
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
} // namespace fsim::frontend::class_resolution_detail

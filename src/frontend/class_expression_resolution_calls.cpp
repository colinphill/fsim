// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution_internal.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <utility>

namespace fsim::frontend::class_resolution_detail {

[[nodiscard]] std::optional<MethodMatch> Resolver::select_method(
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
  if (expression.text == "process::self"
      || expression.text == "std::process::self") {
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
std::optional<Type> Resolver::resolve_typed_expression(
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
void Resolver::resolve_task_call(Statement& statement, const Scope& scope) {
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
void Resolver::resolve_statement(
    Statement& statement,
    const Scope& inherited_scope,
    const std::optional<std::string>& return_class,
    const SystemVerilogScalarKind return_scalar) {
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
void Resolver::resolve_function(FunctionDeclaration& function, Scope scope) {
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
void Resolver::resolve_task(TaskDeclaration& task, Scope scope) {
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
} // namespace fsim::frontend::class_resolution_detail

// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution_internal.hpp"
#include "fsim/support/process_method.hpp"
#include "fsim/support/stack_growth.hpp"
#include <array>

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::frontend::class_resolution_detail {

namespace {

// Turns `expression` into the synthesized node `marker` with `argument`; the
// text of a marked node is empty.
void mark_expression(Expression& expression,
    const support::ExpressionMarker marker, std::string argument = { },
    std::string owner = { })
{
  expression.text.clear();
  expression.marker = marker;
  expression.marker_argument = std::move(argument);
  expression.marker_owner = std::move(owner);
}

// Turns the callee of `statement` into the synthesized callee `marker` with
// `argument`; the task name is empty.
void mark_task(Statement& statement, const support::ExpressionMarker marker,
    std::string argument = { })
{
  statement.task_name.clear();
  statement.task_marker = marker;
  statement.task_marker_argument = std::move(argument);
}

// A cast to the built-in data type `kind` (IEEE 1800-2023 6.24.1).
[[nodiscard]] bool builtin_cast(const Expression& expression,
    const support::SystemVerilogBuiltinType kind) noexcept
{
  return expression.marker == support::ExpressionMarker::sv_cast
      && expression.systemverilog_cast_target
          == support::SystemVerilogCastTarget::builtin
      && expression.systemverilog_cast_builtin == kind;
}

// A synthesized SystemVerilog expression the class resolution has already
// bound: every marker of the expression family from a member selection to a
// coverage query or option read, and the built-in methods of a class object.
[[nodiscard]] bool synthesized_expression(
    const support::ExpressionMarker marker) noexcept
{
  return (marker >= support::ExpressionMarker::sv_select
          && marker <= support::ExpressionMarker::sv_coverage_option)
      || (marker >= support::ExpressionMarker::builtin_randomize
          && marker <= support::ExpressionMarker::builtin_get_randstate);
}

// Array methods whose call takes an iterator with-clause (IEEE 1800-2023
// 7.12).
[[nodiscard]] bool iterator_method(
    const support::BuiltinMethod method) noexcept
{
  switch (method) {
  case support::BuiltinMethod::find:
  case support::BuiltinMethod::find_index:
  case support::BuiltinMethod::find_first:
  case support::BuiltinMethod::find_first_index:
  case support::BuiltinMethod::find_last:
  case support::BuiltinMethod::find_last_index:
  case support::BuiltinMethod::min:
  case support::BuiltinMethod::max:
  case support::BuiltinMethod::unique:
  case support::BuiltinMethod::unique_index:
  case support::BuiltinMethod::sort:
  case support::BuiltinMethod::rsort:
  case support::BuiltinMethod::sum:
  case support::BuiltinMethod::product:
  case support::BuiltinMethod::reduce_and:
  case support::BuiltinMethod::reduce_or:
  case support::BuiltinMethod::reduce_xor:
  case support::BuiltinMethod::map:
    return true;
  default:
    return false;
  }
}

// Built-in array methods that may omit the parentheses of a call without
// arguments (IEEE 1800-2023 7.12, 13.4.1) when used in an expression.
[[nodiscard]] bool argumentless_array_method(
    const support::BuiltinMethod method) noexcept
{
  switch (method) {
  case support::BuiltinMethod::size:
  case support::BuiltinMethod::num:
  case support::BuiltinMethod::delete_entries:
  case support::BuiltinMethod::pop_front:
  case support::BuiltinMethod::pop_back:
  case support::BuiltinMethod::sum:
  case support::BuiltinMethod::product:
  case support::BuiltinMethod::reduce_and:
  case support::BuiltinMethod::reduce_or:
  case support::BuiltinMethod::reduce_xor:
  case support::BuiltinMethod::min:
  case support::BuiltinMethod::max:
  case support::BuiltinMethod::unique:
  case support::BuiltinMethod::unique_index:
  case support::BuiltinMethod::reverse:
  case support::BuiltinMethod::sort:
  case support::BuiltinMethod::rsort:
  case support::BuiltinMethod::shuffle:
    return true;
  default:
    return false;
  }
}

// Built-in array methods that may omit their parentheses as a statement
// (IEEE 1800-2023 7.10.2, 7.12, 13.4.1).
[[nodiscard]] bool argumentless_statement_method(
    const support::BuiltinMethod method) noexcept
{
  switch (method) {
  case support::BuiltinMethod::delete_entries:
  case support::BuiltinMethod::reverse:
  case support::BuiltinMethod::sort:
  case support::BuiltinMethod::rsort:
  case support::BuiltinMethod::shuffle:
  case support::BuiltinMethod::pop_front:
  case support::BuiltinMethod::pop_back:
    return true;
  default:
    return false;
  }
}

} // namespace

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
  if (expression.marker == support::ExpressionMarker::sv_null) {
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
  if (builtin_cast(expression, support::SystemVerilogBuiltinType::chandle)) {
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
  if (builtin_cast(expression, support::SystemVerilogBuiltinType::void_type)
      && expression.operands.size() == 1U) {
    resolve_expression(expression.operands.front(), scope);
    expression.systemverilog_scalar_kind =
        SystemVerilogScalarKind::None;
    return Type{};
  }
  if (expression.marker == support::ExpressionMarker::sv_size_cast
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
  if (expression.marker == support::ExpressionMarker::sv_cast
      && expression.operands.size() == 1U) {
    // A static cast of a class handle to a class it may be assigned to is
    // that handle (IEEE 1800-2017 6.24.1, 8.15).
    std::vector<const SystemVerilogClassDeclaration*> classes;
    if (expression.systemverilog_cast_target
        == support::SystemVerilogCastTarget::named) {
      classes = resolve_class_name(expression.marker_argument, scope);
    }
    if (classes.size() == 1U) {
      const auto target = class_type(*classes.front());
      auto operand_type = resolve_expression(
          expression.operands.front(), scope);
      const auto operand_class = operand_type
          ? class_identity(*operand_type) : std::nullopt;
      const auto target_class = class_identity(target);
      const bool null_operand = expression.operands.front().marker
          == support::ExpressionMarker::sv_null;
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
  if (expression.marker == support::ExpressionMarker::sv_process_self) {
    for (auto& operand : expression.operands) {
      resolve_expression(operand, scope);
    }
    expression.systemverilog_scalar_kind =
        SystemVerilogScalarKind::Chandle;
    return chandle_type();
  }
  if (expression.marker == support::ExpressionMarker::sv_shallow_copy
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
  if (expression.marker == support::ExpressionMarker::sv_new_typed) {
    const auto name = expression.marker_argument;
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
    // `C#(4)::new` constructs that specialization (IEEE 1800-2023 8.8,
    // 8.25).
    if (const auto selection = resolve_class_selection(name, scope,
            expression.systemverilog_class_selection.get());
        selection && selects_specialization(name, *selection)) {
      expression.systemverilog_class_owner =
          std::make_shared<const Type>(selection->type);
    }
    mark_expression(expression, support::ExpressionMarker::sv_new);
    return resolve_expression(
        expression, scope, constructed,
        SystemVerilogScalarKind::None);
  }
  if (expression.marker == support::ExpressionMarker::sv_new) {
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
      mark_expression(expression,
          support::ExpressionMarker::sv_new_constructor, *expected_class,
          *expected_class);
      expression.nominal_type = *expected_class;
      for (auto& operand : expression.operands) {
        resolve_expression(operand, scope);
      }
      Type deferred;
      set_systemverilog_type_name(deferred, *expected_class);
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
    mark_expression(expression,
        support::ExpressionMarker::sv_new_constructor, *expected_class,
        *expected_class);
    expression.nominal_type = *expected_class;
    for (auto& operand : expression.operands) {
      resolve_expression(operand, scope);
    }
    return class_type(*find_class(*expected_class));
  }
  if (expression.marker == support::ExpressionMarker::sv_dollar_cast) {
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
      mark_expression(expression,
          support::ExpressionMarker::sv_dollar_cast_class,
          *class_identity(*operand_types.front()),
          *class_identity(*operand_types.front()));
    }
    return systemverilog_builtin_type(ValueDomain::Logic4, support::SystemVerilogBuiltinType::bit,
        PackedRange{0, 0, true}, false);
  }
  if (expression.marker == support::ExpressionMarker::sv_select
      && expression.operands.size() == 1U) {
    const auto receiver_type = resolve_expression(
        expression.operands.front(), scope);
    const auto identity = receiver_type
        ? class_identity(*receiver_type) : std::nullopt;
    const auto member = expression.marker_argument;
    // A member of a packed structure class property stays a selection.
    if (!identity && receiver_type
        && is_class_property_access(expression.operands.front())) {
      return packed_member_type(*receiver_type, member);
    }
    // A class handle member of an unpacked structure element (7.2).
    if (!identity && receiver_type) {
      if (auto type = structure_member_class_type(
              *receiver_type, member, scope)) {
        expression.nominal_type = *class_identity(*type);
        return type;
      }
    }
    // A member of a value element of an object's array (`obj.v[i].m`) is
    // a member path on the indexed property (`index.m`).
    if (!identity
        && expression.operands.front().kind == ExpressionKind::Call
        && expression.operands.front().marker
            == support::ExpressionMarker::sv_container_index
        && expression.operands.front().operands.size() == 2U) {
      auto element = std::move(expression.operands.front());
      auto property = marked_expression(
          support::ExpressionMarker::sv_container_property,
          std::move(element.marker_argument),
          {std::move(element.operands[0])}, element.span);
      Expression selected{ExpressionKind::Index,
          "index." + std::string{member},
          {std::move(property), std::move(element.operands[1])},
          expression.span};
      selected.builtin_method = support::builtin_method_named(member)
              == support::BuiltinMethod::triggered
          ? support::BuiltinMethod::triggered
          : support::BuiltinMethod::none;
      selected.systemverilog_source_order
          = expression.systemverilog_source_order;
      expression = std::move(selected);
      return resolve_expression(
          expression, scope, expected_class, expected_scalar);
    }
    // A member of an element of an instance array selected by a constant
    // index (`u.a[1].m`) is named by the whole path (IEEE 1800-2023 23.6).
    if (!identity && !receiver_type
        && expression.operands.front().kind == ExpressionKind::Index
        && expression.operands.front().operands.size() == 2U
        && expression.operands.front().operands[0].kind
            == ExpressionKind::Identifier
        && expression.operands.front().operands[1].kind
            == ExpressionKind::IntegerLiteral
        && !expression.operands.front().operands[1].text.empty()
        && expression.operands.front().operands[1].text.find_first_not_of(
               "0123456789") == std::string::npos
        && expression.operands.front().text == "index") {
      const auto& element = expression.operands.front();
      Expression named{ExpressionKind::Identifier,
          element.operands[0].text + "[" + element.operands[1].text + "]."
              + std::string{member},
          {}, expression.span};
      named.systemverilog_source_order = expression.systemverilog_source_order;
      if (auto type = resolve_expression(named, scope);
          type && class_identity(*type)) {
        expression = std::move(named);
        return type;
      }
    }
    if (!identity) {
      auto receiver = std::move(expression.operands.front());
      receiver.text += "." + std::string{member};
      if (support::builtin_method_named(member)
          == support::BuiltinMethod::triggered) {
        receiver.builtin_method = support::BuiltinMethod::triggered;
      }
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
          mark_expression(expression,
              support::ExpressionMarker::sv_static_method,
              method->method->canonical_identity,
              method->owner->canonical_identity);
          expression.operands.clear();
          expression.call_argument_names.clear();
          retain_call_profile(expression, *method->method, false);
        } else {
          mark_expression(expression, support::ExpressionMarker::sv_method,
              method->method->canonical_identity,
              method->owner->canonical_identity);
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
        mark_expression(expression, support::ExpressionMarker::none);
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
      mark_expression(expression,
          type.systemverilog_container
              ? support::ExpressionMarker::sv_static_container_property
              : support::ExpressionMarker::sv_static_property,
          match.owner->canonical_identity + "::" + std::string{member},
          match.owner->canonical_identity);
      expression.operands.clear();
      retain_result_type(expression, type);
      if (const auto result_identity = class_identity(type)) {
        expression.nominal_type = *result_identity;
      }
      return type;
    }
    mark_expression(expression,
        match.property->declaration.type.systemverilog_container
            ? support::ExpressionMarker::sv_container_property
            : support::ExpressionMarker::sv_property,
        match.owner->canonical_identity + "::" + std::string{member},
        match.owner->canonical_identity);
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
  if (synthesized_expression(expression.marker)) {
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
    // The call on `receiver` sets the mode of `target`: a random property
    // or constraint block by canonical identity, or no target (an empty
    // payload) for every one of the object.
    const auto mode_call = [&](Expression receiver, const std::string& target) {
      expression.operands.front() = std::move(receiver);
      mark_expression(expression,
          expression.text == ".rand_mode"
              ? support::ExpressionMarker::builtin_rand_mode
              : support::ExpressionMarker::builtin_constraint_mode,
          target);
      expression.call_argument_names.resize(expression.operands.size());
      expression.call_argument_directions.assign(
          expression.operands.size(), PortDirection::Input);
      for (auto& operand : expression.operands | std::views::drop(1)) {
        resolve_expression(operand, scope);
      }
      Type result = systemverilog_builtin_type(ValueDomain::Integer,
          support::SystemVerilogBuiltinType::int_type,
          PackedRange{31, 0, true}, true);
      retain_result_type(expression, result);
      return result;
    };
    // The random property or constraint block `member` of class `identity`
    // names, diagnosed when it is not a visible one.
    const auto mode_target = [&](const std::optional<std::string>& identity,
                                 const std::string_view member)
        -> std::optional<std::string> {
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
      return canonical;
    };
    if (built_in_mode && expression.operands.size() == 2U
        && !implicit_member
        && expression.operands.front().kind == ExpressionKind::Identifier
        && object_receiver(expression.operands.front())) {
      auto receiver = expression.operands.front();
      const auto receiver_type = resolve_expression(receiver, scope);
      if (receiver_type && class_identity(*receiver_type)
          && !receiver_type->systemverilog_container) {
        return mode_call(std::move(receiver), std::string { });
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
        const auto canonical = mode_target(identity,
            std::string_view{expression.operands.front().text}.substr(
                selected + 1U));
        if (!canonical) return std::nullopt;
        return mode_call(std::move(receiver), *canonical);
      }
    }
    // `a[i].rand_mode(b)` and `a[i].m.rand_mode(b)`: an element of an
    // array of handles, or a member below one: an index node whose text
    // carries the member path, or its member selection once resolved
    // (IEEE 1800-2017 8.22, 18.8).
    const auto& mode_receiver = expression.operands.front();
    const bool indexed = mode_receiver.kind == ExpressionKind::Index
        && mode_receiver.operands.size() == 2U
        && (mode_receiver.text == "index"
            || mode_receiver.text.starts_with("index."));
    const bool selected_member = mode_receiver.kind == ExpressionKind::Call
        && mode_receiver.marker == support::ExpressionMarker::sv_select
        && mode_receiver.operands.size() == 1U;
    if (built_in_mode && !implicit_member && (indexed || selected_member)) {
      auto whole = mode_receiver;
      const auto whole_type = resolve_expression(whole, scope);
      if (whole_type && class_identity(*whole_type)
          && !whole_type->systemverilog_container
          && expression.operands.size() == 2U) {
        return mode_call(std::move(whole), std::string { });
      }
      std::optional<Expression> receiver;
      std::string member;
      if (selected_member) {
        receiver = mode_receiver.operands.front();
        member = mode_receiver.marker_argument;
      } else if (const auto dot = mode_receiver.text.rfind('.');
                 dot != std::string::npos) {
        receiver = mode_receiver;
        receiver->text = mode_receiver.text.substr(0, dot);
        member = mode_receiver.text.substr(dot + 1U);
      }
      if (receiver) {
        const auto receiver_type = resolve_expression(*receiver, scope);
        const auto identity = receiver_type
            ? class_identity(*receiver_type) : std::nullopt;
        if (identity && !receiver_type->systemverilog_container) {
          const auto canonical = mode_target(identity, member);
          if (!canonical) return std::nullopt;
          return mode_call(std::move(*receiver), *canonical);
        }
      }
    }
    // An unqualified method call in a randomize with-clause is called on
    // the randomized object, named again by this unresolved receiver.
    const auto unresolved_receiver = expression.text == ".randomize"
        ? std::optional<Expression>{expression.operands.front()}
        : std::nullopt;
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
    if (receiver_type && receiver_type->systemverilog_container) {
      const auto method = expression.text.substr(1U);
      const bool instance_property =
          expression.operands.front().kind == ExpressionKind::Call
          && expression.operands.front().marker
              == support::ExpressionMarker::sv_container_property
          && expression.operands.front().operands.size() == 1U;
      // A static property's methods apply to the class's one container
      // object, which the container method lowering binds directly
      // (IEEE 1800-2017 8.9, 7.10).
      if (instance_property) {
        const auto property = expression.operands.front().marker_argument;
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
        // The marked methods are the ones the container method lowering
        // reads (IEEE 1800-2017 7.5.1, 7.10.2, 7.12.3).
        using Marker = support::ExpressionMarker;
        using Method = support::BuiltinMethod;
        const auto builtin = support::builtin_method_named(method);
        const auto container_marker = [&] {
          switch (builtin) {
          case Method::pop_front:
            return Marker::sv_container_pop_front;
          case Method::pop_back:
            return Marker::sv_container_pop_back;
          case Method::size:
            return Marker::sv_container_size;
          case Method::sum:
            return Marker::sv_container_sum;
          case Method::product:
            return Marker::sv_container_product;
          case Method::reduce_and:
            return Marker::sv_container_and;
          case Method::reduce_or:
            return Marker::sv_container_or;
          case Method::reduce_xor:
            return Marker::sv_container_xor;
          default:
            return Marker::none;
          }
        }();
        if (container_marker != Marker::none) {
          mark_expression(expression, container_marker, property);
        } else {
          // Any other built-in array method keeps its name as the text
          // for display; push_back has its own marker, as in a task call
          // statement.
          const std::string method_name { method };
          mark_expression(expression,
              builtin == Method::push_back ? Marker::sv_container_push_back
                                           : Marker::sv_container_method,
              property);
          expression.text = method_name;
        }
        expression.builtin_method = builtin;
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
          Type result = systemverilog_builtin_type(ValueDomain::Integer,
              support::SystemVerilogBuiltinType::int_type,
              PackedRange{31, 0, true}, true);
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
      mark_expression(expression,
          support::ExpressionMarker::builtin_randomize);
      expression.call_argument_names.resize(expression.operands.size());
      expression.call_argument_directions.assign(
          expression.operands.size(), PortDirection::Input);
      const auto null_argument = [](const Expression& operand) {
        return operand.kind == ExpressionKind::Call
            && operand.marker == support::ExpressionMarker::sv_null;
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
          // The checker form `randomize(null)` (IEEE 1800-2017 18.11.1)
          // keeps its operand marked; it selects no property.
          expression.operands[index] = Expression{
              ExpressionKind::IntegerLiteral,
              "0",
              {},
              expression.operands[index].span};
          if (checker_call) {
            expression.operands[index].marker
                = support::ExpressionMarker::randomize_null;
          }
          continue;
        }
        const auto selected_name = expression.operands[index].text;
        const auto selected =
            expression.operands[index].kind == ExpressionKind::Identifier
                ? find_property(receiver_identity, selected_name)
                : PropertyMatch{};
        // Any property may be listed, static ones included; it is random
        // for this call (IEEE 1800-2017 18.11).
        if (!selected.property) {
          diagnose(
              diagnostics_,
              "FSIM-SV-CLASS-019",
              "object randomize variable list requires a property of "
              "class '" + receiver_identity + "'",
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
      // Inside the arguments of a call that is resolved as a whole, names
      // are only rewritten; that call's resolution binds them.
      bool deferring{false};
      const auto resolve_caller_names = [&](const auto& self,
                                            Expression& node) -> void {
        if (node.kind != ExpressionKind::Identifier
            && iterator_method(node.builtin_method)
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
        enum class Callee { none, object, caller };
        const auto callee_of = [&](const std::string_view text) {
          constexpr std::string_view local_prefix{"local::"};
          if (text.empty() || text.front() == '.'
              || text.front() == '$') {
            return Callee::none;
          }
          const bool local = text.starts_with(local_prefix);
          const auto callee = local ? text.substr(local_prefix.size()) : text;
          if (callee.find("::") != std::string_view::npos
              || callee.find('.') != std::string_view::npos) {
            return Callee::none;
          }
          if (!local && unresolved_receiver
              && !find_methods(receiver_identity, std::string{callee},
                      SystemVerilogClassMethodKind::Function,
                      &*receiver_type).empty()) {
            return Callee::object;
          }
          if (scope.class_owner != nullptr
              && !find_methods(scope.class_owner->canonical_identity,
                      std::string{callee},
                      SystemVerilogClassMethodKind::Function).empty()) {
            return Callee::caller;
          }
          return Callee::none;
        };
        // `(f).m`: a member of the object a with-clause function returns,
        // written as a path from the function's name.
        if ((node.kind == ExpressionKind::Identifier
                || (node.kind == ExpressionKind::Call && node.operands.empty()))
            && node.text.find('.') != std::string::npos
            && callee_of(std::string_view{node.text}.substr(
                   0, node.text.find('.'))) != Callee::none) {
          const auto dot = node.text.find('.');
          Expression selected{ExpressionKind::Call, node.text.substr(0, dot),
              {}, node.span};
          std::string_view path{node.text};
          path.remove_prefix(dot + 1U);
          while (!path.empty()) {
            const auto next = path.find('.');
            auto select = marked_expression(
                support::ExpressionMarker::sv_select,
                std::string{path.substr(0, next)}, {}, node.span);
            select.operands.push_back(std::move(selected));
            selected = std::move(select);
            path = next == std::string_view::npos ? std::string_view{}
                                                 : path.substr(next + 1U);
          }
          selected.systemverilog_source_order = node.systemverilog_source_order;
          node = std::move(selected);
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
              if (!deferring) resolve_expression(node, scope);
            }
            return;
          }
          // A class-scoped name, `C::m`, names a member of class C, such as
          // a class enum literal (IEEE 1800-2017 8.23).
          if (const auto scoped = node.text.rfind("::");
              !deferring && scoped != std::string::npos
              && node.text.find('.') == std::string::npos
              && resolve_class_name(node.text.substr(0, scoped), scope).size()
                  == 1U) {
            resolve_expression(node, scope);
            return;
          }
          // A class constant of the randomized object, such as an enum
          // literal declared in its class, is that constant (18.5.1, 18.7).
          if (const auto constant = node.text.find_first_of(".:")
                      == std::string::npos
                  ? find_property(receiver_identity, node.text)
                  : PropertyMatch{};
              !deferring && constant.property && constant.property->is_static
              && constant.property->is_const
              && constant.property->declaration.initializer
              && constant.owner->parameters.empty()) {
            node.text = constant.owner->canonical_identity + "::" + node.text;
            resolve_expression(node, scope);
            return;
          }
          if (!deferring && node.text.find("::") == std::string::npos
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
        // A function named in the with-clause is a method of the randomized
        // object when that class has one; otherwise, or when `local::`
        // qualified, it is a function of the calling scope (IEEE 1800-2017
        // 18.7, 18.7.1).
        // `get_c().m` and `(get_c).m`: a member of the object a function
        // returns is selected once the call is bound (IEEE 1800-2017 18.7).
        if (!deferring && node.kind == ExpressionKind::Call
            && node.marker == support::ExpressionMarker::sv_select
            && node.operands.size() == 1U) {
          auto& operand = node.operands.front();
          if (operand.kind == ExpressionKind::Identifier
              && callee_of(operand.text) != Callee::none) {
            operand = Expression{
                ExpressionKind::Call, operand.text, {}, operand.span};
          }
          if (operand.kind == ExpressionKind::Call
              && callee_of(operand.text) != Callee::none) {
            // A function of the calling scope is a method of the calling
            // object (`this.f()`), whose result's member is selected.
            constexpr std::string_view local_prefix{"local::"};
            if (callee_of(operand.text) == Callee::caller) {
              if (operand.text.starts_with(local_prefix)) {
                operand.text.erase(0, local_prefix.size());
              }
              operand.operands.insert(operand.operands.begin(),
                  Expression{ExpressionKind::Identifier, "this", {},
                      operand.span});
              operand.builtin_method
                  = support::builtin_method_named(operand.text);
              operand.text = "." + operand.text;
            }
            deferring = true;
            self(self, operand);
            deferring = false;
            resolve_expression(node, scope);
            return;
          }
        }
        if (node.kind == ExpressionKind::Call && !node.text.empty()
            && node.text.front() != '.'
            && node.text.front() != '$') {
          constexpr std::string_view local_prefix{"local::"};
          const bool local = node.text.starts_with(local_prefix);
          const auto callee = local
              ? node.text.substr(local_prefix.size())
              : node.text;
          if (callee.find("::") == std::string::npos
              && callee.find('.') == std::string::npos) {
            const auto kind = callee_of(node.text);
            const bool object_method = kind == Callee::object;
            const bool caller_method = kind == Callee::caller;
            if (object_method || caller_method) {
              // Each argument names in its own right: `local::` qualifies
              // only the name it prefixes (IEEE 1800-2017 18.7.1).
              const bool outer = deferring;
              deferring = true;
              for (auto& operand : node.operands) self(self, operand);
              deferring = outer;
              if (object_method) {
                node.operands.insert(
                    node.operands.begin(), *unresolved_receiver);
                node.text = "." + callee;
                node.builtin_method = support::builtin_method_named(callee);
              } else if (!deferring) {
                node.text = callee;
              }
              if (!deferring) resolve_expression(node, scope);
              return;
            }
          }
        }
        for (auto& operand : node.operands) self(self, operand);
      };
      for (auto& choices : expression.aggregate_choice_expressions) {
        for (auto& choice : choices) {
          resolve_caller_names(resolve_caller_names, choice);
        }
      }
      Type result = systemverilog_builtin_type(ValueDomain::Integer,
          support::SystemVerilogBuiltinType::int_type,
          PackedRange{31, 0, true}, true);
      retain_result_type(expression, result);
      return result;
    }
    // `obj.get_randstate()` returns the object's random number generator
    // state (IEEE 1800-2017 18.13.4).
    if (name == "get_randstate" && expression.operands.size() == 1U) {
      mark_expression(expression,
          support::ExpressionMarker::builtin_get_randstate);
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
    mark_expression(expression,
        expression.operands.front().text == "super"
            ? support::ExpressionMarker::sv_base_method
            : support::ExpressionMarker::sv_method,
        match->method->canonical_identity,
        match->owner->canonical_identity);
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
      mark_expression(expression, support::ExpressionMarker::sv_static_method,
          match->method->canonical_identity,
          match->owner->canonical_identity);
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
        // A typedef's actuals (`uvm_config_int` names
        // `uvm_config_db#(uvm_bitstream_t)`) select the type they denote.
        auto normalized_owner = *owner;
        normalize_selection_actuals(normalized_owner, scope);
        const auto intrinsic = selected_uvm_intrinsic(normalized_owner, name);
        // The unadorned name of a parameterized class selects the method
        // in the specialization the caller runs in (8.25.1).
        const bool specialized = std::ranges::any_of(
                owner->declaration->parameters,
                [](const ParameterDeclaration& parameter) {
                  return !parameter.local;
                })
            ? selects_specialization(
                  std::string_view{expression.text}.substr(0, selected),
                  *owner)
            : true;
        mark_expression(expression,
            support::ExpressionMarker::sv_static_method,
            intrinsic.empty() ? match->method->canonical_identity
                              : intrinsic,
            intrinsic.empty() ? match->owner->canonical_identity
                              : std::string { });
        if (specialized) {
          expression.systemverilog_class_owner =
              std::make_shared<const Type>(owner->type);
        }
        // A registry's create makes an object of the class its type
        // expression denotes (`C#(3)::type_id::create`, IEEE 1800-2023
        // 8.25, 6.22.1), so the call selects that class specialization.
        if (intrinsic.starts_with("@uvm-registry-create:")
            || intrinsic.starts_with("@uvm-registry-get:")) {
          expression.systemverilog_class_owner
              = selected_uvm_registry_type(*owner);
          resolve_class_owner_constants(expression, scope, 0U);
        }
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
  // function, `[std::] randomize` (IEEE 1800-2017 18.12), unless the design
  // declares a subroutine of that name here (26.7).
  if (scope.class_owner == nullptr && expression.text == "randomize"
      && !design_subroutine_visible(expression.text, scope)) {
    expression.text = "std::randomize";
    expression.marker = support::ExpressionMarker::sv_std_randomize;
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
    expression.builtin_method
        = support::builtin_method_named(expression.text);
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
      mark_expression(expression,
          match->method->is_static
              ? support::ExpressionMarker::sv_static_method
              : support::ExpressionMarker::sv_method,
          match->method->canonical_identity,
          match->owner->canonical_identity);
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
    if (aggregate_choice_marker(expression, index)
        != support::ExpressionMarker::sv_inline_constraint) {
      continue;
    }
    for (auto& choice : expression.aggregate_choice_expressions[index]) {
      resolve_expression(choice, scope);
    }
  }
}
// The std package is only wildcard imported into the compilation unit, so a
// class named process that the design declares or imports hides std::process
// (IEEE 1800-2023 26.7); `process::self` then calls that class's method. A
// name written `std::process::m` always selects the std class.
void Resolver::release_hidden_process_member(
    Expression& expression, const Scope& scope) const {
  if (expression.marker != support::ExpressionMarker::sv_process_self
      && expression.marker != support::ExpressionMarker::sv_process_state) {
    return;
  }
  const std::string_view text { expression.text };
  const auto separator = text.find("::");
  if (separator == std::string_view::npos || text.starts_with("std::")
      || resolve_class_name(text.substr(0U, separator), scope).empty()) {
    return;
  }
  expression.marker = support::ExpressionMarker::none;
  expression.marker_argument.clear();
}
// A std package class named by its bare name is hidden by a class of that
// name the design declares or imports (IEEE 1800-2023 26.7).
support::SystemVerilogStandardClass Resolver::visible_standard_class(
    const Type& type, const Scope& scope) const {
  const auto standard = type.systemverilog_standard_class;
  if (standard == support::SystemVerilogStandardClass::none
      || type.named_type.find("::") != std::string::npos
      || resolve_class_name(type.named_type, scope).empty()) {
    return standard;
  }
  return support::SystemVerilogStandardClass::none;
}
Type Resolver::release_hidden_standard_class(
    Type type, const Scope& scope) const {
  if (type.systemverilog_container) {
    for (auto& element : type.systemverilog_container->element_types) {
      element = release_hidden_standard_class(std::move(element), scope);
    }
  }
  if (type.systemverilog_standard_class
          == support::SystemVerilogStandardClass::none
      || visible_standard_class(type, scope)
          != support::SystemVerilogStandardClass::none) {
    return type;
  }
  const auto declarations = resolve_class_name(type.named_type, scope);
  auto released = class_type(*declarations.front());
  released.named_type_span = type.named_type_span;
  released.systemverilog_source_order = type.systemverilog_source_order;
  released.systemverilog_container = std::move(type.systemverilog_container);
  return released;
}
// A subroutine the unit declares, or a package it imports declares, under a
// std package subroutine's name hides that subroutine (IEEE 1800-2023 26.7).
bool Resolver::design_subroutine_visible(
    const std::string_view name, const Scope& scope) const {
  const auto declares = [&](const DesignUnit& unit) {
    return std::ranges::any_of(unit.functions,
               [&](const FunctionDeclaration& function) {
                 return function.name == name;
               })
        || std::ranges::any_of(unit.tasks,
               [&](const TaskDeclaration& task) { return task.name == name; });
  };
  if (scope.unit != nullptr && declares(*scope.unit)) {
    return true;
  }
  for (const auto* import : scope.imports) {
    if (!import->name.empty() && import->name != name) {
      continue;
    }
    for (const auto& candidate : design_.units) {
      if (candidate.kind == UnitKind::SystemVerilogPackage
          && candidate.name == import->package
          && effective_library(candidate.library) == scope.library
          && declares(candidate)) {
        return true;
      }
    }
  }
  return false;
}
std::optional<Type> Resolver::resolve_expression(
    Expression& expression,
    const Scope& scope,
    const std::optional<std::string>& expected_class,
    const SystemVerilogScalarKind expected_scalar) {
  if (!expression.valid()) return std::nullopt;
  release_hidden_process_member(expression, scope);
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
    // An element of an instance array selected by a constant index
    // (`u.a[1].m`) is named by the whole path (IEEE 1800-2023 23.6).
    if (!element_type && expression.operands[0].kind == ExpressionKind::Identifier
        && expression.operands[1].kind == ExpressionKind::IntegerLiteral
        && !expression.operands[1].text.empty()
        && expression.operands[1].text.find_first_not_of("0123456789")
            == std::string::npos) {
      Expression named{ExpressionKind::Identifier,
          expression.operands[0].text + "[" + expression.operands[1].text
              + "]." + expression.text.substr(std::string_view{"index."}.size()),
          {}, expression.span};
      named.systemverilog_source_order = expression.systemverilog_source_order;
      if (auto type = resolve_expression(named, scope);
          type && class_identity(*type)) {
        expression = std::move(named);
        return type;
      }
    }
    if (element_type && class_identity(*element_type)
        && !element_type->systemverilog_container) {
      Expression selected = std::move(element);
      std::string_view path{expression.text};
      path.remove_prefix(std::string_view{"index."}.size());
      while (!path.empty()) {
        const auto dot = path.find('.');
        const auto member = path.substr(0, dot);
        auto select = marked_expression(support::ExpressionMarker::sv_select,
            std::string{member}, {}, expression.span);
        if (support::builtin_method_named(member)
            == support::BuiltinMethod::triggered) {
          select.builtin_method = support::BuiltinMethod::triggered;
        }
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
    const auto member = std::string_view{expression.text}.substr(
        std::string_view{"index."}.size());
    if (element_type && element_type->systemverilog_container
        && member.find('.') == std::string_view::npos
        && argumentless_array_method(support::builtin_method_named(member))) {
      Expression call{ExpressionKind::Call, "." + std::string{member},
          {std::move(element)}, expression.span};
      call.builtin_method = support::builtin_method_named(member);
      call.systemverilog_source_order = expression.systemverilog_source_order;
      expression = std::move(call);
      return resolve_expression(
          expression, scope, expected_class, expected_scalar);
    }
  }
  // An element of a nested container of handles already resolved
  // (`q[i][j]` over a container index of the property `q`) is the object its
  // handles name (IEEE 1800-2017 7.4.5, 8.9).
  if (expression.kind == ExpressionKind::Index
      && expression.operands.size() == 2U
      && !expression.nominal_type.empty()
      && expression.operands[0].kind == ExpressionKind::Call
      && expression.operands[0].marker
          == support::ExpressionMarker::sv_container_index) {
    if (const auto* declaration = find_class(expression.nominal_type)) {
      auto type = class_type(*declaration);
      retain_result_type(expression, type);
      return type;
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
    auto& base_expression = expression.operands.front();
    // A fixed-size array nested in a dynamic, queue or associative array is
    // a container element of the property, not another dimension of it
    // (IEEE 1800-2017 7.4.5).
    const auto outer_static = [&]() {
      if (base_expression.kind != ExpressionKind::Call
          || base_expression.marker
              != support::ExpressionMarker::sv_container_index) {
        return true;
      }
      const std::string_view qualified{base_expression.marker_argument};
      const auto split = qualified.rfind("::");
      if (split == std::string_view::npos) return true;
      const auto match = find_property(
          qualified.substr(0, split), qualified.substr(split + 2U));
      if (match.property == nullptr) return true;
      const auto& outer = match.property->declaration.type.systemverilog_container;
      return !outer || outer->kind == SystemVerilogContainerKind::StaticArray;
    }();
    if (outer_static && base_expression.kind == ExpressionKind::Call
        && base_expression.marker
            == support::ExpressionMarker::sv_container_index
        && base_expression.operands.size() >= 2U
        && container->systemverilog_container->kind
            == SystemVerilogContainerKind::StaticArray
        && !container->systemverilog_container->element_types.empty()
        && !container->systemverilog_container->element_types.front()
                .systemverilog_class_declaration.empty()) {
      auto argument = std::move(base_expression.marker_argument);
      auto operands = std::move(base_expression.operands);
      operands.push_back(std::move(expression.operands[1]));
      expression.kind = ExpressionKind::Call;
      mark_expression(expression,
          support::ExpressionMarker::sv_container_index, std::move(argument));
      expression.operands = std::move(operands);
      retain_result_type(expression, element);
      expression.nominal_type = element.systemverilog_class_declaration;
      return element;
    }
    // `obj.v[i].m` keeps its member path (`index.m`): the elaborator
    // walks it as a value path rooted at the property.
    if (expression.operands[0].kind != ExpressionKind::Call
        || expression.operands[0].marker
            != support::ExpressionMarker::sv_container_property
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
    mark_expression(expression, support::ExpressionMarker::sv_container_index,
        expression.operands[0].marker_argument);
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
    // enumeration, not the handle (IEEE 1800-2023 9.7): a receiver of the
    // built-in process class selecting its status method.
    const auto process_typed = [&](const Type* type) {
      if (type == nullptr) {
        return false;
      }
      const auto& element = type->systemverilog_container
              && !type->systemverilog_container->element_types.empty()
          ? type->systemverilog_container->element_types.front()
          : *type;
      return visible_standard_class(element, scope)
          == support::SystemVerilogStandardClass::process;
    };
    const auto process_receiver = [&](const Expression& receiver) {
      return receiver.marker == support::ExpressionMarker::sv_process_self
          || (receiver.kind == ExpressionKind::Identifier
              && process_typed(scope.objects.find(receiver.text)));
    };
    const auto selects_status = [](const std::string_view member) {
      return support::process_method(member)
          == support::ProcessMethod::status;
    };
    const auto process_status = [&](const Expression& operand) {
      constexpr std::string_view index_member_prefix{"index."};
      if (operand.kind == ExpressionKind::Identifier) {
        const auto dot = operand.text.rfind('.');
        return dot != std::string::npos
            && selects_status(std::string_view{operand.text}.substr(dot + 1U))
            && process_typed(scope.objects.find(
                std::string_view{operand.text}.substr(0U, dot)));
      }
      if (operand.kind == ExpressionKind::Call
          && operand.text.starts_with('.') && operand.operands.size() == 1U) {
        return selects_status(std::string_view{operand.text}.substr(1U))
            && process_receiver(operand.operands.front());
      }
      return operand.kind == ExpressionKind::Index
          && operand.text.starts_with(index_member_prefix)
          && !operand.operands.empty()
          && selects_status(std::string_view{operand.text}.substr(
              index_member_prefix.size()))
          && process_receiver(operand.operands.front());
    };
    if (process_status(expression.operands[0])) {
      left = systemverilog_builtin_type(
          ValueDomain::Integer, support::SystemVerilogBuiltinType::int_type, PackedRange{31, 0, true}, true);
    }
    if (process_status(expression.operands[1])) {
      right = systemverilog_builtin_type(
          ValueDomain::Integer, support::SystemVerilogBuiltinType::int_type, PackedRange{31, 0, true}, true);
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
      return systemverilog_builtin_type(
          ValueDomain::Bit2, support::SystemVerilogBuiltinType::bit, PackedRange{0, 0, true}, false);
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
        && aggregate_choice_marker(expression, index)
            == support::ExpressionMarker::key;
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
      // So is a static property of a class (`C#(virtual I)::vif`, 8.9,
      // 8.25.1).
      const auto selected = root.rfind("::");
      if ((expression.text.find('.') != std::string::npos
              && scope.objects.contains(root))
          || (selected != std::string_view::npos
              && !resolve_class_name(root.substr(0, selected), scope)
                      .empty())) {
        (void)resolve_expression(expression, scope);
      }
    } else if (expression.kind == ExpressionKind::Call) {
      // A handle a static class method returns (`C#(virtual I)::get()`,
      // 8.10, 8.25.1).
      const auto selected = expression.text.rfind("::");
      if (selected != std::string::npos
          && !resolve_class_name(
                  std::string_view { expression.text }.substr(0, selected),
                  scope)
                  .empty()) {
        (void)resolve_expression(expression, scope);
      }
    }
    return expected;
  }
  if (const auto standard = visible_standard_class(expected, scope);
      expression.marker == support::ExpressionMarker::sv_new
      && (standard == support::SystemVerilogStandardClass::mailbox
          || standard == support::SystemVerilogStandardClass::semaphore)) {
    mark_expression(expression, support::ExpressionMarker::sv_sync_new,
        expected.spelling);
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
    // A method of a semaphore or mailbox (IEEE 1800-2023 15.3, 15.4)
    // called as a statement: the member call with the receiver first, its
    // method resolved here from the class's member list, so no later
    // reader splits the task name or resolves the receiver's name again.
    if (const auto standard = receiver_type
                ? visible_standard_class(*receiver_type, scope)
                : support::SystemVerilogStandardClass::none;
        standard == support::SystemVerilogStandardClass::mailbox
        || standard == support::SystemVerilogStandardClass::semaphore) {
      const auto name = std::string_view{statement.task_name}.substr(
          dot + 1U);
      Expression call{ExpressionKind::Call, "." + std::string{name}, {},
          statement.span};
      call.builtin_method = support::builtin_method_named(name);
      call.operands.push_back(Expression{ExpressionKind::Identifier,
          statement.task_name.substr(0, dot), {}, statement.span});
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
    if (receiver_type && receiver_type->systemverilog_container
        && receiver.kind == ExpressionKind::Call
        && receiver.marker == support::ExpressionMarker::sv_container_property
        && receiver.operands.size() == 1U) {
      const auto name = statement.task_name.substr(dot + 1U);
      if (name == "push_back") {
        mark_task(statement, support::ExpressionMarker::sv_container_push_back,
            receiver.marker_argument);
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
      const auto name = std::string_view{statement.task_name}.substr(
          dot + 1U);
      if (argumentless_statement_method(support::builtin_method_named(name))) {
        Expression call{ExpressionKind::Call, "." + std::string{name}, {},
            statement.span};
        call.builtin_method = support::builtin_method_named(name);
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
    // A call on a handle whose type is a class type parameter of the
    // enclosing class (`T inner; ... inner.set(x);`) calls a method of the
    // class the specialization binds the parameter to (IEEE 1800-2023 8.25).
    // The parameter's class is known only there, so the call keeps the
    // member-call form with the receiver first, and as a statement it
    // discards its result (13.4.1).
    if (receiver_type && !class_identity(*receiver_type)
        && scope.class_owner != nullptr && scope.method_body
        && !support::uvm_library(scope.class_owner->uvm_role)
        && !receiver_type->named_type.empty()
        // The innermost declaration of the name decides (6.18, 26.3).
        && class_type_parameter_binding(receiver_type->named_type, scope)
            != nullptr) {
      Expression call{ExpressionKind::Call,
          "." + statement.task_name.substr(dot + 1U), {}, statement.span};
      call.builtin_method = support::builtin_method_named(
          std::string_view{statement.task_name}.substr(dot + 1U));
      call.operands.push_back(std::move(receiver));
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
    if (receiver_type && class_identity(*receiver_type)) {
      const auto name = statement.task_name.substr(dot + 1U);
      // `obj.set_randstate(state)` (IEEE 1800-2017 18.13.5).
      if (name == "set_randstate" && statement.task_arguments.size() == 1U) {
        mark_task(statement,
            support::ExpressionMarker::sv_object_set_randstate);
        statement.task_arguments.insert(
            statement.task_arguments.begin(), std::move(receiver));
        statement.task_argument_names.insert(
            statement.task_argument_names.begin(), std::string{});
        resolve_expression(statement.task_arguments.back(), scope);
        return;
      }
      if (name == "srandom" && statement.task_arguments.size() == 1U) {
        mark_task(statement, support::ExpressionMarker::sv_object_srandom);
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
        call.builtin_method = support::builtin_method_named(name);
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
        mark_task(statement, support::ExpressionMarker::sv_static_task,
            match->method->canonical_identity);
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
        mark_task(statement,
            kind == SystemVerilogClassMethodKind::Constructor
                ? support::ExpressionMarker::sv_base_constructor
                : support::ExpressionMarker::sv_task,
            canonical);
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
        statement.task_name.substr(0, selected), scope,
        statement.systemverilog_task_class_selection.get());
    if (owner) {
      const auto name = statement.task_name.substr(selected + 2U);
      // A typedef's actuals (`uvm_config_int` names
      // `uvm_config_db#(uvm_bitstream_t)`) select the type they denote.
      const auto normalized_owner = [&] {
        auto normalized = *owner;
        normalize_selection_actuals(normalized, scope);
        return normalized;
      };
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
        // In a class, a call through a specialization keeps the
        // specialization only in the expression form (IEEE 1800-2023
        // 8.25.1).
        if (match && match->method && match->method->is_static
            && kind == SystemVerilogClassMethodKind::Function
            && (scope.class_owner == nullptr
                || selects_specialization(
                    std::string_view{statement.task_name}.substr(0, selected),
                    *owner))
            && selected_uvm_intrinsic(normalized_owner(), name).empty()
            && !support::uvm_library(match->method->uvm_role)) {
          // A static function called as a statement discards its result
          // (IEEE 1800-2017 13.4.1) and lowers as an expression statement;
          // native UVM methods keep their task form.
          Expression call{
              ExpressionKind::Call, statement.task_name, {}, statement.span};
          call.systemverilog_class_selection
              = statement.systemverilog_task_class_selection;
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
          const auto intrinsic = selected_uvm_intrinsic(normalized_owner(), name);
          // The UVM library's own methods run on its native services.
          const bool specialized = intrinsic.empty()
              && !support::uvm_library(match->method->uvm_role)
              && selects_specialization(
                  std::string_view{statement.task_name}.substr(0, selected),
                  *owner);
          mark_task(statement, support::ExpressionMarker::sv_static_task,
              intrinsic.empty() ? match->method->canonical_identity
                                : intrinsic);
          const auto method = specialize_selected_method(
              *match->method, *owner, scope);
          retain_task_profile(
              statement,
              method,
              owner->declaration,
              &owner->type);
          // `C#(4)::t();` or `typedef C#(4) T; T::t();` runs the method in
          // that specialization, on its static properties (IEEE 1800-2023
          // 8.25.1, 8.10): the statement keeps the selection as its value,
          // as the expression form of a static call keeps it.
          if (specialized) {
            auto selection = marked_expression(
                support::ExpressionMarker::sv_static_call_selection, {}, {},
                statement.span);
            selection.systemverilog_source_order
                = statement.systemverilog_source_order;
            selection.systemverilog_class_owner =
                std::make_shared<const Type>(owner->type);
            resolve_class_owner_constants(selection, scope, 0U);
            statement.value = std::move(selection);
          }
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
        mark_task(statement,
            match->method->is_static
                ? support::ExpressionMarker::sv_static_task
                : support::ExpressionMarker::sv_task,
            match->method->canonical_identity);
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
[[gnu::noinline]] std::unique_ptr<Scope> Resolver::declare_statement_scope(
    Statement& statement, const Scope& inherited_scope) {
  auto declared_scope = std::make_unique<Scope>(inherited_scope);
  for (const auto& import : statement.systemverilog_imports) {
    if (!import.name.empty()) declared_scope->imported_names.insert(import.name);
    declared_scope->imports.push_back(&import);
  }
  for (auto& alias : statement.type_aliases) {
    if (auto scoped = resolve_class_scoped_type(alias.type, *declared_scope)) {
      alias.type = std::move(*scoped);
    }
    declared_scope->aliases.insert_or_assign(alias.name, &alias.type);
  }
  for (auto& declaration : statement.declarations) {
    if (auto scoped = resolve_class_scoped_type(
            declaration.type, *declared_scope)) {
      declaration.type = std::move(*scoped);
    }
    // A class typedef a block import makes visible (IEEE 1800-2023 26.3)
    // names its class and specialization.
    if (!statement.systemverilog_imports.empty()
        && !declaration.type.named_type.empty()
        && !class_identity(declaration.type)
        && !declared_scope->aliases.contains(declaration.type.named_type)) {
      if (const auto* imported = imported_class_alias(
              declaration.type.named_type, *declared_scope)) {
        declaration.type.systemverilog_class_declaration
            = imported->systemverilog_class_declaration;
        declaration.type.systemverilog_class_name
            = imported->systemverilog_class_name;
        declaration.type.systemverilog_class_parameter_actuals
            = imported->systemverilog_class_parameter_actuals;
      }
    }
    if (declaration.initializer) {
      resolve_typed_expression(
          *declaration.initializer, *declared_scope, declaration.type);
    }
    declared_scope->objects.assign(declaration.name, declaration.type);
  }
  return declared_scope;
}
[[gnu::noinline]] bool Resolver::resolve_statement_head(
    Statement& statement,
    const Scope& scope,
    const std::optional<std::string>& return_class,
    const SystemVerilogScalarKind return_scalar) {
  // `$cast(dest, value);` discards the call's result (IEEE 1800-2017
  // 6.24.2); a class destination resolves to the checked class cast.
  if (statement.kind == StatementKind::TaskCall
      && statement.task_name == "$cast"
      && statement.task_arguments.size() == 2U) {
    auto call = marked_expression(
        support::ExpressionMarker::sv_dollar_cast, {},
        {std::move(statement.task_arguments[0]),
         std::move(statement.task_arguments[1])},
        statement.span);
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
      return false;
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
      mark_task(statement, name == "srandom"
              ? support::ExpressionMarker::sv_object_srandom
              : support::ExpressionMarker::sv_object_set_randstate);
      statement.task_arguments.clear();
      statement.task_argument_names.clear();
      statement.task_arguments.push_back(std::move(receiver));
      statement.task_arguments.push_back(
          std::move(statement.value.operands[1]));
      statement.task_argument_names.assign(2U, std::string{});
      resolve_expression(statement.task_arguments.back(), scope);
      statement.value = Expression{};
      return false;
    }
    if (identity
        && !find_methods(*identity, name, SystemVerilogClassMethodKind::Task)
                .empty()) {
      const auto match = select_method(
          *identity, name, SystemVerilogClassMethodKind::Task,
          statement.value.operands.size() - 1U, statement.span);
      if (!match || !match->method) {
        return false;
      }
      statement.kind = StatementKind::TaskCall;
      mark_task(statement, support::ExpressionMarker::sv_task,
          match->method->canonical_identity);
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
      return false;
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
         marked_expression(
             support::ExpressionMarker::sv_null, {}, {}, span)},
        span};
    resolve_expression(statement.condition, scope);
  }
  resolve_expression(statement.loop_initial, scope);
  resolve_expression(statement.loop_limit, scope);
  resolve_expression(statement.loop_update_target, scope);
  return true;
}
[[gnu::noinline]] void Resolver::resolve_statement_tail(
    Statement& statement, const Scope& scope) {
  for (auto& output : statement.output_values) {
    resolve_expression(output.value, scope);
  }
  // The argument expressions kept for the VPI (vpiArgument, 37.43) name
  // the same objects as the output values.
  for (auto& argument : statement.output_arguments) {
    resolve_expression(argument, scope);
  }
  // The descriptor of $fdisplay, $fwrite, $fflush and $fclose (IEEE
  // 1800-2023 21.3) names a class's property as any operand does.
  resolve_expression(statement.file_handle, scope);
  for (auto& sensitivity : statement.sensitivities) {
    resolve_member_sensitivity(sensitivity, scope);
    resolve_expression(sensitivity.expression, scope);
    if (sensitivity.guard) {
      resolve_expression(*sensitivity.guard, scope);
    }
  }
  if (statement.kind == StatementKind::Report) {
    for (auto& argument : statement.task_arguments) {
      resolve_expression(argument, scope);
    }
  }
  if (statement.kind == StatementKind::TaskCall) {
    resolve_task_call(statement, scope);
  }
}
void Resolver::resolve_statement(
    Statement& statement,
    const Scope& inherited_scope,
    const std::optional<std::string>& return_class,
    const SystemVerilogScalarKind return_scalar) {
  if (!fsim::support::stack_growth_detail::has_room()) {
      return fsim::support::with_stack(
          [&] { return resolve_statement(statement, inherited_scope, return_class, return_scalar); });
  }
  // Only a statement that declares objects needs its own scope copy. The
  // copy and the statement's own work stay out of this frame, which every
  // nested statement repeats (a long else-if chain nests a level per branch).
  std::unique_ptr<Scope> declared_scope;
  if (!statement.type_aliases.empty() || !statement.declarations.empty()) {
    declared_scope = declare_statement_scope(statement, inherited_scope);
  }
  const Scope& scope = declared_scope ? *declared_scope : inherited_scope;
  if (!resolve_statement_head(statement, scope, return_class, return_scalar)) {
    return;
  }
  for (auto& initialization : statement.loop_initializations) {
    resolve_statement(initialization, scope, return_class, return_scalar);
  }
  for (auto& update : statement.loop_updates) {
    resolve_statement(update, scope, return_class, return_scalar);
  }
  resolve_statement_tail(statement, scope);
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
  release_hidden_standard_classes(function.arguments, scope);
  release_hidden_standard_classes(function.variables, scope);
  function.return_type
      = release_hidden_standard_class(std::move(function.return_type), scope);
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
  release_hidden_standard_classes(task.arguments, scope);
  release_hidden_standard_classes(task.variables, scope);
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
  // A property of the class or a base hides an object of the same name in
  // an enclosing scope, such as the compilation unit (IEEE 1800-2017 8.5,
  // 8.13, 23.9).
  scope.objects.hide_if([&](const std::string& name) {
    return find_property(declaration.canonical_identity, name).property
        != nullptr;
  });
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
            || !unit.systemverilog_compilation_unit_package) {
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
    // A default argument is evaluated in the scope that declares the
    // method, the class (IEEE 1800-2023 13.5.3): a property or method it
    // names is the one of the object the method is called on, and the
    // method's own formals and locals are not visible to it.
    for (auto& argument : method.arguments) {
      if (argument.default_value) {
        resolve_typed_expression(
            *argument.default_value, method_scope, argument.type);
      }
    }
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

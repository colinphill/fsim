// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include "vhdl_array_boundary.hpp"


namespace fsim::elaboration {
using namespace elaboration_detail;

namespace {

template <typename Formal>
std::optional<std::vector<const frontend::Expression*>>
bind_actual_shape(
    const std::span<const Formal> formals,
    const std::span<const frontend::Expression> operands,
    const std::span<const std::string> names) {
  if (!names.empty() && names.size() != operands.size()) {
    return std::nullopt;
  }
  std::vector<const frontend::Expression*> result(formals.size());
  bool saw_named = false;
  std::size_t positional = 0;
  for (std::size_t index = 0; index < operands.size(); ++index) {
    const auto& name = names.empty() ? std::string{} : names[index];
    std::size_t formal_index = 0;
    if (name.empty()) {
      if (saw_named || positional >= formals.size()) {
        return std::nullopt;
      }
      formal_index = positional++;
    } else {
      saw_named = true;
      const auto found = std::ranges::find(formals, name, &Formal::name);
      if (found == formals.end()) {
        return std::nullopt;
      }
      formal_index = static_cast<std::size_t>(
          std::distance(formals.begin(), found));
    }
    if (result[formal_index] != nullptr || !operands[index].valid()) {
      return std::nullopt;
    }
    result[formal_index] = &operands[index];
  }
  for (std::size_t index = 0; index < formals.size(); ++index) {
    if (result[index] == nullptr) {
      if constexpr (requires { formals[index].default_value; }) {
        if (formals[index].default_value) {
          result[index] = &*formals[index].default_value;
        }
      }
      if (result[index] == nullptr) {
        return std::nullopt;
      }
    }
  }
  return result;
}

}  // namespace

bool Lowerer::vhdl_callable_type_matches(
    const frontend::Type& formal,
    const frontend::Type& actual) const {
  if (formal.domain != actual.domain
      || formal.is_signed != actual.is_signed) {
    return false;
  }
  const bool formal_nominal = !formal.nominal_type.empty();
  const bool actual_nominal = !actual.nominal_type.empty();
  if (formal_nominal || actual_nominal) {
    if (!formal_nominal || !actual_nominal
        || formal.nominal_type != actual.nominal_type) {
      return false;
    }
    return !formal.vhdl_array || !actual.vhdl_array
        || vhdl_array_shape_matches(
            *formal.vhdl_array, *actual.vhdl_array, true);
  }
  if (formal.systemverilog_container
      || actual.systemverilog_container) {
    return formal.systemverilog_container
        && actual.systemverilog_container
        && formal.spelling == actual.spelling;
  }
  const auto simple_name = [](const std::string_view spelling) {
    const auto separator = spelling.find_last_of('.');
    return spelling.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
  };
  const auto formal_name = simple_name(formal.spelling);
  const bool unconstrained_builtin_array =
      !formal.packed_range && !formal.vhdl_array
      && (formal_name == "bit_vector"
          || formal_name == "std_logic_vector"
          || formal_name == "std_ulogic_vector"
          || formal_name == "signed"
          || formal_name == "unsigned");
  if (unconstrained_builtin_array) {
    return formal_name == simple_name(actual.spelling);
  }
  const auto formal_width = formal.width();
  const auto actual_width = actual.width();
  return !formal_width || !actual_width
      || *formal_width == *actual_width;
}

bool Lowerer::vhdl_expression_matches_type(
    const Expression& expression,
    const frontend::Type& formal) const {
  if (formal.vhdl_access
      && expression.kind == ExpressionKind::Call
      && (expression.text == "@vhdl-null"
          || expression.text == "@vhdl-new"
          || expression.text == "@vhdl-new-qualified")) {
    return true;
  }
  if (const auto* enumeration =
          enumeration_expression_type(expression)) {
    return vhdl_callable_type_matches(formal, *enumeration);
  }
  if (expression.kind == ExpressionKind::Identifier) {
    if (const auto* actual = object_type(expression.text)) {
      return vhdl_callable_type_matches(formal, *actual);
    }
    if (!formal.enumeration_literals.empty()) {
      const auto separator = expression.text.find_last_of('.');
      const auto literal = separator == std::string::npos
          ? std::string_view{expression.text}
          : std::string_view{expression.text}.substr(separator + 1);
      return std::ranges::find(formal.enumeration_literals, literal)
          != formal.enumeration_literals.end();
    }
    if (expression.text == "true" || expression.text == "false") {
      return formal.domain == frontend::ValueDomain::Boolean;
    }
    return true;
  }
  if (expression.kind == ExpressionKind::IntegerLiteral) {
    return formal.domain == frontend::ValueDomain::Integer;
  }
  if (expression.kind == ExpressionKind::BooleanLiteral) {
    return formal.domain == frontend::ValueDomain::Boolean;
  }
  if (expression.kind == ExpressionKind::LogicLiteral) {
    if (!formal.enumeration_literals.empty()) {
      return std::ranges::find(
                 formal.enumeration_literals, expression.text)
          != formal.enumeration_literals.end();
    }
    return formal.domain == frontend::ValueDomain::Bit2
        || formal.domain == frontend::ValueDomain::Logic4
        || formal.domain == frontend::ValueDomain::Logic9;
  }
  if (expression.kind == ExpressionKind::StringLiteral) {
    return formal.domain == frontend::ValueDomain::String
        || formal.vhdl_array.has_value()
        || (formal.packed_range.has_value()
            && formal.packed_members.empty());
  }
  if (expression.kind == ExpressionKind::Aggregate) {
    return formal.vhdl_array.has_value()
        || !formal.packed_members.empty();
  }
  if (expression.kind == ExpressionKind::Call) {
    if (const auto actual = vhdl_expression_type(expression)) {
      return vhdl_callable_type_matches(formal, *actual);
    }
    if (expression.operands.size() == 1) {
      if (const auto* conversion =
              visible_type_mark(expression.text)) {
        return vhdl_callable_type_matches(formal, *conversion);
      }
      if (expression.text == "integer"
          || expression.text == "natural"
          || expression.text == "positive") {
        return formal.domain == frontend::ValueDomain::Integer;
      }
      if (expression.text == "boolean") {
        return formal.domain == frontend::ValueDomain::Boolean;
      }
      if (expression.text == "bit") {
        return formal.domain == frontend::ValueDomain::Bit2;
      }
    }
    const auto found = function_indices_.find(expression.text);
    if (found == function_indices_.end()) {
      return true;
    }
    return std::ranges::any_of(
        found->second,
        [&](const std::size_t index) {
          return vhdl_function_profile_matches(
              expression,
              *function_frames_[index].source,
              &formal);
        });
  }
  if (is_integer_expression(expression)) {
    return formal.domain == frontend::ValueDomain::Integer;
  }
  return true;
}

bool Lowerer::vhdl_function_profile_matches(
    const Expression& expression,
    const frontend::FunctionDeclaration& function,
    const frontend::Type* expected_type) const {
  if (expected_type != nullptr
      && !vhdl_callable_type_matches(
          function.return_type, *expected_type)) {
    return false;
  }
  const auto actuals = bind_actual_shape(
      std::span{function.arguments},
      std::span{expression.operands},
      std::span{expression.call_argument_names});
  if (!actuals) {
    return false;
  }
  for (std::size_t argument = 0;
       argument < function.arguments.size(); ++argument) {
    if (!vhdl_expression_matches_type(
            *(*actuals)[argument],
            function.arguments[argument].type)) {
      return false;
    }
  }
  return true;
}

Lowerer::CallableSelection Lowerer::select_function_overload(
    const Expression& expression,
    const frontend::Type* expected_type,
    const FunctionResultKind result_kind) {
  const auto found = function_indices_.find(expression.text);
  if (found == function_indices_.end()) {
    return {};
  }
  if (language_ != frontend::Language::Vhdl2008) {
    return {true, found->second.front()};
  }

  std::vector<std::size_t> matches;
  for (const auto index : found->second) {
    const auto& function = *function_frames_[index].source;
    const bool result_matches =
        result_kind == FunctionResultKind::String
            ? function.return_type.domain
                == frontend::ValueDomain::String
            : result_kind == FunctionResultKind::Container
                ? function.return_type.systemverilog_container.has_value()
                : function.return_type.domain
                        != frontend::ValueDomain::String
                    && !function.return_type.systemverilog_container;
    if (result_matches
        && vhdl_function_profile_matches(
            expression, function, expected_type)) {
      matches.push_back(index);
    }
  }
  if (matches.size() == 1) {
    return {true, matches.front()};
  }
  report(
      matches.empty()
          ? "FSIM-ELAB-VHOVER-002"
          : "FSIM-ELAB-VHOVER-001",
      matches.empty()
          ? "VHDL function call '" + expression.text
                + "' matches no visible overload"
          : "VHDL function call '" + expression.text
                + "' is ambiguous among visible overloads",
      expression.span);
  return {true, std::nullopt};
}

Lowerer::CallableSelection Lowerer::select_procedure_overload(
    const Statement& statement) {
  const auto found = procedure_indices_.find(statement.procedure_name);
  if (found == procedure_indices_.end()) {
    return {};
  }
  std::vector<frontend::Expression> operands;
  std::vector<std::string> names;
  operands.reserve(statement.procedure_arguments.size());
  names.reserve(statement.procedure_arguments.size());
  for (const auto& association : statement.procedure_arguments) {
    operands.push_back(association.value);
    names.push_back(association.formal.value_or(std::string{}));
  }
  std::vector<std::size_t> matches;
  for (const auto index : found->second) {
    const auto& procedure = *procedure_frames_[index].source;
    const auto actuals = bind_actual_shape(
        std::span{procedure.arguments},
        std::span{operands},
        std::span{names});
    if (!actuals) {
      continue;
    }
    bool compatible = true;
    for (std::size_t argument = 0;
         argument < procedure.arguments.size(); ++argument) {
      const auto& formal = procedure.arguments[argument];
      if (!vhdl_expression_matches_type(
              *(*actuals)[argument], formal.type)) {
        compatible = false;
        break;
      }
      const auto& actual = *(*actuals)[argument];
      const bool writable =
          actual.kind == ExpressionKind::Identifier
          || (actual.kind == ExpressionKind::Index
              && actual.operands.size() == 2)
          || (actual.kind == ExpressionKind::Slice
              && actual.operands.size() == 3);
      if (found->second.size() > 1
          && (formal.object_class
                  == frontend::InterfaceObjectClass::Variable
              || formal.direction
                  != frontend::PortDirection::Input)
          && !writable) {
        compatible = false;
        break;
      }
    }
    if (compatible) {
      matches.push_back(index);
    }
  }
  if (matches.size() == 1) {
    return {true, matches.front()};
  }
  report(
      matches.empty()
          ? "FSIM-ELAB-VHOVER-005"
          : "FSIM-ELAB-VHOVER-004",
      matches.empty()
          ? "VHDL procedure call '" + statement.procedure_name
                + "' matches no visible overload"
          : "VHDL procedure call '" + statement.procedure_name
                + "' is ambiguous among visible overloads",
      statement.span);
  return {true, std::nullopt};
}

}  // namespace fsim::elaboration

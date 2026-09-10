// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/constraint_solver.hpp"

#include <algorithm>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

using Operator = SystemVerilogConstraintExpressionOperator;
using Profile = SystemVerilogConstraintVariableProfile;

[[nodiscard]] bool unary_operator(const Operator operation) noexcept {
  return operation == Operator::UnaryPlus
      || operation == Operator::UnaryMinus
      || operation == Operator::BitwiseNot
      || operation == Operator::LogicalNot;
}

[[nodiscard]] bool binary_operator(const Operator operation) noexcept {
  return operation >= Operator::Add && operation <= Operator::LogicalOr;
}

[[nodiscard]] bool logical_result(const Operator operation) noexcept {
  return operation == Operator::LogicalNot
      || (operation >= Operator::Equal
          && operation <= Operator::LogicalOr);
}

[[nodiscard]] Profile logical_profile() {
  return {
      SystemVerilogConstraintDomainKind::BitVector,
      1,
      false,
      "bit",
      false};
}

[[nodiscard]] Profile common_profile(
    const Profile& left,
    const Profile& right) {
  Profile result;
  result.kind = left.kind == right.kind
      ? left.kind
      : SystemVerilogConstraintDomainKind::BitVector;
  result.width = std::max(left.width, right.width);
  result.signed_value = left.signed_value && right.signed_value;
  result.nominal_type = left.nominal_type == right.nominal_type
      ? left.nominal_type
      : "$constraint-expression";
  result.four_state = left.four_state || right.four_state;
  return result;
}

[[nodiscard]] bool has_unknown(const PackedLogic4& value) {
  for (std::size_t index = 0; index < value.width(); ++index) {
    const auto bit = value.get(index);
    if (bit == Logic4::x || bit == Logic4::z) return true;
  }
  return false;
}

[[nodiscard]] PackedLogic4 resized(
    const PackedLogic4& value,
    const std::size_t width,
    const bool sign_extend) {
  if (width == 0) {
    throw std::invalid_argument{
        "constraint expression width must not be zero"};
  }
  const auto extension = sign_extend && !value.empty()
      ? value.get(value.width() - 1U)
      : Logic4::zero;
  PackedLogic4 result(width, extension);
  for (std::size_t index = 0;
       index < std::min(width, value.width()); ++index) {
    result.set(index, value.get(index));
  }
  return result;
}

[[nodiscard]] PackedLogic4 negate_known(const PackedLogic4& value) {
  PackedLogic4 result(value.width(), Logic4::zero);
  bool carry = true;
  for (std::size_t index = 0; index < value.width(); ++index) {
    const bool inverted = value.get(index) != Logic4::one;
    result.set(index, inverted != carry ? Logic4::one : Logic4::zero);
    carry = inverted && carry;
  }
  return result;
}

[[nodiscard]] PackedLogic4 add_known(
    const PackedLogic4& left,
    const PackedLogic4& right) {
  PackedLogic4 result(left.width(), Logic4::zero);
  bool carry = false;
  for (std::size_t index = 0; index < left.width(); ++index) {
    const bool lhs = left.get(index) == Logic4::one;
    const bool rhs = right.get(index) == Logic4::one;
    result.set(index, (lhs != rhs) != carry
        ? Logic4::one : Logic4::zero);
    carry = (lhs && rhs) || (carry && (lhs || rhs));
  }
  return result;
}

[[nodiscard]] PackedLogic4 multiply_known(
    const PackedLogic4& left,
    const PackedLogic4& right) {
  PackedLogic4 result(left.width(), Logic4::zero);
  for (std::size_t rhs_bit = 0; rhs_bit < right.width(); ++rhs_bit) {
    if (right.get(rhs_bit) != Logic4::one) continue;
    PackedLogic4 shifted(left.width(), Logic4::zero);
    for (std::size_t bit = rhs_bit; bit < left.width(); ++bit) {
      shifted.set(bit, left.get(bit - rhs_bit));
    }
    result = add_known(result, shifted);
  }
  return result;
}

[[nodiscard]] int compare_unsigned(
    const PackedLogic4& left,
    const PackedLogic4& right) {
  for (std::size_t offset = 0; offset < left.width(); ++offset) {
    const auto index = left.width() - offset - 1U;
    if (left.get(index) == right.get(index)) continue;
    return left.get(index) == Logic4::one ? 1 : -1;
  }
  return 0;
}

[[nodiscard]] bool is_zero(const PackedLogic4& value) {
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (value.get(index) == Logic4::one) return false;
  }
  return true;
}

[[nodiscard]] bool is_one(const PackedLogic4& value) {
  if (value.get(0) != Logic4::one) return false;
  for (std::size_t index = 1; index < value.width(); ++index) {
    if (value.get(index) != Logic4::zero) return false;
  }
  return true;
}

[[nodiscard]] bool is_all_ones(const PackedLogic4& value) {
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (value.get(index) != Logic4::one) return false;
  }
  return true;
}

[[nodiscard]] PackedLogic4 one_value(const std::size_t width) {
  PackedLogic4 result(width, Logic4::zero);
  result.set(0, Logic4::one);
  return result;
}

[[nodiscard]] PackedLogic4 power_known(
    const PackedLogic4& base,
    const PackedLogic4& exponent,
    const bool signed_exponent) {
  const auto negative = signed_exponent
      && exponent.get(exponent.width() - 1U) == Logic4::one;
  if (negative) {
    if (is_zero(base)) return PackedLogic4(base.width(), Logic4::x);
    if (is_one(base)) return one_value(base.width());
    if (is_all_ones(base)) {
      return exponent.get(0) == Logic4::one
          ? base : one_value(base.width());
    }
    return PackedLogic4(base.width(), Logic4::zero);
  }
  auto result = PackedLogic4(base.width(), Logic4::zero);
  result.set(0, Logic4::one);
  auto factor = base;
  for (std::size_t bit = 0; bit < exponent.width(); ++bit) {
    if (exponent.get(bit) == Logic4::one) {
      result = multiply_known(result, factor);
    }
    if (bit + 1U < exponent.width()) {
      factor = multiply_known(factor, factor);
    }
  }
  return result;
}

[[nodiscard]] std::pair<PackedLogic4, PackedLogic4> divide_unsigned(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor) {
  if (is_zero(divisor)) {
    throw std::invalid_argument{"constraint expression divides by zero"};
  }
  PackedLogic4 quotient(dividend.width(), Logic4::zero);
  PackedLogic4 remainder(dividend.width(), Logic4::zero);
  for (std::size_t offset = 0; offset < dividend.width(); ++offset) {
    const auto bit = dividend.width() - offset - 1U;
    for (std::size_t index = remainder.width() - 1U;
         index > 0; --index) {
      remainder.set(index, remainder.get(index - 1U));
    }
    remainder.set(0, dividend.get(bit));
    if (compare_unsigned(remainder, divisor) >= 0) {
      remainder = add_known(remainder, negate_known(divisor));
      quotient.set(bit, Logic4::one);
    }
  }
  return {std::move(quotient), std::move(remainder)};
}

enum class Truth : std::uint8_t { False, True, Unknown };

[[nodiscard]] Truth truth(const PackedLogic4& value) {
  bool unknown = false;
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (value.get(index) == Logic4::one) return Truth::True;
    if (value.get(index) == Logic4::x
        || value.get(index) == Logic4::z) {
      unknown = true;
    }
  }
  return unknown ? Truth::Unknown : Truth::False;
}

[[nodiscard]] PackedLogic4 truth_value(const Truth value) {
  return PackedLogic4(
      1,
      value == Truth::True
          ? Logic4::one
          : value == Truth::False ? Logic4::zero : Logic4::x);
}

[[nodiscard]] PackedLogic4 bitwise(
    const Operator operation,
    const PackedLogic4& left,
    const PackedLogic4& right) {
  PackedLogic4 result(left.width(), Logic4::x);
  for (std::size_t index = 0; index < left.width(); ++index) {
    const auto lhs = left.get(index);
    const auto rhs = right.get(index);
    if (operation == Operator::BitwiseAnd) {
      result.set(index,
          lhs == Logic4::zero || rhs == Logic4::zero
              ? Logic4::zero
              : lhs == Logic4::one && rhs == Logic4::one
                  ? Logic4::one : Logic4::x);
    } else if (operation == Operator::BitwiseOr) {
      result.set(index,
          lhs == Logic4::one || rhs == Logic4::one
              ? Logic4::one
              : lhs == Logic4::zero && rhs == Logic4::zero
                  ? Logic4::zero : Logic4::x);
    } else {
      result.set(index,
          (lhs == Logic4::zero || lhs == Logic4::one)
              && (rhs == Logic4::zero || rhs == Logic4::one)
          ? (lhs == rhs ? Logic4::zero : Logic4::one)
          : Logic4::x);
    }
  }
  return result;
}

[[nodiscard]] PackedLogic4 comparison(
    const Operator operation,
    const PackedLogic4& left,
    const PackedLogic4& right,
    const bool signed_values) {
  if (operation == Operator::CaseEqual
      || operation == Operator::CaseNotEqual) {
    const auto equal = left == right;
    return truth_value((operation == Operator::CaseEqual) == equal
        ? Truth::True : Truth::False);
  }
  if (has_unknown(left) || has_unknown(right)) {
    return truth_value(Truth::Unknown);
  }
  auto order = compare_unsigned(left, right);
  if (signed_values) {
    const auto left_negative = left.get(left.width() - 1U) == Logic4::one;
    const auto right_negative = right.get(right.width() - 1U) == Logic4::one;
    if (left_negative != right_negative) order = left_negative ? -1 : 1;
  }
  bool result{};
  switch (operation) {
    case Operator::Equal: result = order == 0; break;
    case Operator::NotEqual: result = order != 0; break;
    case Operator::Less: result = order < 0; break;
    case Operator::LessEqual: result = order <= 0; break;
    case Operator::Greater: result = order > 0; break;
    case Operator::GreaterEqual: result = order >= 0; break;
    default: throw std::logic_error{"invalid constraint comparison"};
  }
  return truth_value(result ? Truth::True : Truth::False);
}

[[nodiscard]] PackedLogic4 arithmetic(
    const Operator operation,
    PackedLogic4 left,
    PackedLogic4 right,
    const bool signed_values) {
  if (has_unknown(left) || has_unknown(right)) {
    return PackedLogic4(left.width(), Logic4::x);
  }
  if (operation == Operator::Add) return add_known(left, right);
  if (operation == Operator::Subtract) {
    return add_known(left, negate_known(right));
  }
  if (operation == Operator::Multiply) return multiply_known(left, right);
  if (operation == Operator::Power) {
    return power_known(left, right, signed_values);
  }
  bool left_negative = false;
  bool right_negative = false;
  if (signed_values) {
    left_negative = left.get(left.width() - 1U) == Logic4::one;
    right_negative = right.get(right.width() - 1U) == Logic4::one;
    if (left_negative) left = negate_known(left);
    if (right_negative) right = negate_known(right);
  }
  auto [quotient, remainder] = divide_unsigned(left, right);
  if (left_negative != right_negative) quotient = negate_known(quotient);
  if (left_negative) remainder = negate_known(remainder);
  return operation == Operator::Divide ? quotient : remainder;
}

}  // namespace

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::append(
    SystemVerilogConstraintExpressionNode node) {
  const auto id = nodes_.size();
  nodes_.push_back(std::move(node));
  return id;
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::variable(
    const SystemVerilogConstraintVariableId variable,
    SystemVerilogConstraintVariableProfile profile) {
  if (profile.width == 0 || profile.nominal_type.empty()) {
    throw std::invalid_argument{
        "constraint expression variable requires an exact profile"};
  }
  SystemVerilogConstraintExpressionNode node;
  node.operation = Operator::Variable;
  node.profile = std::move(profile);
  node.variable = variable;
  return append(std::move(node));
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::constant(
    PackedLogic4 value,
    SystemVerilogConstraintVariableProfile profile) {
  if (profile.width == 0 || profile.width != value.width()
      || profile.nominal_type.empty() || value.is_logic9()) {
    throw std::invalid_argument{
        "constraint expression constant does not match its exact profile"};
  }
  SystemVerilogConstraintExpressionNode node;
  node.operation = Operator::Constant;
  node.profile = std::move(profile);
  node.constant = std::move(value);
  return append(std::move(node));
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::unary(
    const SystemVerilogConstraintExpressionOperator operation,
    const SystemVerilogConstraintExpressionId operand) {
  if (!unary_operator(operation) || operand >= nodes_.size()) {
    throw std::invalid_argument{"invalid unary constraint expression"};
  }
  SystemVerilogConstraintExpressionNode node;
  node.operation = operation;
  node.profile = logical_result(operation)
      ? logical_profile() : nodes_[operand].profile;
  node.operands = {operand};
  return append(std::move(node));
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::binary(
    const SystemVerilogConstraintExpressionOperator operation,
    const SystemVerilogConstraintExpressionId left,
    const SystemVerilogConstraintExpressionId right) {
  if (!binary_operator(operation)
      || left >= nodes_.size() || right >= nodes_.size()) {
    throw std::invalid_argument{"invalid binary constraint expression"};
  }
  SystemVerilogConstraintExpressionNode node;
  node.operation = operation;
  node.profile = logical_result(operation)
      ? logical_profile()
      : common_profile(nodes_[left].profile, nodes_[right].profile);
  node.operands = {left, right};
  return append(std::move(node));
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::conditional(
    const SystemVerilogConstraintExpressionId condition,
    const SystemVerilogConstraintExpressionId when_true,
    const SystemVerilogConstraintExpressionId when_false) {
  if (condition >= nodes_.size() || when_true >= nodes_.size()
      || when_false >= nodes_.size()) {
    throw std::invalid_argument{"invalid conditional constraint expression"};
  }
  SystemVerilogConstraintExpressionNode node;
  node.operation = Operator::Conditional;
  node.profile = common_profile(
      nodes_[when_true].profile, nodes_[when_false].profile);
  node.operands = {condition, when_true, when_false};
  return append(std::move(node));
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::implication(
    const SystemVerilogConstraintExpressionId antecedent,
    const SystemVerilogConstraintExpressionId consequent) {
  if (antecedent >= nodes_.size() || consequent >= nodes_.size()) {
    throw std::invalid_argument{"invalid constraint implication"};
  }
  const auto inverted = unary(Operator::LogicalNot, antecedent);
  return binary(Operator::LogicalOr, inverted, consequent);
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::conditional_constraint(
    const SystemVerilogConstraintExpressionId condition,
    const SystemVerilogConstraintExpressionId when_true,
    const std::optional<SystemVerilogConstraintExpressionId> when_false) {
  if (!when_false) return implication(condition, when_true);
  return conditional(condition, when_true, *when_false);
}

SystemVerilogConstraintExpressionId
SystemVerilogConstraintExpressionBuilder::conjunction(
    const std::span<const SystemVerilogConstraintExpressionId> predicates) {
  if (predicates.empty()) {
    return constant(PackedLogic4(1, Logic4::one), logical_profile());
  }
  if (std::ranges::any_of(
          predicates, [&](const auto predicate) {
            return predicate >= nodes_.size();
          })) {
    throw std::invalid_argument{
        "constraint conjunction references an invalid predicate"};
  }
  auto result = predicates.front();
  for (const auto predicate : predicates.subspan(1)) {
    result = binary(Operator::LogicalAnd, result, predicate);
  }
  return result;
}

SystemVerilogConstraintExpression
SystemVerilogConstraintExpressionBuilder::finish(
    const SystemVerilogConstraintExpressionId root) && {
  if (root >= nodes_.size()) {
    throw std::invalid_argument{"constraint expression root is invalid"};
  }
  return {std::move(nodes_), root};
}

PackedLogic4 evaluate_systemverilog_constraint_expression(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintAssignment& assignment) {
  if (expression.nodes.empty() || expression.root >= expression.nodes.size()) {
    throw std::invalid_argument{"constraint expression graph has no root"};
  }
  std::vector<PackedLogic4> values;
  values.reserve(expression.nodes.size());
  for (std::size_t id = 0; id < expression.nodes.size(); ++id) {
    const auto& node = expression.nodes[id];
    for (const auto operand : node.operands) {
      if (operand >= id) {
        throw std::invalid_argument{
            "constraint expression operand is not source ordered"};
      }
    }
    if (node.operation == Operator::Variable) {
      const auto& assigned_profile = assignment.profile(node.variable);
      if (assigned_profile.kind != node.profile.kind
          || assigned_profile.width != node.profile.width
          || assigned_profile.signed_value != node.profile.signed_value
          || assigned_profile.nominal_type != node.profile.nominal_type
          || assigned_profile.four_state != node.profile.four_state) {
        throw std::invalid_argument{
            "constraint expression variable profile does not match the solver"};
      }
      values.push_back(assignment.value(node.variable));
      continue;
    }
    if (node.operation == Operator::Constant) {
      if (node.constant.width() != node.profile.width) {
        throw std::invalid_argument{
            "constraint expression constant width is corrupt"};
      }
      values.push_back(node.constant);
      continue;
    }
    if (unary_operator(node.operation)) {
      if (node.operands.size() != 1) {
        throw std::invalid_argument{
            "unary constraint expression has invalid arity"};
      }
      const auto& operand = values[node.operands[0]];
      if (node.operation == Operator::UnaryPlus) {
        values.push_back(operand);
      } else if (node.operation == Operator::UnaryMinus) {
        values.push_back(has_unknown(operand)
            ? PackedLogic4(operand.width(), Logic4::x)
            : negate_known(operand));
      } else if (node.operation == Operator::LogicalNot) {
        const auto state = truth(operand);
        values.push_back(truth_value(
            state == Truth::True ? Truth::False
                : state == Truth::False ? Truth::True : Truth::Unknown));
      } else {
        PackedLogic4 result(operand.width(), Logic4::x);
        for (std::size_t index = 0; index < operand.width(); ++index) {
          result.set(index,
              operand.get(index) == Logic4::zero ? Logic4::one
                  : operand.get(index) == Logic4::one
                      ? Logic4::zero : Logic4::x);
        }
        values.push_back(std::move(result));
      }
      continue;
    }
    if (node.operation == Operator::Conditional) {
      if (node.operands.size() != 3) {
        throw std::invalid_argument{
            "conditional constraint expression has invalid arity"};
      }
      const auto condition = truth(values[node.operands[0]]);
      const auto& true_node = expression.nodes[node.operands[1]];
      const auto& false_node = expression.nodes[node.operands[2]];
      const auto when_true = resized(
          values[node.operands[1]], node.profile.width,
          true_node.profile.signed_value);
      const auto when_false = resized(
          values[node.operands[2]], node.profile.width,
          false_node.profile.signed_value);
      if (condition == Truth::True) {
        values.push_back(when_true);
      } else if (condition == Truth::False) {
        values.push_back(when_false);
      } else {
        PackedLogic4 result(node.profile.width, Logic4::x);
        for (std::size_t index = 0; index < result.width(); ++index) {
          if (when_true.get(index) == when_false.get(index)) {
            result.set(index, when_true.get(index));
          }
        }
        values.push_back(std::move(result));
      }
      continue;
    }
    if (!binary_operator(node.operation) || node.operands.size() != 2) {
      throw std::invalid_argument{
          "binary constraint expression has invalid operation or arity"};
    }
    const auto& left_node = expression.nodes[node.operands[0]];
    const auto& right_node = expression.nodes[node.operands[1]];
    const auto operand_width = std::max(
        left_node.profile.width, right_node.profile.width);
    const auto signed_values = left_node.profile.signed_value
        && right_node.profile.signed_value;
    auto left = resized(
        values[node.operands[0]], operand_width,
        left_node.profile.signed_value);
    auto right = resized(
        values[node.operands[1]], operand_width,
        right_node.profile.signed_value);
    if (node.operation >= Operator::Add
        && node.operation <= Operator::Modulo) {
      values.push_back(arithmetic(
          node.operation, std::move(left), std::move(right), signed_values));
    } else if (node.operation >= Operator::BitwiseAnd
               && node.operation <= Operator::BitwiseXor) {
      values.push_back(bitwise(node.operation, left, right));
    } else if (node.operation >= Operator::Equal
               && node.operation <= Operator::GreaterEqual) {
      values.push_back(comparison(
          node.operation, left, right, signed_values));
    } else {
      const auto lhs = truth(left);
      const auto rhs = truth(right);
      Truth result = Truth::Unknown;
      if (node.operation == Operator::LogicalAnd) {
        if (lhs == Truth::False || rhs == Truth::False) {
          result = Truth::False;
        } else if (lhs == Truth::True && rhs == Truth::True) {
          result = Truth::True;
        }
      } else {
        if (lhs == Truth::True || rhs == Truth::True) {
          result = Truth::True;
        } else if (lhs == Truth::False && rhs == Truth::False) {
          result = Truth::False;
        }
      }
      values.push_back(truth_value(result));
    }
  }
  return values[expression.root];
}

SystemVerilogConstraintClause systemverilog_constraint_expression_clause(
    std::string canonical_identity,
    std::vector<SystemVerilogConstraintVariableId> variables,
    SystemVerilogConstraintExpression expression) {
  std::set<SystemVerilogConstraintVariableId> declared(
      variables.begin(), variables.end());
  for (const auto& node : expression.nodes) {
    if (node.operation == Operator::Variable
        && !declared.contains(node.variable)) {
      throw std::invalid_argument{
          "constraint expression dependency list omits a variable"};
    }
  }
  SystemVerilogConstraintClause clause;
  clause.canonical_identity = std::move(canonical_identity);
  clause.variables = std::move(variables);
  clause.evaluate = [expression = std::move(expression),
                     dependencies = clause.variables](
      const SystemVerilogConstraintAssignment& assignment) {
    for (const auto variable : dependencies) {
      if (!assignment.assigned(variable)) {
        return SystemVerilogConstraintClauseState::Undetermined;
      }
    }
    const auto result = evaluate_systemverilog_constraint_expression(
        expression, assignment);
    if (result.width() != 1) {
      throw std::invalid_argument{
          "constraint expression root is not a scalar predicate"};
    }
    if (result.get(0) == Logic4::x || result.get(0) == Logic4::z) {
      throw std::invalid_argument{
          "constraint expression predicate has an illegal four-state result"};
    }
    return result.get(0) == Logic4::one
        ? SystemVerilogConstraintClauseState::Satisfied
        : SystemVerilogConstraintClauseState::Violated;
  };
  return clause;
}

namespace {

    using Template = SystemVerilogConstraintTemplate;
    using TemplateKind = SystemVerilogConstraintTemplateKind;

    [[nodiscard]] std::optional<Operator> template_unary_operator(
        const std::string_view spelling) noexcept
    {
        if (spelling == "+")
            return Operator::UnaryPlus;
        if (spelling == "-")
            return Operator::UnaryMinus;
        if (spelling == "~")
            return Operator::BitwiseNot;
        if (spelling == "!")
            return Operator::LogicalNot;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Operator> template_binary_operator(
        const std::string_view spelling) noexcept
    {
        if (spelling == "+")
            return Operator::Add;
        if (spelling == "-")
            return Operator::Subtract;
        if (spelling == "*")
            return Operator::Multiply;
        if (spelling == "**")
            return Operator::Power;
        if (spelling == "/")
            return Operator::Divide;
        if (spelling == "%")
            return Operator::Modulo;
        if (spelling == "&")
            return Operator::BitwiseAnd;
        if (spelling == "|")
            return Operator::BitwiseOr;
        if (spelling == "^")
            return Operator::BitwiseXor;
        if (spelling == "==")
            return Operator::Equal;
        if (spelling == "!=")
            return Operator::NotEqual;
        if (spelling == "===")
            return Operator::CaseEqual;
        if (spelling == "!==")
            return Operator::CaseNotEqual;
        if (spelling == "<")
            return Operator::Less;
        if (spelling == "<=")
            return Operator::LessEqual;
        if (spelling == ">")
            return Operator::Greater;
        if (spelling == ">=")
            return Operator::GreaterEqual;
        if (spelling == "&&")
            return Operator::LogicalAnd;
        if (spelling == "||")
            return Operator::LogicalOr;
        return std::nullopt;
    }

    [[nodiscard]] bool variable_suffix(
        const std::string_view canonical,
        const std::string_view name) noexcept
    {
        if (canonical == name)
            return true;
        if (canonical.size() <= name.size())
            return false;
        const auto prefix = canonical.substr(0, canonical.size() - name.size());
        return (prefix.ends_with("::") || prefix.ends_with('.'))
            && canonical.ends_with(name);
    }

    [[nodiscard]] SystemVerilogConstraintVariableId find_template_variable(
        const SystemVerilogConstraintVariables& variables,
        std::string_view name)
    {
        if (name.starts_with("this."))
            name.remove_prefix(5U);
        if (const auto exact = variables.find(std::string { name });
            exact != variables.end()) {
            return exact->second;
        }
        std::optional<SystemVerilogConstraintVariableId> result;
        for (const auto& [canonical, variable] : variables) {
            if (!variable_suffix(canonical, name))
                continue;
            if (result && *result != variable) {
                throw std::invalid_argument {
                    "inline constraint name '" + std::string { name }
                    + "' is ambiguous"
                };
            }
            result = variable;
        }
        if (!result) {
            throw std::invalid_argument {
                "inline constraint name '" + std::string { name }
                + "' has no solver variable"
            };
        }
        return *result;
    }

    [[nodiscard]] std::uint64_t template_unsigned_word(
        const Template& expression,
        const std::string_view role)
    {
        if (expression.kind != TemplateKind::Constant
            || expression.constant.empty()
            || expression.constant.is_logic9()) {
            throw std::invalid_argument {
                std::string { role } + " must be a known packed constant"
            };
        }
        if (expression.profile.signed_value
            && expression.constant.get(expression.constant.width() - 1U)
                == Logic4::one) {
            throw std::invalid_argument {
                std::string { role } + " cannot be negative"
            };
        }
        std::uint64_t result { };
        for (std::size_t bit = 0; bit < expression.constant.width(); ++bit) {
            const auto value = expression.constant.get(bit);
            if (value == Logic4::x || value == Logic4::z) {
                throw std::invalid_argument {
                    std::string { role } + " cannot contain X or Z"
                };
            }
            if (value == Logic4::zero)
                continue;
            if (bit >= 64U) {
                throw std::overflow_error {
                    std::string { role } + " exceeds the distribution-weight ABI"
                };
            }
            result |= std::uint64_t { 1 } << bit;
        }
        return result;
    }

    class InlineTemplateLowerer final {
    public:
        InlineTemplateLowerer(
            const SystemVerilogConstraintSolver& solver,
            const SystemVerilogConstraintVariables& variables)
            : solver_(solver)
            , variables_(variables)
        {
        }

        [[nodiscard]] std::optional<SystemVerilogConstraintExpressionId> lower(
            const Template& expression)
        {
            if (expression.kind == TemplateKind::Name) {
                const auto variable = find_template_variable(
                    variables_, expression.text);
                if (variable >= solver_.variables().size()) {
                    throw std::invalid_argument {
                        "inline constraint variable id is outside the solver"
                    };
                }
                if (std::ranges::find(dependencies_, variable)
                    == dependencies_.end()) {
                    dependencies_.push_back(variable);
                }
                return builder_.variable(
                    variable, solver_.variables()[variable].profile);
            }
            if (expression.kind == TemplateKind::Constant) {
                if (expression.constant.empty()
                    || expression.profile.width != expression.constant.width()) {
                    throw std::invalid_argument {
                        "inline constraint constant has inconsistent metadata"
                    };
                }
                return builder_.constant(expression.constant, expression.profile);
            }
            if (expression.kind == TemplateKind::Soft
                && expression.operands.size() == 1U) {
                return lower(expression.operands.front());
            }
            if (expression.kind == TemplateKind::InsideSet
                && expression.operands.size() >= 2U) {
                const auto subject = lower(expression.operands.front());
                if (!subject)
                    return std::nullopt;
                std::optional<SystemVerilogConstraintExpressionId> membership;
                for (std::size_t index = 1; index < expression.operands.size(); ++index) {
                    const auto& item = expression.operands[index];
                    std::optional<SystemVerilogConstraintExpressionId> selected;
                    if (item.kind == TemplateKind::InsideRange
                        && item.operands.size() == 2U) {
                        const auto low = lower(item.operands[0]);
                        const auto high = lower(item.operands[1]);
                        if (!low || !high)
                            return std::nullopt;
                        const auto above_low = builder_.binary(
                            Operator::GreaterEqual, *subject, *low);
                        const auto below_high = builder_.binary(
                            Operator::LessEqual, *subject, *high);
                        selected = builder_.binary(
                            Operator::LogicalAnd, above_low, below_high);
                    } else {
                        const auto choice = lower(item);
                        if (!choice)
                            return std::nullopt;
                        selected = builder_.binary(
                            Operator::Equal, *subject, *choice);
                    }
                    membership = membership
                        ? std::optional { builder_.binary(
                              Operator::LogicalOr, *membership, *selected) }
                        : selected;
                }
                return membership;
            }
            if (expression.kind == TemplateKind::Unique) {
                std::vector<SystemVerilogConstraintExpressionId> values;
                values.reserve(expression.operands.size());
                for (const auto& item : expression.operands) {
                    if (item.kind != TemplateKind::Name) {
                        throw std::invalid_argument {
                            "inline unique constraint items must be named variables"
                        };
                    }
                    const auto value = lower(item);
                    if (!value)
                        return std::nullopt;
                    values.push_back(*value);
                }
                if (values.size() < 2U) {
                    return builder_.constant(
                        PackedLogic4::from_aval_bval(1, 1, 0),
                        SystemVerilogConstraintVariableProfile {
                            SystemVerilogConstraintDomainKind::BitVector,
                            1, false, "$unique-result", false });
                }
                std::vector<SystemVerilogConstraintExpressionId> comparisons;
                for (std::size_t left = 0; left < values.size(); ++left) {
                    for (std::size_t right = left + 1U;
                         right < values.size(); ++right) {
                        comparisons.push_back(builder_.binary(
                            Operator::NotEqual, values[left], values[right]));
                    }
                }
                return builder_.conjunction(comparisons);
            }

            std::vector<SystemVerilogConstraintExpressionId> operands;
            operands.reserve(expression.operands.size());
            for (const auto& operand : expression.operands) {
                const auto retained = lower(operand);
                if (!retained)
                    return std::nullopt;
                operands.push_back(*retained);
            }
            if (expression.kind == TemplateKind::Unary
                && operands.size() == 1U) {
                const auto operation = template_unary_operator(expression.text);
                if (operation)
                    return builder_.unary(*operation, operands.front());
            }
            if (expression.kind == TemplateKind::Binary
                && operands.size() == 2U) {
                const auto operation = template_binary_operator(expression.text);
                if (operation) {
                    return builder_.binary(*operation, operands[0], operands[1]);
                }
            }
            if (expression.kind == TemplateKind::Conditional
                && operands.size() == 3U) {
                return builder_.conditional(operands[0], operands[1], operands[2]);
            }
            if (expression.kind == TemplateKind::Block) {
                return builder_.conjunction(operands);
            }
            if (expression.kind == TemplateKind::Implication
                && operands.size() == 2U) {
                return builder_.implication(operands[0], operands[1]);
            }
            if (expression.kind == TemplateKind::ConditionalConstraint
                && (operands.size() == 2U || operands.size() == 3U)) {
                return builder_.conditional_constraint(
                    operands[0], operands[1],
                    operands.size() == 3U
                        ? std::optional { operands[2] }
                        : std::nullopt);
            }
            throw std::invalid_argument {
                "unsupported inline constraint operator '" + expression.text + "'"
            };
        }

        [[nodiscard]] SystemVerilogConstraintExpression finish(
            const SystemVerilogConstraintExpressionId root) &&
        {
            return std::move(builder_).finish(root);
        }

        [[nodiscard]] std::vector<SystemVerilogConstraintVariableId>
        dependencies() &&
        {
            return std::move(dependencies_);
        }

    private:
        const SystemVerilogConstraintSolver& solver_;
        const SystemVerilogConstraintVariables& variables_;
        SystemVerilogConstraintExpressionBuilder builder_;
        std::vector<SystemVerilogConstraintVariableId> dependencies_;
    };

    [[nodiscard]] SystemVerilogConstraintDistribution
    template_distribution(
        const Template& expression,
        const SystemVerilogConstraintSolver& solver,
        const SystemVerilogConstraintVariables& variables,
        const std::string_view identity)
    {
        if (expression.kind != TemplateKind::Distribution
            || expression.operands.size() < 2U
            || expression.operands.front().kind != TemplateKind::Name) {
            throw std::invalid_argument {
                "inline distribution requires a named variable and entries"
            };
        }
        SystemVerilogConstraintDistribution result;
        result.canonical_identity = std::string { identity };
        result.variable = find_template_variable(
            variables, expression.operands.front().text);
        if (result.variable >= solver.variables().size()) {
            throw std::invalid_argument {
                "inline distribution variable id is outside the solver"
            };
        }
        const auto& profile = solver.variables()[result.variable].profile;
        const auto normalize_choice = [&](const Template& choice) {
            if (choice.kind != TemplateKind::Constant || choice.constant.empty()) {
                throw std::invalid_argument {
                    "inline distribution choice is not a packed constant"
                };
            }
            return resized(
                choice.constant, profile.width, choice.profile.signed_value);
        };
        for (const auto& item : expression.operands | std::views::drop(1)) {
            if (item.kind != TemplateKind::DistributionItem
                || item.operands.size() != 2U) {
                throw std::invalid_argument {
                    "inline distribution has an invalid weighted item"
                };
            }
            const auto& choice = item.operands[0];
            const auto& weight = item.operands[1];
            SystemVerilogConstraintDistributionEntry entry;
            if (choice.kind == TemplateKind::InsideRange
                && choice.operands.size() == 2U
                && choice.operands[0].kind == TemplateKind::Constant
                && choice.operands[1].kind == TemplateKind::Constant) {
                entry.low = normalize_choice(choice.operands[0]);
                entry.high = normalize_choice(choice.operands[1]);
            } else if (choice.kind == TemplateKind::Constant) {
                entry.low = normalize_choice(choice);
                entry.high = entry.low;
            } else {
                throw std::invalid_argument {
                    "inline distribution choices must be constant values or ranges"
                };
            }
            entry.weight = template_unsigned_word(weight, "distribution weight");
            entry.weight_kind = item.text == "@dist-:/"
                ? SystemVerilogConstraintDistributionWeight::AcrossRange
                : SystemVerilogConstraintDistributionWeight::PerValue;
            result.entries.push_back(std::move(entry));
        }
        return result;
    }

    [[nodiscard]] std::vector<SystemVerilogConstraintVariableId>
    template_solve_list(
        const Template& expression,
        const SystemVerilogConstraintVariables& variables)
    {
        if (expression.kind != TemplateKind::SolveList) {
            throw std::invalid_argument {
                "inline solve-before operand is not a solve list"
            };
        }
        std::vector<SystemVerilogConstraintVariableId> result;
        result.reserve(expression.operands.size());
        for (const auto& operand : expression.operands) {
            if (operand.kind != TemplateKind::Name) {
                throw std::invalid_argument {
                    "inline solve-before list requires named variables"
                };
            }
            result.push_back(find_template_variable(variables, operand.text));
        }
        return result;
    }

} // namespace

void configure_systemverilog_inline_constraints(
    SystemVerilogConstraintSolver& solver,
    const SystemVerilogConstraintVariables& variables,
    const std::span<const SystemVerilogConstraintTemplate> expressions,
    const std::string_view identity_prefix)
{
    if (identity_prefix.empty()) {
        throw std::invalid_argument {
            "inline constraint identity prefix must not be empty"
        };
    }
    for (std::size_t index = 0; index < expressions.size(); ++index) {
        const auto& expression = expressions[index];
        const auto identity = std::string { identity_prefix } + "::"
            + std::to_string(index);
        if (expression.kind == TemplateKind::Distribution) {
            solver.add_distribution(template_distribution(
                expression, solver, variables, identity));
            continue;
        }
        if (expression.kind == TemplateKind::SolveBefore) {
            if (expression.operands.size() != 2U) {
                throw std::invalid_argument {
                    "inline solve-before requires two variable lists"
                };
            }
            const auto earlier = template_solve_list(
                expression.operands[0], variables);
            const auto later = template_solve_list(
                expression.operands[1], variables);
            for (const auto left : earlier) {
                for (const auto right : later) {
                    solver.add_solve_before(left, right);
                }
            }
            continue;
        }
        const auto soft = expression.kind == TemplateKind::Soft;
        InlineTemplateLowerer lowerer { solver, variables };
        const auto root = lowerer.lower(expression);
        if (!root) {
            throw std::invalid_argument {
                "inline constraint could not be lowered"
            };
        }
        auto retained_expression = std::move(lowerer).finish(*root);
        auto dependencies = std::move(lowerer).dependencies();
        auto clause = systemverilog_constraint_expression_clause(
            identity, std::move(dependencies), std::move(retained_expression));
        clause.soft = soft;
        solver.add_clause(std::move(clause));
    }
}

} // namespace fsim::runtime

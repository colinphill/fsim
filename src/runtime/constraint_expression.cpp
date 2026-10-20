// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/constraint_solver.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <charconv>
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

// A value of at most 64 bits as its aval/bval words (0=00, 1=10, X=11,
// Z=01), so the solver's hot evaluations run on machine words.
struct Word {
  std::uint64_t aval{};
  std::uint64_t bval{};
  std::uint64_t mask{};
  [[nodiscard]] std::uint64_t ones() const noexcept {
    return aval & ~bval & mask;
  }
  [[nodiscard]] bool known() const noexcept { return (bval & mask) == 0U; }
};

[[nodiscard]] std::optional<Word> word_of(const PackedLogic4& value) {
  const auto width = value.width();
  if (width == 0U || width > 64U || value.is_logic9()) return std::nullopt;
  const auto aval = value.aval_words();
  const auto bval = value.bval_words();
  const auto mask = width == 64U
      ? ~std::uint64_t{} : (std::uint64_t{1} << width) - 1U;
  return Word{aval.empty() ? 0U : aval[0] & mask,
      bval.empty() ? 0U : bval[0] & mask, mask};
}

[[nodiscard]] PackedLogic4 known_word(
    const std::size_t width, const std::uint64_t value) {
  const auto mask = width == 64U
      ? ~std::uint64_t{} : (std::uint64_t{1} << width) - 1U;
  return PackedLogic4::from_aval_bval(width, value & mask, 0U);
}

[[nodiscard]] bool has_unknown(const PackedLogic4& value) {
  if (const auto word = word_of(value)) return !word->known();
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
  if (const auto word = word_of(value); word && width <= 64U) {
    const auto source_width = value.width();
    const auto target_mask = width == 64U
        ? ~std::uint64_t{} : (std::uint64_t{1} << width) - 1U;
    auto aval = word->aval;
    auto bval = word->bval;
    if (width > source_width && sign_extend) {
      const auto high = ~word->mask & target_mask;
      if (((aval >> (source_width - 1U)) & 1U) != 0U) aval |= high;
      if (((bval >> (source_width - 1U)) & 1U) != 0U) bval |= high;
    }
    return PackedLogic4::from_aval_bval(
        width, aval & target_mask, bval & target_mask);
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
  if (const auto word = word_of(value)) {
    return known_word(value.width(), ~word->ones() + 1U);
  }
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
  if (const auto lhs = word_of(left), rhs = word_of(right);
      lhs && rhs && left.width() == right.width()) {
    return known_word(left.width(), lhs->ones() + rhs->ones());
  }
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
  if (const auto lhs = word_of(left), rhs = word_of(right); lhs && rhs) {
    return known_word(left.width(), lhs->ones() * rhs->ones());
  }
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
  if (const auto lhs = word_of(left), rhs = word_of(right);
      lhs && rhs && lhs->known() && rhs->known()
      && left.width() == right.width()) {
    return lhs->aval == rhs->aval ? 0 : lhs->aval > rhs->aval ? 1 : -1;
  }
  for (std::size_t offset = 0; offset < left.width(); ++offset) {
    const auto index = left.width() - offset - 1U;
    if (left.get(index) == right.get(index)) continue;
    return left.get(index) == Logic4::one ? 1 : -1;
  }
  return 0;
}

[[nodiscard]] bool is_zero(const PackedLogic4& value) {
  if (const auto word = word_of(value)) return word->ones() == 0U;
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (value.get(index) == Logic4::one) return false;
  }
  return true;
}

[[nodiscard]] bool is_one(const PackedLogic4& value) {
  if (const auto word = word_of(value)) {
    return word->known() && word->aval == 1U;
  }
  if (value.get(0) != Logic4::one) return false;
  for (std::size_t index = 1; index < value.width(); ++index) {
    if (value.get(index) != Logic4::zero) return false;
  }
  return true;
}

[[nodiscard]] bool is_all_ones(const PackedLogic4& value) {
  if (const auto word = word_of(value)) return word->ones() == word->mask;
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
  if (const auto lhs = word_of(dividend), rhs = word_of(divisor);
      lhs && rhs && dividend.width() == divisor.width()) {
    return {known_word(dividend.width(), lhs->ones() / rhs->ones()),
        known_word(dividend.width(), lhs->ones() % rhs->ones())};
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
  if (const auto word = word_of(value)) {
    return word->ones() != 0U ? Truth::True
        : word->known() ? Truth::False : Truth::Unknown;
  }
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
  if (const auto lhs = word_of(left), rhs = word_of(right);
      lhs && rhs && left.width() == right.width()) {
    const auto mask = lhs->mask;
    const auto left_one = lhs->ones();
    const auto right_one = rhs->ones();
    const auto left_zero = ~lhs->aval & ~lhs->bval & mask;
    const auto right_zero = ~rhs->aval & ~rhs->bval & mask;
    std::uint64_t one{};
    std::uint64_t zero{};
    if (operation == Operator::BitwiseAnd) {
      one = left_one & right_one;
      zero = left_zero | right_zero;
    } else if (operation == Operator::BitwiseOr) {
      one = left_one | right_one;
      zero = left_zero & right_zero;
    } else {
      const auto known = (left_one | left_zero) & (right_one | right_zero);
      one = known & (left_one ^ right_one);
      zero = known & ~one;
    }
    const auto unknown = mask & ~(one | zero);
    return PackedLogic4::from_aval_bval(
        left.width(), one | unknown, unknown);
  }
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
    const bool signed_values,
    const bool signed_exponent) {
  if (has_unknown(left) || has_unknown(right)) {
    return PackedLogic4(left.width(), Logic4::x);
  }
  // Division or modulus by zero yields x (IEEE 1800-2017 11.4.2).
  if ((operation == Operator::Divide || operation == Operator::Modulo)
      && is_zero(right)) {
    return PackedLogic4(left.width(), Logic4::x);
  }
  if (operation == Operator::Add) return add_known(left, right);
  if (operation == Operator::Subtract) {
    return add_known(left, negate_known(right));
  }
  if (operation == Operator::Multiply) return multiply_known(left, right);
  if (operation == Operator::Power) {
    // The exponent is self-determined (11.4.3): its own signedness
    // decides whether it is negative.
    return power_known(left, right, signed_exponent);
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
SystemVerilogConstraintExpressionBuilder::resize(
    const SystemVerilogConstraintExpressionId operand,
    SystemVerilogConstraintVariableProfile profile) {
  if (operand >= nodes_.size() || profile.width == 0U) {
    throw std::invalid_argument{"invalid constraint expression conversion"};
  }
  SystemVerilogConstraintExpressionNode node;
  node.operation = Operator::UnaryPlus;
  node.profile = std::move(profile);
  node.operands = {operand};
  return append(std::move(node));
}

const SystemVerilogConstraintVariableProfile&
SystemVerilogConstraintExpressionBuilder::profile(
    const SystemVerilogConstraintExpressionId expression) const {
  if (expression >= nodes_.size()) {
    throw std::invalid_argument{"constraint expression node does not exist"};
  }
  return nodes_[expression].profile;
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
      // A non-random (state) property carries only its storage profile; the
      // constraint reads its value as the property's declared type (IEEE
      // 1800-2017 18.3).
      const bool state_variable
          = assigned_profile.nominal_type == "packed property"
          || assigned_profile.nominal_type == "integer";
      if (assigned_profile.width != node.profile.width
          || (!state_variable
              && (assigned_profile.kind != node.profile.kind
                  || assigned_profile.signed_value
                      != node.profile.signed_value
                  || assigned_profile.nominal_type
                      != node.profile.nominal_type
                  || assigned_profile.four_state
                      != node.profile.four_state))) {
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
        // Unary plus keeps its operand; a conversion resizes it.
        values.push_back(operand.width() == node.profile.width
            ? operand
            : resized(operand, node.profile.width,
                  expression.nodes[node.operands[0]].profile.signed_value));
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
          node.operation, std::move(left), std::move(right), signed_values,
          right_node.profile.signed_value));
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
  // `-4` and profile conversions of constants are constants, so
  // comparisons with them bound their variable's domain (18.5).
  for (bool folded = true; folded;) {
    folded = false;
    for (auto& node : expression.nodes) {
      if ((node.operation != Operator::UnaryPlus
              && node.operation != Operator::UnaryMinus)
          || node.operands.size() != 1U) {
        continue;
      }
      const auto& operand = expression.nodes[node.operands.front()];
      const auto from = operand.constant.width();
      const auto to = node.profile.width;
      if (operand.operation != Operator::Constant || from == 0U
          || from > 64U || to == 0U || to > 64U
          || operand.constant.is_logic9()
          || operand.constant.low_word().bval != 0U) {
        continue;
      }
      auto bits = operand.constant.low_word().aval;
      if (from < 64U) {
        bits &= (std::uint64_t{1} << from) - 1U;
        if (operand.profile.signed_value
            && ((bits >> (from - 1U)) & 1U) != 0U) {
          bits |= ~((std::uint64_t{1} << from) - 1U);
        }
      }
      if (node.operation == Operator::UnaryMinus) bits = ~bits + 1U;
      if (to < 64U) bits &= (std::uint64_t{1} << to) - 1U;
      node.constant = PackedLogic4::from_aval_bval(to, bits, 0U);
      node.operation = Operator::Constant;
      node.operands.clear();
      folded = true;
    }
  }
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
  clause.expression = std::make_shared<const SystemVerilogConstraintExpression>(
      std::move(expression));
  clause.evaluate = [expression = clause.expression,
                     dependencies = clause.variables](
      const SystemVerilogConstraintAssignment& assignment) {
    for (const auto variable : dependencies) {
      if (!assignment.assigned(variable)) {
        return SystemVerilogConstraintClauseState::Undetermined;
      }
    }
    const auto result = evaluate_systemverilog_constraint_expression(
        *expression, assignment);
    if (result.width() != 1) {
      throw std::invalid_argument{
          "constraint expression root is not a scalar predicate"};
    }
    // An unknown result (`0 ** -1`, division by zero) is not true, so the
    // candidate does not satisfy the constraint (IEEE 1800-2017 18.5,
    // 11.4.5, 11.4.2).
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
        std::string_view canonical,
        std::string_view name) noexcept
    {
        if (canonical == name)
            return true;
        // `h.m` names the variable whose identity is the path of canonical
        // property identities `C::h.D::m` (IEEE 1800-2017 18.5.9).
        if (name.find('.') != std::string_view::npos) {
            while (true) {
                const auto name_dot = name.find('.');
                const auto canonical_dot = canonical.find('.');
                if ((name_dot == std::string_view::npos)
                    != (canonical_dot == std::string_view::npos)) {
                    return false;
                }
                const auto segment = name.substr(0U, name_dot);
                const auto component = canonical.substr(0U, canonical_dot);
                if (component != segment
                    && !(component.size() > segment.size() + 1U
                        && component.ends_with(segment)
                        && component.substr(
                               component.size() - segment.size() - 2U, 2U)
                            == "::")) {
                    return false;
                }
                if (name_dot == std::string_view::npos) {
                    return true;
                }
                name.remove_prefix(name_dot + 1U);
                canonical.remove_prefix(canonical_dot + 1U);
            }
        }
        if (canonical.size() <= name.size())
            return false;
        const auto prefix = canonical.substr(0, canonical.size() - name.size());
        return (prefix.ends_with("::") || prefix.ends_with('.'))
            && canonical.ends_with(name);
    }

    [[nodiscard]] std::optional<SystemVerilogConstraintVariableId>
    find_optional_template_variable(
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
        return result;
    }

    [[nodiscard]] SystemVerilogConstraintVariableId find_template_variable(
        const SystemVerilogConstraintVariables& variables,
        const std::string_view name)
    {
        const auto result = find_optional_template_variable(variables, name);
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

    // The canonical identity a template name denotes, by the same suffix
    // rule as find_optional_template_variable.
    [[nodiscard]] std::optional<std::string> find_template_key(
        const SystemVerilogConstraintVariables& variables,
        std::string_view name)
    {
        if (name.starts_with("this."))
            name.remove_prefix(5U);
        if (variables.contains(std::string { name }))
            return std::string { name };
        std::optional<std::string> result;
        for (const auto& [canonical, variable] : variables) {
            (void)variable;
            if (!variable_suffix(canonical, name))
                continue;
            if (result && *result != canonical) {
                throw std::invalid_argument {
                    "inline constraint name '" + std::string { name }
                    + "' is ambiguous"
                };
            }
            result = canonical;
        }
        return result;
    }

    [[nodiscard]] SystemVerilogConstraintVariableProfile index_profile()
    {
        return { SystemVerilogConstraintDomainKind::Integer, 32, true, "int",
            false };
    }

    [[nodiscard]] SystemVerilogConstraintVariableProfile truth_profile()
    {
        return { SystemVerilogConstraintDomainKind::BitVector, 1, false, "bit",
            false };
    }

    [[nodiscard]] std::optional<SystemVerilogConstraintVariableProfile>
    integral_cast_profile(const std::string_view type)
    {
        using Kind = SystemVerilogConstraintDomainKind;
        if (type == "int")
            return SystemVerilogConstraintVariableProfile { Kind::Integer, 32,
                true, "int", false };
        if (type == "integer")
            return SystemVerilogConstraintVariableProfile { Kind::Integer, 32,
                true, "integer", true };
        if (type == "shortint")
            return SystemVerilogConstraintVariableProfile { Kind::Integer, 16,
                true, "shortint", false };
        if (type == "longint")
            return SystemVerilogConstraintVariableProfile { Kind::Integer, 64,
                true, "longint", false };
        if (type == "byte")
            return SystemVerilogConstraintVariableProfile { Kind::Integer, 8,
                true, "byte", false };
        if (type == "bit")
            return SystemVerilogConstraintVariableProfile { Kind::BitVector, 1,
                false, "bit", false };
        if (type == "logic" || type == "reg")
            return SystemVerilogConstraintVariableProfile { Kind::BitVector, 1,
                false, "logic", true };
        return std::nullopt;
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

        struct Clause {
            SystemVerilogConstraintExpression expression;
            std::vector<SystemVerilogConstraintVariableId> dependencies;
            bool soft { };
        };

        /// Lower a constraint as separate clauses: block members, foreach
        /// iterations and unique pairs, each under its guards. A clause
        /// naming an array whose size is still being solved is left out.
        void lower_clauses(const Template& expression,
            std::vector<Clause>& clauses)
        {
            split(expression, clauses, false);
        }

        [[nodiscard]] std::optional<SystemVerilogConstraintExpressionId> lower(
            const Template& expression)
        {
            if (expression.kind == TemplateKind::Name) {
                // A loop index, or a with-clause item and its index (IEEE
                // 1800-2017 18.5.8.1, 7.12.4).
                if (const auto* frame = iterator(expression.text)) {
                    if (!frame->item) {
                        return builder_.constant(
                            PackedLogic4::from_aval_bval(32, frame->index, 0),
                            index_profile());
                    }
                    use(*frame->element);
                    return builder_.variable(*frame->element,
                        solver_.variables()[*frame->element].profile);
                }
                if (const auto dot = expression.text.rfind('.');
                    dot != std::string::npos
                    && std::string_view { expression.text }.substr(dot)
                        == ".index") {
                    if (const auto* frame = iterator(
                            std::string_view { expression.text }.substr(0, dot));
                        frame != nullptr && frame->item) {
                        return builder_.constant(
                            PackedLogic4::from_aval_bval(32, frame->index, 0),
                            index_profile());
                    }
                }
                // `a.size` without parentheses (IEEE 1800-2017 13.4.5).
                if (const auto size = size_method(expression))
                    return lower_method(*size);
                // A name the object does not declare reads the calling
                // scope's value carried as the operand (IEEE 1800-2017 18.7).
                if (expression.operands.size() == 1U
                    && !find_optional_template_variable(
                        variables_, expression.text)) {
                    if (container(expression.text) != nullptr || deferred_)
                        throw std::invalid_argument {
                            "inline constraint array '" + expression.text
                            + "' is used as a value"
                        };
                    return lower(expression.operands.front());
                }
                if (!find_optional_template_variable(
                        variables_, expression.text)) {
                    (void)container(expression.text);
                    if (deferred_)
                        return std::nullopt;
                }
                const auto variable = find_template_variable(
                    variables_, expression.text);
                if (variable >= solver_.variables().size()) {
                    throw std::invalid_argument {
                        "inline constraint variable id is outside the solver"
                    };
                }
                use(variable);
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
            if (expression.kind == TemplateKind::Index
                && (expression.operands.size() == 2U
                    || expression.operands.size() == 3U)
                && expression.operands[0].kind == TemplateKind::Name) {
                const auto* elements = container(expression.operands[0].text);
                if (elements == nullptr) {
                    if (deferred_)
                        return std::nullopt;
                    // An element of an array of the calling scope.
                    if (expression.operands.size() == 3U)
                        return lower(expression.operands[2]);
                    throw std::invalid_argument {
                        "inline constraint array '"
                        + expression.operands[0].text
                        + "' has no element solver variables"
                    };
                }
                const auto profile = element_profile(*elements);
                const auto outside = [&] {
                    // A selection outside the array occurs only where a
                    // guard keeps it from constraining.
                    return builder_.constant(
                        PackedLogic4(profile.width, Logic4::zero), profile);
                };
                if (const auto position = constant_value(expression.operands[1])) {
                    if (*position < 0
                        || static_cast<std::uint64_t>(*position)
                            >= elements->size()) {
                        return outside();
                    }
                    const auto element
                        = (*elements)[static_cast<std::size_t>(*position)];
                    use(element);
                    return builder_.variable(element, profile);
                }
                const auto selector = lower(expression.operands[1]);
                if (!selector)
                    return std::nullopt;
                auto result = outside();
                for (std::size_t index = elements->size(); index-- > 0U;) {
                    use((*elements)[index]);
                    result = builder_.conditional(
                        builder_.binary(Operator::Equal, *selector,
                            builder_.constant(
                                PackedLogic4::from_aval_bval(32, index, 0),
                                index_profile())),
                        builder_.variable((*elements)[index], profile), result);
                }
                return result;
            }
            if (expression.kind == TemplateKind::Foreach
                && expression.operands.size() == 2U) {
                const auto selection = foreach_selection(expression);
                if (!selection)
                    return std::nullopt;
                std::vector<SystemVerilogConstraintExpressionId> predicates;
                for (std::size_t index = 0;
                     index < selection->second->size(); ++index) {
                    iterators_.push_back({ selection->first, index,
                        (*selection->second)[index], false });
                    const auto predicate = lower(expression.operands[1]);
                    iterators_.pop_back();
                    if (!predicate)
                        return std::nullopt;
                    predicates.push_back(*predicate);
                }
                if (predicates.empty()) {
                    return builder_.constant(
                        PackedLogic4::from_aval_bval(1, 1, 0), truth_profile());
                }
                return builder_.conjunction(predicates);
            }
            if (expression.kind == TemplateKind::Method) {
                return lower_method(expression);
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
                    } else if (const auto* elements = item.kind
                                       == TemplateKind::Name
                                   ? container(item.text) : nullptr) {
                        // An unpacked array in the set contributes its
                        // elements (IEEE 1800-2017 11.4.13).
                        for (const auto element : *elements) {
                            use(element);
                            const auto equal = builder_.binary(Operator::Equal,
                                *subject,
                                builder_.variable(element,
                                    solver_.variables()[element].profile));
                            selected = selected
                                ? builder_.binary(
                                      Operator::LogicalOr, *selected, equal)
                                : equal;
                        }
                        if (!selected) {
                            selected = builder_.constant(
                                PackedLogic4::from_aval_bval(1, 0, 0),
                                truth_profile());
                        }
                    } else {
                        if (deferred_)
                            return std::nullopt;
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
                    if (item.kind != TemplateKind::Name
                        && item.kind != TemplateKind::Index) {
                        throw std::invalid_argument {
                            "inline unique constraint items must be named variables"
                        };
                    }
                    if (const auto* elements = item.kind == TemplateKind::Name
                            ? container(item.text) : nullptr) {
                        for (const auto element : *elements) {
                            use(element);
                            values.push_back(builder_.variable(element,
                                solver_.variables()[element].profile));
                        }
                        continue;
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
        struct Iterator {
            std::string name;
            std::size_t index { };
            std::optional<SystemVerilogConstraintVariableId> element;
            bool item { };
        };
        struct Guard {
            const Template* condition { };
            bool holds { };
        };

        [[nodiscard]] const Iterator* iterator(const std::string_view name) const
        {
            for (auto frame = iterators_.rbegin(); frame != iterators_.rend();
                 ++frame) {
                if (frame->name == name)
                    return &*frame;
            }
            return nullptr;
        }

        void use(const SystemVerilogConstraintVariableId variable)
        {
            if (std::ranges::find(dependencies_, variable)
                == dependencies_.end()) {
                dependencies_.push_back(variable);
            }
        }

        /// The elements of the array a name denotes, materialized by the
        /// class randomizer as `a[]` and `a[0]`, `a[1]`, ... An array whose
        /// size is still a variable defers the expression.
        [[nodiscard]] const std::vector<SystemVerilogConstraintVariableId>*
        container(const std::string_view name)
        {
            if (iterator(name) != nullptr)
                return nullptr;
            const auto cached = containers_.find(std::string { name });
            if (cached != containers_.end())
                return &cached->second;
            const auto marker = find_template_key(
                variables_, std::string { name } + "[]");
            if (!marker) {
                if (find_template_key(
                        variables_, std::string { name } + ".size()")) {
                    deferred_ = true;
                }
                return nullptr;
            }
            const auto base = marker->substr(0, marker->size() - 2U);
            std::vector<SystemVerilogConstraintVariableId> elements;
            for (std::size_t index = 0;; ++index) {
                const auto element = variables_.find(
                    base + "[" + std::to_string(index) + "]");
                if (element == variables_.end())
                    break;
                elements.push_back(element->second);
            }
            return &containers_.emplace(std::string { name },
                std::move(elements)).first->second;
        }

        [[nodiscard]] SystemVerilogConstraintVariableProfile element_profile(
            const std::vector<SystemVerilogConstraintVariableId>& elements) const
        {
            return elements.empty()
                ? index_profile()
                : solver_.variables()[elements.front()].profile;
        }

        [[nodiscard]] std::optional<std::pair<std::string,
            const std::vector<SystemVerilogConstraintVariableId>*>>
        foreach_selection(const Template& expression)
        {
            const auto& selection = expression.operands[0];
            if (selection.kind != TemplateKind::Index
                || selection.operands.size() != 2U
                || selection.operands[0].kind != TemplateKind::Name
                || selection.operands[1].kind != TemplateKind::Name) {
                throw std::invalid_argument {
                    "inline foreach requires one named array index"
                };
            }
            const auto* elements = container(selection.operands[0].text);
            if (elements == nullptr) {
                if (deferred_)
                    return std::nullopt;
                throw std::invalid_argument {
                    "inline foreach array '" + selection.operands[0].text
                    + "' has no materialized elements"
                };
            }
            return std::pair { selection.operands[1].text, elements };
        }

        /// `a.size` names the size method of array `a`.
        [[nodiscard]] std::optional<Template> size_method(
            const Template& expression)
        {
            constexpr std::string_view suffix = ".size";
            if (!expression.text.ends_with(suffix)
                || find_optional_template_variable(variables_, expression.text))
                return std::nullopt;
            const auto array = expression.text.substr(
                0, expression.text.size() - suffix.size());
            const bool sized = container(array) != nullptr || deferred_;
            if (!sized)
                return std::nullopt;
            Template method;
            method.kind = TemplateKind::Method;
            method.text = ".size";
            Template name;
            name.kind = TemplateKind::Name;
            name.text = array;
            method.operands.push_back(std::move(name));
            return method;
        }

        /// The value of an index expression known while lowering.
        [[nodiscard]] std::optional<std::int64_t> constant_value(
            const Template& expression)
        {
            if (const auto size = size_method(expression))
                return constant_value(*size);
            if (expression.kind == TemplateKind::Constant) {
                if (expression.constant.width() > 64U
                    || expression.constant.is_logic9())
                    return std::nullopt;
                const auto word = expression.constant.low_word();
                if (word.bval != 0U)
                    return std::nullopt;
                auto value = word.aval;
                const auto width = expression.constant.width();
                if (expression.profile.signed_value && width < 64U
                    && ((value >> (width - 1U)) & 1U) != 0U) {
                    value |= ~((std::uint64_t { 1 } << width) - 1U);
                }
                return static_cast<std::int64_t>(value);
            }
            if (expression.kind == TemplateKind::Name) {
                if (const auto* frame = iterator(expression.text);
                    frame != nullptr && !frame->item) {
                    return static_cast<std::int64_t>(frame->index);
                }
                if (expression.operands.size() == 1U
                    && !find_optional_template_variable(
                        variables_, expression.text)) {
                    return constant_value(expression.operands.front());
                }
                return std::nullopt;
            }
            if (expression.kind == TemplateKind::Method
                && expression.text == ".size"
                && !expression.operands.empty()
                && expression.operands[0].kind == TemplateKind::Name) {
                const auto* elements = container(expression.operands[0].text);
                if (elements == nullptr) {
                    return expression.operands.size() == 2U
                        ? constant_value(expression.operands[1])
                        : std::nullopt;
                }
                return static_cast<std::int64_t>(elements->size());
            }
            if (expression.kind == TemplateKind::Unary
                && expression.operands.size() == 1U
                && (expression.text == "-" || expression.text == "+")) {
                const auto value = constant_value(expression.operands[0]);
                if (!value)
                    return std::nullopt;
                return expression.text == "-" ? -*value : *value;
            }
            if (expression.kind == TemplateKind::Binary
                && expression.operands.size() == 2U) {
                const auto left = constant_value(expression.operands[0]);
                const auto right = constant_value(expression.operands[1]);
                if (!left || !right)
                    return std::nullopt;
                constexpr std::int64_t bound = std::int64_t { 1 } << 40;
                if (*left > bound || *left < -bound || *right > bound
                    || *right < -bound)
                    return std::nullopt;
                if (expression.text == "+")
                    return *left + *right;
                if (expression.text == "-")
                    return *left - *right;
                if (expression.text == "*")
                    return *left * *right;
                if ((expression.text == "/" || expression.text == "%")
                    && *right != 0) {
                    return expression.text == "/" ? *left / *right
                                                  : *left % *right;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<SystemVerilogConstraintExpressionId>
        lower_method(const Template& expression)
        {
            const auto& method = expression.text;
            if (method == ".size" && !expression.operands.empty()
                && expression.operands.size() <= 2U
                && expression.operands[0].kind == TemplateKind::Name) {
                if (const auto* elements
                    = container(expression.operands[0].text)) {
                    return builder_.constant(PackedLogic4::from_aval_bval(
                                                 32, elements->size(), 0),
                        index_profile());
                }
                // While sizes are solved, a random array's size is a
                // variable (IEEE 1800-2017 18.4).
                const auto size = find_optional_template_variable(variables_,
                    expression.operands[0].text + ".size()");
                // An array of the calling scope passes its size.
                if (!size && expression.operands.size() == 2U)
                    return lower(expression.operands[1]);
                if (!size) {
                    throw std::invalid_argument {
                        "inline constraint array '"
                        + expression.operands[0].text + "' has no size"
                    };
                }
                use(*size);
                return builder_.variable(
                    *size, solver_.variables()[*size].profile);
            }
            if ((method == ".sum" || method == ".product" || method == ".and"
                    || method == ".or" || method == ".xor")
                && !expression.operands.empty()
                && expression.operands.size() <= 3U
                && expression.operands[0].kind == TemplateKind::Name) {
                // Array reductions, optionally through a with-clause (IEEE
                // 1800-2017 7.12.3, 18.5.8.2).
                const auto* elements = container(expression.operands[0].text);
                if (elements == nullptr) {
                    if (deferred_)
                        return std::nullopt;
                    throw std::invalid_argument {
                        "inline constraint array '"
                        + expression.operands[0].text
                        + "' has no element solver variables"
                    };
                }
                const auto profile = element_profile(*elements);
                const auto* with = expression.operands.size() >= 2U
                    ? &expression.operands.back() : nullptr;
                const std::string item = expression.operands.size() == 3U
                    ? expression.operands[1].text : std::string { "item" };
                const auto operation = method == ".sum" ? Operator::Add
                    : method == ".product"              ? Operator::Multiply
                    : method == ".and"                  ? Operator::BitwiseAnd
                    : method == ".or"                   ? Operator::BitwiseOr
                                                        : Operator::BitwiseXor;
                std::optional<SystemVerilogConstraintExpressionId> result;
                for (std::size_t index = 0; index < elements->size(); ++index) {
                    std::optional<SystemVerilogConstraintExpressionId> value;
                    if (with != nullptr) {
                        iterators_.push_back(
                            { item, index, (*elements)[index], true });
                        value = lower(*with);
                        iterators_.pop_back();
                        if (!value)
                            return std::nullopt;
                    } else {
                        use((*elements)[index]);
                        value = builder_.variable((*elements)[index], profile);
                    }
                    result = result
                        ? builder_.binary(operation, *result, *value) : *value;
                }
                if (result)
                    return result;
                PackedLogic4 identity(profile.width,
                    operation == Operator::BitwiseAnd ? Logic4::one
                                                      : Logic4::zero);
                if (operation == Operator::Multiply)
                    identity.set(0, Logic4::one);
                return builder_.constant(std::move(identity), profile);
            }
            if (method.starts_with("@sv-cast:")
                && expression.operands.size() == 1U) {
                const auto operand = lower(expression.operands[0]);
                if (!operand)
                    return std::nullopt;
                const auto target = std::string_view { method }.substr(9U);
                auto profile = builder_.profile(*operand);
                if (target == "signed" || target == "unsigned") {
                    profile.signed_value = target == "signed";
                } else if (const auto scalar = integral_cast_profile(target)) {
                    profile = *scalar;
                } else {
                    std::uint64_t width { };
                    const auto parsed = std::from_chars(target.data(),
                        target.data() + target.size(), width);
                    if (parsed.ec != std::errc { }
                        || parsed.ptr != target.data() + target.size()
                        || width == 0U || width > 4096U) {
                        throw std::invalid_argument {
                            "unsupported inline constraint cast '"
                            + std::string { target } + "'"
                        };
                    }
                    profile.width = static_cast<std::size_t>(width);
                }
                return builder_.resize(*operand, profile);
            }
            if ((method == "$countones" || method == "$onehot"
                    || method == "$onehot0")
                && expression.operands.size() == 1U) {
                // Bit-count functions (IEEE 1800-2017 20.9).
                const auto operand = lower(expression.operands[0]);
                if (!operand)
                    return std::nullopt;
                auto profile = builder_.profile(*operand);
                const auto width = profile.width;
                if (width == 0U || width > 64U) {
                    throw std::invalid_argument {
                        "inline bit count requires a 1- to 64-bit operand"
                    };
                }
                profile.signed_value = false;
                const auto value = builder_.resize(*operand, profile);
                std::optional<SystemVerilogConstraintExpressionId> count;
                for (std::size_t bit = 0; bit < width; ++bit) {
                    const auto set = builder_.binary(Operator::NotEqual,
                        builder_.binary(Operator::BitwiseAnd, value,
                            builder_.constant(PackedLogic4::from_aval_bval(
                                                  width,
                                                  std::uint64_t { 1 } << bit, 0),
                                profile)),
                        builder_.constant(
                            PackedLogic4::from_aval_bval(width, 0, 0), profile));
                    const auto counted = builder_.resize(set, index_profile());
                    count = count
                        ? builder_.binary(Operator::Add, *count, counted)
                        : counted;
                }
                if (method == "$countones")
                    return count;
                return builder_.binary(method == "$onehot" ? Operator::Equal
                                                           : Operator::LessEqual,
                    *count,
                    builder_.constant(
                        PackedLogic4::from_aval_bval(32, 1, 0), index_profile()));
            }
            throw std::invalid_argument {
                "unsupported inline constraint method '" + method + "'"
            };
        }

        [[nodiscard]] bool leaf(const Template& body,
            std::vector<Clause>& clauses, const bool soft)
        {
            return leaf([&] { return lower(body); }, clauses, soft);
        }

        // One clause from the expression `body` builds, under the active
        // guards.
        template <typename Body>
        [[nodiscard]] bool leaf(const Body& body,
            std::vector<Clause>& clauses, const bool soft)
        {
            builder_ = { };
            dependencies_.clear();
            deferred_ = false;
            auto root = body();
            for (auto guard = guards_.rbegin(); root && guard != guards_.rend();
                 ++guard) {
                auto condition = lower(*guard->condition);
                if (!condition) {
                    root.reset();
                    break;
                }
                if (!guard->holds) {
                    condition = builder_.unary(Operator::LogicalNot, *condition);
                }
                root = builder_.implication(*condition, *root);
            }
            if (!root) {
                if (deferred_)
                    return true;
                throw std::invalid_argument {
                    "inline constraint could not be lowered"
                };
            }
            clauses.push_back({ std::move(builder_).finish(*root),
                dependencies_, soft });
            builder_ = { };
            return true;
        }

        void split(const Template& expression, std::vector<Clause>& clauses,
            const bool soft)
        {
            if (expression.kind == TemplateKind::Block) {
                for (const auto& item : expression.operands)
                    split(item, clauses, soft);
                return;
            }
            if (expression.kind == TemplateKind::Soft
                && expression.operands.size() == 1U) {
                split(expression.operands.front(), clauses, true);
                return;
            }
            if (expression.kind == TemplateKind::Implication
                && expression.operands.size() == 2U) {
                guards_.push_back({ &expression.operands[0], true });
                split(expression.operands[1], clauses, soft);
                guards_.pop_back();
                return;
            }
            if (expression.kind == TemplateKind::ConditionalConstraint
                && (expression.operands.size() == 2U
                    || expression.operands.size() == 3U)) {
                guards_.push_back({ &expression.operands[0], true });
                split(expression.operands[1], clauses, soft);
                guards_.pop_back();
                if (expression.operands.size() == 3U) {
                    guards_.push_back({ &expression.operands[0], false });
                    split(expression.operands[2], clauses, soft);
                    guards_.pop_back();
                }
                return;
            }
            // Each pair of the unique items differs, one clause per pair
            // (IEEE 1800-2017 18.5.5).
            if (expression.kind == TemplateKind::Unique) {
                struct Item {
                    const Template* expression { };
                    std::optional<SystemVerilogConstraintVariableId> element;
                };
                std::vector<Item> items;
                for (const auto& item : expression.operands) {
                    if (item.kind != TemplateKind::Name
                        && item.kind != TemplateKind::Index) {
                        throw std::invalid_argument {
                            "inline unique constraint items must be named variables"
                        };
                    }
                    if (const auto* elements = item.kind == TemplateKind::Name
                            ? container(item.text) : nullptr) {
                        for (const auto element : *elements) {
                            items.push_back({ nullptr, element });
                        }
                        continue;
                    }
                    items.push_back({ &item, std::nullopt });
                }
                const auto value = [&](const Item& item)
                    -> std::optional<SystemVerilogConstraintExpressionId> {
                    if (item.element) {
                        use(*item.element);
                        return builder_.variable(*item.element,
                            solver_.variables()[*item.element].profile);
                    }
                    return lower(*item.expression);
                };
                for (std::size_t left = 0; left < items.size(); ++left) {
                    for (std::size_t right = left + 1U; right < items.size();
                         ++right) {
                        (void)leaf([&]()
                                -> std::optional<
                                    SystemVerilogConstraintExpressionId> {
                            const auto first = value(items[left]);
                            if (!first)
                                return std::nullopt;
                            const auto second = value(items[right]);
                            if (!second)
                                return std::nullopt;
                            return builder_.binary(
                                Operator::NotEqual, *first, *second);
                        }, clauses, soft);
                    }
                }
                return;
            }
            if (expression.kind == TemplateKind::Foreach
                && expression.operands.size() == 2U) {
                deferred_ = false;
                const auto selection = foreach_selection(expression);
                if (!selection)
                    return;
                for (std::size_t index = 0;
                     index < selection->second->size(); ++index) {
                    iterators_.push_back({ selection->first, index,
                        (*selection->second)[index], false });
                    split(expression.operands[1], clauses, soft);
                    iterators_.pop_back();
                }
                return;
            }
            (void)leaf(expression, clauses, soft);
        }

        const SystemVerilogConstraintSolver& solver_;
        const SystemVerilogConstraintVariables& variables_;
        SystemVerilogConstraintExpressionBuilder builder_;
        std::vector<SystemVerilogConstraintVariableId> dependencies_;
        std::vector<Iterator> iterators_;
        std::vector<Guard> guards_;
        std::map<std::string, std::vector<SystemVerilogConstraintVariableId>,
            std::less<>>
            containers_;
        bool deferred_ { };
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
        InlineTemplateLowerer lowerer { solver, variables };
        std::vector<InlineTemplateLowerer::Clause> clauses;
        lowerer.lower_clauses(expression, clauses);
        for (std::size_t part = 0; part < clauses.size(); ++part) {
            auto clause = systemverilog_constraint_expression_clause(
                part == 0U ? identity : identity + "::" + std::to_string(part),
                std::move(clauses[part].dependencies),
                std::move(clauses[part].expression));
            clause.soft = clauses[part].soft;
            solver.add_clause(std::move(clause));
        }
    }
}

} // namespace fsim::runtime

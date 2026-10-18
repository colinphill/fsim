// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/constraint_solver.hpp"
#include "constraint_bounds_internal.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

using Operator = SystemVerilogConstraintExpressionOperator;
using Ranges = std::vector<SystemVerilogConstraintKeyRange>;
using Wide = __int128;

struct Analysis {
  Ranges ranges;
  // The ranges are exactly the satisfying keys, not a superset; only an
  // exact result can be complemented.
  bool exact{};
};

[[nodiscard]] Ranges normalized(Ranges ranges) {
  std::ranges::sort(ranges, {}, &SystemVerilogConstraintKeyRange::low);
  Ranges result;
  for (const auto& range : ranges) {
    if (!result.empty()
        && (result.back().high == std::numeric_limits<std::uint64_t>::max()
            || range.low <= result.back().high + 1U)) {
      result.back().high = std::max(result.back().high, range.high);
      continue;
    }
    result.push_back(range);
  }
  return result;
}

[[nodiscard]] Ranges intersected(const Ranges& left, const Ranges& right) {
  Ranges result;
  for (const auto& a : left) {
    for (const auto& b : right) {
      const auto low = std::max(a.low, b.low);
      const auto high = std::min(a.high, b.high);
      if (low <= high) result.push_back({low, high});
    }
  }
  return normalized(std::move(result));
}

[[nodiscard]] Ranges united(const Ranges& left, const Ranges& right) {
  auto result = left;
  result.insert(result.end(), right.begin(), right.end());
  return normalized(std::move(result));
}

[[nodiscard]] Ranges complemented(
    const Ranges& ranges, const std::uint64_t maximum_key) {
  Ranges result;
  std::uint64_t next{};
  bool done{};
  for (const auto& range : ranges) {
    if (range.low > next) result.push_back({next, range.low - 1U});
    if (range.high == maximum_key) {
      done = true;
      break;
    }
    next = range.high + 1U;
  }
  if (!done) result.push_back({next, maximum_key});
  return result;
}

// The satisfying keys of `variable op constant` (or the mirrored form),
// following the comparison's context width and signedness (11.8.1).
[[nodiscard]] std::optional<Ranges> comparison_ranges(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintExpressionNode& node,
    const SystemVerilogConstraintVariableId variable,
    const std::size_t width) {
  if (node.operands.size() != 2U) return std::nullopt;
  const auto* left = &expression.nodes[node.operands[0]];
  const auto* right = &expression.nodes[node.operands[1]];
  auto operation = node.operation;
  if (right->operation == Operator::Variable
      && left->operation == Operator::Constant) {
    std::swap(left, right);
    switch (operation) {
    case Operator::Less: operation = Operator::Greater; break;
    case Operator::LessEqual: operation = Operator::GreaterEqual; break;
    case Operator::Greater: operation = Operator::Less; break;
    case Operator::GreaterEqual: operation = Operator::LessEqual; break;
    default: break;
    }
  }
  if (left->operation != Operator::Variable || left->variable != variable
      || right->operation != Operator::Constant) {
    return std::nullopt;
  }
  const auto& constant = right->constant;
  const auto constant_width = constant.width();
  if (constant_width == 0U || constant_width > 64U || constant.is_logic9()
      || std::ranges::any_of(constant.bval_words(),
             [](const auto word) { return word != 0U; })) {
    return std::nullopt;
  }
  const bool variable_signed = left->profile.signed_value;
  const bool signed_context = variable_signed && right->profile.signed_value;
  const auto context_width = std::max(width, constant_width);
  const Wide one{1};
  auto raw = static_cast<Wide>(constant.unchecked_low_word().aval);
  if (constant_width < 64U) raw &= (one << constant_width) - 1;
  // The constant extended to the context width by its own signedness.
  Wide value = raw;
  if (right->profile.signed_value && (raw >> (constant_width - 1U)) != 0) {
    value = raw - (one << constant_width);
  }
  Wide context_low{};
  Wide context_high{};
  if (signed_context) {
    context_low = -(one << (context_width - 1U));
    context_high = (one << (context_width - 1U)) - 1;
  } else {
    context_high = (one << context_width) - 1;
    if (value < 0) value += one << context_width;
  }
  std::vector<std::pair<Wide, Wide>> accepted;
  switch (operation) {
  case Operator::Equal:
  case Operator::CaseEqual:
    accepted.emplace_back(value, value);
    break;
  case Operator::NotEqual:
  case Operator::CaseNotEqual:
    accepted.emplace_back(context_low, value - 1);
    accepted.emplace_back(value + 1, context_high);
    break;
  case Operator::Less: accepted.emplace_back(context_low, value - 1); break;
  case Operator::LessEqual: accepted.emplace_back(context_low, value); break;
  case Operator::Greater: accepted.emplace_back(value + 1, context_high); break;
  case Operator::GreaterEqual:
    accepted.emplace_back(value, context_high);
    break;
  default: return std::nullopt;
  }
  // Each piece maps a run of context values to consecutive keys.
  struct Piece {
    Wide low;
    Wide high;
    Wide first_key;
  };
  std::vector<Piece> pieces;
  const auto half = one << (width - 1U);
  if (!variable_signed) {
    pieces.push_back({0, (one << width) - 1, 0});
  } else if (signed_context) {
    pieces.push_back({-half, half - 1, 0});
  } else {
    // A signed variable in an unsigned comparison is sign-extended, so its
    // negative values sit at the top of the context range.
    pieces.push_back({0, half - 1, half});
    pieces.push_back({(one << context_width) - half,
                      (one << context_width) - 1, 0});
  }
  Ranges result;
  for (const auto& [low, high] : accepted) {
    for (const auto& piece : pieces) {
      const auto from = std::max(low, piece.low);
      const auto to = std::min(high, piece.high);
      if (from > to) continue;
      result.push_back({
          static_cast<std::uint64_t>(piece.first_key + (from - piece.low)),
          static_cast<std::uint64_t>(piece.first_key + (to - piece.low))});
    }
  }
  return normalized(std::move(result));
}

// Keys within ranges: every key when there are at most 4096, otherwise
// each range's ends and a uniform sample of 512.
[[nodiscard]] std::vector<std::uint64_t> bounded_keys(
    const Ranges& ranges, const std::function<std::uint64_t()>& random) {
  constexpr unsigned __int128 enumerated_values = 4096U;
  constexpr std::size_t sampled_values = 512U;
  unsigned __int128 count{};
  for (const auto& range : ranges) {
    count += static_cast<unsigned __int128>(range.high - range.low) + 1U;
  }
  std::vector<std::uint64_t> keys;
  if (count <= enumerated_values) {
    for (const auto& range : ranges) {
      for (auto key = range.low;; ++key) {
        keys.push_back(key);
        if (key == range.high) break;
      }
    }
    return keys;
  }
  for (const auto& range : ranges) {
    keys.push_back(range.low);
    keys.push_back(range.high);
  }
  for (std::size_t sample = 0; sample < sampled_values; ++sample) {
    const unsigned __int128 high_word = random();
    auto offset = ((high_word << 64U) | random()) % count;
    for (const auto& range : ranges) {
      const auto size
          = static_cast<unsigned __int128>(range.high - range.low) + 1U;
      if (offset < size) {
        keys.push_back(range.low + static_cast<std::uint64_t>(offset));
        break;
      }
      offset -= size;
    }
  }
  return keys;
}

}  // namespace

std::vector<SystemVerilogConstraintKeyRange>
systemverilog_constraint_key_ranges(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile) {
  const auto width = profile.width;
  if (width == 0U || width > 64U) {
    throw std::invalid_argument{
        "constraint key ranges require a 1- to 64-bit variable"};
  }
  const auto maximum_key = width == 64U
      ? std::numeric_limits<std::uint64_t>::max()
      : (std::uint64_t{1} << width) - 1U;
  const Analysis full{{{0U, maximum_key}}, false};
  std::vector<std::optional<Analysis>> results(expression.nodes.size());
  const auto operand = [&](const SystemVerilogConstraintExpressionId id) {
    return id < results.size() && results[id] ? *results[id] : full;
  };
  // A predicate over constants only (`!(10 > 0)` from a guard) is
  // decided: every key or none.
  std::vector<std::optional<std::uint64_t>> known(expression.nodes.size());
  std::vector<std::optional<bool>> truth(expression.nodes.size());
  const auto signed_key = [](const std::uint64_t value, const std::size_t bits,
                             const bool signed_value) {
    return signed_value && bits < 64U && ((value >> (bits - 1U)) & 1U) != 0U
        ? value | ~((std::uint64_t{1} << bits) - 1U) : value;
  };
  for (std::size_t index = 0; index < expression.nodes.size(); ++index) {
    const auto& node = expression.nodes[index];
    const auto operand_known = [&](const std::size_t position) {
      return position < node.operands.size()
              && node.operands[position] < index
          ? known[node.operands[position]] : std::nullopt;
    };
    const auto operand_truth = [&](const std::size_t position) {
      return position < node.operands.size()
              && node.operands[position] < index
          ? truth[node.operands[position]] : std::nullopt;
    };
    if (node.operation == Operator::Constant && node.constant.width() <= 64U
        && !node.constant.is_logic9() && node.constant.low_word().bval == 0U
        && node.constant.width() > 0U) {
      known[index] = node.constant.low_word().aval;
      truth[index] = known[index] != 0U;
    } else if (node.operation == Operator::LogicalNot) {
      if (const auto value = operand_truth(0)) truth[index] = !*value;
    } else if (node.operation == Operator::LogicalAnd
               || node.operation == Operator::LogicalOr) {
      const auto left = operand_truth(0);
      const auto right = operand_truth(1);
      const bool conjunction = node.operation == Operator::LogicalAnd;
      if ((left && *left != conjunction) || (right && *right != conjunction)) {
        truth[index] = !conjunction;
      } else if (left && right) {
        truth[index] = conjunction;
      }
    } else if (node.operation >= Operator::Equal
               && node.operation <= Operator::GreaterEqual
               && node.operation != Operator::CaseEqual
               && node.operation != Operator::CaseNotEqual) {
      const auto left = operand_known(0);
      const auto right = operand_known(1);
      if (left && right) {
        const auto& left_profile = expression.nodes[node.operands[0]].profile;
        const auto& right_profile = expression.nodes[node.operands[1]].profile;
        const auto bits = std::max(left_profile.width, right_profile.width);
        const bool signed_compare
            = left_profile.signed_value && right_profile.signed_value;
        if (bits <= 64U) {
          const auto a = signed_key(*left, left_profile.width,
              left_profile.signed_value);
          const auto b = signed_key(*right, right_profile.width,
              right_profile.signed_value);
          const auto less = signed_compare
              ? static_cast<std::int64_t>(a) < static_cast<std::int64_t>(b)
              : a < b;
          const auto equal = a == b;
          switch (node.operation) {
          case Operator::Equal: truth[index] = equal; break;
          case Operator::NotEqual: truth[index] = !equal; break;
          case Operator::Less: truth[index] = less; break;
          case Operator::LessEqual: truth[index] = less || equal; break;
          case Operator::Greater: truth[index] = !less && !equal; break;
          case Operator::GreaterEqual: truth[index] = !less; break;
          default: break;
          }
        }
      }
    }
  }
  for (std::size_t index = 0; index < expression.nodes.size(); ++index) {
    const auto& node = expression.nodes[index];
    if (truth[index]
        && (node.operation == Operator::LogicalNot
            || (node.operation >= Operator::Equal
                && node.operation <= Operator::LogicalOr))) {
      results[index] = Analysis{
          *truth[index] ? Ranges{{0U, maximum_key}} : Ranges{}, true};
      continue;
    }
    switch (node.operation) {
    case Operator::LogicalAnd:
    case Operator::LogicalOr: {
      if (node.operands.size() != 2U) break;
      const auto left = operand(node.operands[0]);
      const auto right = operand(node.operands[1]);
      results[index] = Analysis{
          node.operation == Operator::LogicalAnd
              ? intersected(left.ranges, right.ranges)
              : united(left.ranges, right.ranges),
          left.exact && right.exact};
      break;
    }
    case Operator::LogicalNot: {
      if (node.operands.size() != 1U) break;
      const auto inner = operand(node.operands[0]);
      if (inner.exact) {
        results[index] = Analysis{
            complemented(inner.ranges, maximum_key), true};
      }
      break;
    }
    case Operator::Equal:
    case Operator::NotEqual:
    case Operator::CaseEqual:
    case Operator::CaseNotEqual:
    case Operator::Less:
    case Operator::LessEqual:
    case Operator::Greater:
    case Operator::GreaterEqual:
      if (auto ranges = comparison_ranges(expression, node, variable, width)) {
        results[index] = Analysis{std::move(*ranges), true};
      }
      break;
    default:
      break;
    }
  }
  return operand(expression.root).ranges;
}

std::vector<PackedLogic4> systemverilog_constraint_variable_domain(
    const std::span<const SystemVerilogConstraintClause> clauses,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random) {
  if (profile.width == 0U || profile.width > 64U || !random) {
    throw std::invalid_argument{
        "a bounded constraint domain requires a 1- to 64-bit variable"};
  }
  const auto depends = [&](const SystemVerilogConstraintClause& clause) {
    return clause.expression
        && std::ranges::find(clause.variables, variable)
            != clause.variables.end();
  };
  Ranges hard{{0U, profile.width == 64U
          ? std::numeric_limits<std::uint64_t>::max()
          : (std::uint64_t{1} << profile.width) - 1U}};
  for (const auto& clause : clauses) {
    if (!clause.soft && depends(clause)) {
      hard = intersected(hard,
          systemverilog_constraint_key_ranges(
              *clause.expression, variable, profile));
    }
  }
  if (hard.empty()) return {};
  auto keys = bounded_keys(hard, random);
  for (const auto& clause : clauses) {
    if (!clause.soft || !depends(clause)) continue;
    const auto preferred = intersected(hard,
        systemverilog_constraint_key_ranges(
            *clause.expression, variable, profile));
    if (preferred.empty()) continue;
    const auto more = bounded_keys(preferred, random);
    keys.insert(keys.end(), more.begin(), more.end());
  }
  std::ranges::sort(keys);
  keys.erase(std::ranges::unique(keys).begin(), keys.end());
  const auto sign = profile.signed_value
      ? std::uint64_t{1} << (profile.width - 1U)
      : std::uint64_t{};
  std::vector<PackedLogic4> result;
  result.reserve(keys.size());
  for (const auto key : keys) {
    result.push_back(
        PackedLogic4::from_aval_bval(profile.width, key ^ sign, 0));
  }
  return result;
}

std::vector<PackedLogic4> systemverilog_constraint_variable_domain_with_fixed(
    const std::span<const SystemVerilogConstraintClause> clauses,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random,
    const std::map<SystemVerilogConstraintVariableId, PackedLogic4>& fixed) {
  if (fixed.empty()) {
    return systemverilog_constraint_variable_domain(
        clauses, variable, profile, random);
  }
  std::vector<SystemVerilogConstraintClause> substituted;
  substituted.reserve(clauses.size());
  for (const auto& clause : clauses) {
    auto copy = clause;
    if (clause.expression) {
      auto expression = *clause.expression;
      for (auto& node : expression.nodes) {
        if (node.operation != Operator::Variable) continue;
        const auto value = fixed.find(node.variable);
        if (value == fixed.end() || node.variable == variable
            || value->second.width() != node.profile.width) {
          continue;
        }
        node.operation = Operator::Constant;
        node.constant = value->second;
      }
      copy.expression = std::make_shared<const SystemVerilogConstraintExpression>(
          std::move(expression));
      std::erase_if(copy.variables,
          [&](const SystemVerilogConstraintVariableId id) {
            return id != variable && fixed.contains(id);
          });
    }
    substituted.push_back(std::move(copy));
  }
  return systemverilog_constraint_variable_domain(
      substituted, variable, profile, random);
}

}  // namespace fsim::runtime

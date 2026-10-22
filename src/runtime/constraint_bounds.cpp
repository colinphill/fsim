// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/constraint_solver.hpp"
#include "constraint_bounds_internal.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <memory>
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
// each range's ends and a uniform sample of 2048, so a constraint the range
// analysis does not bound (a byte select equal to a constant) still meets a
// candidate with high probability.
[[nodiscard]] std::vector<std::uint64_t> bounded_keys(
    const Ranges& ranges, const std::function<std::uint64_t()>& random) {
  constexpr unsigned __int128 enumerated_values = 4096U;
  constexpr std::size_t sampled_values = 2048U;
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

namespace {

// The 64-bit value of a known constant node, extended by its signedness.
[[nodiscard]] std::optional<std::uint64_t> constant_bits(
    const SystemVerilogConstraintExpressionNode& node) {
  const auto bits = node.constant.width();
  if (node.operation != Operator::Constant || bits == 0U || bits > 64U
      || node.constant.is_logic9() || node.constant.low_word().bval != 0U) {
    return std::nullopt;
  }
  auto value = node.constant.low_word().aval;
  if (bits < 64U) {
    value &= (std::uint64_t{1} << bits) - 1U;
    if (node.profile.signed_value && ((value >> (bits - 1U)) & 1U) != 0U) {
      value |= ~((std::uint64_t{1} << bits) - 1U);
    }
  }
  return value;
}

[[nodiscard]] PackedLogic4 constant_of_width(
    std::uint64_t value, const std::size_t bits) {
  if (bits < 64U) value &= (std::uint64_t{1} << bits) - 1U;
  return PackedLogic4::from_aval_bval(bits, value, 0U);
}

// Folds integral arithmetic over constants (state values already
// substituted), and turns `v + c == k`, `v - c == k` and `c - v == k`
// into `v == k'` in the comparison's modular context (IEEE 1800-2017
// 11.4.3, 11.8.1), so the range analysis bounds `v`.
[[nodiscard]] SystemVerilogConstraintExpression normalized_expression(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintVariableId variable) {
  auto result = expression;
  auto& nodes = result.nodes;
  const auto original_count = nodes.size();
  for (std::size_t index = 0; index < original_count; ++index) {
    auto& node = nodes[index];
    const auto width = node.profile.width;
    if (width == 0U || width > 64U) continue;
    const auto operand = [&](const std::size_t position)
        -> std::optional<std::uint64_t> {
      return position < node.operands.size()
              && node.operands[position] < index
          ? constant_bits(nodes[node.operands[position]])
          : std::nullopt;
    };
    // A selection by a known condition is its chosen operand (IEEE
    // 1800-2017 11.4.11).
    if (node.operation == Operator::Conditional
        && node.operands.size() == 3U) {
      if (const auto condition = operand(0)) {
        const auto chosen = node.operands[*condition != 0U ? 1U : 2U];
        if (nodes[chosen].profile.width == width) {
          node.operation = Operator::UnaryPlus;
          node.operands = { chosen };
        }
      }
      continue;
    }
    std::optional<std::uint64_t> folded;
    switch (node.operation) {
    case Operator::UnaryMinus:
      if (node.operands.size() == 1U) {
        if (const auto value = operand(0)) folded = ~*value + 1U;
      }
      break;
    case Operator::BitwiseNot:
      if (node.operands.size() == 1U) {
        if (const auto value = operand(0)) folded = ~*value;
      }
      break;
    case Operator::Add:
    case Operator::Subtract:
    case Operator::Multiply:
    case Operator::BitwiseAnd:
    case Operator::BitwiseOr:
    case Operator::BitwiseXor:
      if (node.operands.size() == 2U) {
        const auto left = operand(0);
        const auto right = operand(1);
        if (!left || !right) break;
        switch (node.operation) {
        case Operator::Add: folded = *left + *right; break;
        case Operator::Subtract: folded = *left - *right; break;
        case Operator::Multiply: folded = *left * *right; break;
        case Operator::BitwiseAnd: folded = *left & *right; break;
        case Operator::BitwiseOr: folded = *left | *right; break;
        default: folded = *left ^ *right; break;
        }
      }
      break;
    default:
      break;
    }
    if (folded) {
      node.operation = Operator::Constant;
      node.constant = constant_of_width(*folded, width);
      node.operands.clear();
    }
  }
  const auto is_variable = [&](std::size_t id) {
    for (std::size_t depth = 0; depth < 4U && id < nodes.size(); ++depth) {
      if (nodes[id].operation == Operator::Variable) {
        return nodes[id].variable == variable ? std::optional { id }
                                              : std::nullopt;
      }
      if (nodes[id].operation != Operator::UnaryPlus
          || nodes[id].operands.size() != 1U) {
        return std::optional<std::size_t> {};
      }
      id = nodes[id].operands.front();
    }
    return std::optional<std::size_t> {};
  };
  // `c * v == k` and `v + v == k` hold for the few values of `v` that solve
  // the congruence modulo 2^w (IEEE 1800-2017 11.4.3): a disjunction of
  // equalities, or a constant when no value does.
  const auto rewrite_scaled = [&](const std::size_t index,
                                  const std::size_t arithmetic_id,
                                  const std::uint64_t known,
                                  const std::size_t context) {
    const auto& arithmetic = nodes[arithmetic_id];
    std::optional<std::size_t> found;
    std::optional<std::uint64_t> factor;
    const auto left = is_variable(arithmetic.operands[0]);
    const auto right = is_variable(arithmetic.operands[1]);
    if (arithmetic.operation == Operator::Add) {
      if (left && right) {
        found = left;
        factor = 2U;
      }
    } else if (arithmetic.operation == Operator::Multiply) {
      if (left) {
        found = left;
        factor = constant_bits(nodes[arithmetic.operands[1]]);
      } else if (right) {
        found = right;
        factor = constant_bits(nodes[arithmetic.operands[0]]);
      }
    }
    if (!found || !factor || nodes[*found].profile.width != context) {
      return false;
    }
    const auto mask = context == 64U
        ? ~std::uint64_t{} : (std::uint64_t{1} << context) - 1U;
    const auto scale = *factor & mask;
    const auto target = known & mask;
    if (scale == 0U) return false;
    const auto shift = static_cast<std::size_t>(std::countr_zero(scale));
    if (shift > 2U || shift >= context) return false;
    const bool equal = nodes[index].operation == Operator::Equal
        || nodes[index].operation == Operator::CaseEqual;
    if ((target & ((std::uint64_t{1} << shift) - 1U)) != 0U) {
      const auto width = nodes[index].profile.width;
      nodes[index].operation = Operator::Constant;
      nodes[index].constant = constant_of_width(equal ? 0U : 1U, width);
      nodes[index].operands.clear();
      return true;
    }
    // The inverse of the odd part modulo 2^64 by Newton's iteration.
    const auto odd = scale >> shift;
    auto inverse = odd;
    for (int step = 0; step < 6; ++step) inverse *= 2U - odd * inverse;
    const auto low_bits = context - shift;
    const auto low_mask = low_bits == 64U
        ? ~std::uint64_t{} : (std::uint64_t{1} << low_bits) - 1U;
    const auto first = ((target >> shift) * inverse) & low_mask;
    std::optional<std::size_t> combined;
    for (std::uint64_t step = 0; step < (std::uint64_t{1} << shift); ++step) {
      SystemVerilogConstraintExpressionNode value;
      value.operation = Operator::Constant;
      value.constant = constant_of_width(
          first + (low_bits == 64U ? 0U : step << low_bits), context);
      value.profile = nodes[*found].profile;
      nodes.push_back(std::move(value));
      SystemVerilogConstraintExpressionNode compare;
      compare.operation = equal ? Operator::Equal : Operator::NotEqual;
      compare.profile = nodes[index].profile;
      compare.operands = { *found, nodes.size() - 1U };
      nodes.push_back(std::move(compare));
      if (combined) {
        SystemVerilogConstraintExpressionNode join;
        join.operation = equal ? Operator::LogicalOr : Operator::LogicalAnd;
        join.profile = nodes[index].profile;
        join.operands = { *combined, nodes.size() - 1U };
        nodes.push_back(std::move(join));
      }
      combined = nodes.size() - 1U;
    }
    const auto replacement = nodes[*combined];
    nodes[index].operation = replacement.operation;
    nodes[index].operands = replacement.operands;
    return true;
  };
  for (std::size_t index = 0; index < original_count; ++index) {
    const auto operation = nodes[index].operation;
    if ((operation != Operator::Equal && operation != Operator::NotEqual
            && operation != Operator::CaseEqual
            && operation != Operator::CaseNotEqual)
        || nodes[index].operands.size() != 2U) {
      continue;
    }
    for (std::size_t side = 0; side < 2U; ++side) {
      const auto arithmetic_id = nodes[index].operands[side];
      const auto other_id = nodes[index].operands[1U - side];
      const auto& arithmetic = nodes[arithmetic_id];
      const auto known = constant_bits(nodes[other_id]);
      const auto context = std::max(
          arithmetic.profile.width, nodes[other_id].profile.width);
      if (known && context != 0U && context <= 64U
          && arithmetic.operands.size() == 2U
          && rewrite_scaled(index, arithmetic_id, *known, context)) {
        break;
      }
      // `~v == k` and `-v == k` invert: `v == ~k`, `v == -k`.
      if (known && context != 0U && context <= 64U
          && (arithmetic.operation == Operator::BitwiseNot
              || arithmetic.operation == Operator::UnaryMinus)
          && arithmetic.operands.size() == 1U) {
        const auto inverted = is_variable(arithmetic.operands.front());
        if (inverted && nodes[*inverted].profile.width == context) {
          SystemVerilogConstraintExpressionNode constant;
          constant.operation = Operator::Constant;
          constant.constant = constant_of_width(
              arithmetic.operation == Operator::BitwiseNot
                  ? ~*known : ~*known + 1U,
              context);
          constant.profile = nodes[*inverted].profile;
          const auto variable_id = *inverted;
          nodes.push_back(std::move(constant));
          nodes[index].operands = { variable_id, nodes.size() - 1U };
          break;
        }
      }
      if (!known || context == 0U || context > 64U
          || (arithmetic.operation != Operator::Add
              && arithmetic.operation != Operator::Subtract)
          || arithmetic.operands.size() != 2U) {
        continue;
      }
      std::optional<std::size_t> found;
      std::optional<std::uint64_t> offset;
      bool variable_first = true;
      if (const auto left = is_variable(arithmetic.operands[0])) {
        found = left;
        offset = constant_bits(nodes[arithmetic.operands[1]]);
      } else if (const auto right = is_variable(arithmetic.operands[1])) {
        found = right;
        offset = constant_bits(nodes[arithmetic.operands[0]]);
        variable_first = false;
      }
      if (!found || !offset) continue;
      std::uint64_t value{};
      if (arithmetic.operation == Operator::Add) {
        value = *known - *offset;
      } else if (variable_first) {
        value = *known + *offset;
      } else {
        value = *offset - *known;
      }
      SystemVerilogConstraintExpressionNode constant;
      constant.operation = Operator::Constant;
      constant.constant = constant_of_width(value, context);
      constant.profile = nodes[*found].profile;
      constant.profile.width = context;
      nodes.push_back(std::move(constant));
      nodes[index].operands = { *found, nodes.size() - 1U };
      break;
    }
  }
  // A rewritten comparison names nodes appended after it; restore source
  // order, every operand before its user.
  bool ordered = true;
  for (std::size_t index = 0; index < nodes.size() && ordered; ++index) {
    for (const auto operand : nodes[index].operands) {
      if (operand >= index) {
        ordered = false;
        break;
      }
    }
  }
  if (ordered) return result;
  std::vector<std::optional<std::size_t>> renumbered(nodes.size());
  std::vector<SystemVerilogConstraintExpressionNode> sorted;
  sorted.reserve(nodes.size());
  std::vector<std::pair<std::size_t, std::size_t>> stack;
  for (std::size_t start = 0; start < nodes.size(); ++start) {
    if (renumbered[start]) continue;
    stack.emplace_back(start, 0U);
    while (!stack.empty()) {
      auto& [id, next] = stack.back();
      if (next < nodes[id].operands.size()) {
        const auto operand = nodes[id].operands[next++];
        if (operand < nodes.size() && !renumbered[operand]) {
          stack.emplace_back(operand, 0U);
        }
        continue;
      }
      auto node = nodes[id];
      for (auto& operand : node.operands) {
        if (operand < nodes.size()) operand = *renumbered[operand];
      }
      renumbered[id] = sorted.size();
      sorted.push_back(std::move(node));
      stack.pop_back();
    }
  }
  nodes = std::move(sorted);
  if (result.root < renumbered.size() && renumbered[result.root]) {
    result.root = *renumbered[result.root];
  }
  return result;
}

// The values a profile represents.
[[nodiscard]] SystemVerilogConstraintValueInterval profile_range(
    const SystemVerilogConstraintVariableProfile& profile) {
  const Wide one{1};
  const auto width = profile.width;
  return profile.signed_value
      ? SystemVerilogConstraintValueInterval{
            -(one << (width - 1U)), (one << (width - 1U)) - 1}
      : SystemVerilogConstraintValueInterval{0, (one << width) - 1};
}

[[nodiscard]] bool within(const SystemVerilogConstraintValueInterval& inner,
    const SystemVerilogConstraintValueInterval& outer) {
  return outer.low <= inner.low && inner.high <= outer.high;
}

// The values an expression over constants and variables of known
// intervals can take, when no step wraps (IEEE 1800-2017 11.4.3, 11.8.1).
[[nodiscard]] std::optional<SystemVerilogConstraintValueInterval>
node_interval(const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintExpressionId id,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintValueIntervals& intervals,
    const std::size_t depth) {
  if (id >= expression.nodes.size() || depth > 32U) return std::nullopt;
  const auto& node = expression.nodes[id];
  const auto width = node.profile.width;
  if (width == 0U || width > 64U) return std::nullopt;
  const auto representable = profile_range(node.profile);
  const auto operand = [&](const std::size_t position)
      -> std::optional<SystemVerilogConstraintValueInterval> {
    if (position >= node.operands.size()) return std::nullopt;
    auto value = node_interval(expression, node.operands[position], variable,
        intervals, depth + 1U);
    // An operand is extended to the node's width keeping its value only
    // when the node's profile represents every value it can take.
    if (!value || !within(*value, representable)) return std::nullopt;
    return value;
  };
  switch (node.operation) {
  case Operator::Constant: {
    const auto bits = constant_bits(node);
    if (!bits) return std::nullopt;
    auto value = static_cast<Wide>(*bits);
    if (!node.profile.signed_value && width < 64U) {
      value &= (Wide{1} << width) - 1;
    } else if (node.profile.signed_value) {
      value = static_cast<Wide>(static_cast<std::int64_t>(*bits));
    }
    return SystemVerilogConstraintValueInterval{value, value};
  }
  case Operator::Variable: {
    if (node.variable == variable) return std::nullopt;
    const auto found = intervals.find(node.variable);
    if (found == intervals.end() || !within(found->second, representable)) {
      return std::nullopt;
    }
    return found->second;
  }
  case Operator::UnaryPlus:
    if (node.operands.size() != 1U) return std::nullopt;
    return operand(0);
  case Operator::Add:
  case Operator::Subtract: {
    if (node.operands.size() != 2U) return std::nullopt;
    const auto left = operand(0);
    const auto right = operand(1);
    if (!left || !right) return std::nullopt;
    const SystemVerilogConstraintValueInterval result
        = node.operation == Operator::Add
        ? SystemVerilogConstraintValueInterval{
              left->low + right->low, left->high + right->high}
        : SystemVerilogConstraintValueInterval{
              left->low - right->high, left->high - right->low};
    if (!within(result, representable)) return std::nullopt;
    return result;
  }
  default:
    return std::nullopt;
  }
}

// The keys `variable op e` allows for every value of an expression `e`
// over variables of known intervals: a superset bounded by e's extreme
// values (IEEE 1800-2017 18.5).
[[nodiscard]] std::optional<Ranges> interval_comparison_ranges(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintExpressionNode& node,
    const SystemVerilogConstraintVariableId variable,
    const std::size_t width,
    const SystemVerilogConstraintValueIntervals& intervals) {
  if (node.operands.size() != 2U) return std::nullopt;
  auto variable_side = node.operands[0];
  auto other_side = node.operands[1];
  auto operation = node.operation;
  const auto is_variable = [&](const SystemVerilogConstraintExpressionId id) {
    return id < expression.nodes.size()
        && expression.nodes[id].operation == Operator::Variable
        && expression.nodes[id].variable == variable;
  };
  if (!is_variable(variable_side)) {
    if (!is_variable(other_side)) return std::nullopt;
    std::swap(variable_side, other_side);
    switch (operation) {
    case Operator::Less: operation = Operator::Greater; break;
    case Operator::LessEqual: operation = Operator::GreaterEqual; break;
    case Operator::Greater: operation = Operator::Less; break;
    case Operator::GreaterEqual: operation = Operator::LessEqual; break;
    default: break;
    }
  }
  const auto& variable_node = expression.nodes[variable_side];
  const auto& other = expression.nodes[other_side];
  const auto bound = node_interval(
      expression, other_side, variable, intervals, 0U);
  if (!bound || other.profile.width == 0U || other.profile.width > 64U) {
    return std::nullopt;
  }
  // The comparison orders e's values monotonically only when it reads
  // them in their own signedness.
  const bool signed_context = variable_node.profile.signed_value
      && other.profile.signed_value;
  if (!signed_context && bound->low < 0) return std::nullopt;
  const auto compare = [&](const Operator with, const Wide value) {
    SystemVerilogConstraintExpression synthetic;
    synthetic.nodes.push_back(variable_node);
    SystemVerilogConstraintExpressionNode constant;
    constant.operation = Operator::Constant;
    constant.profile = other.profile;
    constant.constant = constant_of_width(
        static_cast<std::uint64_t>(value), other.profile.width);
    synthetic.nodes.push_back(std::move(constant));
    SystemVerilogConstraintExpressionNode comparison;
    comparison.operation = with;
    comparison.profile = node.profile;
    comparison.operands = {0U, 1U};
    synthetic.nodes.push_back(std::move(comparison));
    synthetic.root = 2U;
    return comparison_ranges(synthetic, synthetic.nodes[2], variable, width);
  };
  switch (operation) {
  case Operator::Less: return compare(Operator::Less, bound->high);
  case Operator::LessEqual: return compare(Operator::LessEqual, bound->high);
  case Operator::Greater: return compare(Operator::Greater, bound->low);
  case Operator::GreaterEqual:
    return compare(Operator::GreaterEqual, bound->low);
  case Operator::Equal:
  case Operator::CaseEqual: {
    const auto low = compare(Operator::GreaterEqual, bound->low);
    const auto high = compare(Operator::LessEqual, bound->high);
    if (!low || !high) return std::nullopt;
    return intersected(*low, *high);
  }
  default:
    return std::nullopt;
  }
}

[[nodiscard]] std::vector<SystemVerilogConstraintKeyRange> key_ranges(
    const SystemVerilogConstraintExpression& source_expression,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const SystemVerilogConstraintValueIntervals* intervals) {
  const auto expression = normalized_expression(source_expression, variable);
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
      } else if (intervals != nullptr) {
        if (auto bounded = interval_comparison_ranges(
                expression, node, variable, width, *intervals)) {
          results[index] = Analysis{std::move(*bounded), false};
        }
      }
      break;
    default:
      break;
    }
  }
  return operand(expression.root).ranges;
}

[[nodiscard]] std::vector<PackedLogic4> variable_domain(
    const std::span<const SystemVerilogConstraintClause> clauses,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random,
    const SystemVerilogConstraintValueIntervals* intervals) {
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
          key_ranges(
              *clause.expression, variable, profile, intervals));
    }
  }
  if (hard.empty()) return {};
  auto keys = bounded_keys(hard, random);
  // A value the variable is compared equal to under a guard
  // (`if (c) x == 'h333`) is a candidate even when the guard keeps the
  // comparison from bounding the domain (18.5.6, 18.5.7).
  const auto sign_bit = profile.signed_value
      ? std::uint64_t{1} << (profile.width - 1U)
      : std::uint64_t{};
  const auto width_mask = profile.width == 64U
      ? std::numeric_limits<std::uint64_t>::max()
      : (std::uint64_t{1} << profile.width) - 1U;
  for (const auto& clause : clauses) {
    if (!depends(clause)) continue;
    const auto& nodes = clause.expression->nodes;
    const auto is_variable = [&](std::size_t id) {
      for (std::size_t depth = 0; depth < 4U && id < nodes.size(); ++depth) {
        const auto& node = nodes[id];
        if (node.operation == Operator::Variable) {
          return node.variable == variable;
        }
        if (node.operation != Operator::UnaryPlus
            || node.operands.size() != 1U) {
          return false;
        }
        id = node.operands.front();
      }
      return false;
    };
    for (const auto& node : nodes) {
      // A comparison with a constant under a guard (`if (c) {x > 4;
      // x < 7;}`) bounds nothing either; its boundary values and their
      // neighbours are candidates.
      const bool equality = node.operation == Operator::Equal
          || node.operation == Operator::CaseEqual;
      const bool relational = node.operation == Operator::Less
          || node.operation == Operator::LessEqual
          || node.operation == Operator::Greater
          || node.operation == Operator::GreaterEqual
          || node.operation == Operator::NotEqual
          || node.operation == Operator::CaseNotEqual;
      if ((!equality && !relational) || node.operands.size() != 2U) {
        continue;
      }
      for (std::size_t side = 0; side < 2U; ++side) {
        const auto& other = nodes[node.operands[1U - side]];
        if (!is_variable(node.operands[side])
            || other.operation != Operator::Constant
            || other.constant.width() == 0U || other.constant.width() > 64U
            || other.constant.is_logic9()
            || other.constant.low_word().bval != 0U) {
          continue;
        }
        const auto from = other.constant.width();
        auto bits = other.constant.low_word().aval;
        if (from < 64U) {
          bits &= (std::uint64_t{1} << from) - 1U;
          if (other.profile.signed_value
              && ((bits >> (from - 1U)) & 1U) != 0U) {
            bits |= ~((std::uint64_t{1} << from) - 1U);
          }
        }
        const auto key = (bits & width_mask) ^ sign_bit;
        const auto add = [&](const std::uint64_t candidate) {
          if (std::ranges::any_of(hard, [&](const auto& range) {
                return range.low <= candidate && candidate <= range.high;
              })) {
            keys.push_back(candidate);
          }
        };
        add(key);
        if (relational) {
          if (key != 0U) add(key - 1U);
          if (key != width_mask) add(key + 1U);
        }
      }
    }
  }
  for (const auto& clause : clauses) {
    if (!clause.soft || !depends(clause)) continue;
    const auto preferred = intersected(hard,
        key_ranges(
            *clause.expression, variable, profile, intervals));
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

}  // namespace

std::vector<SystemVerilogConstraintKeyRange>
systemverilog_constraint_key_ranges(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile) {
  return key_ranges(expression, variable, profile, nullptr);
}

std::vector<PackedLogic4> systemverilog_constraint_variable_domain(
    const std::span<const SystemVerilogConstraintClause> clauses,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random) {
  return variable_domain(clauses, variable, profile, random, nullptr);
}

std::vector<PackedLogic4> systemverilog_constraint_wide_variable_domain(
    const std::span<const SystemVerilogConstraintClause> clauses,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random) {
  const auto width = profile.width;
  if (width <= 64U || !random) {
    throw std::invalid_argument{
        "a wide constraint domain requires a variable wider than 64 bits"};
  }
  const auto words = (width + 63U) / 64U;
  const auto widened = [&](const std::uint64_t low) {
    std::vector<std::uint64_t> aval(words);
    const std::vector<std::uint64_t> bval(words);
    aval.front() = low;
    return PackedLogic4::from_word_planes(width, aval, bval);
  };
  const auto depends = [&](const SystemVerilogConstraintClause& clause) {
    return clause.expression
        && std::ranges::find(clause.variables, variable)
            != clause.variables.end();
  };
  // A constant whose bits above the low word are zero.
  const auto low_constant = [](const SystemVerilogConstraintExpressionNode& node)
      -> std::optional<std::uint64_t> {
    if (node.operation != Operator::Constant || node.constant.is_logic9()) {
      return std::nullopt;
    }
    const auto aval = node.constant.aval_words();
    const auto bval = node.constant.bval_words();
    if (aval.empty()
        || std::ranges::any_of(bval, [](const auto word) { return word != 0U; })
        || std::ranges::any_of(aval | std::views::drop(1),
               [](const auto word) { return word != 0U; })
        || (node.profile.signed_value && node.constant.width() <= 64U
            && node.constant.width() > 0U
            && ((aval.front() >> (node.constant.width() - 1U)) & 1U) != 0U)) {
      return std::nullopt;
    }
    return aval.front();
  };
  if (!profile.signed_value) {
    // The variable's comparisons with such constants read as 64-bit
    // comparisons: below 2^64 they order values alike.
    std::vector<SystemVerilogConstraintClause> narrowed;
    narrowed.reserve(clauses.size());
    for (const auto& clause : clauses) {
      auto copy = clause;
      if (depends(clause)) {
        auto expression = *clause.expression;
        const auto count = expression.nodes.size();
        for (std::size_t index = 0; index < count; ++index) {
          const auto operation = expression.nodes[index].operation;
          if (operation < Operator::Equal || operation > Operator::GreaterEqual
              || expression.nodes[index].operands.size() != 2U) {
            continue;
          }
          for (std::size_t side = 0; side < 2U; ++side) {
            const auto variable_id = expression.nodes[index].operands[side];
            const auto constant_id
                = expression.nodes[index].operands[1U - side];
            const auto& subject = expression.nodes[variable_id];
            const auto value = low_constant(expression.nodes[constant_id]);
            if (subject.operation != Operator::Variable
                || subject.variable != variable || !value) {
              continue;
            }
            SystemVerilogConstraintExpressionNode constant;
            constant.operation = Operator::Constant;
            constant.profile = expression.nodes[constant_id].profile;
            constant.profile.width = 64U;
            constant.profile.signed_value = false;
            constant.constant = PackedLogic4::from_aval_bval(64U, *value, 0U);
            expression.nodes.push_back(std::move(constant));
            expression.nodes[index].operands[1U - side]
                = expression.nodes.size() - 1U;
          }
        }
        copy.expression = std::make_shared<const SystemVerilogConstraintExpression>(
            std::move(expression));
      }
      narrowed.push_back(std::move(copy));
    }
    auto proxy = profile;
    proxy.width = 64U;
    const auto bounded = variable_domain(narrowed, variable, proxy, random,
        nullptr);
    if (!bounded.empty()
        && bounded.back().low_word().aval
            != std::numeric_limits<std::uint64_t>::max()) {
      std::vector<PackedLogic4> result;
      result.reserve(bounded.size());
      for (const auto& value : bounded) {
        result.push_back(widened(value.low_word().aval));
      }
      return result;
    }
  }
  // The constants the variable is compared equal to, and uniform samples.
  std::vector<PackedLogic4> values;
  for (const auto& clause : clauses) {
    if (!depends(clause)) continue;
    const auto& nodes = clause.expression->nodes;
    for (const auto& node : nodes) {
      if ((node.operation != Operator::Equal
              && node.operation != Operator::CaseEqual)
          || node.operands.size() != 2U) {
        continue;
      }
      for (std::size_t side = 0; side < 2U; ++side) {
        const auto& subject = nodes[node.operands[side]];
        const auto& other = nodes[node.operands[1U - side]];
        if (subject.operation != Operator::Variable
            || subject.variable != variable
            || other.operation != Operator::Constant
            || other.constant.is_logic9() || other.constant.empty()) {
          continue;
        }
        const auto& constant = other.constant;
        const auto extension = other.profile.signed_value
            ? constant.get(constant.width() - 1U) : Logic4::zero;
        PackedLogic4 value(width, extension);
        for (std::size_t bit = 0; bit < std::min(width, constant.width());
             ++bit) {
          value.set(bit, constant.get(bit));
        }
        values.push_back(std::move(value));
      }
    }
  }
  for (std::size_t sample = 0; sample < 16U; ++sample) {
    std::vector<std::uint64_t> aval(words);
    const std::vector<std::uint64_t> bval(words);
    for (auto& word : aval) word = random();
    values.push_back(PackedLogic4::from_word_planes(width, aval, bval));
  }
  std::vector<PackedLogic4> unique;
  for (auto& value : values) {
    if (std::ranges::find(unique, value) == unique.end()) {
      unique.push_back(std::move(value));
    }
  }
  return unique;
}

std::optional<SystemVerilogConstraintValueInterval>
systemverilog_constraint_value_interval(
    const std::span<const PackedLogic4> domain,
    const SystemVerilogConstraintVariableProfile& profile) {
  const auto width = profile.width;
  if (domain.empty() || width == 0U || width > 64U) return std::nullopt;
  std::optional<SystemVerilogConstraintValueInterval> result;
  for (const auto& value : domain) {
    if (value.width() != width || value.is_logic9()
        || value.low_word().bval != 0U) {
      return std::nullopt;
    }
    auto bits = value.low_word().aval;
    Wide number{};
    if (width < 64U) bits &= (std::uint64_t{1} << width) - 1U;
    if (profile.signed_value && ((bits >> (width - 1U)) & 1U) != 0U) {
      number = static_cast<Wide>(bits) - (Wide{1} << width);
    } else {
      number = static_cast<Wide>(bits);
    }
    if (!result) {
      result = SystemVerilogConstraintValueInterval{number, number};
    } else {
      result->low = std::min(result->low, number);
      result->high = std::max(result->high, number);
    }
  }
  return result;
}

std::vector<PackedLogic4> systemverilog_constraint_variable_domain_with_fixed(
    const std::span<const SystemVerilogConstraintClause> clauses,
    const SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random,
    const std::map<SystemVerilogConstraintVariableId, PackedLogic4>& fixed,
    const SystemVerilogConstraintValueIntervals* intervals) {
  if (fixed.empty()) {
    return variable_domain(clauses, variable, profile, random, intervals);
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
      // A substituted state value read through a profile conversion
      // (UnaryPlus) is itself a constant of the conversion's profile, so
      // `x == state` bounds `x` like `x == 10` (18.5).
      for (bool folded = true; folded;) {
        folded = false;
        for (auto& node : expression.nodes) {
          if (node.operation != Operator::UnaryPlus
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
          if (to < 64U) bits &= (std::uint64_t{1} << to) - 1U;
          node.constant = PackedLogic4::from_aval_bval(to, bits, 0U);
          node.operation = Operator::Constant;
          node.operands.clear();
          folded = true;
        }
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
  return variable_domain(substituted, variable, profile, random, intervals);
}

}  // namespace fsim::runtime

// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

struct ChoiceInterval {
  std::int64_t lower{};
  std::int64_t upper{};
};

[[nodiscard]] bool is_case_range(const Expression& choice) {
  return choice.kind == ExpressionKind::Call
      && (choice.text == "@vhdl-case-range-to"
          || choice.text == "@vhdl-case-range-downto");
}

[[nodiscard]] std::optional<std::uint64_t> known_binary_value(
    const PackedLogic4& value) {
  if (value.width() > 64) {
    return std::nullopt;
  }
  std::uint64_t result = 0;
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    const auto state = value.get_logic9(bit);
    if (state == runtime::Logic9::one || state == runtime::Logic9::h) {
      result |= std::uint64_t{1} << bit;
    } else if (state != runtime::Logic9::zero
               && state != runtime::Logic9::l) {
      return std::nullopt;
    }
  }
  return result;
}

[[nodiscard]] bool intervals_cover(
    std::vector<ChoiceInterval> intervals,
    const std::int64_t lower,
    const std::int64_t upper) {
  std::ranges::sort(intervals, {}, &ChoiceInterval::lower);
  auto expected = lower;
  for (const auto& interval : intervals) {
    if (interval.upper < expected) {
      continue;
    }
    if (interval.lower > expected) {
      return false;
    }
    if (interval.upper >= upper) {
      return true;
    }
    expected = interval.upper + 1;
  }
  return false;
}

}  // namespace

std::optional<std::int64_t> Lowerer::vhdl_case_choice_ordinal(
    const Expression& expression,
    const frontend::Type* selector_type,
    const frontend::ValueDomain selector_domain) {
  if (selector_type != nullptr
      && !selector_type->enumeration_literals.empty()) {
    return vhdl_enumeration_ordinal(expression, *selector_type);
  }
  if (selector_domain == frontend::ValueDomain::Boolean) {
    return expression.kind == ExpressionKind::BooleanLiteral
        ? std::optional<std::int64_t>{
              expression.text == "true" ? 1 : 0}
        : std::nullopt;
  }
  if (selector_domain != frontend::ValueDomain::Integer) {
    return std::nullopt;
  }
  return static_integer_value(expression);
}

bool Lowerer::validate_vhdl_case_choices(
    const Statement& statement,
    const frontend::Type* selector_type,
    const std::size_t selector_width,
    const frontend::ValueDomain selector_domain) {
  const bool enumeration = selector_type != nullptr
      && !selector_type->enumeration_literals.empty();
  const bool boolean = selector_domain == frontend::ValueDomain::Boolean;
  const bool integer = selector_domain == frontend::ValueDomain::Integer;
  const bool ordinal = enumeration || boolean || integer;
  std::optional<std::int64_t> domain_lower;
  std::optional<std::int64_t> domain_upper;
  if (enumeration) {
    const auto range = selector_type->enumeration_range.value_or(
        frontend::EnumerationRange{
            0,
            static_cast<std::int64_t>(
                selector_type->enumeration_literals.size() - 1U),
            false});
    domain_lower = std::min(range.left, range.right);
    domain_upper = std::max(range.left, range.right);
  } else if (boolean) {
    domain_lower = 0;
    domain_upper = 1;
  } else if (integer) {
    const auto range = selector_type != nullptr
            && selector_type->integer_range
        ? *selector_type->integer_range
        : frontend::vhdl_predefined_integer_range(
              vhdl_standard_, "integer");
    domain_lower = std::min(range.left, range.right);
    domain_upper = std::max(range.left, range.right);
  } else if (selector_domain == frontend::ValueDomain::Bit2
             && selector_width < 64) {
    domain_lower = 0;
    domain_upper = selector_width == 63
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(
              (std::uint64_t{1} << selector_width) - 1U);
  }

  std::vector<ChoiceInterval> intervals;
  std::unordered_set<std::string> packed_choices;
  bool valid = true;
  bool has_default = false;
  for (const auto& alternative : statement.case_alternatives) {
    if (alternative.is_default) {
      has_default = true;
      continue;
    }
    for (const auto& choice : alternative.choices) {
      std::optional<ChoiceInterval> interval;
      if (is_case_range(choice)) {
        if (!ordinal || choice.operands.size() != 2) {
          report(
              "FSIM-ELAB-VHDLCASE-001",
              "a VHDL case range requires two locally static bounds of "
              "the selector's discrete type",
              choice.span);
          valid = false;
          continue;
        }
        const auto left = vhdl_case_choice_ordinal(
            choice.operands[0], selector_type, selector_domain);
        const auto right = vhdl_case_choice_ordinal(
            choice.operands[1], selector_type, selector_domain);
        if (!left || !right) {
          report(
              "FSIM-ELAB-VHDLCASE-001",
              "VHDL case range bounds must be locally static values of "
              "the selector's discrete type",
              choice.span);
          valid = false;
          continue;
        }
        const bool descending =
            choice.text == "@vhdl-case-range-downto";
        const bool empty = descending ? *left < *right : *left > *right;
        if (empty) {
          continue;
        }
        interval = ChoiceInterval{
            std::min(*left, *right), std::max(*left, *right)};
      } else if (ordinal) {
        const auto value = vhdl_case_choice_ordinal(
            choice, selector_type, selector_domain);
        if (!value) {
          report(
              "FSIM-ELAB-VHDLCASE-001",
              "VHDL case choices must be locally static values of the "
              "selector's discrete type",
              choice.span);
          valid = false;
          continue;
        }
        interval = ChoiceInterval{*value, *value};
      } else {
        const auto literal = literal_value(
            choice, selector_width, frontend::Language::Vhdl2008);
        if (!literal || literal->value.width() != selector_width
            || (selector_domain == frontend::ValueDomain::Bit2
                && literal->domain != frontend::ValueDomain::Bit2)) {
          report(
              "FSIM-ELAB-VHDLCASE-001",
              "a packed VHDL case choice must be a locally static literal "
              "of the selector width and element domain",
              choice.span);
          valid = false;
          continue;
        }
        const auto key = literal->value.to_msb_string();
        if (!packed_choices.insert(key).second) {
          report(
              "FSIM-ELAB-VHDLCASE-003",
              "a VHDL case statement repeats the same discrete choice",
              choice.span);
          valid = false;
          continue;
        }
        if (selector_domain == frontend::ValueDomain::Bit2
            && selector_width < 64) {
          const auto value = known_binary_value(literal->value);
          if (value
              && *value <= static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max())) {
            interval = ChoiceInterval{
                static_cast<std::int64_t>(*value),
                static_cast<std::int64_t>(*value)};
          }
        }
      }

      if (!interval) {
        continue;
      }
      if (domain_lower
          && (interval->lower < *domain_lower
              || interval->upper > *domain_upper)) {
        report(
            "FSIM-ELAB-VHDLCASE-002",
            "a VHDL case choice lies outside the selector subtype range",
            choice.span);
        valid = false;
      }
      const auto overlap = std::ranges::find_if(
          intervals,
          [&](const ChoiceInterval& prior) {
            return interval->lower <= prior.upper
                && prior.lower <= interval->upper;
          });
      if (overlap != intervals.end()) {
        const bool duplicate = overlap->lower == interval->lower
            && overlap->upper == interval->upper;
        report(
            duplicate ? "FSIM-ELAB-VHDLCASE-003"
                      : "FSIM-ELAB-VHDLCASE-004",
            duplicate
                ? "a VHDL case statement repeats the same discrete choice"
                : "VHDL case choices or ranges overlap",
            choice.span);
        valid = false;
      } else {
        intervals.push_back(*interval);
      }
    }
  }

  if (!valid || has_default) {
    return valid;
  }
  bool complete = false;
  if (domain_lower && domain_upper) {
    complete = intervals_cover(
        intervals, *domain_lower, *domain_upper);
  } else if (selector_domain == frontend::ValueDomain::Logic9
             && selector_width <= 6) {
    std::size_t domain_size = 1;
    for (std::size_t bit = 0; bit < selector_width; ++bit) {
      domain_size *= 9;
    }
    complete = packed_choices.size() == domain_size;
  }
  if (!complete) {
    report(
        "FSIM-ELAB-VHDLCASE-005",
        "a VHDL case statement does not cover its complete selector "
        "subtype and requires an others alternative",
        statement.span);
    return false;
  }
  return true;
}

std::optional<RegisterId> Lowerer::lower_vhdl_case_range_condition(
    const Expression& choice,
    const RegisterId selector,
    const frontend::Type* selector_type,
    const bool selector_signed) {
  if (choice.operands.size() != 2) {
    report(
        "FSIM-ELAB-VHDLCASE-006",
        "a retained VHDL case range does not have two bounds",
        choice.span);
    return std::nullopt;
  }
  const auto domain = register_domain(selector);
  const auto left = vhdl_case_choice_ordinal(
      choice.operands[0], selector_type, domain);
  const auto right = vhdl_case_choice_ordinal(
      choice.operands[1], selector_type, domain);
  if (!left || !right) {
    report(
        "FSIM-ELAB-VHDLCASE-006",
        "a validated VHDL case range lost its static bounds",
        choice.span);
    return std::nullopt;
  }
  const bool descending = choice.text == "@vhdl-case-range-downto";
  const bool empty = descending ? *left < *right : *left > *right;
  const auto result = allocate_register(
      1, frontend::ValueDomain::Bit2);
  if (empty) {
    process_.operations.emplace_back(
        LoadConstant{result, PackedLogic4(1, Logic4::zero)});
    return result;
  }

  const auto width = register_width(selector);
  const auto load_bound = [&](const std::int64_t value) {
    const auto target = allocate_register(width, domain);
    process_.operations.emplace_back(LoadConstant{
        target,
        domain == frontend::ValueDomain::Integer
            ? integer_value(value, width)
            : unsigned_value(static_cast<std::uint64_t>(value), width)});
    return target;
  };
  const auto low = load_bound(std::min(*left, *right));
  const auto high = load_bound(std::max(*left, *right));
  const auto above_low = allocate_register(
      1, frontend::ValueDomain::Bit2);
  const auto below_high = allocate_register(
      1, frontend::ValueDomain::Bit2);
  const bool signed_comparison = selector_signed
      || domain == frontend::ValueDomain::Integer;
  process_.operations.emplace_back(Binary{
      signed_comparison
          ? BinaryOperator::greater_equal_signed
          : BinaryOperator::greater_equal_unsigned,
      above_low,
      selector,
      low});
  process_.operations.emplace_back(Binary{
      signed_comparison
          ? BinaryOperator::less_equal_signed
          : BinaryOperator::less_equal_unsigned,
      below_high,
      selector,
      high});
  process_.operations.emplace_back(LogicalBinary{
      LogicalBinaryOperator::logical_and,
      result,
      above_low,
      below_high});
  return result;
}

}  // namespace fsim::elaboration

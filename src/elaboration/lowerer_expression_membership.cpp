// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

[[maybe_unused]] constexpr std::string_view
    kRetiredExactPatternProfileDiagnostic = "FSIM-ELAB-SVMATCH-004";
[[maybe_unused]] constexpr std::string_view
    kRetiredExactMembershipProfileDiagnostic = "FSIM-ELAB-SVMEMBER-005";

std::optional<Lowerer::InsideIntegralOperand>
Lowerer::lower_inside_integral_operand(
    const Expression& expression,
    const std::string_view diagnostic_code,
    const std::string_view diagnostic_message)
{
    if (is_container_expression(expression)
        || is_string_expression(expression)
        || expression.kind == ExpressionKind::Aggregate
        || (expression.kind == ExpressionKind::Call
            && expression.text == "@inside-range")) {
        report(
            std::string(diagnostic_code),
            std::string(diagnostic_message),
            expression.span);
        return std::nullopt;
    }
    const auto width = infer_width(expression);
    if (!width || *width == 0) {
        report(
            std::string(diagnostic_code),
            "an inside operand must have a statically inferable, nonzero "
            "integral width",
            expression.span);
        return std::nullopt;
    }
    const auto value = lower_expression(expression, *width);
    if (!value) {
        return std::nullopt;
    }
    return InsideIntegralOperand {
        *value,
        register_width(*value),
        is_signed_expression(expression)
    };
}

Lowerer::SizedIntegralComparison Lowerer::size_integral_comparison(
    InsideIntegralOperand lhs,
    InsideIntegralOperand rhs)
{
    const auto width = std::max(lhs.width, rhs.width);
    const bool signed_value = lhs.signed_value && rhs.signed_value;
    if (lhs.width != width) {
        lhs.value = resize_register(lhs.value, width, signed_value);
    }
    if (rhs.width != width) {
        rhs.value = resize_register(rhs.value, width, signed_value);
    }
    return SizedIntegralComparison {
        lhs.value,
        rhs.value,
        signed_value
    };
}

std::optional<Lowerer::CasePatternMatch>
Lowerer::lower_case_match_pattern(
    const Expression& pattern,
    const RegisterId value,
    const std::size_t width,
    const bool signed_value,
    const frontend::Type* type)
{
    const auto make_true = [&]() {
        const auto result = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { result, PackedLogic4(1, Logic4::one) });
        return result;
    };
    const auto combine = [&](const RegisterId left, const RegisterId right) {
        const auto result = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::bit_and, result, left, right });
        return result;
    };
    if (pattern.kind == ExpressionKind::Call
        && pattern.text == "@match-wildcard") {
        return CasePatternMatch { make_true(), { } };
    }
    constexpr std::string_view bind_prefix { "@match-bind:" };
    if (pattern.kind == ExpressionKind::Call
        && pattern.text.starts_with(bind_prefix)) {
        const auto name = pattern.text.substr(bind_prefix.size());
        if (name.empty()) {
            report(
                "FSIM-ELAB-SVMATCH-007",
                "a case matches binding pattern requires a name",
                pattern.span);
            return std::nullopt;
        }
        return CasePatternMatch {
            make_true(),
            { CasePatternBinding {
                name,
                value,
                signed_value,
                type != nullptr ? type->packed_range : std::nullopt,
                type != nullptr ? type->integer_range : std::nullopt,
                type != nullptr
                    ? type->packed_members
                    : std::vector<frontend::PackedMember> { },
                type } }
        };
    }
    constexpr std::string_view tagged_prefix { "@match-tagged:" };
    if (pattern.kind == ExpressionKind::Call
        && pattern.text.starts_with(tagged_prefix)) {
        if (type == nullptr
            || type->packed_aggregate
                != frontend::PackedAggregateKind::TaggedUnion
            || type->packed_members.empty()
            || pattern.operands.size() > 1U) {
            report(
                "FSIM-ELAB-SVMATCH-008",
                "a tagged case pattern requires a tagged-union selector and at "
                "most one nested pattern",
                pattern.span);
            return std::nullopt;
        }
        const auto member_name = pattern.text.substr(tagged_prefix.size());
        const auto member = std::ranges::find_if(
            type->packed_members,
            [&](const frontend::PackedMember& candidate) {
                return candidate.name == member_name;
            });
        if (member == type->packed_members.end()) {
            report(
                "FSIM-ELAB-SVMATCH-008",
                "tagged case pattern names unknown member '" + member_name + "'",
                pattern.span);
            return std::nullopt;
        }
        const auto member_index = static_cast<std::size_t>(
            std::distance(type->packed_members.begin(), member));
        const auto member_width = member->width();
        const auto tag_width = std::max<std::size_t>(
            1U, static_cast<std::size_t>(
                    std::bit_width(type->packed_members.size() - 1U)));
        if (!member_width || *member_width == 0U
            || *member_width > std::numeric_limits<std::uint32_t>::max()
            || member->lsb_offset > std::numeric_limits<std::uint32_t>::max()
            || tag_width > width
            || width - tag_width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-SVMATCH-008",
                "tagged case pattern member has no executable packed layout",
                pattern.span);
            return std::nullopt;
        }
        const auto actual_tag = allocate_register(
            tag_width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Extract {
            actual_tag,
            value,
            static_cast<std::uint32_t>(width - tag_width),
            static_cast<std::uint32_t>(tag_width) });
        const auto expected_tag = allocate_register(
            tag_width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            expected_tag, unsigned_value(member_index, tag_width) });
        const auto tag_matches = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal,
            tag_matches,
            actual_tag,
            expected_tag });
        if (pattern.operands.empty()) {
            return CasePatternMatch { tag_matches, { } };
        }
        const auto member_value = allocate_register(
            *member_width, member->domain);
        process_.operations.emplace_back(Extract {
            member_value,
            value,
            static_cast<std::uint32_t>(member->lsb_offset),
            static_cast<std::uint32_t>(*member_width) });
        const frontend::Type* member_type = member->nested_types.empty()
            ? nullptr
            : &member->nested_types.front();
        auto nested = lower_case_match_pattern(
            pattern.operands.front(),
            member_value,
            static_cast<std::size_t>(*member_width),
            member->is_signed,
            member_type);
        if (!nested) {
            return std::nullopt;
        }
        nested->condition = combine(tag_matches, nested->condition);
        return nested;
    }
    if (pattern.kind == ExpressionKind::Aggregate
        && pattern.text == "@match-structure") {
        if (type == nullptr
            || (type->packed_aggregate
                    != frontend::PackedAggregateKind::Struct
                && type->packed_aggregate
                    != frontend::PackedAggregateKind::Union)
            || pattern.operands.empty()
            || pattern.aggregate_choices.size() != pattern.operands.size()
            || pattern.aggregate_choice_expressions.size()
                != pattern.operands.size()) {
            report(
                "FSIM-ELAB-SVMATCH-009",
                "a structured case pattern requires compatible packed aggregate "
                "metadata",
                pattern.span);
            return std::nullopt;
        }
        auto result = CasePatternMatch { make_true(), { } };
        std::vector<bool> selected(type->packed_members.size());
        const auto named = pattern.aggregate_choices.front() == "@key";
        if (std::ranges::any_of(
                pattern.aggregate_choices,
                [named](const std::string& choice) {
                    return (choice == "@key") != named;
                })
            || (!named
                && pattern.operands.size() != type->packed_members.size())) {
            report(
                "FSIM-ELAB-SVMATCH-009",
                "structured case patterns cannot mix positional and named members, "
                "and positional patterns must cover every member",
                pattern.span);
            return std::nullopt;
        }
        std::size_t positional = 0;
        for (std::size_t index = 0; index < pattern.operands.size(); ++index) {
            std::size_t member_index = positional++;
            if (pattern.aggregate_choices[index] == "@key") {
                const auto& choices = pattern.aggregate_choice_expressions[index];
                if (choices.size() != 1U
                    || choices.front().kind != ExpressionKind::Identifier) {
                    report(
                        "FSIM-ELAB-SVMATCH-009",
                        "a named structured case pattern requires a direct member",
                        pattern.operands[index].span);
                    return std::nullopt;
                }
                const auto member = std::ranges::find_if(
                    type->packed_members,
                    [&](const frontend::PackedMember& candidate) {
                        return candidate.name == choices.front().text;
                    });
                if (member == type->packed_members.end()) {
                    report(
                        "FSIM-ELAB-SVMATCH-009",
                        "structured case pattern names unknown member '"
                            + choices.front().text + "'",
                        choices.front().span);
                    return std::nullopt;
                }
                member_index = static_cast<std::size_t>(
                    std::distance(type->packed_members.begin(), member));
            } else if (!pattern.aggregate_choices[index].empty()) {
                report(
                    "FSIM-ELAB-SVMATCH-009",
                    "structured case patterns accept positional or member keys",
                    pattern.operands[index].span);
                return std::nullopt;
            }
            if (member_index >= type->packed_members.size()
                || selected[member_index]) {
                report(
                    "FSIM-ELAB-SVMATCH-009",
                    "structured case pattern has too many or duplicate members",
                    pattern.operands[index].span);
                return std::nullopt;
            }
            selected[member_index] = true;
            const auto& member = type->packed_members[member_index];
            const auto member_width = member.width();
            if (!member_width || *member_width == 0U
                || *member_width > std::numeric_limits<std::uint32_t>::max()
                || member.lsb_offset > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVMATCH-009",
                    "structured case pattern member has no executable layout",
                    pattern.operands[index].span);
                return std::nullopt;
            }
            const auto member_value = allocate_register(
                *member_width, member.domain);
            process_.operations.emplace_back(Extract {
                member_value,
                value,
                static_cast<std::uint32_t>(member.lsb_offset),
                static_cast<std::uint32_t>(*member_width) });
            const frontend::Type* member_type = member.nested_types.empty()
                ? nullptr
                : &member.nested_types.front();
            auto nested = lower_case_match_pattern(
                pattern.operands[index],
                member_value,
                static_cast<std::size_t>(*member_width),
                member.is_signed,
                member_type);
            if (!nested) {
                return std::nullopt;
            }
            result.condition = combine(result.condition, nested->condition);
            result.bindings.insert(
                result.bindings.end(),
                std::make_move_iterator(nested->bindings.begin()),
                std::make_move_iterator(nested->bindings.end()));
        }
        std::unordered_set<std::string> names;
        for (const auto& binding : result.bindings) {
            if (!names.insert(binding.name).second) {
                report(
                    "FSIM-ELAB-SVMATCH-007",
                    "case pattern binds '" + binding.name + "' more than once",
                    pattern.span);
                return std::nullopt;
            }
        }
        return result;
    }
    if (!is_bounded_case_pattern_constant(pattern)) {
        report(
            "FSIM-ELAB-SVMATCH-003",
            "case matches pattern is not an integral constant, wildcard, "
            "binding, tagged, or structured pattern",
            pattern.span);
        return std::nullopt;
    }
    const auto constant = lower_inside_integral_operand(
        pattern,
        "FSIM-ELAB-SVMATCH-003",
        "case matches constant patterns must be integral expressions");
    if (!constant) {
        return std::nullopt;
    }
    const auto comparison = size_integral_comparison(
        InsideIntegralOperand { value, width, signed_value }, *constant);
    const auto matched = allocate_register(
        1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::case_equal,
        matched,
        comparison.lhs,
        comparison.rhs });
    return CasePatternMatch { matched, { } };
}

Lowerer::ExpressionAttempt Lowerer::lower_membership_expression(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Call
      || expression.text != "inside") {
    return ExpressionAttempt{};
  }
  if (language_ != frontend::Language::SystemVerilog2017) {
    report(
        "FSIM-ELAB-SVMEMBER-001",
        "the inside membership operator requires SystemVerilog",
        expression.span);
    return std::nullopt;
  }
  if (expression.operands.size() < 2) {
    report(
        "FSIM-ELAB-SVMEMBER-002",
        "an inside expression requires a left operand and a nonempty list",
        expression.span);
    return std::nullopt;
  }
  const auto& lhs_expression = expression.operands.front();
  if (is_container_expression(lhs_expression)
      || is_string_expression(lhs_expression)
      || lhs_expression.kind == ExpressionKind::Aggregate) {
      report(
          "FSIM-ELAB-SVMEMBER-003",
          "bounded inside membership requires scalar integral operands",
          lhs_expression.span);
      return std::nullopt;
  }
  const auto lhs_width = infer_width(lhs_expression);
  if (!lhs_width || *lhs_width == 0) {
    report(
        "FSIM-ELAB-SVMEMBER-003",
        "the inside left operand width is not statically inferable",
        lhs_expression.span);
    return std::nullopt;
  }
  const auto lhs = lower_expression(lhs_expression, *lhs_width);
  if (!lhs) {
    return std::nullopt;
  }
  const bool lhs_signed = is_signed_expression(lhs_expression);
  const InsideIntegralOperand lhs_operand {
      *lhs, register_width(*lhs), lhs_signed
  };
  const auto result_domain = frontend::ValueDomain::Logic4;
  const auto result = allocate_register(1, result_domain);
  process_.operations.emplace_back(LoadConstant{
      result, PackedLogic4(1, Logic4::zero)});
  std::vector<InstructionIndex> matched_branches;

  for (std::size_t item_index = 1;
       item_index < expression.operands.size(); ++item_index) {
    const auto& item = expression.operands[item_index];
    std::optional<RegisterId> matched;
    if (item.kind == ExpressionKind::Call
        && (item.text == "@inside-range"
            || item.text == "@inside-absolute-tolerance"
            || item.text == "@inside-relative-tolerance")) {
      if (item.operands.size() != 2) {
        report(
            "FSIM-ELAB-SVMEMBER-006",
            "an inside range requires exactly one low and high bound",
            item.span);
        return std::nullopt;
      }
      const bool tolerance_range = item.text != "@inside-range";
      if (tolerance_range
          && systemverilog_standard_
              != frontend::StandardRevision::SystemVerilog2023) {
        report(
            "FSIM-ELAB-SVTOLERANCE-001",
            "inside tolerance ranges require the exact SystemVerilog-2023 profile",
            item.span);
        return std::nullopt;
      }
      auto low = lower_inside_integral_operand(
          item.operands[0],
          "FSIM-ELAB-SVMEMBER-004",
          "inside range bounds must be integral expressions");
      auto high = lower_inside_integral_operand(
          item.operands[1],
          "FSIM-ELAB-SVMEMBER-004",
          "inside range bounds must be integral expressions");
      if (!low || !high) {
        return std::nullopt;
      }
      if (tolerance_range) {
        auto tolerance = high->value;
        if (register_width(tolerance) != low->width) {
          tolerance = resize_register(
              tolerance, low->width, high->signed_value);
        }
        if (item.text == "@inside-relative-tolerance") {
          const auto arithmetic_width = std::max<std::size_t>(
              64U, low->width > std::numeric_limits<std::size_t>::max() / 2U
                  ? low->width : low->width * 2U);
          auto center = low->value;
          if (register_width(center) != arithmetic_width) {
            center = resize_register(
                center, arithmetic_width, low->signed_value);
          }
          if (register_width(tolerance) != arithmetic_width) {
            tolerance = resize_register(
                tolerance, arithmetic_width, low->signed_value);
          }
          const auto product = allocate_register(
              arithmetic_width, register_domain(center));
          process_.operations.emplace_back(Binary {
              low->signed_value
                  ? BinaryOperator::multiply_signed
                  : BinaryOperator::multiply_unsigned,
              product, center, tolerance });
          const auto hundred = allocate_register(
              arithmetic_width, register_domain(center));
          process_.operations.emplace_back(LoadConstant {
              hundred, unsigned_value(100U, arithmetic_width) });
          const auto quotient = allocate_register(
              arithmetic_width, register_domain(center));
          process_.operations.emplace_back(Binary {
              low->signed_value
                  ? BinaryOperator::divide_signed
                  : BinaryOperator::divide_unsigned,
              quotient, product, hundred });
          tolerance = resize_register(
              quotient, low->width, low->signed_value);
        }
        const auto lower = allocate_register(
            low->width, register_domain(low->value));
        const auto upper = allocate_register(
            low->width, register_domain(low->value));
        process_.operations.emplace_back(Binary {
            low->signed_value
                ? BinaryOperator::subtract_signed
                : BinaryOperator::subtract_unsigned,
            lower, low->value, tolerance });
        process_.operations.emplace_back(Binary {
            low->signed_value
                ? BinaryOperator::add_signed
                : BinaryOperator::add_unsigned,
            upper, low->value, tolerance });
        low = InsideIntegralOperand {
            lower, low->width, low->signed_value };
        high = InsideIntegralOperand {
            upper, low->width, low->signed_value };
      }
      const auto valid_operands = size_integral_comparison(*low, *high);
      const auto low_operands = size_integral_comparison(lhs_operand, *low);
      const auto high_operands = size_integral_comparison(lhs_operand, *high);
      const auto valid = allocate_register(1, result_domain);
      const auto above_low = allocate_register(1, result_domain);
      const auto below_high = allocate_register(1, result_domain);
      const auto within_lower = allocate_register(1, result_domain);
      matched = allocate_register(1, result_domain);
      process_.operations.emplace_back(Binary {
          valid_operands.signed_value
              ? BinaryOperator::less_equal_signed
              : BinaryOperator::less_equal_unsigned,
          valid, valid_operands.lhs, valid_operands.rhs });
      process_.operations.emplace_back(Binary {
          low_operands.signed_value
              ? BinaryOperator::greater_equal_signed
              : BinaryOperator::greater_equal_unsigned,
          above_low, low_operands.lhs, low_operands.rhs });
      process_.operations.emplace_back(Binary {
          high_operands.signed_value
              ? BinaryOperator::less_equal_signed
              : BinaryOperator::less_equal_unsigned,
          below_high, high_operands.lhs, high_operands.rhs });
      process_.operations.emplace_back(LogicalBinary{
          LogicalBinaryOperator::logical_and,
          within_lower, valid, above_low});
      process_.operations.emplace_back(LogicalBinary{
          LogicalBinaryOperator::logical_and,
          *matched, within_lower, below_high});
      if (tolerance_range) {
        const auto above_swapped = allocate_register(1, result_domain);
        const auto below_swapped = allocate_register(1, result_domain);
        const auto within_swapped = allocate_register(1, result_domain);
        const auto swapped = allocate_register(1, result_domain);
        process_.operations.emplace_back(Binary {
            high_operands.signed_value
                ? BinaryOperator::greater_equal_signed
                : BinaryOperator::greater_equal_unsigned,
            above_swapped, high_operands.lhs, high_operands.rhs });
        process_.operations.emplace_back(Binary {
            low_operands.signed_value
                ? BinaryOperator::less_equal_signed
                : BinaryOperator::less_equal_unsigned,
            below_swapped, low_operands.lhs, low_operands.rhs });
        process_.operations.emplace_back(LogicalBinary {
            LogicalBinaryOperator::logical_and,
            within_swapped, above_swapped, below_swapped });
        process_.operations.emplace_back(LogicalBinary {
            LogicalBinaryOperator::logical_or,
            swapped, *matched, within_swapped });
        matched = swapped;
      }
    } else {
        const auto value = lower_inside_integral_operand(
            item,
            "FSIM-ELAB-SVMEMBER-004",
            "inside members must be integral expressions");
        if (!value) {
            return std::nullopt;
        }
        const auto operands = size_integral_comparison(lhs_operand, *value);
        matched = allocate_register(1, result_domain);
        process_.operations.emplace_back(Binary {
            BinaryOperator::wildcard_equal,
            *matched, operands.lhs, operands.rhs });
    }
    const auto accumulated = allocate_register(1, result_domain);
    process_.operations.emplace_back(LogicalBinary{
        LogicalBinaryOperator::logical_or,
        accumulated, result, *matched});
    process_.operations.emplace_back(CopyRegister{result, accumulated});
    if (item_index + 1 < expression.operands.size()) {
      const auto branch = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Branch{
          result, 0, branch + 1,
          UnknownBranchPolicy::when_false});
      matched_branches.push_back(branch);
    }
  }
  const auto end = static_cast<InstructionIndex>(
      process_.operations.size());
  for (const auto branch : matched_branches) {
    process_.operations[branch] = Branch{
        result, end, branch + 1,
        UnknownBranchPolicy::when_false};
  }
  return result;
}

}  // namespace fsim::elaboration

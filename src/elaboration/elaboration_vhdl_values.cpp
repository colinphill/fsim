// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

std::optional<std::int64_t> vhdl_physical_literal_value(
    const Expression& expression,
    const frontend::Type& type,
    std::string& error) {
    constexpr std::string_view prefix{"@vhdl-physical:"};
    if (!type.vhdl_physical
        || expression.kind != ExpressionKind::Call
        || !expression.text.starts_with(prefix)
        || expression.operands.size() != 1) {
        return std::nullopt;
    }
    const auto unit_name = expression.text.substr(prefix.size());
    const auto unit = std::ranges::find(
        type.vhdl_physical->units,
        unit_name,
        &frontend::VhdlPhysicalUnit::name);
    std::string magnitude_error;
    const auto magnitude = evaluate_constant_expression(
        expression.operands.front(), {}, magnitude_error);
    if (unit == type.vhdl_physical->units.end()
        || !unit->scale_factor || !magnitude) {
        error = unit == type.vhdl_physical->units.end()
            ? "physical unit '" + unit_name
                + "' is not declared by type '" + type.spelling + "'"
            : "physical literal magnitude is not locally static";
        return std::nullopt;
    }
    if (*magnitude != 0
        && (*magnitude > 0
                ? (*unit->scale_factor
                    > std::numeric_limits<std::int64_t>::max()
                        / *magnitude)
                : (*magnitude
                    < std::numeric_limits<std::int64_t>::min()
                        / *unit->scale_factor))) {
        error = "physical literal overflows its signed runtime representation";
        return std::nullopt;
    }
    const auto value = *magnitude * *unit->scale_factor;
    if (type.integer_range && !type.integer_range->contains(value)) {
        error = "physical literal is outside the declared type range";
        return std::nullopt;
    }
    return value;
}

std::optional<PackedLogic4> static_vhdl_value(
    const Expression& expression,
    const frontend::Type& type,
    std::string& error) {
    const auto width_value = type.width().value_or(1);
    if (width_value == 0
        || width_value
            > std::numeric_limits<std::size_t>::max()) {
        error = "the contextual type has no representable non-null width";
        return std::nullopt;
    }
    const auto width = static_cast<std::size_t>(width_value);
    const auto normalize =
        [&](PackedLogic4 value)
            -> std::optional<PackedLogic4> {
          if (value.width() != width) {
              error = "the default value width differs from its port type";
              return std::nullopt;
          }
          if (type.domain == frontend::ValueDomain::Logic9
              && !value.is_logic9()) {
              value = value.promoted_to_logic9();
          }
          if (is_two_state_domain(type.domain)) {
              for (std::size_t bit = 0; bit < value.width(); ++bit) {
                  const auto digit = value.get(bit);
                  if (digit == Logic4::x || digit == Logic4::z) {
                      error = "a two-state port default contains an "
                              "unknown or high-impedance digit";
                      return std::nullopt;
                  }
              }
          }
          return value;
        };

    if (expression.kind != ExpressionKind::Aggregate) {
        std::string physical_error;
        if (const auto physical = vhdl_physical_literal_value(
                expression, type, physical_error)) {
            return normalize(unsigned_value(
                static_cast<std::uint64_t>(*physical), width));
        }
        if (type.vhdl_physical
            && expression.kind == ExpressionKind::Call
            && expression.text.starts_with("@vhdl-physical:")) {
            error = std::move(physical_error);
            return std::nullopt;
        }
        if (const auto ordinal =
                vhdl_enumeration_ordinal(expression, type)) {
            return normalize(unsigned_value(
                static_cast<std::uint64_t>(*ordinal), width));
        }
        if (auto literal = literal_value(
                expression,
                width,
                frontend::Language::Vhdl2008)) {
            return normalize(std::move(literal->value));
        }
        std::string evaluation_error;
        if (const auto value =
                evaluate_constant_expression(
                    expression, {}, evaluation_error)) {
            return normalize(
                type.domain == frontend::ValueDomain::Integer
                    ? unsigned_value(
                          static_cast<std::uint64_t>(*value), width)
                    : unsigned_value(
                          static_cast<std::uint64_t>(*value),
                          width));
        }
        error = evaluation_error.empty()
            ? "the expression is not a supported static VHDL value"
            : evaluation_error;
        return std::nullopt;
    }

    if (expression.aggregate_choices.size()
            != expression.operands.size()
        || expression.aggregate_choice_expressions.size()
            != expression.operands.size()) {
        error = "aggregate association metadata is inconsistent";
        return std::nullopt;
    }

    auto result = default_packed_value(type, width);
    const auto insert =
        [&](const PackedLogic4& value,
            const std::size_t offset) {
          for (std::size_t bit = 0; bit < value.width(); ++bit) {
              if (result.is_logic9()) {
                  result.set_logic9(
                      offset + bit,
                      value.is_logic9()
                          ? value.get_logic9(bit)
                          : runtime::to_logic9(value.get(bit)));
              } else {
                  result.set(offset + bit, value.get(bit));
              }
          }
        };

    if (!type.packed_members.empty()) {
        std::vector<bool> assigned(type.packed_members.size());
        std::optional<std::size_t> others;
        std::size_t positional = 0;
        const auto assign =
            [&](const std::size_t member_index,
                const Expression& value) -> bool {
              if (member_index >= type.packed_members.size()
                  || assigned[member_index]) {
                  error = member_index
                              >= type.packed_members.size()
                      ? "record aggregate has too many positional "
                        "associations"
                      : "record aggregate assigns one element more "
                        "than once";
                  return false;
              }
              const auto& member =
                  type.packed_members[member_index];
              frontend::Type member_type;
              member_type.domain = member.domain;
              member_type.spelling = member.spelling;
              member_type.packed_range = member.packed_range;
              member_type.packed_range_expression =
                  member.packed_range_expression;
              member_type.is_signed = member.is_signed;
              auto member_value =
                  static_vhdl_value(
                      value, member_type, error);
              if (!member_value) {
                  return false;
              }
              insert(
                  *member_value,
                  static_cast<std::size_t>(
                      member.lsb_offset));
              assigned[member_index] = true;
              return true;
            };
        for (std::size_t index = 0;
             index < expression.operands.size(); ++index) {
            const auto& choice =
                expression.aggregate_choices[index];
            if (choice.empty()) {
                if (!assign(
                        positional++,
                        expression.operands[index])) {
                    return std::nullopt;
                }
                continue;
            }
            if (choice == "others") {
                if (others) {
                    error = "record aggregate has more than one others "
                            "association";
                    return std::nullopt;
                }
                others = index;
                continue;
            }
            const auto member = std::ranges::find_if(
                type.packed_members,
                [&](const auto& candidate) {
                  return candidate.name == choice;
                });
            if (member == type.packed_members.end()) {
                error = "record aggregate names an unknown element '"
                    + choice + "'";
                return std::nullopt;
            }
            if (!assign(
                    static_cast<std::size_t>(
                        std::distance(
                            type.packed_members.begin(), member)),
                    expression.operands[index])) {
                return std::nullopt;
            }
        }
        if (others) {
            for (std::size_t member = 0;
                 member < assigned.size(); ++member) {
                if (!assigned[member]
                    && !assign(
                        member,
                        expression.operands[*others])) {
                    return std::nullopt;
                }
            }
        }
        if (std::ranges::find(assigned, false)
            != assigned.end()) {
            error = "record aggregate omits a required element";
            return std::nullopt;
        }
        return normalize(std::move(result));
    }

    if (type.packed_range) {
        frontend::Type element_type;
        if (type.vhdl_array
            && !type.vhdl_array->element_types.empty()) {
            element_type = type.vhdl_array->element_types.front();
        } else {
            element_type.domain = type.vhdl_array
                ? type.vhdl_array->element_domain
                : type.domain;
            element_type.spelling = type.vhdl_array
                ? type.vhdl_array->element_spelling
                : type.spelling;
            element_type.named_type = type.vhdl_array
                ? type.vhdl_array->element_named_type
                : std::string{};
        }
        const auto element_width_value = element_type.width().value_or(1);
        if (element_width_value == 0 || width % element_width_value != 0) {
            error = "array aggregate element width does not divide its layout";
            return std::nullopt;
        }
        const auto element_width = static_cast<std::size_t>(
            element_width_value);
        const auto element_count = width / element_width;
        std::vector<bool> assigned(element_count);
        std::optional<std::size_t> others;
        std::size_t positional = 0;
        const auto assign_offset =
            [&](const std::size_t offset,
                const Expression& value) -> bool {
              if (offset >= width || assigned[offset]) {
                  error = offset >= width
                      ? "array aggregate index is outside its constraint"
                      : "array aggregate assigns one index more than once";
                  return false;
              }
              auto element =
                  static_vhdl_value(
                      value, element_type, error);
              if (!element || element->width() != element_width) {
                  if (error.empty()) {
                      error = "array aggregate element has the wrong width";
                  }
                  return false;
              }
              insert(*element, offset * element_width);
              assigned[offset] = true;
              return true;
            };
        const auto assign_index =
            [&](const std::int64_t source_index,
                const Expression& value) -> bool {
              const auto& range = *type.packed_range;
              const auto low = std::min(range.left, range.right);
              const auto high = std::max(range.left, range.right);
              if (source_index < low || source_index > high) {
                  error = "array aggregate index is outside its constraint";
                  return false;
              }
              return assign_offset(
                  static_cast<std::size_t>(
                      index_distance(
                          source_index, range.right)),
                  value);
            };
        for (std::size_t association = 0;
             association < expression.operands.size();
             ++association) {
            const auto& choices =
                expression.aggregate_choice_expressions[
                    association];
            if (choices.empty()) {
                const auto source_index =
                    type.packed_range->left
                    + (type.packed_range->descending
                           ? -static_cast<std::int64_t>(positional)
                           : static_cast<std::int64_t>(positional));
                ++positional;
                if (!assign_index(
                        source_index,
                        expression.operands[association])) {
                    return std::nullopt;
                }
                continue;
            }
            for (const auto& choice : choices) {
                if (choice.kind == ExpressionKind::Identifier
                    && choice.text == "others") {
                    if (others) {
                        error = "array aggregate has more than one others "
                                "association";
                        return std::nullopt;
                    }
                    others = association;
                    continue;
                }
                if (choice.kind == ExpressionKind::Binary
                    && (choice.text == "to"
                        || choice.text == "downto")
                    && choice.operands.size() == 2) {
                    std::string choice_error;
                    const auto left =
                        evaluate_constant_expression(
                            choice.operands[0], {}, choice_error);
                    const auto right =
                        evaluate_constant_expression(
                            choice.operands[1], {}, choice_error);
                    if (!left || !right) {
                        error = "array aggregate range is not static";
                        return std::nullopt;
                    }
                    const auto step =
                        choice.text == "downto" ? -1 : 1;
                    for (auto current = *left;; current += step) {
                        if (!assign_index(
                                current,
                                expression.operands[association])) {
                            return std::nullopt;
                        }
                        if (current == *right) {
                            break;
                        }
                        if ((step > 0 && current > *right)
                            || (step < 0 && current < *right)) {
                            error = "array aggregate range direction "
                                    "does not reach its right bound";
                            return std::nullopt;
                        }
                    }
                    continue;
                }
                std::string choice_error;
                const auto selected =
                    evaluate_constant_expression(
                        choice, {}, choice_error);
                if (!selected
                    || !assign_index(
                        *selected,
                        expression.operands[association])) {
                    if (!selected) {
                        error = "array aggregate choice is not static";
                    }
                    return std::nullopt;
                }
            }
        }
        if (others) {
            for (std::size_t offset = 0;
                 offset < assigned.size(); ++offset) {
                if (!assigned[offset]
                    && !assign_offset(
                        offset,
                        expression.operands[*others])) {
                    return std::nullopt;
                }
            }
        }
        if (std::ranges::find(assigned, false)
            != assigned.end()) {
            error = "array aggregate omits a required index";
            return std::nullopt;
        }
        return normalize(std::move(result));
    }

    error = "aggregate defaults require a supported record or array type";
    return std::nullopt;
}




}  // namespace fsim::elaboration::elaboration_detail

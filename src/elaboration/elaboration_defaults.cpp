// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <bit>

namespace fsim::elaboration::elaboration_detail {
namespace {

void insert_default(
    PackedLogic4& destination,
    const PackedLogic4& source,
    const std::size_t offset) {
  for (std::size_t bit = 0; bit < source.width(); ++bit) {
    if (destination.is_logic9()) {
      destination.set_logic9(
          offset + bit, source.get_logic9(bit));
    } else {
      destination.set(
          offset + bit,
          runtime::to_logic4(source.get_logic9(bit)));
    }
  }
}

}  // namespace

std::optional<PackedLogic4> evaluate_systemverilog_packed_constant(
    const Expression& source_expression,
    const frontend::Type& type,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error) {
  const auto type_width = type.width();
  if (!type_width || *type_width == 0
      || *type_width > 16U * 1024U * 1024U) {
    error = "contextual packed constant type has no governed width";
    return std::nullopt;
  }
  const auto width = static_cast<std::size_t>(*type_width);
  if (source_expression.kind != ExpressionKind::Aggregate
      && !(source_expression.kind == ExpressionKind::Call
           && source_expression.text.starts_with("@sv-tagged:"))) {
    const auto value = evaluate_systemverilog_constant_expression(
        source_expression, environment, fallback_environment, error);
    if (!value) {
      return std::nullopt;
    }
    const auto converted = convert_systemverilog_parameter_value(
        *value, type, error);
    return converted
        ? std::optional<PackedLogic4>{converted->packed}
        : std::nullopt;
  }
  if (type.packed_members.empty()) {
    error = "assignment pattern requires a contextual packed aggregate type";
    return std::nullopt;
  }

  Expression expression = source_expression;
  if (expression.kind == ExpressionKind::Call
      && expression.text.starts_with("@sv-tagged:")) {
    if (type.packed_aggregate
            != frontend::PackedAggregateKind::TaggedUnion
        || expression.operands.size() != 1U) {
      error = "tagged construction requires one value and a tagged-union context";
      return std::nullopt;
    }
    const auto member = expression.text.substr(
        std::string_view{"@sv-tagged:"}.size());
    expression.kind = ExpressionKind::Aggregate;
    expression.aggregate_choices = {"@key"};
    expression.aggregate_choice_expressions = {{Expression{
        ExpressionKind::Identifier, member, {}, expression.span}}};
  }
  if (expression.aggregate_choices.size() != expression.operands.size()
      || expression.aggregate_choice_expressions.size()
          != expression.operands.size()) {
    error = "assignment-pattern metadata is inconsistent";
    return std::nullopt;
  }

  const bool is_union =
      type.packed_aggregate == frontend::PackedAggregateKind::Union
      || type.packed_aggregate
          == frontend::PackedAggregateKind::TaggedUnion;
  const bool tagged = type.packed_aggregate
      == frontend::PackedAggregateKind::TaggedUnion;
  const auto tag_width = tagged
      ? std::max<std::size_t>(
            1U, static_cast<std::size_t>(
                    std::bit_width(type.packed_members.size() - 1U)))
      : 0U;
  auto result = PackedLogic4(width, Logic4::zero);
  std::vector<bool> assigned(type.packed_members.size());
  std::optional<std::size_t> default_index;
  std::size_t positional_index = 0;

  const auto member_type = [](const frontend::PackedMember& member) {
    frontend::Type member_result;
    if (!member.nested_types.empty()) {
      return member.nested_types.front();
    }
    member_result.domain = member.domain;
    member_result.spelling = member.spelling;
    member_result.packed_range = member.packed_range;
    member_result.packed_range_expression = member.packed_range_expression;
    member_result.is_signed = member.is_signed;
    return member_result;
  };
  const auto insert_member = [&](const std::size_t member_index,
                                 const Expression& value) -> bool {
    if (member_index >= type.packed_members.size()
        || assigned[member_index]) {
      error = member_index < type.packed_members.size()
          ? "assignment pattern assigns member '"
                + type.packed_members[member_index].name + "' more than once"
          : "assignment pattern has too many positional members";
      return false;
    }
    const auto& member = type.packed_members[member_index];
    const auto member_width = member.width();
    if (!member_width || *member_width == 0
        || member.lsb_offset > width
        || *member_width > width - member.lsb_offset) {
      error = "packed member '" + member.name
          + "' has no executable contextual layout";
      return false;
    }
    const auto nested_type = member_type(member);
    const auto packed = evaluate_systemverilog_packed_constant(
        value, nested_type, environment, fallback_environment, error);
    if (!packed || packed->width() != *member_width) {
      if (packed) {
        error = "packed member '" + member.name
            + "' produced the wrong contextual width";
      }
      return false;
    }
    insert_default(
        result, *packed, static_cast<std::size_t>(member.lsb_offset));
    if (tagged) {
      const auto tag_offset = width - tag_width;
      for (std::size_t bit = 0; bit < tag_width; ++bit) {
        result.set(
            tag_offset + bit,
            ((member_index >> bit) & 1U) != 0U
                ? Logic4::one : Logic4::zero);
      }
    }
    assigned[member_index] = true;
    return true;
  };

  for (std::size_t index = 0; index < expression.operands.size(); ++index) {
    const auto& choice = expression.aggregate_choices[index];
    const auto& choices = expression.aggregate_choice_expressions[index];
    if (choice.empty()) {
      if (!insert_member(positional_index++, expression.operands[index])) {
        return std::nullopt;
      }
      continue;
    }
    if (choice == "default") {
      if (default_index) {
        error = "assignment pattern has more than one default";
        return std::nullopt;
      }
      default_index = index;
      continue;
    }
    if (choice != "@key" || choices.size() != 1U
        || choices.front().kind != ExpressionKind::Identifier) {
      error = "assignment-pattern keys must name direct packed members";
      return std::nullopt;
    }
    const auto found = std::ranges::find_if(
        type.packed_members,
        [&](const frontend::PackedMember& member) {
          return member.name == choices.front().text;
        });
    if (found == type.packed_members.end()) {
      error = "assignment pattern names unknown member '"
          + choices.front().text + "'";
      return std::nullopt;
    }
    if (!insert_member(
            static_cast<std::size_t>(std::distance(
                type.packed_members.begin(), found)),
            expression.operands[index])) {
      return std::nullopt;
    }
  }
  if (is_union && default_index) {
    error = "a packed union constant cannot use a default pattern arm";
    return std::nullopt;
  }
  if (default_index) {
    for (std::size_t index = 0; index < assigned.size(); ++index) {
      if (!assigned[index]
          && !insert_member(index, expression.operands[*default_index])) {
        return std::nullopt;
      }
    }
  }
  const auto count = static_cast<std::size_t>(
      std::ranges::count(assigned, true));
  if ((is_union && count != 1)
      || (!is_union && count != assigned.size())) {
    error = is_union
        ? "a packed union constant requires exactly one member"
        : "a packed struct constant does not initialize every member";
    return std::nullopt;
  }
  return result;
}

PackedLogic4 default_packed_value(
    const frontend::Type& type,
    const std::size_t width) {
  if (!type.systemverilog_class_declaration.empty()) {
    return unsigned_value(0, width);
  }
  if (!type.enumeration_literals.empty()
      && type.enumeration_range
      && type.systemverilog_enumeration_values.empty()) {
      return unsigned_value(
          static_cast<std::uint64_t>(type.enumeration_range->left),
          width);
  }
  auto result = PackedLogic4(
      width,
      is_two_state_domain(type.domain)
          ? Logic4::zero
          : Logic4::x);
  if (type.domain == frontend::ValueDomain::Logic9) {
    result.fill(runtime::Logic9::u);
  }
  if (type.vhdl_array
      && !type.vhdl_array->element_types.empty()) {
    const auto& element = type.vhdl_array->element_types.front();
    const auto element_width = element.width();
    if (element_width && *element_width != 0) {
      const auto element_default = default_packed_value(
          element, static_cast<std::size_t>(*element_width));
      for (std::size_t offset = 0;
           offset + *element_width <= width;
           offset += static_cast<std::size_t>(*element_width)) {
        insert_default(result, element_default, offset);
      }
    }
    return result;
  }
  if (type.domain == frontend::ValueDomain::Integer
      && type.systemverilog_enumeration_values.empty()
      && width != 0) {
      return unsigned_value(
          static_cast<std::uint64_t>(
              type.integer_range
                  ? type.integer_range->left
                  : std::numeric_limits<std::int32_t>::min()),
          width);
  }
  if (type.packed_aggregate
      == frontend::PackedAggregateKind::Union) {
    return result;
  }
  if (type.packed_aggregate
          == frontend::PackedAggregateKind::TaggedUnion
      && !type.packed_members.empty()) {
    result = PackedLogic4(width, Logic4::zero);
    const auto& member = type.packed_members.front();
    const auto member_width = member.width();
    if (member_width && *member_width <= width) {
      frontend::Type scalar_type;
      const frontend::Type* member_type = &scalar_type;
      if (!member.nested_types.empty()) {
        member_type = &member.nested_types.front();
      } else {
        scalar_type.domain = member.domain;
        scalar_type.spelling = member.spelling;
        scalar_type.packed_range = member.packed_range;
        scalar_type.is_signed = member.is_signed;
      }
      insert_default(
          result,
          default_packed_value(
              *member_type,
              static_cast<std::size_t>(*member_width)),
          0);
    }
    return result;
  }
  for (const auto& member : type.packed_members) {
    const auto member_width = member.width();
    if (!member_width
        || member.lsb_offset > width
        || *member_width > width - member.lsb_offset) {
      continue;
    }
    frontend::Type scalar_type;
    const frontend::Type* member_type = &scalar_type;
    if (!member.nested_types.empty()) {
      member_type = &member.nested_types.front();
    } else {
      scalar_type.domain = member.domain;
      scalar_type.spelling = member.spelling;
      scalar_type.packed_range = member.packed_range;
      scalar_type.is_signed = member.is_signed;
    }
    if (member.initializer) {
      std::string error;
      if (const auto initialized = evaluate_systemverilog_packed_constant(
              *member.initializer, *member_type, {}, {}, error)) {
        insert_default(
            result, *initialized,
            static_cast<std::size_t>(member.lsb_offset));
        continue;
      }
    }
    if (!member.nested_types.empty()) {
      insert_default(
          result,
          default_packed_value(
              member.nested_types.front(),
              static_cast<std::size_t>(*member_width)),
          static_cast<std::size_t>(member.lsb_offset));
      continue;
    }
    for (std::uint64_t bit = 0; bit < *member_width; ++bit) {
      const auto index =
          static_cast<std::size_t>(member.lsb_offset + bit);
      if (result.is_logic9()) {
        result.set_logic9(
            index,
            member.domain == frontend::ValueDomain::Logic9
                ? runtime::Logic9::u
                : member.domain == frontend::ValueDomain::Logic4
                    ? runtime::Logic9::x
                    : runtime::Logic9::zero);
      } else {
        result.set(
            index,
            is_two_state_domain(member.domain)
                ? Logic4::zero
                : Logic4::x);
      }
    }
  }
  return result;
}

}  // namespace fsim::elaboration::elaboration_detail

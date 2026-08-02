// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::optional<std::uint64_t> range_count(
    const frontend::IntegerRange& range) {
  if (range.descending ? range.left < range.right
                       : range.left > range.right) {
    return std::uint64_t{0};
  }
  const auto left = static_cast<std::uint64_t>(range.left);
  const auto right = static_cast<std::uint64_t>(range.right);
  const auto distance = range.left >= range.right
      ? left - right
      : right - left;
  if (distance == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return distance + 1U;
}

std::optional<frontend::Type> array_element_type(
    const frontend::Type& type) {
  if (!type.vhdl_array || type.vhdl_array->dimensions.empty()
      || type.vhdl_array->element_types.empty()) {
    return std::nullopt;
  }
  if (type.vhdl_array->dimensions.size() == 1U) {
    return type.vhdl_array->element_types.front();
  }
  auto result = type;
  auto& array = *result.vhdl_array;
  const auto width = array.dimensions.front().stride;
  array.dimensions.erase(array.dimensions.begin());
  const auto& first = array.dimensions.front();
  array.index_subtype = first.index_subtype;
  array.index_span = first.index_span;
  array.index_base_range = first.index_base_range;
  array.unconstrained = false;
  array.flat_width = width;
  result.vhdl_array_constraints.clear();
  result.packed_range_expression.reset();
  if (array.dimensions.size() == 1U && first.range && !first.null) {
    result.packed_range = frontend::PackedRange{
        first.range->left, first.range->right,
        first.range->descending};
  } else if (width != 0
             && width - 1U
                 <= static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())) {
    result.packed_range = frontend::PackedRange{
        static_cast<std::int64_t>(width - 1U), 0, true};
  } else {
    result.packed_range.reset();
  }
  return result;
}

std::optional<frontend::Type> array_slice_type(
    const frontend::Type& type,
    const frontend::IntegerRange& range) {
  if (!type.vhdl_array || type.vhdl_array->dimensions.empty()) {
    return std::nullopt;
  }
  const auto count = range_count(range);
  const auto stride = type.vhdl_array->dimensions.front().stride;
  if (!count || (stride != 0
                 && *count
                     > std::numeric_limits<std::uint64_t>::max()
                           / stride)) {
    return std::nullopt;
  }
  auto result = type;
  auto& array = *result.vhdl_array;
  auto& first = array.dimensions.front();
  first.range = range;
  first.null = *count == 0;
  first.unconstrained = false;
  array.unconstrained = false;
  array.flat_width = *count * stride;
  array.index_subtype = first.index_subtype;
  array.index_span = first.index_span;
  array.index_base_range = first.index_base_range;
  result.vhdl_array_constraints.clear();
  result.packed_range_expression.reset();
  if (array.dimensions.size() == 1U && *count != 0) {
    result.packed_range = frontend::PackedRange{
        range.left, range.right, range.descending};
  } else if (*array.flat_width != 0
             && *array.flat_width - 1U
                 <= static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())) {
    result.packed_range = frontend::PackedRange{
        static_cast<std::int64_t>(*array.flat_width - 1U), 0, true};
  } else {
    result.packed_range.reset();
  }
  return result;
}

std::optional<frontend::Type> record_member_type(
    const frontend::Type& source,
    const std::string_view name) {
  const auto member = std::ranges::find(
      source.packed_members, name, &frontend::PackedMember::name);
  if (member == source.packed_members.end()) {
    return std::nullopt;
  }
  if (!member->nested_types.empty()) {
    return member->nested_types.front();
  }
  frontend::Type result;
  result.domain = member->domain;
  result.spelling = member->spelling;
  result.packed_range = member->packed_range;
  result.is_signed = member->is_signed;
  return result;
}

std::optional<std::uint64_t> static_array_offset(
    const frontend::VhdlArrayDimension& dimension,
    const std::int64_t index) {
  if (!dimension.range || dimension.null
      || !dimension.range->contains(index)) {
    return std::nullopt;
  }
  const auto distance = dimension.range->right >= index
      ? static_cast<std::uint64_t>(dimension.range->right)
            - static_cast<std::uint64_t>(index)
      : static_cast<std::uint64_t>(index)
            - static_cast<std::uint64_t>(dimension.range->right);
  if (dimension.stride != 0
      && distance
          > std::numeric_limits<std::uint64_t>::max()
                / dimension.stride) {
    return std::nullopt;
  }
  return distance * dimension.stride;
}

}  // namespace

std::optional<frontend::Type> Lowerer::vhdl_expression_type(
    const Expression& expression) const {
  if (language_ != frontend::Language::Vhdl2008) {
    return std::nullopt;
  }
  if (expression.kind == ExpressionKind::Identifier) {
    const auto* type = object_type(expression.text);
    return type != nullptr
        ? std::optional<frontend::Type>{*type}
        : std::nullopt;
  }
  if (expression.kind == ExpressionKind::Call) {
    constexpr std::string_view member_prefix{"@vhdl-member:"};
    if (expression.text.starts_with(member_prefix)
        && expression.operands.size() == 1) {
      const auto source = vhdl_expression_type(
          expression.operands.front());
      return source
          ? record_member_type(
              *source,
              std::string_view{expression.text}.substr(
                  member_prefix.size()))
          : std::nullopt;
    }
    if (expression.text == "?:" && expression.operands.size() == 3) {
      auto when_true = vhdl_expression_type(expression.operands[1]);
      auto when_false = vhdl_expression_type(expression.operands[2]);
      if (when_true && when_false
          && vhdl_callable_type_matches(*when_true, *when_false)) {
        return when_true;
      }
      if (when_true
          && expression.operands[2].kind
              == ExpressionKind::Aggregate) {
        return when_true;
      }
      if (when_false
          && expression.operands[1].kind
              == ExpressionKind::Aggregate) {
        return when_false;
      }
      return std::nullopt;
    }
    constexpr std::string_view qualification_prefix{
        "@vhdl-qualified:"};
    auto type_name = std::string_view{expression.text};
    if (type_name.starts_with(qualification_prefix)) {
      type_name.remove_prefix(qualification_prefix.size());
    }
    if (expression.operands.size() == 1
        && object_type(expression.text) == nullptr) {
      if (const auto* converted = visible_type_mark(type_name)) {
        return *converted;
      }
    }
    const auto functions = function_indices_.find(expression.text);
    if (functions != function_indices_.end()) {
      std::optional<frontend::Type> result;
      for (const auto index : functions->second) {
        const auto& function = *function_frames_[index].source;
        if (!vhdl_function_profile_matches(
                expression, function, nullptr)) {
          continue;
        }
        if (!result) {
          result = function.return_type;
        } else if (!vhdl_callable_type_matches(
                       *result, function.return_type)) {
          return std::nullopt;
        }
      }
      if (result) {
        return result;
      }
    }
    const auto* root = object_type(expression.text);
    if (root == nullptr || !root->vhdl_array
        || expression.operands.empty()) {
      return std::nullopt;
    }
    auto type = std::optional<frontend::Type>{*root};
    for (const auto& argument : expression.operands) {
      if (!type || !type->vhdl_array) {
        return std::nullopt;
      }
      if (argument.kind == ExpressionKind::Binary
          && argument.operands.size() == 2
          && (argument.text == "to"
              || argument.text == "downto")) {
        const auto left = constant_index(argument.operands[0]);
        const auto right = constant_index(argument.operands[1]);
        if (!left || !right) {
          return std::nullopt;
        }
        type = array_slice_type(
            *type,
            frontend::IntegerRange{
                *left, *right, argument.text == "downto"});
      } else {
        type = array_element_type(*type);
      }
    }
    return type;
  }
  if ((expression.kind != ExpressionKind::Index
       || expression.operands.size() != 2)
      && (expression.kind != ExpressionKind::Slice
          || expression.operands.size() != 3)) {
    return std::nullopt;
  }
  const auto source = vhdl_expression_type(expression.operands.front());
  if (!source || !source->vhdl_array) {
    return std::nullopt;
  }
  if (expression.kind == ExpressionKind::Index) {
    return array_element_type(*source);
  }
  const auto left = constant_index(expression.operands[1]);
  const auto right = constant_index(expression.operands[2]);
  return left && right
      ? array_slice_type(
            *source,
            frontend::IntegerRange{
                *left, *right, expression.text == "downto"})
      : std::nullopt;
}

Lowerer::ExpressionAttempt
Lowerer::lower_vhdl_array_selection_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  if (language_ != frontend::Language::Vhdl2008) {
    return {};
  }
  if (expression.kind == ExpressionKind::Call) {
    constexpr std::string_view member_prefix{"@vhdl-member:"};
    if (expression.text.starts_with(member_prefix)
        && expression.operands.size() == 1) {
      const auto source_type = vhdl_expression_type(
          expression.operands.front());
      const auto selected_type = vhdl_expression_type(expression);
      if (!source_type || !selected_type) {
        report(
            "FSIM-ELAB-VHCOMPOP-004",
            "a chained VHDL member selection requires a visible record "
            "element after its array selection",
            expression.span);
        return std::nullopt;
      }
      const auto name = std::string_view{expression.text}.substr(
          member_prefix.size());
      const auto member = std::ranges::find(
          source_type->packed_members, name,
          &frontend::PackedMember::name);
      const auto source_width = source_type->width();
      const auto member_width = selected_type->width();
      if (member == source_type->packed_members.end()
          || !source_width || !member_width || *member_width == 0
          || *source_width > 64 || *member_width > 64
          || member->lsb_offset + *member_width > *source_width) {
        report(
            "FSIM-ELAB-VHCOMPOP-004",
            "a chained VHDL member selection has no bounded executable "
            "record layout",
            expression.span);
        return std::nullopt;
      }
      const auto source = lower_expression(
          expression.operands.front(),
          static_cast<std::size_t>(*source_width), &*source_type);
      if (!source) {
        return std::nullopt;
      }
      const auto destination = allocate_register(
          static_cast<std::size_t>(*member_width),
          selected_type->domain);
      process_.operations.emplace_back(Extract{
          destination, *source,
          static_cast<std::uint32_t>(member->lsb_offset),
          static_cast<std::uint32_t>(*member_width)});
      return destination;
    }
    const auto* root_type = object_type(expression.text);
    if (root_type == nullptr || !root_type->vhdl_array
        || expression.operands.empty()) {
      return {};
    }
    Expression selected{
        ExpressionKind::Identifier, expression.text, {}, expression.span};
    for (const auto& argument : expression.operands) {
      if (argument.kind == ExpressionKind::Binary
          && argument.operands.size() == 2
          && (argument.text == "to"
              || argument.text == "downto")) {
        selected = Expression{
            ExpressionKind::Slice, argument.text,
            {std::move(selected), argument.operands[0],
             argument.operands[1]},
            expression.span};
      } else {
        selected = Expression{
            ExpressionKind::Index, "index",
            {std::move(selected), argument}, expression.span};
      }
    }
    return lower_expression(selected, expected_width, expected_type);
  }
  if ((expression.kind != ExpressionKind::Index
       || expression.operands.size() != 2)
      && (expression.kind != ExpressionKind::Slice
          || expression.operands.size() != 3)) {
    return {};
  }
  const auto source_type =
      vhdl_expression_type(expression.operands.front());
  if (!source_type || !source_type->vhdl_array) {
    return {};
  }
  const auto& array = *source_type->vhdl_array;
  if (!array.dimensions.empty()
      && array.dimensions.front().range
      && array.dimensions.front().stride != 0
      && expression.kind == ExpressionKind::Slice) {
    const bool descending = expression.text == "downto";
    const auto left = static_integer_value(expression.operands[1]);
    const auto right = static_integer_value(expression.operands[2]);
    const bool null_slice = left && right
        && (descending ? *left < *right : *left > *right);
    if (null_slice) {
      const auto& dimension = array.dimensions.front();
      const bool exact_null_source = dimension.null
          && dimension.range->left == *left
          && dimension.range->right == *right
          && dimension.range->descending == descending;
      if ((expression.text != "to" && !descending)
          || dimension.range->descending != descending
          || (!exact_null_source
              && (!dimension.range->contains(*left)
                  || !dimension.range->contains(*right)))) {
        report(
            "FSIM-ELAB-VHARRAYSEL-004",
            "VHDL null slice bounds must retain the selected dimension "
            "direction and index subtype",
            expression.span);
        return std::nullopt;
      }
      return allocate_register(0, source_type->domain);
    }
  }
  if (!array.dimensions.empty()
      && array.dimensions.size() == 1U
      && array.dimensions.front().stride == 1U
      && expression.operands.front().kind
          == ExpressionKind::Identifier) {
    return {};
  }
  if (array.dimensions.empty()
      || !array.dimensions.front().range
      || array.dimensions.front().null
      || array.dimensions.front().stride == 0) {
    report(
        "FSIM-ELAB-VHARRAYSEL-001",
        "VHDL array selection requires a concrete non-null dimension "
        "with executable element layout",
        expression.span);
    return std::nullopt;
  }
  const auto source_width = source_type->width();
  if (!source_width || *source_width == 0 || *source_width > 64) {
    report(
        "FSIM-ELAB-VHARRAYSEL-001",
        "VHDL array selection source has no executable packed width",
        expression.span);
    return std::nullopt;
  }
  const auto& dimension = array.dimensions.front();
  const auto emit_dynamic_ordinal =
      [&](const Expression& index) -> std::optional<RegisterId> {
        if (!is_integer_expression(index)) {
          report(
              "FSIM-ELAB-VHARRAYSEL-002",
              "a runtime VHDL array index requires an integer-family "
              "expression",
              index.span);
          return std::nullopt;
        }
        const auto value = lower_expression(index, 32);
        const auto low = std::min(
            dimension.range->left, dimension.range->right);
        const auto high = std::max(
            dimension.range->left, dimension.range->right);
        if (!value || register_width(*value) != 32
            || low < std::numeric_limits<std::int32_t>::min()
            || high > std::numeric_limits<std::int32_t>::max()) {
          report(
              "FSIM-ELAB-VHARRAYSEL-002",
              "a runtime VHDL array index and its bounds must fit the "
              "signed 32-bit execution representation",
              index.span);
          return std::nullopt;
        }
        process_.operations.emplace_back(IntegerCheck{
            *value, static_cast<std::int32_t>(low),
            static_cast<std::int32_t>(high)});
        const auto right = allocate_register(
            32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant{
            right, integer_value(dimension.range->right)});
        const auto ordinal = allocate_register(
            32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary{
            IntegerBinaryOperator::subtract,
            ordinal,
            dimension.range->descending ? *value : right,
            dimension.range->descending ? right : *value});
        if (dimension.stride == 1U) {
          return ordinal;
        }
        const auto stride = allocate_register(
            32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant{
            stride,
            integer_value(
                static_cast<std::int64_t>(dimension.stride))});
        const auto scaled = allocate_register(
            32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary{
            IntegerBinaryOperator::multiply,
            scaled, ordinal, stride});
        return scaled;
      };

  std::size_t selected_width = 0;
  std::optional<std::uint64_t> static_offset;
  std::optional<RegisterId> dynamic_offset;
  if (expression.kind == ExpressionKind::Index) {
    selected_width = dimension.stride;
    const auto index = static_integer_value(expression.operands[1]);
    if (index) {
      static_offset = static_array_offset(dimension, *index);
      if (!static_offset) {
        report(
            "FSIM-ELAB-VHARRAYSEL-003",
            "VHDL array index " + std::to_string(*index)
                + " is outside the selected dimension",
            expression.operands[1].span);
        return std::nullopt;
      }
    } else {
      dynamic_offset = emit_dynamic_ordinal(expression.operands[1]);
      if (!dynamic_offset) {
        return std::nullopt;
      }
    }
  } else {
    const bool descending = expression.text == "downto";
    if ((expression.text != "to" && !descending)
        || dimension.range->descending != descending) {
      report(
          "FSIM-ELAB-VHARRAYSEL-004",
          "VHDL array slice direction must match the selected dimension",
          expression.span);
      return std::nullopt;
    }
    const auto left = static_integer_value(expression.operands[1]);
    const auto right = static_integer_value(expression.operands[2]);
    if (left && right) {
      if (!dimension.range->contains(*left)
          || !dimension.range->contains(*right)
          || (descending ? *left < *right : *left > *right)) {
        report(
            "FSIM-ELAB-VHARRAYSEL-004",
            "VHDL array slice bounds must form a non-null range inside "
            "the selected dimension",
            expression.span);
        return std::nullopt;
      }
      const auto count = static_cast<std::uint64_t>(
          descending ? *left - *right : *right - *left) + 1U;
      selected_width = count * dimension.stride;
      static_offset = static_array_offset(dimension, *right);
    } else {
      if (expected_width == 0
          || expected_width % dimension.stride != 0) {
        report(
            "FSIM-ELAB-VHARRAYSEL-004",
            "a runtime VHDL array slice requires a statically known shape "
            "compatible with the element stride",
            expression.span);
        return std::nullopt;
      }
      selected_width = expected_width;
      const auto left_value = emit_dynamic_ordinal(
          expression.operands[1]);
      dynamic_offset = emit_dynamic_ordinal(
          expression.operands[2]);
      if (!left_value || !dynamic_offset) {
        return std::nullopt;
      }
      const auto distance = allocate_register(
          32, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(IntegerBinary{
          IntegerBinaryOperator::subtract,
          distance,
          *left_value, *dynamic_offset});
      const auto required = static_cast<std::int32_t>(
          selected_width - dimension.stride);
      process_.operations.emplace_back(
          IntegerCheck{distance, required, required});
    }
  }
  if (selected_width == 0 || selected_width > 64
      || (static_offset
          && *static_offset + selected_width > *source_width)) {
    report(
        "FSIM-ELAB-VHARRAYSEL-001",
        "VHDL array selection has an unrepresentable packed shape",
        expression.span);
    return std::nullopt;
  }
  const auto source = lower_expression(
      expression.operands.front(), *source_width, &*source_type);
  if (!source) {
    return std::nullopt;
  }
  const auto destination = allocate_register(
      selected_width, source_type->domain);
  if (static_offset) {
    process_.operations.emplace_back(Extract{
        destination, *source,
        static_cast<std::uint32_t>(*static_offset),
        static_cast<std::uint32_t>(selected_width)});
  } else {
    process_.operations.emplace_back(DynamicPartSelect{
        destination, *source, *dynamic_offset,
        static_cast<std::int64_t>(*source_width - 1U), 0,
        static_cast<std::uint32_t>(selected_width), true, true,
        source_type->domain == frontend::ValueDomain::Bit2
            || source_type->domain == frontend::ValueDomain::Boolean,
        0});
  }
  return destination;
}

bool Lowerer::lower_assignment_selections(
    const Statement& statement,
    const std::string_view target_name,
    const std::size_t whole_width,
    const std::vector<const Expression*>& selections,
    std::uint32_t& selected_offset,
    bool& has_selected_offset,
    std::optional<std::size_t>& selected_width,
    std::optional<frontend::ValueDomain>& selected_domain,
    std::optional<DynamicIndex>& dynamic_selection,
    std::optional<DynamicPartIndex>& dynamic_part_selection,
    std::optional<frontend::Type>& selected_type) {
  std::optional<frontend::Type> current_type;
  if (const auto* object = object_type(target_name)) {
    current_type = *object;
  }
  std::optional<RegisterId> dynamic_array_offset;
  bool array_selection_active = false;
  const auto add_dynamic_offset =
      [&](const RegisterId contribution) {
        if (!dynamic_array_offset) {
          dynamic_array_offset = contribution;
          return;
        }
        const auto combined = allocate_register(
            32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary{
            IntegerBinaryOperator::add,
            combined, *dynamic_array_offset, contribution});
        dynamic_array_offset = combined;
      };
  for (std::size_t selection_index = 0;
       selection_index < selections.size(); ++selection_index) {
    const auto& selection_expression = *selections[selection_index];
    const auto& selection_source =
        selection_expression.operands.front();
    const auto selection_source_width =
        selected_width.value_or(whole_width);
    const bool final_selection =
        selection_index + 1U == selections.size();
    constexpr std::string_view member_prefix{"@vhdl-member:"};
    if (selection_expression.kind == ExpressionKind::Call
        && selection_expression.text.starts_with(member_prefix)
        && selection_expression.operands.size() == 1) {
      const auto name = std::string_view{selection_expression.text}.substr(
          member_prefix.size());
      const frontend::PackedMember* member = nullptr;
      if (current_type) {
        const auto found = std::ranges::find(
            current_type->packed_members, name,
            &frontend::PackedMember::name);
        if (found != current_type->packed_members.end()) {
          member = &*found;
        }
      }
      if (member == nullptr) {
        report(
            "FSIM-ELAB-VHCOMPOP-004",
            "a chained VHDL assignment member does not name an element "
            "of the selected record",
            selection_expression.span);
        return false;
      }
      const auto width = member->width();
      if (!width || *width == 0 || *width > 64
          || member->lsb_offset
              > std::numeric_limits<std::uint32_t>::max()
                  - selected_offset) {
        report(
            "FSIM-ELAB-VHCOMPOP-004",
            "a chained VHDL assignment member has no bounded executable "
            "record layout",
            selection_expression.span);
        return false;
      }
      selected_offset += static_cast<std::uint32_t>(
          member->lsb_offset);
      has_selected_offset = true;
      selected_width = static_cast<std::size_t>(*width);
      selected_domain = member->domain;
      current_type = record_member_type(*current_type, name);
      selected_type = current_type;
      continue;
    }
    if (current_type && current_type->vhdl_array
        && (current_type->vhdl_array->dimensions.size() != 1U
            || current_type->vhdl_array->dimensions.front().stride
                != 1U
            || dynamic_array_offset || array_selection_active)) {
      const auto& array = *current_type->vhdl_array;
      if (array.dimensions.empty()
          || !array.dimensions.front().range
          || (array.dimensions.front().null
              && selection_expression.kind
                  != ExpressionKind::Slice)
          || array.dimensions.front().stride == 0) {
        report(
            "FSIM-ELAB-VHARRAYSEL-001",
            "VHDL array target selection requires a concrete non-null "
            "dimension with executable element layout",
            selection_expression.span);
        return false;
      }
      const auto dimension = array.dimensions.front();
      const auto emit_ordinal =
          [&](const Expression& index) -> std::optional<RegisterId> {
            if (!is_integer_expression(index)) {
              report(
                  "FSIM-ELAB-VHARRAYSEL-002",
                  "a runtime VHDL array target index requires an "
                  "integer-family expression",
                  index.span);
              return std::nullopt;
            }
            const auto value = lower_expression(index, 32);
            const auto low = std::min(
                dimension.range->left, dimension.range->right);
            const auto high = std::max(
                dimension.range->left, dimension.range->right);
            if (!value || register_width(*value) != 32
                || low < std::numeric_limits<std::int32_t>::min()
                || high > std::numeric_limits<std::int32_t>::max()) {
              report(
                  "FSIM-ELAB-VHARRAYSEL-002",
                  "a runtime VHDL array target index and bounds must fit "
                  "signed 32-bit execution",
                  index.span);
              return std::nullopt;
            }
            process_.operations.emplace_back(IntegerCheck{
                *value, static_cast<std::int32_t>(low),
                static_cast<std::int32_t>(high)});
            const auto right = allocate_register(
                32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant{
                right, integer_value(dimension.range->right)});
            const auto ordinal = allocate_register(
                32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerBinary{
                IntegerBinaryOperator::subtract,
                ordinal,
                dimension.range->descending ? *value : right,
                dimension.range->descending ? right : *value});
            if (dimension.stride == 1U) {
              return ordinal;
            }
            const auto stride = allocate_register(
                32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant{
                stride,
                integer_value(static_cast<std::int64_t>(
                    dimension.stride))});
            const auto scaled = allocate_register(
                32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerBinary{
                IntegerBinaryOperator::multiply,
                scaled, ordinal, stride});
            return scaled;
          };
      if (selection_expression.kind == ExpressionKind::Index) {
        const auto index = static_integer_value(
            selection_expression.operands[1]);
        if (index) {
          const auto offset = static_array_offset(dimension, *index);
          if (!offset
              || *offset
                     > std::numeric_limits<std::uint32_t>::max()
                           - selected_offset) {
            report(
                "FSIM-ELAB-VHARRAYSEL-003",
                "VHDL array target index " + std::to_string(*index)
                    + " is outside the selected dimension",
                selection_expression.operands[1].span);
            return false;
          }
          selected_offset += static_cast<std::uint32_t>(*offset);
          has_selected_offset = true;
        } else {
          const auto ordinal = emit_ordinal(
              selection_expression.operands[1]);
          if (!ordinal) {
            return false;
          }
          add_dynamic_offset(*ordinal);
        }
        current_type = array_element_type(*current_type);
        if (!current_type) {
          return false;
        }
        selected_type = current_type;
        selected_width = dimension.stride;
        selected_domain = current_type->domain;
        array_selection_active = true;
        continue;
      }
      const bool descending = selection_expression.text == "downto";
      if (selection_expression.kind != ExpressionKind::Slice
          || (selection_expression.text != "to" && !descending)
          || dimension.range->descending != descending) {
        report(
            "FSIM-ELAB-VHARRAYSEL-004",
            "VHDL array target slice direction must match the selected "
            "dimension",
            selection_expression.span);
        return false;
      }
      const auto left = static_integer_value(
          selection_expression.operands[1]);
      const auto right = static_integer_value(
          selection_expression.operands[2]);
      if (left && right) {
        const bool null_slice =
            descending ? *left < *right : *left > *right;
        if (null_slice) {
          const bool exact_null_source = dimension.null
              && dimension.range->left == *left
              && dimension.range->right == *right
              && dimension.range->descending == descending;
          if (!exact_null_source
              && (!dimension.range->contains(*left)
                  || !dimension.range->contains(*right))) {
            report(
                "FSIM-ELAB-VHARRAYSEL-004",
                "VHDL null target slice bounds must retain the selected "
                "dimension index subtype",
                selection_expression.span);
            return false;
          }
          selected_offset = 0;
          has_selected_offset = false;
          selected_width = 0;
          current_type = array_slice_type(
              *current_type,
              frontend::IntegerRange{*left, *right, descending});
          selected_type = current_type;
          selected_domain = current_type->domain;
          array_selection_active = true;
          continue;
        }
        if (!dimension.range->contains(*left)
            || !dimension.range->contains(*right)
            || (descending ? *left < *right : *left > *right)) {
          report(
              "FSIM-ELAB-VHARRAYSEL-004",
              "VHDL array target slice bounds must form a non-null range "
              "inside the selected dimension",
              selection_expression.span);
          return false;
        }
        const auto count = static_cast<std::uint64_t>(
            descending ? *left - *right : *right - *left) + 1U;
        const auto offset = static_array_offset(dimension, *right);
        if (!offset || count > 64 / dimension.stride
            || *offset
                   > std::numeric_limits<std::uint32_t>::max()
                         - selected_offset) {
          report(
              "FSIM-ELAB-VHARRAYSEL-001",
              "VHDL array target slice has an unrepresentable packed "
              "shape",
              selection_expression.span);
          return false;
        }
        selected_offset += static_cast<std::uint32_t>(*offset);
        has_selected_offset = true;
        selected_width = count * dimension.stride;
        current_type = array_slice_type(
            *current_type,
            frontend::IntegerRange{*left, *right, descending});
        selected_type = current_type;
        selected_domain = current_type->domain;
        array_selection_active = true;
        continue;
      }
      const auto value_width = infer_width(statement.value);
      if (!final_selection || !value_width || *value_width == 0
          || *value_width % dimension.stride != 0) {
        report(
            "FSIM-ELAB-VHARRAYSEL-004",
            "a runtime VHDL array target slice must be final and have a "
            "statically known shape compatible with the element stride",
            selection_expression.span);
        return false;
      }
      const auto left_ordinal = emit_ordinal(
          selection_expression.operands[1]);
      const auto right_ordinal = emit_ordinal(
          selection_expression.operands[2]);
      if (!left_ordinal || !right_ordinal) {
        return false;
      }
      const auto distance = allocate_register(
          32, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(IntegerBinary{
          IntegerBinaryOperator::subtract,
          distance,
          *left_ordinal, *right_ordinal});
      const auto required = static_cast<std::int32_t>(
          *value_width - dimension.stride);
      process_.operations.emplace_back(
          IntegerCheck{distance, required, required});
      add_dynamic_offset(*right_ordinal);
      selected_width = *value_width;
      selected_domain = current_type->domain;
      selected_type = current_type;
      array_selection_active = true;
      continue;
    }

    if (dynamic_selection || dynamic_part_selection
        || (dynamic_array_offset
            && !static_integer_value(
                selection_expression.operands[1]))) {
      report(
          "FSIM-ELAB-SVEXPR-008",
          "a runtime-selected procedural target cannot be dynamically "
          "selected again",
          selection_expression.span);
      return false;
    }
    const auto base_offset = static_cast<std::uint64_t>(selected_offset);
    if (selection_expression.kind == ExpressionKind::Index) {
      const auto index = static_integer_value(
          selection_expression.operands[1]);
      if (index) {
        const auto offset = select_offset(
            selection_source, *index, selection_source_width);
        if (!offset
            || base_offset + *offset
                > std::numeric_limits<std::uint32_t>::max()) {
          report(
              "FSIM-ELAB-068",
              "an assignment bit-select requires an index inside the "
              "target's declared packed range",
              selection_expression.span);
          return false;
        }
        selected_offset = static_cast<std::uint32_t>(
            base_offset + *offset);
        has_selected_offset = true;
      } else {
        if (!final_selection) {
          report(
              "FSIM-ELAB-SVEXPR-008",
              "a dynamic bit-select must be the final packed procedural "
              "target selection",
              selection_expression.span);
          return false;
        }
        dynamic_selection = lower_dynamic_index(
            selection_source, selection_expression.operands[1],
            selection_source_width, selected_offset,
            selection_expression.span);
        if (!dynamic_selection) {
          return false;
        }
        selected_offset = 0;
        has_selected_offset = false;
      }
      selected_width = 1;
      selected_type.reset();
      current_type.reset();
      continue;
    }

    const auto selection = constant_slice_selection(
        selection_expression, selection_source_width);
    if (selection
        && base_offset + selection->offset
            <= std::numeric_limits<std::uint32_t>::max()
        && selection->width
            <= std::numeric_limits<std::uint32_t>::max()) {
      selected_offset = static_cast<std::uint32_t>(
          base_offset + selection->offset);
      has_selected_offset = true;
      selected_width = selection->width;
      selected_type.reset();
      current_type.reset();
      continue;
    }
    const bool runtime_vhdl_slice =
        language_ == frontend::Language::Vhdl2008
        && (!static_integer_value(selection_expression.operands[1])
            || !static_integer_value(selection_expression.operands[2]));
    if (runtime_vhdl_slice) {
      if (!final_selection) {
        report(
            "FSIM-ELAB-VHSLICE-003",
            "a dynamic VHDL slice must be the final packed "
            "assignment-target selection",
            selection_expression.span);
        return false;
      }
      const auto value_width = infer_width(statement.value);
      if (!value_width) {
        report(
            "FSIM-ELAB-VHSLICE-001",
            "a dynamic VHDL assignment slice requires a statically sized "
            "value",
            statement.value.span);
        return false;
      }
      dynamic_part_selection = lower_vhdl_dynamic_slice(
          selection_expression, selection_source_width,
          *value_width, selected_offset);
      if (!dynamic_part_selection) {
        return false;
      }
      selected_offset = 0;
      has_selected_offset = false;
      selected_width = *value_width;
      selected_type.reset();
      current_type.reset();
      continue;
    }
    const bool runtime_indexed_part =
        language_ == frontend::Language::SystemVerilog2017
        && (selection_expression.text == "+:"
            || selection_expression.text == "-:")
        && !static_integer_value(selection_expression.operands[1]);
    if (!runtime_indexed_part || !final_selection) {
      report(
          runtime_indexed_part
              ? "FSIM-ELAB-SVEXPR-008"
              : "FSIM-ELAB-068",
          runtime_indexed_part
              ? "a runtime-base part-select must be the final packed "
                "procedural target selection"
              : "an assignment part-select requires constant in-range "
                "bounds, a positive indexed width, and a direction "
                "compatible with the target's declared packed range",
          selection_expression.span);
      return false;
    }
    const auto width = static_integer_value(
        selection_expression.operands[2]);
    const auto range = expression_range(
        selection_source, selection_source_width);
    if (!width || *width <= 0 || *width > 64 || !range) {
      report(
          "FSIM-ELAB-SVEXPR-004",
          "a runtime-base procedural part-select requires a fixed width "
          "from 1 through 64 and an inferable packed target range",
          selection_expression.span);
      return false;
    }
    auto dynamic_base = lower_expression(
        selection_expression.operands[1], 32);
    if (!dynamic_base) {
      return false;
    }
    if (register_width(*dynamic_base) != 32) {
      *dynamic_base = resize_register(*dynamic_base, 32, true);
    }
    dynamic_part_selection = DynamicPartIndex{
        *dynamic_base, range->left, range->right,
        selected_offset, static_cast<std::uint32_t>(*width),
        selection_expression.text == "+:", range->descending};
    selected_offset = 0;
    has_selected_offset = false;
    selected_width = static_cast<std::size_t>(*width);
    selected_type.reset();
    current_type.reset();
  }
  if (dynamic_array_offset) {
    if (!selected_width || *selected_width == 0
        || *selected_width > 64 || whole_width == 0
        || whole_width - 1U
            > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
      report(
          "FSIM-ELAB-VHARRAYSEL-001",
          "runtime VHDL array target selection has an unrepresentable "
          "packed shape",
          statement.target.span);
      return false;
    }
    dynamic_part_selection = DynamicPartIndex{
        *dynamic_array_offset,
        static_cast<std::int64_t>(whole_width - 1U), 0,
        selected_offset,
        static_cast<std::uint32_t>(*selected_width),
        true, true};
    selected_offset = 0;
    has_selected_offset = false;
  }
  return true;
}

}  // namespace fsim::elaboration

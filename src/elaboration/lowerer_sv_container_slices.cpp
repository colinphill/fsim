// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

[[nodiscard]] std::uint64_t static_element_count(
    const ContainerType& type) {
  return static_cast<std::uint64_t>(
             type.index_left >= type.index_right
                 ? static_cast<std::int64_t>(type.index_left)
                       - type.index_right
                 : static_cast<std::int64_t>(type.index_right)
                       - type.index_left)
      + 1U;
}

[[nodiscard]] std::int32_t ordinal_index(
    const ContainerType& type,
    const std::uint64_t ordinal) {
  const auto step =
      type.index_left >= type.index_right
          ? -static_cast<std::int64_t>(ordinal)
          : static_cast<std::int64_t>(ordinal);
  return static_cast<std::int32_t>(
      static_cast<std::int64_t>(type.index_left) + step);
}

[[nodiscard]] bool same_element_profile(
    const ContainerType& lhs,
    const ContainerType& rhs) {
  return lhs.element_width == rhs.element_width
      && lhs.two_state == rhs.two_state
      && lhs.signed_elements == rhs.signed_elements;
}

}  // namespace

bool Lowerer::is_static_container_slice_candidate(
    const Expression& expression) const {
  if (expression.kind != ExpressionKind::Slice
      || expression.operands.size() != 3
      || expression.operands.front().kind
          != ExpressionKind::Identifier) {
    return false;
  }
  const auto& base = expression.operands.front().text;
  return container_locals_.contains(base)
      || container_objects_.contains(base);
}

std::optional<Lowerer::StaticContainerSlice>
Lowerer::static_container_slice(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Slice
      || (expression.text != ":"
          && expression.text != "+:"
          && expression.text != "-:")
      || expression.operands.size() != 3
      || expression.operands.front().kind
          != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVSLICE-001",
        "an unpacked-array slice requires a direct static-array "
        "identifier and a left:right or indexed selection",
        expression.span);
    return std::nullopt;
  }

  const auto& base = expression.operands.front();
  const auto* frontend_type = object_type(base.text);
  if (frontend_type == nullptr
      || !frontend_type->systemverilog_container) {
    report(
        "FSIM-ELAB-SVSLICE-001",
        "an unpacked-array slice requires a direct static-array "
        "object",
        base.span);
    return std::nullopt;
  }
  const auto base_type =
      container_type(*frontend_type, base.span);
  if (!base_type || !base_type->fixed) {
    report(
        "FSIM-ELAB-SVSLICE-001",
        "unpacked slicing is limited to one-dimensional static "
        "arrays",
        expression.span);
    return std::nullopt;
  }

  const auto converted_constant =
      [&](const Expression& value,
          const std::string_view role)
          -> std::optional<std::int32_t> {
        std::string error;
        const auto constant =
            evaluate_systemverilog_constant_expression(
                value, {}, {}, error);
        const auto integer =
            constant && constant->known()
                ? constant->integer_value()
                : std::nullopt;
        if (!integer
            || *integer
                < std::numeric_limits<std::int32_t>::min()
            || *integer
                > std::numeric_limits<std::int32_t>::max()) {
          report(
              "FSIM-ELAB-SVSLICE-002",
              "static-array slice " + std::string{role}
                  + " must be a locally constant known signed "
                    "32-bit integral value",
              value.span);
          return std::nullopt;
        }
        return static_cast<std::int32_t>(*integer);
      };

  const auto first =
      converted_constant(
          expression.operands[1],
          expression.text == ":"
              ? "left bound"
              : "base");
  const auto second =
      converted_constant(
          expression.operands[2],
          expression.text == ":"
              ? "right bound"
              : "width");
  if (!first || !second) {
    return std::nullopt;
  }

  auto left = static_cast<std::int64_t>(*first);
  auto right = static_cast<std::int64_t>(*second);
  if (expression.text != ":") {
    if (*second <= 0) {
      report(
          "FSIM-ELAB-SVSLICE-002",
          "a static-array indexed slice width must be positive",
          expression.operands[2].span);
      return std::nullopt;
    }
    const auto distance =
        static_cast<std::int64_t>(*second) - 1;
    const auto lower =
        expression.text == "+:"
            ? static_cast<std::int64_t>(*first)
            : static_cast<std::int64_t>(*first) - distance;
    const auto upper =
        expression.text == "+:"
            ? static_cast<std::int64_t>(*first) + distance
            : static_cast<std::int64_t>(*first);
    if (base_type->index_left >= base_type->index_right) {
      left = upper;
      right = lower;
    } else {
      left = lower;
      right = upper;
    }
  }

  const bool base_descending =
      base_type->index_left >= base_type->index_right;
  const bool selected_descending = left >= right;
  if (left != right
      && base_descending != selected_descending) {
    report(
        "FSIM-ELAB-SVSLICE-003",
        "a static-array slice must preserve the declared index "
        "direction",
        expression.span);
    return std::nullopt;
  }
  const auto base_low =
      std::min(base_type->index_left, base_type->index_right);
  const auto base_high =
      std::max(base_type->index_left, base_type->index_right);
  if (left < base_low || left > base_high
      || right < base_low || right > base_high) {
    report(
        "FSIM-ELAB-SVSLICE-003",
        "a static-array slice is outside the declared index range",
        expression.span);
    return std::nullopt;
  }

  auto selected_type = *base_type;
  selected_type.index_left =
      static_cast<std::int32_t>(left);
  selected_type.index_right =
      static_cast<std::int32_t>(right);
  return StaticContainerSlice{
      *base_type, std::move(selected_type)};
}

void Lowerer::copy_static_container_ordinals(
    const ContainerRegisterId destination,
    const ContainerType& destination_range,
    const ContainerRegisterId source,
    const ContainerType& source_range) {
  const auto count = static_element_count(source_range);
  for (std::uint64_t ordinal = 0;
       ordinal < count;
       ++ordinal) {
    const auto source_index =
        allocate_register(32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        source_index,
        unsigned_value(
            static_cast<std::uint32_t>(
                ordinal_index(source_range, ordinal)),
            32)});
    const auto value = allocate_register(
        source_range.element_width,
        source_range.two_state
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(ContainerRead{
        value, source, source_index, true});

    const auto destination_index =
        allocate_register(32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        destination_index,
        unsigned_value(
            static_cast<std::uint32_t>(
                ordinal_index(destination_range, ordinal)),
            32)});
    process_.operations.emplace_back(ContainerWrite{
        destination, destination_index, value, true});
  }
}

std::optional<Lowerer::LoweredStaticContainer>
Lowerer::lower_static_container_value(
    const Expression& expression) {
  if (expression.kind == ExpressionKind::Call) {
    const auto* function = visible_function(expression.text);
    if (function == nullptr
        || !function->return_type.systemverilog_container) {
      report(
          "FSIM-ELAB-SVSLICE-004",
          "a static-array value requires a whole static array, direct "
          "slice, or fixed-array function call",
          expression.span);
      return std::nullopt;
    }
    const auto type =
        container_type(function->return_type, expression.span);
    if (!type) {
      return std::nullopt;
    }
    if (!type->fixed) {
      report(
          "FSIM-ELAB-SVSLICE-006",
          "a static-array assignment requires a fixed-array function "
          "result with a compatible element profile",
          expression.span);
      return std::nullopt;
    }
    const auto value =
        lower_user_container_function_expression(expression);
    if (!value) {
      return std::nullopt;
    }
    return LoweredStaticContainer{*value, *type};
  }

  if (expression.kind == ExpressionKind::Identifier) {
    const auto* frontend_type = object_type(expression.text);
    if (frontend_type == nullptr
        || !frontend_type->systemverilog_container) {
      report(
          "FSIM-ELAB-SVSLICE-004",
          "a static-array slice assignment requires a whole static "
          "array or direct static-array slice value",
          expression.span);
      return std::nullopt;
    }
    const auto type =
        container_type(*frontend_type, expression.span);
    if (!type || !type->fixed) {
      report(
          "FSIM-ELAB-SVSLICE-004",
          "a static-array slice assignment source must be a fixed "
          "one-dimensional array",
          expression.span);
      return std::nullopt;
    }
    const auto value = lower_container_expression(expression);
    if (!value) {
      return std::nullopt;
    }
    return LoweredStaticContainer{*value, *type};
  }

  if (expression.kind != ExpressionKind::Slice) {
    report(
        "FSIM-ELAB-SVSLICE-004",
        "a static-array slice assignment requires a whole static "
        "array or direct static-array slice value",
        expression.span);
    return std::nullopt;
  }
  const auto slice = static_container_slice(expression);
  if (!slice) {
    return std::nullopt;
  }
  const auto source =
      lower_container_expression(expression.operands.front());
  if (!source) {
    return std::nullopt;
  }
  const auto snapshot =
      allocate_container_register(slice->selected_type);
  copy_static_container_ordinals(
      snapshot,
      slice->selected_type,
      *source,
      slice->selected_type);
  return LoweredStaticContainer{
      snapshot, slice->selected_type};
}

std::optional<ContainerRegisterId>
Lowerer::lower_static_container_assignment_value(
    const Expression& expression,
    const ContainerType& destination_type) {
  if (!destination_type.fixed) {
    report(
        "FSIM-ELAB-SVSLICE-004",
        "a static-array slice value requires a fixed "
        "one-dimensional destination",
        expression.span);
    return std::nullopt;
  }
  const auto source = lower_static_container_value(expression);
  if (!source) {
    return std::nullopt;
  }
  if (static_element_count(source->type)
      != static_element_count(destination_type)) {
    report(
        "FSIM-ELAB-SVSLICE-005",
        "static-array slice assignment requires equal source and "
        "destination element counts",
        expression.span);
    return std::nullopt;
  }
  if (!same_element_profile(
          source->type, destination_type)) {
    report(
        "FSIM-ELAB-SVSLICE-006",
        "static-array slice assignment requires identical element "
        "width, signedness, and state domain",
        expression.span);
    return std::nullopt;
  }

  const auto destination =
      allocate_container_register(destination_type);
  copy_static_container_ordinals(
      destination,
      destination_type,
      source->value,
      source->type);
  return destination;
}

bool Lowerer::lower_static_container_slice_assignment(
    const Expression& target_expression,
    const Expression& value_expression,
    const ContainerRegisterId target,
    const ContainerType& target_type) {
  const auto selection =
      static_container_slice(target_expression);
  if (!selection) {
    return false;
  }
  if (selection->base_type != target_type) {
    report(
        "FSIM-ELAB-SVSLICE-001",
        "a static-array slice target must resolve to its direct "
        "container object",
        target_expression.span);
    return false;
  }

  const auto source =
      lower_static_container_value(value_expression);
  if (!source) {
    return false;
  }
  if (static_element_count(source->type)
      != static_element_count(selection->selected_type)) {
    report(
        "FSIM-ELAB-SVSLICE-005",
        "static-array slice assignment requires equal source and "
        "destination element counts",
        value_expression.span);
    return false;
  }
  if (!same_element_profile(
          source->type, selection->selected_type)) {
    report(
        "FSIM-ELAB-SVSLICE-006",
        "static-array slice assignment requires identical element "
        "width, signedness, and state domain",
        value_expression.span);
    return false;
  }

  const auto selected_value =
      allocate_container_register(selection->selected_type);
  copy_static_container_ordinals(
      selected_value,
      selection->selected_type,
      source->value,
      source->type);

  const auto replacement =
      allocate_container_register(target_type);
  process_.operations.emplace_back(
      CopyContainerRegister{replacement, target});
  copy_static_container_ordinals(
      replacement,
      selection->selected_type,
      selected_value,
      selection->selected_type);
  process_.operations.emplace_back(
      CopyContainerRegister{target, replacement});
  return true;
}

}  // namespace fsim::elaboration

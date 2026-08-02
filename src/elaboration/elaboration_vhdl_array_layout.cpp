// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

bool substitute_vhdl_array_layout(
    frontend::Type& type,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
  if (!type.vhdl_array) {
    return false;
  }
  auto& array = *type.vhdl_array;
  array.flat_width.reset();
  type.packed_range.reset();
  if (array.element_types.empty()) {
    diagnostics.push_back({
        "FSIM-ELAB-VHARRAY-001",
        "VHDL array type '" + type.spelling
            + "' does not retain a concrete element subtype",
        array.element_span});
    type.packed_range_expression.reset();
    return true;
  }

  auto& element = array.element_types.front();
  substitute_parameters(
      element, environment, domains, diagnostics, language);
  const auto element_width = element.width();
  if (!element_width || *element_width == 0
      || element.domain == frontend::ValueDomain::Unknown
      || element.domain == frontend::ValueDomain::Integer
      || element.domain == frontend::ValueDomain::String) {
    diagnostics.push_back({
        "FSIM-ELAB-VHARRAY-001",
        "VHDL array type '" + type.spelling
            + "' requires a concrete bounded scalar or packed composite "
              "element subtype",
        array.element_span});
    type.packed_range_expression.reset();
    return true;
  }
  array.element_domain = element.domain;
  array.element_spelling = element.spelling;
  array.element_named_type.clear();
  type.domain = element.domain;

  bool valid = true;
  bool concrete = true;
  for (auto& dimension : array.dimensions) {
    dimension.stride = 0;
    if (!dimension.constraint) {
      if (!dimension.range) {
        concrete = false;
        dimension.unconstrained = true;
      } else {
        dimension.unconstrained = false;
      }
      continue;
    }
    dimension.range.reset();
    dimension.null = false;
    std::string error;
    const auto left = evaluate_constant_expression(
        dimension.constraint->left, environment, error);
    const auto right = left
        ? evaluate_constant_expression(
              dimension.constraint->right, environment, error)
        : std::nullopt;
    if (!left || !right) {
      diagnostics.push_back({
          "FSIM-ELAB-GENERIC-006",
          "cannot evaluate VHDL array dimension constraint: " + error,
          dimension.constraint->span});
      dimension.constraint.reset();
      valid = false;
      concrete = false;
      continue;
    }
    const auto span = dimension.constraint->span;
    dimension.range = frontend::IntegerRange{
        *left, *right, dimension.constraint->descending};
    dimension.null = dimension.range->descending
        ? dimension.range->left < dimension.range->right
        : dimension.range->left > dimension.range->right;
    dimension.unconstrained = false;
    dimension.constraint.reset();
    if (dimension.index_base_range
        && (!dimension.index_base_range->contains(*left)
            || !dimension.index_base_range->contains(*right))) {
      diagnostics.push_back({
          "FSIM-ELAB-VHARRAY-003",
          "VHDL array constraint lies outside index subtype '"
              + dimension.index_subtype + "'",
          span});
      valid = false;
    }
  }
  array.unconstrained = std::ranges::any_of(
      array.dimensions,
      [](const frontend::VhdlArrayDimension& dimension) {
        return dimension.unconstrained;
      });
  if (!valid || !concrete) {
    type.packed_range_expression.reset();
    return true;
  }

  auto flat_width = *element_width;
  for (auto dimension = array.dimensions.rbegin();
       dimension != array.dimensions.rend(); ++dimension) {
    dimension->stride = flat_width;
    if (dimension->null) {
      flat_width = 0;
      continue;
    }
    const auto unsigned_left =
        static_cast<std::uint64_t>(dimension->range->left);
    const auto unsigned_right =
        static_cast<std::uint64_t>(dimension->range->right);
    const auto distance =
        dimension->range->left >= dimension->range->right
            ? unsigned_left - unsigned_right
            : unsigned_right - unsigned_left;
    if (distance == std::numeric_limits<std::uint64_t>::max()) {
      valid = false;
      break;
    }
    const auto count = distance + 1U;
    if (flat_width != 0
        && count
            > std::numeric_limits<std::uint64_t>::max() / flat_width) {
      valid = false;
      break;
    }
    flat_width *= count;
  }
  if (!valid
      || (flat_width != 0
          && flat_width - 1U
              > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()))) {
    diagnostics.push_back({
        "FSIM-ELAB-VHARRAY-004",
        "VHDL array layout overflows fsim's packed runtime representation",
        array.dimensions.front().index_span});
    type.packed_range_expression.reset();
    return true;
  }

  array.flat_width = flat_width;
  if (array.dimensions.size() == 1U) {
    const auto& range = *array.dimensions.front().range;
    if (!array.dimensions.front().null) {
      type.packed_range = frontend::PackedRange{
          range.left, range.right, range.descending};
    }
  } else if (flat_width != 0) {
    type.packed_range = frontend::PackedRange{
        static_cast<std::int64_t>(flat_width - 1U), 0, true};
  }
  type.packed_range_expression.reset();
  array.index_subtype = array.dimensions.front().index_subtype;
  array.index_span = array.dimensions.front().index_span;
  array.index_base_range = array.dimensions.front().index_base_range;
  return true;
}

}  // namespace fsim::elaboration::elaboration_detail

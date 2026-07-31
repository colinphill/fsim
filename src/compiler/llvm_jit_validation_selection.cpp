// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <limits>
#include <optional>
#include <string>

namespace fsim::compiler::llvm_detail {

using namespace runtime::simir;

namespace {

[[nodiscard]] std::optional<std::uint64_t>
dynamic_range_width(const DynamicIndex& selection) {
  if (selection.left < std::numeric_limits<std::int32_t>::min()
      || selection.left > std::numeric_limits<std::int32_t>::max()
      || selection.right < std::numeric_limits<std::int32_t>::min()
      || selection.right > std::numeric_limits<std::int32_t>::max()) {
    return std::nullopt;
  }
  const auto left = static_cast<std::int64_t>(selection.left);
  const auto right = static_cast<std::int64_t>(selection.right);
  return static_cast<std::uint64_t>(
             left >= right ? left - right : right - left)
      + 1U;
}

}  // namespace

[[nodiscard]] std::optional<std::string>
validate_dynamic_index_metadata(const DynamicIndex& selection) {
  if (!dynamic_range_width(selection)) {
    return "dynamic index bounds must fit signed 32-bit integers";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_dynamic_index_bounds(const DynamicIndex& selection,
                              const std::uint64_t target_width) {
  const auto range_width = dynamic_range_width(selection);
  if (!range_width) {
    return "dynamic index bounds must fit signed 32-bit integers";
  }
  if (selection.base_offset > target_width
      || *range_width > target_width - selection.base_offset) {
    return "dynamic index range is outside its packed target";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_dynamic_part_select_metadata(const DynamicPartSelect& operation) {
  if (operation.width == 0 || operation.width > 64) {
    return "DynamicPartSelect width must be from 1 through 64";
  }
  if (operation.left < std::numeric_limits<std::int32_t>::min()
      || operation.left > std::numeric_limits<std::int32_t>::max()
      || operation.right < std::numeric_limits<std::int32_t>::min()
      || operation.right > std::numeric_limits<std::int32_t>::max()) {
    return "DynamicPartSelect bounds must fit signed 32-bit integers";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_dynamic_part_select_source_width(
    const DynamicPartSelect& operation,
    const std::uint32_t source_width) {
  const auto left = static_cast<std::int64_t>(operation.left);
  const auto right = static_cast<std::int64_t>(operation.right);
  const auto range_width = static_cast<std::uint64_t>(
                               left >= right ? left - right : right - left)
      + 1U;
  if (range_width != source_width) {
    return "DynamicPartSelect declared range does not match its source "
           "register width";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_expression_profile_metadata(
    const std::span<const ExpressionProfile> profiles) {
  for (const auto& profile : profiles) {
    if (profile.width == 0) {
      return "expression profile width must be greater than zero at "
          + profile.source.path + ":"
          + std::to_string(profile.source.line) + ":"
          + std::to_string(profile.source.column);
    }
    switch (profile.sizing) {
    case ExpressionSizingKind::self_determined:
    case ExpressionSizingKind::context_determined:
      break;
    default:
      return "expression profile has an invalid sizing kind";
    }
    switch (profile.domain) {
    case ExpressionValueDomain::two_state:
    case ExpressionValueDomain::four_state:
    case ExpressionValueDomain::nine_state:
    case ExpressionValueDomain::integer:
    case ExpressionValueDomain::boolean:
      break;
    default:
      return "expression profile has an invalid value domain";
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_extract_bounds(const Extract& operation,
                        const std::uint32_t source_width) {
  if (operation.offset > source_width
      || operation.width > source_width - operation.offset) {
    return "Extract range is outside its source register";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_insert_bounds(const Insert& operation,
                       const std::uint32_t target_width,
                       const std::uint32_t source_width) {
  if (operation.offset > target_width
      || source_width > target_width - operation.offset) {
    return "Insert range is outside its target register";
  }
  return std::nullopt;
}

}  // namespace fsim::compiler::llvm_detail

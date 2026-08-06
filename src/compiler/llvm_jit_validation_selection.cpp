// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <limits>
#include <optional>
#include <string>

namespace fsim::compiler::llvm_detail {

using namespace runtime::simir;

namespace {

[[nodiscard]] std::optional<std::uint64_t>
dynamic_range_width(const std::int64_t left, const std::int64_t right) {
  if (left < std::numeric_limits<std::int32_t>::min()
      || left > std::numeric_limits<std::int32_t>::max()
      || right < std::numeric_limits<std::int32_t>::min()
      || right > std::numeric_limits<std::int32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(
             left >= right ? left - right : right - left)
      + 1U;
}

[[nodiscard]] std::optional<std::uint64_t>
dynamic_range_width(const DynamicIndex& selection) {
  return dynamic_range_width(selection.left, selection.right);
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
  const auto range_width =
      dynamic_range_width(operation.left, operation.right);
  if (!range_width) {
    return "DynamicPartSelect bounds must fit signed 32-bit integers";
  }
  if (operation.base_offset > source_width
      || *range_width > source_width - operation.base_offset) {
    return "DynamicPartSelect declared range is outside its source "
           "register";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_dynamic_part_index_metadata(const DynamicPartIndex& selection) {
  if (selection.width == 0 || selection.width > 64) {
    return "dynamic part-select write width must be from 1 through 64";
  }
  if (!dynamic_range_width(selection.left, selection.right)) {
    return "dynamic part-select write bounds must fit signed 32-bit integers";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_dynamic_part_index_bounds(const DynamicPartIndex& selection,
                                   const std::uint64_t target_width) {
  if (const auto error = validate_dynamic_part_index_metadata(selection)) {
    return error;
  }
  const auto range_width =
      *dynamic_range_width(selection.left, selection.right);
  if (selection.base_offset > target_width
      || range_width > target_width - selection.base_offset) {
    return "dynamic part-select write range is outside its packed target";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<OperationValidationError>
validate_selection_operation_bounds(
    const Process& process,
    const std::span<const std::uint32_t> register_widths,
    const std::span<const std::uint32_t> signal_widths) {
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    const auto& operation = process.operations[index];
    std::optional<std::string> error;
    if (const auto* extract = fsim::runtime::simir::operation_get_if<Extract>(&operation)) {
      error = validate_extract_bounds(
          *extract, register_widths[extract->source]);
    } else if (const auto* dynamic_extract =
                   fsim::runtime::simir::operation_get_if<DynamicExtract>(&operation)) {
      error = validate_dynamic_index_bounds(
          dynamic_extract->selection,
          register_widths[dynamic_extract->source]);
    } else if (const auto* part_select =
                   fsim::runtime::simir::operation_get_if<DynamicPartSelect>(&operation)) {
      error = validate_dynamic_part_select_source_width(
          *part_select, register_widths[part_select->source]);
    } else if (const auto* insert = fsim::runtime::simir::operation_get_if<Insert>(&operation)) {
      error = validate_insert_bounds(
          *insert, register_widths[insert->target],
          register_widths[insert->source]);
    } else if (const auto* dynamic_insert =
                   fsim::runtime::simir::operation_get_if<DynamicInsert>(&operation)) {
      error = validate_dynamic_index_bounds(
          dynamic_insert->selection,
          register_widths[dynamic_insert->target]);
    } else if (const auto* part_insert =
                   fsim::runtime::simir::operation_get_if<DynamicPartInsert>(&operation)) {
      error = validate_dynamic_part_index_bounds(
          part_insert->selection, register_widths[part_insert->target]);
    } else if (const auto* force =
                   fsim::runtime::simir::operation_get_if<ForceSignalSlice>(&operation)) {
      const auto target_width = signal_widths[force->signal];
      const auto source_width = register_widths[force->source];
      if (force->selection) {
        error = validate_dynamic_index_bounds(
            *force->selection, target_width);
      } else if (force->offset > target_width
          || source_width > target_width - force->offset) {
        error = "ForceSignalSlice range is outside its signal";
      }
    } else if (const auto* release =
                   fsim::runtime::simir::operation_get_if<ReleaseSignalSlice>(&operation)) {
      const auto target_width = signal_widths[release->signal];
      if (release->selection) {
        error = validate_dynamic_index_bounds(
            *release->selection, target_width);
      } else if (release->offset > target_width
          || release->width > target_width - release->offset) {
        error = "ReleaseSignalSlice range is outside its signal";
      }
    }
    if (error) {
      return OperationValidationError{index, std::move(*error)};
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_expression_profile_metadata(
    const std::span<const ExpressionProfile> profiles) {
  for (const auto& profile : profiles) {
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

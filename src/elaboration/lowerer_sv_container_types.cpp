// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<ContainerType> Lowerer::container_type(
    const frontend::Type& type,
    const frontend::SourceSpan& span) {
  if (!type.systemverilog_container) return std::nullopt;
  const auto width = type.width();
  if (!width || *width == 0 || *width > 64
      || type.packed_aggregate
          == frontend::PackedAggregateKind::UnpackedStruct
      || type.domain == frontend::ValueDomain::String) {
    report(
        "FSIM-ELAB-SVCONTAINER-003",
        "container elements must be bounded integral, enum, or packed "
        "aggregate values with an executable width in 1..64",
        span);
    return std::nullopt;
  }
  ContainerType result;
  result.element_width = static_cast<std::uint32_t>(*width);
  result.element_nominal_type = type.nominal_type;
  result.two_state = is_two_state_domain(type.domain);
  result.signed_elements = type.is_signed;
  result.queue = type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::Queue;
  result.associative = type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::AssociativeArray;
  result.fixed = type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::StaticArray;
  if (result.associative) {
    const auto& index_type =
        type.systemverilog_container->associative_index_type;
    const auto index_width = index_type ? index_type->width() : std::nullopt;
    if (!index_type || !index_width || *index_width == 0
        || *index_width > 64
        || index_type->domain == frontend::ValueDomain::String
        || index_type->domain == frontend::ValueDomain::Unknown
        || !index_type->packed_members.empty() || index_type->vhdl_array) {
      report(
          "FSIM-ELAB-SVCONTAINER-013",
          "associative-array indices require a resolved integral scalar "
          "type with width in 1..64",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    result.index_width = static_cast<std::uint32_t>(*index_width);
    result.two_state_indices = is_two_state_domain(index_type->domain);
    result.signed_indices = index_type->is_signed;
  }
  if (type.systemverilog_container->queue_maximum) {
    const auto& maximum_expression =
        *type.systemverilog_container->queue_maximum;
    auto maximum_index = constant_index(maximum_expression);
    if (!maximum_index) {
      std::string error;
      const auto value = evaluate_systemverilog_constant_expression(
          maximum_expression, {}, {}, error);
      maximum_index = value ? value->integer_value() : std::nullopt;
    }
    if (!maximum_index || *maximum_index < 0) {
      report(
          "FSIM-ELAB-SVCONTAINER-004",
          "bounded queue maximum index must be a known nonnegative value",
          type.systemverilog_container->queue_maximum->span);
      return std::nullopt;
    }
    result.maximum_elements = static_cast<std::uint64_t>(*maximum_index) + 1U;
  }
  if (result.fixed) {
    const auto bound_value = [](const Expression& expression) {
      if (const auto simple = constant_index(expression)) return simple;
      std::string error;
      const auto value = evaluate_systemverilog_constant_expression(
          expression, {}, {}, error);
      return value ? value->integer_value()
                   : std::optional<std::int64_t>{};
    };
    const auto in_int32 = [](const std::int64_t value) {
      return value >= std::numeric_limits<std::int32_t>::min()
          && value <= std::numeric_limits<std::int32_t>::max();
    };
    const auto& ranges =
        type.systemverilog_container->static_range_expressions;
    if (ranges.empty()) {
      if (const auto& concrete = type.systemverilog_container->static_range) {
        result.index_left = static_cast<std::int32_t>(concrete->left);
        result.index_right = static_cast<std::int32_t>(concrete->right);
        const auto count = static_cast<std::uint64_t>(
            concrete->left >= concrete->right
                ? concrete->left - concrete->right
                : concrete->right - concrete->left) + 1U;
        if (count > maximum_container_elements(result)) {
          report(
              "FSIM-ELAB-SVCONTAINER-020",
              "static unpacked array exceeds the per-container "
              "owning-storage budget",
              type.systemverilog_container->span);
          return std::nullopt;
        }
        result.dimensions.push_back(
            ContainerDimension{result.index_left, result.index_right});
        return result;
      }
      report(
          "FSIM-ELAB-SVCONTAINER-020",
          "static unpacked-array bounds must be locally constant "
          "32-bit integral values",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    std::uint64_t total = 1;
    for (const auto& range : ranges) {
      const auto left = bound_value(range.left);
      const auto right = bound_value(range.right);
      if (!left || !right || !in_int32(*left) || !in_int32(*right)) {
        report(
            "FSIM-ELAB-SVCONTAINER-020",
            "static unpacked-array bounds must be locally constant "
            "32-bit integral values",
            range.span);
        return std::nullopt;
      }
      const auto count = *left >= *right
          ? static_cast<std::uint64_t>(*left - *right) + 1U
          : static_cast<std::uint64_t>(*right - *left) + 1U;
      const auto storage_limit = maximum_container_elements(result);
      if (count > storage_limit || total > storage_limit / count) {
        report(
            "FSIM-ELAB-SVCONTAINER-020",
            "static unpacked array exceeds the per-container "
            "owning-storage budget",
            type.systemverilog_container->span);
        return std::nullopt;
      }
      total *= count;
      result.dimensions.push_back(ContainerDimension{
          static_cast<std::int32_t>(*left),
          static_cast<std::int32_t>(*right)});
    }
    result.index_left = result.dimensions.front().first;
    result.index_right = result.dimensions.front().second;
  }
  return result;
}

}  // namespace fsim::elaboration

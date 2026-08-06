// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
namespace {

[[nodiscard]] bool two_state(
    const frontend::ValueDomain domain) noexcept {
  return domain == frontend::ValueDomain::Bit2
      || domain == frontend::ValueDomain::Integer;
}

}  // namespace

std::optional<runtime::simir::ContainerType>
materialize_systemverilog_container_type(
    const frontend::Type& type,
    const frontend::SourceSpan& span,
    const SystemVerilogContainerConstantEvaluator& evaluate,
    const SystemVerilogContainerReporter& report) {
  using namespace runtime::simir;
  if (!type.systemverilog_container) return std::nullopt;
  const auto& source = *type.systemverilog_container;
  const auto* element = &type;
  if (source.element_types.size() == 1) {
    element = &source.element_types.front();
  } else if (!source.element_types.empty()) {
    report(
        "FSIM-ELAB-SVCONTAINER-003",
        "a container declaration must retain exactly one element type",
        span);
    return std::nullopt;
  }

  std::function<std::optional<ContainerType>(
      const frontend::Type&, bool)> profile;
  profile = [&](const frontend::Type& candidate,
                const bool box_leaf) -> std::optional<ContainerType> {
    if (candidate.systemverilog_container) {
      return materialize_systemverilog_container_type(
          candidate, span, evaluate, report);
    }
    ContainerType result;
    result.element_nominal_type = candidate.nominal_type;
    if (candidate.packed_aggregate
        == frontend::PackedAggregateKind::UnpackedStruct) {
      result.element_kind = ContainerElementKind::Aggregate;
      result.element_width = 0;
      result.element_nominal_type = candidate.nominal_type.empty()
          ? candidate.spelling : candidate.nominal_type;
      for (const auto& member : candidate.packed_members) {
        if (member.nested_types.size() != 1) {
          report(
              "FSIM-ELAB-SVCONTAINER-003",
              "an unpacked aggregate member must retain one complete type",
              member.span);
          return std::nullopt;
        }
        auto member_type = profile(member.nested_types.front(), true);
        if (!member_type) return std::nullopt;
        result.element_types.push_back(std::move(*member_type));
        result.member_names.push_back(member.name);
      }
      return result;
    }
    if (candidate.domain == frontend::ValueDomain::String) {
      result.element_kind = ContainerElementKind::String;
      result.element_width = 0;
    } else {
      const auto width = candidate.width();
      if (!width || *width == 0
          || *width > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVCONTAINER-003",
            "container elements require an executable scalar, string, "
            "packed aggregate, unpacked aggregate, or container type",
            span);
        return std::nullopt;
      }
      result.element_width = static_cast<std::uint32_t>(*width);
      result.two_state = two_state(candidate.domain);
      result.signed_elements = candidate.is_signed;
      if (candidate.systemverilog_scalar
          != frontend::SystemVerilogScalarKind::None) {
        result.element_kind = ContainerElementKind::Scalar;
        result.scalar_kind = candidate.systemverilog_scalar;
        result.two_state = true;
      }
    }
    if (box_leaf) {
      result.fixed = true;
      result.index_left = 0;
      result.index_right = 0;
      result.dimensions.push_back({0, 0});
    }
    return result;
  };

  ContainerType result;
  if (element->systemverilog_container) {
    auto nested = profile(*element, false);
    if (!nested) return std::nullopt;
    result.element_kind = ContainerElementKind::Container;
    result.element_width = 0;
    result.element_types.push_back(std::move(*nested));
  } else {
    auto element_type = profile(*element, false);
    if (!element_type) return std::nullopt;
    result.element_kind = element_type->element_kind;
    result.scalar_kind = element_type->scalar_kind;
    result.element_width = element_type->element_width;
    result.two_state = element_type->two_state;
    result.signed_elements = element_type->signed_elements;
    result.element_nominal_type = element_type->element_nominal_type;
    result.element_types = std::move(element_type->element_types);
    result.member_names = std::move(element_type->member_names);
  }
  result.queue = source.kind
      == frontend::SystemVerilogContainerKind::Queue;
  result.associative = source.kind
      == frontend::SystemVerilogContainerKind::AssociativeArray;
  result.fixed = source.kind
      == frontend::SystemVerilogContainerKind::StaticArray;

  if (result.associative) {
    const auto& index_type = source.associative_index_type;
    const auto width = index_type ? index_type->width() : std::nullopt;
    if (!index_type || !width || *width == 0 || *width > 64
        || index_type->domain == frontend::ValueDomain::String
        || index_type->domain == frontend::ValueDomain::Unknown
        || !index_type->packed_members.empty() || index_type->vhdl_array) {
      report(
          "FSIM-ELAB-SVCONTAINER-013",
          "associative-array indices require a resolved integral scalar "
          "type with width in 1..64",
          source.span);
      return std::nullopt;
    }
    result.index_width = static_cast<std::uint32_t>(*width);
    result.two_state_indices = two_state(index_type->domain);
    result.signed_indices = index_type->is_signed;
  }

  if (source.queue_maximum) {
    const auto maximum_index = evaluate(*source.queue_maximum);
    if (!maximum_index || *maximum_index < 0
        || static_cast<std::uint64_t>(*maximum_index)
               == std::numeric_limits<std::uint64_t>::max()) {
      report(
          "FSIM-ELAB-SVCONTAINER-004",
          "bounded queue maximum index must be a known nonnegative value",
          source.queue_maximum->span);
      return std::nullopt;
    }
    result.maximum_elements =
        static_cast<std::uint64_t>(*maximum_index) + 1U;
  }

  if (!result.fixed) return result;
  const auto& ranges = source.static_range_expressions;
  if (ranges.empty() && source.static_range) {
    const auto& range = *source.static_range;
    if (range.left < std::numeric_limits<std::int32_t>::min()
        || range.left > std::numeric_limits<std::int32_t>::max()
        || range.right < std::numeric_limits<std::int32_t>::min()
        || range.right > std::numeric_limits<std::int32_t>::max()) {
      report(
          "FSIM-ELAB-SVCONTAINER-020",
          "static unpacked-array bounds must be 32-bit integral values",
          source.span);
      return std::nullopt;
    }
    result.index_left = static_cast<std::int32_t>(range.left);
    result.index_right = static_cast<std::int32_t>(range.right);
    result.dimensions.push_back(
        {result.index_left, result.index_right});
  } else {
    std::uint64_t total = 1;
    for (const auto& range : ranges) {
      const auto left = evaluate(range.left);
      const auto right = evaluate(range.right);
      if (!left || !right
          || *left < std::numeric_limits<std::int32_t>::min()
          || *left > std::numeric_limits<std::int32_t>::max()
          || *right < std::numeric_limits<std::int32_t>::min()
          || *right > std::numeric_limits<std::int32_t>::max()) {
        report(
            "FSIM-ELAB-SVCONTAINER-020",
            "static unpacked-array bounds must be locally constant "
            "32-bit integral values",
            range.span);
        return std::nullopt;
      }
      const auto count = static_cast<std::uint64_t>(
          *left >= *right ? *left - *right : *right - *left) + 1U;
      const auto limit = maximum_container_elements(result);
      if (count > limit || total > limit / count) {
        report(
            "FSIM-ELAB-SVCONTAINER-020",
            "static unpacked array exceeds the per-container "
            "owning-storage budget",
            source.span);
        return std::nullopt;
      }
      total *= count;
      result.dimensions.push_back({
          static_cast<std::int32_t>(*left),
          static_cast<std::int32_t>(*right)});
    }
    if (result.dimensions.empty()) {
      report(
          "FSIM-ELAB-SVCONTAINER-020",
          "static unpacked-array bounds must be locally constant",
          source.span);
      return std::nullopt;
    }
    result.index_left = result.dimensions.front().first;
    result.index_right = result.dimensions.front().second;
  }
  try {
    (void)default_container_value(result);
  } catch (const std::exception& error) {
    report(
        "FSIM-ELAB-SVCONTAINER-020",
        error.what(), source.span);
    return std::nullopt;
  }
  return result;
}

}  // namespace fsim::elaboration::elaboration_detail

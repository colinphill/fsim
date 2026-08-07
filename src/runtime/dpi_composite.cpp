// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_marshalling.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace fsim::runtime {

namespace {

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) {
  if (right > std::numeric_limits<std::size_t>::max() - left) return false;
  result = left + right;
  return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) {
  if (left != 0
      && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

struct CompositeLimits {
  std::size_t elements;
  std::size_t bits;
  std::size_t depth;
};

SystemVerilogDpiMarshallingError measure(
    const SystemVerilogDpiTypeDescriptor& descriptor,
    const CompositeLimits limits,
    const std::size_t depth,
    SystemVerilogDpiCompositeLayout& layout) {
  if (depth > limits.depth) {
    return SystemVerilogDpiMarshallingError::DepthLimit;
  }
  if (descriptor.kind == SystemVerilogDpiCompositeKind::Scalar
      || descriptor.kind == SystemVerilogDpiCompositeKind::Enum) {
    if (descriptor.width == 0 || !descriptor.children.empty()
        || !descriptor.member_names.empty()
        || (descriptor.kind == SystemVerilogDpiCompositeKind::Scalar
            && !descriptor.enum_values.empty())) {
      return SystemVerilogDpiMarshallingError::InvalidDescriptor;
    }
    if (descriptor.width > limits.bits) {
      return SystemVerilogDpiMarshallingError::LayoutOverflow;
    }
    if (descriptor.kind == SystemVerilogDpiCompositeKind::Enum) {
      if (descriptor.width > 64 || descriptor.enum_values.empty()
          || std::set<std::uint64_t>(
                 descriptor.enum_values.begin(),
                 descriptor.enum_values.end()).size()
              != descriptor.enum_values.size()) {
        return SystemVerilogDpiMarshallingError::InvalidDescriptor;
      }
      const auto maximum = descriptor.width == 64
          ? std::numeric_limits<std::uint64_t>::max()
          : (std::uint64_t{1} << descriptor.width) - 1U;
      if (std::ranges::any_of(
              descriptor.enum_values,
              [&](const std::uint64_t value) { return value > maximum; })) {
        return SystemVerilogDpiMarshallingError::InvalidDescriptor;
      }
    }
    const auto words = descriptor.width / 32U
        + static_cast<std::size_t>(descriptor.width % 32U != 0);
    layout = {
        1,
        descriptor.width,
        words * sizeof(std::uint32_t)
            * (descriptor.scalar_domain
                    == SystemVerilogDpiScalarDomain::Logic4
                ? 2U
                : 1U)};
    return {};
  }

  if (descriptor.kind == SystemVerilogDpiCompositeKind::FixedArray) {
    if (descriptor.element_count == 0 || descriptor.children.size() != 1
        || !descriptor.member_names.empty()
        || !descriptor.enum_values.empty()) {
      return SystemVerilogDpiMarshallingError::InvalidDescriptor;
    }
    SystemVerilogDpiCompositeLayout element;
    const auto error = measure(
        descriptor.children.front(), limits, depth + 1U, element);
    if (error != SystemVerilogDpiMarshallingError::None) return error;
    if (!checked_multiply(
            element.leaf_count, descriptor.element_count,
            layout.leaf_count)
        || !checked_multiply(
            element.total_bits, descriptor.element_count,
            layout.total_bits)
        || !checked_multiply(
            element.payload_bytes, descriptor.element_count,
            layout.payload_bytes)) {
      return SystemVerilogDpiMarshallingError::LayoutOverflow;
    }
  } else if (descriptor.kind == SystemVerilogDpiCompositeKind::Struct) {
    if (descriptor.children.empty()
        || descriptor.children.size() != descriptor.member_names.size()
        || !descriptor.enum_values.empty()
        || std::set<std::string>(
               descriptor.member_names.begin(),
               descriptor.member_names.end()).size()
            != descriptor.member_names.size()) {
      return SystemVerilogDpiMarshallingError::InvalidDescriptor;
    }
    for (const auto& child : descriptor.children) {
      SystemVerilogDpiCompositeLayout field;
      const auto error = measure(child, limits, depth + 1U, field);
      if (error != SystemVerilogDpiMarshallingError::None) return error;
      if (!checked_add(layout.leaf_count, field.leaf_count, layout.leaf_count)
          || !checked_add(
              layout.total_bits, field.total_bits, layout.total_bits)
          || !checked_add(
              layout.payload_bytes, field.payload_bytes,
              layout.payload_bytes)) {
        return SystemVerilogDpiMarshallingError::LayoutOverflow;
      }
    }
  } else {
    return SystemVerilogDpiMarshallingError::InvalidDescriptor;
  }
  if (layout.leaf_count > limits.elements) {
    return SystemVerilogDpiMarshallingError::ElementCount;
  }
  if (layout.total_bits > limits.bits) {
    return SystemVerilogDpiMarshallingError::LayoutOverflow;
  }
  return {};
}

void flatten_descriptors(
    const SystemVerilogDpiTypeDescriptor& descriptor,
    std::vector<const SystemVerilogDpiTypeDescriptor*>& leaves) {
  if (descriptor.kind == SystemVerilogDpiCompositeKind::Scalar
      || descriptor.kind == SystemVerilogDpiCompositeKind::Enum) {
    leaves.push_back(&descriptor);
  } else if (descriptor.kind == SystemVerilogDpiCompositeKind::FixedArray) {
    for (std::size_t index{}; index < descriptor.element_count; ++index) {
      flatten_descriptors(descriptor.children.front(), leaves);
    }
  } else {
    for (const auto& child : descriptor.children) {
      flatten_descriptors(child, leaves);
    }
  }
}

[[nodiscard]] std::optional<std::uint64_t> enum_value(
    const PackedLogic4& value) {
  if (value.width() > 64) return std::nullopt;
  std::uint64_t result{};
  for (std::size_t index{}; index < value.width(); ++index) {
    const auto state = value.get(index);
    if (state == Logic4::x || state == Logic4::z) return std::nullopt;
    if (state == Logic4::one) result |= std::uint64_t{1} << index;
  }
  return result;
}

}  // namespace

SystemVerilogDpiCompositeLayoutResult layout_systemverilog_dpi_composite(
    const SystemVerilogDpiTypeDescriptor& descriptor,
    const std::size_t maximum_elements,
    const std::size_t maximum_bits,
    const std::size_t maximum_depth) {
  SystemVerilogDpiCompositeLayout layout;
  const auto error = measure(
      descriptor,
      {maximum_elements, maximum_bits, maximum_depth},
      1,
      layout);
  return {layout, error};
}

SystemVerilogDpiCompositeMarshalResult marshal_systemverilog_dpi_composite(
    const SystemVerilogDpiTypeDescriptor& descriptor,
    const std::vector<PackedLogic4>& values,
    const SystemVerilogDpiTransferMode mode) {
  const auto layout = layout_systemverilog_dpi_composite(descriptor);
  if (!layout) return {{}, layout.error};
  if (values.size() != layout.value.leaf_count) {
    return {{}, SystemVerilogDpiMarshallingError::ElementCount};
  }
  std::vector<const SystemVerilogDpiTypeDescriptor*> leaf_descriptors;
  leaf_descriptors.reserve(layout.value.leaf_count);
  flatten_descriptors(descriptor, leaf_descriptors);
  SystemVerilogDpiCompositePayload payload;
  payload.mode = mode;
  payload.descriptor = descriptor;
  payload.leaves.reserve(values.size());
  for (std::size_t index{}; index < values.size(); ++index) {
    const auto& leaf = *leaf_descriptors[index];
    if (values[index].width() != leaf.width) {
      return {{}, SystemVerilogDpiMarshallingError::WidthMismatch};
    }
    if (leaf.kind == SystemVerilogDpiCompositeKind::Enum) {
      const auto number = enum_value(values[index]);
      if (!number
          || std::ranges::find(leaf.enum_values, *number)
              == leaf.enum_values.end()) {
        return {{}, SystemVerilogDpiMarshallingError::EnumValue};
      }
    }
    const auto scalar = marshal_systemverilog_dpi_scalar(
        values[index], leaf.scalar_domain);
    if (!scalar) return {{}, scalar.error};
    payload.leaves.push_back(std::move(scalar.value));
  }
  return {std::move(payload), {}};
}

SystemVerilogDpiCompositeUnmarshalResult
unmarshal_systemverilog_dpi_composite(
    const SystemVerilogDpiCompositePayload& value,
    const SystemVerilogDpiTypeDescriptor& expected_descriptor,
    const SystemVerilogDpiTransferMode expected_mode) {
  if (value.mode != expected_mode
      || expected_mode == SystemVerilogDpiTransferMode::Input) {
    return {{}, SystemVerilogDpiMarshallingError::DirectionMismatch};
  }
  if (value.descriptor != expected_descriptor) {
    return {{}, SystemVerilogDpiMarshallingError::InvalidDescriptor};
  }
  const auto layout = layout_systemverilog_dpi_composite(expected_descriptor);
  if (!layout) return {{}, layout.error};
  if (value.leaves.size() != layout.value.leaf_count) {
    return {{}, SystemVerilogDpiMarshallingError::ElementCount};
  }
  std::vector<const SystemVerilogDpiTypeDescriptor*> leaf_descriptors;
  flatten_descriptors(expected_descriptor, leaf_descriptors);
  SystemVerilogDpiCompositeUnmarshalResult result;
  result.value.reserve(value.leaves.size());
  for (std::size_t index{}; index < value.leaves.size(); ++index) {
    const auto decoded = unmarshal_systemverilog_dpi_scalar(
        value.leaves[index], leaf_descriptors[index]->width);
    if (!decoded) return {{}, decoded.error};
    if (leaf_descriptors[index]->kind
        == SystemVerilogDpiCompositeKind::Enum) {
      const auto number = enum_value(decoded.value);
      if (!number
          || std::ranges::find(
                 leaf_descriptors[index]->enum_values, *number)
              == leaf_descriptors[index]->enum_values.end()) {
        return {{}, SystemVerilogDpiMarshallingError::EnumValue};
      }
    }
    result.value.push_back(std::move(decoded.value));
  }
  return result;
}

}  // namespace fsim::runtime

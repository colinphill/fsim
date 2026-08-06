// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/file_binary.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] std::pair<PackedLogic4, std::uint32_t> read_word(
    const std::uint32_t width,
    const std::function<std::int32_t()>& read) {
  const auto byte_count = (width + 7U) / 8U;
  if (byte_count > maximum_memory_file_bytes) {
    throw std::length_error{"$fread target exceeds the bounded file byte limit"};
  }
  PackedLogic4 value{width, Logic4::zero};
  std::uint32_t consumed{};
  for (; consumed < byte_count; ++consumed) {
    const auto character = read();
    if (character < 0) break;
    const auto byte = static_cast<std::uint8_t>(character);
    const auto byte_offset =
        static_cast<std::size_t>(byte_count - consumed - 1U) * 8U;
    for (std::size_t bit = 0; bit < 8U; ++bit) {
      const auto destination = byte_offset + bit;
      if (destination < width && ((byte >> bit) & 1U) != 0) {
        value.set(destination, Logic4::one);
      }
    }
  }
  if (consumed == 0) return {PackedLogic4{width, Logic4::zero}, 0};
  return {value, consumed};
}

[[nodiscard]] std::size_t fixed_offset(
    const ContainerType& type, const std::int32_t index) {
  const auto low = std::min(type.index_left, type.index_right);
  const auto high = std::max(type.index_left, type.index_right);
  if (index < low || index > high) {
    throw std::out_of_range{"$fread start index is outside the target memory"};
  }
  return static_cast<std::size_t>(
      type.index_left >= type.index_right
          ? static_cast<std::int64_t>(type.index_left) - index
          : static_cast<std::int64_t>(index) - type.index_left);
}

void require_safe_chandle(
    const FileBinaryRead& operation, const PackedLogic4& value) {
  if (operation.scalar_kind != SystemVerilogScalarKind::Chandle) return;
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    if (value.get(bit) != Logic4::zero) {
        throw std::invalid_argument{
            "$fread chandle input accepts only the null handle"};
    }
  }
}

}  // namespace

FileBinaryReadResult read_binary_file(
    const FileBinaryRead& operation,
    std::optional<ContainerValue> container,
    const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> count,
    const std::function<std::int32_t()>& read) {
  FileBinaryReadResult result;
  const bool container_target = operation.target_kind
          == FileBinaryTargetKind::container_register
      || operation.target_kind == FileBinaryTargetKind::container_object;
  if (!container_target) {
    if (start || count) {
      throw std::invalid_argument{
          "$fread start/count arguments require a fixed unpacked memory"};
    }
    const auto [value, consumed] = read_word(operation.width, read);
    require_safe_chandle(operation, value);
    result.bytes = consumed;
    result.packed = std::move(value);
    return result;
  }
  if (!container || !container->type.fixed || container->type.associative
      || container_packed_element_width(container->type)
          != operation.width) {
    throw std::invalid_argument{"$fread target memory metadata is invalid"};
  }
  if (count && *count < 0) {
    throw std::invalid_argument{"$fread count cannot be negative"};
  }
  const bool linear = container->type.dimensions.size() > 1U;
  const auto direction = linear ? 1
      : container->type.index_left >= container->type.index_right ? -1 : 1;
  auto index = start.value_or(
      linear ? 0 : container->type.index_left);
  auto offset = linear
      ? index < 0
          ? throw std::out_of_range{
                "$fread start is outside the target memory"}
          : static_cast<std::size_t>(index)
      : fixed_offset(container->type, index);
  const auto element_count = [&]() {
    switch (container->type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
      return container->elements.size();
    case ContainerElementKind::String:
      return container->string_elements.size();
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
      return container->nested_elements.size();
    }
    return std::size_t{};
  }();
  if (offset >= element_count) {
    throw std::out_of_range{
        "$fread start is outside the target memory"};
  }
  const auto maximum = count
      ? static_cast<std::size_t>(*count)
      : element_count - offset;
  for (std::size_t element = 0; element < maximum; ++element) {
    if (offset >= element_count) break;
    const auto [value, consumed] = read_word(operation.width, read);
    if (consumed == 0) break;
    require_safe_chandle(operation, value);
    if (result.bytes > maximum_memory_file_bytes - consumed) {
      throw std::length_error{"$fread exceeds the bounded file byte limit"};
    }
    result.bytes += consumed;
    switch (container->type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
      container->elements[offset] = std::move(value);
      break;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
      unpack_container_signal_value(
          container->nested_elements[offset], value);
      break;
    case ContainerElementKind::String:
      throw std::invalid_argument{
          "$fread does not accept variable-width string elements"};
    }
    if (!linear && index == container->type.index_right) break;
    index = static_cast<std::int32_t>(index + direction);
    offset = linear
        ? offset + 1U
        : fixed_offset(container->type, index);
  }
  validate_container_value(*container);
  result.container = std::move(container);
  return result;
}

}  // namespace fsim::runtime::simir

// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/file_binary.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] std::pair<std::uint64_t, std::uint32_t> read_word(
    const std::uint32_t width,
    const std::function<std::int32_t()>& read) {
  const auto byte_count = (width + 7U) / 8U;
  std::uint64_t value{};
  std::uint32_t consumed{};
  for (; consumed < byte_count; ++consumed) {
    const auto character = read();
    if (character < 0) break;
    value = (value << 8U) | static_cast<std::uint8_t>(character);
  }
  if (consumed == 0) return {0, 0};
  value <<= static_cast<unsigned>((byte_count - consumed) * 8U);
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
    result.bytes = consumed;
    result.packed = PackedLogic4::from_aval_bval(operation.width, value, 0);
    return result;
  }
  if (!container || !container->type.fixed || container->type.associative
      || container->type.dimensions.size() != 1U
      || container->type.element_width != operation.width) {
    throw std::invalid_argument{"$fread target memory metadata is invalid"};
  }
  if (count && *count < 0) {
    throw std::invalid_argument{"$fread count cannot be negative"};
  }
  const auto direction = container->type.index_left >= container->type.index_right
      ? -1 : 1;
  auto index = start.value_or(container->type.index_left);
  auto offset = fixed_offset(container->type, index);
  const auto maximum = count
      ? static_cast<std::size_t>(*count)
      : container->elements.size() - offset;
  for (std::size_t element = 0; element < maximum; ++element) {
    if (offset >= container->elements.size()) break;
    const auto [value, consumed] = read_word(operation.width, read);
    if (consumed == 0) break;
    if (result.bytes > maximum_memory_file_bytes - consumed) {
      throw std::length_error{"$fread exceeds the bounded file byte limit"};
    }
    result.bytes += consumed;
    container->elements[offset] =
        PackedLogic4::from_aval_bval(operation.width, value, 0);
    if (index == container->type.index_right) break;
    index = static_cast<std::int32_t>(index + direction);
    offset = fixed_offset(container->type, index);
  }
  result.container = std::move(container);
  return result;
}

}  // namespace fsim::runtime::simir

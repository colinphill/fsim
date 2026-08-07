// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_marshalling.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] std::uint64_t ordered(const std::int64_t value) noexcept {
  return std::bit_cast<std::uint64_t>(value) ^ (std::uint64_t{1} << 63U);
}

}  // namespace

std::int64_t SystemVerilogDpiArrayRange::low() const noexcept {
  return left < right ? left : right;
}

std::int64_t SystemVerilogDpiArrayRange::high() const noexcept {
  return left > right ? left : right;
}

int SystemVerilogDpiArrayRange::increment() const noexcept {
  return left <= right ? 1 : -1;
}

std::optional<std::size_t> SystemVerilogDpiArrayRange::size() const noexcept {
  const auto first = ordered(low());
  const auto last = ordered(high());
  const auto distance = last - first;
  if (distance == std::numeric_limits<std::uint64_t>::max()
      || distance + 1U > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(distance + 1U);
}

const SystemVerilogDpiOpenArrayRegistry::Slot*
SystemVerilogDpiOpenArrayRegistry::find(
    const SystemVerilogDpiOpenArrayHandle handle) const noexcept {
  if (handle.slot >= slots_.size()) return nullptr;
  const auto& slot = slots_[handle.slot];
  return slot.live && slot.epoch == handle.epoch ? &slot : nullptr;
}

SystemVerilogDpiOpenArrayRegistry::Slot*
SystemVerilogDpiOpenArrayRegistry::find(
    const SystemVerilogDpiOpenArrayHandle handle) noexcept {
  return const_cast<Slot*>(
      static_cast<const SystemVerilogDpiOpenArrayRegistry*>(this)->find(
          handle));
}

SystemVerilogDpiOpenArrayCreateResult
SystemVerilogDpiOpenArrayRegistry::create(
    std::vector<SystemVerilogDpiArrayRange> ranges,
    SystemVerilogDpiTypeDescriptor element_descriptor,
    std::vector<PackedLogic4> flattened_values,
    const bool contiguous,
    const SystemVerilogDpiTransferMode mode) {
  if (ranges.empty()) {
    return {{}, SystemVerilogDpiMarshallingError::InvalidDimension};
  }
  const auto element_layout =
      layout_systemverilog_dpi_composite(element_descriptor);
  if (!element_layout) return {{}, element_layout.error};
  std::size_t elements{1};
  for (const auto& dimension : ranges) {
    const auto count = dimension.size();
    if (!count || (*count != 0
        && elements > maximum_dpi_composite_elements / *count)) {
      return {{}, SystemVerilogDpiMarshallingError::ElementCount};
    }
    elements *= *count;
  }
  if (elements > maximum_dpi_composite_elements
      || (element_layout.value.leaf_count != 0
          && elements > maximum_dpi_composite_elements
              / element_layout.value.leaf_count)
      || flattened_values.size()
          != elements * element_layout.value.leaf_count) {
    return {{}, SystemVerilogDpiMarshallingError::ElementCount};
  }
  if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) {
    return {{}, SystemVerilogDpiMarshallingError::ElementCount};
  }
  Slot slot;
  slot.live = true;
  slot.contiguous = contiguous;
  slot.mode = mode;
  slot.ranges = std::move(ranges);
  slot.element_descriptor = std::move(element_descriptor);
  slot.leaves_per_element = element_layout.value.leaf_count;
  slot.values = std::move(flattened_values);
  slots_.push_back(std::move(slot));
  return {{static_cast<std::uint32_t>(slots_.size() - 1U), 1}, {}};
}

bool SystemVerilogDpiOpenArrayRegistry::release(
    const SystemVerilogDpiOpenArrayHandle handle) noexcept {
  auto* slot = find(handle);
  if (!slot) return false;
  slot->live = false;
  ++slot->epoch;
  slot->values.clear();
  return true;
}

bool SystemVerilogDpiOpenArrayRegistry::contains(
    const SystemVerilogDpiOpenArrayHandle handle) const noexcept {
  return find(handle) != nullptr;
}

std::optional<SystemVerilogDpiArrayRange>
SystemVerilogDpiOpenArrayRegistry::range(
    const SystemVerilogDpiOpenArrayHandle handle,
    const std::size_t dimension) const noexcept {
  const auto* slot = find(handle);
  if (!slot || dimension == 0 || dimension > slot->ranges.size()) {
    return std::nullopt;
  }
  return slot->ranges[dimension - 1U];
}

std::optional<std::size_t> SystemVerilogDpiOpenArrayRegistry::dimensions(
    const SystemVerilogDpiOpenArrayHandle handle) const noexcept {
  const auto* slot = find(handle);
  return slot ? std::optional<std::size_t>{slot->ranges.size()}
              : std::nullopt;
}

std::optional<std::size_t>
SystemVerilogDpiOpenArrayRegistry::linear_index(
    const Slot& slot,
    const std::vector<std::int64_t>& indices) noexcept {
  if (indices.size() != slot.ranges.size()) return std::nullopt;
  std::size_t result{};
  for (std::size_t dimension{}; dimension < indices.size(); ++dimension) {
    const auto& range = slot.ranges[dimension];
    if (indices[dimension] < range.low()
        || indices[dimension] > range.high()) return std::nullopt;
    const auto count = *range.size();
    const auto offset = range.increment() > 0
        ? ordered(indices[dimension]) - ordered(range.left)
        : ordered(range.left) - ordered(indices[dimension]);
    result = result * count + static_cast<std::size_t>(offset);
  }
  return result;
}

SystemVerilogDpiOpenArrayElementResult
SystemVerilogDpiOpenArrayRegistry::element(
    const SystemVerilogDpiOpenArrayHandle handle,
    const std::vector<std::int64_t>& indices) const {
  const auto* slot = find(handle);
  if (!slot) return {{}, SystemVerilogDpiMarshallingError::StaleArray};
  const auto index = linear_index(*slot, indices);
  if (!index) return {{}, SystemVerilogDpiMarshallingError::IndexRange};
  const auto first = *index * slot->leaves_per_element;
  std::vector<PackedLogic4> values{
      slot->values.begin() + static_cast<std::ptrdiff_t>(first),
      slot->values.begin() + static_cast<std::ptrdiff_t>(
          first + slot->leaves_per_element)};
  return {std::move(values), {}};
}

SystemVerilogDpiMarshallingError
SystemVerilogDpiOpenArrayRegistry::store_element(
    const SystemVerilogDpiOpenArrayHandle handle,
    const std::vector<std::int64_t>& indices,
    const std::vector<PackedLogic4>& value) {
  auto* slot = find(handle);
  if (!slot) return SystemVerilogDpiMarshallingError::StaleArray;
  if (slot->mode == SystemVerilogDpiTransferMode::Input) {
    return SystemVerilogDpiMarshallingError::DirectionMismatch;
  }
  const auto index = linear_index(*slot, indices);
  if (!index) return SystemVerilogDpiMarshallingError::IndexRange;
  if (value.size() != slot->leaves_per_element) {
    return SystemVerilogDpiMarshallingError::ElementCount;
  }
  const auto first = *index * slot->leaves_per_element;
  std::copy(value.begin(), value.end(), slot->values.begin()
      + static_cast<std::ptrdiff_t>(first));
  return {};
}

SystemVerilogDpiOpenArrayElementResult
SystemVerilogDpiOpenArrayRegistry::contiguous_values(
    const SystemVerilogDpiOpenArrayHandle handle) const {
  const auto* slot = find(handle);
  if (!slot) return {{}, SystemVerilogDpiMarshallingError::StaleArray};
  if (!slot->contiguous) {
    return {{}, SystemVerilogDpiMarshallingError::Noncontiguous};
  }
  return {slot->values, {}};
}

SystemVerilogDpiOpenArrayPointerResult
SystemVerilogDpiOpenArrayRegistry::array_pointer(
    const SystemVerilogDpiOpenArrayHandle handle) const noexcept {
  const auto* slot = find(handle);
  if (!slot) return {{}, 0, SystemVerilogDpiMarshallingError::StaleArray};
  if (!slot->contiguous) {
    return {{}, 0, SystemVerilogDpiMarshallingError::Noncontiguous};
  }
  return {slot->values.data(), slot->values.size(), {}};
}

SystemVerilogDpiOpenArrayMutablePointerResult
SystemVerilogDpiOpenArrayRegistry::mutable_array_pointer(
    const SystemVerilogDpiOpenArrayHandle handle) noexcept {
  auto* slot = find(handle);
  if (!slot) return {{}, 0, SystemVerilogDpiMarshallingError::StaleArray};
  if (!slot->contiguous) {
    return {{}, 0, SystemVerilogDpiMarshallingError::Noncontiguous};
  }
  if (slot->mode == SystemVerilogDpiTransferMode::Input) {
    return {{}, 0, SystemVerilogDpiMarshallingError::DirectionMismatch};
  }
  return {slot->values.data(), slot->values.size(), {}};
}

SystemVerilogDpiOpenArrayPointerResult
SystemVerilogDpiOpenArrayRegistry::element_pointer(
    const SystemVerilogDpiOpenArrayHandle handle,
    const std::vector<std::int64_t>& indices) const noexcept {
  const auto* slot = find(handle);
  if (!slot) return {{}, 0, SystemVerilogDpiMarshallingError::StaleArray};
  const auto index = linear_index(*slot, indices);
  if (!index) return {{}, 0, SystemVerilogDpiMarshallingError::IndexRange};
  return {slot->values.data() + *index * slot->leaves_per_element,
      slot->leaves_per_element, {}};
}

SystemVerilogDpiOpenArrayMutablePointerResult
SystemVerilogDpiOpenArrayRegistry::mutable_element_pointer(
    const SystemVerilogDpiOpenArrayHandle handle,
    const std::vector<std::int64_t>& indices) noexcept {
  auto* slot = find(handle);
  if (!slot) return {{}, 0, SystemVerilogDpiMarshallingError::StaleArray};
  if (slot->mode == SystemVerilogDpiTransferMode::Input) {
    return {{}, 0, SystemVerilogDpiMarshallingError::DirectionMismatch};
  }
  const auto index = linear_index(*slot, indices);
  if (!index) return {{}, 0, SystemVerilogDpiMarshallingError::IndexRange};
  return {slot->values.data() + *index * slot->leaves_per_element,
      slot->leaves_per_element, {}};
}

}  // namespace fsim::runtime

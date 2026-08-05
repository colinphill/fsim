// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_heap.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] std::size_t checked_add(
    const std::size_t left,
    const std::size_t right,
    const std::string_view description) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw std::length_error{
        std::string{description} + " exceeds addressable storage"};
  }
  return left + right;
}

[[nodiscard]] std::size_t packed_bytes(
    const std::size_t width,
    const std::size_t planes) {
  if (width > std::numeric_limits<std::size_t>::max() - 7U) {
    throw std::length_error{"class property width exceeds addressable storage"};
  }
  const auto bytes = (width + 7U) / 8U;
  if (planes != 0
      && bytes > std::numeric_limits<std::size_t>::max() / planes) {
    throw std::length_error{"class property storage size overflows"};
  }
  return bytes * planes;
}

[[nodiscard]] std::size_t property_bytes(
    const SystemVerilogClassPropertyDescriptor& descriptor) {
  switch (descriptor.kind) {
    case SystemVerilogClassPropertyKind::Bit2:
      return packed_bytes(descriptor.width, 1U);
    case SystemVerilogClassPropertyKind::Logic4:
      return packed_bytes(descriptor.width, 2U);
    case SystemVerilogClassPropertyKind::Logic9:
      return packed_bytes(descriptor.width, 4U);
    case SystemVerilogClassPropertyKind::Integer:
      return sizeof(std::int64_t);
    case SystemVerilogClassPropertyKind::ClassHandle:
      return sizeof(SystemVerilogClassHandle);
    case SystemVerilogClassPropertyKind::Container:
      if (descriptor.handle_container) {
        if (descriptor.handle_container->maximum_elements
            > std::numeric_limits<std::size_t>::max()
                / sizeof(SystemVerilogClassHandle)) {
          throw std::length_error{
              "class handle container storage size overflows"};
        }
        return descriptor.handle_container->maximum_elements
            * sizeof(SystemVerilogClassHandle);
      }
      return 0U;
    case SystemVerilogClassPropertyKind::String:
      return 0U;
  }
  return 0U;
}

[[nodiscard]] SystemVerilogClassObject make_object(
    const SystemVerilogClassDescriptor& descriptor) {
  if (descriptor.declared_type.empty()
      || descriptor.dynamic_type.empty()
      || descriptor.specialization_identity.empty()) {
    throw std::invalid_argument{
        "class allocation requires declared, dynamic, and specialization identities"};
  }
  SystemVerilogClassObject object;
  object.declared_type = descriptor.declared_type;
  object.dynamic_type = descriptor.dynamic_type;
  object.specialization_identity = descriptor.specialization_identity;
  object.assignable_declared_types = descriptor.assignable_declared_types;
  if (object.assignable_declared_types.empty()) {
    object.assignable_declared_types.push_back(object.dynamic_type);
  }
  if (std::ranges::find(
          object.assignable_declared_types,
          object.dynamic_type)
      == object.assignable_declared_types.end()) {
    throw std::invalid_argument{
        "class descriptor assignable types omit the dynamic type"};
  }
  if (std::ranges::find(
          object.assignable_declared_types,
          object.declared_type)
      == object.assignable_declared_types.end()) {
    throw std::invalid_argument{
        "class dynamic type is not assignable to its declared handle type"};
  }
  object.property_names.reserve(descriptor.properties.size());
  object.properties.reserve(descriptor.properties.size());
  std::set<std::string> property_names;
  for (const auto& descriptor_property : descriptor.properties) {
    if (descriptor_property.name.empty()) {
      throw std::invalid_argument{"class property name must not be empty"};
    }
    if (!property_names.insert(descriptor_property.name).second) {
      throw std::invalid_argument{
          "duplicate class property descriptor '"
          + descriptor_property.name + "'"};
    }
    object.accounted_bytes = checked_add(
        object.accounted_bytes,
        property_bytes(descriptor_property),
        "class object");
    SystemVerilogClassPropertyValue property;
    property.kind = descriptor_property.kind;
    const auto width = descriptor_property.kind
            == SystemVerilogClassPropertyKind::Integer
        ? std::size_t{64}
        : descriptor_property.width;
    if (descriptor_property.kind == SystemVerilogClassPropertyKind::Bit2) {
      property.packed = PackedLogic4(width, Logic4::zero);
    } else if (
        descriptor_property.kind == SystemVerilogClassPropertyKind::Logic4) {
      property.packed = PackedLogic4(width, Logic4::x);
    } else if (
        descriptor_property.kind == SystemVerilogClassPropertyKind::Logic9) {
      property.packed = PackedLogic4(width, Logic4::x).promoted_to_logic9();
    } else if (
        descriptor_property.kind == SystemVerilogClassPropertyKind::Integer) {
      property.packed = PackedLogic4(width, Logic4::zero);
    } else if (
        descriptor_property.kind == SystemVerilogClassPropertyKind::Container
        && descriptor_property.handle_container) {
      property.handle_container.emplace(
          *descriptor_property.handle_container);
    }
    if (descriptor_property.initial_packed) {
      if (property.packed.width()
          != descriptor_property.initial_packed->width()) {
        throw std::invalid_argument{
            "class property initializer width does not match its type"};
      }
      property.packed = *descriptor_property.initial_packed;
    }
    if (descriptor_property.initial_string) {
      if (descriptor_property.kind
          != SystemVerilogClassPropertyKind::String) {
        throw std::invalid_argument{
            "class string initializer requires a string property"};
      }
      property.string = *descriptor_property.initial_string;
    }
    object.property_names.push_back(descriptor_property.name);
    object.properties.push_back(std::move(property));
  }
  return object;
}

}  // namespace

SystemVerilogClassHeap::SystemVerilogClassHeap(
    const SystemVerilogClassHeapLimits limits)
    : limits_(limits) {}

SystemVerilogClassHandle SystemVerilogClassHeap::encode(
    const std::uint32_t slot,
    const std::uint32_t generation) noexcept {
  return (static_cast<std::uint64_t>(generation) << 32U)
      | (static_cast<std::uint64_t>(slot) + 1U);
}

std::uint32_t SystemVerilogClassHeap::slot_of(
    const SystemVerilogClassHandle handle) noexcept {
  const auto encoded = static_cast<std::uint32_t>(handle);
  return encoded == 0U
      ? std::numeric_limits<std::uint32_t>::max()
      : encoded - 1U;
}

std::uint32_t SystemVerilogClassHeap::generation_of(
    const SystemVerilogClassHandle handle) noexcept {
  return static_cast<std::uint32_t>(handle >> 32U);
}

const SystemVerilogClassHeap::Slot* SystemVerilogClassHeap::find(
    const SystemVerilogClassHandle handle) const noexcept {
  if (handle == 0) return nullptr;
  const auto slot = slot_of(handle);
  if (slot >= slots_.size()) return nullptr;
  const auto& candidate = slots_[slot];
  return candidate.occupied
          && candidate.generation == generation_of(handle)
      ? &candidate
      : nullptr;
}

SystemVerilogClassHeap::Slot* SystemVerilogClassHeap::find(
    const SystemVerilogClassHandle handle) noexcept {
  return const_cast<Slot*>(
      std::as_const(*this).find(handle));
}

SystemVerilogClassHandle SystemVerilogClassHeap::allocate(
    const SystemVerilogClassDescriptor& descriptor) {
  if (live_objects_ >= limits_.maximum_live_objects) {
    throw std::length_error{"SystemVerilog class live-object budget exceeded"};
  }
  auto object_value = make_object(descriptor);
  if (object_value.accounted_bytes
      > limits_.maximum_storage_bytes - storage_bytes_) {
    throw std::length_error{"SystemVerilog class storage budget exceeded"};
  }

  std::uint32_t slot_index{};
  if (!free_slots_.empty()) {
    const auto free = free_slots_.begin();
    slot_index = *free;
    free_slots_.erase(free);
    auto& slot = slots_[slot_index];
    if (slot.generation == std::numeric_limits<std::uint32_t>::max()) {
      throw std::overflow_error{"SystemVerilog class handle generation exhausted"};
    }
    ++slot.generation;
    slot.object = std::move(object_value);
    slot.occupied = true;
  } else {
    if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error{"SystemVerilog class handle slots exhausted"};
    }
    slot_index = static_cast<std::uint32_t>(slots_.size());
    Slot slot;
    slot.occupied = true;
    slot.object = std::move(object_value);
    slots_.push_back(std::move(slot));
  }
  ++live_objects_;
  storage_bytes_ += slots_[slot_index].object.accounted_bytes;
  return encode(slot_index, slots_[slot_index].generation);
}

SystemVerilogClassHandle SystemVerilogClassHeap::construct(
    const SystemVerilogClassDescriptor& descriptor,
    const std::span<const ConstructorStep> constructor_steps) {
  const auto handle = allocate(descriptor);
  try {
    for (const auto& step : constructor_steps) {
      if (!step) {
        throw std::invalid_argument{"class constructor step is empty"};
      }
      step(*this, handle);
    }
  } catch (...) {
    (void)release(handle);
    throw;
  }
  return handle;
}

bool SystemVerilogClassHeap::release(
    const SystemVerilogClassHandle handle) noexcept {
  auto* slot = find(handle);
  if (slot == nullptr) return false;
  storage_bytes_ -= slot->object.accounted_bytes;
  slot->object = {};
  slot->occupied = false;
  --live_objects_;
  if (slot->generation != std::numeric_limits<std::uint32_t>::max()) {
    free_slots_.insert(slot_of(handle));
  }
  return true;
}

void SystemVerilogClassHeap::clear() noexcept {
  for (std::uint32_t index = 0; index < slots_.size(); ++index) {
    auto& slot = slots_[index];
    if (!slot.occupied) continue;
    slot.object = {};
    slot.occupied = false;
    if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
      free_slots_.insert(index);
    }
  }
  live_objects_ = 0;
  storage_bytes_ = 0;
}

bool SystemVerilogClassHeap::contains(
    const SystemVerilogClassHandle handle) const noexcept {
  return find(handle) != nullptr;
}

SystemVerilogClassObject& SystemVerilogClassHeap::object(
    const SystemVerilogClassHandle handle) {
  auto* slot = find(handle);
  if (slot == nullptr) {
    throw std::out_of_range{"null or stale SystemVerilog class handle"};
  }
  return slot->object;
}

const SystemVerilogClassObject& SystemVerilogClassHeap::object(
    const SystemVerilogClassHandle handle) const {
  const auto* slot = find(handle);
  if (slot == nullptr) {
    throw std::out_of_range{"null or stale SystemVerilog class handle"};
  }
  return slot->object;
}

SystemVerilogClassPropertyValue& SystemVerilogClassHeap::property(
    const SystemVerilogClassHandle handle,
    const std::string_view name) {
  auto& value = object(handle);
  auto found = std::ranges::find(value.property_names, name);
  if (found == value.property_names.end()) {
    const auto suffix = "::" + std::string{name};
    const auto reverse = std::find_if(
        value.property_names.rbegin(), value.property_names.rend(),
        [&](const auto& candidate) { return candidate.ends_with(suffix); });
    if (reverse != value.property_names.rend()) {
      found = std::prev(reverse.base());
    }
  }
  if (found == value.property_names.end()) {
    throw std::out_of_range{
        "SystemVerilog class property '" + std::string{name}
        + "' does not exist"};
  }
  return value.properties[static_cast<std::size_t>(
      std::distance(value.property_names.begin(), found))];
}

const SystemVerilogClassPropertyValue& SystemVerilogClassHeap::property(
    const SystemVerilogClassHandle handle,
    const std::string_view name) const {
  const auto& value = object(handle);
  auto found = std::ranges::find(value.property_names, name);
  if (found == value.property_names.end()) {
    const auto suffix = "::" + std::string{name};
    const auto reverse = std::find_if(
        value.property_names.rbegin(), value.property_names.rend(),
        [&](const auto& candidate) { return candidate.ends_with(suffix); });
    if (reverse != value.property_names.rend()) {
      found = std::prev(reverse.base());
    }
  }
  if (found == value.property_names.end()) {
    throw std::out_of_range{
        "SystemVerilog class property '" + std::string{name}
        + "' does not exist"};
  }
  return value.properties[static_cast<std::size_t>(
      std::distance(value.property_names.begin(), found))];
}

SystemVerilogClassHandle SystemVerilogClassHeap::checked_cast(
    const SystemVerilogClassHandle handle,
    const std::string_view declared_type) const {
  if (handle == 0) return 0;
  const auto& value = object(handle);
  if (std::ranges::find(
          value.assignable_declared_types, declared_type)
      == value.assignable_declared_types.end()) {
    throw std::invalid_argument{
        "class object of dynamic type '" + value.dynamic_type
        + "' cannot be viewed as '" + std::string{declared_type} + "'"};
  }
  return handle;
}

std::vector<SystemVerilogClassHandle>
SystemVerilogClassHeap::live_handles() const {
  std::vector<SystemVerilogClassHandle> result;
  result.reserve(live_objects_);
  for (std::uint32_t index = 0; index < slots_.size(); ++index) {
    const auto& slot = slots_[index];
    if (slot.occupied) result.push_back(encode(index, slot.generation));
  }
  return result;
}

}  // namespace fsim::runtime

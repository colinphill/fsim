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

[[nodiscard]] std::uint64_t mixed(const std::uint64_t input) noexcept {
  auto value = input;
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t identity_hash(
    const std::string_view identity) noexcept {
  auto value = UINT64_C(14695981039346656037);
  for (const auto character : identity) {
    value ^= static_cast<std::uint8_t>(character);
    value *= UINT64_C(1099511628211);
  }
  return value;
}

[[nodiscard]] std::uint64_t derived_seed(
    const std::uint64_t parent,
    const std::string_view identity,
    const std::uint64_t ordinal) noexcept {
  return mixed(
      parent ^ identity_hash(identity)
      ^ mixed(ordinal + UINT64_C(0x9e3779b97f4a7c15)));
}

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
  std::size_t payload{};
  switch (descriptor.kind) {
    case SystemVerilogClassPropertyKind::Bit2:
      payload = packed_bytes(descriptor.width, 1U);
      break;
    case SystemVerilogClassPropertyKind::Logic4:
      payload = packed_bytes(descriptor.width, 2U);
      break;
    case SystemVerilogClassPropertyKind::Logic9:
      payload = packed_bytes(descriptor.width, 4U);
      break;
    case SystemVerilogClassPropertyKind::Integer:
      payload = sizeof(std::int64_t);
      break;
    case SystemVerilogClassPropertyKind::ClassHandle:
      payload = sizeof(SystemVerilogClassHandle);
      break;
    case SystemVerilogClassPropertyKind::Container:
      if (descriptor.handle_container) {
        const auto elements =
            descriptor.handle_container->reserve_maximum_storage
                ? descriptor.handle_container->maximum_elements
                : descriptor.handle_container->initial_elements;
        if (elements
            > std::numeric_limits<std::size_t>::max()
                / sizeof(SystemVerilogClassHandle)) {
          throw std::length_error{
              "class handle container storage size overflows"};
        }
        payload = elements * sizeof(SystemVerilogClassHandle);
      }
      break;
    case SystemVerilogClassPropertyKind::String:
      break;
  }
  if (descriptor.random_kind != SystemVerilogClassRandomKind::None) {
    constexpr auto fixed_random_state_bytes = sizeof(std::uint8_t)
        + 2U * sizeof(std::uint64_t) + sizeof(std::size_t) + 2U;
    payload = checked_add(
        payload, fixed_random_state_bytes, "class random state");
    payload = checked_add(
        payload, descriptor.nominal_type.size(), "class random state");
  }
  return payload;
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
    if (descriptor_property.random_kind
        != SystemVerilogClassRandomKind::None) {
      if (descriptor_property.nominal_type.empty()) {
        throw std::invalid_argument{
            "random class property requires an exact nominal type"};
      }
      if (descriptor_property.random_kind
              == SystemVerilogClassRandomKind::Randc
          && (descriptor_property.kind
                  == SystemVerilogClassPropertyKind::String
              || descriptor_property.kind
                  == SystemVerilogClassPropertyKind::ClassHandle
              || descriptor_property.kind
                  == SystemVerilogClassPropertyKind::Container)) {
        throw std::invalid_argument{
            "randc class property requires an integral or enum profile"};
      }
      property.random_state = SystemVerilogClassRandomState{
          descriptor_property.random_kind,
          width,
          descriptor_property.signed_value,
          descriptor_property.nominal_type,
          true,
          0,
          0,
          0,
          0,
          {}};
    }
    object.property_names.push_back(descriptor_property.name);
    object.properties.push_back(std::move(property));
  }
  for (const auto& [identity, enabled] : descriptor.constraint_modes) {
    if (identity.empty()
        || !object.constraint_modes.emplace(identity, enabled).second) {
      throw std::invalid_argument{
          "class constraint mode requires a unique canonical identity"};
    }
    object.accounted_bytes = checked_add(
        object.accounted_bytes,
        identity.size() + sizeof(bool),
        "class constraint mode");
  }
  return object;
}

}  // namespace

SystemVerilogClassHeap::SystemVerilogClassHeap(
    const SystemVerilogClassHeapLimits limits,
    const std::uint64_t simulation_seed)
    : limits_(limits), simulation_seed_(simulation_seed) {}

std::uint64_t SystemVerilogClassRandomStream::next_u64() noexcept {
  state += UINT64_C(0x9e3779b97f4a7c15);
  return mixed(state);
}

std::uint32_t SystemVerilogClassRandomStream::next_u32() noexcept {
  return static_cast<std::uint32_t>(next_u64() >> 32U);
}

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
  if (descriptor.random_root_identity.empty()) {
    throw std::invalid_argument{
        "class random root identity must not be empty"};
  }
  const auto ordinal_entry = next_object_ordinals_.find(
      descriptor.random_root_identity);
  const auto object_ordinal = ordinal_entry == next_object_ordinals_.end()
      ? std::uint64_t{}
      : ordinal_entry->second;
  if (object_ordinal == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"class random object ordinal exhausted"};
  }
  object_value.random_root_identity = descriptor.random_root_identity;
  object_value.random_root_seed = derived_seed(
      simulation_seed_, descriptor.random_root_identity, 0);
  object_value.random_object_ordinal = object_ordinal;
  object_value.random_object_seed = derived_seed(
      object_value.random_root_seed,
      descriptor.specialization_identity,
      object_ordinal);
  for (std::size_t index = 0; index < object_value.properties.size(); ++index) {
    auto& state = object_value.properties[index].random_state;
    if (state) {
      state->stream_seed = derived_seed(
          object_value.random_object_seed,
          object_value.property_names[index],
          0);
    }
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
  next_object_ordinals_[descriptor.random_root_identity] =
      object_ordinal + 1U;
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
  next_object_ordinals_.clear();
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

SystemVerilogClassRandomState& SystemVerilogClassHeap::random_state(
    const SystemVerilogClassHandle handle,
    const std::string_view name) {
  auto& state = property(handle, name).random_state;
  if (!state) {
    throw std::invalid_argument{
        "SystemVerilog class property '" + std::string{name}
        + "' is not randomizable"};
  }
  return *state;
}

const SystemVerilogClassRandomState& SystemVerilogClassHeap::random_state(
    const SystemVerilogClassHandle handle,
    const std::string_view name) const {
  const auto& state = property(handle, name).random_state;
  if (!state) {
    throw std::invalid_argument{
        "SystemVerilog class property '" + std::string{name}
        + "' is not randomizable"};
  }
  return *state;
}

SystemVerilogClassRandomStream SystemVerilogClassHeap::random_stream(
    const SystemVerilogClassHandle handle,
    const std::string_view call_identity) {
  if (call_identity.empty()) {
    throw std::invalid_argument{
        "class random call identity must not be empty"};
  }
  auto& value = object(handle);
  auto [position, inserted] = value.random_call_ordinals.try_emplace(
      std::string{call_identity}, 0);
  (void)inserted;
  if (position->second == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"class random call ordinal exhausted"};
  }
  const auto call_ordinal = position->second++;
  const auto call_seed = derived_seed(
      value.random_object_seed, call_identity, call_ordinal);
  return {
      simulation_seed_,
      value.random_root_seed,
      value.random_object_seed,
      call_seed,
      call_seed,
      call_ordinal};
}

bool SystemVerilogClassHeap::random_mode(
    const SystemVerilogClassHandle handle,
    const std::string_view property_name) const {
  return random_state(handle, property_name).enabled;
}

void SystemVerilogClassHeap::set_random_mode(
    const SystemVerilogClassHandle handle,
    const std::string_view property_name,
    const bool enabled) {
  random_state(handle, property_name).enabled = enabled;
}

bool SystemVerilogClassHeap::constraint_mode(
    const SystemVerilogClassHandle handle,
    const std::string_view constraint) const {
  const auto& modes = object(handle).constraint_modes;
  auto found = modes.find(constraint);
  if (found == modes.end()) {
    const auto suffix = "::" + std::string{constraint};
    found = std::find_if(modes.begin(), modes.end(), [&](const auto& item) {
      return item.first.ends_with(suffix);
    });
  }
  if (found == modes.end()) {
    throw std::out_of_range{
        "SystemVerilog class constraint '" + std::string{constraint}
        + "' does not exist"};
  }
  return found->second;
}

void SystemVerilogClassHeap::set_constraint_mode(
    const SystemVerilogClassHandle handle,
    const std::string_view constraint,
    const bool enabled) {
  auto& modes = object(handle).constraint_modes;
  auto found = modes.find(constraint);
  if (found == modes.end()) {
    const auto suffix = "::" + std::string{constraint};
    found = std::find_if(modes.begin(), modes.end(), [&](const auto& item) {
      return item.first.ends_with(suffix);
    });
  }
  if (found == modes.end()) {
    throw std::out_of_range{
        "SystemVerilog class constraint '" + std::string{constraint}
        + "' does not exist"};
  }
  found->second = enabled;
}

void SystemVerilogClassHeap::reset_randc_cycle(
    const SystemVerilogClassHandle handle,
    const std::string_view property_name) {
  auto& value = object(handle);
  auto& state = random_state(handle, property_name);
  if (state.kind != SystemVerilogClassRandomKind::Randc) {
    throw std::invalid_argument{
        "SystemVerilog class property '" + std::string{property_name}
        + "' is not randc"};
  }
  const auto released =
      state.randc_used_values.size() * sizeof(std::uint64_t);
  value.accounted_bytes -= released;
  storage_bytes_ -= released;
  state.randc_domain_signature = 0;
  state.randc_cycle = 0;
  state.randc_used_values.clear();
}

void SystemVerilogClassHeap::reseed_random(
    const SystemVerilogClassHandle handle,
    const std::uint64_t seed) {
  auto& value = object(handle);
  value.random_object_seed = seed;
  value.random_call_ordinals.clear();
  std::size_t released{};
  for (std::size_t index = 0; index < value.properties.size(); ++index) {
    auto& state = value.properties[index].random_state;
    if (!state) continue;
    state->stream_seed = derived_seed(seed, value.property_names[index], 0);
    released += state->randc_used_values.size() * sizeof(std::uint64_t);
    state->randc_domain_signature = 0;
    state->randc_cycle = 0;
    state->randc_used_values.clear();
  }
  value.accounted_bytes -= released;
  storage_bytes_ -= released;
}

void SystemVerilogClassHeap::commit_randc_states(
    const SystemVerilogClassHandle handle,
    std::vector<SystemVerilogClassRandcStateUpdate>& updates) {
  auto& value = object(handle);
  std::set<std::size_t> indices;
  std::size_t old_bytes{};
  std::size_t new_bytes{};
  for (const auto& update : updates) {
    if (update.property_index >= value.properties.size()
        || !indices.insert(update.property_index).second) {
      throw std::invalid_argument{"randc state update has an invalid property index"};
    }
    const auto& current = value.properties[update.property_index].random_state;
    if (!current || current->kind != SystemVerilogClassRandomKind::Randc
        || update.state.kind != SystemVerilogClassRandomKind::Randc
        || update.state.width != current->width
        || update.state.nominal_type != current->nominal_type) {
      throw std::invalid_argument{"randc state update does not match its property"};
    }
    std::set<std::uint64_t> used;
    if (std::ranges::any_of(
            update.state.randc_used_values,
            [&](const auto index) { return !used.insert(index).second; })) {
      throw std::invalid_argument{"randc state update repeats a used value"};
    }
    old_bytes = checked_add(
        old_bytes,
        current->randc_used_values.size() * sizeof(std::uint64_t),
        "randc cycle state");
    new_bytes = checked_add(
        new_bytes,
        update.state.randc_used_values.size() * sizeof(std::uint64_t),
        "randc cycle state");
  }
  const auto retained_object_bytes = value.accounted_bytes - old_bytes;
  const auto retained_storage_bytes = storage_bytes_ - old_bytes;
  const auto updated_object_bytes = checked_add(
      retained_object_bytes, new_bytes, "randc cycle state");
  const auto updated_storage_bytes = checked_add(
      retained_storage_bytes, new_bytes, "randc cycle state");
  if (updated_storage_bytes > limits_.maximum_storage_bytes) {
    throw std::length_error{"SystemVerilog class storage budget exceeded"};
  }
  value.accounted_bytes = updated_object_bytes;
  storage_bytes_ = updated_storage_bytes;
  for (auto& update : updates) {
    value.properties[update.property_index].random_state =
        std::move(update.state);
  }
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

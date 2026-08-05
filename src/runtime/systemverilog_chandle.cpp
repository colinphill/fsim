// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_chandle.hpp"

#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::size_t fixed_storage_bytes = 32;

[[nodiscard]] std::size_t descriptor_bytes(
    const SystemVerilogChandleDescriptor& descriptor) {
  if (descriptor.type_identity.empty()) {
    throw std::invalid_argument{
        "chandle requires a nonempty foreign type identity"};
  }
  if (descriptor.type_identity.size()
      > std::numeric_limits<std::size_t>::max()
          - descriptor.debug_label.size()) {
    throw std::length_error{"chandle descriptor storage overflows"};
  }
  const auto text = descriptor.type_identity.size()
      + descriptor.debug_label.size();
  if (text > std::numeric_limits<std::size_t>::max()
          - fixed_storage_bytes) {
    throw std::length_error{"chandle descriptor storage overflows"};
  }
  return fixed_storage_bytes + text;
}

}  // namespace

SystemVerilogChandleRegistry::SystemVerilogChandleRegistry(
    const SystemVerilogChandleLimits limits)
    : limits_(limits) {}

SystemVerilogChandleRegistry::~SystemVerilogChandleRegistry() noexcept {
  for (auto& slot : slots_) destroy_without_notification(slot);
}

SystemVerilogChandle SystemVerilogChandleRegistry::encode(
    const std::uint32_t slot,
    const std::uint32_t generation) noexcept {
  return (static_cast<std::uint64_t>(generation) << 32U)
      | (static_cast<std::uint64_t>(slot) + 1U);
}

std::uint32_t SystemVerilogChandleRegistry::slot_of(
    const SystemVerilogChandle handle) noexcept {
  const auto encoded = static_cast<std::uint32_t>(handle);
  return encoded == 0 ? std::numeric_limits<std::uint32_t>::max()
                      : encoded - 1U;
}

std::uint32_t SystemVerilogChandleRegistry::generation_of(
    const SystemVerilogChandle handle) noexcept {
  return static_cast<std::uint32_t>(handle >> 32U);
}

const SystemVerilogChandleRegistry::Slot*
SystemVerilogChandleRegistry::find(
    const SystemVerilogChandle handle) const noexcept {
  if (handle == 0) return nullptr;
  const auto slot = slot_of(handle);
  if (slot >= slots_.size()) return nullptr;
  const auto& candidate = slots_[slot];
  return candidate.occupied
          && candidate.generation == generation_of(handle)
      ? &candidate : nullptr;
}

SystemVerilogChandleRegistry::Slot* SystemVerilogChandleRegistry::find(
    const SystemVerilogChandle handle) noexcept {
  return const_cast<Slot*>(std::as_const(*this).find(handle));
}

SystemVerilogChandleSnapshot SystemVerilogChandleRegistry::snapshot(
    const std::uint32_t slot,
    const Slot& value) const {
  return {
      encode(slot, value.generation),
      value.type_identity,
      value.debug_label,
      value.alias_transfers};
}

void SystemVerilogChandleRegistry::notify(
    const SystemVerilogChandleEvent& event) noexcept {
  try {
    const auto observers = observers_;
    for (const auto& [token, observer] : observers) {
      (void)token;
      try {
        observer(event);
      } catch (...) {
        // Observation cannot make a published opaque identity unreachable or
        // turn a completed release back into a live handle.
      }
    }
  } catch (...) {
    // Observer snapshot allocation is also advisory and cannot roll back an
    // identity that has already been published.
  }
}

SystemVerilogChandle SystemVerilogChandleRegistry::create(
    SystemVerilogChandleDescriptor descriptor) {
  const auto bytes = descriptor_bytes(descriptor);
  if (live_handles_ >= limits_.maximum_live_handles) {
    throw std::length_error{"chandle live-handle limit exceeded"};
  }
  if (storage_bytes_ > limits_.maximum_storage_bytes
      || bytes > limits_.maximum_storage_bytes - storage_bytes_) {
    throw std::length_error{"chandle storage-byte limit exceeded"};
  }

  const auto reuse = !free_slots_.empty();
  std::uint32_t index{};
  std::uint32_t generation{1};
  if (reuse) {
    index = *free_slots_.begin();
    generation = slots_[index].generation;
  } else {
    if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error{"chandle slot space exhausted"};
    }
    index = static_cast<std::uint32_t>(slots_.size());
  }
  // Copy all callback-visible metadata before publishing or consuming a free
  // slot. Allocation failure therefore leaves the registry unchanged.
  const SystemVerilogChandleSnapshot value{
      encode(index, generation),
      descriptor.type_identity,
      descriptor.debug_label,
      0};
  if (reuse) {
    free_slots_.erase(free_slots_.begin());
  } else {
    slots_.emplace_back();
  }
  auto& slot = slots_[index];
  slot.occupied = true;
  slot.type_identity = std::move(descriptor.type_identity);
  slot.debug_label = std::move(descriptor.debug_label);
  slot.alias_transfers = 0;
  slot.accounted_bytes = bytes;
  slot.cleanup = std::move(descriptor.cleanup);
  ++live_handles_;
  storage_bytes_ += bytes;
  notify({SystemVerilogChandleEventKind::Created, value});
  return value.handle;
}

SystemVerilogChandle SystemVerilogChandleRegistry::alias(
    const SystemVerilogChandle handle) {
  if (handle == 0) return 0;
  auto* slot = find(handle);
  if (slot == nullptr) {
    throw std::out_of_range{"null or stale chandle alias"};
  }
  if (slot->alias_transfers == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"chandle alias counter exhausted"};
  }
  ++slot->alias_transfers;
  const auto value = snapshot(slot_of(handle), *slot);
  notify({SystemVerilogChandleEventKind::Aliased, value});
  return handle;
}

bool SystemVerilogChandleRegistry::release(
    const SystemVerilogChandle handle) {
  auto* slot = find(handle);
  if (slot == nullptr) return false;
  const auto index = slot_of(handle);
  const auto value = snapshot(index, *slot);
  auto cleanup = std::move(slot->cleanup);
  storage_bytes_ -= slot->accounted_bytes;
  --live_handles_;
  slot->occupied = false;
  slot->type_identity.clear();
  slot->debug_label.clear();
  slot->alias_transfers = 0;
  slot->accounted_bytes = 0;
  if (slot->generation != std::numeric_limits<std::uint32_t>::max()) {
    ++slot->generation;
    free_slots_.insert(index);
  }

  std::exception_ptr cleanup_failure;
  try {
    if (cleanup) cleanup();
  } catch (...) {
    cleanup_failure = std::current_exception();
  }
  notify({SystemVerilogChandleEventKind::Released, value});
  if (cleanup_failure) std::rethrow_exception(cleanup_failure);
  return true;
}

void SystemVerilogChandleRegistry::clear() {
  const auto live = snapshots();
  std::exception_ptr first_failure;
  for (const auto& value : live) {
    try {
      (void)release(value.handle);
    } catch (...) {
      if (!first_failure) first_failure = std::current_exception();
    }
  }
  if (first_failure) std::rethrow_exception(first_failure);
}

bool SystemVerilogChandleRegistry::contains(
    const SystemVerilogChandle handle) const noexcept {
  return find(handle) != nullptr;
}

SystemVerilogChandleSnapshot SystemVerilogChandleRegistry::inspect(
    const SystemVerilogChandle handle) const {
  const auto* slot = find(handle);
  if (slot == nullptr) {
    throw std::out_of_range{"null or stale chandle inspection"};
  }
  return snapshot(slot_of(handle), *slot);
}

std::vector<SystemVerilogChandleSnapshot>
SystemVerilogChandleRegistry::snapshots() const {
  std::vector<SystemVerilogChandleSnapshot> result;
  result.reserve(live_handles_);
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (!slots_[index].occupied) continue;
    result.push_back(snapshot(
        static_cast<std::uint32_t>(index), slots_[index]));
  }
  return result;
}

std::string SystemVerilogChandleRegistry::format(
    const SystemVerilogChandle handle) const {
  if (handle == 0) return "null";
  const auto* slot = find(handle);
  if (slot == nullptr) {
    return "stale chandle <opaque " + std::to_string(handle) + ">";
  }
  auto result = "chandle <opaque " + std::to_string(handle) + "> type "
      + slot->type_identity;
  if (!slot->debug_label.empty()) result += " label " + slot->debug_label;
  return result;
}

std::uint64_t SystemVerilogChandleRegistry::add_observer(
    Observer observer) {
  if (!observer) {
    throw std::invalid_argument{"chandle observer cannot be empty"};
  }
  if (observers_.size() >= limits_.maximum_observers) {
    throw std::length_error{"chandle observer limit exceeded"};
  }
  if (next_observer_ == 0) {
    throw std::overflow_error{"chandle observer token space exhausted"};
  }
  const auto token = next_observer_++;
  observers_.emplace(token, std::move(observer));
  return token;
}

bool SystemVerilogChandleRegistry::remove_observer(
    const std::uint64_t token) noexcept {
  return observers_.erase(token) != 0;
}

void SystemVerilogChandleRegistry::destroy_without_notification(
    Slot& slot) noexcept {
  if (!slot.occupied) return;
  slot.occupied = false;
  try {
    if (slot.cleanup) slot.cleanup();
  } catch (...) {
  }
  slot.cleanup = {};
}

}  // namespace fsim::runtime

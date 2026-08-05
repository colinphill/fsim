// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime {

using SystemVerilogClassHandle = std::uint64_t;

class SystemVerilogClassHeap;

enum class SystemVerilogClassContainerKind : std::uint8_t {
  FixedArray,
  DynamicArray,
  Queue,
  AssociativeArray,
  UnpackedAggregate,
};

struct SystemVerilogClassHandleContainerDescriptor {
  SystemVerilogClassContainerKind kind{
      SystemVerilogClassContainerKind::DynamicArray};
  std::string declared_element_type;
  std::size_t maximum_elements{
      std::numeric_limits<std::size_t>::max()};
  std::size_t initial_elements{};
  std::vector<std::string> aggregate_members;
  bool packed{};
  // Fixed and explicitly bounded containers reserve their full declared
  // budget. Language-unbounded source containers account their initial
  // descriptor storage and retain an addressability-derived element ceiling.
  bool reserve_maximum_storage{true};
};

/// Value-semantic container storage for opaque class handles. The caller sets
/// a storage-derived maximum; no language-visible fixed ceiling is imposed.
class SystemVerilogClassHandleContainer final {
 public:
  SystemVerilogClassHandleContainer() = default;
  explicit SystemVerilogClassHandleContainer(
      SystemVerilogClassHandleContainerDescriptor descriptor);

  [[nodiscard]] SystemVerilogClassContainerKind kind() const noexcept {
    return descriptor_.kind;
  }
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t maximum_elements() const noexcept {
    return descriptor_.maximum_elements;
  }
  [[nodiscard]] SystemVerilogClassHandle at(std::size_t index) const;
  void set(
      SystemVerilogClassHeap& heap,
      std::size_t index,
      SystemVerilogClassHandle handle);
  void resize(std::size_t size);
  void push_back(
      SystemVerilogClassHeap& heap,
      SystemVerilogClassHandle handle);
  [[nodiscard]] SystemVerilogClassHandle pop_front();
  [[nodiscard]] SystemVerilogClassHandle at(std::string_view key) const;
  void set(
      SystemVerilogClassHeap& heap,
      std::string key,
      SystemVerilogClassHandle handle);
  [[nodiscard]] bool erase(std::string_view key);

 private:
  void validate(
      SystemVerilogClassHeap& heap,
      SystemVerilogClassHandle handle) const;

  SystemVerilogClassHandleContainerDescriptor descriptor_;
  std::vector<SystemVerilogClassHandle> sequential_;
  std::map<std::string, SystemVerilogClassHandle> keyed_;
};

enum class SystemVerilogClassPropertyKind : std::uint8_t {
  Bit2,
  Logic4,
  Logic9,
  Integer,
  String,
  ClassHandle,
  Container,
};

struct SystemVerilogClassPropertyDescriptor {
  SystemVerilogClassPropertyDescriptor() = default;
  SystemVerilogClassPropertyDescriptor(
      std::string property_name,
      const SystemVerilogClassPropertyKind property_kind,
      const std::size_t property_width,
      std::optional<SystemVerilogClassHandleContainerDescriptor>
          container = std::nullopt)
      : name(std::move(property_name)),
        kind(property_kind),
        width(property_width),
        handle_container(std::move(container)) {}

  std::string name;
  SystemVerilogClassPropertyKind kind{
      SystemVerilogClassPropertyKind::Logic4};
  std::size_t width{1};
  std::optional<SystemVerilogClassHandleContainerDescriptor> handle_container;
  std::optional<PackedLogic4> initial_packed;
  std::optional<std::string> initial_string;
};

struct SystemVerilogClassDescriptor {
  std::string declared_type;
  std::string dynamic_type;
  std::string specialization_identity;
  // Dynamic type followed by every legal base/interface handle view.
  std::vector<std::string> assignable_declared_types;
  std::vector<SystemVerilogClassPropertyDescriptor> properties;
};

struct SystemVerilogClassPropertyValue {
  SystemVerilogClassPropertyKind kind{
      SystemVerilogClassPropertyKind::Logic4};
  PackedLogic4 packed;
  std::string string;
  SystemVerilogClassHandle handle{};
  std::vector<SystemVerilogClassHandle> handles;
  std::optional<SystemVerilogClassHandleContainer> handle_container;
};

struct SystemVerilogClassObject {
  std::string declared_type;
  std::string dynamic_type;
  std::string specialization_identity;
  std::vector<std::string> assignable_declared_types;
  std::vector<std::string> property_names;
  std::vector<SystemVerilogClassPropertyValue> properties;
  std::size_t accounted_bytes{};
};

struct SystemVerilogClassHeapLimits {
  std::size_t maximum_live_objects{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_storage_bytes{
      std::numeric_limits<std::size_t>::max()};
};

/// Scheduler-owned, generation-safe heap for opaque SystemVerilog handles.
/// Zero is null. Handles encode a slot and generation but never a host address.
class SystemVerilogClassHeap final {
 public:
  using ConstructorStep = std::function<void(
      SystemVerilogClassHeap&, SystemVerilogClassHandle)>;

  explicit SystemVerilogClassHeap(
      SystemVerilogClassHeapLimits limits = {});

  [[nodiscard]] SystemVerilogClassHandle allocate(
      const SystemVerilogClassDescriptor& descriptor);
  [[nodiscard]] SystemVerilogClassHandle construct(
      const SystemVerilogClassDescriptor& descriptor,
      std::span<const ConstructorStep> constructor_steps);
  [[nodiscard]] bool release(SystemVerilogClassHandle handle) noexcept;
  void clear() noexcept;

  [[nodiscard]] bool contains(SystemVerilogClassHandle handle) const noexcept;
  [[nodiscard]] SystemVerilogClassObject& object(
      SystemVerilogClassHandle handle);
  [[nodiscard]] const SystemVerilogClassObject& object(
      SystemVerilogClassHandle handle) const;
  [[nodiscard]] SystemVerilogClassPropertyValue& property(
      SystemVerilogClassHandle handle,
      std::string_view name);
  [[nodiscard]] const SystemVerilogClassPropertyValue& property(
      SystemVerilogClassHandle handle,
      std::string_view name) const;
  [[nodiscard]] SystemVerilogClassHandle checked_cast(
      SystemVerilogClassHandle handle,
      std::string_view declared_type) const;
  [[nodiscard]] static bool equal(
      SystemVerilogClassHandle left,
      SystemVerilogClassHandle right) noexcept {
    return left == right;
  }

  [[nodiscard]] std::size_t live_objects() const noexcept {
    return live_objects_;
  }
  [[nodiscard]] std::size_t storage_bytes() const noexcept {
    return storage_bytes_;
  }
  [[nodiscard]] std::vector<SystemVerilogClassHandle>
  live_handles() const;
  [[nodiscard]] const SystemVerilogClassHeapLimits& limits() const noexcept {
    return limits_;
  }

 private:
  struct Slot {
    std::uint32_t generation{1};
    bool occupied{};
    SystemVerilogClassObject object;
  };

  [[nodiscard]] static SystemVerilogClassHandle encode(
      std::uint32_t slot,
      std::uint32_t generation) noexcept;
  [[nodiscard]] static std::uint32_t slot_of(
      SystemVerilogClassHandle handle) noexcept;
  [[nodiscard]] static std::uint32_t generation_of(
      SystemVerilogClassHandle handle) noexcept;
  [[nodiscard]] const Slot* find(SystemVerilogClassHandle handle) const noexcept;
  [[nodiscard]] Slot* find(SystemVerilogClassHandle handle) noexcept;

  SystemVerilogClassHeapLimits limits_;
  std::vector<Slot> slots_;
  std::set<std::uint32_t> free_slots_;
  std::size_t live_objects_{};
  std::size_t storage_bytes_{};
};

}  // namespace fsim::runtime

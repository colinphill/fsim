// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_heap.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogUvmFieldFlag : std::uint16_t {
  None = 0,
  NoCopy = 1U << 0U,
  NoCompare = 1U << 1U,
  NoPrint = 1U << 2U,
  NoRecord = 1U << 3U,
  Reference = 1U << 4U,
};

[[nodiscard]] constexpr SystemVerilogUvmFieldFlag operator|(
    const SystemVerilogUvmFieldFlag left,
    const SystemVerilogUvmFieldFlag right) noexcept {
  return static_cast<SystemVerilogUvmFieldFlag>(
      static_cast<std::uint16_t>(left)
      | static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr bool has_flag(
    const SystemVerilogUvmFieldFlag value,
    const SystemVerilogUvmFieldFlag flag) noexcept {
  return (static_cast<std::uint16_t>(value)
          & static_cast<std::uint16_t>(flag)) != 0;
}

struct SystemVerilogUvmFieldDescriptor {
  std::string property;
  SystemVerilogUvmFieldFlag flags{SystemVerilogUvmFieldFlag::None};
};

enum class SystemVerilogUvmObjectEntryKind : std::uint8_t {
  Object,
  Packed,
  String,
  NullHandle,
  Reference,
  Cycle,
  Container,
  Custom,
};

struct SystemVerilogUvmObjectEntry {
  std::string path;
  std::string type_name;
  std::string value;
  SystemVerilogClassHandle object{};
  SystemVerilogUvmObjectEntryKind kind{
      SystemVerilogUvmObjectEntryKind::Object};
  std::size_t depth{};
};

struct SystemVerilogUvmObjectDescriptor {
  std::string specialization_identity;
  std::string type_name;
  std::vector<SystemVerilogUvmFieldDescriptor> fields;
  std::function<void(
      SystemVerilogClassHandle, SystemVerilogClassHandle)> do_copy;
  std::function<bool(
      SystemVerilogClassHandle, SystemVerilogClassHandle)> do_compare;
  std::function<void(
      SystemVerilogClassHandle,
      std::vector<SystemVerilogUvmObjectEntry>&)> do_print;
  std::function<void(
      SystemVerilogClassHandle,
      std::vector<SystemVerilogUvmObjectEntry>&)> do_record;
  std::function<std::string(SystemVerilogClassHandle)> get_full_name;
};

struct SystemVerilogUvmObjectLimits {
  std::size_t maximum_depth{1024};
  std::size_t maximum_objects{1U << 20U};
  std::size_t maximum_fields{1U << 22U};
  std::size_t maximum_output_bytes{1U << 28U};
};

/// UVM object policy over the simulation-owned class heap. Handles remain
/// opaque and generation-safe. Recursive operations preserve aliases and
/// cycles, are resource bounded, and publish copy changes transactionally.
class SystemVerilogUvmObjectService final {
 public:
  using CreateHook = std::function<SystemVerilogClassHandle(
      std::string_view specialization_identity,
      std::string_view declared_type,
      std::string_view name)>;
  using EntryHook = std::function<void(
      std::span<const SystemVerilogUvmObjectEntry>)>;

  explicit SystemVerilogUvmObjectService(
      SystemVerilogClassHeap& heap,
      CreateHook create_hook,
      SystemVerilogUvmObjectLimits limits = {});

  void register_type(SystemVerilogUvmObjectDescriptor descriptor);
  [[nodiscard]] bool
  contains_type(std::string_view specialization_identity) const noexcept;
  void initialize(SystemVerilogClassHandle object, std::string name = {});
  [[nodiscard]] bool contains(
      SystemVerilogClassHandle object) const noexcept;
  void set_name(SystemVerilogClassHandle object, std::string name);
  void set_full_name(SystemVerilogClassHandle object, std::string full_name);
  [[nodiscard]] std::string_view name(SystemVerilogClassHandle object) const;
  [[nodiscard]] std::string full_name(SystemVerilogClassHandle object) const;
  [[nodiscard]] std::string type_name(SystemVerilogClassHandle object) const;
  [[nodiscard]] std::uint64_t instance_id(
      SystemVerilogClassHandle object) const;
  void erase(SystemVerilogClassHandle object) noexcept;
  [[nodiscard]] std::uint64_t instance_count() const noexcept {
    return next_instance_id_;
  }

  [[nodiscard]] SystemVerilogClassHandle create(
      SystemVerilogClassHandle prototype,
      std::string name = {});
  [[nodiscard]] SystemVerilogClassHandle clone(
      SystemVerilogClassHandle source);
  void copy(
      SystemVerilogClassHandle destination,
      SystemVerilogClassHandle source);
  [[nodiscard]] bool compare(
      SystemVerilogClassHandle left,
      SystemVerilogClassHandle right);
  [[nodiscard]] std::vector<SystemVerilogUvmObjectEntry> print(
      SystemVerilogClassHandle object);
  [[nodiscard]] std::vector<SystemVerilogUvmObjectEntry> record(
      SystemVerilogClassHandle object);
  void set_print_hook(EntryHook hook) { print_hook_ = std::move(hook); }
  void set_record_hook(EntryHook hook) { record_hook_ = std::move(hook); }

  [[nodiscard]] const SystemVerilogUvmObjectLimits& limits() const noexcept {
    return limits_;
  }

 private:
  struct Metadata {
    std::string name;
    std::string full_name;
    std::uint64_t instance_id{};
  };

  [[nodiscard]] const SystemVerilogUvmObjectDescriptor& descriptor(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] Metadata& metadata(SystemVerilogClassHandle object);
  [[nodiscard]] const Metadata& metadata(
      SystemVerilogClassHandle object) const;

  SystemVerilogClassHeap* heap_{};
  CreateHook create_hook_;
  SystemVerilogUvmObjectLimits limits_;
  std::map<std::string, SystemVerilogUvmObjectDescriptor, std::less<>>
      descriptors_;
  std::map<SystemVerilogClassHandle, Metadata> metadata_;
  std::uint64_t next_instance_id_{};
  EntryHook print_hook_;
  EntryHook record_hook_;
};

}  // namespace fsim::runtime

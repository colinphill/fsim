// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_heap.hpp"

#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

class SystemVerilogClassStaticStore;

struct SystemVerilogClassStaticDescriptor {
  using Initializer = std::function<void(
      SystemVerilogClassStaticStore&, std::string_view)>;

  std::string specialization_identity;
  std::string base_specialization_identity;
  std::vector<std::string> aliases;
  std::vector<SystemVerilogClassPropertyDescriptor> properties;
  std::vector<Initializer> initializers;
};

struct SystemVerilogClassStaticLimits {
  std::size_t maximum_properties{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_storage_bytes{
      std::numeric_limits<std::size_t>::max()};
};

struct SystemVerilogClassStaticSnapshot {
  std::string specialization_identity;
  std::vector<std::string> property_names;
  std::vector<SystemVerilogClassPropertyValue> properties;
};

/// Simulation-wide state for class static members. Canonical specialization
/// identities and every registered import/root alias resolve to one entry.
class SystemVerilogClassStaticStore final {
 public:
  explicit SystemVerilogClassStaticStore(
      SystemVerilogClassStaticLimits limits = {});

  void register_specialization(SystemVerilogClassStaticDescriptor descriptor);
  void initialize(std::string_view specialization_or_alias);
  void initialize_all();

  [[nodiscard]] SystemVerilogClassPropertyValue& property(
      std::string_view specialization_or_alias,
      std::string_view name);
  [[nodiscard]] const SystemVerilogClassPropertyValue& property(
      std::string_view specialization_or_alias,
      std::string_view name) const;
  [[nodiscard]] bool initialized(
      std::string_view specialization_or_alias) const;
  /// Canonical, specialization-sorted debugger/trace view. Aliases are not
  /// duplicated and no address into the store escapes.
  [[nodiscard]] std::vector<SystemVerilogClassStaticSnapshot>
  snapshots() const;
  [[nodiscard]] std::size_t property_count() const noexcept {
    return property_count_;
  }
  [[nodiscard]] std::size_t storage_bytes() const noexcept {
    return storage_bytes_;
  }

 private:
  struct Entry {
    SystemVerilogClassStaticDescriptor descriptor;
    std::vector<std::string> property_names;
    std::vector<SystemVerilogClassPropertyValue> values;
    std::size_t accounted_bytes{};
    bool initialized{};
    bool initializing{};
  };

  [[nodiscard]] std::string resolve(
      std::string_view specialization_or_alias) const;
  [[nodiscard]] Entry& entry(std::string_view specialization_or_alias);
  [[nodiscard]] const Entry& entry(
      std::string_view specialization_or_alias) const;
  [[nodiscard]] SystemVerilogClassPropertyValue* find_property(
      Entry& value, std::string_view name);
  [[nodiscard]] const SystemVerilogClassPropertyValue* find_property(
      const Entry& value, std::string_view name) const;

  SystemVerilogClassStaticLimits limits_;
  std::map<std::string, Entry> entries_;
  std::map<std::string, std::string> aliases_;
  std::size_t property_count_{};
  std::size_t storage_bytes_{};
};

}  // namespace fsim::runtime

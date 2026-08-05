// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_static.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] std::size_t packed_bytes(
    const std::size_t width,
    const std::size_t planes) {
  if (width > std::numeric_limits<std::size_t>::max() - 7U) {
    throw std::length_error{"class static property width exceeds storage"};
  }
  const auto bytes = (width + 7U) / 8U;
  if (planes != 0
      && bytes > std::numeric_limits<std::size_t>::max() / planes) {
    throw std::length_error{"class static property storage size overflows"};
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
        const auto elements =
            descriptor.handle_container->reserve_maximum_storage
                ? descriptor.handle_container->maximum_elements
                : descriptor.handle_container->initial_elements;
        if (elements
            > std::numeric_limits<std::size_t>::max()
                / sizeof(SystemVerilogClassHandle)) {
          throw std::length_error{
              "class static handle container storage size overflows"};
        }
        return elements * sizeof(SystemVerilogClassHandle);
      }
      return 0U;
    case SystemVerilogClassPropertyKind::String:
      return 0U;
  }
  return 0U;
}

[[nodiscard]] SystemVerilogClassPropertyValue default_value(
    const SystemVerilogClassPropertyDescriptor& descriptor) {
  SystemVerilogClassPropertyValue result;
  result.kind = descriptor.kind;
  const auto width = descriptor.kind == SystemVerilogClassPropertyKind::Integer
      ? std::size_t{64}
      : descriptor.width;
  if (descriptor.kind == SystemVerilogClassPropertyKind::Bit2) {
    result.packed = PackedLogic4(width, Logic4::zero);
  } else if (descriptor.kind == SystemVerilogClassPropertyKind::Logic4) {
    result.packed = PackedLogic4(width, Logic4::x);
  } else if (descriptor.kind == SystemVerilogClassPropertyKind::Logic9) {
    result.packed = PackedLogic4(width, Logic4::x).promoted_to_logic9();
  } else if (descriptor.kind == SystemVerilogClassPropertyKind::Integer) {
    result.packed = PackedLogic4(width, Logic4::zero);
  } else if (
      descriptor.kind == SystemVerilogClassPropertyKind::Container
      && descriptor.handle_container) {
    result.handle_container.emplace(*descriptor.handle_container);
  }
  if (descriptor.initial_packed) {
    if (result.packed.width() != descriptor.initial_packed->width()) {
      throw std::invalid_argument{
          "class static initializer width does not match its type"};
    }
    result.packed = *descriptor.initial_packed;
  }
  if (descriptor.initial_string) {
    if (descriptor.kind != SystemVerilogClassPropertyKind::String) {
      throw std::invalid_argument{
          "class static string initializer requires a string property"};
    }
    result.string = *descriptor.initial_string;
  }
  return result;
}

}  // namespace

SystemVerilogClassStaticStore::SystemVerilogClassStaticStore(
    const SystemVerilogClassStaticLimits limits)
    : limits_(limits) {}

void SystemVerilogClassStaticStore::register_specialization(
    SystemVerilogClassStaticDescriptor descriptor) {
  if (descriptor.specialization_identity.empty()) {
    throw std::invalid_argument{
        "class static specialization identity must not be empty"};
  }
  if (entries_.contains(descriptor.specialization_identity)) {
    throw std::invalid_argument{
        "duplicate class static specialization '"
        + descriptor.specialization_identity + "'"};
  }
  if (aliases_.contains(descriptor.specialization_identity)) {
    throw std::invalid_argument{
        "class static specialization collides with an existing alias '"
        + descriptor.specialization_identity + "'"};
  }
  std::set<std::string> property_names;
  std::size_t bytes{};
  for (const auto& property : descriptor.properties) {
    if (property.name.empty()
        || !property_names.insert(property.name).second) {
      throw std::invalid_argument{
          "class static property names must be nonempty and unique"};
    }
    const auto additional = property_bytes(property);
    if (additional > std::numeric_limits<std::size_t>::max() - bytes) {
      throw std::length_error{"class static storage size overflows"};
    }
    bytes += additional;
  }
  if (descriptor.properties.size()
          > limits_.maximum_properties - property_count_
      || bytes > limits_.maximum_storage_bytes - storage_bytes_) {
    throw std::length_error{"SystemVerilog class static-state budget exceeded"};
  }
  std::set<std::string> retained_aliases;
  for (const auto& alias : descriptor.aliases) {
    if (alias.empty() || alias == descriptor.specialization_identity
        || !retained_aliases.insert(alias).second
        || entries_.contains(alias) || aliases_.contains(alias)) {
      throw std::invalid_argument{
          "duplicate or empty class static alias '" + alias + "'"};
    }
  }

  const auto identity = descriptor.specialization_identity;
  Entry retained;
  retained.accounted_bytes = bytes;
  retained.descriptor = std::move(descriptor);
  entries_.emplace(identity, std::move(retained));
  for (const auto& alias : entries_.at(identity).descriptor.aliases) {
    aliases_.emplace(alias, identity);
  }
  property_count_ += entries_.at(identity).descriptor.properties.size();
  storage_bytes_ += bytes;
}

void SystemVerilogClassStaticStore::initialize(
    const std::string_view specialization_or_alias) {
  auto& retained = entry(specialization_or_alias);
  if (retained.initialized) return;
  if (retained.initializing) {
    throw std::logic_error{"cyclic class static initialization dependency"};
  }
  retained.initializing = true;
  try {
    if (!retained.descriptor.base_specialization_identity.empty()) {
      initialize(retained.descriptor.base_specialization_identity);
    }
    retained.property_names.reserve(retained.descriptor.properties.size());
    retained.values.reserve(retained.descriptor.properties.size());
    for (const auto& property : retained.descriptor.properties) {
      retained.property_names.push_back(property.name);
      retained.values.push_back(default_value(property));
    }
    for (const auto& initializer : retained.descriptor.initializers) {
      if (!initializer) {
        throw std::invalid_argument{"class static initializer is empty"};
      }
      initializer(*this, retained.descriptor.specialization_identity);
    }
    retained.initialized = true;
    retained.initializing = false;
  } catch (...) {
    retained.property_names.clear();
    retained.values.clear();
    retained.initializing = false;
    throw;
  }
}

void SystemVerilogClassStaticStore::initialize_all() {
  std::vector<std::string> identities;
  identities.reserve(entries_.size());
  for (const auto& [identity, retained] : entries_) {
    (void)retained;
    identities.push_back(identity);
  }
  for (const auto& identity : identities) initialize(identity);
}

SystemVerilogClassPropertyValue& SystemVerilogClassStaticStore::property(
    const std::string_view specialization_or_alias,
    const std::string_view name) {
  auto& retained = entry(specialization_or_alias);
  if (!retained.initialized && !retained.initializing) {
    initialize(specialization_or_alias);
  }
  if (auto* found = find_property(retained, name)) return *found;
  throw std::out_of_range{
      "SystemVerilog class static property '" + std::string{name}
      + "' does not exist"};
}

const SystemVerilogClassPropertyValue&
SystemVerilogClassStaticStore::property(
    const std::string_view specialization_or_alias,
    const std::string_view name) const {
  const auto& retained = entry(specialization_or_alias);
  if (!retained.initialized) {
    throw std::logic_error{"class static specialization is not initialized"};
  }
  if (const auto* found = find_property(retained, name)) return *found;
  throw std::out_of_range{
      "SystemVerilog class static property '" + std::string{name}
      + "' does not exist"};
}

bool SystemVerilogClassStaticStore::initialized(
    const std::string_view specialization_or_alias) const {
  return entry(specialization_or_alias).initialized;
}

std::vector<SystemVerilogClassStaticSnapshot>
SystemVerilogClassStaticStore::snapshots() const {
  std::vector<SystemVerilogClassStaticSnapshot> result;
  result.reserve(entries_.size());
  for (const auto& [identity, retained] : entries_) {
    result.push_back({identity, retained.property_names, retained.values});
  }
  return result;
}

std::string SystemVerilogClassStaticStore::resolve(
    const std::string_view specialization_or_alias) const {
  const std::string spelling{specialization_or_alias};
  if (entries_.contains(spelling)) return spelling;
  if (const auto alias = aliases_.find(spelling); alias != aliases_.end()) {
    return alias->second;
  }
  throw std::out_of_range{
      "class static specialization or alias '" + spelling
      + "' is not registered"};
}

SystemVerilogClassStaticStore::Entry& SystemVerilogClassStaticStore::entry(
    const std::string_view specialization_or_alias) {
  return entries_.at(resolve(specialization_or_alias));
}

const SystemVerilogClassStaticStore::Entry&
SystemVerilogClassStaticStore::entry(
    const std::string_view specialization_or_alias) const {
  return entries_.at(resolve(specialization_or_alias));
}

SystemVerilogClassPropertyValue*
SystemVerilogClassStaticStore::find_property(
    Entry& value,
    const std::string_view name) {
  const auto found = std::ranges::find(value.property_names, name);
  if (found != value.property_names.end()) {
    return &value.values[static_cast<std::size_t>(
        std::distance(value.property_names.begin(), found))];
  }
  if (value.descriptor.base_specialization_identity.empty()) return nullptr;
  auto& base = entry(value.descriptor.base_specialization_identity);
  if (!base.initialized && !base.initializing) {
    initialize(base.descriptor.specialization_identity);
  }
  return find_property(base, name);
}

const SystemVerilogClassPropertyValue*
SystemVerilogClassStaticStore::find_property(
    const Entry& value,
    const std::string_view name) const {
  const auto found = std::ranges::find(value.property_names, name);
  if (found != value.property_names.end()) {
    return &value.values[static_cast<std::size_t>(
        std::distance(value.property_names.begin(), found))];
  }
  if (value.descriptor.base_specialization_identity.empty()) return nullptr;
  return find_property(
      entry(value.descriptor.base_specialization_identity), name);
}

}  // namespace fsim::runtime

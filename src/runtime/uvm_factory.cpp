// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_factory.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

[[nodiscard]] bool wildcard_match(
    const std::string_view pattern,
    const std::string_view value) noexcept {
  std::size_t pattern_index{};
  std::size_t value_index{};
  std::size_t star{std::string_view::npos};
  std::size_t retry{};
  while (value_index < value.size()) {
    if (pattern_index < pattern.size()
        && (pattern[pattern_index] == '?'
            || pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size()
               && pattern[pattern_index] == '*') {
      star = pattern_index++;
      retry = value_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1U;
      value_index = ++retry;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

[[nodiscard]] SystemVerilogUvmFactoryTraceStep trace_step(
    const SystemVerilogUvmFactoryOverride& entry) {
  return {
      entry.kind,
      entry.original,
      entry.override,
      entry.original_name,
      entry.instance_pattern,
      entry.registration_order};
}

}

SystemVerilogUvmFactoryService::SystemVerilogUvmFactoryService(
    SystemVerilogUvmRegistryService& registry,
    const SystemVerilogUvmFactoryLimits limits)
    : registry_(&registry), limits_(limits) {
  if (limits_.max_type_overrides == 0
      || limits_.max_instance_overrides == 0
      || limits_.max_type_name_bytes == 0
      || limits_.max_instance_path_bytes == 0
      || limits_.max_resolution_depth == 0
      || limits_.max_report_bytes == 0) {
    throw std::invalid_argument{"UVM factory limits must be nonzero"};
  }
}

void SystemVerilogUvmFactoryService::validate_type_name(
    const std::string_view name) const {
  if (name.empty()) {
    throw std::invalid_argument{"UVM factory type name must not be empty"};
  }
  if (name.size() > limits_.max_type_name_bytes) {
    throw std::length_error{"UVM factory type name exceeds configured limit"};
  }
}

void SystemVerilogUvmFactoryService::validate_instance_path(
    const std::string_view path) const {
  if (path.size() > limits_.max_instance_path_bytes) {
    throw std::length_error{"UVM factory instance path exceeds configured limit"};
  }
}

std::string SystemVerilogUvmFactoryService::checked_type_name(
    const SystemVerilogUvmTypeHandle wrapper) const {
  if (!registry_->contains(wrapper)) {
    throw std::out_of_range{"unknown UVM factory type wrapper"};
  }
  return registry_->snapshot(wrapper).type_name;
}

bool SystemVerilogUvmFactoryService::set_type_override_by_type(
    const SystemVerilogUvmTypeHandle original,
    const SystemVerilogUvmTypeHandle override,
    const bool replace) {
  const auto original_name = checked_type_name(original);
  (void)checked_type_name(override);
  if (original == override) {
    throw std::invalid_argument{"UVM factory type override forms a self-loop"};
  }
  const auto found = std::ranges::find_if(
      type_overrides_, [&](const auto& entry) {
        return entry.original == original
            || entry.original_name == original_name;
      });
  if (found != type_overrides_.end()) {
    if (!replace) return false;
    found->original = original;
    found->original_name = original_name;
    found->override = override;
    return true;
  }
  if (type_overrides_.size() >= limits_.max_type_overrides) {
    throw std::length_error{"UVM factory type override limit exceeded"};
  }
  type_overrides_.push_back({
      SystemVerilogUvmOverrideKind::Type,
      original,
      override,
      original_name,
      {},
      next_registration_order_++,
      0});
  return true;
}

bool SystemVerilogUvmFactoryService::set_type_override_by_name(
    const std::string_view original_name,
    const std::string_view override_name,
    const bool replace) {
  validate_type_name(original_name);
  validate_type_name(override_name);
  const auto override = registry_->wrapper_by_name(override_name);
  if (override == 0) {
    throw std::out_of_range{"UVM factory override type name is not registered"};
  }
  if (original_name == override_name) {
    throw std::invalid_argument{"UVM factory type override forms a self-loop"};
  }
  const auto original = registry_->wrapper_by_name(original_name);
  const auto found = std::ranges::find(
      type_overrides_, original_name,
      &SystemVerilogUvmFactoryOverride::original_name);
  if (found != type_overrides_.end()) {
    if (!replace) return false;
    found->original = original;
    found->override = override;
    return true;
  }
  if (type_overrides_.size() >= limits_.max_type_overrides) {
    throw std::length_error{"UVM factory type override limit exceeded"};
  }
  type_overrides_.push_back({
      SystemVerilogUvmOverrideKind::Type,
      original,
      override,
      std::string{original_name},
      {},
      next_registration_order_++,
      0});
  return true;
}

bool SystemVerilogUvmFactoryService::set_instance_override_by_type(
    const SystemVerilogUvmTypeHandle original,
    const SystemVerilogUvmTypeHandle override,
    const std::string_view full_instance_pattern) {
  const auto original_name = checked_type_name(original);
  (void)checked_type_name(override);
  validate_instance_path(full_instance_pattern);
  if (full_instance_pattern.empty()) {
    throw std::invalid_argument{"UVM instance override path must not be empty"};
  }
  const auto duplicate = std::ranges::any_of(
      instance_overrides_, [&](const auto& entry) {
        return entry.original == original && entry.override == override
            && entry.instance_pattern == full_instance_pattern;
      });
  if (duplicate) return false;
  if (instance_overrides_.size() >= limits_.max_instance_overrides) {
    throw std::length_error{"UVM factory instance override limit exceeded"};
  }
  instance_overrides_.push_back({
      SystemVerilogUvmOverrideKind::Instance,
      original,
      override,
      original_name,
      std::string{full_instance_pattern},
      next_registration_order_++,
      0});
  return true;
}

bool SystemVerilogUvmFactoryService::set_instance_override_by_name(
    const std::string_view original_name,
    const std::string_view override_name,
    const std::string_view full_instance_pattern) {
  validate_type_name(original_name);
  validate_type_name(override_name);
  validate_instance_path(full_instance_pattern);
  if (full_instance_pattern.empty()) {
    throw std::invalid_argument{"UVM instance override path must not be empty"};
  }
  const auto override = registry_->wrapper_by_name(override_name);
  if (override == 0) {
    throw std::out_of_range{"UVM factory override type name is not registered"};
  }
  const auto original = registry_->wrapper_by_name(original_name);
  const auto duplicate = std::ranges::any_of(
      instance_overrides_, [&](const auto& entry) {
        return entry.original_name == original_name
            && entry.override == override
            && entry.instance_pattern == full_instance_pattern;
      });
  if (duplicate) return false;
  if (instance_overrides_.size() >= limits_.max_instance_overrides) {
    throw std::length_error{"UVM factory instance override limit exceeded"};
  }
  instance_overrides_.push_back({
      SystemVerilogUvmOverrideKind::Instance,
      original,
      override,
      std::string{original_name},
      std::string{full_instance_pattern},
      next_registration_order_++,
      0});
  return true;
}

SystemVerilogUvmFactoryResolution SystemVerilogUvmFactoryService::resolve(
    const SystemVerilogUvmTypeHandle requested,
    std::string requested_name,
    const std::string_view full_instance_path,
    const bool count_uses) const {
  validate_instance_path(full_instance_path);
  if (requested != 0) requested_name = checked_type_name(requested);
  validate_type_name(requested_name);
  SystemVerilogUvmFactoryResolution result{
      requested, requested, requested_name, std::string{full_instance_path}, {}};
  auto current = requested;
  auto current_name = std::move(requested_name);
  std::vector<SystemVerilogUvmTypeHandle> visited;
  if (current != 0) visited.push_back(current);
  std::vector<std::pair<SystemVerilogUvmOverrideKind, std::size_t>> selected;
  while (true) {
    const SystemVerilogUvmFactoryOverride* match{};
    std::size_t match_index{};
    if (!full_instance_path.empty()) {
      for (std::size_t index = 0; index < instance_overrides_.size(); ++index) {
        const auto& candidate = instance_overrides_[index];
        const auto original_matches = candidate.original != 0
            ? candidate.original == current
            : wildcard_match(candidate.original_name, current_name);
        if (original_matches
            && wildcard_match(candidate.instance_pattern, full_instance_path)) {
          match = &candidate;
          match_index = index;
          break;
        }
      }
    }
    auto kind = SystemVerilogUvmOverrideKind::Instance;
    if (match == nullptr) {
      kind = SystemVerilogUvmOverrideKind::Type;
      for (std::size_t index = 0; index < type_overrides_.size(); ++index) {
        const auto& candidate = type_overrides_[index];
        if ((candidate.original != 0 && candidate.original == current)
            || candidate.original_name == current_name) {
          match = &candidate;
          match_index = index;
          break;
        }
      }
    }
    if (match == nullptr) break;
    if (result.steps.size() >= limits_.max_resolution_depth) {
      throw std::length_error{"UVM factory override resolution depth exceeded"};
    }
    result.steps.push_back(trace_step(*match));
    selected.emplace_back(kind, match_index);
    current = match->override;
    if (std::ranges::find(visited, current) != visited.end()) {
      throw std::invalid_argument{"recursive UVM factory override loop"};
    }
    visited.push_back(current);
    current_name = checked_type_name(current);
  }
  if (current == 0) {
    throw std::out_of_range{"requested UVM factory type name is not registered"};
  }
  result.resolved = current;
  if (count_uses) {
    for (const auto& [kind, index] : selected) {
      auto& entry = kind == SystemVerilogUvmOverrideKind::Instance
          ? instance_overrides_[index] : type_overrides_[index];
      if (entry.uses == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error{"UVM factory override use count overflow"};
      }
      ++entry.uses;
    }
  }
  return result;
}

SystemVerilogUvmFactoryResolution
SystemVerilogUvmFactoryService::resolve_by_type(
    const SystemVerilogUvmTypeHandle requested,
    const std::string_view full_instance_path) {
  return resolve(requested, {}, full_instance_path, true);
}

SystemVerilogUvmFactoryResolution
SystemVerilogUvmFactoryService::resolve_by_name(
    const std::string_view requested_name,
    const std::string_view full_instance_path) {
  return resolve(
      registry_->wrapper_by_name(requested_name),
      std::string{requested_name}, full_instance_path, true);
}

SystemVerilogUvmFactoryResolution
SystemVerilogUvmFactoryService::debug_resolve_by_type(
    const SystemVerilogUvmTypeHandle requested,
    const std::string_view full_instance_path) const {
  return resolve(requested, {}, full_instance_path, false);
}

SystemVerilogUvmFactoryResolution
SystemVerilogUvmFactoryService::debug_resolve_by_name(
    const std::string_view requested_name,
    const std::string_view full_instance_path) const {
  return resolve(
      registry_->wrapper_by_name(requested_name),
      std::string{requested_name}, full_instance_path, false);
}

std::string SystemVerilogUvmFactoryService::full_instance_path(
    const std::string_view parent, const std::string_view name) {
  if (parent.empty()) return std::string{name};
  if (name.empty()) return std::string{parent};
  std::string result{parent};
  result += '.';
  result += name;
  return result;
}

SystemVerilogClassHandle SystemVerilogUvmFactoryService::create_object_by_type(
    const SystemVerilogUvmTypeHandle requested,
    const std::string_view parent_instance_path,
    const std::string_view name) {
  const auto resolved = resolve_by_type(
      requested, full_instance_path(parent_instance_path, name));
  return registry_->create_object_by_type(resolved.resolved, name);
}

SystemVerilogClassHandle SystemVerilogUvmFactoryService::create_object_by_name(
    const std::string_view requested_name,
    const std::string_view parent_instance_path,
    const std::string_view name) {
  const auto resolved = resolve_by_name(
      requested_name, full_instance_path(parent_instance_path, name));
  return registry_->create_object_by_type(resolved.resolved, name);
}

SystemVerilogClassHandle
SystemVerilogUvmFactoryService::create_component_by_type(
    const SystemVerilogUvmTypeHandle requested,
    const std::string_view parent_instance_path,
    const std::string_view name,
    const SystemVerilogClassHandle parent,
    const SystemVerilogUvmRootHandle root) {
  const auto resolved = resolve_by_type(
      requested, full_instance_path(parent_instance_path, name));
  return registry_->create_component_by_type(
      resolved.resolved, name, parent, root);
}

SystemVerilogClassHandle
SystemVerilogUvmFactoryService::create_component_by_name(
    const std::string_view requested_name,
    const std::string_view parent_instance_path,
    const std::string_view name,
    const SystemVerilogClassHandle parent,
    const SystemVerilogUvmRootHandle root) {
  const auto resolved = resolve_by_name(
      requested_name, full_instance_path(parent_instance_path, name));
  return registry_->create_component_by_type(
      resolved.resolved, name, parent, root);
}

std::string SystemVerilogUvmFactoryService::report(const bool all_types) const {
  std::string result;
  const auto append = [&](const std::string_view text) {
    if (text.size() > limits_.max_report_bytes - result.size()) {
      throw std::length_error{"UVM factory report exceeds configured limit"};
    }
    result += text;
  };
  if (all_types) {
    append("Registered Types\n");
    for (const auto& type : registry_->types()) {
      append("  [" + std::to_string(type.registration_order) + "] ");
      append(type.type_name);
      append(" wrapper=" + std::to_string(type.wrapper));
      append(type.kind == SystemVerilogUvmRegisteredKind::Component
                 ? " kind=component\n" : " kind=object\n");
    }
  }
  append("Type Overrides\n");
  for (const auto& entry : type_overrides_) {
    append("  [" + std::to_string(entry.registration_order) + "] ");
    append(entry.original_name);
    append(" -> " + registry_->snapshot(entry.override).type_name);
    append(" uses=" + std::to_string(entry.uses) + "\n");
  }
  append("Instance Overrides\n");
  for (const auto& entry : instance_overrides_) {
    append("  [" + std::to_string(entry.registration_order) + "] ");
    append(entry.original_name);
    append(" @ " + entry.instance_pattern + " -> ");
    append(registry_->snapshot(entry.override).type_name);
    append(" uses=" + std::to_string(entry.uses) + "\n");
  }
  return result;
}

}

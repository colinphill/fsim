// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_scope.hpp"

#include <limits>
#include <utility>

namespace fsim::runtime {

namespace {

[[nodiscard]] bool valid_scope_name(const std::string_view name) noexcept {
  if (name.empty() || name.front() == '.' || name.back() == '.') return false;
  bool previous_separator{};
  for (const char character : name) {
    if (character == '\0') return false;
    const bool separator = character == '.';
    if (separator && previous_separator) return false;
    previous_separator = separator;
  }
  return true;
}

[[nodiscard]] std::string_view leaf_name(const std::string_view name) noexcept {
  const auto separator = name.rfind('.');
  return separator == std::string_view::npos
      ? name
      : name.substr(separator + 1U);
}

}  // namespace

SystemVerilogDpiScopeRegistry::SystemVerilogDpiScopeRegistry(
    const std::uint64_t simulation_identity)
    : simulation_identity_(simulation_identity) {}

SystemVerilogDpiScopeHandle SystemVerilogDpiScopeRegistry::handle(
    const std::size_t slot) const noexcept {
  return {simulation_identity_, static_cast<std::uint32_t>(slot),
      scopes_[slot].epoch};
}

const SystemVerilogDpiScopeRegistry::Scope*
SystemVerilogDpiScopeRegistry::scope(
    const SystemVerilogDpiScopeHandle handle) const noexcept {
  if (simulation_identity_ == 0 || handle.simulation != simulation_identity_
      || handle.slot >= scopes_.size()) return nullptr;
  const auto& candidate = scopes_[handle.slot];
  return candidate.epoch == handle.epoch ? &candidate : nullptr;
}

SystemVerilogDpiScopeResult SystemVerilogDpiScopeRegistry::define(
    std::string full_name,
    const std::optional<SystemVerilogDpiScopeHandle> parent) {
  if (simulation_identity_ == 0) {
    return {{}, SystemVerilogDpiScopeError::InvalidSimulation};
  }
  if (!valid_scope_name(full_name)) {
    return {{}, SystemVerilogDpiScopeError::InvalidName};
  }
  if (scopes_by_name_.contains(full_name)) {
    return {{}, SystemVerilogDpiScopeError::DuplicateName};
  }
  std::optional<std::uint32_t> parent_slot;
  if (parent) {
    const auto* parent_scope = scope(*parent);
    if (!parent_scope
        || full_name != parent_scope->full_name + "."
            + std::string{leaf_name(full_name)}) {
      return {{}, SystemVerilogDpiScopeError::InvalidParent};
    }
    parent_slot = parent->slot;
  } else if (full_name.find('.') != std::string::npos) {
    return {{}, SystemVerilogDpiScopeError::InvalidParent};
  }
  if (scopes_.size() >= std::numeric_limits<std::uint32_t>::max()) {
    return {{}, SystemVerilogDpiScopeError::InvalidSimulation};
  }
  const auto slot = static_cast<std::uint32_t>(scopes_.size());
  scopes_.push_back(Scope{1, std::move(full_name), parent_slot});
  scopes_by_name_.emplace(scopes_.back().full_name, slot);
  return {handle(slot), {}};
}

bool SystemVerilogDpiScopeRegistry::contains(
    const SystemVerilogDpiScopeHandle handle) const noexcept {
  return scope(handle) != nullptr;
}

std::optional<SystemVerilogDpiScopeHandle>
SystemVerilogDpiScopeRegistry::find(const std::string_view full_name) const {
  const auto found = scopes_by_name_.find(std::string{full_name});
  if (found == scopes_by_name_.end()) return std::nullopt;
  return handle(found->second);
}

std::optional<std::string> SystemVerilogDpiScopeRegistry::name(
    const SystemVerilogDpiScopeHandle handle) const {
  const auto* found = scope(handle);
  return found ? std::optional<std::string>{found->full_name} : std::nullopt;
}

std::optional<SystemVerilogDpiScopeHandle>
SystemVerilogDpiScopeRegistry::parent(
    const SystemVerilogDpiScopeHandle handle) const noexcept {
  const auto* found = scope(handle);
  if (!found || !found->parent) return std::nullopt;
  return this->handle(*found->parent);
}

std::uint64_t SystemVerilogDpiScopeRegistry::simulation_identity()
    const noexcept {
  return simulation_identity_;
}

SystemVerilogDpiScopeContext::SystemVerilogDpiScopeContext(
    const SystemVerilogDpiScopeRegistry& registry) noexcept
    : registry_(&registry) {}

std::optional<SystemVerilogDpiScopeHandle>
SystemVerilogDpiScopeContext::current() const noexcept {
  return current_;
}

SystemVerilogDpiScopeSetResult SystemVerilogDpiScopeContext::set(
    const std::optional<SystemVerilogDpiScopeHandle> scope) noexcept {
  if (scope && !registry_->contains(*scope)) {
    return {current_, SystemVerilogDpiScopeError::StaleScope};
  }
  const auto previous = current_;
  current_ = scope;
  return {previous, {}};
}

}  // namespace fsim::runtime

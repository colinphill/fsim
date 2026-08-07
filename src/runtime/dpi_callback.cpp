// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_callback.hpp"

#include <exception>
#include <utility>

namespace fsim::runtime {

SystemVerilogDpiCallbackContext::SystemVerilogDpiCallbackContext(
    const SystemVerilogDpiScopeRegistry& scopes) noexcept
    : scope_context_(scopes) {}

std::optional<SystemVerilogDpiScopeHandle>
SystemVerilogDpiCallbackContext::current_scope() const noexcept {
  return scope_context_.current();
}

bool SystemVerilogDpiCallbackContext::is_disabled_state() const noexcept {
  return disabled_;
}

bool SystemVerilogDpiCallbackContext::acknowledge_disabled_state() noexcept {
  if (!disabled_) return false;
  disable_acknowledged_ = true;
  return true;
}

void SystemVerilogDpiCallbackContext::request_disable() noexcept {
  disabled_ = true;
  disable_acknowledged_ = false;
}

SystemVerilogDpiScopeSetResult SystemVerilogDpiCallbackContext::set_scope(
    const std::optional<SystemVerilogDpiScopeHandle> scope) noexcept {
  return scope_context_.set(scope);
}

void SystemVerilogDpiCallbackContext::begin_dispatch() noexcept {
  disable_acknowledged_ = false;
}

bool SystemVerilogDpiCallbackContext::finish_dispatch() noexcept {
  if (!disabled_) return true;
  if (!disable_acknowledged_) return false;
  disabled_ = false;
  disable_acknowledged_ = false;
  return true;
}

SystemVerilogDpiCallbackFrame::SystemVerilogDpiCallbackFrame(
    std::vector<SystemVerilogDpiTransferMode> directions,
    std::vector<std::vector<PackedLogic4>> values,
    SystemVerilogDpiCallbackContext& context)
    : directions_(std::move(directions)),
      values_(std::move(values)),
      context_(&context) {}

std::size_t SystemVerilogDpiCallbackFrame::size() const noexcept {
  return values_.size();
}

const std::vector<PackedLogic4>* SystemVerilogDpiCallbackFrame::read(
    const std::size_t argument) const noexcept {
  return argument < values_.size() ? &values_[argument] : nullptr;
}

std::vector<PackedLogic4>* SystemVerilogDpiCallbackFrame::writable(
    const std::size_t argument) noexcept {
  if (argument >= values_.size()) {
    access_error_ = SystemVerilogDpiCallbackError::ArityMismatch;
    return nullptr;
  }
  if (directions_[argument] == SystemVerilogDpiTransferMode::Input) {
    access_error_ = SystemVerilogDpiCallbackError::DirectionMismatch;
    return nullptr;
  }
  return &values_[argument];
}

std::optional<SystemVerilogDpiScopeHandle>
SystemVerilogDpiCallbackFrame::current_scope() const noexcept {
  return context_->current_scope();
}

bool SystemVerilogDpiCallbackFrame::is_disabled_state() const noexcept {
  return context_->is_disabled_state();
}

bool SystemVerilogDpiCallbackFrame::acknowledge_disabled_state() noexcept {
  return context_->acknowledge_disabled_state();
}

SystemVerilogDpiCallbackRegistry::SystemVerilogDpiCallbackRegistry(
    const SystemVerilogDpiScopeRegistry& scopes) noexcept
    : scopes_(&scopes) {}

SystemVerilogDpiCallbackError
SystemVerilogDpiCallbackRegistry::register_callback(
    std::string linkage_name,
    const SystemVerilogDpiScopeHandle scope,
    std::vector<SystemVerilogDpiTransferMode> directions,
    SystemVerilogDpiExportedCallback callback) {
  if (linkage_name.empty() || !callback) {
    return SystemVerilogDpiCallbackError::InvalidName;
  }
  if (!scopes_->contains(scope)) {
    return SystemVerilogDpiCallbackError::InvalidScope;
  }
  if (callbacks_.contains(linkage_name)) {
    return SystemVerilogDpiCallbackError::DuplicateName;
  }
  callbacks_.emplace(std::move(linkage_name),
      Entry{scope, std::move(directions), std::move(callback)});
  return {};
}

SystemVerilogDpiCallbackResult SystemVerilogDpiCallbackRegistry::dispatch(
    const std::string_view linkage_name,
    std::vector<std::vector<PackedLogic4>> arguments,
    SystemVerilogDpiCallbackContext& context) const {
  const auto found = callbacks_.find(std::string{linkage_name});
  if (found == callbacks_.end()) {
    return {{}, SystemVerilogDpiCallbackError::UnknownName, {}};
  }
  const auto& entry = found->second;
  if (arguments.size() != entry.directions.size()) {
    return {{}, SystemVerilogDpiCallbackError::ArityMismatch, {}};
  }
  context.begin_dispatch();
  const auto changed = context.set_scope(entry.scope);
  if (!changed) {
    return {{}, SystemVerilogDpiCallbackError::InvalidScope, {}};
  }
  SystemVerilogDpiCallbackFrame frame{
      entry.directions, std::move(arguments), context};
  SystemVerilogDpiCallbackError error{};
  std::string message;
  try {
    entry.callback(frame);
    error = frame.access_error_;
  } catch (const std::exception& exception) {
    error = SystemVerilogDpiCallbackError::Exception;
    message = exception.what();
  } catch (...) {
    error = SystemVerilogDpiCallbackError::Exception;
    message = "non-standard exception";
  }
  const auto restored = context.set_scope(changed.previous);
  if (!restored) {
    return {{}, SystemVerilogDpiCallbackError::InvalidScope, {}};
  }
  if (!context.finish_dispatch() && error == SystemVerilogDpiCallbackError::None) {
    error = SystemVerilogDpiCallbackError::Disabled;
  }
  if (error != SystemVerilogDpiCallbackError::None) {
    return {{}, error, std::move(message)};
  }
  return {std::move(frame.values_), {}, {}};
}

}  // namespace fsim::runtime

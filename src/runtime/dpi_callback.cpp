// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_callback.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace fsim::runtime {

SystemVerilogDpiCallbackContext::SystemVerilogDpiCallbackContext(
    const SystemVerilogDpiScopeRegistry& scopes) noexcept
    : scopes_(&scopes), scope_context_(scopes),
      foreign_context_{
          FSIM_SVDPI_CONTEXT_ABI_VERSION,
          sizeof(fsim_svdpi_call_context_v3),
          this,
          &bridge_get_scope,
          &bridge_set_scope,
          &bridge_scope_name,
          &bridge_find_scope,
          &bridge_put_user_data,
          &bridge_get_user_data,
          &bridge_caller_info,
          &bridge_disabled,
          &bridge_ack_disabled,
          &bridge_time,
          &bridge_time_unit,
          &bridge_time_precision} {}

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

void SystemVerilogDpiCallbackContext::set_call_site(
    std::string file_name, const int line_number) {
  caller_file_name_ = std::move(file_name);
  caller_line_number_ = line_number;
}

void SystemVerilogDpiCallbackContext::set_simulation_time(
    const std::uint64_t ticks, const std::int32_t time_unit,
    const std::int32_t time_precision) noexcept {
  simulation_ticks_ = ticks;
  time_unit_ = time_unit;
  time_precision_ = time_precision;
}

SystemVerilogDpiCallbackContext::ScopeToken*
SystemVerilogDpiCallbackContext::token(
    const SystemVerilogDpiScopeHandle handle) noexcept {
  const auto found = std::ranges::find_if(
      scope_tokens_, [&](const auto& candidate) {
        return candidate->handle == handle;
      });
  if (found != scope_tokens_.end()) return found->get();
  const auto name = scopes_->name(handle);
  if (!name) return nullptr;
  try {
    scope_tokens_.push_back(
        std::make_unique<ScopeToken>(ScopeToken{handle, *name}));
    return scope_tokens_.back().get();
  } catch (...) {
    return nullptr;
  }
}

SystemVerilogDpiCallbackContext::ScopeToken*
SystemVerilogDpiCallbackContext::token(const svScope scope) noexcept {
  const auto found = std::ranges::find_if(
      scope_tokens_, [&](const auto& candidate) {
        return candidate.get() == scope;
      });
  return found == scope_tokens_.end() ? nullptr : found->get();
}

svScope FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_get_scope(
    void* const user_data) noexcept {
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  const auto current = self.current_scope();
  return current ? self.token(*current) : nullptr;
}

svScope FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_set_scope(
    void* const user_data, const svScope scope) noexcept {
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  const auto previous = self.current_scope();
  std::optional<SystemVerilogDpiScopeHandle> next;
  if (scope != nullptr) {
    const auto* const selected = self.token(scope);
    if (selected == nullptr) return nullptr;
    next = selected->handle;
  }
  if (!self.set_scope(next)) return nullptr;
  return previous ? self.token(*previous) : nullptr;
}

const char* FSIM_SVDPI_CALL
SystemVerilogDpiCallbackContext::bridge_scope_name(
    void* const user_data, const svScope scope) noexcept {
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  const auto* const selected = self.token(scope);
  return selected == nullptr ? nullptr : selected->name.c_str();
}

svScope FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_find_scope(
    void* const user_data, const char* const name) noexcept {
  if (name == nullptr) return nullptr;
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  try {
    const auto found = self.scopes_->find(name);
    return found ? self.token(*found) : nullptr;
  } catch (...) {
    return nullptr;
  }
}

int FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_put_user_data(
    void* const user_data, const svScope scope, void* const key,
    void* const value) noexcept {
  if (scope == nullptr || key == nullptr || value == nullptr) return -1;
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  auto* const selected = self.token(scope);
  if (selected == nullptr) return -1;
  const auto found = std::ranges::find_if(
      self.user_data_, [&](const UserDataEntry& entry) {
        return entry.scope == selected && entry.key == key;
      });
  if (found != self.user_data_.end()) {
    found->value = value;
    return 0;
  }
  try {
    self.user_data_.push_back({selected, key, value});
    return 0;
  } catch (...) {
    return -1;
  }
}

void* FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_get_user_data(
    void* const user_data, const svScope scope, void* const key) noexcept {
  if (scope == nullptr || key == nullptr) return nullptr;
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  auto* const selected = self.token(scope);
  if (selected == nullptr) return nullptr;
  const auto found = std::ranges::find_if(
      self.user_data_, [&](const UserDataEntry& entry) {
        return entry.scope == selected && entry.key == key;
      });
  return found == self.user_data_.end() ? nullptr : found->value;
}

int FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_caller_info(
    void* const user_data, const char** const file_name,
    int* const line_number) noexcept {
  const auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  if (file_name == nullptr || line_number == nullptr
      || self.caller_file_name_.empty() || self.caller_line_number_ <= 0) {
    return 0;
  }
  *file_name = self.caller_file_name_.c_str();
  *line_number = self.caller_line_number_;
  return 1;
}

int FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_disabled(
    void* const user_data) noexcept {
  const auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  return self.is_disabled_state() ? 1 : 0;
}

void FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_ack_disabled(
    void* const user_data) noexcept {
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  static_cast<void>(self.acknowledge_disabled_state());
}

int FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_time(
    void* const user_data, const svScope scope,
    svTimeVal* const time_value) noexcept {
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  if (time_value == nullptr || (scope != nullptr && self.token(scope) == nullptr)) {
    return -1;
  }
  if (time_value->type == sv_sim_time) {
    time_value->high = static_cast<std::uint32_t>(self.simulation_ticks_ >> 32U);
    time_value->low = static_cast<std::uint32_t>(self.simulation_ticks_);
    time_value->real = 0.0;
    return 0;
  }
  if (time_value->type == sv_scaled_real_time) {
    const auto scale = static_cast<int>(self.time_precision_ - self.time_unit_);
    time_value->high = 0U;
    time_value->low = 0U;
    time_value->real = static_cast<double>(self.simulation_ticks_)
        * std::pow(10.0, static_cast<double>(scale));
    return std::isfinite(time_value->real) ? 0 : -1;
  }
  return -1;
}

int FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_time_unit(
    void* const user_data, const svScope scope,
    std::int32_t* const exponent) noexcept {
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  if (exponent == nullptr || (scope != nullptr && self.token(scope) == nullptr)) {
    return -1;
  }
  *exponent = self.time_unit_;
  return 0;
}

int FSIM_SVDPI_CALL SystemVerilogDpiCallbackContext::bridge_time_precision(
    void* const user_data, const svScope scope,
    std::int32_t* const exponent) noexcept {
  auto& self = *static_cast<SystemVerilogDpiCallbackContext*>(user_data);
  if (exponent == nullptr || (scope != nullptr && self.token(scope) == nullptr)) {
    return -1;
  }
  *exponent = self.time_precision_;
  return 0;
}

bool SystemVerilogDpiCallbackContext::enter_foreign_call() noexcept {
  return fsim_svdpi_call_context_enter_v3(&foreign_context_) == 0;
}

bool SystemVerilogDpiCallbackContext::leave_foreign_call() noexcept {
  return fsim_svdpi_call_context_leave_v3(&foreign_context_) == 0;
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

SystemVerilogDpiCallbackError
SystemVerilogDpiCallbackRegistry::register_callback(
    SystemVerilogDpiRuntimeDeclaration declaration,
    const SystemVerilogDpiScopeHandle scope,
    SystemVerilogDpiExportedCallback callback) {
  if (declaration.qualifier != SystemVerilogDpiRuntimeQualifier::None) {
    return SystemVerilogDpiCallbackError::DeclarationMismatch;
  }
  return register_callback(
      std::move(declaration.linkage_name), scope,
      std::move(declaration.directions), std::move(callback));
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
  bool foreign_call_entered{};
  try {
    foreign_call_entered = context.enter_foreign_call();
    if (!foreign_call_entered) {
      error = SystemVerilogDpiCallbackError::Exception;
      message = "DPI context stack rejected callback entry";
    } else {
      entry.callback(frame);
      foreign_call_entered = false;
      if (!context.leave_foreign_call()) {
        error = SystemVerilogDpiCallbackError::Exception;
        message = "DPI context stack rejected callback exit";
      } else {
        error = frame.access_error_;
      }
    }
  } catch (const std::exception& exception) {
    if (foreign_call_entered) {
      static_cast<void>(context.leave_foreign_call());
    }
    error = SystemVerilogDpiCallbackError::Exception;
    message = exception.what();
  } catch (...) {
    if (foreign_call_entered) {
      static_cast<void>(context.leave_foreign_call());
    }
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

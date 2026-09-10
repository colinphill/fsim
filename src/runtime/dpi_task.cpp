// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_task.hpp"

#include <exception>
#include <limits>
#include <utility>

namespace fsim::runtime {

SystemVerilogDpiImportedTaskRegistry::SystemVerilogDpiImportedTaskRegistry(
    Scheduler& scheduler,
    const SystemVerilogDpiScopeRegistry& scopes) noexcept
    : scheduler_(&scheduler), scopes_(&scopes) {}

SystemVerilogDpiImportedTaskRegistry::~SystemVerilogDpiImportedTaskRegistry() {
  for (const auto& invocation : invocations_) {
    scheduler_->cancel(invocation.pending);
  }
}

SystemVerilogDpiTaskError
SystemVerilogDpiImportedTaskRegistry::register_task(
    std::string linkage_name,
    const SystemVerilogDpiScopeHandle scope,
    std::vector<SystemVerilogDpiTransferMode> directions,
    SystemVerilogDpiImportedTask task) {
  if (linkage_name.empty() || !task) {
    return SystemVerilogDpiTaskError::InvalidName;
  }
  if (!scopes_->contains(scope)) {
    return SystemVerilogDpiTaskError::InvalidScope;
  }
  if (tasks_.contains(linkage_name)) {
    return SystemVerilogDpiTaskError::DuplicateName;
  }
  tasks_.emplace(std::move(linkage_name),
      Entry{scope, std::move(directions), std::move(task)});
  return {};
}

SystemVerilogDpiTaskError
SystemVerilogDpiImportedTaskRegistry::register_task(
    SystemVerilogDpiRuntimeDeclaration declaration,
    const SystemVerilogDpiScopeHandle scope,
    SystemVerilogDpiImportedTask task) {
  if (declaration.callable_kind
          != SystemVerilogDpiRuntimeCallableKind::Task
      || declaration.qualifier == SystemVerilogDpiRuntimeQualifier::Pure) {
    return SystemVerilogDpiTaskError::DeclarationMismatch;
  }
  return register_task(
      std::move(declaration.linkage_name), scope,
      std::move(declaration.directions), std::move(task));
}

SystemVerilogDpiTaskHandle SystemVerilogDpiImportedTaskRegistry::handle(
    const std::size_t slot) const noexcept {
  return {scopes_->simulation_identity(), static_cast<std::uint32_t>(slot),
      invocations_[slot].epoch};
}

const SystemVerilogDpiImportedTaskRegistry::Invocation*
SystemVerilogDpiImportedTaskRegistry::invocation(
    const SystemVerilogDpiTaskHandle handle) const noexcept {
  if (handle.simulation != scopes_->simulation_identity()
      || handle.slot >= invocations_.size()) return nullptr;
  const auto& candidate = invocations_[handle.slot];
  return candidate.epoch == handle.epoch ? &candidate : nullptr;
}

SystemVerilogDpiImportedTaskRegistry::Invocation*
SystemVerilogDpiImportedTaskRegistry::invocation(
    const SystemVerilogDpiTaskHandle handle) noexcept {
  return const_cast<Invocation*>(
      static_cast<const SystemVerilogDpiImportedTaskRegistry*>(this)
          ->invocation(handle));
}

void SystemVerilogDpiImportedTaskRegistry::fail(
    Invocation& invocation,
    const SystemVerilogDpiTaskError error,
    std::string message) noexcept {
  invocation.status = error == SystemVerilogDpiTaskError::Disabled
      ? SystemVerilogDpiTaskStatus::Disabled
      : SystemVerilogDpiTaskStatus::Failed;
  invocation.error = error;
  invocation.message = std::move(message);
  invocation.values.clear();
  invocation.pending = {};
}

void SystemVerilogDpiImportedTaskRegistry::schedule(
    const std::size_t slot,
    const SimulationTick delay) noexcept {
  auto& invocation = invocations_[slot];
  try {
    invocation.pending = scheduler_->schedule_after_cancelable(
        delay, SchedulerPhase::active, static_cast<StableOrder>(slot),
        [this, slot](Scheduler&) { resume(slot); });
  } catch (const std::exception& exception) {
    fail(invocation, SystemVerilogDpiTaskError::SchedulerFailure,
        exception.what());
  } catch (...) {
    fail(invocation, SystemVerilogDpiTaskError::SchedulerFailure,
        "non-standard scheduler exception");
  }
}

SystemVerilogDpiTaskStartResult SystemVerilogDpiImportedTaskRegistry::start(
    const std::string_view linkage_name,
    std::vector<std::vector<PackedLogic4>> arguments,
    SystemVerilogDpiCallbackContext& context) {
  const auto found = tasks_.find(std::string{linkage_name});
  if (found == tasks_.end()) {
    return {{}, SystemVerilogDpiTaskError::UnknownName};
  }
  if (arguments.size() != found->second.directions.size()) {
    return {{}, SystemVerilogDpiTaskError::ArityMismatch};
  }
  if (invocations_.size() >= std::numeric_limits<std::uint32_t>::max()) {
    return {{}, SystemVerilogDpiTaskError::SchedulerFailure};
  }
  invocations_.push_back(Invocation{
      1, found->second, SystemVerilogDpiTaskStatus::Pending,
      std::move(arguments), {}, {}, &context, {}, 0});
  const auto slot = invocations_.size() - 1U;
  schedule(slot, 0);
  if (invocations_[slot].status == SystemVerilogDpiTaskStatus::Failed) {
    return {{}, invocations_[slot].error};
  }
  return {handle(slot), {}};
}

void SystemVerilogDpiImportedTaskRegistry::resume(
    const std::size_t slot) noexcept {
  if (slot >= invocations_.size()) return;
  auto& invocation = invocations_[slot];
  if (invocation.status != SystemVerilogDpiTaskStatus::Pending
      && invocation.status != SystemVerilogDpiTaskStatus::Suspended) return;
  invocation.pending = {};
  invocation.context->begin_dispatch();
  const auto changed = invocation.context->set_scope(invocation.entry.scope);
  if (!changed) {
    fail(invocation, SystemVerilogDpiTaskError::InvalidScope);
    return;
  }
  SystemVerilogDpiCallbackFrame frame{
      invocation.entry.directions, invocation.values, *invocation.context};
  SystemVerilogDpiTaskAction action;
  bool foreign_call_entered{};
  try {
    foreign_call_entered = invocation.context->enter_foreign_call();
    if (!foreign_call_entered) {
      static_cast<void>(invocation.context->set_scope(changed.previous));
      static_cast<void>(invocation.context->finish_dispatch());
      fail(invocation, SystemVerilogDpiTaskError::Exception,
          "DPI context stack rejected task entry");
      return;
    }
    action = invocation.entry.task(frame, invocation.resume_count);
    foreign_call_entered = false;
    if (!invocation.context->leave_foreign_call()) {
      static_cast<void>(invocation.context->set_scope(changed.previous));
      static_cast<void>(invocation.context->finish_dispatch());
      fail(invocation, SystemVerilogDpiTaskError::Exception,
          "DPI context stack rejected task exit");
      return;
    }
  } catch (const std::exception& exception) {
    if (foreign_call_entered) {
      static_cast<void>(invocation.context->leave_foreign_call());
    }
    static_cast<void>(invocation.context->set_scope(changed.previous));
    static_cast<void>(invocation.context->finish_dispatch());
    fail(invocation, SystemVerilogDpiTaskError::Exception, exception.what());
    return;
  } catch (...) {
    if (foreign_call_entered) {
      static_cast<void>(invocation.context->leave_foreign_call());
    }
    static_cast<void>(invocation.context->set_scope(changed.previous));
    static_cast<void>(invocation.context->finish_dispatch());
    fail(invocation, SystemVerilogDpiTaskError::Exception,
        "non-standard task exception");
    return;
  }
  const auto restored = invocation.context->set_scope(changed.previous);
  if (!restored) {
    fail(invocation, SystemVerilogDpiTaskError::InvalidScope);
    return;
  }
  if (!invocation.context->finish_dispatch()) {
    fail(invocation, SystemVerilogDpiTaskError::Disabled);
    return;
  }
  if (frame.access_error_ != SystemVerilogDpiCallbackError::None) {
    fail(invocation,
        frame.access_error_ == SystemVerilogDpiCallbackError::ArityMismatch
            ? SystemVerilogDpiTaskError::ArityMismatch
            : SystemVerilogDpiTaskError::DirectionMismatch);
    return;
  }
  invocation.values = std::move(frame.values_);
  if (action.kind == SystemVerilogDpiTaskActionKind::Complete) {
    invocation.status = SystemVerilogDpiTaskStatus::Completed;
    invocation.error = {};
    return;
  }
  invocation.status = SystemVerilogDpiTaskStatus::Suspended;
  ++invocation.resume_count;
  schedule(slot, action.delay);
}

std::optional<SystemVerilogDpiTaskResult>
SystemVerilogDpiImportedTaskRegistry::result(
    const SystemVerilogDpiTaskHandle handle) const {
  const auto* found = invocation(handle);
  if (!found) return std::nullopt;
  return SystemVerilogDpiTaskResult{
      found->status,
      found->status == SystemVerilogDpiTaskStatus::Completed
          ? found->values
          : std::vector<std::vector<PackedLogic4>>{},
      found->error,
      found->message};
}

bool SystemVerilogDpiImportedTaskRegistry::cancel(
    const SystemVerilogDpiTaskHandle handle) noexcept {
  auto* found = invocation(handle);
  if (!found || (found->status != SystemVerilogDpiTaskStatus::Pending
      && found->status != SystemVerilogDpiTaskStatus::Suspended)) return false;
  scheduler_->cancel(found->pending);
  found->pending = {};
  found->status = SystemVerilogDpiTaskStatus::Cancelled;
  found->values.clear();
  return true;
}

}  // namespace fsim::runtime

// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application_tf_scheduler.hpp"

#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <utility>

namespace fsim::app {
namespace {

std::atomic<std::uint64_t> next_tf_scheduler_owner{1};

[[nodiscard]] TfSchedulerBindResult bind_failure(
    const TfSchedulerError error,
    const TfApplicationError application_error = TfApplicationError::None,
    const runtime::TfCallError call_error = runtime::TfCallError::None) {
  return {.value = {},
          .error = error,
          .application_error = application_error,
          .call_error = call_error,
          .lifecycle_effects = {}};
}

[[nodiscard]] TfSchedulerInvokeResult invoke_failure(
    const TfSchedulerError error,
    const runtime::TfCallError call_error = runtime::TfCallError::None) {
  return {.error = error,
          .call_error = call_error,
          .callback_value = 0,
          .function_result = std::nullopt,
          .argument_updates = 0,
          .control_effects = 0,
          .callbacks_scheduled = 0};
}

[[nodiscard]] runtime::SchedulerPhase callback_phase(
    const TfSchedulerCallbackKind kind) noexcept {
  switch (kind) {
    case TfSchedulerCallbackKind::Call:
    case TfSchedulerCallbackKind::Reactivate:
      return runtime::SchedulerPhase::active;
    case TfSchedulerCallbackKind::ReadWriteSynchronize:
      return runtime::SchedulerPhase::reactive;
    case TfSchedulerCallbackKind::ReadOnlySynchronize:
      return runtime::SchedulerPhase::postponed;
  }
  return runtime::SchedulerPhase::active;
}

}  // namespace

struct TfSchedulerCoordinator::Impl {
  struct CallRecord {
    std::shared_ptr<runtime::TfBoundCall> callable;
    std::vector<runtime::TfArgumentValue> values;
    runtime::TfCallError last_call_error{runtime::TfCallError::None};
    std::uint64_t invocations{};
    std::uint64_t callback_failures{};
    std::uint32_t pending_callbacks{};
    std::optional<runtime::TfControlEffectKind> terminal_effect;
  };

  runtime::Scheduler* scheduler{};
  runtime::StableOrder stable_order_base{};
  std::uint64_t owner{};
  std::uint64_t next_call{1};
  std::uint64_t next_callback{1};
  std::uint64_t next_stable_order{};
  TfSchedulerPublishHook publish;
  mutable std::recursive_mutex mutex;
  bool dispatching{};
  std::map<std::uint64_t, CallRecord> calls;
  std::map<std::uint64_t, runtime::ScheduledTaskHandle> pending;
};

namespace {

[[nodiscard]] TfSchedulerError validate_handle(
    const TfSchedulerCoordinator::Impl& state,
    const TfSchedulerCallHandle handle) noexcept {
  if (!handle) return TfSchedulerError::InvalidHandle;
  if (handle.owner != state.owner) return TfSchedulerError::CrossCoordinator;
  if (!state.calls.contains(handle.id)) return TfSchedulerError::InvalidHandle;
  return TfSchedulerError::None;
}

[[nodiscard]] runtime::TfTimeState current_time(
    const TfSchedulerCoordinator::Impl& state) noexcept {
  runtime::TfTimeState result;
  result.scheduler_ticks = state.scheduler->now();
  if (const auto next = state.scheduler->next_pending_time()) {
    result.has_next_event = true;
    result.next_event_ticks = *next;
  }
  return result;
}

void execute_scheduled(
    const std::weak_ptr<TfSchedulerCoordinator::Impl>& weak,
    std::uint64_t callback_id,
    TfSchedulerCallHandle call,
    TfSchedulerCallbackKind kind);

struct ScheduledRollback {
  TfSchedulerCoordinator::Impl& state;
  std::vector<std::uint64_t> ids;
  bool committed{};

  ~ScheduledRollback() {
    if (committed) return;
    for (const auto id : ids) {
      const auto found = state.pending.find(id);
      if (found == state.pending.end()) continue;
      state.scheduler->cancel(found->second);
      state.pending.erase(found);
    }
  }
};

[[nodiscard]] TfSchedulerInvokeResult publish_result(
    const std::shared_ptr<TfSchedulerCoordinator::Impl>& state,
    TfSchedulerCallHandle call,
    TfSchedulerCallbackKind kind,
    runtime::TfInvokeResult invoked,
    std::span<const runtime::TfArgumentValue> input_values) {
  auto& record = state->calls.at(call.id);
  if (!invoked) {
    record.last_call_error = invoked.error;
    ++record.callback_failures;
    return invoke_failure(TfSchedulerError::Invocation, invoked.error);
  }

  std::vector<runtime::TfArgumentValue> staged_values;
  try {
    staged_values.assign(input_values.begin(), input_values.end());
    for (const auto& update : invoked.argument_updates) {
      if (update.parameter <= 0 ||
          static_cast<std::size_t>(update.parameter) > staged_values.size()) {
        record.last_call_error = runtime::TfCallError::InvalidValues;
        ++record.callback_failures;
        return invoke_failure(TfSchedulerError::Invocation,
                              runtime::TfCallError::InvalidValues);
      }
      staged_values[static_cast<std::size_t>(update.parameter) - 1U] =
          update.value;
    }
  } catch (const std::bad_alloc&) {
    return invoke_failure(TfSchedulerError::Allocation);
  } catch (...) {
    return invoke_failure(TfSchedulerError::Allocation);
  }

  const auto request_count = invoked.delay_requests.size() +
      invoked.synchronization_requests.size();
  if (request_count > kMaxTfSchedulerCallbacks - state->pending.size()) {
    return invoke_failure(TfSchedulerError::ResourceLimit);
  }
  if (request_count != 0U &&
      (state->next_callback >
           std::numeric_limits<std::uint64_t>::max() - request_count ||
       state->next_stable_order >
           std::numeric_limits<runtime::StableOrder>::max() - request_count ||
       state->stable_order_base >
           std::numeric_limits<runtime::StableOrder>::max() -
               state->next_stable_order - request_count)) {
    return invoke_failure(TfSchedulerError::Overflow);
  }

  ScheduledRollback rollback{.state = *state,
                             .ids = {},
                             .committed = false};
  try {
    rollback.ids.reserve(request_count);
    const auto schedule = [&](const runtime::SimulationTick delay,
                              const TfSchedulerCallbackKind callback) {
      const auto id = state->next_callback++;
      const auto order = state->stable_order_base + state->next_stable_order++;
      const std::weak_ptr<TfSchedulerCoordinator::Impl> weak = state;
      auto handle = state->scheduler->schedule_after_cancelable(
          delay, callback_phase(callback), order,
          [weak, id, call, callback](runtime::Scheduler&) {
            execute_scheduled(weak, id, call, callback);
          });
      try {
        state->pending.emplace(id, handle);
      } catch (...) {
        state->scheduler->cancel(handle);
        throw;
      }
      rollback.ids.push_back(id);
    };
    for (const auto& delay : invoked.delay_requests) {
      schedule(delay.scheduler_ticks, TfSchedulerCallbackKind::Reactivate);
    }
    for (const auto& request : invoked.synchronization_requests) {
      if (request.instance != record.callable->instance_identity()) {
        return invoke_failure(TfSchedulerError::Invocation,
                              runtime::TfCallError::InvalidInstance);
      }
      schedule(0, request.kind == runtime::TfSynchronizationKind::ReadWrite
                      ? TfSchedulerCallbackKind::ReadWriteSynchronize
                      : TfSchedulerCallbackKind::ReadOnlySynchronize);
    }
  } catch (const std::bad_alloc&) {
    return invoke_failure(TfSchedulerError::Allocation);
  } catch (...) {
    return invoke_failure(TfSchedulerError::Scheduling);
  }

  const auto phase = state->scheduler->current_phase();
  if (!phase.has_value()) {
    return invoke_failure(TfSchedulerError::InactiveScheduler);
  }
  const TfSchedulerPublication publication{
      .call = call,
      .callback = kind,
      .instance = record.callable->instance_identity(),
      .time = state->scheduler->now(),
      .delta = state->scheduler->delta(),
      .phase = *phase,
      .argument_updates = invoked.argument_updates,
      .control_effects = invoked.control_effects,
      .delay_requests = invoked.delay_requests,
      .synchronization_requests = invoked.synchronization_requests,
  };
  try {
    if (state->publish && !state->publish(publication)) {
      ++record.callback_failures;
      return invoke_failure(TfSchedulerError::Publication);
    }
  } catch (...) {
    ++record.callback_failures;
    return invoke_failure(TfSchedulerError::Publication);
  }

  record.values = std::move(staged_values);
  record.pending_callbacks += static_cast<std::uint32_t>(request_count);
  record.last_call_error = runtime::TfCallError::None;
  ++record.invocations;
  for (const auto& effect : invoked.control_effects) {
    if (effect.kind == runtime::TfControlEffectKind::Finish ||
        effect.kind == runtime::TfControlEffectKind::Stop) {
      record.terminal_effect = effect.kind;
    }
  }
  rollback.committed = true;
  if (record.terminal_effect.has_value()) {
    state->scheduler->request_stop();
  }
  return {.error = TfSchedulerError::None,
          .call_error = runtime::TfCallError::None,
          .callback_value = invoked.callback_value,
          .function_result = std::move(invoked.function_result),
          .argument_updates =
              static_cast<std::uint32_t>(invoked.argument_updates.size()),
          .control_effects =
              static_cast<std::uint32_t>(invoked.control_effects.size()),
          .callbacks_scheduled = static_cast<std::uint32_t>(request_count)};
}

void execute_scheduled(
    const std::weak_ptr<TfSchedulerCoordinator::Impl>& weak,
    const std::uint64_t callback_id,
    const TfSchedulerCallHandle call,
    const TfSchedulerCallbackKind kind) {
  const auto state = weak.lock();
  if (!state) return;
  std::lock_guard lock{state->mutex};
  state->pending.erase(callback_id);
  const auto found = state->calls.find(call.id);
  if (found == state->calls.end()) return;
  auto& record = found->second;
  if (record.pending_callbacks != 0U) --record.pending_callbacks;
  if (state->dispatching) {
    ++record.callback_failures;
    record.last_call_error = runtime::TfCallError::ContextBusy;
    return;
  }
  struct DispatchGuard {
    bool& value;
    ~DispatchGuard() { value = false; }
  } guard{state->dispatching};
  state->dispatching = true;
  const auto time = current_time(*state);
  auto invoked = kind == TfSchedulerCallbackKind::Reactivate
      ? record.callable->reactivate(record.values, time)
      : record.callable->synchronize(
            kind == TfSchedulerCallbackKind::ReadWriteSynchronize
                ? runtime::TfSynchronizationKind::ReadWrite
                : runtime::TfSynchronizationKind::ReadOnly,
            record.values, time);
  (void)publish_result(
      state, call, kind, std::move(invoked), record.values);
}

}  // namespace

TfSchedulerCoordinator::TfSchedulerCoordinator(
    runtime::Scheduler& scheduler,
    const runtime::StableOrder stable_order_base,
    TfSchedulerPublishHook publish)
    : impl_(std::make_shared<Impl>()) {
  impl_->scheduler = &scheduler;
  impl_->stable_order_base = stable_order_base;
  impl_->owner = next_tf_scheduler_owner.fetch_add(
      1, std::memory_order_relaxed);
  impl_->publish = std::move(publish);
}

TfSchedulerCoordinator::~TfSchedulerCoordinator() {
  if (!impl_) return;
  std::lock_guard lock{impl_->mutex};
  for (const auto& [id, handle] : impl_->pending) {
    (void)id;
    impl_->scheduler->cancel(handle);
  }
  impl_->pending.clear();
}

TfSchedulerBindResult TfSchedulerCoordinator::bind(
    TfApplicationRegistry& registry,
    const frontend::StandardRevision profile,
    const std::string_view name,
    const std::span<const runtime::TfArgument> arguments,
    const runtime::TfInstanceIdentity instance,
    const runtime::TfTimeProfile time_profile,
    const runtime::TfContextProfile& context_profile) noexcept {
  auto binding = registry.bind(
      profile, name, arguments, instance, time_profile, context_profile);
  if (!binding) {
    return bind_failure(TfSchedulerError::Binding, binding.error,
                        binding.binding.error);
  }
  auto result = adopt(std::move(binding.binding));
  if (result) {
    result.application_error = TfApplicationError::None;
  }
  return result;
}

TfSchedulerBindResult TfSchedulerCoordinator::adopt(
    runtime::TfBindResult binding) noexcept {
  if (!impl_ || impl_->owner == 0U || impl_->scheduler == nullptr) {
    return bind_failure(TfSchedulerError::InvalidCoordinator);
  }
  if (!binding) {
    return bind_failure(TfSchedulerError::Binding,
                        TfApplicationError::None, binding.error);
  }
  std::lock_guard lock{impl_->mutex};
  if (impl_->calls.size() >= kMaxTfSchedulerCalls ||
      impl_->next_call == 0U) {
    return bind_failure(TfSchedulerError::ResourceLimit);
  }
  try {
    const TfSchedulerCallHandle handle{impl_->owner, impl_->next_call++};
    auto callable = std::shared_ptr<runtime::TfBoundCall>{
        std::move(binding.value)};
    const auto [position, inserted] = impl_->calls.emplace(
        handle.id,
        Impl::CallRecord{
            .callable = std::move(callable),
            .values = {},
            .last_call_error = runtime::TfCallError::None,
            .invocations = 0,
            .callback_failures = 0,
            .pending_callbacks = 0,
            .terminal_effect = std::nullopt});
    (void)position;
    if (!inserted) return bind_failure(TfSchedulerError::ResourceLimit);
    return {.value = handle,
            .error = TfSchedulerError::None,
            .application_error = TfApplicationError::None,
            .call_error = runtime::TfCallError::None,
            .lifecycle_effects = std::move(binding.control_effects)};
  } catch (const std::bad_alloc&) {
    return bind_failure(TfSchedulerError::Allocation);
  } catch (...) {
    return bind_failure(TfSchedulerError::Allocation);
  }
}

TfSchedulerInvokeResult TfSchedulerCoordinator::invoke(
    const TfSchedulerCallHandle call,
    const std::span<const runtime::TfArgumentValue> values) noexcept {
  if (!impl_ || impl_->owner == 0U || impl_->scheduler == nullptr) {
    return invoke_failure(TfSchedulerError::InvalidCoordinator);
  }
  std::lock_guard lock{impl_->mutex};
  const auto handle_error = validate_handle(*impl_, call);
  if (handle_error != TfSchedulerError::None) {
    return invoke_failure(handle_error);
  }
  if (!impl_->scheduler->running() ||
      !impl_->scheduler->current_phase().has_value()) {
    return invoke_failure(TfSchedulerError::InactiveScheduler);
  }
  if (impl_->dispatching) {
    return invoke_failure(TfSchedulerError::Reentrant,
                          runtime::TfCallError::ContextBusy);
  }
  struct DispatchGuard {
    bool& value;
    ~DispatchGuard() { value = false; }
  } guard{impl_->dispatching};
  impl_->dispatching = true;
  auto& record = impl_->calls.at(call.id);
  auto invoked = record.callable->invoke(values, current_time(*impl_));
  return publish_result(
      impl_, call, TfSchedulerCallbackKind::Call,
      std::move(invoked), values);
}

TfSchedulerCallSnapshot TfSchedulerCoordinator::snapshot(
    const TfSchedulerCallHandle call) const noexcept {
  TfSchedulerCallSnapshot result;
  result.handle = call;
  if (!impl_ || impl_->owner == 0U) {
    result.error = TfSchedulerError::InvalidCoordinator;
    return result;
  }
  std::lock_guard lock{impl_->mutex};
  result.error = validate_handle(*impl_, call);
  if (result.error != TfSchedulerError::None) return result;
  try {
    const auto& record = impl_->calls.at(call.id);
    result.last_call_error = record.last_call_error;
    result.invocations = record.invocations;
    result.callback_failures = record.callback_failures;
    result.pending_callbacks = record.pending_callbacks;
    result.values = record.values;
    result.terminal_effect = record.terminal_effect;
  } catch (...) {
    result.error = TfSchedulerError::Allocation;
  }
  return result;
}

std::uint32_t TfSchedulerCoordinator::call_count() const noexcept {
  if (!impl_) return 0;
  std::lock_guard lock{impl_->mutex};
  return static_cast<std::uint32_t>(impl_->calls.size());
}

std::uint32_t TfSchedulerCoordinator::pending_callbacks() const noexcept {
  if (!impl_) return 0;
  std::lock_guard lock{impl_->mutex};
  return static_cast<std::uint32_t>(impl_->pending.size());
}

}  // namespace fsim::app

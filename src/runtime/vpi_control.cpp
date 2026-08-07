// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_control.hpp"

#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_control_operations = 65'536;
std::atomic<std::uint64_t> next_control_owner{1};

bool valid_operation(const SystemVerilogVpiControlOperation operation) {
  return static_cast<unsigned>(operation)
      <= static_cast<unsigned>(SystemVerilogVpiControlOperation::Release);
}

SystemVerilogVpiControlSubmitResult submit_failure(
    const SystemVerilogVpiControlError error,
    const SystemVerilogVpiObjectError object_error =
        SystemVerilogVpiObjectError::None,
    const SystemVerilogVpiValueError value_error =
        SystemVerilogVpiValueError::None) {
  SystemVerilogVpiControlSubmitResult result;
  result.error = error;
  result.object_error = object_error;
  result.value_error = value_error;
  return result;
}

SystemVerilogVpiControlStatusResult status_failure(
    const SystemVerilogVpiControlError error) {
  SystemVerilogVpiControlStatusResult result;
  result.error = error;
  return result;
}

SystemVerilogVpiControlError object_control_error(
    const SystemVerilogVpiObjectError error) {
  return error == SystemVerilogVpiObjectError::CrossSimulation
      ? SystemVerilogVpiControlError::CrossSimulation
      : SystemVerilogVpiControlError::InvalidObject;
}

}  // namespace

struct SystemVerilogVpiControlService::Impl {
  struct Record {
    SystemVerilogVpiControlOperation operation{
        SystemVerilogVpiControlOperation::Stop};
    SystemVerilogVpiControlStatus status{
        SystemVerilogVpiControlStatus::Pending};
    SystemVerilogVpiValueError value_error{
        SystemVerilogVpiValueError::None};
    ScheduledTaskHandle scheduler_handle;
  };

  std::uint64_t owner{};
  std::uint64_t next_id{1};
  StableOrder stable_order_base{};
  SystemVerilogVpiObjectRegistry* registry{};
  Scheduler* scheduler{};
  SystemVerilogVpiCallbackManager* callbacks{};
  SystemVerilogVpiControlState state{
      SystemVerilogVpiControlState::Running};
  mutable std::mutex mutex;
  std::map<std::uint64_t, Record> records;
};

namespace {

bool checked_control_order(
    const SystemVerilogVpiControlService::Impl& state,
    const std::uint64_t id,
    StableOrder& result) {
  if (id > std::numeric_limits<StableOrder>::max()
      - state.stable_order_base) {
    return false;
  }
  result = state.stable_order_base + id;
  return true;
}

void finish_operation(
    const std::shared_ptr<SystemVerilogVpiControlService::Impl>& state,
    const std::uint64_t id,
    const SystemVerilogVpiValueError value_error =
        SystemVerilogVpiValueError::None) {
  std::scoped_lock lock{state->mutex};
  const auto found = state->records.find(id);
  if (found == state->records.end()
      || found->second.status
          != SystemVerilogVpiControlStatus::Pending) {
    return;
  }
  found->second.value_error = value_error;
  found->second.status = value_error == SystemVerilogVpiValueError::None
      ? SystemVerilogVpiControlStatus::Applied
      : SystemVerilogVpiControlStatus::Failed;
}

void stop_at_safe_point(
    const std::shared_ptr<SystemVerilogVpiControlService::Impl>& state,
    const std::uint64_t id,
    const SystemVerilogVpiControlState control_state) {
  {
    std::scoped_lock lock{state->mutex};
    const auto found = state->records.find(id);
    if (found == state->records.end()
        || found->second.status
            != SystemVerilogVpiControlStatus::Pending) {
      return;
    }
    state->state = control_state;
    found->second.status = SystemVerilogVpiControlStatus::Applied;
  }
  state->scheduler->request_stop();
}

}  // namespace

SystemVerilogVpiControlService::SystemVerilogVpiControlService(
    SystemVerilogVpiObjectRegistry& registry,
    Scheduler& scheduler,
    SystemVerilogVpiCallbackManager& callbacks,
    const StableOrder stable_order_base)
    : impl_(std::make_shared<Impl>()) {
  impl_->owner =
      next_control_owner.fetch_add(1, std::memory_order_relaxed);
  impl_->stable_order_base = stable_order_base;
  impl_->registry = &registry;
  impl_->scheduler = &scheduler;
  impl_->callbacks = &callbacks;
}

bool SystemVerilogVpiControlService::valid() const noexcept {
  return impl_ && impl_->owner != 0U && impl_->registry->valid()
      && impl_->callbacks->valid();
}

std::uint64_t
SystemVerilogVpiControlService::simulation_identity() const noexcept {
  return valid() ? impl_->registry->simulation_identity() : 0U;
}

SystemVerilogVpiControlSubmitResult
SystemVerilogVpiControlService::submit(
    SystemVerilogVpiControlRequest request) {
  if (!valid()) {
    return submit_failure(SystemVerilogVpiControlError::InvalidControl);
  }
  if (!valid_operation(request.operation)) {
    return submit_failure(SystemVerilogVpiControlError::InvalidOperation);
  }
  const bool object_operation =
      request.operation == SystemVerilogVpiControlOperation::Force
      || request.operation == SystemVerilogVpiControlOperation::Release;
  if (object_operation != request.object.has_value()
      || ((request.operation == SystemVerilogVpiControlOperation::Force)
          != request.value.has_value())) {
    return submit_failure(SystemVerilogVpiControlError::InvalidRequest);
  }
  if (request.object) {
    const auto object = impl_->registry->lookup(*request.object);
    if (!object) {
      return submit_failure(
          object_control_error(object.error), object.error);
    }
    if (request.operation == SystemVerilogVpiControlOperation::Force) {
      const auto value_error = impl_->registry->validate_value_write(
          *request.object, &*request.value);
      if (value_error != SystemVerilogVpiValueError::None) {
        return submit_failure(
            SystemVerilogVpiControlError::InvalidRequest,
            SystemVerilogVpiObjectError::None,
            value_error);
      }
    }
  }

  std::scoped_lock lock{impl_->mutex};
  if (impl_->state == SystemVerilogVpiControlState::Finished) {
    return submit_failure(SystemVerilogVpiControlError::Terminal);
  }
  if (impl_->state != SystemVerilogVpiControlState::Running) {
    return submit_failure(SystemVerilogVpiControlError::NotRunning);
  }
  if (impl_->records.size() >= maximum_control_operations
      || impl_->next_id == 0U) {
    return submit_failure(SystemVerilogVpiControlError::ResourceLimit);
  }
  const auto id = impl_->next_id++;
  StableOrder order{};
  if (!checked_control_order(*impl_, id, order)) {
    return submit_failure(SystemVerilogVpiControlError::ResourceLimit);
  }
  auto [position, inserted] = impl_->records.emplace(
      id,
      Impl::Record{request.operation, {}, {}, {}});
  if (!inserted) {
    return submit_failure(SystemVerilogVpiControlError::ResourceLimit);
  }

  const std::weak_ptr<Impl> weak = impl_;
  try {
    switch (request.operation) {
      case SystemVerilogVpiControlOperation::Force:
      case SystemVerilogVpiControlOperation::Release: {
        const auto operation = request.operation;
        const auto object = *request.object;
        auto value = std::move(request.value);
        position->second.scheduler_handle = impl_->scheduler->schedule_after_cancelable(
            0,
            SchedulerPhase::update,
            order,
            [weak, id, operation, object, value = std::move(value)](
                Scheduler&) mutable {
              if (const auto state = weak.lock()) {
                SystemVerilogVpiValueError error{};
                try {
                  error = operation
                          == SystemVerilogVpiControlOperation::Force
                      ? state->registry->force_value(
                            object, std::move(*value))
                      : state->registry->release_forced_value(object);
                } catch (...) {
                  error = SystemVerilogVpiValueError::ResourceLimit;
                }
                finish_operation(state, id, error);
              }
            });
        break;
      }
      case SystemVerilogVpiControlOperation::Stop:
      case SystemVerilogVpiControlOperation::Interactive: {
        const auto control_state = request.operation
                == SystemVerilogVpiControlOperation::Stop
            ? SystemVerilogVpiControlState::Stopped
            : SystemVerilogVpiControlState::Interactive;
        position->second.scheduler_handle = impl_->scheduler->schedule_after_cancelable(
            0,
            SchedulerPhase::postponed,
            std::numeric_limits<StableOrder>::max() - 1U,
            [weak, id, control_state](Scheduler&) {
              if (const auto state = weak.lock()) {
                stop_at_safe_point(state, id, control_state);
              }
            });
        break;
      }
      case SystemVerilogVpiControlOperation::Finish: {
        const auto callback_error = impl_->callbacks->dispatch_lifecycle(
            SystemVerilogVpiCallbackKind::EndOfSimulation);
        if (callback_error != SystemVerilogVpiCallbackError::None) {
          impl_->records.erase(position);
          return submit_failure(
              SystemVerilogVpiControlError::ScheduleFailure);
        }
        position->second.scheduler_handle = impl_->scheduler->schedule_after_cancelable(
            0,
            SchedulerPhase::postponed,
            std::numeric_limits<StableOrder>::max(),
            [weak, id](Scheduler&) {
              if (const auto state = weak.lock()) {
                stop_at_safe_point(
                    state, id, SystemVerilogVpiControlState::Finished);
              }
            });
        break;
      }
      case SystemVerilogVpiControlOperation::Reset: {
        const auto callback_error = impl_->callbacks->dispatch_lifecycle(
            SystemVerilogVpiCallbackKind::StartOfReset);
        if (callback_error != SystemVerilogVpiCallbackError::None) {
          impl_->records.erase(position);
          return submit_failure(
              SystemVerilogVpiControlError::ScheduleFailure);
        }
        position->second.scheduler_handle = impl_->scheduler->schedule_after_cancelable(
            0,
            SchedulerPhase::update,
            order,
            [weak, id, order](Scheduler& scheduler) {
              const auto state = weak.lock();
              if (!state) {
                return;
              }
              SystemVerilogVpiValueError error{};
              try {
                error = state->registry->reset_values();
              } catch (...) {
                error = SystemVerilogVpiValueError::ResourceLimit;
              }
              if (error != SystemVerilogVpiValueError::None) {
                finish_operation(state, id, error);
                return;
              }
              scheduler.schedule(
                  SchedulerPhase::reactive,
                  order,
                  [weak, id](Scheduler& running) {
                    const auto reset_state = weak.lock();
                    if (!reset_state) {
                      return;
                    }
                    const auto callback_result =
                        reset_state->callbacks->dispatch_lifecycle(
                            SystemVerilogVpiCallbackKind::EndOfReset);
                    if (callback_result
                        != SystemVerilogVpiCallbackError::None) {
                      finish_operation(
                          reset_state,
                          id,
                          SystemVerilogVpiValueError::ResourceLimit);
                      return;
                    }
                    running.schedule(
                        SchedulerPhase::postponed,
                        std::numeric_limits<StableOrder>::max(),
                        [weak, id](Scheduler&) {
                          if (const auto final_state = weak.lock()) {
                            stop_at_safe_point(
                                final_state,
                                id,
                                SystemVerilogVpiControlState::Reset);
                          }
                        });
                  });
            });
        break;
      }
    }
  } catch (...) {
    impl_->records.erase(position);
    return submit_failure(SystemVerilogVpiControlError::ScheduleFailure);
  }

  SystemVerilogVpiControlSubmitResult result;
  result.value = {impl_->owner, id};
  return result;
}

SystemVerilogVpiControlStatusResult
SystemVerilogVpiControlService::status(
    const SystemVerilogVpiControlHandle handle) const {
  if (!impl_ || !handle) {
    return status_failure(SystemVerilogVpiControlError::InvalidHandle);
  }
  if (handle.owner != impl_->owner) {
    return status_failure(SystemVerilogVpiControlError::CrossControl);
  }
  std::scoped_lock lock{impl_->mutex};
  const auto found = impl_->records.find(handle.id);
  if (found == impl_->records.end()) {
    return status_failure(SystemVerilogVpiControlError::NotFound);
  }
  return {
      found->second.status,
      found->second.value_error,
      SystemVerilogVpiControlError::None,
  };
}

SystemVerilogVpiControlState
SystemVerilogVpiControlService::state() const {
  if (!impl_) {
    return SystemVerilogVpiControlState::Finished;
  }
  std::scoped_lock lock{impl_->mutex};
  return impl_->state;
}

SystemVerilogVpiControlError SystemVerilogVpiControlService::resume() {
  if (!valid()) {
    return SystemVerilogVpiControlError::InvalidControl;
  }
  SystemVerilogVpiControlState stopped_state;
  {
    std::scoped_lock lock{impl_->mutex};
    stopped_state = impl_->state;
    if (stopped_state == SystemVerilogVpiControlState::Finished) {
      return SystemVerilogVpiControlError::Terminal;
    }
    if (stopped_state == SystemVerilogVpiControlState::Running) {
      return SystemVerilogVpiControlError::NotStopped;
    }
  }

  try {
    if (stopped_state == SystemVerilogVpiControlState::Reset) {
      impl_->scheduler->reset();
    } else {
      impl_->scheduler->clear_stop();
    }
  } catch (...) {
    return SystemVerilogVpiControlError::ScheduleFailure;
  }
  std::scoped_lock lock{impl_->mutex};
  impl_->state = SystemVerilogVpiControlState::Running;
  return SystemVerilogVpiControlError::None;
}

std::size_t SystemVerilogVpiControlService::operations() const {
  if (!impl_) {
    return 0;
  }
  std::scoped_lock lock{impl_->mutex};
  return impl_->records.size();
}

}  // namespace fsim::runtime

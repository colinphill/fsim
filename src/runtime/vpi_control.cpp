// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_control.hpp"

#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <utility>

namespace fsim::runtime {

namespace {

    constexpr std::size_t maximum_control_operations = 65'536;
    constexpr std::size_t maximum_assertion_objects = 65'536;
    constexpr std::size_t maximum_assertion_text_bytes = 1U << 20U;
    std::atomic<std::uint64_t> next_control_owner { 1 };

    bool valid_operation(const SystemVerilogVpiControlOperation operation)
    {
        return static_cast<unsigned>(operation)
            <= static_cast<unsigned>(SystemVerilogVpiControlOperation::Release);
    }

    SystemVerilogVpiControlSubmitResult submit_failure(
        const SystemVerilogVpiControlError error,
        const SystemVerilogVpiObjectError object_error = SystemVerilogVpiObjectError::None,
        const SystemVerilogVpiValueError value_error = SystemVerilogVpiValueError::None)
    {
        SystemVerilogVpiControlSubmitResult result;
        result.error = error;
        result.object_error = object_error;
        result.value_error = value_error;
        return result;
    }

    SystemVerilogVpiControlStatusResult status_failure(
        const SystemVerilogVpiControlError error)
    {
        SystemVerilogVpiControlStatusResult result;
        result.error = error;
        return result;
    }

    SystemVerilogVpiControlError object_control_error(
        const SystemVerilogVpiObjectError error)
    {
        return error == SystemVerilogVpiObjectError::CrossSimulation
            ? SystemVerilogVpiControlError::CrossSimulation
            : SystemVerilogVpiControlError::InvalidObject;
    }

} // namespace

struct SystemVerilogVpiAssertionApi::Impl {
    struct Record {
        bool enabled { true };
        std::optional<SystemVerilogVpiAssertionKind> kind;
        SystemVerilogVpiAssertionStatistics statistics;
    };

    SystemVerilogVpiObjectRegistry* registry { };
    SystemVerilogVpiCallbackManager* callbacks { };
    SystemVerilogVpiAssertionControlHook control_hook;
    mutable std::mutex mutex;
    std::map<fsim_vpi_handle_v1, Record> records;
    std::set<fsim_vpi_handle_v1> reserved_objects;
};

namespace {

    bool valid_assertion_control_operation(
        const SystemVerilogVpiAssertionControlOperation operation)
    {
        return static_cast<std::uint32_t>(operation)
            <= static_cast<std::uint32_t>(
                SystemVerilogVpiAssertionControlOperation::Kill);
    }

    bool valid_assertion_kind(const SystemVerilogVpiAssertionKind kind)
    {
        return static_cast<std::uint32_t>(kind)
            <= static_cast<std::uint32_t>(
                SystemVerilogVpiAssertionKind::Restriction);
    }

    bool valid_assertion_outcome(const SystemVerilogVpiAssertionOutcome outcome)
    {
        return static_cast<std::uint32_t>(outcome)
            <= static_cast<std::uint32_t>(
                SystemVerilogVpiAssertionOutcome::Aborted);
    }

    SystemVerilogVpiAssertionApiError assertion_object_error(
        const SystemVerilogVpiObjectError error)
    {
        return error == SystemVerilogVpiObjectError::CrossSimulation
            ? SystemVerilogVpiAssertionApiError::CrossSimulation
            : SystemVerilogVpiAssertionApiError::InvalidObject;
    }

    bool assertion_text_is_bounded(const SystemVerilogVpiAssertionEvent& event)
    {
        const auto first = event.name.size();
        const auto second = event.process.size();
        const auto third = event.instance_identity.size();
        return first <= maximum_assertion_text_bytes
            && second <= maximum_assertion_text_bytes - first
            && third <= maximum_assertion_text_bytes - first - second;
    }

    void saturating_increment(
        std::uint64_t& value,
        SystemVerilogVpiAssertionStatistics& statistics)
    {
        if (value == std::numeric_limits<std::uint64_t>::max()) {
            statistics.saturated = true;
            return;
        }
        ++value;
    }

} // namespace

struct SystemVerilogVpiControlService::Impl {
    struct Record {
        SystemVerilogVpiControlOperation operation {
            SystemVerilogVpiControlOperation::Stop
        };
        SystemVerilogVpiControlStatus status {
            SystemVerilogVpiControlStatus::Pending
        };
        SystemVerilogVpiValueError value_error {
            SystemVerilogVpiValueError::None
        };
        ScheduledTaskHandle scheduler_handle;
    };

    std::uint64_t owner { };
    std::uint64_t next_id { 1 };
    StableOrder stable_order_base { };
    SystemVerilogVpiObjectRegistry* registry { };
    Scheduler* scheduler { };
    SystemVerilogVpiCallbackManager* callbacks { };
    SystemVerilogVpiControlState state {
        SystemVerilogVpiControlState::Running
    };
    bool dispatch_finish_lifecycle { true };
    bool reset_supported { true };
    mutable std::mutex mutex;
    std::map<std::uint64_t, Record> records;
};

namespace {

    bool checked_control_order(
        const SystemVerilogVpiControlService::Impl& state,
        const std::uint64_t id,
        StableOrder& result)
    {
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
        const SystemVerilogVpiValueError value_error = SystemVerilogVpiValueError::None)
    {
        std::scoped_lock lock { state->mutex };
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
        const SystemVerilogVpiControlState control_state)
    {
        {
            std::scoped_lock lock { state->mutex };
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

} // namespace

SystemVerilogVpiControlService::SystemVerilogVpiControlService(
    SystemVerilogVpiObjectRegistry& registry,
    Scheduler& scheduler,
    SystemVerilogVpiCallbackManager& callbacks,
    const StableOrder stable_order_base,
    const bool dispatch_finish_lifecycle,
    const bool reset_supported)
    : impl_(std::make_shared<Impl>())
{
    impl_->owner = next_control_owner.fetch_add(1, std::memory_order_relaxed);
    impl_->stable_order_base = stable_order_base;
    impl_->registry = &registry;
    impl_->scheduler = &scheduler;
    impl_->callbacks = &callbacks;
    impl_->dispatch_finish_lifecycle = dispatch_finish_lifecycle;
    impl_->reset_supported = reset_supported;
}

bool SystemVerilogVpiControlService::valid() const noexcept
{
    return impl_ && impl_->owner != 0U && impl_->registry->valid()
        && impl_->callbacks->valid();
}

std::uint64_t
SystemVerilogVpiControlService::simulation_identity() const noexcept
{
    return valid() ? impl_->registry->simulation_identity() : 0U;
}

SystemVerilogVpiControlSubmitResult
SystemVerilogVpiControlService::submit(
    SystemVerilogVpiControlRequest request)
{
    if (!valid()) {
        return submit_failure(SystemVerilogVpiControlError::InvalidControl);
    }
    if (!valid_operation(request.operation)) {
        return submit_failure(SystemVerilogVpiControlError::InvalidOperation);
    }
    if (request.operation == SystemVerilogVpiControlOperation::Reset
        && !impl_->reset_supported) {
        return submit_failure(SystemVerilogVpiControlError::InvalidOperation);
    }
    const bool object_operation = request.operation == SystemVerilogVpiControlOperation::Force
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

    std::scoped_lock lock { impl_->mutex };
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
    StableOrder order { };
    if (!checked_control_order(*impl_, id, order)) {
        return submit_failure(SystemVerilogVpiControlError::ResourceLimit);
    }
    auto [position, inserted] = impl_->records.emplace(
        id,
        Impl::Record { request.operation, { }, { }, { } });
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
                        SystemVerilogVpiValueError error { };
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
            if (impl_->dispatch_finish_lifecycle) {
                const auto callback_error = impl_->callbacks->dispatch_lifecycle(
                    SystemVerilogVpiCallbackKind::EndOfSimulation);
                if (callback_error != SystemVerilogVpiCallbackError::None) {
                    impl_->records.erase(position);
                    return submit_failure(
                        SystemVerilogVpiControlError::ScheduleFailure);
                }
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
                    SystemVerilogVpiValueError error { };
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
                            const auto callback_result = reset_state->callbacks->dispatch_lifecycle(
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
    result.value = { impl_->owner, id };
    return result;
}

SystemVerilogVpiControlStatusResult
SystemVerilogVpiControlService::status(
    const SystemVerilogVpiControlHandle handle) const
{
    if (!impl_ || !handle) {
        return status_failure(SystemVerilogVpiControlError::InvalidHandle);
    }
    if (handle.owner != impl_->owner) {
        return status_failure(SystemVerilogVpiControlError::CrossControl);
    }
    std::scoped_lock lock { impl_->mutex };
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
SystemVerilogVpiControlService::state() const
{
    if (!impl_) {
        return SystemVerilogVpiControlState::Finished;
    }
    std::scoped_lock lock { impl_->mutex };
    return impl_->state;
}

SystemVerilogVpiControlError SystemVerilogVpiControlService::resume()
{
    if (!valid()) {
        return SystemVerilogVpiControlError::InvalidControl;
    }
    SystemVerilogVpiControlState stopped_state;
    {
        std::scoped_lock lock { impl_->mutex };
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
    std::scoped_lock lock { impl_->mutex };
    impl_->state = SystemVerilogVpiControlState::Running;
    return SystemVerilogVpiControlError::None;
}

void SystemVerilogVpiControlService::mark_finished() noexcept
{
    if (!impl_) {
        return;
    }
    std::scoped_lock lock { impl_->mutex };
    impl_->state = SystemVerilogVpiControlState::Finished;
}

std::size_t SystemVerilogVpiControlService::operations() const
{
    if (!impl_) {
        return 0;
    }
    std::scoped_lock lock { impl_->mutex };
    return impl_->records.size();
}

SystemVerilogVpiAssertionApi::SystemVerilogVpiAssertionApi(
    SystemVerilogVpiObjectRegistry& registry,
    SystemVerilogVpiCallbackManager& callbacks,
    SystemVerilogVpiAssertionControlHook control_hook)
    : impl_(std::make_shared<Impl>())
{
    impl_->registry = &registry;
    impl_->callbacks = &callbacks;
    impl_->control_hook = std::move(control_hook);
}

bool SystemVerilogVpiAssertionApi::valid() const noexcept
{
    return impl_ && impl_->registry && impl_->registry->valid()
        && impl_->callbacks && impl_->callbacks->valid()
        && static_cast<bool>(impl_->control_hook);
}

std::uint64_t
SystemVerilogVpiAssertionApi::simulation_identity() const noexcept
{
    return valid() ? impl_->registry->simulation_identity() : 0U;
}

SystemVerilogVpiAssertionApiError SystemVerilogVpiAssertionApi::control(
    const SystemVerilogVpiAssertionControlOperation operation,
    const std::optional<fsim_vpi_handle_v1> object)
{
    if (!valid()) {
        return SystemVerilogVpiAssertionApiError::InvalidService;
    }
    if (!valid_assertion_control_operation(operation)) {
        return SystemVerilogVpiAssertionApiError::InvalidOperation;
    }
    if (object) {
        const auto info = impl_->registry->lookup(*object);
        if (!info) {
            return assertion_object_error(info.error);
        }
        if (info.value->kind != SystemVerilogVpiObjectKind::Assertion) {
            return SystemVerilogVpiAssertionApiError::InvalidObject;
        }
    }

    bool reserved { };
    if (object) {
        std::scoped_lock lock { impl_->mutex };
        if (!impl_->records.contains(*object)
            && !impl_->reserved_objects.contains(*object)) {
            if (impl_->records.size() + impl_->reserved_objects.size()
                >= maximum_assertion_objects) {
                return SystemVerilogVpiAssertionApiError::ResourceLimit;
            }
            impl_->reserved_objects.insert(*object);
            reserved = true;
        }
    }

    bool applied { };
    try {
        applied = impl_->control_hook(operation, object);
    } catch (...) {
        if (reserved) {
            std::scoped_lock lock { impl_->mutex };
            impl_->reserved_objects.erase(*object);
        }
        return SystemVerilogVpiAssertionApiError::ControlFailure;
    }
    if (!applied) {
        if (reserved) {
            std::scoped_lock lock { impl_->mutex };
            impl_->reserved_objects.erase(*object);
        }
        return SystemVerilogVpiAssertionApiError::ControlFailure;
    }

    std::scoped_lock lock { impl_->mutex };
    if (reserved) {
        impl_->reserved_objects.erase(*object);
    }
    const auto apply = [&](Impl::Record& record) {
        switch (operation) {
        case SystemVerilogVpiAssertionControlOperation::Reset:
            record.statistics = { };
            break;
        case SystemVerilogVpiAssertionControlOperation::Enable:
            record.enabled = true;
            break;
        case SystemVerilogVpiAssertionControlOperation::Disable:
            record.enabled = false;
            break;
        case SystemVerilogVpiAssertionControlOperation::Kill:
            break;
        }
    };
    if (object) {
        apply(impl_->records[*object]);
    } else {
        for (auto& [handle, record] : impl_->records) {
            (void)handle;
            apply(record);
        }
    }
    return SystemVerilogVpiAssertionApiError::None;
}

SystemVerilogVpiAssertionApiError SystemVerilogVpiAssertionApi::observe(
    const fsim_vpi_handle_v1 object,
    SystemVerilogVpiAssertionEvent event)
{
    if (!valid()) {
        return SystemVerilogVpiAssertionApiError::InvalidService;
    }
    if (!valid_assertion_kind(event.kind)
        || !valid_assertion_outcome(event.outcome)
        || !assertion_text_is_bounded(event)) {
        return SystemVerilogVpiAssertionApiError::InvalidRequest;
    }
    const auto info = impl_->registry->lookup(object);
    if (!info) {
        return assertion_object_error(info.error);
    }
    if (info.value->kind != SystemVerilogVpiObjectKind::Assertion) {
        return SystemVerilogVpiAssertionApiError::InvalidObject;
    }

    {
        std::scoped_lock lock { impl_->mutex };
        const auto found = impl_->records.find(object);
        if (found == impl_->records.end()) {
            if (impl_->records.size() + impl_->reserved_objects.size()
                    >= maximum_assertion_objects
                && !impl_->reserved_objects.contains(object)) {
                return SystemVerilogVpiAssertionApiError::ResourceLimit;
            }
            impl_->records.emplace(object, Impl::Record { });
        }
        auto& record = impl_->records.at(object);
        if (record.kind && *record.kind != event.kind) {
            return SystemVerilogVpiAssertionApiError::InvalidRequest;
        }
        record.kind = event.kind;
        auto& statistics = record.statistics;
        if (event.outcome != SystemVerilogVpiAssertionOutcome::Disabled) {
            saturating_increment(statistics.attempts, statistics);
        }
        switch (event.outcome) {
        case SystemVerilogVpiAssertionOutcome::Success:
            saturating_increment(statistics.successes, statistics);
            break;
        case SystemVerilogVpiAssertionOutcome::Failure:
            saturating_increment(statistics.failures, statistics);
            break;
        case SystemVerilogVpiAssertionOutcome::Vacuous:
            saturating_increment(statistics.vacuous, statistics);
            break;
        case SystemVerilogVpiAssertionOutcome::Disabled:
            saturating_increment(statistics.disabled, statistics);
            break;
        case SystemVerilogVpiAssertionOutcome::Aborted:
            saturating_increment(statistics.aborted, statistics);
            break;
        }
    }

    const auto callback = impl_->callbacks->dispatch_assertion(
        object, std::move(event));
    return callback == SystemVerilogVpiCallbackError::None
        ? SystemVerilogVpiAssertionApiError::None
        : SystemVerilogVpiAssertionApiError::CallbackFailure;
}

SystemVerilogVpiAssertionStatusResult
SystemVerilogVpiAssertionApi::status(const fsim_vpi_handle_v1 object) const
{
    SystemVerilogVpiAssertionStatusResult result;
    if (!valid()) {
        result.error = SystemVerilogVpiAssertionApiError::InvalidService;
        return result;
    }
    const auto info = impl_->registry->lookup(object);
    if (!info) {
        result.error = assertion_object_error(info.error);
        result.object_error = info.error;
        return result;
    }
    if (info.value->kind != SystemVerilogVpiObjectKind::Assertion) {
        result.error = SystemVerilogVpiAssertionApiError::InvalidObject;
        return result;
    }
    std::scoped_lock lock { impl_->mutex };
    if (const auto found = impl_->records.find(object);
        found != impl_->records.end()) {
        result.enabled = found->second.enabled;
        result.kind = found->second.kind;
        result.statistics = found->second.statistics;
    }
    return result;
}

std::size_t SystemVerilogVpiAssertionApi::observed_assertions() const
{
    if (!impl_) {
        return 0U;
    }
    std::scoped_lock lock { impl_->mutex };
    return impl_->records.size();
}

} // namespace fsim::runtime

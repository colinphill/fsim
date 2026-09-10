// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_callback.hpp"

#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace fsim::runtime {

namespace {

    constexpr std::size_t maximum_callbacks = 65'536;
    std::atomic<std::uint64_t> next_callback_manager_owner { 1 };

    bool valid_kind(const SystemVerilogVpiCallbackKind kind)
    {
        return static_cast<unsigned>(kind)
            <= static_cast<unsigned>(
                SystemVerilogVpiCallbackKind::EndOfRestart);
    }

    bool is_one_shot(const SystemVerilogVpiCallbackKind kind)
    {
        return kind != SystemVerilogVpiCallbackKind::ValueChange
            && kind != SystemVerilogVpiCallbackKind::AssertionSuccess
            && kind != SystemVerilogVpiCallbackKind::AssertionFailure
            && kind != SystemVerilogVpiCallbackKind::AssertionVacuous
            && kind != SystemVerilogVpiCallbackKind::AssertionDisabled
            && kind != SystemVerilogVpiCallbackKind::AssertionAborted;
    }

    bool is_assertion(const SystemVerilogVpiCallbackKind kind)
    {
        return static_cast<unsigned>(kind)
            >= static_cast<unsigned>(
                SystemVerilogVpiCallbackKind::AssertionSuccess)
            && static_cast<unsigned>(kind)
            <= static_cast<unsigned>(
                SystemVerilogVpiCallbackKind::AssertionAborted);
    }

    bool is_lifecycle(const SystemVerilogVpiCallbackKind kind)
    {
        return static_cast<unsigned>(kind)
            >= static_cast<unsigned>(
                SystemVerilogVpiCallbackKind::StartOfSimulation)
            && static_cast<unsigned>(kind)
            <= static_cast<unsigned>(SystemVerilogVpiCallbackKind::EndOfRestart);
    }

    SystemVerilogVpiCallbackRegistrationResult registration_failure(
        const SystemVerilogVpiCallbackError error,
        const SystemVerilogVpiObjectError object_error = SystemVerilogVpiObjectError::None,
        const SystemVerilogVpiTimeError time_error = SystemVerilogVpiTimeError::None)
    {
        SystemVerilogVpiCallbackRegistrationResult result;
        result.error = error;
        result.object_error = object_error;
        result.time_error = time_error;
        return result;
    }

    SystemVerilogVpiCallbackStatusResult status_failure(
        const SystemVerilogVpiCallbackError error)
    {
        SystemVerilogVpiCallbackStatusResult result;
        result.error = error;
        return result;
    }

    SystemVerilogVpiCallbackError object_callback_error(
        const SystemVerilogVpiObjectError error)
    {
        return error == SystemVerilogVpiObjectError::CrossSimulation
            ? SystemVerilogVpiCallbackError::CrossSimulation
            : SystemVerilogVpiCallbackError::InvalidObject;
    }

    SchedulerPhase callback_phase(
        const SystemVerilogVpiCallbackKind kind)
    {
        switch (kind) {
        case SystemVerilogVpiCallbackKind::Synchronization:
            return SchedulerPhase::update;
        case SystemVerilogVpiCallbackKind::StartOfSimulation:
        case SystemVerilogVpiCallbackKind::StartOfReset:
        case SystemVerilogVpiCallbackKind::StartOfSave:
        case SystemVerilogVpiCallbackKind::StartOfRestart:
            return SchedulerPhase::active;
        case SystemVerilogVpiCallbackKind::ValueChange:
        case SystemVerilogVpiCallbackKind::AssertionSuccess:
        case SystemVerilogVpiCallbackKind::AssertionFailure:
        case SystemVerilogVpiCallbackKind::AssertionVacuous:
        case SystemVerilogVpiCallbackKind::AssertionDisabled:
        case SystemVerilogVpiCallbackKind::AssertionAborted:
        case SystemVerilogVpiCallbackKind::ReadWrite:
            return SchedulerPhase::reactive;
        case SystemVerilogVpiCallbackKind::ReadOnly:
        case SystemVerilogVpiCallbackKind::EndOfSimulation:
        case SystemVerilogVpiCallbackKind::EndOfReset:
        case SystemVerilogVpiCallbackKind::EndOfSave:
        case SystemVerilogVpiCallbackKind::EndOfRestart:
            return SchedulerPhase::postponed;
        case SystemVerilogVpiCallbackKind::AfterDelay:
        case SystemVerilogVpiCallbackKind::NextTime:
            return SchedulerPhase::active;
        }
        return SchedulerPhase::active;
    }

} // namespace

struct SystemVerilogVpiCallbackManager::Impl {
    struct Record {
        SystemVerilogVpiCallbackKind kind {
            SystemVerilogVpiCallbackKind::ValueChange
        };
        std::optional<fsim_vpi_handle_v1> object;
        std::uint64_t user_data { };
        SystemVerilogVpiCallback callback;
        SystemVerilogVpiCallbackStatus status {
            SystemVerilogVpiCallbackStatus::Active
        };
        ScheduledTaskHandle scheduler_handle;
        SystemVerilogVpiTimedHandle timed_handle;
    };

    std::uint64_t owner { };
    std::uint64_t next_id { 1 };
    std::uint64_t observer { };
    StableOrder stable_order_base { };
    SystemVerilogVpiObjectRegistry* registry { };
    Scheduler* scheduler { };
    SystemVerilogVpiTimeService* time_service { };
    mutable std::mutex mutex;
    std::map<std::uint64_t, Record> records;
    std::array<bool,
        static_cast<std::size_t>(SystemVerilogVpiCallbackKind::EndOfRestart)
            + 1U>
        dispatching { };
    std::atomic_bool has_registrations { false };
};

namespace {

    SystemVerilogVpiCallbackEvent make_event(
        const SystemVerilogVpiCallbackManager::Impl& state,
        const std::uint64_t id,
        const SystemVerilogVpiCallbackManager::Impl::Record& record,
        std::optional<SystemVerilogVpiStoredValue> value,
        std::optional<SystemVerilogVpiTimeQueryResult> supplied_time = std::nullopt,
        std::optional<SystemVerilogVpiAssertionEvent> assertion = std::nullopt)
    {
        SystemVerilogVpiCallbackEvent event;
        event.registration = { state.owner, id };
        event.kind = record.kind;
        event.object = record.object;
        event.time = supplied_time.value_or(state.time_service->query(
            SystemVerilogVpiTimeFormat::IntegerTicks));
        event.value = std::move(value);
        event.assertion = std::move(assertion);
        event.user_data = record.user_data;
        event.registration_order = id;
        event.simulation_identity = state.registry->simulation_identity();
        return event;
    }

    void dispatch_callback(
        const std::shared_ptr<SystemVerilogVpiCallbackManager::Impl>& state,
        const std::uint64_t id,
        std::optional<SystemVerilogVpiStoredValue> value,
        std::optional<SystemVerilogVpiTimeQueryResult> supplied_time = std::nullopt,
        std::optional<SystemVerilogVpiAssertionEvent> assertion = std::nullopt)
    {
        SystemVerilogVpiCallback callback;
        SystemVerilogVpiCallbackEvent event;
        bool one_shot { };
        {
            std::scoped_lock lock { state->mutex };
            const auto found = state->records.find(id);
            if (found == state->records.end()
                || found->second.status
                    != SystemVerilogVpiCallbackStatus::Active) {
                return;
            }
            callback = found->second.callback;
            event = make_event(
                *state, id, found->second, std::move(value), supplied_time,
                std::move(assertion));
            one_shot = is_one_shot(found->second.kind);
        }

        bool failed { };
        try {
            callback(event);
        } catch (...) {
            failed = true;
        }

        std::scoped_lock lock { state->mutex };
        const auto found = state->records.find(id);
        if (found == state->records.end()
            || found->second.status
                != SystemVerilogVpiCallbackStatus::Active) {
            return;
        }
        if (failed) {
            found->second.status = SystemVerilogVpiCallbackStatus::CallbackFailed;
        } else if (one_shot) {
            found->second.status = SystemVerilogVpiCallbackStatus::Fired;
        }
    }

    bool dispatch_callbacks(
        const std::shared_ptr<SystemVerilogVpiCallbackManager::Impl>& state,
        const SystemVerilogVpiCallbackKind kind,
        const std::vector<std::uint64_t>& callbacks,
        const std::optional<SystemVerilogVpiStoredValue>& value = std::nullopt,
        const std::optional<SystemVerilogVpiAssertionEvent>& assertion = std::nullopt)
    {
        const auto index = static_cast<std::size_t>(kind);
        {
            std::scoped_lock lock { state->mutex };
            if (state->dispatching[index]) {
                return false;
            }
            state->dispatching[index] = true;
        }

        try {
            for (const auto id : callbacks) {
                dispatch_callback(
                    state, id, value, std::nullopt, assertion);
            }
        } catch (...) {
            std::scoped_lock lock { state->mutex };
            state->dispatching[index] = false;
            throw;
        }

        std::scoped_lock lock { state->mutex };
        state->dispatching[index] = false;
        return true;
    }

    bool checked_stable_order(
        const SystemVerilogVpiCallbackManager::Impl& state,
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

    void observe_value_change(
        const std::weak_ptr<SystemVerilogVpiCallbackManager::Impl>& weak,
        const fsim_vpi_handle_v1 object,
        const std::optional<SystemVerilogVpiStoredValue>& value)
    {
        const auto state = weak.lock();
        if (!state) {
            return;
        }

        std::vector<std::uint64_t> callbacks;
        {
            std::scoped_lock lock { state->mutex };
            for (const auto& [id, record] : state->records) {
                if (record.status == SystemVerilogVpiCallbackStatus::Active
                    && record.kind
                        == SystemVerilogVpiCallbackKind::ValueChange
                    && record.object == object) {
                    callbacks.push_back(id);
                }
            }
        }

        if (callbacks.empty()) {
            return;
        }
        StableOrder order { };
        if (!checked_stable_order(*state, callbacks.front(), order)) {
            return;
        }
        try {
            state->scheduler->schedule(
                callback_phase(SystemVerilogVpiCallbackKind::ValueChange),
                order,
                [weak, callbacks, value](Scheduler&) {
                    if (const auto locked = weak.lock()) {
                        (void)dispatch_callbacks(
                            locked,
                            SystemVerilogVpiCallbackKind::ValueChange,
                            callbacks, value);
                    }
                });
        } catch (...) {
            std::scoped_lock lock { state->mutex };
            for (const auto id : callbacks) {
                const auto found = state->records.find(id);
                if (found != state->records.end()) {
                    found->second.status = SystemVerilogVpiCallbackStatus::CallbackFailed;
                }
            }
        }
    }

} // namespace

SystemVerilogVpiCallbackManager::SystemVerilogVpiCallbackManager(
    SystemVerilogVpiObjectRegistry& registry,
    Scheduler& scheduler,
    SystemVerilogVpiTimeService& time_service,
    const StableOrder stable_order_base)
    : impl_(std::make_shared<Impl>())
{
    impl_->owner = next_callback_manager_owner.fetch_add(
        1, std::memory_order_relaxed);
    impl_->stable_order_base = stable_order_base;
    impl_->registry = &registry;
    impl_->scheduler = &scheduler;
    impl_->time_service = &time_service;
    const std::weak_ptr<Impl> weak = impl_;
    const auto observer = registry.add_value_observer(
        [weak](const fsim_vpi_handle_v1 object,
            const SystemVerilogVpiStoredValue& value) {
            observe_value_change(weak, object, value);
        });
    if (observer) {
        impl_->observer = *observer;
    }
}

SystemVerilogVpiCallbackManager::~SystemVerilogVpiCallbackManager()
{
    if (!impl_) {
        return;
    }
    if (impl_->observer != 0U) {
        (void)impl_->registry->remove_value_observer(impl_->observer);
    }
    std::scoped_lock lock { impl_->mutex };
    for (auto& [id, record] : impl_->records) {
        (void)id;
        if (record.status != SystemVerilogVpiCallbackStatus::Active) {
            continue;
        }
        if (record.timed_handle) {
            (void)impl_->time_service->cancel(record.timed_handle);
        }
        if (record.scheduler_handle) {
            impl_->scheduler->cancel(record.scheduler_handle);
        }
    }
}

bool SystemVerilogVpiCallbackManager::valid() const noexcept
{
    return impl_ && impl_->owner != 0U && impl_->observer != 0U
        && impl_->registry->valid() && impl_->time_service->valid();
}

SystemVerilogVpiCallbackRegistrationResult
SystemVerilogVpiCallbackManager::register_callback(
    SystemVerilogVpiCallbackRegistration registration)
{
    if (!valid()) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidManager);
    }
    if (!valid_kind(registration.kind)) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidKind);
    }
    if (!registration.callback) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidRequest);
    }
    if (registration.kind
            == SystemVerilogVpiCallbackKind::ValueChange
        && (!registration.object || registration.delay)) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidRequest);
    }
    if (is_assertion(registration.kind)
        && (!registration.object || registration.delay)) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidRequest);
    }
    if (registration.kind
            == SystemVerilogVpiCallbackKind::AfterDelay
        && !registration.delay) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidRequest);
    }
    if (registration.kind
            == SystemVerilogVpiCallbackKind::NextTime
        && registration.delay) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidRequest);
    }
    if (is_lifecycle(registration.kind)
        && (registration.object || registration.delay)) {
        return registration_failure(
            SystemVerilogVpiCallbackError::InvalidRequest);
    }

    if (registration.object) {
        const auto object = impl_->registry->lookup(*registration.object);
        if (!object) {
            return registration_failure(
                object_callback_error(object.error), object.error);
        }
        if (is_assertion(registration.kind)
            && object.value->kind
                != SystemVerilogVpiObjectKind::Assertion) {
            return registration_failure(
                SystemVerilogVpiCallbackError::InvalidObject,
                SystemVerilogVpiObjectError::InvalidKind);
        }
    }

    SimulationTick scheduled_delay { };
    if (registration.kind
            != SystemVerilogVpiCallbackKind::ValueChange
        && registration.kind
            != SystemVerilogVpiCallbackKind::NextTime
        && !is_assertion(registration.kind)
        && !is_lifecycle(registration.kind)) {
        if (registration.delay) {
            const auto converted = impl_->time_service->convert_delay(*registration.delay);
            if (!converted) {
                return registration_failure(
                    SystemVerilogVpiCallbackError::ScheduleFailure,
                    SystemVerilogVpiObjectError::None,
                    converted.error);
            }
            scheduled_delay = converted.ticks;
        }
    } else if (registration.kind
        == SystemVerilogVpiCallbackKind::NextTime) {
        const auto next = impl_->scheduler->next_pending_time();
        if (!next || *next < impl_->scheduler->now()) {
            return registration_failure(
                SystemVerilogVpiCallbackError::NoFutureTime);
        }
        scheduled_delay = *next - impl_->scheduler->now();
    }

    std::scoped_lock lock { impl_->mutex };
    if (impl_->records.size() >= maximum_callbacks
        || impl_->next_id == 0U) {
        return registration_failure(
            SystemVerilogVpiCallbackError::ResourceLimit);
    }
    const auto id = impl_->next_id++;
    StableOrder stable_order { };
    if (!checked_stable_order(*impl_, id, stable_order)) {
        return registration_failure(
            SystemVerilogVpiCallbackError::ResourceLimit);
    }
    auto [position, inserted] = impl_->records.emplace(
        id,
        Impl::Record {
            registration.kind,
            registration.object,
            registration.user_data,
            std::move(registration.callback),
            SystemVerilogVpiCallbackStatus::Active,
            { },
            { },
        });
    if (!inserted) {
        return registration_failure(
            SystemVerilogVpiCallbackError::ResourceLimit);
    }
    impl_->has_registrations.store(true, std::memory_order_release);

    const std::weak_ptr<Impl> weak = impl_;
    try {
        if (registration.kind
            == SystemVerilogVpiCallbackKind::AfterDelay) {
            const auto timed = impl_->time_service->schedule(
                *registration.delay,
                stable_order,
                [weak, id](const SystemVerilogVpiTimeQueryResult& query) {
                    if (const auto state = weak.lock()) {
                        dispatch_callback(state, id, std::nullopt, query);
                    }
                });
            if (!timed) {
                const auto time_error = timed.error;
                impl_->records.erase(position);
                return registration_failure(
                    SystemVerilogVpiCallbackError::ScheduleFailure,
                    SystemVerilogVpiObjectError::None,
                    time_error);
            }
            position->second.timed_handle = timed.value;
        } else if (registration.kind
                != SystemVerilogVpiCallbackKind::ValueChange
            && !is_assertion(registration.kind)
            && !is_lifecycle(registration.kind)) {
            position->second.scheduler_handle = impl_->scheduler->schedule_after_cancelable(
                scheduled_delay,
                callback_phase(registration.kind),
                stable_order,
                [weak, id](Scheduler&) {
                    if (const auto state = weak.lock()) {
                        dispatch_callback(state, id, std::nullopt);
                    }
                });
        }
    } catch (...) {
        impl_->records.erase(position);
        return registration_failure(
            SystemVerilogVpiCallbackError::ScheduleFailure);
    }

    SystemVerilogVpiCallbackRegistrationResult result;
    result.value = { impl_->owner, id };
    return result;
}
SystemVerilogVpiCallbackError
SystemVerilogVpiCallbackManager::remove_callback(
    const SystemVerilogVpiCallbackHandle handle)
{
    if (!impl_ || !handle) {
        return SystemVerilogVpiCallbackError::InvalidHandle;
    }
    if (handle.owner != impl_->owner) {
        return SystemVerilogVpiCallbackError::CrossManager;
    }

    ScheduledTaskHandle scheduler_handle;
    SystemVerilogVpiTimedHandle timed_handle;
    {
        std::scoped_lock lock { impl_->mutex };
        const auto found = impl_->records.find(handle.id);
        if (found == impl_->records.end()) {
            return SystemVerilogVpiCallbackError::NotFound;
        }
        if (found->second.status
            != SystemVerilogVpiCallbackStatus::Active) {
            return SystemVerilogVpiCallbackError::NotActive;
        }
        found->second.status = SystemVerilogVpiCallbackStatus::Removed;
        scheduler_handle = found->second.scheduler_handle;
        timed_handle = found->second.timed_handle;
    }

    if (timed_handle) {
        (void)impl_->time_service->cancel(timed_handle);
    }
    if (scheduler_handle) {
        impl_->scheduler->cancel(scheduler_handle);
    }
    return SystemVerilogVpiCallbackError::None;
}

SystemVerilogVpiCallbackError
SystemVerilogVpiCallbackManager::dispatch_lifecycle(
    const SystemVerilogVpiCallbackKind kind)
{
    if (!valid()) {
        return SystemVerilogVpiCallbackError::InvalidManager;
    }
    if (!is_lifecycle(kind)) {
        return SystemVerilogVpiCallbackError::InvalidKind;
    }

    std::vector<std::uint64_t> callbacks;
    {
        std::scoped_lock lock { impl_->mutex };
        for (const auto& [id, record] : impl_->records) {
            if (record.status == SystemVerilogVpiCallbackStatus::Active
                && record.kind == kind) {
                callbacks.push_back(id);
            }
        }
    }
    if (callbacks.empty()) {
        return SystemVerilogVpiCallbackError::None;
    }

    StableOrder order { };
    if (!checked_stable_order(*impl_, callbacks.front(), order)) {
        return SystemVerilogVpiCallbackError::ResourceLimit;
    }
    const std::weak_ptr<Impl> weak = impl_;
    try {
        impl_->scheduler->schedule(
            callback_phase(kind),
            order,
            [weak, callbacks, kind](Scheduler&) {
                if (const auto state = weak.lock()) {
                    (void)dispatch_callbacks(state, kind, callbacks);
                }
            });
    } catch (...) {
        return SystemVerilogVpiCallbackError::ScheduleFailure;
    }
    return SystemVerilogVpiCallbackError::None;
}

SystemVerilogVpiCallbackError
SystemVerilogVpiCallbackManager::dispatch_lifecycle_now(
    const SystemVerilogVpiCallbackKind kind)
{
    if (!valid()) {
        return SystemVerilogVpiCallbackError::InvalidManager;
    }
    if (!is_lifecycle(kind)) {
        return SystemVerilogVpiCallbackError::InvalidKind;
    }

    std::vector<std::uint64_t> callbacks;
    {
        std::scoped_lock lock { impl_->mutex };
        for (const auto& [id, record] : impl_->records) {
            if (record.status == SystemVerilogVpiCallbackStatus::Active
                && record.kind == kind) {
                callbacks.push_back(id);
            }
        }
    }
    return dispatch_callbacks(impl_, kind, callbacks)
        ? SystemVerilogVpiCallbackError::None
        : SystemVerilogVpiCallbackError::ReentrantDispatch;
}

SystemVerilogVpiCallbackError
SystemVerilogVpiCallbackManager::dispatch_named_event(
    const fsim_vpi_handle_v1 object)
{
    if (!valid()) {
        return SystemVerilogVpiCallbackError::InvalidManager;
    }
    const auto info = impl_->registry->lookup(object);
    if (!info || info.value->kind != SystemVerilogVpiObjectKind::NamedEvent) {
        return SystemVerilogVpiCallbackError::InvalidObject;
    }
    observe_value_change(impl_, object, std::nullopt);
    return SystemVerilogVpiCallbackError::None;
}

SystemVerilogVpiCallbackError
SystemVerilogVpiCallbackManager::dispatch_assertion(
    const fsim_vpi_handle_v1 object,
    SystemVerilogVpiAssertionEvent event)
{
    if (!valid()) {
        return SystemVerilogVpiCallbackError::InvalidManager;
    }
    const auto info = impl_->registry->lookup(object);
    if (!info || info.value->kind != SystemVerilogVpiObjectKind::Assertion) {
        return SystemVerilogVpiCallbackError::InvalidObject;
    }
    const auto kind
        = event.outcome == SystemVerilogVpiAssertionOutcome::Success
        ? SystemVerilogVpiCallbackKind::AssertionSuccess
        : event.outcome == SystemVerilogVpiAssertionOutcome::Failure
        ? SystemVerilogVpiCallbackKind::AssertionFailure
        : event.outcome == SystemVerilogVpiAssertionOutcome::Vacuous
        ? SystemVerilogVpiCallbackKind::AssertionVacuous
        : event.outcome == SystemVerilogVpiAssertionOutcome::Disabled
        ? SystemVerilogVpiCallbackKind::AssertionDisabled
        : SystemVerilogVpiCallbackKind::AssertionAborted;

    std::vector<std::uint64_t> callbacks;
    {
        std::scoped_lock lock { impl_->mutex };
        for (const auto& [id, record] : impl_->records) {
            if (record.status == SystemVerilogVpiCallbackStatus::Active
                && record.kind == kind && record.object == object) {
                callbacks.push_back(id);
            }
        }
    }
    return dispatch_callbacks(impl_, kind, callbacks, std::nullopt, event)
        ? SystemVerilogVpiCallbackError::None
        : SystemVerilogVpiCallbackError::ReentrantDispatch;
}

SystemVerilogVpiCallbackStatusResult
SystemVerilogVpiCallbackManager::status(
    const SystemVerilogVpiCallbackHandle handle) const
{
    if (!impl_ || !handle) {
        return status_failure(
            SystemVerilogVpiCallbackError::InvalidHandle);
    }
    if (handle.owner != impl_->owner) {
        return status_failure(
            SystemVerilogVpiCallbackError::CrossManager);
    }
    std::scoped_lock lock { impl_->mutex };
    const auto found = impl_->records.find(handle.id);
    if (found == impl_->records.end()) {
        return status_failure(SystemVerilogVpiCallbackError::NotFound);
    }
    return { found->second.status, SystemVerilogVpiCallbackError::None };
}

std::size_t SystemVerilogVpiCallbackManager::registrations() const
{
    if (!impl_) {
        return 0;
    }
    std::scoped_lock lock { impl_->mutex };
    return impl_->records.size();
}

bool SystemVerilogVpiCallbackManager::has_registrations() const noexcept
{
    return impl_
        && impl_->has_registrations.load(std::memory_order_acquire);
}

} // namespace fsim::runtime

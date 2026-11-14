// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler_events.hpp"
#include "fsim/runtime/vpi_callback.hpp"

#include "fsim/runtime/run_state_sections.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace fsim::runtime {

namespace {

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
        // A value change is reported right after it, before the update
        // region applies further nonblocking assignments of the time slot
        // (IEEE 1800-2023 38.36.1, 4.4.2).
        case SystemVerilogVpiCallbackKind::ValueChange:
        case SystemVerilogVpiCallbackKind::AfterDelay:
        case SystemVerilogVpiCallbackKind::NextTime:
        // Reaches its slot in the active region, then waits for the slot
        // to become quiescent.
        case SystemVerilogVpiCallbackKind::EndOfTimeSlot:
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
        /// SystemVerilogVpiCallbackRegistration::keeps_pending_values.
        bool keeps_pending_values { };
        /// The last change dispatch_pending delivered to it ahead of the
        /// change's own dispatch.
        std::uint64_t delivered_through { };
        /// SystemVerilogVpiCallbackRegistration::host_service.
        bool host_service { };
    };

    std::uint64_t owner { };
    std::uint64_t next_id { 1 };
    std::uint64_t observer { };
    StableOrder stable_order_base { };
    SystemVerilogVpiObjectRegistry* registry { };
    Scheduler* scheduler { };
    SystemVerilogVpiTimeService* time_service { };
    SystemVerilogVpiObservationPrepareHook prepare_observation;
    Scheduler::SlotStartHookToken slot_start_hook { };
    /// Callbacks that run when a slot at or after the given tick starts:
    /// cbNextSimTime and slot-start delays (IEEE 1800-2023 38.36.2).
    std::vector<std::pair<std::uint64_t, SimulationTick>> slot_start_waiting;
    mutable std::mutex mutex;
    /// Active registrations. A fired, failed or removed registration is
    /// retired: its record (and callable) is released and only its final
    /// status is kept, two bits per issued ID (0 while not retired), for
    /// status queries.
    std::map<std::uint64_t, Record> records;
    std::vector<std::uint8_t> retired_status;

    void set_retired(const std::uint64_t id, const SystemVerilogVpiCallbackStatus status)
    {
        const auto index = id - 1U;
        if (retired_status.size() <= index / 4U) {
            retired_status.resize(index / 4U + 1U, 0U);
        }
        const auto shift = static_cast<unsigned>(index % 4U) * 2U;
        auto& packed = retired_status[index / 4U];
        packed = static_cast<std::uint8_t>((packed & ~(3U << shift))
            | (static_cast<unsigned>(status) << shift));
    }

    [[nodiscard]] std::optional<SystemVerilogVpiCallbackStatus> retired_of(
        const std::uint64_t id) const
    {
        const auto index = id - 1U;
        if (id == 0U || index / 4U >= retired_status.size()) {
            return std::nullopt;
        }
        const auto code = (retired_status[index / 4U]
                              >> (static_cast<unsigned>(index % 4U) * 2U))
            & 3U;
        return code == 0U ? std::nullopt
                          : std::optional { static_cast<SystemVerilogVpiCallbackStatus>(code) };
    }
    std::size_t retired { };
    std::map<fsim_vpi_handle_v1, std::vector<std::uint64_t>>
        value_change_callbacks_by_object;
    /// The order of the value changes observed so far: their callbacks run
    /// in that order.
    std::uint64_t next_change { 1 };
    /// The objects with a registration that keeps pending values, and how
    /// many such registrations each has.
    std::map<fsim_vpi_handle_v1, std::uint32_t> pending_value_objects;
    /// Their observed changes not yet dispatched, in order, with the value
    /// of each.
    std::map<fsim_vpi_handle_v1,
        std::deque<std::pair<std::uint64_t,
            std::optional<SystemVerilogVpiStoredValue>>>>
        pending_values;
    std::array<bool,
        static_cast<std::size_t>(SystemVerilogVpiCallbackKind::EndOfRestart)
            + 1U>
        dispatching { };
    std::atomic_bool has_registrations { false };
    /// The same for the assertion reasons (cbAssertion*).
    std::atomic_bool has_assertion_registrations { false };
};

namespace {

    void unindex_value_change_callback(
        SystemVerilogVpiCallbackManager::Impl& state,
        const fsim_vpi_handle_v1 object,
        const std::uint64_t id)
    {
        const auto indexed = state.value_change_callbacks_by_object.find(object);
        if (indexed == state.value_change_callbacks_by_object.end()) {
            return;
        }
        const auto callback = std::lower_bound(
            indexed->second.begin(), indexed->second.end(), id);
        if (callback != indexed->second.end() && *callback == id) {
            indexed->second.erase(callback);
        }
        if (indexed->second.empty()) {
            state.value_change_callbacks_by_object.erase(indexed);
        }
    }

    void retire(
        SystemVerilogVpiCallbackManager::Impl& state,
        const std::map<std::uint64_t,
            SystemVerilogVpiCallbackManager::Impl::Record>::iterator found,
        const SystemVerilogVpiCallbackStatus status)
    {
        const auto id = found->first;
        if (found->second.kind == SystemVerilogVpiCallbackKind::ValueChange
            && found->second.object) {
            unindex_value_change_callback(state, *found->second.object, id);
            if (found->second.keeps_pending_values) {
                const auto counted
                    = state.pending_value_objects.find(*found->second.object);
                if (counted != state.pending_value_objects.end()
                    && --counted->second == 0U) {
                    state.pending_values.erase(counted->first);
                    state.pending_value_objects.erase(counted);
                }
            }
        }
        state.records.erase(found);
        state.set_retired(id, status);
        ++state.retired;
    }

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
            retire(*state, found,
                SystemVerilogVpiCallbackStatus::CallbackFailed);
        } else if (one_shot) {
            retire(*state, found, SystemVerilogVpiCallbackStatus::Fired);
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

    void dispatch_slot_start(
        const std::shared_ptr<SystemVerilogVpiCallbackManager::Impl>& state,
        const SimulationTick now)
    {
        std::vector<std::uint64_t> due;
        {
            std::scoped_lock lock { state->mutex };
            std::erase_if(state->slot_start_waiting, [&](const auto& entry) {
                if (entry.second > now) {
                    return false;
                }
                due.push_back(entry.first);
                return true;
            });
        }
        std::ranges::sort(due);
        for (const auto id : due) {
            dispatch_callback(state, id, std::nullopt);
        }
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

        // The changes of one region are updates in turn (IEEE 1800-2023
        // 4.4.2): a callback of an earlier change that registers on the
        // object of a later one sees that change, and one it removes does
        // not run, so the object's callbacks are those when its change is
        // dispatched, in the order of the changes.
        std::uint64_t change { };
        {
            std::scoped_lock lock { state->mutex };
            if (!state->value_change_callbacks_by_object.contains(object)) {
                return;
            }
            change = state->next_change++;
            if (!state->pending_value_objects.empty()
                && state->pending_value_objects.contains(object)) {
                try {
                    state->pending_values[object].emplace_back(change, value);
                } catch (...) {
                    // The change is still dispatched in its turn.
                }
            }
        }
        StableOrder order { };
        if (change > std::numeric_limits<StableOrder>::max()
                - state->stable_order_base) {
            return;
        }

        order = state->stable_order_base + change;
        // The SystemVerilog Active region of the change, ahead of the NBA
        // region of the time slot; the generic queue would run only after
        // the SystemVerilog regions settle (IEEE 1800-2023 4.4.2, 38.36.1).
        // A registration that received the change from dispatch_pending
        // does not receive it again.
        const auto current = [](SystemVerilogVpiCallbackManager::Impl& locked,
                                 const fsim_vpi_handle_v1 watched,
                                 const std::uint64_t number) {
            std::scoped_lock lock { locked.mutex };
            const auto indexed
                = locked.value_change_callbacks_by_object.find(watched);
            auto callbacks = indexed != locked.value_change_callbacks_by_object.end()
                ? indexed->second
                : std::vector<std::uint64_t> { };
            if (!locked.pending_values.empty()) {
                const auto pending = locked.pending_values.find(watched);
                if (pending != locked.pending_values.end()) {
                    while (!pending->second.empty()
                        && pending->second.front().first <= number) {
                        pending->second.pop_front();
                    }
                    std::erase_if(callbacks, [&](const std::uint64_t id) {
                        const auto found = locked.records.find(id);
                        return found != locked.records.end()
                            && found->second.delivered_through >= number;
                    });
                }
            }
            return callbacks;
        };
        try {
            state->scheduler->schedule_systemverilog(
                callback_phase(SystemVerilogVpiCallbackKind::ValueChange),
                order,
                [weak, object, value, change, current](Scheduler&) {
                    if (const auto locked = weak.lock()) {
                        const auto callbacks = current(*locked, object, change);
                        if (!callbacks.empty()) {
                            (void)dispatch_callbacks(
                                locked,
                                SystemVerilogVpiCallbackKind::ValueChange,
                                callbacks, value);
                        }
                    }
                });
        } catch (...) {
            const auto callbacks = current(*state, object, change);
            std::scoped_lock lock { state->mutex };
            for (const auto id : callbacks) {
                const auto found = state->records.find(id);
                if (found != state->records.end()) {
                    retire(*state, found,
                        SystemVerilogVpiCallbackStatus::CallbackFailed);
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
    : SystemVerilogVpiCallbackManager(
        registry, scheduler, time_service, stable_order_base, { })
{
}

SystemVerilogVpiCallbackManager::SystemVerilogVpiCallbackManager(
    SystemVerilogVpiObjectRegistry& registry,
    Scheduler& scheduler,
    SystemVerilogVpiTimeService& time_service,
    const StableOrder stable_order_base,
    SystemVerilogVpiObservationPrepareHook prepare_observation)
    : impl_(std::make_shared<Impl>())
{
    impl_->owner = next_callback_manager_owner.fetch_add(
        1, std::memory_order_relaxed);
    impl_->stable_order_base = stable_order_base;
    impl_->registry = &registry;
    impl_->scheduler = &scheduler;
    impl_->time_service = &time_service;
    impl_->prepare_observation = std::move(prepare_observation);
    const std::weak_ptr<Impl> weak = impl_;
    const auto observer = registry.add_value_observer(
        [weak](const fsim_vpi_handle_v1 object,
            const SystemVerilogVpiStoredValue& value) {
            observe_value_change(weak, object, value);
        });
    if (observer) {
        impl_->observer = *observer;
    }
    impl_->slot_start_hook = scheduler.add_slot_start_hook(
        [weak](Scheduler& owner) {
            if (const auto state = weak.lock()) {
                dispatch_slot_start(state, owner.now());
            }
        });
}

SystemVerilogVpiCallbackManager::~SystemVerilogVpiCallbackManager()
{
    if (!impl_) {
        return;
    }
    if (impl_->observer != 0U) {
        (void)impl_->registry->remove_value_observer(impl_->observer);
    }
    impl_->scheduler->remove_slot_start_hook(impl_->slot_start_hook);
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
    }

    // Preparation may query the manager or materialize host state. It must
    // finish before publishing the registration, and must not hold our lock.
    // Failure leaves no record, activity bit, ID, or scheduled callback behind.
    if (impl_->prepare_observation) {
        try {
            impl_->prepare_observation(registration.kind, registration.object);
        } catch (const std::bad_alloc&) {
            return registration_failure(SystemVerilogVpiCallbackError::ResourceLimit);
        } catch (...) {
            return registration_failure(
                SystemVerilogVpiCallbackError::ObservationPreparationFailure);
        }
    }

    std::scoped_lock lock { impl_->mutex };
    if (impl_->next_id == 0U) {
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
            registration.kind == SystemVerilogVpiCallbackKind::ValueChange
                && registration.keeps_pending_values,
            { },
            registration.host_service,
        });
    if (!inserted) {
        return registration_failure(
            SystemVerilogVpiCallbackError::ResourceLimit);
    }
    if (registration.kind
        == SystemVerilogVpiCallbackKind::ValueChange) {
        try {
            auto [indexed, created]
                = impl_->value_change_callbacks_by_object.try_emplace(
                    *registration.object);
            (void)created;
            try {
                indexed->second.push_back(id);
                if (registration.keeps_pending_values) {
                    ++impl_->pending_value_objects[*registration.object];
                }
            } catch (...) {
                if (indexed->second.empty()) {
                    impl_->value_change_callbacks_by_object.erase(indexed);
                }
                throw;
            }
        } catch (...) {
            impl_->records.erase(position);
            return registration_failure(
                SystemVerilogVpiCallbackError::ResourceLimit);
        }
    }
    impl_->has_registrations.store(true, std::memory_order_release);
    if (is_assertion(registration.kind)) {
        impl_->has_assertion_registrations.store(
            true, std::memory_order_release);
    }

    const std::weak_ptr<Impl> weak = impl_;
    try {
        // cbNextSimTime waits for the next slot to start, which need not be
        // queued yet; a slot-start delay also creates its slot.
        if (registration.kind == SystemVerilogVpiCallbackKind::NextTime
            || (registration.kind == SystemVerilogVpiCallbackKind::AfterDelay
                && registration.at_slot_start && scheduled_delay != 0U)) {
            const auto now = impl_->scheduler->now();
            if (registration.kind == SystemVerilogVpiCallbackKind::NextTime) {
                impl_->slot_start_waiting.emplace_back(id, now + 1U);
            } else {
                impl_->slot_start_waiting.emplace_back(id, now + scheduled_delay);
                position->second.scheduler_handle
                    = impl_->scheduler->schedule_after_cancelable(
                        scheduled_delay, SchedulerPhase::active, stable_order,
                        foreign_callback_task([](Scheduler&) { }));
            }
        } else if (registration.kind
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
            == SystemVerilogVpiCallbackKind::Synchronization) {
            // Pre-NBA (IEEE 1800-2023 4.4.3.3, 38.36.2): first in the
            // SystemVerilog NBA region, ahead of every nonblocking update.
            position->second.scheduler_handle
                = impl_->scheduler->schedule_systemverilog_after_cancelable(
                    scheduled_delay, SchedulerPhase::update,
                    nonblocking_region_first,
                    foreign_callback_task([weak, id](Scheduler&) {
                        if (const auto state = weak.lock()) {
                            dispatch_callback(state, id, std::nullopt);
                        }
                    }));
        } else if (registration.kind
            == SystemVerilogVpiCallbackKind::EndOfTimeSlot) {
            position->second.scheduler_handle
                = impl_->scheduler->schedule_after_cancelable(
                    scheduled_delay, SchedulerPhase::active, stable_order,
                    foreign_callback_task([weak, id, stable_order](
                                              Scheduler& scheduler) {
                        scheduler.schedule_end_of_time_slot(stable_order,
                            foreign_callback_task([weak, id](Scheduler&) {
                                if (const auto state = weak.lock()) {
                                    dispatch_callback(state, id, std::nullopt);
                                }
                            }));
                    }));
        } else if (registration.kind
                != SystemVerilogVpiCallbackKind::ValueChange
            && !is_assertion(registration.kind)
            && !is_lifecycle(registration.kind)) {
            position->second.scheduler_handle = impl_->scheduler->schedule_after_cancelable(
                scheduled_delay,
                callback_phase(registration.kind),
                stable_order,
                foreign_callback_task([weak, id](Scheduler&) {
                    if (const auto state = weak.lock()) {
                        dispatch_callback(state, id, std::nullopt);
                    }
                }));
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
            return impl_->retired_of(handle.id)
                ? SystemVerilogVpiCallbackError::NotActive
                : SystemVerilogVpiCallbackError::NotFound;
        }
        if (found->second.status
            != SystemVerilogVpiCallbackStatus::Active) {
            return SystemVerilogVpiCallbackError::NotActive;
        }
        scheduler_handle = found->second.scheduler_handle;
        timed_handle = found->second.timed_handle;
        retire(*impl_, found, SystemVerilogVpiCallbackStatus::Removed);
        std::erase_if(impl_->slot_start_waiting, [&](const auto& entry) {
            return entry.first == handle.id;
        });
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
SystemVerilogVpiCallbackManager::dispatch_pending(
    const SystemVerilogVpiCallbackHandle handle)
{
    if (!impl_ || !handle) {
        return SystemVerilogVpiCallbackError::InvalidHandle;
    }
    if (handle.owner != impl_->owner) {
        return SystemVerilogVpiCallbackError::CrossManager;
    }
    std::vector<std::optional<SystemVerilogVpiStoredValue>> values;
    {
        std::scoped_lock lock { impl_->mutex };
        const auto found = impl_->records.find(handle.id);
        if (found == impl_->records.end()
            || found->second.status != SystemVerilogVpiCallbackStatus::Active) {
            return SystemVerilogVpiCallbackError::NotActive;
        }
        if (!found->second.keeps_pending_values || !found->second.object) {
            return SystemVerilogVpiCallbackError::InvalidRequest;
        }
        const auto pending = impl_->pending_values.find(*found->second.object);
        if (pending != impl_->pending_values.end()) {
            for (const auto& [number, value] : pending->second) {
                if (number > found->second.delivered_through) {
                    values.push_back(value);
                    found->second.delivered_through = number;
                }
            }
        }
    }
    for (auto& value : values) {
        dispatch_callback(impl_, handle.id, std::move(value));
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
    const SystemVerilogVpiCallbackKind kind, const bool host_services_only)
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
                && record.kind == kind
                && (!host_services_only || record.host_service)) {
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
    if (!has_assertion_registrations()) {
        return SystemVerilogVpiCallbackError::None;
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
    if (found != impl_->records.end()) {
        return { found->second.status, SystemVerilogVpiCallbackError::None };
    }
    if (const auto retired = impl_->retired_of(handle.id)) {
        return { *retired, SystemVerilogVpiCallbackError::None };
    }
    return status_failure(SystemVerilogVpiCallbackError::NotFound);
}

std::size_t SystemVerilogVpiCallbackManager::registrations() const
{
    if (!impl_) {
        return 0;
    }
    std::scoped_lock lock { impl_->mutex };
    return impl_->records.size() + impl_->retired;
}

void SystemVerilogVpiCallbackManager::audit_rest_state(
    std::vector<std::string>& messages) const
{
    if (!impl_) {
        return;
    }
    // A callback is delivered synchronously inside dispatch_callbacks, which
    // sets the kind's guard around it; the save point is outside every
    // delivery, so a guard still set names a delivery the file would lose.
    FSIM_RS_IDLE("foreign.vpi_callbacks", dispatching);
    std::scoped_lock lock { impl_->mutex };
    for (std::size_t kind = 0U; kind < impl_->dispatching.size(); ++kind) {
        if (impl_->dispatching[kind]) {
            messages.push_back("FSIM-SAVE-0011: VPI callbacks of kind "
                + std::to_string(kind)
                + " are being delivered at the save point");
        }
    }
}

bool SystemVerilogVpiCallbackManager::has_registrations() const noexcept
{
    return impl_
        && impl_->has_registrations.load(std::memory_order_acquire);
}

bool SystemVerilogVpiCallbackManager::has_assertion_registrations()
    const noexcept
{
    return impl_
        && impl_->has_assertion_registrations.load(std::memory_order_acquire);
}

} // namespace fsim::runtime

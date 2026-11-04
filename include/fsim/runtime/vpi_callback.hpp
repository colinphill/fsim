// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_object.hpp"
#include "fsim/runtime/vpi_time.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace fsim::runtime {

enum class SystemVerilogVpiCallbackKind {
    ValueChange,
    AssertionSuccess,
    AssertionFailure,
    AssertionVacuous,
    AssertionDisabled,
    AssertionAborted,
    AfterDelay,
    ReadWrite,
    ReadOnly,
    NextTime,
    // Immediately before the nonblocking assignments (IEEE 1800-2023 4.4.3.3
    // Pre-NBA, cbNBASynch).
    Synchronization,
    // After every region of the time slot but the postponed one (IEEE
    // 1800-2023 4.4.3.9 Pre-Postponed, cbAtEndOfSimTime).
    EndOfTimeSlot,
    StartOfSimulation,
    EndOfSimulation,
    StartOfReset,
    EndOfReset,
    StartOfSave,
    EndOfSave,
    StartOfRestart,
    EndOfRestart,
};

enum class SystemVerilogVpiAssertionKind {
    Assertion,
    Assumption,
    Cover,
    Restriction,
};

enum class SystemVerilogVpiAssertionOutcome {
    Success,
    Failure,
    Vacuous,
    Disabled,
    Aborted,
};

struct SystemVerilogVpiAssertionEvent {
    SystemVerilogVpiAssertionKind kind {
        SystemVerilogVpiAssertionKind::Assertion
    };
    SystemVerilogVpiAssertionOutcome outcome {
        SystemVerilogVpiAssertionOutcome::Success
    };
    std::string name;
    std::string process;
    std::string instance_identity;
    std::uint32_t slot { };
    std::uint32_t source_span { };
    bool action_suppressed { };
    // The tick the attempt started at (IEEE 1800-2023 39.4.2).
    std::uint64_t attempt_start { };
};

enum class SystemVerilogVpiCallbackError {
    None,
    InvalidManager,
    InvalidKind,
    InvalidRequest,
    InvalidObject,
    CrossSimulation,
    NoFutureTime,
    ResourceLimit,
    ScheduleFailure,
    InvalidHandle,
    CrossManager,
    NotFound,
    NotActive,
    ReentrantDispatch,
    ObservationPreparationFailure,
};

enum class SystemVerilogVpiCallbackStatus {
    Active,
    Fired,
    CallbackFailed,
    Removed,
};

struct SystemVerilogVpiCallbackHandle {
    std::uint64_t owner { };
    std::uint64_t id { };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owner != 0U && id != 0U;
    }

    friend bool operator==(
        const SystemVerilogVpiCallbackHandle&,
        const SystemVerilogVpiCallbackHandle&) = default;
};

struct SystemVerilogVpiCallbackEvent {
    SystemVerilogVpiCallbackHandle registration;
    SystemVerilogVpiCallbackKind kind {
        SystemVerilogVpiCallbackKind::ValueChange
    };
    std::optional<fsim_vpi_handle_v1> object;
    SystemVerilogVpiTimeQueryResult time;
    std::optional<SystemVerilogVpiStoredValue> value;
    std::optional<SystemVerilogVpiAssertionEvent> assertion;
    std::uint64_t user_data { };
    std::uint64_t registration_order { };
    std::uint64_t simulation_identity { };
};

using SystemVerilogVpiCallback = std::function<void(const SystemVerilogVpiCallbackEvent&)>;

struct SystemVerilogVpiCallbackRegistration {
    SystemVerilogVpiCallbackKind kind {
        SystemVerilogVpiCallbackKind::ValueChange
    };
    std::optional<fsim_vpi_handle_v1> object;
    std::optional<SystemVerilogVpiTimeValue> delay;
    std::uint64_t user_data { };
    SystemVerilogVpiCallback callback;
    /// An AfterDelay callback with a nonzero delay runs before any event of
    /// its time slot (cbAfterDelay, cbAtStartOfSimTime; IEEE 1800-2023
    /// 38.36.2).
    bool at_slot_start { };
    /// A value change registration whose object's changes keep their values
    /// until dispatched, for dispatch_pending. Only ACC links ask for it, so
    /// other registrations pay nothing for it.
    bool keeps_pending_values { };
};

struct SystemVerilogVpiCallbackRegistrationResult {
    SystemVerilogVpiCallbackHandle value;
    SystemVerilogVpiCallbackError error {
        SystemVerilogVpiCallbackError::None
    };
    SystemVerilogVpiObjectError object_error {
        SystemVerilogVpiObjectError::None
    };
    SystemVerilogVpiTimeError time_error {
        SystemVerilogVpiTimeError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCallbackError::None
            && static_cast<bool>(value);
    }
};

struct SystemVerilogVpiCallbackStatusResult {
    SystemVerilogVpiCallbackStatus status {
        SystemVerilogVpiCallbackStatus::Active
    };
    SystemVerilogVpiCallbackError error {
        SystemVerilogVpiCallbackError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCallbackError::None;
    }
};

/// Synchronously materialize and invalidate affected compiled state before a
/// registration becomes visible. Called without the manager lock; the host is
/// responsible for executing this on its simulation owner thread.
using SystemVerilogVpiObservationPrepareHook = std::function<void(
    SystemVerilogVpiCallbackKind, std::optional<fsim_vpi_handle_v1>)>;

class SystemVerilogVpiCallbackManager final {
public:
    SystemVerilogVpiCallbackManager(
        SystemVerilogVpiObjectRegistry& registry,
        Scheduler& scheduler,
        SystemVerilogVpiTimeService& time_service,
        StableOrder stable_order_base = 0);
    SystemVerilogVpiCallbackManager(
        SystemVerilogVpiObjectRegistry& registry,
        Scheduler& scheduler,
        SystemVerilogVpiTimeService& time_service,
        StableOrder stable_order_base,
        SystemVerilogVpiObservationPrepareHook prepare_observation);
    ~SystemVerilogVpiCallbackManager();

    SystemVerilogVpiCallbackManager(
        const SystemVerilogVpiCallbackManager&) = delete;
    SystemVerilogVpiCallbackManager& operator=(
        const SystemVerilogVpiCallbackManager&) = delete;
    SystemVerilogVpiCallbackManager(
        SystemVerilogVpiCallbackManager&&) = delete;
    SystemVerilogVpiCallbackManager& operator=(
        SystemVerilogVpiCallbackManager&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] SystemVerilogVpiCallbackRegistrationResult register_callback(
        SystemVerilogVpiCallbackRegistration registration);
    [[nodiscard]] SystemVerilogVpiCallbackStatusResult status(
        SystemVerilogVpiCallbackHandle handle) const;
    [[nodiscard]] SystemVerilogVpiCallbackError remove_callback(
        SystemVerilogVpiCallbackHandle handle);
    /// Deliver to a value change registration that keeps pending values,
    /// now, the changes of its object observed and not yet dispatched; their
    /// own dispatch then skips it (an ACC value change link deleted after a
    /// change, IEEE 1364-2001 23.97, 23.98).
    [[nodiscard]] SystemVerilogVpiCallbackError dispatch_pending(
        SystemVerilogVpiCallbackHandle handle);
    [[nodiscard]] SystemVerilogVpiCallbackError dispatch_lifecycle(
        SystemVerilogVpiCallbackKind kind);
    /// Invoke active lifecycle callbacks immediately at the scheduler's current
    /// time and delta. Integrated kernels use this at lifecycle boundaries that
    /// must precede or follow all HDL process execution.
    [[nodiscard]] SystemVerilogVpiCallbackError dispatch_lifecycle_now(
        SystemVerilogVpiCallbackKind kind);
    /// Notify ValueChange registrations that a named event was delivered. The
    /// callback event intentionally has no stored value payload.
    [[nodiscard]] SystemVerilogVpiCallbackError dispatch_named_event(
        fsim_vpi_handle_v1 object);
    /// Publish one completed assertion attempt to the matching persistent
    /// assertion registration at the current scheduler coordinate.
    [[nodiscard]] SystemVerilogVpiCallbackError dispatch_assertion(
        fsim_vpi_handle_v1 object,
        SystemVerilogVpiAssertionEvent event);
    [[nodiscard]] std::size_t registrations() const;
    /// Lock-free conservative activity bit for signal publication hot paths.
    /// Once any registration succeeds it remains true for the manager's
    /// lifetime, including after a one-shot callback fires or is removed.
    [[nodiscard]] bool has_registrations() const noexcept;

    // Public only so translation-unit helpers can name the opaque state type.
    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

} // namespace fsim::runtime

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
    AfterDelay,
    ReadWrite,
    ReadOnly,
    NextTime,
    Synchronization,
    StartOfSimulation,
    EndOfSimulation,
    StartOfReset,
    EndOfReset,
    StartOfSave,
    EndOfSave,
    StartOfRestart,
    EndOfRestart,
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

class SystemVerilogVpiCallbackManager final {
public:
    SystemVerilogVpiCallbackManager(
        SystemVerilogVpiObjectRegistry& registry,
        Scheduler& scheduler,
        SystemVerilogVpiTimeService& time_service,
        StableOrder stable_order_base = 0);
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
    [[nodiscard]] std::size_t registrations() const;

    // Public only so translation-unit helpers can name the opaque state type.
    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

} // namespace fsim::runtime

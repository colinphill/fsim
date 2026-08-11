// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_callback.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace fsim::runtime {

enum class SystemVerilogVpiControlOperation {
    Stop,
    Finish,
    Reset,
    Interactive,
    Force,
    Release,
};

enum class SystemVerilogVpiControlState {
    Running,
    Stopped,
    Finished,
    Reset,
    Interactive,
};

enum class SystemVerilogVpiControlStatus {
    Pending,
    Applied,
    Failed,
};

enum class SystemVerilogVpiControlError {
    None,
    InvalidControl,
    InvalidOperation,
    InvalidRequest,
    InvalidObject,
    CrossSimulation,
    InvalidHandle,
    CrossControl,
    NotFound,
    NotRunning,
    NotStopped,
    Terminal,
    ResourceLimit,
    ScheduleFailure,
};

struct SystemVerilogVpiControlHandle {
    std::uint64_t owner { };
    std::uint64_t id { };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owner != 0U && id != 0U;
    }

    friend bool operator==(
        const SystemVerilogVpiControlHandle&,
        const SystemVerilogVpiControlHandle&) = default;
};

struct SystemVerilogVpiControlRequest {
    SystemVerilogVpiControlOperation operation {
        SystemVerilogVpiControlOperation::Stop
    };
    std::optional<fsim_vpi_handle_v1> object;
    std::optional<SystemVerilogVpiStoredValue> value;
};

struct SystemVerilogVpiControlSubmitResult {
    SystemVerilogVpiControlHandle value;
    SystemVerilogVpiControlError error {
        SystemVerilogVpiControlError::None
    };
    SystemVerilogVpiObjectError object_error {
        SystemVerilogVpiObjectError::None
    };
    SystemVerilogVpiValueError value_error {
        SystemVerilogVpiValueError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiControlError::None
            && static_cast<bool>(value);
    }
};

struct SystemVerilogVpiControlStatusResult {
    SystemVerilogVpiControlStatus status {
        SystemVerilogVpiControlStatus::Pending
    };
    SystemVerilogVpiValueError value_error {
        SystemVerilogVpiValueError::None
    };
    SystemVerilogVpiControlError error {
        SystemVerilogVpiControlError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiControlError::None;
    }
};

class SystemVerilogVpiControlService final {
public:
    SystemVerilogVpiControlService(
        SystemVerilogVpiObjectRegistry& registry,
        Scheduler& scheduler,
        SystemVerilogVpiCallbackManager& callbacks,
        StableOrder stable_order_base = 0,
        bool dispatch_finish_lifecycle = true,
        bool reset_supported = true);

    SystemVerilogVpiControlService(
        const SystemVerilogVpiControlService&) = delete;
    SystemVerilogVpiControlService& operator=(
        const SystemVerilogVpiControlService&) = delete;
    SystemVerilogVpiControlService(
        SystemVerilogVpiControlService&&) = delete;
    SystemVerilogVpiControlService& operator=(
        SystemVerilogVpiControlService&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
    [[nodiscard]] SystemVerilogVpiControlSubmitResult submit(
        SystemVerilogVpiControlRequest request);
    [[nodiscard]] SystemVerilogVpiControlStatusResult status(
        SystemVerilogVpiControlHandle handle) const;
    [[nodiscard]] SystemVerilogVpiControlState state() const;
    [[nodiscard]] SystemVerilogVpiControlError resume();
    /// Mark natural kernel completion without scheduling another stop request.
    void mark_finished() noexcept;
    [[nodiscard]] std::size_t operations() const;

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

} // namespace fsim::runtime

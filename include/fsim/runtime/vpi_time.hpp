// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace fsim::runtime {

enum class SystemVerilogVpiTimeFormat {
    IntegerTicks,
    ScaledReal,
};

struct SystemVerilogVpiTimeProfile {
    std::int8_t unit_exponent { -9 };
    std::int8_t precision_exponent { -12 };
    /// Number of precision quanta represented by one scheduler tick.
    std::uint64_t tick_multiplier { 1 };

    friend bool operator==(
        const SystemVerilogVpiTimeProfile&,
        const SystemVerilogVpiTimeProfile&) = default;
};

struct SystemVerilogVpiTimeValue {
    SystemVerilogVpiTimeFormat format {
        SystemVerilogVpiTimeFormat::IntegerTicks
    };
    std::uint32_t high { };
    std::uint32_t low { };
    double scaled_real { };
};

enum class SystemVerilogVpiTimeError {
    None,
    InvalidProfile,
    InvalidFormat,
    Negative,
    NonFinite,
    Overflow,
    InvalidHandle,
    CrossService,
    NotFound,
    NotPending,
    ResourceLimit,
};

struct SystemVerilogVpiTimeConversionResult {
    SimulationTick ticks { };
    SystemVerilogVpiTimeError error { SystemVerilogVpiTimeError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiTimeError::None;
    }
};

struct SystemVerilogVpiTimeQueryResult {
    SystemVerilogVpiTimeValue value;
    SystemVerilogVpiTimeProfile profile;
    SimulationTick ticks { };
    std::uint64_t delta { };
    SystemVerilogVpiTimeError error { SystemVerilogVpiTimeError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiTimeError::None;
    }
};

enum class SystemVerilogVpiTimedStatus {
    Pending,
    Fired,
    Cancelled,
    CallbackFailed,
};

struct SystemVerilogVpiTimedHandle {
    std::uint64_t owner { };
    std::uint64_t id { };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owner != 0U && id != 0U;
    }

    friend bool operator==(
        const SystemVerilogVpiTimedHandle&,
        const SystemVerilogVpiTimedHandle&) = default;
};

struct SystemVerilogVpiTimedScheduleResult {
    SystemVerilogVpiTimedHandle value;
    SystemVerilogVpiTimeError error { SystemVerilogVpiTimeError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiTimeError::None
            && static_cast<bool>(value);
    }
};

struct SystemVerilogVpiTimedStatusResult {
    SystemVerilogVpiTimedStatus status {
        SystemVerilogVpiTimedStatus::Pending
    };
    SystemVerilogVpiTimeError error { SystemVerilogVpiTimeError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiTimeError::None;
    }
};

class SystemVerilogVpiTimeService final {
public:
    using Callback = std::function<void(const SystemVerilogVpiTimeQueryResult&)>;

    SystemVerilogVpiTimeService(
        Scheduler& scheduler,
        SystemVerilogVpiTimeProfile profile);
    ~SystemVerilogVpiTimeService();

    SystemVerilogVpiTimeService(
        const SystemVerilogVpiTimeService&) = delete;
    SystemVerilogVpiTimeService& operator=(
        const SystemVerilogVpiTimeService&) = delete;
    SystemVerilogVpiTimeService(
        SystemVerilogVpiTimeService&&) = delete;
    SystemVerilogVpiTimeService& operator=(
        SystemVerilogVpiTimeService&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] SystemVerilogVpiTimeProfile profile() const noexcept;
    [[nodiscard]] SystemVerilogVpiTimeConversionResult convert_delay(
        const SystemVerilogVpiTimeValue& delay) const noexcept;
    [[nodiscard]] SystemVerilogVpiTimeQueryResult query(
        SystemVerilogVpiTimeFormat format) const noexcept;

    [[nodiscard]] SystemVerilogVpiTimedScheduleResult schedule(
        const SystemVerilogVpiTimeValue& delay,
        StableOrder stable_order,
        Callback callback);
    [[nodiscard]] SystemVerilogVpiTimeError cancel(
        SystemVerilogVpiTimedHandle handle);
    [[nodiscard]] SystemVerilogVpiTimedStatusResult status(
        SystemVerilogVpiTimedHandle handle) const;
    [[nodiscard]] SystemVerilogVpiTimeError release(
        SystemVerilogVpiTimedHandle handle);
    [[nodiscard]] std::size_t pending() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace fsim::runtime

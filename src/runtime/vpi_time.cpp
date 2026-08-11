// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_time.hpp"

#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace fsim::runtime {

namespace {

    constexpr std::size_t maximum_timed_callbacks = 65'536;
    std::atomic<std::uint64_t> next_time_service_owner { 1 };

    bool valid_profile(const SystemVerilogVpiTimeProfile profile)
    {
        const auto unit = static_cast<int>(profile.unit_exponent);
        const auto precision = static_cast<int>(profile.precision_exponent);
        return unit <= 0 && unit >= -15
            && precision <= unit && precision >= -15
            && profile.tick_multiplier != 0U;
    }

    bool valid_format(const SystemVerilogVpiTimeFormat format)
    {
        return static_cast<unsigned>(format)
            <= static_cast<unsigned>(
                SystemVerilogVpiTimeFormat::ScaledReal);
    }

    std::uint64_t scale_for(
        const SystemVerilogVpiTimeProfile profile)
    {
        const auto difference = static_cast<int>(profile.unit_exponent)
            - static_cast<int>(profile.precision_exponent);
        std::uint64_t scale = 1;
        for (int digit = 0; digit < difference; ++digit) {
            scale *= 10U;
        }
        return scale;
    }

    SystemVerilogVpiTimedScheduleResult schedule_failure(
        const SystemVerilogVpiTimeError error)
    {
        SystemVerilogVpiTimedScheduleResult result;
        result.error = error;
        return result;
    }

    SystemVerilogVpiTimedStatusResult status_failure(
        const SystemVerilogVpiTimeError error)
    {
        SystemVerilogVpiTimedStatusResult result;
        result.error = error;
        return result;
    }

} // namespace

struct SystemVerilogVpiTimeService::Impl {
    struct Record {
        SystemVerilogVpiTimedStatus status {
            SystemVerilogVpiTimedStatus::Pending
        };
        ScheduledTaskHandle scheduler_handle;
        Callback callback;
    };

    std::uint64_t owner { };
    std::uint64_t next_id { 1 };
    Scheduler* scheduler { };
    SystemVerilogVpiTimeProfile profile;
    bool valid { };
    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, Record> records;
};

SystemVerilogVpiTimeService::SystemVerilogVpiTimeService(
    Scheduler& scheduler,
    const SystemVerilogVpiTimeProfile profile)
    : impl_(std::make_shared<Impl>())
{
    impl_->owner = next_time_service_owner.fetch_add(1, std::memory_order_relaxed);
    impl_->scheduler = &scheduler;
    impl_->profile = profile;
    impl_->valid = valid_profile(profile) && impl_->owner != 0U;
}

SystemVerilogVpiTimeService::~SystemVerilogVpiTimeService()
{
    if (!impl_) {
        return;
    }
    std::scoped_lock lock { impl_->mutex };
    for (auto& [id, record] : impl_->records) {
        (void)id;
        if (record.status == SystemVerilogVpiTimedStatus::Pending) {
            impl_->scheduler->cancel(record.scheduler_handle);
            record.status = SystemVerilogVpiTimedStatus::Cancelled;
        }
    }
}

bool SystemVerilogVpiTimeService::valid() const noexcept
{
    return impl_ && impl_->valid;
}

SystemVerilogVpiTimeProfile
SystemVerilogVpiTimeService::profile() const noexcept
{
    return impl_ ? impl_->profile : SystemVerilogVpiTimeProfile { };
}

SystemVerilogVpiTimeConversionResult
SystemVerilogVpiTimeService::convert_delay(
    const SystemVerilogVpiTimeValue& delay) const noexcept
{
    if (!valid()) {
        return { { }, SystemVerilogVpiTimeError::InvalidProfile };
    }
    if (!valid_format(delay.format)) {
        return { { }, SystemVerilogVpiTimeError::InvalidFormat };
    }
    if (delay.format == SystemVerilogVpiTimeFormat::IntegerTicks) {
        const auto precision_ticks
            = (static_cast<std::uint64_t>(delay.high) << 32U)
            | static_cast<std::uint64_t>(delay.low);
        const auto quotient
            = precision_ticks / impl_->profile.tick_multiplier;
        const auto remainder
            = precision_ticks % impl_->profile.tick_multiplier;
        const auto round_up = remainder
            >= impl_->profile.tick_multiplier / 2U
                + impl_->profile.tick_multiplier % 2U;
        if (round_up
            && quotient == std::numeric_limits<SimulationTick>::max()) {
            return { { }, SystemVerilogVpiTimeError::Overflow };
        }
        return { quotient + static_cast<SimulationTick>(round_up), { } };
    }
    if (!std::isfinite(delay.scaled_real)) {
        return { { }, SystemVerilogVpiTimeError::NonFinite };
    }
    if (delay.scaled_real < 0.0) {
        return { { }, SystemVerilogVpiTimeError::Negative };
    }

    const auto scaled = static_cast<long double>(delay.scaled_real)
        * static_cast<long double>(scale_for(impl_->profile))
        / static_cast<long double>(impl_->profile.tick_multiplier);
    const auto rounded = std::floor(scaled + 0.5L);
    if (rounded
        > static_cast<long double>(
            std::numeric_limits<SimulationTick>::max())) {
        return { { }, SystemVerilogVpiTimeError::Overflow };
    }
    return { static_cast<SimulationTick>(rounded), { } };
}

SystemVerilogVpiTimeQueryResult SystemVerilogVpiTimeService::query(
    const SystemVerilogVpiTimeFormat format) const noexcept
{
    SystemVerilogVpiTimeQueryResult result;
    if (!valid()) {
        result.error = SystemVerilogVpiTimeError::InvalidProfile;
        return result;
    }
    if (!valid_format(format)) {
        result.error = SystemVerilogVpiTimeError::InvalidFormat;
        return result;
    }
    result.profile = impl_->profile;
    result.ticks = impl_->scheduler->now();
    result.delta = impl_->scheduler->delta();
    result.value.format = format;
    if (format == SystemVerilogVpiTimeFormat::IntegerTicks) {
        if (result.ticks
            > std::numeric_limits<std::uint64_t>::max()
                / impl_->profile.tick_multiplier) {
            result.error = SystemVerilogVpiTimeError::Overflow;
            return result;
        }
        const auto precision_ticks
            = result.ticks * impl_->profile.tick_multiplier;
        result.value.high = static_cast<std::uint32_t>(precision_ticks >> 32U);
        result.value.low = static_cast<std::uint32_t>(
            precision_ticks & 0xffffffffULL);
    } else {
        result.value.scaled_real = static_cast<double>(result.ticks)
            * static_cast<double>(impl_->profile.tick_multiplier)
            / static_cast<double>(scale_for(impl_->profile));
    }
    return result;
}

SystemVerilogVpiTimedScheduleResult
SystemVerilogVpiTimeService::schedule(
    const SystemVerilogVpiTimeValue& delay,
    const StableOrder stable_order,
    Callback callback)
{
    if (!valid()) {
        return schedule_failure(
            SystemVerilogVpiTimeError::InvalidProfile);
    }
    if (!callback) {
        return schedule_failure(
            SystemVerilogVpiTimeError::InvalidHandle);
    }
    const auto converted = convert_delay(delay);
    if (!converted) {
        return schedule_failure(converted.error);
    }
    if (converted.ticks
        > std::numeric_limits<SimulationTick>::max()
            - impl_->scheduler->now()) {
        return schedule_failure(SystemVerilogVpiTimeError::Overflow);
    }

    std::scoped_lock lock { impl_->mutex };
    if (impl_->records.size() >= maximum_timed_callbacks
        || impl_->next_id == 0U) {
        return schedule_failure(
            SystemVerilogVpiTimeError::ResourceLimit);
    }
    const auto id = impl_->next_id++;
    auto [position, inserted] = impl_->records.emplace(
        id,
        Impl::Record {
            SystemVerilogVpiTimedStatus::Pending,
            { },
            std::move(callback),
        });
    if (!inserted) {
        return schedule_failure(
            SystemVerilogVpiTimeError::ResourceLimit);
    }

    const std::weak_ptr<Impl> weak = impl_;
    try {
        position->second.scheduler_handle = impl_->scheduler->schedule_after_cancelable(
            converted.ticks,
            SchedulerPhase::active,
            stable_order,
            [weak, id](Scheduler&) {
                const auto state = weak.lock();
                if (!state) {
                    return;
                }
                Callback timed_callback;
                {
                    std::scoped_lock callback_lock { state->mutex };
                    const auto found = state->records.find(id);
                    if (found == state->records.end()
                        || found->second.status
                            != SystemVerilogVpiTimedStatus::Pending) {
                        return;
                    }
                    timed_callback = found->second.callback;
                }

                SystemVerilogVpiTimeQueryResult query;
                query.profile = state->profile;
                query.ticks = state->scheduler->now();
                query.delta = state->scheduler->delta();
                query.value.format = SystemVerilogVpiTimeFormat::IntegerTicks;
                query.value.high = static_cast<std::uint32_t>(query.ticks >> 32U);
                query.value.low = static_cast<std::uint32_t>(
                    query.ticks & 0xffffffffULL);
                bool failed { };
                try {
                    timed_callback(query);
                } catch (...) {
                    failed = true;
                }

                std::scoped_lock callback_lock { state->mutex };
                const auto found = state->records.find(id);
                if (found == state->records.end()
                    || found->second.status
                        != SystemVerilogVpiTimedStatus::Pending) {
                    return;
                }
                found->second.status = failed
                    ? SystemVerilogVpiTimedStatus::CallbackFailed
                    : SystemVerilogVpiTimedStatus::Fired;
            });
    } catch (...) {
        impl_->records.erase(position);
        return schedule_failure(
            SystemVerilogVpiTimeError::ResourceLimit);
    }

    SystemVerilogVpiTimedScheduleResult result;
    result.value = { impl_->owner, id };
    return result;
}

SystemVerilogVpiTimeError SystemVerilogVpiTimeService::cancel(
    const SystemVerilogVpiTimedHandle handle)
{
    if (!valid()) {
        return SystemVerilogVpiTimeError::InvalidProfile;
    }
    if (!handle) {
        return SystemVerilogVpiTimeError::InvalidHandle;
    }
    if (handle.owner != impl_->owner) {
        return SystemVerilogVpiTimeError::CrossService;
    }
    std::scoped_lock lock { impl_->mutex };
    const auto found = impl_->records.find(handle.id);
    if (found == impl_->records.end()) {
        return SystemVerilogVpiTimeError::NotFound;
    }
    if (found->second.status != SystemVerilogVpiTimedStatus::Pending) {
        return SystemVerilogVpiTimeError::NotPending;
    }
    impl_->scheduler->cancel(found->second.scheduler_handle);
    found->second.status = SystemVerilogVpiTimedStatus::Cancelled;
    return SystemVerilogVpiTimeError::None;
}

SystemVerilogVpiTimedStatusResult SystemVerilogVpiTimeService::status(
    const SystemVerilogVpiTimedHandle handle) const
{
    if (!valid()) {
        return status_failure(
            SystemVerilogVpiTimeError::InvalidProfile);
    }
    if (!handle) {
        return status_failure(
            SystemVerilogVpiTimeError::InvalidHandle);
    }
    if (handle.owner != impl_->owner) {
        return status_failure(
            SystemVerilogVpiTimeError::CrossService);
    }
    std::scoped_lock lock { impl_->mutex };
    const auto found = impl_->records.find(handle.id);
    if (found == impl_->records.end()) {
        return status_failure(SystemVerilogVpiTimeError::NotFound);
    }
    SystemVerilogVpiTimedStatusResult result;
    result.status = found->second.status;
    return result;
}

SystemVerilogVpiTimeError SystemVerilogVpiTimeService::release(
    const SystemVerilogVpiTimedHandle handle)
{
    if (!valid()) {
        return SystemVerilogVpiTimeError::InvalidProfile;
    }
    if (!handle) {
        return SystemVerilogVpiTimeError::InvalidHandle;
    }
    if (handle.owner != impl_->owner) {
        return SystemVerilogVpiTimeError::CrossService;
    }
    std::scoped_lock lock { impl_->mutex };
    const auto found = impl_->records.find(handle.id);
    if (found == impl_->records.end()) {
        return SystemVerilogVpiTimeError::NotFound;
    }
    if (found->second.status == SystemVerilogVpiTimedStatus::Pending) {
        return SystemVerilogVpiTimeError::NotPending;
    }
    impl_->records.erase(found);
    return SystemVerilogVpiTimeError::None;
}

std::size_t SystemVerilogVpiTimeService::pending() const
{
    if (!valid()) {
        return 0;
    }
    std::scoped_lock lock { impl_->mutex };
    std::size_t result { };
    for (const auto& [id, record] : impl_->records) {
        (void)id;
        result += record.status == SystemVerilogVpiTimedStatus::Pending;
    }
    return result;
}

} // namespace fsim::runtime

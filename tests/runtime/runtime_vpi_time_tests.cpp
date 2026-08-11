// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_time.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {

namespace {

    using fsim::runtime::RunStatus;
    using fsim::runtime::Scheduler;
    using fsim::runtime::SchedulerPhase;
    using fsim::runtime::SystemVerilogVpiTimedStatus;
    using fsim::runtime::SystemVerilogVpiTimeError;
    using fsim::runtime::SystemVerilogVpiTimeFormat;
    using fsim::runtime::SystemVerilogVpiTimeProfile;
    using fsim::runtime::SystemVerilogVpiTimeQueryResult;
    using fsim::runtime::SystemVerilogVpiTimeService;
    using fsim::runtime::SystemVerilogVpiTimeValue;

    void require_vpi_time(const bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    SystemVerilogVpiTimeValue scaled_delay(const double value)
    {
        SystemVerilogVpiTimeValue result;
        result.format = SystemVerilogVpiTimeFormat::ScaledReal;
        result.scaled_real = value;
        return result;
    }

    SystemVerilogVpiTimeValue tick_delay(const std::uint64_t value)
    {
        SystemVerilogVpiTimeValue result;
        result.high = static_cast<std::uint32_t>(value >> 32U);
        result.low = static_cast<std::uint32_t>(value & 0xffffffffULL);
        return result;
    }

} // namespace

void test_systemverilog_vpi_time_service()
{
    Scheduler scheduler;
    SystemVerilogVpiTimeService service {
        scheduler, SystemVerilogVpiTimeProfile { -9, -12 }
    };
    SystemVerilogVpiTimeService invalid_precision {
        scheduler, SystemVerilogVpiTimeProfile { -9, -6 }
    };
    SystemVerilogVpiTimeService invalid_range {
        scheduler, SystemVerilogVpiTimeProfile { -9, -16 }
    };
    require_vpi_time(
        service.valid() && !invalid_precision.valid()
            && !invalid_range.valid()
            && invalid_precision.query(
                                    SystemVerilogVpiTimeFormat::IntegerTicks)
                    .error
                == SystemVerilogVpiTimeError::InvalidProfile,
        "VPI time profiles require precision at or below units within femtoseconds");

    const auto initial_integer = service.query(SystemVerilogVpiTimeFormat::IntegerTicks);
    const auto initial_real = service.query(SystemVerilogVpiTimeFormat::ScaledReal);
    require_vpi_time(
        initial_integer && initial_real
            && initial_integer.ticks == 0
            && initial_integer.value.high == 0
            && initial_integer.value.low == 0
            && initial_integer.delta == 0
            && initial_real.value.scaled_real == 0.0
            && initial_real.profile
                == SystemVerilogVpiTimeProfile { -9, -12 },
        "VPI time queries expose exact units, precision, integer ticks, scaled real, and delta identity");

    const auto integer = service.convert_delay(tick_delay(0x100000002ULL));
    const auto scaled = service.convert_delay(scaled_delay(2.5));
    const auto half_tick = service.convert_delay(scaled_delay(0.0005));
    require_vpi_time(
        integer && integer.ticks == 0x100000002ULL
            && scaled && scaled.ticks == 2500
            && half_tick && half_tick.ticks == 1,
        "VPI delay conversion combines high/low ticks and rounds scaled units to precision deterministically");

    Scheduler multiplied_scheduler;
    SystemVerilogVpiTimeService multiplied {
        multiplied_scheduler, SystemVerilogVpiTimeProfile { -12, -12, 2 }
    };
    const auto multiplied_integer = multiplied.convert_delay(tick_delay(3));
    const auto multiplied_scaled = multiplied.convert_delay(scaled_delay(6.0));
    const auto multiplied_event = multiplied.schedule(
        tick_delay(6),
        1,
        [](const SystemVerilogVpiTimeQueryResult&) { });
    const auto multiplied_run = multiplied_scheduler.run();
    const auto multiplied_query = multiplied.query(
        SystemVerilogVpiTimeFormat::IntegerTicks);
    const auto multiplied_real = multiplied.query(
        SystemVerilogVpiTimeFormat::ScaledReal);
    require_vpi_time(
        multiplied.valid() && multiplied_integer
            && multiplied_integer.ticks == 2 && multiplied_scaled
            && multiplied_scaled.ticks == 3 && multiplied_event
            && multiplied_run.status == RunStatus::completed
            && multiplied_query.value.high == 0
            && multiplied_query.value.low == 6
            && multiplied_real.value.scaled_real == 6.0,
        "VPI time preserves non-power-of-ten scheduler resolutions without silently treating 2 ps as 1 ps");

    auto invalid_format = tick_delay(0);
    invalid_format.format = static_cast<SystemVerilogVpiTimeFormat>(99);
    require_vpi_time(
        service.convert_delay(scaled_delay(-1.0)).error
                == SystemVerilogVpiTimeError::Negative
            && service.convert_delay(
                          scaled_delay(
                              std::numeric_limits<double>::infinity()))
                    .error
                == SystemVerilogVpiTimeError::NonFinite
            && service.convert_delay(invalid_format).error
                == SystemVerilogVpiTimeError::InvalidFormat,
        "VPI delay conversion rejects negative, nonfinite, and foreign formats");

    std::vector<std::string> order;
    SystemVerilogVpiTimeQueryResult first_query;
    const auto first = service.schedule(
        scaled_delay(1.25),
        10,
        [&](const SystemVerilogVpiTimeQueryResult& query) {
            order.emplace_back("first");
            first_query = query;
        });
    const auto second = service.schedule(
        scaled_delay(1.25),
        10,
        [&](const SystemVerilogVpiTimeQueryResult&) {
            order.emplace_back("second");
        });
    require_vpi_time(
        first && second && service.pending() == 2,
        "VPI after-delay scheduling accepts exact scaled delays");
    const auto run = scheduler.run();
    require_vpi_time(
        run.status == RunStatus::completed
            && order == std::vector<std::string> { "first", "second" }
            && first_query.ticks == 1250
            && first_query.value.high == 0
            && first_query.value.low == 1250
            && service.status(first.value).status
                == SystemVerilogVpiTimedStatus::Fired
            && service.status(second.value).status
                == SystemVerilogVpiTimedStatus::Fired,
        "VPI after-delay callbacks map to common ticks and preserve stable insertion order");

    const auto current_real = service.query(SystemVerilogVpiTimeFormat::ScaledReal);
    require_vpi_time(
        current_real && current_real.ticks == 1250
            && current_real.value.scaled_real == 1.25,
        "VPI scaled-real queries invert the configured unit-to-precision mapping");

    const auto throwing = service.schedule(
        tick_delay(0),
        11,
        [](const SystemVerilogVpiTimeQueryResult&) {
            throw std::runtime_error("contained VPI callback");
        });
    (void)scheduler.run();
    require_vpi_time(
        throwing
            && service.status(throwing.value).status
                == SystemVerilogVpiTimedStatus::CallbackFailed,
        "VPI after-delay callback exceptions are contained and retained");

    const auto cancelled = service.schedule(
        tick_delay(100),
        12,
        [](const SystemVerilogVpiTimeQueryResult&) { });
    require_vpi_time(
        cancelled
            && service.cancel(cancelled.value)
                == SystemVerilogVpiTimeError::None
            && service.status(cancelled.value).status
                == SystemVerilogVpiTimedStatus::Cancelled
            && service.cancel(cancelled.value)
                == SystemVerilogVpiTimeError::NotPending,
        "VPI timed handles support explicit cancellation with retained status");

    SystemVerilogVpiTimeService other {
        scheduler, SystemVerilogVpiTimeProfile { -9, -12 }
    };
    require_vpi_time(
        other.cancel(cancelled.value)
                == SystemVerilogVpiTimeError::CrossService
            && service.release(first.value)
                == SystemVerilogVpiTimeError::None
            && service.status(first.value).error
                == SystemVerilogVpiTimeError::NotFound
            && service.release(cancelled.value)
                == SystemVerilogVpiTimeError::None,
        "VPI timed handles preserve service ownership and independent release");

    const auto overflow = service.schedule(
        tick_delay(std::numeric_limits<std::uint64_t>::max()),
        13,
        [](const SystemVerilogVpiTimeQueryResult&) { });
    require_vpi_time(
        overflow.error == SystemVerilogVpiTimeError::Overflow,
        "VPI after-delay scheduling rejects current-time overflow before publication");

    std::uint64_t next_delta { };
    scheduler.schedule(
        SchedulerPhase::active,
        14,
        [&](Scheduler& running) {
            running.schedule_next_delta(
                SchedulerPhase::active,
                15,
                [&](Scheduler&) {
                    next_delta = service.query(
                                            SystemVerilogVpiTimeFormat::IntegerTicks)
                                     .delta;
                });
        });
    (void)scheduler.run();
    require_vpi_time(
        next_delta != 0U,
        "VPI time queries preserve common-scheduler delta identity");

    bool destroyed_callback { };
    {
        SystemVerilogVpiTimeService transient {
            scheduler, SystemVerilogVpiTimeProfile { -9, -12 }
        };
        const auto pending = transient.schedule(
            tick_delay(5),
            15,
            [&](const SystemVerilogVpiTimeQueryResult&) {
                destroyed_callback = true;
            });
        require_vpi_time(
            pending && transient.pending() == 1,
            "VPI transient time service queues one callback");
    }
    (void)scheduler.run();
    require_vpi_time(
        !destroyed_callback,
        "VPI time-service teardown cancels pending callbacks before state release");

    require_vpi_time(
        service.schedule(
                   tick_delay(1),
                   16,
                   { })
                    .error
                == SystemVerilogVpiTimeError::InvalidHandle
            && service.status({ }).error
                == SystemVerilogVpiTimeError::InvalidHandle,
        "VPI time service rejects missing callbacks and malformed handles");
}

} // namespace fsim::tests::runtime

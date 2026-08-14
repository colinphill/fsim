// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_observability.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

std::shared_ptr<const fsim::app::SdfForeignInterfaceApplication> foreign(
    const bool duplicate = false, const bool stale = false)
{
    using namespace fsim;
    std::vector<app::SdfForeignTimingObject> objects;
    for (std::uint64_t index = 0; index < 3U; ++index) {
        app::SdfForeignTimingObject object;
        object.interface_kind = index == 2U
            ? app::SdfForeignInterfaceKind::Vpi
            : app::SdfForeignInterfaceKind::Vhpi;
        object.timing_kind = index == 0U
            ? app::SdfForeignTimingKind::VitalDelay
            : index == 1U ? app::SdfForeignTimingKind::VitalTimingCheck
                          : app::SdfForeignTimingKind::MixedBoundary;
        object.handle = stale && index == 0U ? 0U : index + 1U;
        object.generation = 23U;
        object.root_identity = "root";
        object.library_identity = "work";
        object.hierarchy_path = "top.t" + std::to_string(index);
        object.source_identity = "source-" + std::to_string(index);
        object.effective_ticks = { index + 3U, index + 7U };
        object.canonical_identity = "foreign-object-" + std::to_string(index);
        objects.push_back(std::move(object));
    }
    if (duplicate)
        objects.push_back(objects.front());
    return std::make_shared<const app::SdfForeignInterfaceApplication>(
        std::move(objects), "foreign-observation-source");
}

void test_surfaces_values_and_deterministic_order()
{
    using namespace fsim;
    const auto built = app::build_sdf_vital_observability(foreign());
    require(built.ok() && built.application->objects().size() == 3U,
        "VITAL observability must publish every foreign timing object");
    constexpr std::array surfaces {
        app::SdfVitalObservationSurface::Debugger,
        app::SdfVitalObservationSurface::Callback,
        app::SdfVitalObservationSurface::InternalTrace,
        app::SdfVitalObservationSurface::Vcd,
    };
    app::SdfVitalObservationRecorder recorder(built.application, surfaces, 4U);
    const auto& object = built.application->objects().front();
    const std::vector<runtime::SimulationTick> none;
    const std::vector<runtime::SimulationTick> effective
        = object.timing.effective_ticks;
    require(recorder.record(app::SdfVitalObservationEventKind::EffectiveTiming,
                object.canonical_identity, 0U, 0U,
                runtime::SchedulerPhase::active, 0U, none, effective)
            == app::SdfVitalObservationStatus::Recorded,
        "effective delay view must record at its exact coordinate");
    require(recorder.record(
                app::SdfVitalObservationEventKind::PendingTransaction,
                object.canonical_identity, 5U, 1U,
                runtime::SchedulerPhase::active, 19U, effective, effective)
            == app::SdfVitalObservationStatus::Recorded,
        "pending transaction view must retain its future publication time");
    const std::vector<runtime::SimulationTick> violated { 99U, 101U };
    require(recorder.record(app::SdfVitalObservationEventKind::Violation,
                object.canonical_identity, 5U, 1U,
                runtime::SchedulerPhase::observed, 5U, effective, violated)
            == app::SdfVitalObservationStatus::Recorded,
        "violation view must retain observed-region before/after values");
    for (const auto surface : surfaces) {
        const auto events = recorder.events(surface);
        require(events.size() == 3U && events[0].sequence == 1U
                && events[1].transaction_time == 19U
                && events[2].region == runtime::SchedulerPhase::observed
                && events[2].after[0] == 99U,
            "debugger/callback/trace/VCD views must be identical and ordered");
    }
    require(!object.vcd_name.empty()
            && object.vcd_name.starts_with("sdf_vital_"),
        "VCD view must retain a deterministic escaped object name");
}

void test_disabled_bounds_and_order_rejection()
{
    using namespace fsim;
    const auto built = app::build_sdf_vital_observability(foreign());
    const auto& object = built.application->objects().front();
    const std::vector<runtime::SimulationTick> value { 1U };
    app::SdfVitalObservationRecorder disabled(built.application, { }, 2U);
    require(!disabled.enabled() && disabled.reserved_event_slots() == 0U
            && disabled.record(
                   app::SdfVitalObservationEventKind::EffectiveTiming,
                   object.canonical_identity, 0U, 0U,
                   runtime::SchedulerPhase::active, 0U, { }, value)
                == app::SdfVitalObservationStatus::Disabled,
        "disabled observation must reserve no event storage or inspect events");
    constexpr std::array enabled {
        app::SdfVitalObservationSurface::Debugger
    };
    app::SdfVitalObservationRecorder bounded(built.application, enabled, 1U);
    require(bounded.record(app::SdfVitalObservationEventKind::EffectiveTiming,
                object.canonical_identity, 7U, 0U,
                runtime::SchedulerPhase::active, 7U, { }, value)
                == app::SdfVitalObservationStatus::Recorded
            && bounded.record(
                   app::SdfVitalObservationEventKind::Violation,
                   object.canonical_identity, 6U, 0U,
                   runtime::SchedulerPhase::active, 6U, value, value)
                == app::SdfVitalObservationStatus::OutOfOrder,
        "time/delta/region regression must reject before consuming capacity");
    require(bounded.record(app::SdfVitalObservationEventKind::Violation,
                object.canonical_identity, 8U, 0U,
                runtime::SchedulerPhase::observed, 8U, value, value)
            == app::SdfVitalObservationStatus::LimitReached,
        "enabled surface capacity must reject without partial fanout");
}

void test_atomic_setup_negatives()
{
    using namespace fsim;
    auto result = app::build_sdf_vital_observability(nullptr);
    require(!result.ok() && result.diagnostics.front().code == "FSIM-SDF-VITAL-OBSERVE-001",
        "missing foreign application must reject atomically");
    result = app::build_sdf_vital_observability(foreign(true));
    require(!result.ok() && result.diagnostics.front().code == "FSIM-SDF-VITAL-OBSERVE-002",
        "duplicate observed identity must reject atomically");
    result = app::build_sdf_vital_observability(foreign(false, true));
    require(!result.ok() && result.diagnostics.front().code == "FSIM-SDF-VITAL-OBSERVE-003",
        "stale foreign handle must reject atomically");
    app::SdfVitalObservabilityLimits limits;
    limits.max_objects = 2U;
    result = app::build_sdf_vital_observability(foreign(), limits);
    require(!result.ok() && result.diagnostics.front().code == "FSIM-SDF-VITAL-OBSERVE-004",
        "observability resource overflow must reject atomically");
}
} // namespace

int main()
{
    try {
        test_surfaces_values_and_deterministic_order();
        test_disabled_bounds_and_order_rejection();
        test_atomic_setup_negatives();
    } catch (const std::exception& error) {
        std::cerr << "sdf vital observability test failure: " << error.what()
                  << '\n';
        return 1;
    }
    std::cout << "sdf vital observability tests passed\n";
    return 0;
}

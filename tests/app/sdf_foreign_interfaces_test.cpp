// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_foreign_interfaces.hpp"

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

std::shared_ptr<const fsim::app::SdfVitalReannotationApplication> make_vital()
{
    using namespace fsim;
    app::SdfVitalScheduledDelay delay;
    delay.call.canonical_identity = "work.top.u_delay";
    delay.effective_delay_ticks = { 7U, 9U };
    delay.canonical_identity = "vital-delay";
    app::SdfVitalScheduledTimingCheck check;
    check.call.canonical_identity = "work.top.u_check";
    check.effective_limits = { 3U, 4U, 5U, 6U };
    check.canonical_identity = "vital-check";
    auto scheduling
        = std::make_shared<const app::SdfVitalSchedulingApplication>(nullptr,
            elaboration::ElaboratedDesign { },
            std::vector<app::SdfVitalScheduledDelay> { delay },
            "foreign-scheduling");
    auto baseline
        = std::make_shared<const app::SdfVitalTimingCheckApplication>(scheduling,
            elaboration::ElaboratedDesign { },
            std::vector<app::SdfVitalScheduledTimingCheck> { check },
            "foreign-timing");
    app::SdfVitalReannotationRevision delay_revision;
    delay_revision.target_identity = "vital-delay";
    delay_revision.root = "root-a";
    delay_revision.cell_pattern = "top.u_delay";
    app::SdfVitalReannotationRevision check_revision;
    check_revision.target_identity = "vital-check";
    check_revision.root = "root-a";
    check_revision.cell_pattern = "top.u_check";
    return std::make_shared<const app::SdfVitalReannotationApplication>(
        baseline, elaboration::ElaboratedDesign { },
        std::vector<app::SdfVitalScheduledDelay> { std::move(delay) },
        std::vector<app::SdfVitalScheduledTimingCheck> { std::move(check) },
        std::vector<app::SdfVitalTimingGenericValue> { },
        std::vector<runtime::simir::Interpreter::VitalTimingReannotation> { },
        std::vector<app::SdfVitalReannotationRevision> {
            std::move(delay_revision), std::move(check_revision) },
        23U, app::SdfVitalPendingTransactionPolicy::PreserveScheduledTiming,
        app::SdfVitalTimingStatePolicy::PreserveHistory, "foreign-vital");
}

std::shared_ptr<const fsim::app::SdfMixedResolutionApplication> make_mixed(
    const bool duplicate = false, const bool missing_values = false)
{
    using namespace fsim;
    app::SdfMixedResolvedBoundary verilog;
    verilog.kind = app::SdfMixedResolutionKind::VerilogBoundary;
    verilog.root_identity = "root-a";
    verilog.library_identity = "work";
    verilog.source_language = app::SdfScopeRootLanguage::Vhdl;
    verilog.destination_language = app::SdfScopeRootLanguage::Verilog;
    verilog.source_path = "top.q";
    verilog.destination_path = "top.net";
    verilog.boundary_path = "top.q";
    verilog.source_identity = "mixed-verilog-source";
    verilog.effective_ticks = missing_values
        ? std::vector<runtime::SimulationTick> { }
        : std::vector<runtime::SimulationTick> { 11U, 13U };
    verilog.canonical_identity = "mixed-verilog";
    app::SdfMixedResolvedBoundary systemc;
    systemc.kind = app::SdfMixedResolutionKind::SystemCProxy;
    systemc.root_identity = "root-b";
    systemc.library_identity = "native";
    systemc.source_language = app::SdfScopeRootLanguage::Vhdl;
    systemc.destination_language = app::SdfScopeRootLanguage::SystemC;
    systemc.source_path = "top.data";
    systemc.destination_path = "native.data";
    systemc.boundary_path = "native.data";
    systemc.source_identity = "mixed-systemc-source";
    systemc.effective_ticks = { 17U };
    systemc.canonical_identity = "mixed-systemc";
    std::vector<app::SdfMixedResolvedBoundary> boundaries {
        verilog, std::move(systemc)
    };
    if (duplicate)
        boundaries.push_back(verilog);
    return std::make_shared<const app::SdfMixedResolutionApplication>(
        std::move(boundaries), "foreign-mixed");
}

void test_stable_vhpi_vpi_enumeration_and_values()
{
    using namespace fsim;
    const auto result
        = app::publish_sdf_foreign_interfaces(make_vital(), make_mixed());
    require(result.ok() && result.application->objects().size() == 5U,
        "VITAL and mixed timing must publish four VHPI objects and one VPI object");
    app::SdfForeignObservationSession session(result.application, 2U);
    const auto vhpi
        = session.enumerate(app::SdfForeignInterfaceKind::Vhpi, 0U, 8U);
    const auto vpi
        = session.enumerate(app::SdfForeignInterfaceKind::Vpi, 0U, 8U);
    require(vhpi && vpi && vhpi.objects.size() == 4U
            && vpi.objects.size() == 1U,
        "VHPI/VPI enumeration must remain interface-specific and bounded");
    require(vhpi.objects.front().handle == 1U && vpi.objects.front().handle == 1U,
        "each foreign ABI must receive deterministic dense stable handles");
    require(vpi.objects.front().effective_ticks
            == std::vector<runtime::SimulationTick> { 11U, 13U },
        "VPI must expose exact effective mixed transition values");
    const auto tail
        = session.enumerate(app::SdfForeignInterfaceKind::Vhpi, 3U, 1U);
    require(tail && tail.objects.size() == 1U,
        "bounded offset enumeration must publish no more than requested");
}

void test_callbacks_controls_and_disabled_observation()
{
    using namespace fsim;
    const auto result
        = app::publish_sdf_foreign_interfaces(make_vital(), make_mixed());
    require(result.ok(), "foreign timing fixture must publish");
    const auto* object = result.application->find_handle(
        app::SdfForeignInterfaceKind::Vpi, 1U);
    require(object != nullptr, "stable VPI handle lookup must succeed");
    app::SdfForeignObservationSession session(result.application, 1U);
    std::size_t callbacks { };
    auto registered = session.register_callback(object->canonical_identity,
        [&](const app::SdfForeignObservationEvent& event) {
            ++callbacks;
            require(event.time == 12U && event.delta == 3U
                    && event.effective_ticks == object->effective_ticks,
                "callback must retain exact coordinate and effective timing");
        });
    require(registered && session.callbacks() == 1U,
        "one bounded foreign callback must register");
    const auto overflow = session.register_callback(
        object->canonical_identity, [](const auto&) { });
    require(!overflow
            && overflow.error == app::SdfForeignInterfaceError::ResourceLimit,
        "callback resource overflow must reject without replacing state");

    runtime::Scheduler scheduler;
    scheduler.schedule_at(50U, runtime::SchedulerPhase::active, 1U,
        [](runtime::Scheduler&) { });
    const auto before = scheduler.next_pending_time();
    session.set_observation_enabled(false);
    require(session.publish(object->canonical_identity, 12U, 3U)
            == app::SdfForeignInterfaceError::ObservationDisabled,
        "disabled observation must reject event delivery explicitly");
    require(callbacks == 0U && scheduler.next_pending_time() == before
            && scheduler.now() == 0U && scheduler.delta() == 0U,
        "disabled observation must not invoke callbacks or alter scheduling");
    session.set_observation_enabled(true);
    require(session.publish(object->canonical_identity, 12U, 3U)
                == app::SdfForeignInterfaceError::None
            && callbacks == 1U && scheduler.next_pending_time() == before,
        "enabled observation must deliver synchronously without scheduling work");
    const std::vector<runtime::SimulationTick> replacement { 99U };
    require(session.control_value(object->canonical_identity,
                app::SdfForeignValueControl::Deposit, replacement)
            == app::SdfForeignInterfaceError::ReadOnly,
        "effective foreign timing values must reject deposit/force/release control");
    require(result.application->find_identity(object->canonical_identity)
                ->effective_ticks
            == object->effective_ticks,
        "rejected control must leave the immutable effective value unchanged");
}

void test_atomic_input_and_resource_rejection()
{
    using namespace fsim;
    auto result = app::publish_sdf_foreign_interfaces(nullptr, make_mixed());
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code == "FSIM-SDF-FOREIGN-001",
        "incomplete foreign source applications must reject atomically");
    result = app::publish_sdf_foreign_interfaces(make_vital(), make_mixed(true));
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code == "FSIM-SDF-FOREIGN-002",
        "duplicate foreign object identities must reject atomically");
    result = app::publish_sdf_foreign_interfaces(
        make_vital(), make_mixed(false, true));
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code == "FSIM-SDF-FOREIGN-003",
        "mixed timing without effective values must reject atomically");
    app::SdfForeignInterfaceLimits limits;
    limits.max_objects = 4U;
    result = app::publish_sdf_foreign_interfaces(
        make_vital(), make_mixed(), limits);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code == "FSIM-SDF-FOREIGN-004",
        "foreign object resource overflow must roll back atomically");
}
} // namespace

int main()
{
    try {
        test_stable_vhpi_vpi_enumeration_and_values();
        test_callbacks_controls_and_disabled_observation();
        test_atomic_input_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf foreign interfaces test failure: " << error.what()
                  << '\n';
        return 1;
    }
    std::cout << "sdf foreign interfaces tests passed\n";
    return 0;
}

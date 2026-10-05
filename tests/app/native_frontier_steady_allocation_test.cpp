// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../runtime/runtime_fused_staging_failure_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <streambuf>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
    struct RawDriver {
        ProcessId process { };
        std::string value;
        DriveStrength strength;

        friend bool operator==(const RawDriver&, const RawDriver&) = default;
    };

    struct SignalSnapshot {
        SignalId signal { };
        std::string current;
        std::string last;
        std::string stored;
        std::vector<RawDriver> raw_drivers;
        std::optional<std::string> owned_raw;
        std::optional<std::string> external_raw;
        std::optional<std::string> force_value;
        std::optional<std::string> force_mask;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        ProcessSchedulingDomain event_domain {
            ProcessSchedulingDomain::generic };
        SchedulerPhase event_phase { SchedulerPhase::active };
        std::uint64_t systemverilog_round { };
        SimulationTick now { };
        std::uint64_t delta { };

        friend bool operator==(
            const SignalSnapshot&, const SignalSnapshot&) = default;
    };

    struct GenericBridgeRun {
        ProcessId process { };
        SimulationTick callback_time { };
        fsim::runtime::SchedulerSystemVerilogKeyReceipt receipt;
        fsim::runtime::RunStatus status { fsim::runtime::RunStatus::completed };
        std::uint64_t final_delta { };
        bool deposit_executed { };
        bool receipt_captured { };
        bool process_queued_at_receipt { };
    };

    struct ComponentIdentity {
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        const void* authoritative_identity { };
        const void* local_identity { };
        std::uint64_t authoritative_generation { };
        std::uint64_t authoritative_revision { };
        std::uint64_t local_generation { };
        std::uint64_t local_authoritative_revision { };
        bool authoritative_valid { };
        bool packed_slots_bound { };
        bool signal_slots_bound { };
        bool owner_slots_bound { };
        bool requires_prewrite_unbind { };
        bool local_seeded { };
        bool graph_epoch_current { };
        bool value_only_candidate { };
    };

    struct RuntimeIdentity {
        std::uint64_t runtime_generation { };
        std::uint64_t recertification_attempts { };
        std::uint64_t recertification_successes { };
        std::uint64_t recertification_failures { };
        std::uint64_t slot_bind_components { };
        std::uint64_t slot_bindings { };
        bool recertification_pending { };
        bool snapshot_required { };
        bool value_only_candidate_pending { };
    };

    [[nodiscard]] static std::uint64_t native_member_dispatches(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the native frontier allocation probe has no interpreter"
            };
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_native_frontier_member_dispatches;
    }

    [[nodiscard]] static std::uint64_t forwarding_member_consumptions(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the native frontier allocation probe has no interpreter"
            };
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_member_consumptions;
    }

    [[nodiscard]] static RuntimeIdentity runtime_identity(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the quiet-rebind probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        RuntimeIdentity result;
        result.runtime_generation = state.region_runtime_generation;
        result.recertification_attempts
            = state.systemverilog_wave_profile_region_recertification_attempts;
        result.recertification_successes
            = state.systemverilog_wave_profile_region_recertification_successes;
        result.recertification_failures
            = state.systemverilog_wave_profile_region_recertification_failures;
        result.slot_bind_components
            = state.systemverilog_wave_profile_a4_slot_bind_components;
        result.slot_bindings
            = state.systemverilog_wave_profile_a4_slot_bindings;
        result.recertification_pending = state.region_recertification_pending;
        result.snapshot_required = state.region_recertification_requires_snapshot;
        for (const auto candidate :
            state.region_value_only_recertification_by_component) {
            result.value_only_candidate_pending
                = result.value_only_candidate_pending || candidate != 0U;
        }
        return result;
    }

    [[nodiscard]] static ComponentIdentity component_identity(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the quiet-rebind probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        ComponentIdentity result;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            return result;
        }
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (component == std::numeric_limits<std::size_t>::max()) {
            return result;
        }
        result.component = component;
        if (component < state.region_authoritative_state_by_component.size()) {
            const auto& authoritative
                = state.region_authoritative_state_by_component[component];
            if (authoritative) {
                result.authoritative_identity = authoritative.get();
                result.authoritative_generation = authoritative->generation();
                result.authoritative_revision = authoritative->values().revision();
                result.authoritative_valid = authoritative->valid();
                result.packed_slots_bound
                    = authoritative->values().packed_slots_bound();
                const auto& values = authoritative->values();
                if (values.layout().contains(signal)) {
                    result.signal_slots_bound
                        = values.packed_signal_slots_bound(signal);
                    const auto owners = values.layout().owners(signal);
                    result.owner_slots_bound = !owners.empty()
                        && std::ranges::all_of(owners,
                            [&](const SignalDriverOwnerLayout& owner) {
                                return values.packed_owner_slot_bound(
                                    signal, owner.process);
                            });
                }
                result.requires_prewrite_unbind
                    = authoritative->values().requires_prewrite_unbind();
            }
        }
        if (component < state.region_local_wave_state_by_component.size()) {
            const auto& local = state.region_local_wave_state_by_component[component];
            if (local) {
                result.local_identity = local.get();
                result.local_generation = local->generation;
                result.local_authoritative_revision
                    = local->authoritative_revision;
                result.local_seeded = local->seeded;
            }
        }
        result.graph_epoch_current = state.region_graph
            && state.region_graph->component_epochs_current(component);
        result.value_only_candidate
            = component < state.region_value_only_recertification_by_component.size()
            && state.region_value_only_recertification_by_component[component] != 0U;
        return result;
    }

    [[nodiscard]] static SignalSnapshot snapshot(
        const fsim::app::Simulation& simulation, const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the native frontier allocation probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.signals.size()
            || state.has_container_signal_alias(signal)) {
            throw std::logic_error {
                "the native frontier allocation probe requires a direct signal"
            };
        }

        SignalSnapshot result;
        result.signal = signal;
        const bool materialization_pending
            = signal < state.direct_signal_materialization_pending.size()
            && state.direct_signal_materialization_pending[signal] != 0U;
        const AuthoritativeSignalPlanes* authoritative_values { };
        const auto width = state.signals[signal].initial_value.width();
        if (width > 64U
            && signal < state.region_authoritative_component_by_signal.size()) {
            const auto component
                = state.region_authoritative_component_by_signal[signal];
            if (component
                    < state.region_authoritative_state_by_component.size()
                && state.region_authoritative_state_by_component[component]
                && state.region_authoritative_state_by_component[component]
                       ->values().packed_signal_slots_bound(signal)) {
                authoritative_values
                    = &state.region_authoritative_state_by_component[component]
                           ->values();
            }
        }

        PackedLogic4 current;
        PackedLogic4 last;
        PackedLogic4 stored;
        if (authoritative_values != nullptr) {
            current = authoritative_values->current(signal);
            last = authoritative_values->previous(signal);
            stored = authoritative_values->stored(signal);
        } else if (materialization_pending) {
            if (state.signals[signal].value_kind != ValueKind::logic4
                || width == 0U || width > 64U
                || signal >= state.direct_signal_aval.size()
                || signal >= state.direct_signal_bval.size()
                || signal >= state.direct_signal_last_aval.size()
                || signal >= state.direct_signal_last_bval.size()) {
                throw std::logic_error {
                    "the pending direct plane is outside the passive snapshot path"
                };
            }
            current = PackedLogic4::from_aval_bval(width,
                state.direct_signal_aval[signal],
                state.direct_signal_bval[signal]);
            last = PackedLogic4::from_aval_bval(width,
                state.direct_signal_last_aval[signal],
                state.direct_signal_last_bval[signal]);
            // A pending native publication has not copied the direct CURRENT
            // plane into the packed stored mirror yet.
            stored = current;
        } else {
            current = state.signals[signal].initial_value;
            last = state.signal_last_values.at(signal);
            stored = state.driven_values.at(signal);
        }
        result.current = current.to_msb_string();
        result.last = last.to_msb_string();
        result.stored = stored.to_msb_string();

        state.driver_values.at(signal).for_each_in_process_order(
            [&](const DriverRecord& record) {
                const auto value = [&] {
                    if (authoritative_values != nullptr) {
                        return authoritative_values->owner_value(
                            signal, record.process);
                    }
                    if (materialization_pending
                        && state.direct_single_driver_record(signal) == &record) {
                        return current;
                    }
                    if (state.owned_driver_active(signal)) {
                        return state.owned_driver_value(record.process, signal);
                    }
                    return record.value;
                }();
                result.raw_drivers.push_back(
                    { record.process, value.to_msb_string(), record.strength });
            });
        if (signal < state.owned_driver_composites.size()
            && state.owned_driver_composites[signal].active) {
            result.owned_raw
                = state.owned_driver_composites[signal].committed.to_msb_string();
        }
        if (signal < state.external_driver_values.size()
            && state.external_driver_values[signal]) {
            result.external_raw
                = state.external_driver_values[signal]->to_msb_string();
        }
        if (signal < state.forced_values.size()
            && state.forced_values[signal]) {
            result.force_value = state.forced_values[signal]->to_msb_string();
        }
        if (signal < state.forced_masks.size()
            && state.forced_masks[signal]) {
            result.force_mask = state.forced_masks[signal]->to_msb_string();
        }
        result.event = state.signal_events.at(signal);
        result.transaction = state.signal_transactions.at(signal);
        const auto& stamp
            = state.signal_event_scheduling_stamps.at(signal);
        result.event_domain = stamp.origin.process_domain;
        result.event_phase = stamp.origin.phase;
        result.systemverilog_round = stamp.systemverilog_round;
        result.now = state.scheduler.now();
        result.delta = state.scheduler.delta();
        return result;
    }

    [[nodiscard]] static ProcessId assignment_owner(
        const fsim::app::Simulation& simulation, const SignalId output_signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the generic bridge probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        if (output_signal >= state.driver_values.size()) {
            throw std::logic_error {
                "the generic bridge output has no settled driver table"
            };
        }
        const auto& drivers = state.driver_values[output_signal];
        std::optional<ProcessId> owner;
        for (ProcessId process = 0U; process < state.processes.size(); ++process) {
            if (drivers.find(process) == nullptr
                || state.processes[process].program().scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog) {
                continue;
            }
            if (owner) {
                throw std::logic_error {
                    "the generic bridge output must have one settled SV driver"
                };
            }
            owner = process;
        }
        if (owner) {
            return *owner;
        }
        throw std::logic_error {
            "the generic bridge probe has no settled SystemVerilog driver owner"
        };
    }

    [[nodiscard]] static GenericBridgeRun schedule_generic_deposit(
        fsim::app::Simulation& simulation,
        const SignalId input_signal,
        const SignalId output_signal,
        const bool stop_after_receipt)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the generic bridge probe has no interpreter"
            };
        }
        auto& state = *application.interpreter->impl_;
        auto& scheduler = state.scheduler;

        GenericBridgeRun result;
        result.process = assignment_owner(simulation, output_signal);
        if (result.process >= state.processes.size()
            || (stop_after_receipt
                && result.process
                    >= state.region_readiness_queued_by_process.size())) {
            throw std::logic_error {
                "the generic bridge owner is outside process readiness state"
            };
        }
        if (state.processes[result.process].queued
            || (stop_after_receipt
                && state.region_readiness_queued_by_process[
                    result.process].key_valid)) {
            throw std::logic_error {
                "the generic bridge owner must be settled before its stimulus"
            };
        }

        result.callback_time = scheduler.now() + 1U;
        const auto stable_order
            = static_cast<StableOrder>(result.process);
        Scheduler::SafePointHookToken hook_token { };
        if (stop_after_receipt) {
            hook_token = scheduler.add_safe_point_hook(
                [&state, &result](Scheduler& active_scheduler,
                    const SchedulerPhase phase) {
                    if (!result.deposit_executed
                        || phase != SchedulerPhase::active
                        || result.process
                            >= state.region_readiness_queued_by_process.size()) {
                        return;
                    }
                    const auto& readiness
                        = state.region_readiness_queued_by_process[
                            result.process];
                    if (!readiness.key_valid) {
                        return;
                    }
                    const auto& key = readiness.queued_key;
                    if (key.time != result.callback_time
                        || key.delta != active_scheduler.delta()
                        || key.stable_order != result.process
                        || key.systemverilog_round == 0U
                        || key.process_domain
                            != static_cast<std::uint32_t>(
                                ProcessSchedulingDomain::systemverilog)
                        || key.phase != static_cast<std::uint32_t>(
                            SchedulerPhase::active)) {
                        return;
                    }
                    result.receipt = {
                        true,
                        key.time,
                        key.delta,
                        key.systemverilog_round,
                        static_cast<SchedulerPhase>(key.phase),
                        key.stable_order,
                        key.sequence,
                    };
                    result.receipt_captured = true;
                    result.process_queued_at_receipt
                        = state.processes[result.process].queued;
                    active_scheduler.request_stop();
                });
        }

        try {
            scheduler.schedule_at(result.callback_time,
                SchedulerPhase::active, stable_order,
                [&simulation, input_signal, &result](Scheduler&) {
                    simulation.deposit_signal(input_signal,
                        PackedLogic4::from_msb_string("1"));
                    result.deposit_executed = true;
                });
            const auto run = simulation.run(result.callback_time + 1U);
            result.status = run.status;
            result.final_delta = run.delta;
        } catch (...) {
            if (hook_token != 0U) {
                scheduler.remove_safe_point_hook(hook_token);
            }
            throw;
        }
        if (hook_token != 0U) {
            scheduler.remove_safe_point_hook(hook_token);
        }
        return result;
    }
};

} // namespace fsim::runtime::simir

namespace {

using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::app::SystemVerilogVpiRuntimeUpdates;
using fsim::project::Optimization;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunStatus;
using fsim::runtime::SimulationTick;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::ProcessSchedulingDomain;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::SignalId;
namespace staging_failure_support
    = fsim::tests::runtime::staging_failure_support;
using staging_failure_support::begin_allocation_count;
using staging_failure_support::end_allocation_count;
using staging_failure_support::require;

constexpr std::size_t measured_window_count { 8U };
constexpr std::size_t warm_window_count { 4U };
constexpr std::size_t signal_count { 10U };
constexpr std::size_t wide_stage0_signal_index { 5U };

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error { "failed to update process environment" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        static_cast<void>(set(previous_ ? previous_->c_str() : nullptr));
    }

private:
    [[nodiscard]] bool set(const char* const value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
        return value == nullptr
            ? ::unsetenv(name_.c_str()) == 0
            : ::setenv(name_.c_str(), value, 1) == 0;
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

class ScopedCerrCapture final {
public:
    explicit ScopedCerrCapture(std::streambuf& output)
        : previous_(std::cerr.rdbuf(&output))
    {
    }

    ~ScopedCerrCapture()
    {
        std::cerr.rdbuf(previous_);
    }

private:
    std::streambuf* previous_;
};

class FixedCerrBuffer final : public std::streambuf {
public:
    FixedCerrBuffer()
    {
        setp(storage_.data(), storage_.data() + storage_.size());
    }

    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

protected:
    int_type overflow(const int_type) override
    {
        overflowed_ = true;
        return traits_type::eof();
    }

private:
    std::array<char, 256U * 1024U> storage_ { };
    bool overflowed_ { };
};

[[nodiscard]] fsim::project::Config make_config(
    const Optimization optimization,
    const std::filesystem::path& root, const std::size_t width)
{
    const auto source = root / "native_frontier_steady_allocation.sv";
    std::ofstream output { source, std::ios::binary };
    const std::string range = width == 1U
        ? std::string { }
        : "[" + std::to_string(width - 1U) + ":0] ";
    output << "module native_frontier_steady_allocation;\n"
           << "  logic stimulus = 1'b0;\n"
           << "  wire scalar0;\n"
           << "  wire scalar1;\n"
           << "  wire scalar2;\n"
           << "  wire scalar3;\n"
           << "  wire " << range << "wide_stage0;\n"
           << "  wire " << range << "wide_stage1;\n"
           << "  wire " << range << "wide_stage2;\n"
           << "  wire wide_reduction;\n"
           << "  wire sink;\n\n"
           << "  assign scalar0 = stimulus;\n"
           << "  assign scalar1 = ~scalar0;\n"
           << "  assign scalar2 = scalar1 ^ stimulus;\n"
           << "  assign scalar3 = ~scalar2;\n"
           << "  assign wide_stage0 = {" << width << "{stimulus}};\n"
           << "  assign wide_stage1 = ~wide_stage0;\n"
           << "  assign wide_stage2 = ~wide_stage1;\n"
           << "  assign wide_reduction = ^wide_stage2;\n"
           << "  assign sink = scalar3 ^ wide_reduction;\n\n"
           << "  initial begin\n"
           << "    #100;\n"
           << "    $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the steady-state native-frontier source must be written completely");

    fsim::project::Config config;
    config.project.name = "native-frontier-steady-allocation";
    config.project.top = "sv:work.native_frontier_steady_allocation";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

using Snapshot = NativeRegionAllocationTestAccess::SignalSnapshot;
using Frame = std::array<Snapshot, signal_count>;

struct RuntimeIdentitySnapshot {
    NativeRegionAllocationTestAccess::RuntimeIdentity runtime;
    std::array<NativeRegionAllocationTestAccess::ComponentIdentity, signal_count>
        components { };
};

struct RouteResult {
    Frame initial_frame { };
    std::vector<Frame> measured_frames;
    std::array<std::size_t, measured_window_count> allocations { };
    std::array<std::uint64_t, measured_window_count> dispatch_deltas { };
    std::array<std::uint64_t, measured_window_count> frontier_dispatch_deltas { };
    std::array<std::uint64_t, measured_window_count> forwarding_member_deltas { };
    std::array<RuntimeIdentitySnapshot, measured_window_count> identity_before { };
    std::array<RuntimeIdentitySnapshot, measured_window_count> identity_after { };
};

[[nodiscard]] bool same_semantics(const Snapshot& left, const Snapshot& right)
{
    // Private pending/materialization state is deliberately excluded. All
    // signal roles, owner values, external drivers, force state and
    // event/transaction stamps remain part of the parity check.
    return left.signal == right.signal
        && left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.raw_drivers == right.raw_drivers
        && left.owned_raw == right.owned_raw
        && left.external_raw == right.external_raw
        && left.force_value == right.force_value
        && left.force_mask == right.force_mask
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_domain == right.event_domain
        && left.event_phase == right.event_phase
        && left.systemverilog_round == right.systemverilog_round
        && left.now == right.now
        && left.delta == right.delta;
}

[[nodiscard]] bool same_frame(const Frame& left, const Frame& right)
{
    for (std::size_t signal = 0U; signal < signal_count; ++signal) {
        if (!same_semantics(left[signal], right[signal])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_frames(
    const std::vector<Frame>& left, const std::vector<Frame>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t frame = 0U; frame < left.size(); ++frame) {
        if (!same_frame(left[frame], right[frame])) {
            return false;
        }
    }
    return true;
}

void require_value_only_rebind(
    const RuntimeIdentitySnapshot& before,
    const RuntimeIdentitySnapshot& after)
{
    require(after.runtime.runtime_generation
                == before.runtime.runtime_generation
            && after.runtime.recertification_attempts
                == before.runtime.recertification_attempts + 1U
            && after.runtime.recertification_successes
                == before.runtime.recertification_successes + 1U
            && after.runtime.recertification_failures
                == before.runtime.recertification_failures
            && after.runtime.slot_bind_components
                > before.runtime.slot_bind_components
            && after.runtime.slot_bindings
                > before.runtime.slot_bindings
            && !after.runtime.recertification_pending
            && !after.runtime.snapshot_required
            && !after.runtime.value_only_candidate_pending,
        "the width-one first measured quiet point must rebind its existing "
        "A4 storage without publishing a new runtime snapshot");

    for (std::size_t index = 0U; index < signal_count; ++index) {
        const auto& old_component = before.components[index];
        const auto& new_component = after.components[index];
        require(old_component.component == new_component.component
                && old_component.authoritative_identity
                    == new_component.authoritative_identity
                && old_component.local_identity == new_component.local_identity,
            "quiet value-only recertification must retain each component's "
            "A4 and local-state object identities");
        if (old_component.component
                == std::numeric_limits<std::size_t>::max()
            || old_component.authoritative_identity == nullptr) {
            continue;
        }
        require(new_component.authoritative_valid
                && new_component.packed_slots_bound
                && new_component.graph_epoch_current
                && new_component.authoritative_generation
                    == after.runtime.runtime_generation,
            "rebound component roles must match the current graph epoch");
        if (new_component.authoritative_revision
                > old_component.authoritative_revision) {
            require(new_component.local_identity != nullptr
                    && new_component.local_generation
                        == after.runtime.runtime_generation
                    && new_component.local_authoritative_revision
                        == new_component.authoritative_revision
                    && !new_component.local_seeded,
                "the retained local state must refresh its A4 revision and "
                "clear its old activation seed after rebind");
        }
    }
    const auto& old_wide_component
        = before.components[wide_stage0_signal_index];
    const auto& new_wide_component
        = after.components[wide_stage0_signal_index];
    require(old_wide_component.component
                != std::numeric_limits<std::size_t>::max()
            && old_wide_component.authoritative_identity != nullptr
            && new_wide_component.authoritative_revision
                > old_wide_component.authoritative_revision
            && new_wide_component.packed_slots_bound
            && new_wide_component.signal_slots_bound
            && new_wide_component.owner_slots_bound
            && new_wide_component.local_identity != nullptr
            && new_wide_component.local_authoritative_revision
                == new_wide_component.authoritative_revision
            && !new_wide_component.local_seeded,
        "the fixed wide_stage0 component must retain its A4/local identities "
        "and refresh its binding revision");
}

[[nodiscard]] RouteResult run_route(
    const Optimization optimization,
    const std::filesystem::path& root,
    const std::size_t width,
    const SimulationEngine engine)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_config(optimization, root, width);
    FixedCerrBuffer profile;
    RouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the steady-state native-frontier design must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the measured route must use the real default native provider");
        }

        constexpr std::array<std::string_view, signal_count> signal_names {
            "native_frontier_steady_allocation.stimulus",
            "native_frontier_steady_allocation.scalar0",
            "native_frontier_steady_allocation.scalar1",
            "native_frontier_steady_allocation.scalar2",
            "native_frontier_steady_allocation.scalar3",
            "native_frontier_steady_allocation.wide_stage0",
            "native_frontier_steady_allocation.wide_stage1",
            "native_frontier_steady_allocation.wide_stage2",
            "native_frontier_steady_allocation.wide_reduction",
            "native_frontier_steady_allocation.sink",
        };
        std::array<SignalId, signal_count> signals { };
        for (std::size_t index = 0U; index < signal_count; ++index) {
            const auto signal = simulation.find_signal(signal_names[index]);
            require(signal.has_value(),
                "the fixed-topology design must retain every signal handle");
            signals[index] = *signal;
        }

        const auto capture_frame = [&] {
            Frame frame;
            for (std::size_t index = 0U; index < signal_count; ++index) {
                frame[index] = NativeRegionAllocationTestAccess::snapshot(
                    simulation, signals[index]);
            }
            return frame;
        };
        const auto capture_identity = [&] {
            RuntimeIdentitySnapshot identity;
            identity.runtime
                = NativeRegionAllocationTestAccess::runtime_identity(simulation);
            for (std::size_t index = 0U; index < signal_count; ++index) {
                identity.components[index]
                    = NativeRegionAllocationTestAccess::component_identity(
                        simulation, signals[index]);
            }
            return identity;
        };
        const std::array<PackedLogic4, 2U> inputs {
            PackedLogic4::from_msb_string("1"),
            PackedLogic4::from_msb_string("0"),
        };
        const auto run_input = [&](const std::size_t phase) {
            simulation.deposit_signal(signals[0U], inputs[phase % inputs.size()]);
            const auto run = simulation.run(simulation.now() + 1U);
            require(run.status == RunStatus::time_limit,
                "the measured HDL task must remain beyond each fixed window");
        };

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the future finish must leave fixed-topology work pending");
        if (compiled) {
            simulation.await_all_native_compilation();
        }
        // Capture the settled design before external deposits, warmup, or any
        // allocation-counted measurement. snapshot() reads authoritative planes
        // or the pending direct input plane without observing/materializing it.
        result.initial_frame = capture_frame();

        // Materialize the same source transitions and settle all specialization
        // before opening any C++ allocation-counted window.
        for (std::size_t phase = 0U; phase < warm_window_count; ++phase) {
            run_input(phase);
        }
        if (compiled) {
            simulation.await_all_native_compilation();
        }

        result.measured_frames.reserve(measured_window_count);
        for (std::size_t window = 0U; window < measured_window_count; ++window) {
            const auto phase = warm_window_count + window;
            if (compiled) {
                result.identity_before[window] = capture_identity();
            }
            const auto before_dispatches = compiled
                ? NativeRegionAllocationTestAccess::native_member_dispatches(
                      simulation)
                : 0U;
            const auto before_forwarded = compiled
                ? NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                      simulation)
                : 0U;
            if (compiled) {
                begin_allocation_count();
                try {
                    run_input(phase);
                } catch (...) {
                    static_cast<void>(end_allocation_count());
                    throw;
                }
                result.allocations[window] = end_allocation_count();
            } else {
                run_input(phase);
            }
            const auto after_dispatches = compiled
                ? NativeRegionAllocationTestAccess::native_member_dispatches(
                      simulation)
                : 0U;
            const auto after_forwarded = compiled
                ? NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                      simulation)
                : 0U;
            if (compiled) {
                result.identity_after[window] = capture_identity();
                if (width == 1U && window == 0U) {
                    require_value_only_rebind(result.identity_before[window],
                        result.identity_after[window]);
                }
                result.frontier_dispatch_deltas[window]
                    = after_dispatches - before_dispatches;
                result.forwarding_member_deltas[window]
                    = after_forwarded - before_forwarded;
                result.dispatch_deltas[window]
                    = result.frontier_dispatch_deltas[window]
                    + result.forwarding_member_deltas[window];
                require(result.dispatch_deltas[window] != 0U,
                    "every measured window must consume a native-frontier or flattened forwarding member");
                if (result.allocations[window] != 0U) {
                    std::cerr << "native-frontier steady allocation width="
                              << width << " window=" << window
                              << " deposit_plus_run_cpp_new_requests="
                              << result.allocations[window] << '\n';
                }
                require(result.allocations[window] == 0U,
                    "the warmed fixed-topology deposit-plus-run window "
                    "must allocate no C++ new storage");
            }
            result.measured_frames.push_back(capture_frame());
        }
        require(simulation.read_signal_snapshot(signals[9U]).to_msb_string()
                    == "0",
            "the scalar reduction output must reflect the final low stimulus");
    }
    require(!profile.overflowed(),
        "the fixed native-frontier profile buffer must remain complete");
    return result;
}

struct GenericBridgeRouteResult {
    Snapshot input;
    Snapshot output;
    ProcessId process { };
    SimulationTick callback_time { };
    fsim::runtime::SchedulerSystemVerilogKeyReceipt receipt;
    bool receipt_captured { };
    std::uint64_t native_dispatch_delta { };
    std::uint64_t forwarding_member_delta { };
};

struct HookPolicyControlResult {
    Frame frame { };
    RuntimeIdentitySnapshot before_hook;
    RuntimeIdentitySnapshot armed;
    RuntimeIdentitySnapshot after_rebuild;
    std::uint64_t native_dispatch_delta { };
    std::uint64_t forwarding_member_delta { };
    std::size_t hook_calls { };
};

[[nodiscard]] HookPolicyControlResult run_hook_policy_control(
    const std::filesystem::path& root, const SimulationEngine engine)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_config(Optimization::o0, root, 1U);
    FixedCerrBuffer profile;
    HookPolicyControlResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the hook-control design must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            simulation.await_all_native_compilation();
        }

        constexpr std::array<std::string_view, signal_count> signal_names {
            "native_frontier_steady_allocation.stimulus",
            "native_frontier_steady_allocation.scalar0",
            "native_frontier_steady_allocation.scalar1",
            "native_frontier_steady_allocation.scalar2",
            "native_frontier_steady_allocation.scalar3",
            "native_frontier_steady_allocation.wide_stage0",
            "native_frontier_steady_allocation.wide_stage1",
            "native_frontier_steady_allocation.wide_stage2",
            "native_frontier_steady_allocation.wide_reduction",
            "native_frontier_steady_allocation.sink",
        };
        std::array<SignalId, signal_count> signals { };
        for (std::size_t index = 0U; index < signal_count; ++index) {
            const auto signal = simulation.find_signal(signal_names[index]);
            require(signal.has_value(),
                "the hook-control fixture must retain all signal handles");
            signals[index] = *signal;
        }
        const auto capture_identity = [&] {
            RuntimeIdentitySnapshot identity;
            identity.runtime
                = NativeRegionAllocationTestAccess::runtime_identity(simulation);
            for (std::size_t index = 0U; index < signal_count; ++index) {
                identity.components[index]
                    = NativeRegionAllocationTestAccess::component_identity(
                        simulation, signals[index]);
            }
            return identity;
        };
        const auto capture_frame = [&] {
            Frame frame;
            for (std::size_t index = 0U; index < signal_count; ++index) {
                frame[index] = NativeRegionAllocationTestAccess::snapshot(
                    simulation, signals[index]);
            }
            return frame;
        };
        const std::array<PackedLogic4, 2U> inputs {
            PackedLogic4::from_msb_string("1"),
            PackedLogic4::from_msb_string("0"),
        };
        const auto run_input = [&](const std::size_t phase) {
            simulation.deposit_signal(signals[0U],
                inputs[phase % inputs.size()]);
            const auto run = simulation.run(simulation.now() + 1U);
            require(run.status == RunStatus::time_limit,
                "the hook-control fixture must settle each scheduled write");
        };

        simulation.start();
        require(simulation.run(1U).status == RunStatus::time_limit,
            "the hook-control fixture must settle its initial assignments");
        if (compiled) {
            simulation.await_all_native_compilation();
        }
        for (std::size_t phase = 0U; phase < warm_window_count; ++phase) {
            run_input(phase);
        }
        if (compiled) {
            simulation.await_all_native_compilation();
        }
        result.before_hook = capture_identity();
        const auto before_native = compiled
            ? NativeRegionAllocationTestAccess::native_member_dispatches(
                  simulation)
            : 0U;
        const auto before_forwarding = compiled
            ? NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                  simulation)
            : 0U;
        simulation.set_signal_change_hook(
            [&result](const SignalId, const PackedLogic4&,
                const SimulationTick, const std::uint64_t) {
                ++result.hook_calls;
            });
        result.armed = capture_identity();
        run_input(warm_window_count);
        result.after_rebuild = capture_identity();
        result.frame = capture_frame();
        if (compiled) {
            result.native_dispatch_delta
                = NativeRegionAllocationTestAccess::native_member_dispatches(
                      simulation) - before_native;
            result.forwarding_member_delta
                = NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                      simulation) - before_forwarding;
        }
    }
    require(!profile.overflowed(),
        "the hook-control profile buffer must remain complete");
    return result;
}

[[nodiscard]] GenericBridgeRouteResult run_generic_bridge_route(
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const bool stop_after_receipt)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_config(Optimization::o0, root, 1U);
    FixedCerrBuffer profile;
    GenericBridgeRouteResult result;
    {
        ScopedCerrCapture capture { profile };
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        require(project.has_value(),
            "the generic-to-SystemVerilog bridge design must elaborate");
        Simulation simulation(std::move(*project), config.run.max_deltas,
            engine, SystemVerilogVpiRuntimeUpdates::omitted);
        if (compiled) {
            simulation.await_all_native_compilation();
            require(simulation.compiled_process_count() != 0U,
                "the bridge case must use the real compiled process provider");
        }

        const auto input = simulation.find_signal(
            "native_frontier_steady_allocation.stimulus");
        const auto output = simulation.find_signal(
            "native_frontier_steady_allocation.scalar0");
        require(input.has_value() && output.has_value(),
            "the bridge case must retain its input and scalar output handles");

        simulation.start();
        const auto initial = simulation.run(1U);
        require(initial.status == RunStatus::time_limit,
            "the bridge case must settle initial assignments before its stimulus");
        if (compiled) {
            simulation.await_all_native_compilation();
        }

        const auto before_dispatches = compiled
            ? NativeRegionAllocationTestAccess::native_member_dispatches(
                  simulation)
            : 0U;
        const auto before_forwarded = compiled
            ? NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                  simulation)
            : 0U;
        const auto bridge
            = NativeRegionAllocationTestAccess::schedule_generic_deposit(
                simulation, *input, *output, stop_after_receipt);
        result.process = bridge.process;
        result.callback_time = bridge.callback_time;
        result.receipt = bridge.receipt;
        result.receipt_captured = bridge.receipt_captured;
        if (stop_after_receipt) {
            require(bridge.deposit_executed
                    && bridge.status == RunStatus::stopped
                    && bridge.receipt_captured
                    && bridge.process_queued_at_receipt,
                "the callback bridge must stop after publishing its exact SV receipt");
            require(bridge.final_delta == bridge.receipt.delta,
                "the safe-point stop must leave the receipt-bearing task unconsumed");
            simulation.clear_stop();
        } else {
            require(bridge.deposit_executed
                    && bridge.status == RunStatus::time_limit,
                "the checked reference route must settle the same generic deposit");
        }

        const auto settled = simulation.run(bridge.callback_time + 1U);
        require(settled.status == RunStatus::time_limit,
            "the bridge task must settle before the later finish event");
        if (compiled) {
            const auto after_dispatches
                = NativeRegionAllocationTestAccess::native_member_dispatches(
                    simulation);
            const auto after_forwarded
                = NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                    simulation);
            result.native_dispatch_delta = after_dispatches - before_dispatches;
            result.forwarding_member_delta = after_forwarded - before_forwarded;
            if (stop_after_receipt) {
                require(after_dispatches > before_dispatches
                        || after_forwarded > before_forwarded,
                    "resuming the exact SV receipt must consume a native member");
            }
        }
        result.input = NativeRegionAllocationTestAccess::snapshot(
            simulation, *input);
        result.output = NativeRegionAllocationTestAccess::snapshot(
            simulation, *output);
    }
    require(!profile.overflowed(),
        "the generic bridge profile buffer must remain complete");
    return result;
}

void test_value_only_rebind_and_policy_snapshot_control()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-quiet-rebind-"
            + std::to_string(nonce)) };
    const auto compiled_root = root.path / "compiled";
    const auto interpreter_root = root.path / "interpreter";
    std::filesystem::create_directories(compiled_root);
    std::filesystem::create_directories(interpreter_root);

    const auto compiled = run_hook_policy_control(
        compiled_root, SimulationEngine::compiled);
    const auto reference = run_hook_policy_control(
        interpreter_root, SimulationEngine::interpreter);
    for (std::size_t signal = 0U; signal < signal_count; ++signal) {
        require(same_semantics(compiled.frame[signal], reference.frame[signal]),
            "a policy-triggered full rebuild must preserve values, owners, "
            "and event/transaction metadata against the interpreter");
    }
    require(compiled.hook_calls != 0U
            && compiled.armed.runtime.snapshot_required
            && compiled.armed.runtime.recertification_pending
            && !compiled.armed.runtime.value_only_candidate_pending
            && compiled.after_rebuild.runtime.runtime_generation
                > compiled.before_hook.runtime.runtime_generation
            && compiled.after_rebuild.runtime.recertification_attempts
                > compiled.before_hook.runtime.recertification_attempts
            && compiled.after_rebuild.runtime.recertification_successes
                > compiled.before_hook.runtime.recertification_successes
            && !compiled.after_rebuild.runtime.snapshot_required
            && !compiled.after_rebuild.runtime.recertification_pending,
        "installing a signal observer must promote quiet recertification to "
        "a full runtime snapshot");
    require(compiled.native_dispatch_delta == 0U
            && compiled.forwarding_member_delta == 0U,
        "the policy-triggered snapshot control must use checked execution "
        "while retaining complete signal-role parity");
    bool replaced_component_state { };
    for (std::size_t signal = 0U; signal < signal_count; ++signal) {
        const auto& before = compiled.before_hook.components[signal];
        const auto& after = compiled.after_rebuild.components[signal];
        replaced_component_state = replaced_component_state
            || (before.authoritative_identity != nullptr
                && before.authoritative_identity != after.authoritative_identity)
            || (before.local_identity != nullptr
                && before.local_identity != after.local_identity);
    }
    require(replaced_component_state,
        "a hard policy cause must publish new component state identities");
}

void test_generic_origin_bridge_preserves_delta_and_receipt()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-generic-bridge-"
            + std::to_string(nonce)) };
    const auto compiled_root = root.path / "compiled";
    const auto interpreter_root = root.path / "interpreter";
    std::filesystem::create_directories(compiled_root);
    std::filesystem::create_directories(interpreter_root);

    const auto compiled = run_generic_bridge_route(compiled_root,
        SimulationEngine::compiled, true);
    const auto reference = run_generic_bridge_route(interpreter_root,
        SimulationEngine::interpreter, false);

    require(compiled.receipt.valid
            && compiled.receipt.time == compiled.callback_time
            && compiled.receipt.delta == 1U
            && compiled.receipt.phase == fsim::runtime::SchedulerPhase::active
            && compiled.receipt.stable_order == compiled.process
            && compiled.receipt.systemverilog_round != 0U,
        "the queued SV task must retain its exact time/delta/Active/order/round key");
    require(compiled.input.event
            == std::pair { compiled.callback_time, std::uint64_t { 1U } }
            && compiled.input.event_domain == ProcessSchedulingDomain::generic
            && compiled.input.event_phase == fsim::runtime::SchedulerPhase::active
            && compiled.input.systemverilog_round == 0U,
        "a generic callback deposit must keep its generic delta-one event stamp");
    require(compiled.output.event
            == std::pair { compiled.callback_time, std::uint64_t { 2U } }
            && compiled.output.event_domain
                == ProcessSchedulingDomain::systemverilog
            && compiled.output.event_phase
                == fsim::runtime::SchedulerPhase::active
            && compiled.output.systemverilog_round
                == compiled.receipt.systemverilog_round + 1U,
        "the SV commit must publish at the next exact round after its activation receipt");
    if (compiled.process != reference.process
        || !same_semantics(compiled.input, reference.input)
        || !same_semantics(compiled.output, reference.output)) {
        const auto report = [](const char* name, const Snapshot& actual,
                               const Snapshot& expected) {
            std::cerr << "bridge parity detail: " << name
                      << " signal=" << actual.signal << '/' << expected.signal
                      << " current=" << actual.current << '/' << expected.current
                      << " last=" << actual.last << '/' << expected.last
                      << " stored=" << actual.stored << '/' << expected.stored
                      << " raw_match=" << (actual.raw_drivers == expected.raw_drivers)
                      << " owned_match=" << (actual.owned_raw == expected.owned_raw)
                      << " external_match=" << (actual.external_raw == expected.external_raw)
                      << " force_match=" << (actual.force_value == expected.force_value)
                      << " mask_match=" << (actual.force_mask == expected.force_mask)
                      << " event_match=" << (actual.event == expected.event)
                      << " transaction_match=" << (actual.transaction == expected.transaction)
                      << " domain=" << static_cast<unsigned>(actual.event_domain)
                      << '/' << static_cast<unsigned>(expected.event_domain)
                      << " phase=" << static_cast<unsigned>(actual.event_phase)
                      << '/' << static_cast<unsigned>(expected.event_phase)
                      << " round=" << actual.systemverilog_round
                      << '/' << expected.systemverilog_round
                      << " now=" << actual.now << '/' << expected.now
                      << " delta=" << actual.delta << '/' << expected.delta << '\n';
        };
        std::cerr << "bridge parity detail: process=" << compiled.process
                  << '/' << reference.process
                  << " native_dispatch_delta=" << compiled.native_dispatch_delta
                  << " forwarding_member_delta=" << compiled.forwarding_member_delta
                  << '\n';
        report("input", compiled.input, reference.input);
        report("output", compiled.output, reference.output);
    }
    require(compiled.process == reference.process
            && same_semantics(compiled.input, reference.input)
            && same_semantics(compiled.output, reference.output),
        "native bridge execution must match the checked interpreter's values and stamps");
}

void test_o0_and_o2_native_frontier_steady_allocation()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-steady-allocation-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : { Optimization::o0, Optimization::o2 }) {
        const auto optimization_name
            = optimization == Optimization::o0 ? "o0" : "o2";
        for (const auto width : { 1U, 65U, 129U }) {
            const auto case_root = root.path / optimization_name
                / ("width-" + std::to_string(width));
            const auto compiled_root = case_root / "compiled";
            const auto interpreter_root = case_root / "interpreter";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(interpreter_root);

            const auto compiled = run_route(optimization, compiled_root,
                width, SimulationEngine::compiled);
            const auto reference = run_route(optimization, interpreter_root,
                width, SimulationEngine::interpreter);
            require(same_frame(compiled.initial_frame, reference.initial_frame),
                "O0/O2 compiled and interpreter initial-settled frames must "
                "match CURRENT, LAST, stored/owner roles, drivers, and stamps");
            require(compiled.initial_frame[0U].current == "0"
                    && compiled.initial_frame[1U].current == "0"
                    && reference.initial_frame[0U].current == "0"
                    && reference.initial_frame[1U].current == "0",
                "the passive initial snapshot must see the actual input zero "
                "and its settled scalar0 output zero");
            require(compiled.measured_frames.size() == measured_window_count
                    && same_frames(compiled.measured_frames,
                        reference.measured_frames),
                "O0/O2 native-frontier scalar and wide internal chains must "
                "match interpreter values and metadata at every measured window");
            require(std::all_of(compiled.dispatch_deltas.begin(),
                        compiled.dispatch_deltas.end(),
                        [](const std::uint64_t delta) { return delta != 0U; }),
                "a successful native-frontier or flattened-forwarding member "
                "must execute in each of the eight measured windows");
        }
    }
}

} // namespace

int main()
{
    try {
        test_value_only_rebind_and_policy_snapshot_control();
        test_o0_and_o2_native_frontier_steady_allocation();
        test_generic_origin_bridge_preserves_delta_and_receipt();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native frontier steady allocation test failure: "
                  << error.what() << '\n';
        return 1;
    }
}

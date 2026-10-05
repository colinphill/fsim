// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using RuntimeImplementation = OwnedDriverDemotionTestAccess::Implementation;
using FrontierRuntime = RuntimeImplementation::RegionFrontierComponentRuntime;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

struct SignalTraceEvent {
    SchedulerTraceKind kind { };
    RuntimeSignalId signal { };
};

struct SignalTraceCapture {
    std::array<SignalTraceEvent, 16U> events { };
    std::size_t count { };
    bool overflow { };
};

void capture_signal_trace(
    void* const context, const SchedulerTraceRecord& record) noexcept
{
    if (record.kind != SchedulerTraceKind::signal_transaction
        && record.kind != SchedulerTraceKind::signal_change) {
        return;
    }
    auto& capture = *static_cast<SignalTraceCapture*>(context);
    if (capture.count == capture.events.size()) {
        capture.overflow = true;
        return;
    }
    capture.events[capture.count++] = { record.kind, record.signal };
}

void fill_role_words(
    AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal,
    const ProcessId owner,
    const std::uint64_t current,
    const std::uint64_t previous,
    const std::uint64_t stored,
    const std::uint64_t raw_owner)
{
    constexpr auto word_count = std::size_t { 1U };
    std::array<std::span<std::uint64_t>, 4U> current_words { };
    std::array<std::span<std::uint64_t>, 4U> previous_words { };
    std::array<std::span<std::uint64_t>, 4U> stored_words { };
    std::array<std::span<std::uint64_t>, 4U> owner_words { };
    require(lease.plane_words(signal, PackedPlaneRole::current, owner,
                current_words)
            && lease.plane_words(signal, PackedPlaneRole::previous, owner,
                previous_words)
            && lease.plane_words(signal, PackedPlaneRole::stored, owner,
                stored_words)
            && lease.plane_words(signal, PackedPlaneRole::owner, owner,
                owner_words),
        "fixture acquires each writable A4 role through the real lease");
    require(current_words[0U].size() == word_count
            && previous_words[0U].size() == word_count
            && stored_words[0U].size() == word_count
            && owner_words[0U].size() == word_count,
        "fixture roles have the certified one-word signal shape");
    current_words[0U][0U] = current;
    previous_words[0U][0U] = previous;
    stored_words[0U][0U] = stored;
    owner_words[0U][0U] = raw_owner;
}

void bind_frame_plane(
    FrontierRuntime& runtime,
    AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const std::size_t plane_index,
    const SignalId signal,
    const ProcessId owner)
{
    std::array<std::span<std::uint64_t>, 4U> current { };
    std::array<std::span<std::uint64_t>, 4U> previous { };
    std::array<std::span<std::uint64_t>, 4U> stored { };
    std::array<std::span<std::uint64_t>, 4U> raw_owner { };
    require(lease.plane_words(signal, PackedPlaneRole::current, owner, current)
            && lease.plane_words(signal, PackedPlaneRole::previous, owner,
                previous)
            && lease.plane_words(signal, PackedPlaneRole::stored, owner,
                stored)
            && lease.plane_words(signal, PackedPlaneRole::owner, owner,
                raw_owner),
        "frame plane pointers come from the active A4 lease");

    auto& plane = runtime.planes[plane_index];
    plane.signal_id = signal;
    plane.owner_process_id = owner;
    plane.value_kind = RegionFrontierValueKindV2::logic4;
    plane.width = 1U;
    plane.word_count = 1U;
    plane.plane_count = kRegionFrontierLogic4PlaneCountV2;
    plane.flags = RegionFrontierPlaneFlagsV2::certified_internal_single_owner;
    plane.metadata_index = static_cast<std::uint32_t>(plane_index);
    plane.current_planes[0U] = current[0U].data();
    plane.current_planes[1U] = current[1U].data();
    plane.previous_planes[0U] = previous[0U].data();
    plane.previous_planes[1U] = previous[1U].data();
    plane.stored_planes[0U] = stored[0U].data();
    plane.stored_planes[1U] = stored[1U].data();
    plane.owner_planes[0U] = raw_owner[0U].data();
    plane.owner_planes[1U] = raw_owner[1U].data();
    require(region_frontier_plane_bindings_valid_v2(plane),
        "fixture exposes a valid typed Logic4 plane descriptor");
}

RegionFrontierSignalMetadataV2 metadata_for(
    const std::uint64_t event_time,
    const std::uint64_t event_delta,
    const std::uint64_t transaction_time,
    const std::uint64_t transaction_delta,
    const std::uint64_t value_revision,
    const std::uint64_t systemverilog_round,
    const bool event_valid)
{
    RegionFrontierSignalMetadataV2 metadata;
    metadata.event_time = event_time;
    metadata.event_delta = event_delta;
    metadata.transaction_time = transaction_time;
    metadata.transaction_delta = transaction_delta;
    metadata.value_revision = value_revision;
    metadata.systemverilog_round = systemverilog_round;
    metadata.event_process_domain = static_cast<std::uint32_t>(
        ProcessSchedulingDomain::systemverilog);
    metadata.event_phase = static_cast<std::uint32_t>(SchedulerPhase::active);
    metadata.event_valid = event_valid ? 1U : 0U;
    metadata.transaction_valid = 1U;
    return metadata;
}

/// Host-sync tests still attach an immutable layout because the synchronizer
/// validates the same certificate/runtime shape as generated execution. This
/// backend deliberately has no entry; these fixtures call only the sync helper.
class SyncOnlyFrontierBackend final : public RegionFrontierBackend {
public:
    explicit SyncOnlyFrontierBackend(const FrontierRuntime& runtime)
    {
        const auto& frame = runtime.frame;
        layout_.struct_size = sizeof(RegionFrontierLayoutV2);
        layout_.certificate_generation = frame.certificate_generation;
        layout_.component_generation = frame.component_generation;
        layout_.member_count = static_cast<std::uint32_t>(runtime.members.size());
        layout_.readiness_word_count = frame.readiness_word_count;
        layout_.signal_slot_count = static_cast<std::uint32_t>(runtime.planes.size());
        layout_.metadata_count = static_cast<std::uint32_t>(runtime.metadata.size());
        layout_.fanout_edge_count = frame.fanout_edge_count;
        layout_.write_site_count
            = static_cast<std::uint32_t>(runtime.pending_writes.size());
        layout_.pending_write_capacity = frame.pending_write_capacity;
        layout_.staged_event_capacity = frame.staged_event_capacity;
        layout_.committed_signal_capacity = frame.committed_signal_capacity;
        signals_.reserve(runtime.planes.size());
        for (const auto& plane : runtime.planes) {
            signals_.push_back({ plane.signal_id, plane.owner_process_id,
                plane.value_kind, plane.width, plane.word_count,
                plane.plane_count, plane.flags, plane.metadata_index });
        }
        layout_.signals = signals_.empty() ? nullptr : signals_.data();
    }

    [[nodiscard]] RegionFrontierStepEntryV2
    step_entry() const noexcept override
    {
        return nullptr;
    }

    [[nodiscard]] const RegionFrontierLayoutV2&
    layout() const noexcept override
    {
        return layout_;
    }

private:
    std::vector<RegionFrontierSignalLayoutV2> signals_;
    RegionFrontierLayoutV2 layout_;
};

void exercise_ordered_log_adapter_sync()
{
    constexpr auto signal_count = std::size_t { 3U };
    constexpr auto log_count = std::size_t { 4U };
    constexpr auto component_generation = std::uint64_t { 41U };
    constexpr auto s0 = SignalId { 0U };
    constexpr auto s1 = SignalId { 1U };
    constexpr auto s2 = SignalId { 2U };
    constexpr auto p0 = ProcessId { 0U };
    constexpr auto p1 = ProcessId { 1U };
    constexpr auto p2 = ProcessId { 2U };
    const PackedLogic4 zero { 1U, Logic4::zero };
    const PackedLogic4 one { 1U, Logic4::one };

    Interpreter interpreter;
    for (std::size_t index = 0U; index < signal_count; ++index) {
        static_cast<void>(interpreter.add_signal({
            "frontier.adapter." + std::to_string(index), zero,
            ResolutionKind::none, ValueKind::logic4 }));
    }
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);

    std::array<Process, signal_count> programs;
    for (std::size_t index = 0U; index < signal_count; ++index) {
        programs[index].id = static_cast<ProcessId>(index);
        programs[index].name = "frontier.adapter.writer." + std::to_string(index);
        programs[index].scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        programs[index].driver_regions = {
            { static_cast<SignalId>(index), 0U, 0U, true }
        };
    }
    const std::array<const Process*, signal_count> program_views {
        &programs[0U], &programs[1U], &programs[2U]
    };
    const std::array descriptors {
        RegionSignalDescriptor { 1U, ResolutionKind::none, ValueKind::logic4 },
        RegionSignalDescriptor { 1U, ResolutionKind::none, ValueKind::logic4 },
        RegionSignalDescriptor { 1U, ResolutionKind::none, ValueKind::logic4 }
    };
    const auto graph = RegionGraph::build(program_views, descriptors);
    const std::array<SignalId, signal_count> component_signals { s0, s1, s2 };
    auto layout = SignalDriverLayout::build(graph, component_signals);
    for (std::size_t index = 0U; index < signal_count; ++index) {
        const auto signal = static_cast<SignalId>(index);
        const auto owners = layout.owners(signal);
        require(layout.signal(signal).storage_class
                    == SignalDriverStorageClass::single_owner
                && owners.size() == 1U
                && owners[0U].process == static_cast<ProcessId>(index),
            "fixture graph certifies one writable owner for each signal");
    }
    const std::array<ProcessId, signal_count> component_members { p0, p1, p2 };
    auto fanout = RegionGroupedFanout::build(program_views, component_members);

    std::array<PackedLogic4, signal_count> stored_values {
        zero, zero, one
    };
    std::array<PackedLogic4, signal_count> raw_owner_values {
        zero, zero, one
    };
    auto state = std::make_shared<RegionAuthoritativeComponentState>(
        component_generation, std::move(layout), std::move(fanout),
        signal_count, PackedSlotBindingPolicy::experimental_wide);
    auto& values = state->values();
    for (std::size_t index = 0U; index < signal_count; ++index) {
        const auto signal = static_cast<SignalId>(index);
        const auto owner = static_cast<ProcessId>(index);
        const auto& current = impl.signals[signal].initial_value;
        values.seed_signal(signal, current, impl.signal_last_values[signal],
            stored_values[index]);
        values.seed_owner(signal, owner, raw_owner_values[index]);
        values.stage_packed_signal_slots(signal,
            impl.signals[signal].initial_value,
            impl.signal_last_values[signal], stored_values[index]);
        values.stage_packed_owner_slot(
            signal, owner, raw_owner_values[index]);
    }
    require(values.bind_packed_slots() == signal_count * 4U
            && values.packed_slots_bound(),
        "fixture binds a real four-role A4 component state");
    values.clear_dirty();

    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding,
        signal_count> writable_signals { {
            { s0, p0 }, { s1, p1 }, { s2, p2 }
        } };
    auto runtime = std::make_shared<FrontierRuntime>();
    runtime->owner = &impl;
    runtime->component = 0U;
    runtime->runtime_generation = component_generation;
    runtime->authoritative_state = state;
    runtime->writable_signals.assign(
        writable_signals.begin(), writable_signals.end());
    runtime->planes.resize(signal_count);
    runtime->metadata.resize(signal_count);
    runtime->committed_signals.resize(log_count);
    runtime->frame.abi_version = kRegionFrontierAbiVersionV2;
    runtime->frame.struct_size = sizeof(RegionFrontierFrameV2);
    runtime->frame.runtime_generation = component_generation;
    runtime->frame.bound_runtime_generation = component_generation;
    runtime->frame.certificate_generation = component_generation;
    runtime->frame.component_generation = component_generation;
    runtime->frame.signal_slot_count = static_cast<std::uint32_t>(signal_count);
    runtime->frame.metadata_count = static_cast<std::uint32_t>(signal_count);
    runtime->frame.committed_signal_capacity = static_cast<std::uint32_t>(log_count);
    runtime->frame.planes = runtime->planes.data();
    runtime->frame.metadata = runtime->metadata.data();
    runtime->frame.committed_signals = runtime->committed_signals.data();
    runtime->frame_initialized = true;

    impl.region_authoritative_state_by_component = { state };
    impl.region_authoritative_component_by_signal.assign(
        impl.signals.size(), RuntimeImplementation::no_systemverilog_update_slot);
    for (const auto signal : component_signals) {
        impl.region_authoritative_component_by_signal[signal] = 0U;
    }
    impl.region_frontier_runtime_by_component = { runtime };

    const auto expected_generation = values.revision();
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    require(values.try_acquire_frontier_write_lease(
                expected_generation, writable_signals, lease)
            && lease.active(),
        "adapter test acquires the genuine component write lease");
    require(values.current(s2) == zero && values.stored(s2) == one
            && values.owner_value(s2, p2) == one,
        "raw stored and owner state begins different from equal current");
    fill_role_words(lease, s0, p0, 0U, 1U, 0U, 0U);
    fill_role_words(lease, s1, p1, 1U, 0U, 1U, 1U);
    fill_role_words(lease, s2, p2, 0U, 0U, 0U, 0U);
    bind_frame_plane(*runtime, lease, 0U, s0, p0);
    bind_frame_plane(*runtime, lease, 1U, s1, p1);
    bind_frame_plane(*runtime, lease, 2U, s2, p2);

    runtime->metadata[0U] = metadata_for(
        20U, 8U, 20U, 8U, 3U, 7U, true);
    runtime->metadata[1U] = metadata_for(
        20U, 9U, 20U, 9U, 2U, 8U, true);
    runtime->metadata[2U] = metadata_for(
        0U, 0U, 20U, 10U, impl.signal_value_revisions[s2], 0U, false);
    runtime->committed_signals[0U] = { 0U, 1U, 1U };
    runtime->committed_signals[1U] = { 1U, 1U, 1U };
    runtime->committed_signals[2U] = { 0U, 1U, 1U };
    runtime->committed_signals[3U] = { 2U, 0U, 1U };
    runtime->frame.committed_signal_count = static_cast<std::uint32_t>(log_count);

    auto backend_entry
        = std::make_shared<RuntimeImplementation::RegionFrontierBackendEntry>();
    backend_entry->provider_identity = "frontier-adapter-sync-only";
    backend_entry->executor
        = std::make_unique<SyncOnlyFrontierBackend>(*runtime);
    runtime->backend = std::move(backend_entry);

    SignalTraceCapture trace;
    impl.scheduler.set_trace_hook(&trace, &capture_signal_trace);
    runtime->synchronize_committed_state(lease);
    require(!runtime->invalidated,
        "a coherent ordered committed log synchronizes successfully");
    require(runtime->frame.committed_signal_count == 0U,
        "successful adapter sync drains the committed-log count");
    require(!trace.overflow && trace.count == 7U,
        "each log record emits one transaction, plus only changed-value notices");
    const std::array expected_trace {
        SignalTraceEvent { SchedulerTraceKind::signal_transaction, s0 },
        SignalTraceEvent { SchedulerTraceKind::signal_change, s0 },
        SignalTraceEvent { SchedulerTraceKind::signal_transaction, s1 },
        SignalTraceEvent { SchedulerTraceKind::signal_change, s1 },
        SignalTraceEvent { SchedulerTraceKind::signal_transaction, s0 },
        SignalTraceEvent { SchedulerTraceKind::signal_change, s0 },
        SignalTraceEvent { SchedulerTraceKind::signal_transaction, s2 }
    };
    for (std::size_t index = 0U; index < expected_trace.size(); ++index) {
        require(trace.events[index].kind == expected_trace[index].kind
                && trace.events[index].signal == expected_trace[index].signal,
            "scheduler notifications preserve ordered duplicate transactions");
    }

    runtime->synchronize_committed_state(lease);
    require(!runtime->invalidated && trace.count == expected_trace.size()
            && runtime->frame.committed_signal_count == 0U,
        "a second adapter sync cannot replay an already drained log");

    require(impl.direct_signal_aval[s0] == 0U
            && impl.direct_signal_last_aval[s0] == 1U
            && impl.direct_signal_aval[s1] == 1U
            && impl.direct_signal_last_aval[s1] == 0U
            && impl.direct_signal_aval[s2] == 0U
            && impl.direct_signal_last_aval[s2] == 0U,
        "distinct changed signals retain their own CURRENT and LAST words");
    require(impl.direct_signal_materialization_pending[s0] == 0U
            && impl.direct_signal_materialization_pending[s1] == 0U
            && impl.direct_signal_materialization_pending[s2] == 0U,
        "A4-bound roles stay authoritative without arming the scalar materializer");
    for (const auto signal : component_signals) {
        const auto offset = static_cast<std::size_t>(
            impl.direct_wide_signal_offsets[signal]);
        require(impl.direct_wide_signal_aval[offset]
                    == impl.direct_signal_aval[signal]
                && impl.direct_wide_signal_bval[offset]
                    == impl.direct_signal_bval[signal],
            "adapter sync copies committed CURRENT words to direct mirrors");
    }

    require(impl.signal_value_revisions[s0] == 3U
            && impl.signal_value_revisions[s1] == 2U
            && impl.signal_value_revisions[s2] == 1U,
        "value revisions follow frame metadata; equal-current role changes do not add one");
    require(impl.signal_transactions[s0]
                && impl.signal_transactions[s0]->first == SimulationTick { 20U }
                && impl.signal_transactions[s0]->second == 8U
            && impl.signal_transactions[s1]
                && impl.signal_transactions[s1]->first == SimulationTick { 20U }
                && impl.signal_transactions[s1]->second == 9U
            && impl.signal_transactions[s2]
                && impl.signal_transactions[s2]->first == SimulationTick { 20U }
                && impl.signal_transactions[s2]->second == 10U,
        "every distinct or repeated commit retains its transaction metadata");
    require(impl.signal_events[s0]
                && impl.signal_events[s0]->first == SimulationTick { 20U }
                && impl.signal_events[s0]->second == 8U
            && impl.signal_events[s1]
                && impl.signal_events[s1]->first == SimulationTick { 20U }
                && impl.signal_events[s1]->second == 9U
            && !impl.signal_events[s2],
        "changed writes publish event metadata while the equal-current write does not");
    require(impl.signal_event_scheduling_stamps[s0].origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && impl.signal_event_scheduling_stamps[s0].origin.phase
                == SchedulerPhase::active
            && impl.signal_event_scheduling_stamps[s0].systemverilog_round == 7U
            && impl.signal_event_scheduling_stamps[s1].systemverilog_round == 8U
            && impl.signal_event_scheduling_stamps[s2].systemverilog_round == 0U,
        "event scheduling stamps stay attached to their signal and final commit");

    const auto dirty = values.dirty_signals();
    require(dirty.size() == signal_count
            && dirty[0U] != 0U && dirty[1U] != 0U && dirty[2U] != 0U,
        "state_changed records dirty A4 for visible and raw-only role mutations");
    require(values.current(s0) == zero && values.previous(s0) == one
            && values.current(s1) == one && values.previous(s1) == zero
            && values.current(s2) == zero && values.previous(s2) == zero
            && values.stored(s2) == zero && values.owner_value(s2, p2) == zero,
        "the equal-current record converges differing stored/owner roles");

    lease.release();
    require(values.revision() == expected_generation + 1U,
        "all ordered A4 role mutations publish one component generation");
    impl.scheduler.set_trace_hook(nullptr, nullptr);
}

} // namespace

void run_native_frontier_adapter_log_tests()
{
    exercise_ordered_log_adapter_sync();
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::run_native_frontier_adapter_log_tests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native frontier adapter sync test failed: "
                  << error.what() << '\n';
        return 1;
    }
}

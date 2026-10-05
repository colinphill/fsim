// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using RuntimeImplementation = OwnedDriverDemotionTestAccess::Implementation;
using FrontierRuntime = RuntimeImplementation::RegionFrontierComponentRuntime;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

void copy_value_to_role(
    AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal,
    const ProcessId owner,
    const PackedPlaneRole role,
    const PackedLogic4& value)
{
    std::array<std::span<std::uint64_t>, 4U> planes;
    require(lease.plane_words(signal, role, owner, planes)
            && planes[0U].size() == value.aval_words().size()
            && planes[1U].size() == value.bval_words().size()
            && planes[2U].empty() && planes[3U].empty(),
        "wide fixture binds the exact mutable A4 role span");
    std::copy(value.aval_words().begin(), value.aval_words().end(),
        planes[0U].begin());
    std::copy(value.bval_words().begin(), value.bval_words().end(),
        planes[1U].begin());
}

/// Host-sync coverage supplies the immutable layout that production sync
/// validates, but deliberately leaves the generated entry null because this
/// fixture directly exercises synchronization and late observation.
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

void exercise_wide_committed_state_observation()
{
    constexpr auto width = std::uint32_t { 129U };
    constexpr auto process_id = ProcessId { 0U };
    constexpr auto signal_id = SignalId { 0U };
    const PackedLogic4 zero { width, Logic4::zero };

    Interpreter interpreter;
    require(interpreter.add_signal({ "frontier.wide.sync",
                zero, ResolutionKind::sv_wire, ValueKind::logic4 }) == signal_id,
        "wide fixture registers its owned signal");
    Process writer;
    writer.id = process_id;
    writer.name = "frontier.wide.sync.writer";
    writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    writer.initialize = true;
    writer.register_count = 1U;
    writer.driver_regions = { { signal_id, 0U, 0U, true } };
    writer.operations = {
        LoadConstant { 0U, zero },
        WriteUpdate { signal_id, 0U,
            SignalUpdateDomain::systemverilog_active },
        Halt { },
    };
    require(interpreter.add_process(std::move(writer)) == process_id,
        "wide fixture registers one exact whole-signal owner");
    interpreter.start();
    const auto startup = interpreter.run(0U);
    require(startup.status != RunStatus::stopped
            && startup.callbacks_executed != 0U,
        "wide fixture executes its original owner before binding A4 roles");

    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    impl.demote_all_region_authoritative_slots(false);
    auto& current = impl.signals[signal_id].initial_value;
    auto& previous = impl.signal_last_values[signal_id];
    auto& stored = impl.driven_values[signal_id];
    auto* const driver = impl.driver_values[signal_id].find(process_id);
    require(driver != nullptr
            && impl.region_runtime_generation != 0U
            && current.width() == width && previous.width() == width
            && stored.width() == width && driver->value.width() == width,
        "wide fixture has full-width current, LAST, stored and original owner roles");

    const auto* const registered_process = &interpreter.process_program(process_id);
    const std::array<const Process*, 1U> process_views { registered_process };
    const std::array<RegionSignalDescriptor, 1U> signal_descriptors { {
        { width, ResolutionKind::sv_wire, ValueKind::logic4 }
    } };
    const auto graph = RegionGraph::build(process_views, signal_descriptors);
    const std::array<SignalId, 1U> component_signals { signal_id };
    auto driver_layout = SignalDriverLayout::build(graph, component_signals);
    const std::array<ProcessId, 1U> component_members { process_id };
    auto fanout = RegionGroupedFanout::build(process_views, component_members);
    auto state = std::make_shared<RegionAuthoritativeComponentState>(
        impl.region_runtime_generation, std::move(driver_layout),
        std::move(fanout), 1U, PackedSlotBindingPolicy::experimental_wide);
    auto& values = state->values();
    values.seed_signal(signal_id, current, previous, stored);
    values.seed_owner(signal_id, process_id, driver->value);
    values.stage_packed_signal_slots(
        signal_id, current, previous, stored);
    values.stage_packed_owner_slot(signal_id, process_id, driver->value);
    require(values.bind_packed_slots() == 4U
            && values.packed_signal_slots_bound(signal_id)
            && values.packed_owner_slot_bound(signal_id, process_id),
        "wide fixture binds all four public PackedLogic4 roles to A4 planes");

    const auto component = impl.region_authoritative_state_by_component.size();
    impl.region_authoritative_state_by_component.push_back(state);
    if (impl.region_authoritative_component_by_signal.size()
        < impl.signals.size()) {
        impl.region_authoritative_component_by_signal.resize(
            impl.signals.size(),
            std::numeric_limits<std::size_t>::max());
    }
    impl.region_authoritative_component_by_signal[signal_id] = component;

    auto next_current = PackedLogic4 { width, Logic4::zero };
    next_current.set(64U, Logic4::x);
    next_current.set(128U, Logic4::one);
    auto next_previous = PackedLogic4 { width, Logic4::zero };
    next_previous.set(63U, Logic4::one);
    next_previous.set(127U, Logic4::z);
    const auto initial_generation = values.revision();
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 1U>
        writable { { { signal_id, process_id } } };
    require(values.try_acquire_frontier_write_lease(
                initial_generation, writable, lease)
            && lease.active(),
        "wide sync fixture acquires the real A4 frontier write lease");
    copy_value_to_role(lease, signal_id, process_id,
        PackedPlaneRole::current, next_current);
    copy_value_to_role(lease, signal_id, process_id,
        PackedPlaneRole::previous, next_previous);
    copy_value_to_role(lease, signal_id, process_id,
        PackedPlaneRole::stored, next_current);
    copy_value_to_role(lease, signal_id, process_id,
        PackedPlaneRole::owner, next_current);

    auto runtime = std::make_shared<FrontierRuntime>();
    runtime->owner = &impl;
    runtime->component = component;
    runtime->runtime_generation = impl.region_runtime_generation;
    runtime->authoritative_state = state;
    runtime->writable_signals.assign(writable.begin(), writable.end());
    runtime->planes.resize(1U);
    runtime->metadata.resize(1U);
    runtime->committed_signals.resize(1U);
    auto& plane = runtime->planes[0U];
    plane.signal_id = signal_id;
    plane.owner_process_id = process_id;
    plane.value_kind = RegionFrontierValueKindV2::logic4;
    plane.width = width;
    plane.word_count = 3U;
    plane.plane_count = kRegionFrontierLogic4PlaneCountV2;
    plane.flags = RegionFrontierPlaneFlagsV2::certified_internal_single_owner;
    plane.metadata_index = 0U;
    std::array<std::span<std::uint64_t>, 4U> current_words;
    std::array<std::span<std::uint64_t>, 4U> previous_words;
    std::array<std::span<std::uint64_t>, 4U> stored_words;
    std::array<std::span<std::uint64_t>, 4U> owner_words;
    require(lease.plane_words(signal_id, PackedPlaneRole::current,
                process_id, current_words)
            && lease.plane_words(signal_id, PackedPlaneRole::previous,
                process_id, previous_words)
            && lease.plane_words(signal_id, PackedPlaneRole::stored,
                process_id, stored_words)
            && lease.plane_words(signal_id, PackedPlaneRole::owner,
                process_id, owner_words),
        "wide frame captures all four active lease roles");
    plane.current_planes[0U] = current_words[0U].data();
    plane.current_planes[1U] = current_words[1U].data();
    plane.previous_planes[0U] = previous_words[0U].data();
    plane.previous_planes[1U] = previous_words[1U].data();
    plane.stored_planes[0U] = stored_words[0U].data();
    plane.stored_planes[1U] = stored_words[1U].data();
    plane.owner_planes[0U] = owner_words[0U].data();
    plane.owner_planes[1U] = owner_words[1U].data();
    require(region_frontier_plane_bindings_valid_v2(plane),
        "wide fixture exposes a valid typed Logic4 plane descriptor");

    const auto next_revision = impl.signal_value_revisions[signal_id] + 1U;
    auto& metadata = runtime->metadata[0U];
    metadata.event_time = 9U;
    metadata.event_delta = 4U;
    metadata.transaction_time = 9U;
    metadata.transaction_delta = 4U;
    metadata.value_revision = next_revision;
    metadata.systemverilog_round = 2U;
    metadata.event_process_domain = static_cast<std::uint32_t>(
        ProcessSchedulingDomain::systemverilog);
    metadata.event_phase = static_cast<std::uint32_t>(SchedulerPhase::active);
    metadata.event_valid = 1U;
    metadata.transaction_valid = 1U;
    runtime->committed_signals[0U] = { 0U, 1U, 1U };

    auto& frame = runtime->frame;
    frame.abi_version = kRegionFrontierAbiVersionV2;
    frame.struct_size = sizeof(RegionFrontierFrameV2);
    frame.runtime_generation = impl.region_runtime_generation;
    frame.bound_runtime_generation = impl.region_runtime_generation;
    frame.certificate_generation = impl.region_runtime_generation;
    frame.component_generation = impl.region_runtime_generation;
    frame.signal_slot_count = 1U;
    frame.metadata_count = 1U;
    frame.committed_signal_capacity = 1U;
    frame.committed_signal_count = 1U;
    frame.planes = runtime->planes.data();
    frame.metadata = runtime->metadata.data();
    frame.committed_signals = runtime->committed_signals.data();
    runtime->frame_initialized = true;

    auto backend_entry
        = std::make_shared<RuntimeImplementation::RegionFrontierBackendEntry>();
    backend_entry->provider_identity = "frontier-wide-sync-only";
    backend_entry->executor
        = std::make_unique<SyncOnlyFrontierBackend>(*runtime);
    runtime->backend = std::move(backend_entry);

    runtime->synchronize_committed_state(lease);
    require(!runtime->invalidated
            && runtime->frame.committed_signal_count == 0U
            && impl.direct_signal_materialization_pending[signal_id] == 0U,
        "wide native sync drains its log without arming the scalar-word materializer");
    require(values.current(signal_id) == next_current
            && values.previous(signal_id) == next_previous
            && values.stored(signal_id) == next_current
            && values.owner_value(signal_id, process_id) == next_current,
        "the wide A4 roles retain the complete committed planes");
    const auto wide_offset = static_cast<std::size_t>(
        impl.direct_wide_signal_offsets[signal_id]);
    require(std::equal(next_current.aval_words().begin(),
                next_current.aval_words().end(),
                impl.direct_wide_signal_aval.begin()
                    + static_cast<std::ptrdiff_t>(wide_offset))
            && std::equal(next_current.bval_words().begin(),
                next_current.bval_words().end(),
                impl.direct_wide_signal_bval.begin()
                    + static_cast<std::ptrdiff_t>(wide_offset)),
        "the direct-wide current mirror receives every committed word");
    lease.release();
    require(values.revision() == initial_generation + 1U,
        "the ordered native commit publishes one A4 generation");

    const auto observed = interpreter.signal_value_snapshot(signal_id);
    const auto* const observed_driver
        = impl.driver_values[signal_id].find(process_id);
    require(observed == next_current
            && impl.signals[signal_id].initial_value == next_current
            && impl.signal_last_values[signal_id] == next_previous
            && impl.driven_values[signal_id] == next_current
            && observed_driver != nullptr
            && observed_driver->value == next_current,
        "late observation materializes full-width CURRENT, LAST, STORED and original-owner values");
    require(!values.packed_slots_bound()
            && values.current(signal_id) == next_current
            && values.previous(signal_id) == next_previous
            && values.stored(signal_id) == next_current
            && values.owner_value(signal_id, process_id) == next_current,
        "observation demotes the component only after its four role values are preserved");
}

} // namespace

void run_native_frontier_wide_sync_tests()
{
    exercise_wide_committed_state_observation();
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::run_native_frontier_wide_sync_tests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native frontier wide sync test failed: "
                  << error.what() << '\n';
        return 1;
    }
}

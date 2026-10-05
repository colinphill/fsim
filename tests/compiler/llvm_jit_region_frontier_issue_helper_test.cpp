// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"
#include "llvm_jit_region_frontier_issue_helper_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {

enum class PrefixKind {
    imported_wave_activation,
    native_internal_commit,
};

enum class TamperKind {
    none,
    original_payload,
    original_sequence,
    translated_mapping,
    ready_pointer,
    ready_count,
};

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] bool same_key(const runtime::simir::RegionFrontierKeyV2& left,
    const runtime::simir::RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_member(
    const runtime::simir::RegionFrontierMemberV2& left,
    const runtime::simir::RegionFrontierMemberV2& right) noexcept
{
    return left.process_id == right.process_id
        && left.flags == right.flags
        && left.static_trigger_mask == right.static_trigger_mask
        && same_key(left.queued_key, right.queued_key)
        && same_key(left.activation_origin, right.activation_origin)
        && same_key(left.pending_activation_origin,
            right.pending_activation_origin);
}

[[nodiscard]] bool same_write(
    const runtime::simir::RegionFrontierPendingWriteV2& left,
    const runtime::simir::RegionFrontierPendingWriteV2& right) noexcept
{
    return left.member_index == right.member_index
        && left.signal_slot == right.signal_slot
        && left.source_instruction == right.source_instruction
        && left.update_kind == right.update_kind
        && left.flags == right.flags
        && left.reserved == right.reserved
        && same_key(left.commit_key, right.commit_key)
        && same_key(left.origin, right.origin)
        && left.value_kind == right.value_kind
        && left.width == right.width
        && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && std::equal(std::begin(left.value_planes),
            std::end(left.value_planes), std::begin(right.value_planes));
}

[[nodiscard]] bool same_event(
    const runtime::simir::RegionFrontierStagedEventV2& left,
    const runtime::simir::RegionFrontierStagedEventV2& right) noexcept
{
    return left.kind == right.kind
        && left.descriptor_index == right.descriptor_index
        && left.stable_order == right.stable_order
        && same_key(left.origin, right.origin);
}

[[nodiscard]] bool same_native_task(
    const runtime::simir::RegionFrontierSchedulerTaskV2& left,
    const runtime::simir::RegionFrontierSchedulerTaskV2& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.payload == right.payload;
}

[[nodiscard]] bool same_original_task(
    const runtime::SchedulerBatchFrontierEntry& left,
    const runtime::SchedulerBatchFrontierEntry& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.payload == right.payload;
}

[[nodiscard]] bool same_plane(
    const runtime::simir::RegionFrontierPlaneV2& left,
    const runtime::simir::RegionFrontierPlaneV2& right) noexcept
{
    return left.signal_id == right.signal_id
        && left.owner_process_id == right.owner_process_id
        && left.value_kind == right.value_kind
        && left.width == right.width
        && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && left.flags == right.flags
        && left.metadata_index == right.metadata_index
        && std::equal(std::begin(left.boundary_planes),
            std::end(left.boundary_planes), std::begin(right.boundary_planes))
        && std::equal(std::begin(left.current_planes),
            std::end(left.current_planes), std::begin(right.current_planes))
        && std::equal(std::begin(left.previous_planes),
            std::end(left.previous_planes), std::begin(right.previous_planes))
        && std::equal(std::begin(left.stored_planes),
            std::end(left.stored_planes), std::begin(right.stored_planes))
        && std::equal(std::begin(left.owner_planes),
            std::end(left.owner_planes), std::begin(right.owner_planes));
}

[[nodiscard]] bool same_metadata(
    const runtime::simir::RegionFrontierSignalMetadataV2& left,
    const runtime::simir::RegionFrontierSignalMetadataV2& right) noexcept
{
    return left.event_time == right.event_time
        && left.event_delta == right.event_delta
        && left.transaction_time == right.transaction_time
        && left.transaction_delta == right.transaction_delta
        && left.value_revision == right.value_revision
        && left.systemverilog_round == right.systemverilog_round
        && left.event_process_domain == right.event_process_domain
        && left.event_phase == right.event_phase
        && left.event_valid == right.event_valid
        && left.transaction_valid == right.transaction_valid
        && std::equal(std::begin(left.reserved), std::end(left.reserved),
            std::begin(right.reserved));
}

[[nodiscard]] bool same_committed_signal(
    const runtime::simir::RegionFrontierCommittedSignalV2& left,
    const runtime::simir::RegionFrontierCommittedSignalV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.changed == right.changed
        && left.state_changed == right.state_changed;
}

[[nodiscard]] bool same_frame(
    const runtime::simir::RegionFrontierFrameV2& left,
    const runtime::simir::RegionFrontierFrameV2& right) noexcept
{
    return left.abi_version == right.abi_version
        && left.struct_size == right.struct_size
        && left.value_plane_contract == right.value_plane_contract
        && left.generic_update_ack_count == right.generic_update_ack_count
        && left.runtime_generation == right.runtime_generation
        && left.bound_runtime_generation == right.bound_runtime_generation
        && left.certificate_generation == right.certificate_generation
        && left.component_generation == right.component_generation
        && left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && left.member_count == right.member_count
        && left.scheduler_task_count == right.scheduler_task_count
        && left.scheduler_task_cursor == right.scheduler_task_cursor
        && left.scheduler_task_capacity == right.scheduler_task_capacity
        && left.readiness_word_count == right.readiness_word_count
        && left.signal_slot_count == right.signal_slot_count
        && left.metadata_count == right.metadata_count
        && left.fanout_edge_count == right.fanout_edge_count
        && left.committed_signal_capacity == right.committed_signal_capacity
        && left.committed_signal_count == right.committed_signal_count
        && left.pending_write_capacity == right.pending_write_capacity
        && left.pending_write_count == right.pending_write_count
        && left.staged_event_capacity == right.staged_event_capacity
        && left.staged_event_count == right.staged_event_count
        && left.current_member == right.current_member
        && left.current_pending_write == right.current_pending_write
        && left.current_commit_changed == right.current_commit_changed
        && left.saved_body_pc == right.saved_body_pc
        && left.ready_words == right.ready_words
        && left.members == right.members
        && left.scheduler_tasks == right.scheduler_tasks
        && left.planes == right.planes
        && left.metadata == right.metadata
        && left.fanout_edges == right.fanout_edges
        && left.port_planes == right.port_planes
        && left.pending_writes == right.pending_writes
        && left.staged_events == right.staged_events
        && left.committed_signals == right.committed_signals
        && left.native_frontier_member_dispatches
            == right.native_frontier_member_dispatches
        && left.stop_requested == right.stop_requested
        && left.slot.time == right.slot.time
        && left.slot.delta == right.slot.delta
        && left.slot.systemverilog_round == right.slot.systemverilog_round
        && left.slot.process_domain == right.slot.process_domain
        && left.slot.phase == right.slot.phase
        && left.cut.scheduler_frontier_generation
            == right.cut.scheduler_frontier_generation
        && left.cut.next_key.time == right.cut.next_key.time
        && left.cut.next_key.delta == right.cut.next_key.delta
        && left.cut.next_key.systemverilog_round
            == right.cut.next_key.systemverilog_round
        && left.cut.next_key.stable_order
            == right.cut.next_key.stable_order
        && left.cut.next_key.sequence == right.cut.next_key.sequence
        && left.cut.next_key.process_domain
            == right.cut.next_key.process_domain
        && left.cut.next_key.phase == right.cut.next_key.phase
        && left.cut.kind == right.cut.kind;
}

[[nodiscard]] bool same_compaction_stats(
    const runtime::SchedulerBatchCompactionStats& left,
    const runtime::SchedulerBatchCompactionStats& right) noexcept
{
    return left.tickets == right.tickets
        && left.members == right.members
        && left.entries_elided == right.entries_elided
        && left.direct_dispatches == right.direct_dispatches
        && left.direct_members == right.direct_members
        && left.readiness_ticket_queue_insertions
            == right.readiness_ticket_queue_insertions
        && left.readiness_ticket_members == right.readiness_ticket_members
        && left.readiness_ticket_members_elided
            == right.readiness_ticket_members_elided
        && left.readiness_ticket_fallback_members
            == right.readiness_ticket_fallback_members;
}

[[nodiscard]] bool same_compact_member(
    const runtime::SystemVerilogCompactBatchMember& left,
    const runtime::SystemVerilogCompactBatchMember& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.payload == right.payload;
}

[[nodiscard]] bool same_fanout_edge(
    const runtime::simir::RegionFrontierFanoutEdgeV2& left,
    const runtime::simir::RegionFrontierFanoutEdgeV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.member_index == right.member_index
        && left.trigger_mask == right.trigger_mask;
}

[[nodiscard]] std::size_t member_index_for_process(
    const runtime::simir::RegionFrontierLayoutV2& layout,
    const runtime::simir::ProcessId process)
{
    for (std::size_t index = 0U; index < layout.member_count; ++index) {
        if (layout.members[index].process_id == process) {
            return index;
        }
    }
    throw std::runtime_error { "the process has a local frontier member" };
}

[[nodiscard]] std::size_t plane_index_for_signal(
    const runtime::simir::RegionFrontierLayoutV2& layout,
    const runtime::simir::SignalId signal)
{
    for (std::size_t index = 0U; index < layout.signal_slot_count; ++index) {
        if (layout.signals[index].signal_id == signal) {
            return index;
        }
    }
    throw std::runtime_error { "the signal has a canonical frontier plane" };
}

[[nodiscard]] std::size_t internal_write_slot(
    const runtime::simir::RegionFrontierLayoutV2& layout)
{
    for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
        const auto& site = layout.write_sites[index];
        if (site.event_kind == static_cast<std::uint32_t>(
                runtime::simir::RegionFrontierEventKindV2::internal_commit)) {
            return site.pending_slot;
        }
    }
    throw std::runtime_error { "the fixture has an internal commit site" };
}

struct PlaneBacking final {
    std::array<std::vector<std::uint64_t>, 4U> boundary;
    std::array<std::vector<std::uint64_t>, 4U> current;
    std::array<std::vector<std::uint64_t>, 4U> previous;
    std::array<std::vector<std::uint64_t>, 4U> stored;
    std::array<std::vector<std::uint64_t>, 4U> owner;

    friend bool operator==(const PlaneBacking&, const PlaneBacking&) = default;
};

struct IssueFrameStorage final {
    static constexpr std::size_t scheduler_task_capacity = 64U;

    IssueFrameStorage(const runtime::simir::RegionConeActivationKernel& kernel,
        const runtime::simir::RegionFrontierLayoutV2& layout)
        : planes(layout.signal_slot_count)
        , plane_backing(layout.signal_slot_count)
        , metadata(layout.metadata_count)
        , fanout_edges(layout.fanout_edges,
              layout.fanout_edges + layout.fanout_edge_count)
        , port_planes(layout.signal_slot_count)
        , pending_writes(layout.pending_write_capacity)
        , staged_events(layout.staged_event_capacity)
        , committed_signals(layout.committed_signal_capacity)
    {
        require(layout.abi_version == runtime::simir::kRegionFrontierAbiVersionV2
                && layout.struct_size == sizeof(runtime::simir::RegionFrontierLayoutV2)
                && runtime::simir::region_frontier_layout_header_valid_v2(layout)
                && layout.member_count == kernel.members.size()
                && layout.member_count == 2U
                && layout.members != nullptr && layout.signals != nullptr
                && layout.write_sites != nullptr
                && layout.max_member_staged_event_counts != nullptr
                && layout.write_site_count == 2U
                && layout.signal_slot_count == 3U
                && layout.readiness_word_count == 1U
                && layout.fanout_edges != nullptr
                && layout.fanout_edge_count == 1U,
            "the helper fixture is a certified one-bit two-member chain");

        ready_words.assign(layout.readiness_word_count, 0U);
        members.resize(layout.member_count);
        for (std::size_t index = 0U; index < layout.member_count; ++index) {
            members[index].process_id = layout.members[index].process_id;
            members[index].flags
                = runtime::simir::RegionFrontierMemberFlagsV2::waiting_on_static;
            members[index].static_trigger_mask = UINT64_MAX;
        }

        for (std::size_t index = 0U; index < layout.signal_slot_count; ++index) {
            const auto& descriptor = layout.signals[index];
            auto& plane = planes[index];
            auto& backing = plane_backing[index];
            const auto words = static_cast<std::size_t>(descriptor.word_count);
            require(descriptor.value_kind
                        == runtime::simir::RegionFrontierValueKindV2::logic4
                    && descriptor.width == 1U && words == 1U
                    && descriptor.plane_count
                        == runtime::simir::kRegionFrontierLogic4PlaneCountV2,
                "each helper fixture plane is one bit");
            plane.signal_id = descriptor.signal_id;
            plane.owner_process_id = descriptor.owner_process_id;
            plane.value_kind = descriptor.value_kind;
            plane.width = descriptor.width;
            plane.word_count = descriptor.word_count;
            plane.plane_count = descriptor.plane_count;
            plane.flags = descriptor.flags;
            plane.metadata_index = descriptor.metadata_index;
            if ((descriptor.flags
                    & runtime::simir::RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U) {
                for (std::size_t plane_index = 0U;
                     plane_index < descriptor.plane_count; ++plane_index) {
                    backing.current[plane_index].assign(words, 0U);
                    backing.previous[plane_index].assign(words, 0U);
                    backing.stored[plane_index].assign(words, 0U);
                    backing.owner[plane_index].assign(words, 0U);
                    plane.current_planes[plane_index]
                        = backing.current[plane_index].data();
                    plane.previous_planes[plane_index]
                        = backing.previous[plane_index].data();
                    plane.stored_planes[plane_index]
                        = backing.stored[plane_index].data();
                    plane.owner_planes[plane_index]
                        = backing.owner[plane_index].data();
                }
            } else {
                require((descriptor.flags
                            & runtime::simir::RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                        != 0U,
                    "every non-internal signal is a read-only boundary plane");
                for (std::size_t plane_index = 0U;
                     plane_index < descriptor.plane_count; ++plane_index) {
                    backing.boundary[plane_index].assign(words, 0U);
                    plane.boundary_planes[plane_index]
                        = backing.boundary[plane_index].data();
                }
            }
            require(runtime::simir::region_frontier_plane_bindings_valid_v2(
                        plane),
                "each helper fixture plane has valid typed bindings");
            port_planes[index] = &plane;
        }

        const auto source_slot = plane_index_for_signal(layout, 0U);
        plane_backing[source_slot].boundary[0U][0U] = 1U;
        for (auto& edge : fanout_edges) {
            edge.trigger_mask = 1U;
        }

        for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
            const auto& site = layout.write_sites[index];
            auto& write = pending_writes[site.pending_slot];
            write.member_index = site.member_index;
            write.signal_slot = site.signal_slot;
            write.source_instruction = site.source_instruction;
            write.update_kind = site.update_kind;
            write.value_kind = site.value_kind;
            write.width = site.width;
            write.word_count = site.word_count;
            write.plane_count = site.plane_count;
        }

        frame.abi_version = runtime::simir::kRegionFrontierAbiVersionV2;
        frame.struct_size = sizeof(runtime::simir::RegionFrontierFrameV2);
        frame.runtime_generation = 41U;
        frame.bound_runtime_generation = 41U;
        frame.certificate_generation = layout.certificate_generation;
        frame.component_generation = layout.component_generation;
        frame.member_count = layout.member_count;
        frame.scheduler_task_capacity
            = static_cast<std::uint32_t>(scheduler_task_capacity);
        frame.readiness_word_count = layout.readiness_word_count;
        frame.signal_slot_count = layout.signal_slot_count;
        frame.metadata_count = layout.metadata_count;
        frame.fanout_edge_count = layout.fanout_edge_count;
        frame.committed_signal_capacity = layout.committed_signal_capacity;
        frame.pending_write_capacity = layout.pending_write_capacity;
        frame.staged_event_capacity = layout.staged_event_capacity;
        frame.current_member = UINT32_MAX;
        frame.current_pending_write = UINT32_MAX;
        frame.ready_words = ready_words.data();
        frame.members = members.data();
        frame.scheduler_tasks = scheduler_tasks.data();
        frame.planes = planes.data();
        frame.metadata = metadata.data();
        frame.fanout_edges = fanout_edges.data();
        frame.port_planes = port_planes.data();
        frame.pending_writes = pending_writes.data();
        frame.staged_events = staged_events.data();
        frame.committed_signals = committed_signals.data();
        frame.slot.process_domain = static_cast<std::uint32_t>(
            runtime::simir::ProcessSchedulingDomain::systemverilog);
        frame.slot.phase = static_cast<std::uint32_t>(
            runtime::SchedulerPhase::active);
        frame.cut.kind = runtime::simir::RegionFrontierCutKindV2::closed_prefix;
    }

    runtime::simir::RegionFrontierFrameV2 frame;
    std::vector<std::uint64_t> ready_words;
    std::vector<runtime::simir::RegionFrontierMemberV2> members;
    std::vector<runtime::simir::RegionFrontierSchedulerTaskV2> scheduler_tasks
        = std::vector<runtime::simir::RegionFrontierSchedulerTaskV2>(
            scheduler_task_capacity);
    std::vector<runtime::simir::RegionFrontierPlaneV2> planes;
    std::vector<PlaneBacking> plane_backing;
    std::vector<runtime::simir::RegionFrontierSignalMetadataV2> metadata;
    std::vector<runtime::simir::RegionFrontierFanoutEdgeV2> fanout_edges;
    std::vector<const runtime::simir::RegionFrontierPlaneV2*> port_planes;
    std::vector<runtime::simir::RegionFrontierPendingWriteV2> pending_writes;
    std::vector<runtime::simir::RegionFrontierStagedEventV2> staged_events;
    std::vector<runtime::simir::RegionFrontierCommittedSignalV2>
        committed_signals;
};

struct BatchTestTask final : runtime::SchedulerBatchTask {
    using Callback = std::function<runtime::SchedulerBatchResult(
        runtime::Scheduler&, std::span<const std::uint64_t>)>;

    explicit BatchTestTask(Callback callback)
        : callback_ { std::move(callback) }
    {
    }

    [[nodiscard]] runtime::SchedulerBatchResult execute(
        runtime::Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        return callback_(scheduler, payloads);
    }

private:
    Callback callback_;
};

} // namespace
} // namespace fsim::compiler::test

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess final {
    static void run(const RegionConeActivationKernel& kernel,
        std::unique_ptr<RegionFrontierBackend> backend);

private:
    static void run_case(Interpreter::Impl& owner,
        const std::shared_ptr<Interpreter::Impl::RegionFrontierBackendEntry>& backend,
        const RegionConeActivationKernel& kernel,
        const std::uint64_t wave_payload,
        fsim::compiler::test::PrefixKind prefix_kind,
        fsim::compiler::test::TamperKind tamper,
        bool expect_success,
        bool request_stop_before_issue);
};

namespace {

void require_runtime(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

} // namespace

void NativeRegionAllocationTestAccess::run(
    const RegionConeActivationKernel& kernel,
    std::unique_ptr<RegionFrontierBackend> backend)
{
    require_runtime(backend != nullptr && backend->step_entry() != nullptr,
        "the helper regression owns a generated native entry");
    Interpreter interpreter;
    auto& owner = *interpreter.impl_;
    auto entry = std::make_shared<Interpreter::Impl::RegionFrontierBackendEntry>();
    entry->kernel = kernel;
    entry->provider_identity = "frontier-issue-helper-test";
    entry->executor = std::move(backend);

    const auto wave_payload = owner.systemverilog_wave_payload;
    for (const auto tamper : {
             fsim::compiler::test::TamperKind::original_payload,
             fsim::compiler::test::TamperKind::original_sequence,
             fsim::compiler::test::TamperKind::translated_mapping,
             fsim::compiler::test::TamperKind::ready_pointer,
             fsim::compiler::test::TamperKind::ready_count,
         }) {
        run_case(owner, entry, kernel, wave_payload,
            fsim::compiler::test::PrefixKind::imported_wave_activation,
            tamper, false, false);
    }
    run_case(owner, entry, kernel, wave_payload,
        fsim::compiler::test::PrefixKind::imported_wave_activation,
        fsim::compiler::test::TamperKind::none, true, true);
    run_case(owner, entry, kernel, wave_payload,
        fsim::compiler::test::PrefixKind::native_internal_commit,
        fsim::compiler::test::TamperKind::none, true, true);
}

void NativeRegionAllocationTestAccess::run_case(
    Interpreter::Impl& owner,
    const std::shared_ptr<Interpreter::Impl::RegionFrontierBackendEntry>& backend,
    const RegionConeActivationKernel& kernel,
    const std::uint64_t wave_payload,
    const fsim::compiler::test::PrefixKind prefix_kind,
    const fsim::compiler::test::TamperKind tamper,
    const bool expect_success,
    const bool request_stop_before_issue)
{
    using namespace fsim::compiler::test;
    const auto& layout = backend->executor->layout();
    auto runtime = std::make_shared<Interpreter::Impl::RegionFrontierComponentRuntime>();
    IssueFrameStorage storage { kernel, layout };

    runtime->owner = &owner;
    runtime->component = 0U;
    runtime->runtime_generation = storage.frame.runtime_generation;
    runtime->backend = backend;
    runtime->frame = storage.frame;
    runtime->ready_words.assign(
        storage.ready_words.begin(), storage.ready_words.end());
    runtime->members.assign(storage.members.begin(), storage.members.end());
    runtime->scheduler_tasks.assign(IssueFrameStorage::scheduler_task_capacity, { });
    runtime->original_scheduler_tasks.assign(
        IssueFrameStorage::scheduler_task_capacity, { });
    runtime->planes.assign(storage.planes.begin(), storage.planes.end());
    runtime->metadata.assign(storage.metadata.begin(), storage.metadata.end());
    runtime->fanout_edges.assign(
        storage.fanout_edges.begin(), storage.fanout_edges.end());
    runtime->port_planes.resize(storage.planes.size(), nullptr);
    runtime->pending_writes.assign(
        storage.pending_writes.begin(), storage.pending_writes.end());
    runtime->staged_events.assign(
        storage.staged_events.begin(), storage.staged_events.end());
    runtime->committed_signals.assign(
        storage.committed_signals.begin(), storage.committed_signals.end());
    runtime->compact_members.resize(storage.staged_events.size());
    runtime->issued_sequences.resize(storage.staged_events.size());
    runtime->frame_initialized = true;
    runtime->pending_plane_offsets.assign(layout.pending_write_capacity,
        std::numeric_limits<std::size_t>::max());
    std::size_t pending_word_count { };
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        const auto plane_words = static_cast<std::size_t>(site.word_count)
            * site.plane_count;
        require_runtime(site.pending_slot < runtime->pending_plane_offsets.size()
                && plane_words <= std::numeric_limits<std::size_t>::max()
                    - pending_word_count,
            "typed pending plane extents fit their private frame backing");
        runtime->pending_plane_offsets[site.pending_slot]
            = pending_word_count;
        pending_word_count += plane_words;
    }
    runtime->pending_plane_words.assign(pending_word_count, 0U);
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        auto& write = runtime->pending_writes[site.pending_slot];
        const auto offset = runtime->pending_plane_offsets[site.pending_slot];
        for (std::size_t plane_index = 0U;
             plane_index < site.plane_count; ++plane_index) {
            write.value_planes[plane_index]
                = runtime->pending_plane_words.data() + offset
                    + plane_index * site.word_count;
        }
        require_runtime(region_frontier_pending_write_bindings_valid_v2(write),
            "typed pending writes point into the runtime-owned plane storage");
    }
    for (std::size_t index = 0U; index < runtime->planes.size(); ++index) {
        runtime->port_planes[index] = &runtime->planes[index];
    }
    runtime->frame.ready_words = runtime->ready_words.data();
    runtime->frame.members = runtime->members.data();
    runtime->frame.scheduler_tasks = runtime->scheduler_tasks.data();
    runtime->frame.planes = runtime->planes.data();
    runtime->frame.metadata = runtime->metadata.data();
    runtime->frame.fanout_edges = runtime->fanout_edges.data();
    runtime->frame.port_planes = runtime->port_planes.data();
    runtime->frame.pending_writes = runtime->pending_writes.data();
    runtime->frame.staged_events = runtime->staged_events.data();
    runtime->frame.committed_signals = runtime->committed_signals.data();
    runtime->frame.native_frontier_member_dispatches
        = &runtime->native_member_dispatches;
    runtime->frame.stop_requested = &runtime->stop_requested;

    const auto root_member = member_index_for_process(
        layout, kernel.members.front().process);
    const auto child_member = member_index_for_process(
        layout, kernel.members.back().process);
    require_runtime(root_member == 0U && child_member == 1U
            && kernel.members.front().process == 7U
            && kernel.members.back().process == 23U,
        "the imported raw ProcessIds 7 and 23 map to local member indexes 0 and 1");
    const auto write_slot = internal_write_slot(layout);
    if (prefix_kind == PrefixKind::native_internal_commit) {
        auto& write = runtime->pending_writes[write_slot];
        write.flags = RegionFrontierPendingWriteFlagsV2::pending_active
            | RegionFrontierPendingWriteFlagsV2::pending_value_ready
            | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
            | RegionFrontierPendingWriteFlagsV2::pending_internal_target;
        write.value_planes[0U][0U] = 1U;
        write.value_planes[1U][0U] = 0U;
        runtime->frame.pending_write_count = 1U;
    }

    bool callback_ran { };
    bool helper_result { };
    bool sentinel_ran { };
    std::string callback_error;
    std::uint64_t source_sequence { };
    std::uint64_t sentinel_sequence { };
    std::optional<std::uint64_t> target_round;
    const auto compact_before
        = owner.scheduler.systemverilog_batch_compaction_stats();
    const auto source_payload = prefix_kind == PrefixKind::imported_wave_activation
        ? wave_payload | static_cast<std::uint64_t>(kernel.members.front().process)
        : encode_region_frontier_payload_v1(
              RegionFrontierEventKindV2::internal_commit, write_slot);
    // StableOrder is scheduler-owned and deliberately differs from ProcessId.
    constexpr std::uint64_t source_stable_order = 101U;
    BatchTestTask sentinel { [&](Scheduler& scheduler,
                                 const std::span<const std::uint64_t> payloads) {
        const auto frontier = scheduler.current_batch_frontier();
        require_runtime(frontier.has_value() && payloads.size() == 1U
                && frontier->tasks.size() == 1U
                && frontier->tasks.front().sequence == source_sequence + 1U,
            "a declined helper leaves the next scheduler sequence unconsumed");
        sentinel_sequence = frontier->tasks.front().sequence;
        sentinel_ran = true;
        scheduler.request_stop();
        return SchedulerBatchResult { 1U, { } };
    } };

    BatchTestTask batch { [&](Scheduler& scheduler,
                              const std::span<const std::uint64_t> payloads) {
        try {
            require_runtime(!callback_ran && payloads.size() == 1U
                    && payloads.front() == source_payload,
                "the helper receives the original task payload exactly once");
            const auto current = scheduler.current_batch_frontier();
            require_runtime(current.has_value()
                    && current->phase == SchedulerPhase::active
                    && current->cursor == 0U && current->end == 1U
                    && current->tasks.size() == 1U
                    && current->tasks.front().payload == source_payload,
                "the helper sees one scheduler-authored Active task");
            callback_ran = true;
            source_sequence = current->tasks.front().sequence;

            runtime->frame.slot = {
                current->time,
                current->delta,
                current->systemverilog_round,
                static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog),
                static_cast<std::uint32_t>(SchedulerPhase::active),
            };
            runtime->frame.scheduler_frontier_generation = current->generation;
            runtime->frame.cut.scheduler_frontier_generation = current->generation;
            runtime->frame.cut.kind = RegionFrontierCutKindV2::closed_prefix;
            runtime->frame.scheduler_task_count = 1U;
            runtime->frame.scheduler_task_cursor = 0U;

            const auto& original = current->tasks.front();
            runtime->original_scheduler_tasks[0U] = original;
            auto translated_payload = source_payload;
            if (prefix_kind == PrefixKind::imported_wave_activation) {
                translated_payload = encode_region_frontier_payload_v1(
                    RegionFrontierEventKindV2::member_activation, root_member);
                auto& root = runtime->members[root_member];
                root.flags = RegionFrontierMemberFlagsV2::queued
                    | RegionFrontierMemberFlagsV2::queued_key_valid;
                root.queued_key = {
                    current->time, current->delta,
                    current->systemverilog_round, original.stable_order,
                    original.sequence,
                    static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog),
                    static_cast<std::uint32_t>(SchedulerPhase::active),
                };
                root.activation_origin = root.queued_key;
                root.pending_activation_origin = root.queued_key;
                runtime->ready_words[root_member / 64U]
                    |= UINT64_C(1) << (root_member % 64U);
            } else {
                auto& write = runtime->pending_writes[write_slot];
                write.commit_key = {
                    current->time, current->delta,
                    current->systemverilog_round, original.stable_order,
                    original.sequence,
                    static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog),
                    static_cast<std::uint32_t>(SchedulerPhase::active),
                };
                write.origin = write.commit_key;
                runtime->members[write.member_index].activation_origin
                    = write.origin;
                for (auto& member : runtime->members) {
                    member.flags
                        = RegionFrontierMemberFlagsV2::waiting_on_static;
                }
            }
            runtime->scheduler_tasks[0U] = {
                original.stable_order, original.sequence, translated_payload,
            };

            const SchedulerBatchGroupKey group_key {
                current->generation,
                static_cast<std::uint64_t>(tamper) + 1U,
            };
            auto reservation = scheduler.reserve_systemverilog_compact_group_batch(
                SchedulerPhase::active, *runtime, group_key,
                runtime->frame.staged_event_capacity);
            require_runtime(static_cast<bool>(reservation),
                "the scheduler reserves the generated event capacity before entry");
            target_round = reservation.target_systemverilog_round();
            require_runtime(target_round.has_value(),
                "the live scheduler reservation supplies its target Active round");

            const auto status = runtime->backend->executor->step_entry()(
                &runtime->frame);
            require_runtime(status == RegionFrontierStatusV2::need_scheduler_keys
                    && runtime->frame.staged_event_count == 1U,
                "the actual generated loop stages one write or successor event");
            const auto expected_event = runtime->staged_events[0U];
            const auto expected_origin = prefix_kind
                    == PrefixKind::imported_wave_activation
                ? runtime->members[root_member].activation_origin
                : runtime->pending_writes[write_slot].origin;
            require_runtime(same_key(expected_event.origin, expected_origin),
                "generated events preserve the source activation or commit origin");

            auto& original_sidecar = runtime->original_scheduler_tasks[0U];
            switch (tamper) {
            case TamperKind::none:
                break;
            case TamperKind::original_payload:
                original_sidecar.payload ^= UINT64_C(1);
                break;
            case TamperKind::original_sequence:
                ++original_sidecar.sequence;
                break;
            case TamperKind::translated_mapping:
                runtime->scheduler_tasks[0U].payload
                    = encode_region_frontier_payload_v1(
                        RegionFrontierEventKindV2::member_activation,
                        child_member);
                break;
            case TamperKind::ready_pointer:
                runtime->frame.ready_words = nullptr;
                break;
            case TamperKind::ready_count:
                --runtime->frame.readiness_word_count;
                break;
            }

            const auto frame_before = runtime->frame;
            const auto members_before = runtime->members;
            const auto ready_before = runtime->ready_words;
            const auto tasks_before = runtime->scheduler_tasks;
            const auto original_tasks_before = runtime->original_scheduler_tasks;
            const auto planes_before = runtime->planes;
            const auto fanout_before = runtime->fanout_edges;
            const auto port_planes_before = runtime->port_planes;
            const auto plane_backing_before = storage.plane_backing;
            const auto metadata_before = runtime->metadata;
            const auto writes_before = runtime->pending_writes;
            const auto pending_plane_words_before
                = runtime->pending_plane_words;
            const auto pending_plane_offsets_before
                = runtime->pending_plane_offsets;
            const auto events_before = runtime->staged_events;
            const auto committed_before = runtime->committed_signals;
            const auto compact_members_before = runtime->compact_members;
            const auto sequences_before = runtime->issued_sequences;

            if (request_stop_before_issue) {
                runtime->stop_requested = 1U;
                scheduler.request_stop();
            }
            helper_result = runtime->commit_staged_events(
                reservation, std::static_pointer_cast<void>(runtime), *current);
            require_runtime(helper_result == expect_success,
                expect_success
                    ? "staged work publishes through its reserved ticket after stop"
                    : "invalid authentication declines before scheduler publication");

            if (expect_success) {
                const auto expected_key = RegionFrontierKeyV2 {
                    current->time,
                    current->delta,
                    *target_round,
                    expected_event.stable_order,
                    runtime->issued_sequences[0U],
                    static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog),
                    static_cast<std::uint32_t>(SchedulerPhase::active),
                };
                require_runtime(runtime->frame.staged_event_count == 0U
                        && runtime->issued_sequences[0U] != 0U,
                    "successful compact issue retires events and returns a sequence");
                const auto compact_after
                    = scheduler.systemverilog_batch_compaction_stats();
                require_runtime(compact_after.tickets == compact_before.tickets + 1U
                        && compact_after.members == compact_before.members + 1U,
                    "the generated event appears as one real compact scheduler member");
                if (prefix_kind == PrefixKind::imported_wave_activation) {
                    const auto& write = runtime->pending_writes[write_slot];
                    require_runtime(expected_event.kind
                            == static_cast<std::uint32_t>(
                                RegionFrontierEventKindV2::internal_commit)
                            && write.commit_key.stable_order
                                == expected_key.stable_order
                            && write.commit_key.sequence == expected_key.sequence
                            && write.commit_key.systemverilog_round
                                == expected_key.systemverilog_round
                            && write.commit_key.time == expected_key.time
                            && write.commit_key.delta == expected_key.delta
                            && write.commit_key.process_domain
                                == expected_key.process_domain
                            && write.commit_key.phase == expected_key.phase
                            && (write.flags
                                & RegionFrontierPendingWriteFlagsV2::pending_key_assigned)
                                != 0U,
                        "an imported ProcessId maps locally and its write keeps the issued scheduler key");
                } else {
                    const auto& member = runtime->members[child_member];
                    const auto ready_bit = UINT64_C(1) << (child_member % 64U);
                    constexpr auto queued
                        = RegionFrontierMemberFlagsV2::queued
                        | RegionFrontierMemberFlagsV2::queued_key_valid;
                    require_runtime(expected_event.kind
                            == static_cast<std::uint32_t>(
                                RegionFrontierEventKindV2::member_activation)
                            && expected_event.descriptor_index == child_member
                            && (runtime->ready_words[child_member / 64U]
                                & ready_bit) != 0U
                            && (member.flags & queued) == queued
                            && same_key(member.queued_key, expected_key)
                            && same_key(member.pending_activation_origin,
                                expected_event.origin),
                        "a native encoded commit publishes the child ready bit and exact fresh key");
                }
                require_runtime(runtime->backend->executor->step_entry()(
                                    &runtime->frame)
                        == RegionFrontierStatusV2::stopped,
                    "the next native call observes the requested stop");
            } else {
                require_runtime(same_frame(runtime->frame, frame_before)
                        && runtime->members.size() == members_before.size()
                        && std::equal(runtime->members.begin(),
                            runtime->members.end(), members_before.begin(),
                            same_member)
                        && runtime->ready_words == ready_before
                        && runtime->scheduler_tasks.size() == tasks_before.size()
                        && std::equal(runtime->scheduler_tasks.begin(),
                            runtime->scheduler_tasks.end(), tasks_before.begin(),
                            same_native_task)
                        && runtime->original_scheduler_tasks.size()
                            == original_tasks_before.size()
                        && std::equal(runtime->original_scheduler_tasks.begin(),
                            runtime->original_scheduler_tasks.end(),
                            original_tasks_before.begin(), same_original_task)
                        && runtime->planes.size() == planes_before.size()
                        && std::equal(runtime->planes.begin(),
                            runtime->planes.end(), planes_before.begin(),
                            same_plane)
                        && runtime->fanout_edges.size() == fanout_before.size()
                        && std::equal(runtime->fanout_edges.begin(),
                            runtime->fanout_edges.end(), fanout_before.begin(),
                            same_fanout_edge)
                        && runtime->port_planes == port_planes_before
                        && storage.plane_backing == plane_backing_before
                        && runtime->metadata.size() == metadata_before.size()
                        && std::equal(runtime->metadata.begin(),
                            runtime->metadata.end(), metadata_before.begin(),
                            same_metadata)
                        && runtime->pending_writes.size() == writes_before.size()
                        && std::equal(runtime->pending_writes.begin(),
                            runtime->pending_writes.end(), writes_before.begin(),
                            same_write)
                        && runtime->pending_plane_words
                            == pending_plane_words_before
                        && runtime->pending_plane_offsets
                            == pending_plane_offsets_before
                        && runtime->staged_events.size() == events_before.size()
                        && std::equal(runtime->staged_events.begin(),
                            runtime->staged_events.end(), events_before.begin(),
                            same_event)
                        && runtime->committed_signals.size()
                            == committed_before.size()
                        && std::equal(runtime->committed_signals.begin(),
                            runtime->committed_signals.end(),
                            committed_before.begin(), same_committed_signal)
                        && runtime->compact_members.size()
                            == compact_members_before.size()
                        && std::equal(runtime->compact_members.begin(),
                            runtime->compact_members.end(),
                            compact_members_before.begin(), same_compact_member)
                        && runtime->issued_sequences == sequences_before,
                    "decline preserves generated frame, member, key, and staged-event state");
                require_runtime(same_compaction_stats(
                        scheduler.systemverilog_batch_compaction_stats(),
                        compact_before),
                    "decline cancels its reservation without publishing a queue ticket");
                scheduler.schedule_systemverilog_batchable(
                    SchedulerPhase::active, UINT64_C(0x7fffffff), sentinel,
                    UINT64_C(0x1234), [](Scheduler&) { });
            }
        } catch (const std::exception& error) {
            callback_error = error.what();
            scheduler.request_stop();
        }
        return SchedulerBatchResult { 1U, { } };
    } };

    owner.scheduler.schedule_systemverilog_batchable(
        SchedulerPhase::active, source_stable_order, batch, source_payload,
        [](Scheduler&) { });
    const auto result = owner.scheduler.run();
    require_runtime(result.status == RunStatus::stopped,
        "the helper scenario ends at its explicit stop boundary");
    require_runtime(callback_ran && callback_error.empty(),
        callback_error.empty() ? "the initial scheduler callback ran"
                               : callback_error);
    require_runtime(helper_result == expect_success,
        "the helper's final result matches the expected preflight outcome");
    if (expect_success) {
        require_runtime(request_stop_before_issue && !sentinel_ran,
            "successful post-stop publication leaves following work queued");
    } else {
        require_runtime(sentinel_ran && sentinel_sequence == source_sequence + 1U,
            "failed authentication leaves no sequence gap");
    }
    owner.scheduler.discard_pending();
    owner.scheduler.clear_stop();
}

} // namespace fsim::runtime::simir

namespace fsim::compiler::test {

void run_region_frontier_issue_helper_tests()
{
    using namespace fsim::runtime::simir;
    auto kernel = make_certified_frontier_kernel(1U);
    LlvmJitOptions options;
    options.optimization = JitOptimizationLevel::o0;
    options.debug_instrumentation = false;
    auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, std::move(options), "frontier-issue-helper-test");
    require(executor != nullptr,
        "the helper regression compiles an actual generated native entry");
    class Backend final : public RegionFrontierBackend {
    public:
        explicit Backend(std::unique_ptr<LlvmRegionFrontierExecutor> executor)
            : executor_ { std::move(executor) }
        {
        }

        [[nodiscard]] RegionFrontierStepEntryV2
        step_entry() const noexcept override
        {
            return executor_->step_entry();
        }

        [[nodiscard]] const RegionFrontierLayoutV2&
        layout() const noexcept override
        {
            return executor_->layout();
        }

    private:
        std::unique_ptr<LlvmRegionFrontierExecutor> executor_;
    };
    NativeRegionAllocationTestAccess::run(
        kernel, std::make_unique<Backend>(std::move(executor)));
}

} // namespace fsim::compiler::test

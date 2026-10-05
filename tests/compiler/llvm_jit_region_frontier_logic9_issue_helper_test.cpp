// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"
#include "llvm_jit_region_frontier_logic9_issue_helper_test.hpp"
#include "simir_internal.hpp"
#include "../runtime/runtime_owned_driver_demotion_test_access.hpp"

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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using namespace fsim::runtime::simir;
namespace fruntime = fsim::runtime;
using FrontierImplementation = OwnedDriverDemotionTestAccess::Implementation;

constexpr std::uint32_t fixture_width = 65U;
constexpr std::size_t fixture_word_count = 2U;
constexpr std::size_t scheduler_task_capacity = 64U;
constexpr ProcessId fixture_producer_id = 7U;
constexpr ProcessId fixture_consumer_id = 23U;
constexpr SignalId fixture_input_signal = 0U;
constexpr SignalId fixture_internal_signal = 1U;
constexpr SignalId fixture_boundary_signal = 2U;
constexpr std::uint64_t source_stable_order = 101U;
constexpr std::uint64_t sentinel_stable_order = UINT64_C(0x7fffffff);
constexpr std::uint64_t sentinel_payload = UINT64_C(0x12345678);

enum class PendingTamper : std::uint8_t {
    none,
    null_fourth_plane,
    reserved_ordinal,
    nonzero_tail,
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] std::uint64_t encode_payload(
    const RegionFrontierEventKindV2 kind, const std::uint64_t index) noexcept
{
    return (static_cast<std::uint64_t>(kind)
            << kRegionFrontierPayloadKindShiftV2)
        | index;
}

[[nodiscard]] bool same_key(const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_write(const RegionFrontierPendingWriteV2& left,
    const RegionFrontierPendingWriteV2& right) noexcept
{
    return left.member_index == right.member_index
        && left.signal_slot == right.signal_slot
        && left.source_instruction == right.source_instruction
        && left.update_kind == right.update_kind
        && left.flags == right.flags && left.reserved == right.reserved
        && same_key(left.commit_key, right.commit_key)
        && same_key(left.origin, right.origin)
        && left.value_kind == right.value_kind && left.width == right.width
        && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && std::equal(std::begin(left.value_planes),
            std::end(left.value_planes), std::begin(right.value_planes));
}

[[nodiscard]] bool same_event(const RegionFrontierStagedEventV2& left,
    const RegionFrontierStagedEventV2& right) noexcept
{
    return left.kind == right.kind
        && left.descriptor_index == right.descriptor_index
        && left.stable_order == right.stable_order
        && same_key(left.origin, right.origin);
}

[[nodiscard]] RegionConeActivationKernel make_logic9_kernel()
{
    std::vector<RegionSignalDescriptor> signals(3U,
        RegionSignalDescriptor { fixture_width });
    for (auto& signal : signals) {
        signal.value_kind = ValueKind::logic9;
    }
    signals[fixture_boundary_signal].observations
        = RegionObservation::current;

    Process producer;
    producer.id = 0U;
    producer.name = "frontier_logic9_issue_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 2U;
    producer.register_value_kinds = {
        ValueKind::logic9, ValueKind::logic9,
    };
    producer.static_sensitivity = {
        { fixture_input_signal, EdgeKind::any },
    };
    producer.driver_regions = {
        { fixture_internal_signal, 0U, fixture_width, true },
    };
    producer.operations = {
        ReadSignal { 0U, fixture_input_signal },
        CopyRegister { 1U, 0U },
        WriteUpdate { fixture_internal_signal, 1U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    Process consumer;
    consumer.id = 1U;
    consumer.name = "frontier_logic9_issue_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    consumer.register_count = 1U;
    consumer.register_value_kinds = { ValueKind::logic9 };
    consumer.static_sensitivity = {
        { fixture_internal_signal, EdgeKind::any },
    };
    consumer.driver_regions = {
        { fixture_boundary_signal, 0U, fixture_width, true },
    };
    consumer.operations = {
        ReadSignal { 0U, fixture_internal_signal },
        WriteUpdate { fixture_boundary_signal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    const std::array<const Process*, 2U> processes { &producer, &consumer };
    const auto graph = RegionGraph::build(processes, signals);
    const auto& certificates = graph.certificate_inventory().components;
    require(certificates.size() == 1U
            && certificates.front().members
                == std::vector<ProcessId> { ProcessId { 0U }, ProcessId { 1U } }
            && certificates.front().structural_internal_signal_candidates
                == std::vector<SignalId> { fixture_internal_signal },
        "the two-member Logic9 graph certifies its one internal signal");

    auto program = graph.build_compute_program(0U, processes);
    require(program.has_value(),
        "the certified Logic9 graph builds a native activation kernel");
    auto kernel = std::move(program->activation_kernel);
    for (auto& member : kernel.members) {
        if (member.process == 0U) {
            member.process = fixture_producer_id;
        } else if (member.process == 1U) {
            member.process = fixture_consumer_id;
        }
    }
    for (auto& output : kernel.outputs) {
        if (output.owner == 0U) {
            output.owner = fixture_producer_id;
        } else if (output.owner == 1U) {
            output.owner = fixture_consumer_id;
        }
    }
    require(kernel.members.size() == 2U
            && kernel.members[0U].process == fixture_producer_id
            && kernel.members[1U].process == fixture_consumer_id
            && kernel.internal_signals
                == std::vector<SignalId> { fixture_internal_signal },
        "the activation kernel retains its certified member and signal map");
    return kernel;
}

struct PlaneBacking final {
    std::array<std::vector<std::uint64_t>, 4U> boundary;
    std::array<std::vector<std::uint64_t>, 4U> current;
    std::array<std::vector<std::uint64_t>, 4U> previous;
    std::array<std::vector<std::uint64_t>, 4U> stored;
    std::array<std::vector<std::uint64_t>, 4U> owner;
};

struct FrameStorage final {
    FrameStorage(const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout)
        : planes(layout.signal_slot_count)
        , backing(layout.signal_slot_count)
        , metadata(layout.metadata_count)
        , fanout_edges(layout.fanout_edges,
              layout.fanout_edges + layout.fanout_edge_count)
        , port_planes(layout.signal_slot_count)
        , pending_writes(layout.pending_write_capacity)
        , staged_events(layout.staged_event_capacity)
        , committed_signals(layout.committed_signal_capacity)
    {
        require(region_frontier_layout_header_valid_v2(layout)
                && layout.abi_version == kRegionFrontierAbiVersionV2
                && layout.struct_size == sizeof(RegionFrontierLayoutV2)
                && layout.member_count == 2U
                && layout.member_count == kernel.members.size()
                && layout.signal_slot_count == 3U
                && layout.write_site_count == 2U
                && layout.signals != nullptr && layout.members != nullptr
                && layout.write_sites != nullptr
                && layout.max_member_staged_event_counts != nullptr
                && layout.fanout_edge_count == 1U
                && layout.fanout_edges != nullptr,
            "the generated Logic9 fixture has the certified two-member shape");

        ready_words.assign(layout.readiness_word_count, 0U);
        members.resize(layout.member_count);
        for (std::size_t index = 0U; index < members.size(); ++index) {
            members[index].process_id = layout.members[index].process_id;
            members[index].flags = RegionFrontierMemberFlagsV2::waiting_on_static;
            members[index].static_trigger_mask = UINT64_MAX;
        }

        for (std::size_t index = 0U; index < planes.size(); ++index) {
            const auto& descriptor = layout.signals[index];
            auto& plane = planes[index];
            auto& plane_backing = backing[index];
            require(descriptor.value_kind == RegionFrontierValueKindV2::logic9
                    && descriptor.width == fixture_width
                    && descriptor.word_count == fixture_word_count
                    && descriptor.plane_count == kRegionFrontierLogic9PlaneCountV2,
                "all frame signals use four planes and a two-word 65-bit extent");
            plane.signal_id = descriptor.signal_id;
            plane.owner_process_id = descriptor.owner_process_id;
            plane.value_kind = descriptor.value_kind;
            plane.width = descriptor.width;
            plane.word_count = descriptor.word_count;
            plane.plane_count = descriptor.plane_count;
            plane.flags = descriptor.flags;
            plane.metadata_index = descriptor.metadata_index;
            if ((descriptor.flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U) {
                for (std::size_t plane_index = 0U;
                     plane_index < descriptor.plane_count; ++plane_index) {
                    plane_backing.current[plane_index].assign(
                        fixture_word_count, 0U);
                    plane_backing.previous[plane_index].assign(
                        fixture_word_count, 0U);
                    plane_backing.stored[plane_index].assign(
                        fixture_word_count, 0U);
                    plane_backing.owner[plane_index].assign(
                        fixture_word_count, 0U);
                    plane.current_planes[plane_index]
                        = plane_backing.current[plane_index].data();
                    plane.previous_planes[plane_index]
                        = plane_backing.previous[plane_index].data();
                    plane.stored_planes[plane_index]
                        = plane_backing.stored[plane_index].data();
                    plane.owner_planes[plane_index]
                        = plane_backing.owner[plane_index].data();
                }
            } else {
                require((descriptor.flags
                            & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                        != 0U,
                    "the other frame signals are read-only boundaries");
                for (std::size_t plane_index = 0U;
                     plane_index < descriptor.plane_count; ++plane_index) {
                    plane_backing.boundary[plane_index].assign(
                        fixture_word_count, 0U);
                    plane.boundary_planes[plane_index]
                        = plane_backing.boundary[plane_index].data();
                }
            }
            require(region_frontier_plane_bindings_valid_v2(plane),
                "each Logic9 frame plane has valid role bindings");
            port_planes[index] = &plane;
        }

        constexpr std::array<char, 9U> states {
            'U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-',
        };
        std::string input_text;
        input_text.reserve(fixture_width);
        for (std::uint32_t bit = 0U; bit < fixture_width; ++bit) {
            input_text.push_back(states[bit % states.size()]);
        }
        const auto input_value = runtime::PackedLogic4::from_logic9_msb_string(
            input_text);
        const auto input_slot = plane_slot(layout, fixture_input_signal);
        for (std::size_t plane_index = 0U; plane_index < 4U; ++plane_index) {
            const auto source_words = input_value.logic9_plane_words(plane_index);
            require(source_words.size() == fixture_word_count,
                "the test stimulus fills both words of each Logic9 plane");
            std::copy(source_words.begin(), source_words.end(),
                backing[input_slot].boundary[plane_index].begin());
        }
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

        frame.abi_version = kRegionFrontierAbiVersionV2;
        frame.struct_size = sizeof(RegionFrontierFrameV2);
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
            ProcessSchedulingDomain::systemverilog);
        frame.slot.phase
            = static_cast<std::uint32_t>(fruntime::SchedulerPhase::active);
        frame.cut.kind = RegionFrontierCutKindV2::closed_prefix;
    }

    [[nodiscard]] static std::size_t plane_slot(
        const RegionFrontierLayoutV2& layout, const SignalId signal)
    {
        for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
            if (layout.signals[slot].signal_id == signal) {
                return slot;
            }
        }
        throw std::runtime_error { "the certified layout contains the signal" };
    }

    RegionFrontierFrameV2 frame;
    std::vector<std::uint64_t> ready_words;
    std::vector<RegionFrontierMemberV2> members;
    std::vector<RegionFrontierSchedulerTaskV2> scheduler_tasks
        = std::vector<RegionFrontierSchedulerTaskV2>(scheduler_task_capacity);
    std::vector<RegionFrontierPlaneV2> planes;
    std::vector<PlaneBacking> backing;
    std::vector<RegionFrontierSignalMetadataV2> metadata;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_edges;
    std::vector<const RegionFrontierPlaneV2*> port_planes;
    std::vector<RegionFrontierPendingWriteV2> pending_writes;
    std::vector<RegionFrontierStagedEventV2> staged_events;
    std::vector<RegionFrontierCommittedSignalV2> committed_signals;
};

struct BatchTask final : runtime::SchedulerBatchTask {
    using Callback = std::function<runtime::SchedulerBatchResult(
        runtime::Scheduler&, std::span<const std::uint64_t>)>;

    explicit BatchTask(Callback callback)
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

[[nodiscard]] std::size_t member_index_for_process(
    const RegionFrontierLayoutV2& layout, const ProcessId process)
{
    for (std::size_t index = 0U; index < layout.member_count; ++index) {
        if (layout.members[index].process_id == process) {
            return index;
        }
    }
    throw std::runtime_error { "the frontier layout contains the process" };
}

[[nodiscard]] std::size_t write_slot_for_internal_signal(
    const RegionFrontierLayoutV2& layout, const SignalId signal)
{
    const auto expected_kind = static_cast<std::uint32_t>(
        RegionFrontierEventKindV2::internal_commit);
    for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
        const auto& site = layout.write_sites[index];
        if (site.event_kind == expected_kind
            && site.signal_slot < layout.signal_slot_count
            && layout.signals[site.signal_slot].signal_id == signal) {
            return site.pending_slot;
        }
    }
    throw std::runtime_error { "the producer has one internal commit site" };
}

class Backend final : public RegionFrontierBackend {
public:
    explicit Backend(std::unique_ptr<LlvmRegionFrontierExecutor> executor)
        : executor_ { std::move(executor) }
    {
    }

    [[nodiscard]] RegionFrontierStepEntryV2 step_entry() const noexcept override
    {
        return executor_->step_entry();
    }

    [[nodiscard]] const RegionFrontierLayoutV2& layout() const noexcept override
    {
        return executor_->layout();
    }

private:
    std::unique_ptr<LlvmRegionFrontierExecutor> executor_;
};

void run_case(const RegionConeActivationKernel& kernel,
    const std::shared_ptr<FrontierImplementation::RegionFrontierBackendEntry>&
        backend_entry,
    const PendingTamper tamper)
{
    require(backend_entry != nullptr && backend_entry->executor != nullptr
            && backend_entry->executor->step_entry() != nullptr,
        "the Logic9 witness owns an actual generated step entry");
    FrontierImplementation owner {
        fruntime::SchedulerOptions { }, UINT64_C(0x5a17),
    };
    const auto& layout = backend_entry->executor->layout();
    auto storage = std::make_unique<FrameStorage>(kernel, layout);

    auto component_runtime
        = std::make_shared<FrontierImplementation::RegionFrontierComponentRuntime>();
    component_runtime->owner = &owner;
    component_runtime->component = 0U;
    component_runtime->runtime_generation = storage->frame.runtime_generation;
    component_runtime->backend = backend_entry;
    component_runtime->frame = storage->frame;
    component_runtime->ready_words.assign(
        storage->ready_words.begin(), storage->ready_words.end());
    component_runtime->members.assign(
        storage->members.begin(), storage->members.end());
    component_runtime->scheduler_tasks.assign(scheduler_task_capacity, { });
    component_runtime->original_scheduler_tasks.assign(
        scheduler_task_capacity, { });
    component_runtime->planes.assign(
        storage->planes.begin(), storage->planes.end());
    component_runtime->metadata.assign(
        storage->metadata.begin(), storage->metadata.end());
    component_runtime->fanout_edges.assign(
        storage->fanout_edges.begin(), storage->fanout_edges.end());
    component_runtime->port_planes.resize(storage->planes.size(), nullptr);
    component_runtime->pending_writes.assign(
        storage->pending_writes.begin(), storage->pending_writes.end());
    component_runtime->staged_events.assign(
        storage->staged_events.begin(), storage->staged_events.end());
    component_runtime->committed_signals.assign(
        storage->committed_signals.begin(), storage->committed_signals.end());
    component_runtime->compact_members.resize(storage->staged_events.size());
    component_runtime->issued_sequences.resize(storage->staged_events.size());
    component_runtime->frame_initialized = true;

    component_runtime->pending_plane_offsets.assign(layout.pending_write_capacity,
        std::numeric_limits<std::size_t>::max());
    std::size_t total_pending_words { };
    for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
        const auto& site = layout.write_sites[index];
        const auto extent = static_cast<std::size_t>(site.word_count)
            * site.plane_count;
        require(site.pending_slot < component_runtime->pending_plane_offsets.size()
                && extent <= std::numeric_limits<std::size_t>::max()
                    - total_pending_words,
            "the four-plane pending extents fit their backing vector");
        component_runtime->pending_plane_offsets[site.pending_slot]
            = total_pending_words;
        total_pending_words += extent;
    }
    component_runtime->pending_plane_words.assign(total_pending_words, 0U);
    for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
        const auto& site = layout.write_sites[index];
        auto& write = component_runtime->pending_writes[site.pending_slot];
        const auto base = component_runtime->pending_plane_offsets[site.pending_slot];
        for (std::size_t plane = 0U; plane < site.plane_count; ++plane) {
            write.value_planes[plane]
                = component_runtime->pending_plane_words.data() + base
                    + plane * site.word_count;
        }
        require(region_frontier_pending_write_bindings_valid_v2(write),
            "the generated Logic9 write owns four exact plane spans");
    }
    for (std::size_t index = 0U;
         index < component_runtime->planes.size(); ++index) {
        component_runtime->port_planes[index]
            = &component_runtime->planes[index];
    }
    component_runtime->frame.ready_words = component_runtime->ready_words.data();
    component_runtime->frame.members = component_runtime->members.data();
    component_runtime->frame.scheduler_tasks
        = component_runtime->scheduler_tasks.data();
    component_runtime->frame.planes = component_runtime->planes.data();
    component_runtime->frame.metadata = component_runtime->metadata.data();
    component_runtime->frame.fanout_edges
        = component_runtime->fanout_edges.data();
    component_runtime->frame.port_planes = component_runtime->port_planes.data();
    component_runtime->frame.pending_writes
        = component_runtime->pending_writes.data();
    component_runtime->frame.staged_events
        = component_runtime->staged_events.data();
    component_runtime->frame.committed_signals
        = component_runtime->committed_signals.data();
    component_runtime->frame.native_frontier_member_dispatches
        = &component_runtime->native_member_dispatches;
    component_runtime->frame.stop_requested = &component_runtime->stop_requested;

    const auto producer_member
        = member_index_for_process(layout, fixture_producer_id);
    const auto input_slot
        = FrameStorage::plane_slot(layout, fixture_input_signal);
    const auto write_slot
        = write_slot_for_internal_signal(layout, fixture_internal_signal);
    auto& pending = component_runtime->pending_writes[write_slot];

    bool callback_ran { };
    bool helper_result { };
    bool sentinel_ran { };
    std::uint64_t source_sequence { };
    std::uint64_t sentinel_sequence { };
    std::string callback_error;
    const auto compaction_before
        = owner.scheduler.systemverilog_batch_compaction_stats();
    const auto source_payload = owner.systemverilog_wave_payload
        | static_cast<std::uint64_t>(fixture_producer_id);

    BatchTask sentinel { [&](runtime::Scheduler& scheduler,
                             const std::span<const std::uint64_t> payloads) {
        const auto current = scheduler.current_batch_frontier();
        require(current.has_value() && payloads.size() == 1U
                && payloads.front() == sentinel_payload
                && current->tasks.size() == 1U
                && current->tasks.front().sequence == source_sequence + 1U,
            "declined Logic9 issue leaves the next scheduler sequence intact");
        sentinel_sequence = current->tasks.front().sequence;
        sentinel_ran = true;
        scheduler.request_stop();
        return fruntime::SchedulerBatchResult { 1U, { } };
    } };

    BatchTask source { [&](runtime::Scheduler& scheduler,
                           const std::span<const std::uint64_t> payloads) {
        try {
            require(!callback_ran && payloads.size() == 1U
                    && payloads.front() == source_payload,
                "the scheduler provides the original producer activation");
            const auto frontier = scheduler.current_batch_frontier();
            require(frontier.has_value()
                    && frontier->phase == fruntime::SchedulerPhase::active
                    && frontier->cursor == 0U && frontier->end == 1U
                    && frontier->tasks.size() == 1U
                    && frontier->tasks.front().payload == source_payload,
                "the generated entry receives one scheduler-authored Active task");
            callback_ran = true;
            source_sequence = frontier->tasks.front().sequence;

        component_runtime->frame.slot = {
                frontier->time,
                frontier->delta,
                frontier->systemverilog_round,
                static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog),
                static_cast<std::uint32_t>(fruntime::SchedulerPhase::active),
            };
            component_runtime->frame.scheduler_frontier_generation
                = frontier->generation;
            component_runtime->frame.cut.scheduler_frontier_generation
                = frontier->generation;
            component_runtime->frame.scheduler_task_count = 1U;
            component_runtime->frame.scheduler_task_cursor = 0U;
            const auto& original = frontier->tasks.front();
            component_runtime->original_scheduler_tasks[0U] = original;
            auto& member = component_runtime->members[producer_member];
            member.flags = RegionFrontierMemberFlagsV2::queued
                | RegionFrontierMemberFlagsV2::queued_key_valid;
            member.queued_key = {
                frontier->time,
                frontier->delta,
                frontier->systemverilog_round,
                original.stable_order,
                original.sequence,
                static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog),
                static_cast<std::uint32_t>(fruntime::SchedulerPhase::active),
            };
            member.activation_origin = member.queued_key;
            member.pending_activation_origin = member.queued_key;
            component_runtime->ready_words[producer_member / 64U]
                |= UINT64_C(1) << (producer_member % 64U);
            component_runtime->scheduler_tasks[0U] = {
                original.stable_order,
                original.sequence,
                encode_payload(RegionFrontierEventKindV2::member_activation,
                    producer_member),
            };

            const fruntime::SchedulerBatchGroupKey group_key {
                frontier->generation,
                static_cast<std::uint64_t>(tamper) + 1U,
            };
            auto reservation = scheduler.reserve_systemverilog_compact_group_batch(
                fruntime::SchedulerPhase::active, *component_runtime, group_key,
                component_runtime->frame.staged_event_capacity);
            const auto target_round = reservation.target_systemverilog_round();
            require(static_cast<bool>(reservation)
                    && target_round.has_value(),
                "the scheduler reserves the real staged-event capacity");

            const auto status = backend_entry->executor->step_entry()(
                &component_runtime->frame);
            require(status == RegionFrontierStatusV2::need_scheduler_keys
                    && component_runtime->frame.staged_event_count == 1U
                    && component_runtime->frame.pending_write_count == 1U
                    && pending.value_kind == RegionFrontierValueKindV2::logic9
                    && pending.width == fixture_width
                    && pending.word_count == fixture_word_count
                    && pending.plane_count == 4U,
                "the actual Logic9 entry stages one four-plane 65-bit write");
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                require((pending.value_planes[plane][0U]
                            | pending.value_planes[plane][1U]) != 0U
                        && (pending.value_planes[plane][1U] & ~UINT64_C(1)) == 0U,
                    "generated staging fills every Logic9 plane with a canonical 65-bit tail");
                for (std::size_t word = 0U;
                     word < fixture_word_count; ++word) {
                    require(pending.value_planes[plane][word]
                            == storage->backing[input_slot]
                                .boundary[plane][word],
                        "generated Logic9 staging copies both words of every plane");
                }
            }
            const auto event_before = component_runtime->staged_events[0U];
            require(event_before.kind == static_cast<std::uint32_t>(
                        RegionFrontierEventKindV2::internal_commit)
                    && event_before.descriptor_index == write_slot,
                "the generated event is the producer's authenticated internal commit");

            if (tamper == PendingTamper::null_fourth_plane) {
                pending.value_planes[3U] = nullptr;
            } else if (tamper == PendingTamper::reserved_ordinal) {
                require((pending.value_planes[0U][0U] & UINT64_C(1)) != 0U
                        && (pending.value_planes[3U][0U] & UINT64_C(1)) == 0U,
                    "the least significant stimulus bit is canonical Logic9 ordinal one");
                pending.value_planes[3U][0U] |= UINT64_C(1);
            } else if (tamper == PendingTamper::nonzero_tail) {
                pending.value_planes[3U][fixture_word_count - 1U]
                    |= ~UINT64_C(1);
            }
            const auto pending_before = pending;
            const auto frame_pending_count_before
                = component_runtime->frame.pending_write_count;
            const auto frame_event_count_before
                = component_runtime->frame.staged_event_count;
            const auto frame_cursor_before
                = component_runtime->frame.scheduler_task_cursor;
            const auto pending_words_before
                = component_runtime->pending_plane_words;
            const auto events_before = component_runtime->staged_events;
            const auto sequences_before = component_runtime->issued_sequences;

            if (tamper == PendingTamper::none) {
                component_runtime->stop_requested = 1U;
                scheduler.request_stop();
            }
            helper_result = component_runtime->commit_staged_events(
                reservation, std::static_pointer_cast<void>(component_runtime),
                *frontier);
            require(helper_result == (tamper == PendingTamper::none),
                "the real issue helper accepts canonical Logic9 or declines tampering");

            if (tamper == PendingTamper::none) {
                const auto stats_after
                    = scheduler.systemverilog_batch_compaction_stats();
                require(component_runtime->frame.staged_event_count == 0U
                        && (pending.flags
                            & RegionFrontierPendingWriteFlagsV2::pending_key_assigned)
                            != 0U
                        && stats_after.tickets == compaction_before.tickets + 1U
                        && stats_after.members == compaction_before.members + 1U
                        && component_runtime->compact_members[0U].stable_order
                            == event_before.stable_order
                        && component_runtime->compact_members[0U].payload
                            == encode_payload(
                                RegionFrontierEventKindV2::internal_commit,
                                write_slot),
                    "canonical four-plane staging publishes through the reserved helper");
                const RegionFrontierKeyV2 expected_key {
                    frontier->time,
                    frontier->delta,
                    *target_round,
                    event_before.stable_order,
                    component_runtime->issued_sequences[0U],
                    static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog),
                    static_cast<std::uint32_t>(fruntime::SchedulerPhase::active),
                };
                require(same_key(pending.commit_key, expected_key),
                    "the committed Logic9 write keeps its exact scheduler-authored key");
            } else {
                const auto stats_after
                    = scheduler.systemverilog_batch_compaction_stats();
                require(stats_after.tickets == compaction_before.tickets
                        && stats_after.members == compaction_before.members
                        && same_write(pending, pending_before)
                        && component_runtime->frame.pending_write_count
                            == frame_pending_count_before
                        && component_runtime->frame.staged_event_count
                            == frame_event_count_before
                        && component_runtime->frame.scheduler_task_cursor
                            == frame_cursor_before
                        && component_runtime->pending_plane_words
                            == pending_words_before
                        && component_runtime->issued_sequences == sequences_before
                        && component_runtime->staged_events.size()
                            == events_before.size()
                        && std::equal(component_runtime->staged_events.begin(),
                            component_runtime->staged_events.end(), events_before.begin(),
                            same_event)
                        && same_event(component_runtime->staged_events[0U], event_before)
                        && (pending.flags
                            & RegionFrontierPendingWriteFlagsV2::pending_key_assigned)
                            == 0U
                        && pending.commit_key.sequence == 0U,
                    "malformed plane storage is rejected without queue or key mutation");
                scheduler.schedule_systemverilog_batchable(
                    fruntime::SchedulerPhase::active, sentinel_stable_order,
                    sentinel, sentinel_payload, [](fruntime::Scheduler&) { });
            }
        } catch (const std::exception& error) {
            callback_error = error.what();
            scheduler.request_stop();
        }
        return fruntime::SchedulerBatchResult { 1U, { } };
    } };

    owner.scheduler.schedule_systemverilog_batchable(
        fruntime::SchedulerPhase::active, source_stable_order,
        source, source_payload, [](fruntime::Scheduler&) { });
    const auto result = owner.scheduler.run();
    require(result.status == fruntime::RunStatus::stopped
            && callback_ran && callback_error.empty(),
        callback_error.empty()
            ? "the actual Logic9 scheduler witness stops at its checked boundary"
            : callback_error);
    if (tamper == PendingTamper::none) {
        require(!sentinel_ran && helper_result,
            "successful issue holds its compact ticket across stop");
    } else {
        require(sentinel_ran && sentinel_sequence == source_sequence + 1U,
            "invalid Logic9 data leaves the scheduler sequence available");
    }
    owner.scheduler.discard_pending();
    owner.scheduler.clear_stop();
}

void run_optimization(const JitOptimizationLevel optimization)
{
    const auto kernel = make_logic9_kernel();
    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.cache_directory.clear();
    auto executor = LlvmRegionFrontierExecutor::try_create(kernel,
        std::move(options), "frontier-logic9-issue-helper-test");
    require(executor != nullptr,
        "O0 and O2 compile the certified 65-bit Logic9 frontier");
    auto backend_entry
        = std::make_shared<FrontierImplementation::RegionFrontierBackendEntry>();
    backend_entry->kernel = kernel;
    backend_entry->provider_identity = "frontier-logic9-issue-helper-test";
    backend_entry->executor = std::make_unique<Backend>(std::move(executor));
    for (const auto tamper : {
             PendingTamper::none,
             PendingTamper::null_fourth_plane,
             PendingTamper::reserved_ordinal,
             PendingTamper::nonzero_tail,
         }) {
        run_case(kernel, backend_entry, tamper);
    }
}

} // namespace

void run_region_frontier_logic9_issue_helper_tests()
{
    run_optimization(JitOptimizationLevel::o0);
    run_optimization(JitOptimizationLevel::o2);
}

} // namespace fsim::compiler::test

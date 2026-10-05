// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using namespace runtime;
using namespace runtime::simir;

constexpr std::uint32_t leaf_count = 33U;
constexpr std::uint32_t frame_event_budget = 64U;
constexpr SignalId source_signal = 0U;
constexpr SignalId internal_signal = 1U;
constexpr ProcessId root_process = 0U;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] bool same_key(const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] RegionFrontierKeyV2 make_key(
    const RegionFrontierSlotV2& slot,
    const std::uint64_t stable_order,
    const std::uint64_t sequence) noexcept
{
    return { slot.time, slot.delta, slot.systemverilog_round, stable_order,
        sequence, slot.process_domain, slot.phase };
}

[[nodiscard]] bool all_zero(
    const std::span<const std::uint64_t> words) noexcept
{
    return std::ranges::all_of(words,
        [](const std::uint64_t word) { return word == 0U; });
}

[[nodiscard]] RegionConeActivationKernel make_capacity_kernel()
{
    const auto output_count = static_cast<std::size_t>(leaf_count) + 1U;
    const auto signal_count = output_count + 2U;
    std::vector<RegionSignalDescriptor> signals(signal_count,
        RegionSignalDescriptor { 1U });
    for (std::size_t index = 0U; index < leaf_count; ++index) {
        signals[index + 2U].observations = RegionObservation::current;
    }

    std::vector<Process> processes(output_count);
    auto& root = processes.front();
    root.id = root_process;
    root.name = "frontier_capacity_root";
    root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    root.register_count = 1U;
    root.register_value_kinds = { ValueKind::logic4 };
    root.static_sensitivity = { { source_signal, EdgeKind::any } };
    root.driver_regions = { { internal_signal, 0U, 0U, true } };
    root.operations = {
        ReadSignal { 0U, source_signal },
        WriteUpdate { internal_signal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    for (std::uint32_t index = 0U; index < leaf_count; ++index) {
        auto& leaf = processes[static_cast<std::size_t>(index) + 1U];
        leaf.id = index + 1U;
        leaf.name = "frontier_capacity_leaf_" + std::to_string(index);
        leaf.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        leaf.register_count = 1U;
        leaf.register_value_kinds = { ValueKind::logic4 };
        leaf.static_sensitivity = { { internal_signal, EdgeKind::any } };
        const auto output = static_cast<SignalId>(index + 2U);
        leaf.driver_regions = { { output, 0U, 0U, true } };
        leaf.operations = {
            ReadSignal { 0U, internal_signal },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { },
            Jump { 0U },
        };
    }

    std::vector<const Process*> programs;
    programs.reserve(processes.size());
    for (const auto& process : processes) {
        programs.push_back(&process);
    }
    const auto graph = RegionGraph::build(programs, signals);
    const auto& components = graph.certificate_inventory().components;
    require(components.size() == 1U
            && components.front().members.size() == processes.size()
            && components.front().structural_internal_signal_candidates
                == std::vector<SignalId> { internal_signal },
        "the capacity fixture is one certified root with 33 static readers");
    auto program = graph.build_compute_program(0U, programs);
    require(program.has_value(),
        "the graph certifies the complete acyclic capacity fixture");
    auto kernel = std::move(program->activation_kernel);
    require(kernel.members.size() == processes.size()
            && kernel.outputs.size() == output_count
            && kernel.internal_signals
                == std::vector<SignalId> { internal_signal },
        "the activation kernel retains every whole write site");
    return kernel;
}

struct SignalBacking {
    std::vector<std::uint64_t> boundary_aval;
    std::vector<std::uint64_t> boundary_bval;
    std::vector<std::uint64_t> current_aval;
    std::vector<std::uint64_t> current_bval;
    std::vector<std::uint64_t> previous_aval;
    std::vector<std::uint64_t> previous_bval;
    std::vector<std::uint64_t> stored_aval;
    std::vector<std::uint64_t> stored_bval;
    std::vector<std::uint64_t> owner_aval;
    std::vector<std::uint64_t> owner_bval;
};

struct WriteBacking {
    std::vector<std::uint64_t> aval;
    std::vector<std::uint64_t> bval;
};

class CapacityFrame final {
public:
    CapacityFrame(const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout,
        const std::uint32_t event_budget)
        : kernel_ { kernel }
        , layout_ { layout }
        , ready_words_ (layout.readiness_word_count, 0U)
        , members_ (layout.member_count)
        , tasks_ (128U)
        , planes_ (layout.signal_slot_count)
        , fanout_edges_ (layout.fanout_edge_count)
        , signal_backing_ (layout.signal_slot_count)
        , port_planes_ (layout.signal_slot_count, nullptr)
        , metadata_ (layout.metadata_count)
        , writes_ (layout.pending_write_capacity)
        , write_backing_ (layout.pending_write_capacity)
        , staged_events_ (event_budget)
        , committed_signals_ (layout.committed_signal_capacity)
    {
        require(event_budget <= layout.staged_event_capacity,
            "frame event budget never exceeds the certified layout bound");
        require(region_frontier_layout_header_valid_v2(layout),
            "the capacity frame binds the exact typed V2 layout");
        require(layout.member_count == kernel.members.size()
                && layout.write_site_count == kernel.outputs.size()
                && layout.metadata_count == 1U
                && layout.fanout_edge_count == leaf_count,
            "large layout retains all certified members and fanout edges");
        require(layout.staged_event_capacity == 67U
                && layout.max_commit_fanout_events == leaf_count,
            "fixture separates total capacity 67 from per-commit fanout 33");
        require(layout.max_member_staged_event_counts != nullptr
                && layout.max_member_write_counts != nullptr,
            "the immutable layout publishes per-member bounds");
        std::uint32_t maximum_member_events { };
        std::uint32_t maximum_member_writes { };
        for (std::size_t index = 0U; index < layout.member_count; ++index) {
            maximum_member_events = std::max(maximum_member_events,
                layout.max_member_staged_event_counts[index]);
            maximum_member_writes = std::max(maximum_member_writes,
                layout.max_member_write_counts[index]);
        }
        require(maximum_member_events == 1U && maximum_member_writes == 1U,
            "each member and static write stays within a one-event bound");
        require(layout.fanout_edges != nullptr && layout.write_sites != nullptr
                && layout.signals != nullptr && layout.members != nullptr,
            "the executable fixture has complete immutable descriptors");
        bind_planes();
        bind_members_and_edges();
        bind_writes();
        initialize_frame(event_budget);
    }

    [[nodiscard]] RegionFrontierFrameV2& frame() noexcept { return frame_; }
    [[nodiscard]] const RegionFrontierFrameV2& frame() const noexcept
    {
        return frame_;
    }
    [[nodiscard]] std::size_t slot_for_signal(const SignalId signal) const
    {
        for (std::size_t index = 0U; index < planes_.size(); ++index) {
            if (planes_[index].signal_id == signal) {
                return index;
            }
        }
        throw std::runtime_error { "fixture signal has a canonical plane slot" };
    }
    [[nodiscard]] std::span<const RegionFrontierSchedulerTaskV2> tasks(
        const std::size_t begin, const std::size_t end) const
    {
        require(begin <= end && end <= frame_.scheduler_task_count,
            "task snapshots stay inside the borrowed prefix");
        return { tasks_.data() + begin, end - begin };
    }
    [[nodiscard]] std::uint64_t dispatch_count() const noexcept
    {
        return dispatch_count_;
    }
    [[nodiscard]] std::size_t member_for_process(const ProcessId process) const
    {
        for (std::size_t index = 0U; index < members_.size(); ++index) {
            if (members_[index].process_id == process) {
                return index;
            }
        }
        throw std::runtime_error { "fixture process has a member slot" };
    }
    [[nodiscard]] std::size_t write_for_process(const ProcessId process) const
    {
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            const auto& site = layout_.write_sites[index];
            if (layout_.members[site.member_index].process_id == process) {
                return site.pending_slot;
            }
        }
        throw std::runtime_error { "fixture process has a write slot" };
    }
    [[nodiscard]] std::size_t write_count() const noexcept
    {
        return layout_.write_site_count;
    }
    [[nodiscard]] std::size_t write_slot_at(const std::size_t index) const
    {
        return layout_.write_sites[index].pending_slot;
    }
    [[nodiscard]] const RegionFrontierKeyV2& root_key() const noexcept
    {
        return root_key_;
    }
    [[nodiscard]] const RegionFrontierKeyV2& root_commit_key() const
    {
        return writes_.at(root_write_slot_).commit_key;
    }
    [[nodiscard]] const RegionFrontierPendingWriteV2& write(
        const std::size_t slot) const
    {
        return writes_.at(slot);
    }
    [[nodiscard]] RegionFrontierPendingWriteV2& write(
        const std::size_t slot)
    {
        return writes_.at(slot);
    }
    [[nodiscard]] const RegionFrontierStagedEventV2& staged_event(
        const std::size_t index) const
    {
        require(index < frame_.staged_event_count,
            "staged event lookup stays inside the event prefix");
        return staged_events_[index];
    }
    [[nodiscard]] std::span<const std::uint64_t> pending_aval(
        const std::size_t slot) const
    {
        return write_backing_.at(slot).aval;
    }
    [[nodiscard]] std::span<const std::uint64_t> pending_bval(
        const std::size_t slot) const
    {
        return write_backing_.at(slot).bval;
    }
    [[nodiscard]] SignalBacking& backing(const std::size_t slot)
    {
        return signal_backing_.at(slot);
    }

    void queue_root_activation()
    {
        std::fill(ready_words_.begin(), ready_words_.end(), 0U);
        const auto root = member_for_process(root_process);
        const auto key = make_key(frame_.slot, 0U, 100U);
        root_key_ = key;
        auto& member = members_[root];
        member.flags = RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid;
        member.queued_key = key;
        member.activation_origin = key;
        member.pending_activation_origin = key;
        ready_words_[root / 64U] |= UINT64_C(1) << (root % 64U);
        tasks_[0U] = { key.stable_order, key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::member_activation, root) };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        frame_.scheduler_tasks = tasks_.data();
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
    }

    void set_cut_before(const RegionFrontierSchedulerTaskV2& task)
    {
        frame_.cut.kind = RegionFrontierCutKindV2::same_slot_key;
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        frame_.cut.next_key = make_key(frame_.slot,
            task.stable_order, task.sequence);
    }

    [[nodiscard]] std::vector<RegionFrontierSchedulerTaskV2> suffix_after_cursor()
        const
    {
        const auto first = static_cast<std::size_t>(
            frame_.scheduler_task_cursor);
        return { tasks_.begin() + static_cast<std::ptrdiff_t>(first),
            tasks_.begin() + static_cast<std::ptrdiff_t>(
                frame_.scheduler_task_count) };
    }

    [[nodiscard]] bool issue_staged_events(
        std::vector<RegionFrontierSchedulerTaskV2> suffix)
    {
        const auto event_count
            = static_cast<std::size_t>(frame_.staged_event_count);
        if (event_count == 0U || event_count > staged_events_.size()) {
            return false;
        }
        std::vector<RegionFrontierSchedulerTaskV2> merged;
        merged.reserve(suffix.size() + event_count);
        merged.insert(merged.end(), suffix.begin(), suffix.end());
        for (std::size_t index = 0U; index < event_count; ++index) {
            const auto& event = staged_events_[index];
            const auto kind = static_cast<RegionFrontierEventKindV2>(event.kind);
            const auto sequence = next_sequence_++;
            const auto key = make_key(frame_.slot, event.stable_order, sequence);
            merged.push_back({ event.stable_order, sequence,
                encode_region_frontier_payload_v2(kind,
                    event.descriptor_index) });
            if (kind == RegionFrontierEventKindV2::member_activation) {
                if (event.descriptor_index >= members_.size()) {
                    return false;
                }
                auto& member = members_[event.descriptor_index];
                if ((member.flags
                        & RegionFrontierMemberFlagsV2::pending_activation) == 0U
                    || !same_key(member.pending_activation_origin, event.origin)) {
                    return false;
                }
                member.queued_key = key;
                member.activation_origin = key;
                member.pending_activation_origin = event.origin;
                member.flags |= RegionFrontierMemberFlagsV2::queued
                    | RegionFrontierMemberFlagsV2::queued_key_valid;
                ready_words_[event.descriptor_index / 64U]
                    |= UINT64_C(1) << (event.descriptor_index % 64U);
            } else if (kind == RegionFrontierEventKindV2::internal_commit
                || kind == RegionFrontierEventKindV2::boundary_commit) {
                if (event.descriptor_index >= writes_.size()) {
                    return false;
                }
                auto& write = writes_[event.descriptor_index];
                write.commit_key = key;
                write.flags |= RegionFrontierPendingWriteFlagsV2::pending_key_assigned;
            } else {
                return false;
            }
        }
        std::ranges::stable_sort(merged,
            [](const auto& left, const auto& right) {
                return left.stable_order < right.stable_order
                    || (left.stable_order == right.stable_order
                        && left.sequence < right.sequence);
            });
        require(merged.size() <= tasks_.size(),
            "merged scheduler keys fit preallocated test backing");
        std::copy(merged.begin(), merged.end(), tasks_.begin());
        frame_.scheduler_tasks = tasks_.data();
        frame_.scheduler_task_count = static_cast<std::uint32_t>(merged.size());
        frame_.scheduler_task_cursor = 0U;
        frame_.staged_event_count = 0U;
        ++frame_.scheduler_frontier_generation;
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        return true;
    }

private:
    void bind_planes()
    {
        std::vector<SignalId> expected;
        for (const auto& input : kernel_.inputs) {
            expected.push_back(input.signal);
        }
        for (const auto& output : kernel_.outputs) {
            expected.push_back(output.signal);
        }
        std::ranges::sort(expected);
        expected.erase(std::unique(expected.begin(), expected.end()),
            expected.end());
        require(expected.size() == layout_.signal_slot_count,
            "canonical slots equal the kernel input/output union");
        std::size_t internal_slots { };
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            const auto& descriptor = layout_.signals[index];
            auto& plane = planes_[index];
            auto& backing = signal_backing_[index];
            plane.signal_id = descriptor.signal_id;
            plane.owner_process_id = descriptor.owner_process_id;
            plane.value_kind = descriptor.value_kind;
            plane.plane_count = descriptor.plane_count;
            plane.width = descriptor.width;
            plane.word_count = descriptor.word_count;
            plane.flags = descriptor.flags;
            plane.metadata_index = descriptor.metadata_index;
            const auto words = static_cast<std::size_t>(descriptor.word_count);
            if ((descriptor.flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U) {
                require(descriptor.signal_id == internal_signal
                        && descriptor.metadata_index < metadata_.size(),
                    "only the root-to-leaf signal has mutable storage");
                ++internal_slots;
                backing.current_aval.assign(words, 0U);
                backing.current_bval.assign(words, 0U);
                backing.previous_aval.assign(words, 0U);
                backing.previous_bval.assign(words, 0U);
                backing.stored_aval.assign(words, 0U);
                backing.stored_bval.assign(words, 0U);
                backing.owner_aval.assign(words, 0U);
                backing.owner_bval.assign(words, 0U);
                plane.current_planes[0] = backing.current_aval.data();
                plane.current_planes[1] = backing.current_bval.data();
                plane.previous_planes[0] = backing.previous_aval.data();
                plane.previous_planes[1] = backing.previous_bval.data();
                plane.stored_planes[0] = backing.stored_aval.data();
                plane.stored_planes[1] = backing.stored_bval.data();
                plane.owner_planes[0] = backing.owner_aval.data();
                plane.owner_planes[1] = backing.owner_bval.data();
            } else {
                require((descriptor.flags
                            & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                        != 0U,
                    "all other signal slots are read-only boundary ports");
                backing.boundary_aval.assign(words, 0U);
                backing.boundary_bval.assign(words, 0U);
                plane.boundary_planes[0] = backing.boundary_aval.data();
                plane.boundary_planes[1] = backing.boundary_bval.data();
                if (descriptor.signal_id == source_signal) {
                    backing.boundary_aval[0U] = 1U;
                }
            }
            require(region_frontier_plane_bindings_valid_v2(plane),
                "fixture backing satisfies the typed plane contract");
            port_planes_[index] = &plane;
        }
        require(internal_slots == 1U && metadata_.size() == 1U,
            "one private role set records the root's internal transaction");
        for (const auto& input : kernel_.inputs) {
            const auto slot = slot_for_signal(input.signal);
            const auto internal = (planes_[slot].flags
                & RegionFrontierPlaneFlagsV2::certified_internal_single_owner) != 0U;
            require(input.internal == internal
                    && input.value_kind == ValueKind::logic4
                    && input.width == planes_[slot].width,
                "each input port retains its certified current-value source");
        }
    }

    void bind_members_and_edges()
    {
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            members_[index].process_id = layout_.members[index].process_id;
            members_[index].flags
                = RegionFrontierMemberFlagsV2::waiting_on_static;
            members_[index].static_trigger_mask
                = Process::full_static_trigger_mask;
        }
        std::copy_n(layout_.fanout_edges, layout_.fanout_edge_count,
            fanout_edges_.begin());
        for (auto& edge : fanout_edges_) {
            require(edge.signal_slot < layout_.signal_slot_count
                    && edge.member_index < layout_.member_count
                    && layout_.signals[edge.signal_slot].signal_id
                        == internal_signal
                    && layout_.members[edge.member_index].process_id != root_process,
                "every fanout edge is a distinct static internal reader");
            edge.trigger_mask = Process::full_static_trigger_mask;
        }
    }

    void bind_writes()
    {
        require(layout_.pending_write_capacity == layout_.write_site_count,
            "all static sites have preassigned unique pending slots");
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            const auto& site = layout_.write_sites[index];
            require(site.pending_slot < writes_.size()
                    && site.member_index < members_.size()
                    && site.signal_slot < planes_.size()
                    && site.width == 1U
                    && site.word_count == 1U,
                "every capacity-test write fits its one-bit role");
            auto& backing = write_backing_[site.pending_slot];
            backing.aval.assign(1U, 0U);
            backing.bval.assign(1U, 0U);
            auto& write = writes_[site.pending_slot];
            write.member_index = site.member_index;
            write.signal_slot = site.signal_slot;
            write.source_instruction = site.source_instruction;
            write.update_kind = site.update_kind;
            write.value_planes[0U] = backing.aval.data();
            write.value_planes[1U] = backing.bval.data();
            write.value_kind = site.value_kind;
            write.plane_count = site.plane_count;
            write.width = 1U;
            write.word_count = 1U;
        }
        root_write_slot_ = write_for_process(root_process);
    }

    void initialize_frame(const std::uint32_t event_budget)
    {
        frame_.abi_version = kRegionFrontierAbiVersionV2;
        frame_.struct_size = sizeof(RegionFrontierFrameV2);
        frame_.value_plane_contract = kRegionFrontierValuePlaneContractV2;
        frame_.runtime_generation = 401U;
        frame_.bound_runtime_generation = 401U;
        frame_.certificate_generation = layout_.certificate_generation;
        frame_.component_generation = layout_.component_generation;
        frame_.scheduler_frontier_generation = 1U;
        frame_.member_count = layout_.member_count;
        frame_.scheduler_task_capacity = static_cast<std::uint32_t>(tasks_.size());
        frame_.readiness_word_count = layout_.readiness_word_count;
        frame_.signal_slot_count = layout_.signal_slot_count;
        frame_.metadata_count = layout_.metadata_count;
        frame_.fanout_edge_count = layout_.fanout_edge_count;
        frame_.committed_signal_capacity = layout_.committed_signal_capacity;
        frame_.committed_signal_count = 0U;
        frame_.pending_write_capacity = layout_.pending_write_capacity;
        frame_.pending_write_count = 0U;
        frame_.staged_event_capacity = event_budget;
        frame_.staged_event_count = 0U;
        frame_.ready_words = ready_words_.data();
        frame_.members = members_.data();
        frame_.scheduler_tasks = tasks_.data();
        frame_.planes = planes_.data();
        frame_.metadata = metadata_.data();
        frame_.fanout_edges = fanout_edges_.data();
        frame_.port_planes = port_planes_.data();
        frame_.pending_writes = writes_.data();
        frame_.staged_events = staged_events_.data();
        frame_.committed_signals = committed_signals_.data();
        frame_.native_frontier_member_dispatches = &dispatch_count_;
        frame_.stop_requested = &stop_value_;
        frame_.slot = { 0U, 0U, 0U,
            static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog),
            0U };
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
    }

    const RegionConeActivationKernel& kernel_;
    const RegionFrontierLayoutV2& layout_;
    std::vector<std::uint64_t> ready_words_;
    std::vector<RegionFrontierMemberV2> members_;
    std::vector<RegionFrontierSchedulerTaskV2> tasks_;
    std::vector<RegionFrontierPlaneV2> planes_;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_edges_;
    std::vector<SignalBacking> signal_backing_;
    std::vector<const RegionFrontierPlaneV2*> port_planes_;
    std::vector<RegionFrontierSignalMetadataV2> metadata_;
    std::vector<RegionFrontierPendingWriteV2> writes_;
    std::vector<WriteBacking> write_backing_;
    std::vector<RegionFrontierStagedEventV2> staged_events_;
    std::vector<RegionFrontierCommittedSignalV2> committed_signals_;
    std::uint64_t dispatch_count_ { };
    std::uint32_t stop_value_ { };
    std::uint64_t next_sequence_ { 1000U };
    std::size_t root_write_slot_ { };
    RegionFrontierKeyV2 root_key_;
    RegionFrontierFrameV2 frame_ { };
};

void acknowledge_boundary_write(CapacityFrame& storage,
    const std::size_t pending_slot)
{
    auto& frame = storage.frame();
    auto& write = storage.write(pending_slot);
    require((write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_active) != 0U
            && (write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_value_ready) != 0U
            && (write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_key_assigned) != 0U
            && (write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_boundary_target) != 0U
            && (write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_committed) == 0U,
        "the boundary write is retained until the host acknowledges it");
    require(storage.pending_aval(pending_slot)[0U] == 1U
            && storage.pending_bval(pending_slot)[0U] == 0U,
        "every leaf forwards the committed internal one into its private row");
    const auto plane_slot = static_cast<std::size_t>(write.signal_slot);
    auto& backing = storage.backing(plane_slot);
    backing.boundary_aval[0U] = storage.pending_aval(pending_slot)[0U];
    backing.boundary_bval[0U] = storage.pending_bval(pending_slot)[0U];
    write.flags |= RegionFrontierPendingWriteFlagsV2::pending_committed;
    frame.committed_signal_count = 0U;
}

void exercise_capacity_route(const RegionConeActivationKernel& kernel,
    const LlvmRegionFrontierExecutor& executor)
{
    const auto& layout = executor.layout();
    require(region_frontier_layout_header_valid_v2(layout),
        "the generated capacity layout has the typed V2 plane contract");
    CapacityFrame storage { kernel, layout, frame_event_budget };
    auto& frame = storage.frame();
    require(frame.staged_event_capacity >= layout.max_commit_fanout_events,
        "the positive frame covers the largest single-commit fanout");
    storage.queue_root_activation();
    require(layout.staged_event_capacity > frame.staged_event_capacity
            && frame.staged_event_capacity == frame_event_budget,
        "the actual event backing is 64 while immutable total layout demand is 67");

    require(executor.step_entry()(&frame)
                == RegionFrontierStatusV2::need_scheduler_keys
            && storage.dispatch_count() == 1U
            && frame.staged_event_count == 1U
            && frame.pending_write_count == 1U,
        "a real root activation executes in the smaller event frame");
    const auto root_event = storage.staged_event(0U);
    require(root_event.kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::internal_commit)
            && root_event.descriptor_index == storage.write_for_process(root_process)
            && same_key(root_event.origin, storage.root_key()),
        "the root commit event retains its original activation cause");
    require(storage.issue_staged_events({ }),
        "the host issues the one staged root commit key");
    const auto root_commit_task = storage.tasks(0U,
        frame.scheduler_task_count).front();
    const auto root_commit_key = make_key(frame.slot,
        root_commit_task.stable_order, root_commit_task.sequence);
    require(same_key(root_commit_key, storage.root_commit_key()),
        "the root output retains the scheduler-issued commit key");
    require(executor.step_entry()(&frame)
                == RegionFrontierStatusV2::need_scheduler_keys
            && frame.committed_signal_count == 1U
            && frame.staged_event_count == leaf_count
            && frame.staged_event_count <= frame.staged_event_capacity,
        "the commit stages all 33 static readers inside the 64-event budget");
    const auto internal_slot = storage.slot_for_signal(internal_signal);
    require(frame.committed_signals[0U].signal_slot == internal_slot
            && frame.committed_signals[0U].changed == 1U
            && frame.committed_signals[0U].state_changed == 1U,
        "native publication records the completed producer transaction");
    auto& internal = storage.backing(internal_slot);
    require(internal.current_aval[0U] == 1U
            && internal.previous_aval[0U] == 0U
            && internal.stored_aval[0U] == 1U
            && internal.owner_aval[0U] == 1U,
        "the small frame preserves current, LAST, stored, and owner roles");
    frame.committed_signal_count = 0U;

    std::vector<std::uint8_t> fanout_members(layout.member_count, 0U);
    for (std::size_t index = 0U; index < frame.staged_event_count; ++index) {
        const auto& event = storage.staged_event(index);
        require(event.kind == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::member_activation)
                && event.descriptor_index < layout.member_count
                && layout.members[event.descriptor_index].process_id != root_process
                && event.stable_order
                    == layout.members[event.descriptor_index].process_id
                && same_key(event.origin, root_commit_key),
            "every staged reader keeps its commit origin and distinct scheduler order");
        require(fanout_members[event.descriptor_index] == 0U,
            "the commit emits each reader activation exactly once");
        fanout_members[event.descriptor_index] = 1U;
    }
    require(static_cast<std::size_t>(std::count(fanout_members.begin(),
                fanout_members.end(), 1U)) == leaf_count,
        "the fanout event set covers every reader in the oversized layout");
    require(storage.issue_staged_events({ }),
        "the scheduler issues all fanout activations from the bounded frame");
    const auto original_task_count = static_cast<std::size_t>(
        frame.scheduler_task_count);
    require(original_task_count == leaf_count,
        "the borrowed activation prefix contains all 33 readers");
    std::uint64_t previous_stable_order { };
    for (std::size_t index = 0U; index < original_task_count; ++index) {
        const auto task = storage.tasks(index, index + 1U).front();
        const auto member = static_cast<std::size_t>(task.payload
            & kRegionFrontierPayloadIndexMaskV2);
        require(member < layout.member_count
                && layout.members[member].process_id == task.stable_order,
            "the scheduler-ordered prefix retains each original member identity");
        require(index == 0U || previous_stable_order < task.stable_order,
            "the borrowed activation prefix retains strict scheduler order");
        previous_stable_order = task.stable_order;
    }

    const auto first_task = storage.tasks(0U, 1U).front();
    const auto second_task = storage.tasks(1U, 2U).front();
    require(first_task.stable_order < second_task.stable_order,
        "the cut witness has a deterministic first task and retained suffix");
    storage.set_cut_before(second_task);
    require(executor.step_entry()(&frame)
                == RegionFrontierStatusV2::need_scheduler_keys
            && frame.scheduler_task_cursor == 1U
            && frame.scheduler_task_count == original_task_count
            && storage.dispatch_count() == 2U
            && frame.staged_event_count == 1U,
        "one useful member executes before the authenticated scheduler cut");
    const auto suffix = storage.suffix_after_cursor();
    require(suffix.size() == leaf_count - 1U,
        "all not-yet-consumed activation keys remain in the borrowed suffix");
    std::vector<RegionFrontierSchedulerTaskV2> suffix_copy {
        suffix.begin(), suffix.end() };
    const auto boundary_event = storage.staged_event(0U);
    const auto first_member = static_cast<std::size_t>(
        first_task.payload & kRegionFrontierPayloadIndexMaskV2);
    const auto first_process = layout.members[first_member].process_id;
    const auto first_write = storage.write_for_process(first_process);
    require(boundary_event.kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::boundary_commit)
            && boundary_event.descriptor_index == first_write
            && boundary_event.stable_order == first_process
            && same_key(boundary_event.origin, root_commit_key)
            && same_key(storage.write(first_write).origin, root_commit_key),
        "the first leaf commit preserves the root fanout's causal origin");

    require(storage.issue_staged_events(std::move(suffix_copy)),
        "a fresh boundary key is reserved without dropping the old suffix");
    require(frame.scheduler_task_count == leaf_count
            && frame.scheduler_task_cursor == 0U,
        "the merged scheduler span contains the fresh commit and 32 retained activations");
    const auto inserted_boundary = storage.tasks(0U, 1U).front();
    require((inserted_boundary.payload >> kRegionFrontierPayloadKindShiftV2)
                == static_cast<std::uint64_t>(
                    RegionFrontierEventKindV2::boundary_commit)
            && (inserted_boundary.payload & kRegionFrontierPayloadIndexMaskV2)
                == first_write,
        "the earlier boundary key precedes the unconsumed activation suffix");
    for (std::size_t index = 0U; index < leaf_count - 1U; ++index) {
        const auto merged_suffix = storage.tasks(index + 1U, index + 2U).front();
        require(merged_suffix.stable_order == suffix[index].stable_order
                && merged_suffix.sequence == suffix[index].sequence
                && merged_suffix.payload == suffix[index].payload,
            "each old borrowed task is preserved byte-for-byte and in order");
    }

    auto status = executor.step_entry()(&frame);
    require(status == RegionFrontierStatusV2::boundary_publication
            && frame.scheduler_task_cursor == 0U,
        "the inserted boundary key reaches host publication at its sorted position");
    acknowledge_boundary_write(storage, first_write);
    status = executor.step_entry()(&frame);
    require(status == RegionFrontierStatusV2::need_scheduler_keys
            && frame.scheduler_task_cursor == leaf_count
            && storage.dispatch_count() == leaf_count + 1U
            && frame.staged_event_count == leaf_count - 1U,
        "the retained suffix drains once and leaves 32 bounded boundary events");
    require(storage.issue_staged_events({ }),
        "the host reserves the remaining leaf commits in a second bounded batch");

    std::size_t acknowledged { };
    status = executor.step_entry()(&frame);
    while (status == RegionFrontierStatusV2::boundary_publication) {
        const auto cursor = static_cast<std::size_t>(
            frame.scheduler_task_cursor);
        require(cursor < frame.scheduler_task_count,
            "a boundary publication remains at the borrowed task cursor");
        const auto task = storage.tasks(cursor, cursor + 1U).front();
        require((task.payload >> kRegionFrontierPayloadKindShiftV2)
                == static_cast<std::uint64_t>(
                    RegionFrontierEventKindV2::boundary_commit),
            "only an original boundary commit requires a host acknowledgement");
        const auto write_slot = static_cast<std::size_t>(
            task.payload & kRegionFrontierPayloadIndexMaskV2);
        const auto& write = storage.write(write_slot);
        require(same_key(write.origin, root_commit_key),
            "all boundary outputs retain the original internal cause");
        acknowledge_boundary_write(storage, write_slot);
        ++acknowledged;
        status = executor.step_entry()(&frame);
    }
    require(status == RegionFrontierStatusV2::quiescent
            && acknowledged == leaf_count - 1U
            && frame.scheduler_task_cursor == frame.scheduler_task_count
            && frame.pending_write_count == 0U
            && frame.staged_event_count == 0U
            && storage.dispatch_count() == leaf_count + 1U,
        "bounded event batches retire every source and boundary task exactly once");
    for (std::uint32_t index = 0U; index < leaf_count; ++index) {
        const auto slot = storage.slot_for_signal(
            static_cast<SignalId>(index + 2U));
        require(storage.backing(slot).boundary_aval[0U] == 1U
                && storage.backing(slot).boundary_bval[0U] == 0U,
            "all boundary outputs publish the committed internal value");
    }
}

void check_too_small_frame_declines(const RegionConeActivationKernel& kernel,
    const LlvmRegionFrontierExecutor& executor)
{
    CapacityFrame storage { kernel, executor.layout(), 32U };
    auto& frame = storage.frame();
    storage.queue_root_activation();
    const auto root_member = storage.member_for_process(root_process);
    const auto root_task_before = storage.tasks(0U, 1U).front();
    const std::vector<std::uint64_t> ready_before(frame.ready_words,
        frame.ready_words + frame.readiness_word_count);
    const RegionFrontierKeyV2 empty_key { };
    require(executor.step_entry()(&frame)
                == RegionFrontierStatusV2::decline_before_mutation
            && storage.dispatch_count() == 0U
            && frame.scheduler_task_cursor == 0U
            && frame.pending_write_count == 0U
            && frame.staged_event_count == 0U
            && frame.committed_signal_count == 0U
            && frame.current_member == UINT32_MAX
            && frame.current_pending_write == UINT32_MAX
            && frame.current_commit_changed == 0U
            && frame.saved_body_pc == 0U
            && storage.tasks(0U, 1U).front().stable_order
                == root_task_before.stable_order
            && storage.tasks(0U, 1U).front().sequence
                == root_task_before.sequence
            && storage.tasks(0U, 1U).front().payload
                == root_task_before.payload
            && std::equal(ready_before.begin(), ready_before.end(),
                frame.ready_words),
        "a frame below the certified fanout bound declines before mutation");

    for (std::size_t index = 0U; index < frame.member_count; ++index) {
        const auto& member = frame.members[index];
        const auto expected_flags = index == root_member
            ? RegionFrontierMemberFlagsV2::queued
                | RegionFrontierMemberFlagsV2::queued_key_valid
            : RegionFrontierMemberFlagsV2::waiting_on_static;
        require(member.flags == expected_flags
                && member.static_trigger_mask
                    == Process::full_static_trigger_mask
                && same_key(member.queued_key,
                    index == root_member ? storage.root_key()
                                         : empty_key)
                && same_key(member.activation_origin,
                    index == root_member ? storage.root_key()
                                         : empty_key)
                && same_key(member.pending_activation_origin,
                    index == root_member ? storage.root_key()
                                         : empty_key),
            "decline preserves every queued-member certificate field");
    }
    for (std::size_t index = 0U; index < storage.write_count(); ++index) {
        const auto slot = storage.write_slot_at(index);
        const auto& write = storage.write(slot);
        require(write.flags == 0U && same_key(write.commit_key, empty_key)
                && same_key(write.origin, empty_key)
                && all_zero(storage.pending_aval(slot))
                && all_zero(storage.pending_bval(slot)),
            "decline leaves every pending output descriptor and value untouched");
    }
    for (std::size_t index = 0U; index < frame.staged_event_capacity; ++index) {
        const auto& event = frame.staged_events[index];
        require(event.kind == 0U && event.descriptor_index == 0U
                && event.stable_order == 0U && same_key(event.origin, empty_key),
            "decline leaves the full staged-event backing untouched");
    }
    for (std::size_t index = 0U; index < frame.committed_signal_capacity; ++index) {
        const auto& committed = frame.committed_signals[index];
        require(committed.signal_slot == 0U && committed.changed == 0U
                && committed.state_changed == 0U,
            "decline leaves the committed-signal backing untouched");
    }
    const auto internal_slot = storage.slot_for_signal(internal_signal);
    const auto& internal = storage.backing(internal_slot);
    require(all_zero(internal.current_aval) && all_zero(internal.current_bval)
            && all_zero(internal.previous_aval)
            && all_zero(internal.previous_bval)
            && all_zero(internal.stored_aval) && all_zero(internal.stored_bval)
            && all_zero(internal.owner_aval) && all_zero(internal.owner_bval),
        "decline leaves all private signal roles untouched");
    const auto& metadata = frame.metadata[0U];
    require(metadata.event_time == 0U && metadata.event_delta == 0U
            && metadata.transaction_time == 0U
            && metadata.transaction_delta == 0U
            && metadata.value_revision == 0U
            && metadata.systemverilog_round == 0U
            && metadata.event_process_domain == 0U
            && metadata.event_phase == 0U && metadata.event_valid == 0U
            && metadata.transaction_valid == 0U,
        "decline leaves event and transaction metadata untouched");
    for (std::uint32_t index = 0U; index < leaf_count; ++index) {
        const auto output_slot = storage.slot_for_signal(
            static_cast<SignalId>(index + 2U));
        const auto& output = storage.backing(output_slot);
        require(all_zero(output.boundary_aval)
                && all_zero(output.boundary_bval),
            "decline leaves every public boundary value untouched");
    }
}

} // namespace

void run_region_frontier_event_capacity_tests()
{
    const auto kernel = make_capacity_kernel();
    for (const auto optimization : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2,
         }) {
        const auto identity = std::string { "frontier-event-budget-67-64-" }
            + std::to_string(static_cast<std::uint8_t>(optimization));
        LlvmJitOptions options;
        options.optimization = optimization;
        options.debug_instrumentation = false;
        options.require_direct_update_slots = true;
        options.require_direct_read_signals = true;
        auto executor = LlvmRegionFrontierExecutor::try_create(
            kernel, options, identity);
        require(executor != nullptr && executor->step_entry() != nullptr,
            "the production emitter accepts the large certified event layout");
        require(executor->layout().staged_event_capacity == 67U
                && executor->layout().max_commit_fanout_events == leaf_count,
            "the compiled immutable layout exposes the exact event bounds");
        exercise_capacity_route(kernel, *executor);
        check_too_small_frame_declines(kernel, *executor);
    }
}

} // namespace fsim::compiler::test

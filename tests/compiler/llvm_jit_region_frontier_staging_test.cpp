// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_staging_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::compiler::test {
namespace {

using namespace runtime::simir;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

std::size_t words_for(const std::uint32_t width)
{
    return (static_cast<std::size_t>(width) + 63U) / 64U;
}

[[nodiscard]] std::uint32_t plane_count_for(
    const ValueKind kind) noexcept
{
    return kind == ValueKind::logic9
        ? kRegionFrontierLogic9PlaneCountV2
        : kRegionFrontierLogic4PlaneCountV2;
}

[[nodiscard]] RegionFrontierValueKindV2 frontier_kind_for(
    const ValueKind kind)
{
    switch (kind) {
    case ValueKind::logic4:
        return RegionFrontierValueKindV2::logic4;
    case ValueKind::logic9:
        return RegionFrontierValueKindV2::logic9;
    }
    throw std::invalid_argument { "unsupported frontier fixture value kind" };
}

[[nodiscard]] std::vector<std::uint64_t>& value_plane(
    RegionFrontierTestValue& value, const std::size_t plane)
{
    switch (plane) {
    case 0U:
        return value.aval;
    case 1U:
        return value.bval;
    case 2U:
        return value.plane2;
    case 3U:
        return value.plane3;
    default:
        throw std::out_of_range { "frontier value plane index is in range" };
    }
}

[[nodiscard]] const std::vector<std::uint64_t>& value_plane(
    const RegionFrontierTestValue& value, const std::size_t plane)
{
    switch (plane) {
    case 0U:
        return value.aval;
    case 1U:
        return value.bval;
    case 2U:
        return value.plane2;
    case 3U:
        return value.plane3;
    default:
        throw std::out_of_range { "frontier value plane index is in range" };
    }
}

using PlaneVectors = std::array<std::vector<std::uint64_t>, 4U>;

bool same_key(const RegionFrontierKeyV2& left,
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

bool same_slot(const RegionFrontierSlotV2& left,
    const RegionFrontierSlotV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

bool same_cut(const RegionFrontierCutV2& left,
    const RegionFrontierCutV2& right) noexcept
{
    return left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && same_key(left.next_key, right.next_key)
        && left.kind == right.kind;
}

bool same_member(const RegionFrontierMemberV2& left,
    const RegionFrontierMemberV2& right) noexcept
{
    return left.process_id == right.process_id
        && left.flags == right.flags
        && left.static_trigger_mask == right.static_trigger_mask
        && same_key(left.queued_key, right.queued_key)
        && same_key(left.activation_origin, right.activation_origin)
        && same_key(left.pending_activation_origin,
            right.pending_activation_origin);
}

bool same_task(const RegionFrontierSchedulerTaskV2& left,
    const RegionFrontierSchedulerTaskV2& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.payload == right.payload;
}

bool same_write(const RegionFrontierPendingWriteV2& left,
    const RegionFrontierPendingWriteV2& right) noexcept
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
        && left.plane_count == right.plane_count
        && std::equal(std::begin(left.value_planes),
            std::end(left.value_planes), std::begin(right.value_planes))
        && left.width == right.width
        && left.word_count == right.word_count;
}

bool same_event(const RegionFrontierStagedEventV2& left,
    const RegionFrontierStagedEventV2& right) noexcept
{
    return left.kind == right.kind
        && left.descriptor_index == right.descriptor_index
        && left.stable_order == right.stable_order
        && same_key(left.origin, right.origin);
}

bool same_fanout_edge(const RegionFrontierFanoutEdgeV2& left,
    const RegionFrontierFanoutEdgeV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.member_index == right.member_index
        && left.trigger_mask == right.trigger_mask;
}

bool same_committed_signal(const RegionFrontierCommittedSignalV2& left,
    const RegionFrontierCommittedSignalV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.changed == right.changed
        && left.state_changed == right.state_changed;
}

bool same_metadata(const RegionFrontierSignalMetadataV2& left,
    const RegionFrontierSignalMetadataV2& right) noexcept
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
        && left.transaction_valid == right.transaction_valid;
}

bool same_plane(const RegionFrontierPlaneV2& left,
    const RegionFrontierPlaneV2& right) noexcept
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
            std::end(left.boundary_planes),
            std::begin(right.boundary_planes))
        && std::equal(std::begin(left.current_planes),
            std::end(left.current_planes),
            std::begin(right.current_planes))
        && std::equal(std::begin(left.previous_planes),
            std::end(left.previous_planes),
            std::begin(right.previous_planes))
        && std::equal(std::begin(left.stored_planes),
            std::end(left.stored_planes),
            std::begin(right.stored_planes))
        && std::equal(std::begin(left.owner_planes),
            std::end(left.owner_planes),
            std::begin(right.owner_planes));
}

struct PlaneWords {
    PlaneVectors boundary;
    PlaneVectors current;
    PlaneVectors previous;
    PlaneVectors stored;
    PlaneVectors owner;

    bool operator==(const PlaneWords&) const = default;
};

struct PlaneBacking {
    PlaneWords words;
};

struct WriteBacking {
    PlaneVectors values;
};

struct FrameSnapshot {
    RegionFrontierFrameV2 frame;
    std::vector<std::uint64_t> ready_words;
    std::vector<RegionFrontierMemberV2> members;
    std::vector<RegionFrontierSchedulerTaskV2> tasks;
    std::vector<RegionFrontierPlaneV2> planes;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_edges;
    std::vector<const RegionFrontierPlaneV2*> port_bindings;
    std::vector<RegionFrontierSignalMetadataV2> metadata;
    std::vector<RegionFrontierPendingWriteV2> writes;
    std::vector<RegionFrontierStagedEventV2> events;
    std::vector<RegionFrontierCommittedSignalV2> committed_signals;
    std::vector<PlaneWords> plane_words;
    std::vector<PlaneVectors> write_values;
    std::uint64_t dispatch_count { };
    std::uint32_t stop_value { };
};

class FrameStorage final {
public:
    FrameStorage(const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout,
        const std::uint64_t runtime_generation,
        RegionFrontierStagingValues values)
        : kernel_ { kernel }
        , layout_ { layout }
        , values_ { std::move(values) }
        , readiness_ (layout.readiness_word_count, 0U)
        , members_ (layout.member_count)
        , tasks_ (std::max<std::size_t>(
              std::max(layout.staged_event_capacity,
                  layout.pending_write_capacity),
              layout.member_count))
        , planes_ (layout.signal_slot_count)
        , fanout_edges_ (layout.fanout_edge_count)
        , plane_backing_ (layout.signal_slot_count)
        , port_bindings_ (layout.signal_slot_count, nullptr)
        , metadata_ (layout.metadata_count)
        , writes_ (layout.pending_write_capacity)
        , write_backing_ (layout.pending_write_capacity)
        , events_ (layout.staged_event_capacity)
        , committed_signals_ (layout.committed_signal_capacity)
    {
        require(layout.abi_version == kRegionFrontierAbiVersionV2
                && layout.struct_size == sizeof(RegionFrontierLayoutV2)
                && region_frontier_layout_header_valid_v2(layout),
            "the real executor exposes the current immutable frontier layout");
        require(layout.member_count == kernel.members.size()
                && layout.member_count >= 2U
                && layout.members != nullptr,
            "the fixture has certified member descriptors");
        require(layout.readiness_word_count
                == (layout.member_count + 63U) / 64U,
            "ready-bit storage covers every certified member");
        require(layout.signal_slot_count != 0U && layout.signals != nullptr,
            "the fixture binds its canonical input/output signal union");
        require(layout.signal_slot_count > kernel.internal_signals.size(),
            "the fixture includes boundary slots outside internal metadata");
        require(layout.write_site_count == kernel.outputs.size()
                && layout.write_sites != nullptr
                && layout.write_site_count >= 2U,
            "the fixture has one immutable write site per source output");
        require(layout.pending_write_capacity != 0U
                && layout.staged_event_capacity != 0U
                && layout.committed_signal_capacity
                    == layout.pending_write_capacity,
            "the executor reserved fixed output and scheduler-event storage");
        require(layout.metadata_count == kernel.internal_signals.size()
                && layout.fanout_edges != nullptr
                && layout.fanout_edge_count != 0U,
            "the fixture binds internal-only metadata and successor edges");
        require(kernel.inputs.size() >= 2U,
            "producer and consumer have mapped read-current ports");

        prepare_staging_values();
        prepare_fanout_edges();
        bind_signal_planes();
        bind_write_sites();
        initialize_frame(runtime_generation);
    }

    [[nodiscard]] RegionFrontierFrameV2& frame() noexcept { return frame_; }

    [[nodiscard]] std::size_t root_member_index() const
    {
        const auto root_process = kernel_.members.front().process;
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            if (layout_.members[index].process_id == root_process) {
                return index;
            }
        }
        throw std::runtime_error {
            "the producer is present in the generated member layout"
        };
    }

    [[nodiscard]] std::size_t consumer_member_index() const
    {
        const auto consumer_process = kernel_.members.back().process;
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            if (layout_.members[index].process_id == consumer_process) {
                return index;
            }
        }
        throw std::runtime_error {
            "the consumer is present in the generated member layout"
        };
    }

    [[nodiscard]] std::size_t signal_slot_index(const SignalId signal) const
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            if (layout_.signals[index].signal_id == signal) {
                return index;
            }
        }
        throw std::runtime_error {
            "a kernel signal has a bound canonical frontier slot"
        };
    }

    [[nodiscard]] std::size_t internal_plane_index(
        const SignalId signal) const
    {
        const auto index = signal_slot_index(signal);
        require((layout_.signals[index].flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U,
            "the requested signal has an internal mutable plane");
        return index;
    }

    [[nodiscard]] std::size_t value_plane_count(
        const std::size_t signal_slot) const
    {
        return planes_.at(signal_slot).plane_count;
    }

    [[nodiscard]] std::size_t external_input_plane_index() const
    {
        return signal_slot_index(external_input_signal_);
    }

    [[nodiscard]] std::span<std::uint64_t> boundary_aval(
        const std::size_t index) noexcept
    {
        return plane_backing_[index].words.boundary[0U];
    }

    [[nodiscard]] std::span<std::uint64_t> boundary_bval(
        const std::size_t index) noexcept
    {
        return plane_backing_[index].words.boundary[1U];
    }

    [[nodiscard]] std::span<std::uint64_t> boundary_plane(
        const std::size_t index, const std::size_t plane) noexcept
    {
        return plane_backing_[index].words.boundary[plane];
    }

    [[nodiscard]] std::span<std::uint64_t> mutable_role_plane(
        const std::size_t index, const std::size_t plane,
        const std::size_t role)
    {
        auto& words = plane_backing_.at(index).words;
        switch (role) {
        case 0U:
            return words.current.at(plane);
        case 1U:
            return words.previous.at(plane);
        case 2U:
            return words.stored.at(plane);
        case 3U:
            return words.owner.at(plane);
        default:
            throw std::out_of_range { "frontier mutable role is in range" };
        }
    }

    [[nodiscard]] std::span<std::uint64_t> mutable_pending_plane(
        const std::size_t index, const std::size_t plane)
    {
        return write_backing_.at(index).values.at(plane);
    }

    [[nodiscard]] std::span<const std::uint64_t> role_plane(
        const std::size_t index, const std::size_t plane,
        const std::size_t role) const
    {
        const auto& words = plane_backing_.at(index).words;
        switch (role) {
        case 0U:
            return words.current[plane];
        case 1U:
            return words.previous[plane];
        case 2U:
            return words.stored[plane];
        case 3U:
            return words.owner[plane];
        default:
            throw std::out_of_range { "frontier mutable role is in range" };
        }
    }

    [[nodiscard]] std::span<const std::uint64_t> current_aval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.current[0U];
    }

    [[nodiscard]] std::span<const std::uint64_t> current_bval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.current[1U];
    }

    [[nodiscard]] std::span<const std::uint64_t> previous_aval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.previous[0U];
    }

    [[nodiscard]] std::span<const std::uint64_t> previous_bval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.previous[1U];
    }

    [[nodiscard]] std::span<const std::uint64_t> stored_aval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.stored[0U];
    }

    [[nodiscard]] std::span<const std::uint64_t> stored_bval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.stored[1U];
    }

    [[nodiscard]] std::span<const std::uint64_t> owner_aval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.owner[0U];
    }

    [[nodiscard]] std::span<const std::uint64_t> owner_bval(
        const std::size_t index) const
    {
        return plane_backing_.at(index).words.owner[1U];
    }

    [[nodiscard]] std::span<const std::uint64_t> pending_aval(
        const std::size_t index) const
    {
        return write_backing_.at(index).values[0U];
    }

    [[nodiscard]] std::span<const std::uint64_t> pending_bval(
        const std::size_t index) const
    {
        return write_backing_.at(index).values[1U];
    }

    [[nodiscard]] std::span<const std::uint64_t> pending_plane(
        const std::size_t index, const std::size_t plane) const
    {
        return write_backing_.at(index).values[plane];
    }

    [[nodiscard]] std::uint64_t dispatch_count() const noexcept
    {
        return dispatch_count_;
    }

    [[nodiscard]] const RegionFrontierStagingValues& values() const noexcept
    {
        return values_;
    }

    [[nodiscard]] std::uint32_t& stop_value() noexcept
    {
        return stop_value_;
    }

    [[nodiscard]] RegionFrontierFrameV2& mutable_frame() noexcept
    {
        return frame_;
    }

    [[nodiscard]] FrameSnapshot snapshot() const
    {
        FrameSnapshot result;
        result.frame = frame_;
        result.ready_words = readiness_;
        result.members = members_;
        result.tasks = tasks_;
        result.planes = planes_;
        result.fanout_edges = fanout_edges_;
        result.port_bindings = port_bindings_;
        result.metadata = metadata_;
        result.writes = writes_;
        result.events = events_;
        result.committed_signals = committed_signals_;
        result.dispatch_count = dispatch_count_;
        result.stop_value = stop_value_;
        for (const auto& backing : plane_backing_) {
            result.plane_words.push_back(backing.words);
        }
        for (const auto& backing : write_backing_) {
            result.write_values.push_back(backing.values);
        }
        return result;
    }

    void require_unchanged(const FrameSnapshot& before,
        const std::string_view message) const
    {
        require(same_frame(frame_, before.frame), message);
        require(readiness_ == before.ready_words, message);
        require(members_.size() == before.members.size()
                && std::equal(members_.begin(), members_.end(),
                    before.members.begin(), same_member), message);
        require(tasks_.size() == before.tasks.size()
                && std::equal(tasks_.begin(), tasks_.end(),
                    before.tasks.begin(), same_task), message);
        require(planes_.size() == before.planes.size()
                && std::equal(planes_.begin(), planes_.end(),
                    before.planes.begin(), same_plane), message);
        require(fanout_edges_.size() == before.fanout_edges.size()
                && std::equal(fanout_edges_.begin(), fanout_edges_.end(),
                    before.fanout_edges.begin(), same_fanout_edge), message);
        require(port_bindings_ == before.port_bindings, message);
        require(metadata_.size() == before.metadata.size()
                && std::equal(metadata_.begin(), metadata_.end(),
                    before.metadata.begin(), same_metadata), message);
        require(writes_.size() == before.writes.size()
                && std::equal(writes_.begin(), writes_.end(),
                    before.writes.begin(), same_write), message);
        require(events_.size() == before.events.size()
                && std::equal(events_.begin(), events_.end(),
                    before.events.begin(), same_event), message);
        require(committed_signals_.size() == before.committed_signals.size()
                && std::equal(committed_signals_.begin(),
                    committed_signals_.end(), before.committed_signals.begin(),
                    same_committed_signal), message);
        require(plane_backing_.size() == before.plane_words.size()
                && write_backing_.size() == before.write_values.size(), message);
        for (std::size_t index = 0U; index < plane_backing_.size(); ++index) {
            require(plane_backing_[index].words == before.plane_words[index],
                message);
        }
        for (std::size_t index = 0U; index < write_backing_.size(); ++index) {
            require(write_backing_[index].values == before.write_values[index],
                message);
        }
        require(dispatch_count_ == before.dispatch_count
                && stop_value_ == before.stop_value, message);
    }

    void set_root_task(const std::uint64_t stable_order,
        const std::uint64_t sequence)
    {
        std::fill(readiness_.begin(), readiness_.end(), 0U);
        for (auto& member : members_) {
            member.flags = RegionFrontierMemberFlagsV2::waiting_on_static;
            member.static_trigger_mask = Process::full_static_trigger_mask;
            member.queued_key = { };
            member.activation_origin = { };
            member.pending_activation_origin = { };
        }
        const auto root = root_member_index();
        readiness_.at(root / 64U) |= UINT64_C(1) << (root % 64U);
        auto& root_member = members_.at(root);
        root_member.static_trigger_mask = sensitivity_trigger_mask(
            layout_.members[root].process_id, external_input_signal_);
        root_member.flags = RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid;
        const auto root_key = make_key(stable_order, sequence);
        root_member.queued_key = root_key;
        root_member.activation_origin = root_key;
        root_member.pending_activation_origin = root_key;
        tasks_[0U] = RegionFrontierSchedulerTaskV2 {
            stable_order,
            sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::member_activation,
                static_cast<std::uint64_t>(root)),
        };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        frame_.committed_signal_count = 0U;
        std::fill(committed_signals_.begin(), committed_signals_.end(),
            RegionFrontierCommittedSignalV2 { });
        frame_.scheduler_tasks = tasks_.data();
        set_before_key(stable_order + 1U, 0U);
    }

    void set_before_key(const std::uint64_t stable_order,
        const std::uint64_t sequence)
    {
        frame_.cut.kind = RegionFrontierCutKindV2::same_slot_key;
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        frame_.cut.next_key = make_key(stable_order, sequence);
    }

    void set_closed_prefix()
    {
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        frame_.cut.next_key = { };
    }

    void reserve_staged_events()
    {
        const auto count = static_cast<std::size_t>(frame_.staged_event_count);
        require(count != 0U && count <= events_.size()
                && count <= tasks_.size(),
            "staged events fit the test's preallocated reservation storage");
        std::vector<RegionFrontierStagedEventV2> ordered(
            events_.begin(), events_.begin()
                + static_cast<std::vector<RegionFrontierStagedEventV2>::difference_type>(
                    count));
        std::stable_sort(ordered.begin(), ordered.end(),
            [](const auto& left, const auto& right) {
                return left.stable_order < right.stable_order;
            });
        for (std::size_t index = 0U; index < count; ++index) {
            const auto& event = ordered[index];
            const auto kind = static_cast<RegionFrontierEventKindV2>(event.kind);
            require(kind == RegionFrontierEventKindV2::member_activation
                    || kind == RegionFrontierEventKindV2::internal_commit
                    || kind == RegionFrontierEventKindV2::boundary_commit,
                "generated events use the certified kind encoding");
            const auto sequence = next_sequence_++;
            const auto task_key = make_key(event.stable_order, sequence);
            tasks_[index] = RegionFrontierSchedulerTaskV2 {
                event.stable_order,
                sequence,
                encode_region_frontier_payload_v2(kind,
                    event.descriptor_index),
            };
            if (kind == RegionFrontierEventKindV2::member_activation) {
                require(event.descriptor_index < members_.size(),
                    "successor activation references one certified member");
                auto& member = members_[event.descriptor_index];
                member.queued_key = task_key;
                member.activation_origin = task_key;
                member.pending_activation_origin = event.origin;
                member.flags |= RegionFrontierMemberFlagsV2::queued
                    | RegionFrontierMemberFlagsV2::queued_key_valid;
                readiness_.at(event.descriptor_index / 64U)
                    |= UINT64_C(1) << (event.descriptor_index % 64U);
            } else {
                require(event.descriptor_index < writes_.size(),
                    "commit event references one pending output slot");
                auto& write = writes_[event.descriptor_index];
                write.commit_key = task_key;
                write.flags |= RegionFrontierPendingWriteFlagsV2::pending_key_assigned;
            }
        }
        frame_.scheduler_task_count = static_cast<std::uint32_t>(count);
        frame_.scheduler_task_cursor = 0U;
        frame_.scheduler_tasks = tasks_.data();
        frame_.staged_event_count = 0U;
        ++frame_.scheduler_frontier_generation;
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        set_closed_prefix();
    }

    [[nodiscard]] std::vector<RegionFrontierPendingWriteV2>& writes() noexcept
    {
        return writes_;
    }

    void verify_and_drain_internal_commit(const std::size_t signal_slot)
    {
        require(frame_.committed_signal_count == 1U
                && frame_.committed_signal_count <= committed_signals_.size(),
            "one generated internal commit is recorded before host drain");
        const auto& record = committed_signals_.front();
        require(record.signal_slot == signal_slot
                && record.changed == 1U
                && record.state_changed == 1U,
            "the commit log records current and role-state changes");
        // Model the runtime draining bookkeeping before the next generated
        // entry call; the log never replays value publication.
        frame_.committed_signal_count = 0U;
    }

private:
    [[nodiscard]] static RegionFrontierTestValue normalize_value(
        RegionFrontierTestValue value, const std::uint32_t width,
        const ValueKind value_kind, const bool first_bit)
    {
        const auto word_count = words_for(width);
        const auto planes = plane_count_for(value_kind);
        bool has_any_plane = false;
        for (std::size_t index = 0U; index < planes; ++index) {
            has_any_plane = has_any_plane || !value_plane(value, index).empty();
        }
        require(!has_any_plane || value.value_kind == value_kind,
            "provided value planes retain their declared SimIR kind");
        for (std::size_t index = planes; index < 4U; ++index) {
            require(value_plane(value, index).empty(),
                "unused packed-value planes are absent from the fixture");
        }
        if (!has_any_plane) {
            for (std::size_t index = 0U; index < planes; ++index) {
                value_plane(value, index).assign(word_count, 0U);
            }
            if (first_bit) {
                value.aval[0U] = 1U;
            }
        } else {
            for (std::size_t index = 0U; index < planes; ++index) {
                require(value_plane(value, index).size() == word_count,
                    "each test plane has exactly the packed width's word count");
            }
        }
        if (value_kind == ValueKind::logic9) {
            for (std::size_t word = 0U; word < word_count; ++word) {
                require((value.plane3[word]
                            & (value.aval[word] | value.bval[word]
                                | value.plane2[word])) == 0U,
                    "Logic9 test planes exclude reserved codes 9 through 15");
            }
        }
        const auto tail = width % 64U;
        if (tail != 0U) {
            const auto unused_mask = ~((UINT64_C(1) << tail) - 1U);
            for (std::size_t plane = 0U; plane < planes; ++plane) {
                require((value_plane(value, plane).back() & unused_mask) == 0U,
                    "test planes have zero bits beyond their declared width");
            }
        }
        value.value_kind = value_kind;
        return value;
    }

    [[nodiscard]] const RegionFrontierTestValue& external_value(
        const SignalId signal) const
    {
        if (values_.external_inputs.empty()) {
            return values_.external_input;
        }
        const auto found = std::ranges::find_if(values_.external_inputs,
            [signal](const RegionFrontierSignalInput& input) {
                return input.signal == signal;
            });
        require(found != values_.external_inputs.end(),
            "every non-internal kernel input has one explicit frame value");
        return found->value;
    }

    void prepare_staging_values()
    {
        require(kernel_.internal_signals.size() == 1U,
            "the staging fixture has one internal signal");
        internal_signal_ = kernel_.internal_signals.front();
        std::vector<const RegionConeKernelInput*> external_inputs;
        for (const auto& input : kernel_.inputs) {
            if (input.internal) {
                continue;
            }
            require(input.width != 0U,
                "the staging fixture maps nonempty typed boundary inputs");
            if (std::ranges::none_of(external_inputs,
                    [&input](const RegionConeKernelInput* existing) {
                        return existing->signal == input.signal;
                    })) {
                external_inputs.push_back(&input);
            }
        }
        require(!external_inputs.empty(),
            "the staging fixture maps at least one typed boundary input");
        external_input_signal_ = external_inputs.front()->signal;
        if (values_.external_inputs.empty()) {
            require(external_inputs.size() == 1U,
                "multiple boundary inputs supply values by signal id");
            values_.external_input = normalize_value(
                std::move(values_.external_input),
                external_inputs.front()->width,
                external_inputs.front()->value_kind, true);
        } else {
            require(values_.external_inputs.size() == external_inputs.size(),
                "the test frame provides each distinct boundary input once");
            for (auto& provided : values_.external_inputs) {
                const auto found = std::ranges::find_if(external_inputs,
                    [&provided](const RegionConeKernelInput* input) {
                        return input->signal == provided.signal;
                    });
                require(found != external_inputs.end(),
                    "test input values name only kernel boundary inputs");
                provided.value = normalize_value(std::move(provided.value),
                    (*found)->width, (*found)->value_kind, false);
            }
        }

        const RegionConeOutputBinding* boundary_output { };
        for (const auto& output : kernel_.outputs) {
            if (std::find(kernel_.internal_signals.begin(),
                    kernel_.internal_signals.end(), output.signal)
                != kernel_.internal_signals.end()) {
                continue;
            }
            require(boundary_output == nullptr,
                "the staging fixture has one boundary output");
            boundary_output = &output;
        }
        require(boundary_output != nullptr,
            "the staging fixture maps one typed boundary output");
        boundary_output_signal_ = boundary_output->signal;

        const auto internal_slot = internal_plane_index(internal_signal_);
        const auto internal_width = layout_.signals[internal_slot].width;
        const auto boundary_slot = signal_slot_index(boundary_output_signal_);
        const auto boundary_width = layout_.signals[boundary_slot].width;
        const auto derive_internal_from_input
            = values_.expected_internal.aval.empty();
        const auto derive_boundary_from_internal
            = values_.expected_boundary.aval.empty();
        values_.initial_internal = normalize_value(
            std::move(values_.initial_internal), internal_width,
            kernel_.outputs.front().value_kind, false);
        values_.expected_internal = normalize_value(
            std::move(values_.expected_internal), internal_width,
            kernel_.outputs.front().value_kind, false);
        if (derive_internal_from_input
            && values_.external_inputs.empty()
            && external_inputs.front()->width == internal_width) {
            values_.expected_internal = values_.external_input;
        }
        values_.initial_boundary = normalize_value(
            std::move(values_.initial_boundary), boundary_width,
            boundary_output->value_kind, false);
        values_.expected_boundary = normalize_value(
            std::move(values_.expected_boundary), boundary_width,
            boundary_output->value_kind, false);
        if (derive_boundary_from_internal
            && boundary_width == internal_width) {
            values_.expected_boundary = values_.expected_internal;
        }
    }

    [[nodiscard]] std::uint64_t sensitivity_trigger_mask(
        const ProcessId process, const SignalId signal) const
    {
        const auto member = std::ranges::find(kernel_.members, process,
            &RegionConeKernelMember::process);
        require(member != kernel_.members.end(),
            "a fanout descriptor names a certified process member");
        const auto has_sensitivity = std::ranges::any_of(member->sensitivities,
            [signal](const Sensitivity& sensitivity) {
                return sensitivity.signal == signal;
            });
        require(has_sensitivity,
            "each generated fanout edge retains a certified static sensitivity");
        // These fixture processes have no explicit StaticTriggerRegion list,
        // so the runtime's grouped-fanout builder uses its full-trigger bit.
        return Process::full_static_trigger_mask;
    }

    void prepare_fanout_edges()
    {
        std::copy_n(layout_.fanout_edges, layout_.fanout_edge_count,
            fanout_edges_.begin());
        for (auto& edge : fanout_edges_) {
            require(edge.signal_slot < layout_.signal_slot_count
                    && edge.member_index < layout_.member_count,
                "fanout edge references valid signal and member slots");
            const auto signal = layout_.signals[edge.signal_slot].signal_id;
            const auto process = layout_.members[edge.member_index].process_id;
            edge.trigger_mask = sensitivity_trigger_mask(process, signal);
        }
    }

    [[nodiscard]] RegionFrontierKeyV2 make_key(
        const std::uint64_t stable_order,
        const std::uint64_t sequence) const noexcept
    {
        return RegionFrontierKeyV2 {
            frame_.slot.time,
            frame_.slot.delta,
            frame_.slot.systemverilog_round,
            stable_order,
            sequence,
            frame_.slot.process_domain,
            frame_.slot.phase,
        };
    }

    void bind_signal_planes()
    {
        std::vector<SignalId> expected_signals;
        expected_signals.reserve(kernel_.inputs.size() + kernel_.outputs.size());
        for (const auto& input : kernel_.inputs) {
            expected_signals.push_back(input.signal);
        }
        for (const auto& output : kernel_.outputs) {
            expected_signals.push_back(output.signal);
        }
        std::sort(expected_signals.begin(), expected_signals.end());
        expected_signals.erase(std::unique(expected_signals.begin(),
            expected_signals.end()), expected_signals.end());
        require(expected_signals.size() == layout_.signal_slot_count,
            "layout slots exactly cover the unique kernel input/output union");

        std::vector<SignalId> seen_signals;
        seen_signals.reserve(layout_.signal_slot_count);
        std::vector<std::uint8_t> metadata_seen(layout_.metadata_count, 0U);
        std::vector<SignalId> internal_signals = kernel_.internal_signals;
        std::sort(internal_signals.begin(), internal_signals.end());
        internal_signals.erase(std::unique(internal_signals.begin(),
            internal_signals.end()), internal_signals.end());
        require(internal_signals.size() == kernel_.internal_signals.size(),
            "the fixture internal-signal descriptor contains no duplicates");
        std::size_t internal_count { };
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            const auto& descriptor = layout_.signals[index];
            require(descriptor.plane_count
                        == region_frontier_required_plane_count_v2(
                            descriptor.value_kind)
                    && descriptor.width != 0U
                    && descriptor.word_count == words_for(descriptor.width)
                    && std::binary_search(expected_signals.begin(),
                        expected_signals.end(), descriptor.signal_id)
                    && std::find(seen_signals.begin(), seen_signals.end(),
                        descriptor.signal_id) == seen_signals.end(),
                "each canonical union slot has one unique signal and exact shape");
            seen_signals.push_back(descriptor.signal_id);

            const auto internal = (descriptor.flags
                & RegionFrontierPlaneFlagsV2::certified_internal_single_owner) != 0U;
            const auto boundary = (descriptor.flags
                & RegionFrontierPlaneFlagsV2::read_only_boundary_port) != 0U;
            require(internal != boundary,
                "each union slot is either internal mutable state or a read-only boundary");
            require(!internal || std::binary_search(internal_signals.begin(),
                        internal_signals.end(), descriptor.signal_id),
                "every mutable slot is listed among the kernel's internal signals");

            auto& plane = planes_[index];
            auto& words = plane_backing_[index].words;
            const auto count = static_cast<std::size_t>(descriptor.word_count);
            plane.signal_id = descriptor.signal_id;
            plane.owner_process_id = descriptor.owner_process_id;
            plane.value_kind = descriptor.value_kind;
            plane.plane_count = descriptor.plane_count;
            plane.width = descriptor.width;
            plane.word_count = descriptor.word_count;
            plane.flags = descriptor.flags;
            plane.metadata_index = descriptor.metadata_index;

            if (internal) {
                require(descriptor.owner_process_id != UINT32_MAX
                        && descriptor.metadata_index < layout_.metadata_count
                        && metadata_seen[descriptor.metadata_index] == 0U,
                    "internal slots own one dense metadata record");
                metadata_seen[descriptor.metadata_index] = 1U;
                ++internal_count;
                for (std::size_t plane_index = 0U;
                     plane_index < descriptor.plane_count; ++plane_index) {
                    words.current[plane_index].assign(count, 0U);
                    words.previous[plane_index].assign(count, 0U);
                    words.stored[plane_index].assign(count, 0U);
                    words.owner[plane_index].assign(count, 0U);
                }
                if (descriptor.signal_id == internal_signal_) {
                    for (std::size_t plane_index = 0U;
                         plane_index < descriptor.plane_count; ++plane_index) {
                        const auto& initial
                            = value_plane(values_.initial_internal, plane_index);
                        std::ranges::copy(initial,
                            words.current[plane_index].begin());
                        std::ranges::copy(initial,
                            words.previous[plane_index].begin());
                        std::ranges::copy(initial,
                            words.stored[plane_index].begin());
                        std::ranges::copy(initial,
                            words.owner[plane_index].begin());
                    }
                }
                for (std::size_t plane_index = 0U;
                     plane_index < descriptor.plane_count; ++plane_index) {
                    plane.current_planes[plane_index]
                        = words.current[plane_index].data();
                    plane.previous_planes[plane_index]
                        = words.previous[plane_index].data();
                    plane.stored_planes[plane_index]
                        = words.stored[plane_index].data();
                    plane.owner_planes[plane_index]
                        = words.owner[plane_index].data();
                }
            } else {
                // Boundary descriptors expose only the public-current input
                // view; all A4-owned mutable roles stay null.
                require(std::all_of(std::begin(plane.current_planes),
                            std::end(plane.current_planes),
                            [](const auto* pointer) { return pointer == nullptr; })
                        && std::all_of(std::begin(plane.previous_planes),
                            std::end(plane.previous_planes),
                            [](const auto* pointer) { return pointer == nullptr; })
                        && std::all_of(std::begin(plane.stored_planes),
                            std::end(plane.stored_planes),
                            [](const auto* pointer) { return pointer == nullptr; })
                        && std::all_of(std::begin(plane.owner_planes),
                            std::end(plane.owner_planes),
                            [](const auto* pointer) { return pointer == nullptr; }),
                    "boundary slots carry no mutable current/LAST/owner storage");
                for (std::size_t plane_index = 0U;
                     plane_index < descriptor.plane_count; ++plane_index) {
                    words.boundary[plane_index].assign(count, 0U);
                    plane.boundary_planes[plane_index]
                        = words.boundary[plane_index].data();
                }
            }
            require(region_frontier_plane_bindings_valid_v2(plane),
                "fixture backing satisfies the typed plane contract");
            port_bindings_[index] = &plane;
        }
        require(internal_count == kernel_.internal_signals.size()
                && internal_count == layout_.metadata_count
                && std::all_of(metadata_seen.begin(), metadata_seen.end(),
                    [](const std::uint8_t seen) { return seen != 0U; }),
            "every internal signal has exactly one dense metadata slot");

        for (const auto signal : internal_signals) {
            const auto slot = signal_slot_index(signal);
            require((layout_.signals[slot].flags
                        & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                    != 0U,
                "kernel internal signals map only to mutable internal slots");
        }

        std::size_t external_input_count { };
        for (const auto& input : kernel_.inputs) {
            require(input.width != 0U,
                "the frontier staging witness uses nonempty typed inputs");
            const auto index = signal_slot_index(input.signal);
            const auto internal = (planes_[index].flags
                & RegionFrontierPlaneFlagsV2::certified_internal_single_owner) != 0U;
            require(input.internal == internal
                    && planes_[index].width == input.width
                    && planes_[index].value_kind
                        == frontier_kind_for(input.value_kind),
                "each body input maps to the exact internal or public-current slot");
            if (input.internal) {
                continue;
            }
            auto& words = plane_backing_[index].words;
            const auto& value = external_value(input.signal);
            for (std::size_t plane_index = 0U;
                 plane_index < planes_[index].plane_count; ++plane_index) {
                std::ranges::copy(value_plane(value, plane_index),
                    words.boundary[plane_index].begin());
            }
            ++external_input_count;
        }
        require(external_input_count != 0U,
            "each producer or consumer read has boundary backing");
        const auto boundary_slot = signal_slot_index(boundary_output_signal_);
        auto& boundary_output = plane_backing_[boundary_slot].words;
        for (std::size_t plane_index = 0U;
             plane_index < planes_[boundary_slot].plane_count; ++plane_index) {
            std::ranges::copy(
                value_plane(values_.initial_boundary, plane_index),
                boundary_output.boundary[plane_index].begin());
        }
    }

    void bind_write_sites()
    {
        require(layout_.pending_write_capacity >= layout_.write_site_count,
            "each static write site has a unique pending output slot");
        std::vector<bool> output_seen(kernel_.outputs.size(), false);
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            const auto& site = layout_.write_sites[index];
            require(site.signal_slot < layout_.signal_slot_count,
                "write site signal slot is in the immutable layout");
            const auto& signal = layout_.signals[site.signal_slot];
            require(site.pending_slot < writes_.size()
                    && site.member_index < layout_.member_count
                    && site.signal_slot < layout_.signal_slot_count
                    && site.plane_count
                        == region_frontier_required_plane_count_v2(
                            site.value_kind)
                    && site.width != 0U
                    && site.word_count == words_for(site.width)
                    && site.value_kind == signal.value_kind
                    && site.plane_count == signal.plane_count,
                "each write site has a valid pending slot and exact shape");
            const auto internal = (signal.flags
                & RegionFrontierPlaneFlagsV2::certified_internal_single_owner) != 0U;
            const auto expected_event = internal
                ? RegionFrontierEventKindV2::internal_commit
                : RegionFrontierEventKindV2::boundary_commit;
            const auto owner = layout_.members[site.member_index].process_id;
            auto output_index = kernel_.outputs.size();
            for (std::size_t candidate_index = 0U;
                 candidate_index < kernel_.outputs.size(); ++candidate_index) {
                const auto& candidate = kernel_.outputs[candidate_index];
                if (!output_seen[candidate_index]
                    && candidate.owner == owner
                    && signal.signal_id == candidate.signal
                    && site.source_instruction == candidate.source_instruction
                    && site.width == candidate.width
                    && site.value_kind
                        == frontier_kind_for(candidate.value_kind)
                    && site.update_kind
                        == static_cast<std::uint32_t>(candidate.update_kind)) {
                    output_index = candidate_index;
                    break;
                }
            }
            require(output_index < kernel_.outputs.size()
                    && site.event_kind == static_cast<std::uint32_t>(expected_event),
                "each site preserves its source owner and target commit class");
            output_seen[output_index] = true;
            auto& write = writes_[site.pending_slot];
            require(write.value_planes[0U] == nullptr,
                "the test shape assigns a distinct pending slot per write site");
            auto& backing = write_backing_[site.pending_slot];
            for (std::size_t plane_index = 0U;
                 plane_index < site.plane_count; ++plane_index) {
                backing.values[plane_index].assign(site.word_count, 0U);
            }
            write.member_index = site.member_index;
            write.signal_slot = site.signal_slot;
            write.source_instruction = site.source_instruction;
            write.update_kind = site.update_kind;
            write.value_kind = site.value_kind;
            write.plane_count = site.plane_count;
            write.width = site.width;
            write.word_count = site.word_count;
            for (std::size_t plane_index = 0U;
                 plane_index < site.plane_count; ++plane_index) {
                write.value_planes[plane_index]
                    = backing.values[plane_index].data();
            }
            require(region_frontier_pending_write_bindings_valid_v2(write),
                "fixture backing satisfies the pending typed plane contract");
        }
        require(std::all_of(output_seen.begin(), output_seen.end(),
                    [](const bool seen) { return seen; }),
            "every kernel output is represented by one immutable write site");
    }

    void initialize_frame(const std::uint64_t runtime_generation)
    {
        frame_.abi_version = kRegionFrontierAbiVersionV2;
        frame_.value_plane_contract = kRegionFrontierValuePlaneContractV2;
        frame_.struct_size = sizeof(RegionFrontierFrameV2);
        frame_.runtime_generation = runtime_generation;
        frame_.bound_runtime_generation = runtime_generation;
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
        frame_.staged_event_capacity = layout_.staged_event_capacity;
        frame_.current_member = UINT32_MAX;
        frame_.current_pending_write = UINT32_MAX;
        frame_.ready_words = readiness_.data();
        frame_.members = members_.data();
        frame_.scheduler_tasks = tasks_.data();
        frame_.planes = planes_.data();
        frame_.metadata = metadata_.data();
        frame_.fanout_edges = fanout_edges_.data();
        frame_.port_planes = port_bindings_.data();
        frame_.pending_writes = writes_.data();
        frame_.staged_events = events_.data();
        frame_.committed_signals = committed_signals_.data();
        frame_.native_frontier_member_dispatches = &dispatch_count_;
        frame_.stop_requested = &stop_value_;
        frame_.slot = RegionFrontierSlotV2 {
            23U, 0U, 0U, 1U, 0U,
        };
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        for (std::size_t index = 0U; index < members_.size(); ++index) {
            members_[index].process_id = layout_.members[index].process_id;
            members_[index].flags
                = RegionFrontierMemberFlagsV2::waiting_on_static;
            members_[index].static_trigger_mask
                = Process::full_static_trigger_mask;
        }
    }

    static bool same_frame(const RegionFrontierFrameV2& left,
        const RegionFrontierFrameV2& right) noexcept
    {
        return left.abi_version == right.abi_version
            && left.struct_size == right.struct_size
            && left.value_plane_contract == right.value_plane_contract
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
            && left.committed_signal_capacity
                == right.committed_signal_capacity
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
            && same_slot(left.slot, right.slot)
            && same_cut(left.cut, right.cut);
    }

    const RegionConeActivationKernel& kernel_;
    const RegionFrontierLayoutV2& layout_;
    RegionFrontierStagingValues values_;
    SignalId external_input_signal_ { };
    SignalId internal_signal_ { };
    SignalId boundary_output_signal_ { };
    std::vector<std::uint64_t> readiness_;
    std::vector<RegionFrontierMemberV2> members_;
    std::vector<RegionFrontierSchedulerTaskV2> tasks_;
    std::vector<RegionFrontierPlaneV2> planes_;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_edges_;
    std::vector<PlaneBacking> plane_backing_;
    std::vector<const RegionFrontierPlaneV2*> port_bindings_;
    std::vector<RegionFrontierSignalMetadataV2> metadata_;
    std::vector<RegionFrontierPendingWriteV2> writes_;
    std::vector<WriteBacking> write_backing_;
    std::vector<RegionFrontierStagedEventV2> events_;
    std::vector<RegionFrontierCommittedSignalV2> committed_signals_;
    std::uint64_t dispatch_count_ { };
    std::uint32_t stop_value_ { };
    std::uint64_t next_sequence_ { 1000U };
    RegionFrontierFrameV2 frame_ { };
};

void require_internal_commit(const FrameStorage& storage,
    const std::size_t plane_index,
    const RegionFrontierStagingValues& values)
{
    for (std::size_t plane = 0U;
         plane < storage.value_plane_count(plane_index); ++plane) {
        const auto expected = value_plane(values.expected_internal, plane);
        const auto initial = value_plane(values.initial_internal, plane);
        require(std::ranges::equal(storage.role_plane(plane_index, plane, 0U),
                    expected),
            "the internal commit updates its current plane");
        require(std::ranges::equal(storage.role_plane(plane_index, plane, 1U),
                    initial),
            "the internal commit retains the previous plane");
        require(std::ranges::equal(storage.role_plane(plane_index, plane, 2U),
                    expected),
            "the internal commit updates its stored-value plane");
        require(std::ranges::equal(storage.role_plane(plane_index, plane, 3U),
                    expected),
            "the internal commit updates its original-owner plane");
    }
}

void exercise_staged_continuation(const RegionFrontierStepEntryV2 entry,
    FrameStorage& storage,
    const std::size_t root_member,
    const std::size_t consumer_member,
    const std::size_t internal_plane,
    const RegionFrontierStagingValues& values)
{
    auto& frame = storage.frame();
    require(entry(&frame) == RegionFrontierStatusV2::need_scheduler_keys,
        "producer execution returns staged scheduler work");
    require(storage.dispatch_count() == 1U && frame.pending_write_count == 1U
            && frame.staged_event_count != 0U,
        "producer body runs once and retains its private output and event");

    const auto retry_before = storage.snapshot();
    require(entry(&frame) == RegionFrontierStatusV2::need_scheduler_keys,
        "a retained staged prefix asks for scheduler keys idempotently");
    storage.require_unchanged(retry_before,
        "need-keys re-entry preserves body result, task cursor, and all planes");
    require(storage.dispatch_count() == 1U,
        "need-keys re-entry does not replay the producer");

    bool saw_boundary { };
    bool saw_internal_commit { };
    for (std::size_t turn = 0U; turn < 8U; ++turn) {
        if (frame.staged_event_count != 0U) {
            storage.reserve_staged_events();
        }
        const auto status = entry(&frame);
        if (frame.committed_signal_count != 0U) {
            storage.verify_and_drain_internal_commit(internal_plane);
            saw_internal_commit = true;
        }
        if (status == RegionFrontierStatusV2::need_scheduler_keys) {
            require(frame.staged_event_count != 0U,
                "fresh scheduler reservation is requested only for staged events");
            continue;
        }
        if (status == RegionFrontierStatusV2::boundary_publication) {
            const auto cursor = frame.scheduler_task_cursor;
            require(cursor < frame.scheduler_task_count,
                "boundary task remains at the current scheduler cursor");
            const auto task = frame.scheduler_tasks[cursor];
            const auto kind = static_cast<std::uint32_t>(
                task.payload >> kRegionFrontierPayloadKindShiftV2);
            const auto pending_index = static_cast<std::uint32_t>(
                task.payload & kRegionFrontierPayloadIndexMaskV2);
            require(kind == static_cast<std::uint32_t>(
                        RegionFrontierEventKindV2::boundary_commit)
                    && pending_index < frame.pending_write_capacity,
                "boundary task binds its original pending publication record");
            require(frame.current_pending_write == pending_index,
                "boundary publication exposes its selected pending slot to the host");
            auto& write = storage.writes()[pending_index];
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
                "boundary output remains private until publication is acknowledged");
            for (std::size_t plane = 0U; plane < write.plane_count; ++plane) {
                require(std::ranges::equal(storage.pending_plane(
                            pending_index, plane),
                            value_plane(values.expected_boundary, plane)),
                    "consumer result remains in every staged output plane");
            }
            const auto boundary_slot = static_cast<std::size_t>(write.signal_slot);
            for (std::size_t plane = 0U; plane < write.plane_count; ++plane) {
                require(std::ranges::equal(storage.boundary_plane(
                            boundary_slot, plane),
                            value_plane(values.initial_boundary, plane)),
                    "boundary current remains unchanged before host publication");
            }
            require(frame.scheduler_task_cursor == cursor,
                "the generated entry leaves boundary task advancement to ack");

            for (std::size_t plane = 0U; plane < write.plane_count; ++plane) {
                std::ranges::copy(value_plane(values.expected_boundary, plane),
                    storage.boundary_plane(boundary_slot, plane).begin());
            }
            write.flags |= RegionFrontierPendingWriteFlagsV2::pending_committed;
            const auto member_calls = storage.dispatch_count();
            const auto after_ack = entry(&frame);
            require(after_ack == RegionFrontierStatusV2::quiescent
                    && frame.scheduler_task_cursor == cursor + 1U
                    && frame.current_pending_write == UINT32_MAX
                    && frame.committed_signal_count == 0U,
                "host ack resumes at the same task, clears its pending slot, "
                "and retires it exactly once");
            require((write.flags
                        & RegionFrontierPendingWriteFlagsV2::pending_active) == 0U
                    && storage.dispatch_count() == member_calls
                    && [&] {
                        for (std::size_t plane = 0U;
                             plane < write.plane_count; ++plane) {
                            if (!std::ranges::equal(
                                    storage.boundary_plane(boundary_slot, plane),
                                    value_plane(values.expected_boundary, plane))) {
                                return false;
                            }
                        }
                        return true;
                    }(),
                "boundary ack publishes once, clears the row, and avoids body replay");
            saw_boundary = true;
            break;
        }
        if (status == RegionFrontierStatusV2::quiescent) {
            break;
        }
        throw std::runtime_error {
            "fixture must progress through staged work to boundary publication"
        };
    }
    require(saw_boundary,
        "producer-to-consumer execution reaches the boundary ack protocol");
    require(saw_internal_commit,
        "the native log reports the internal publication transaction");
    require(storage.dispatch_count() == 2U,
        "producer and consumer bodies each execute exactly once");
    require_internal_commit(storage, internal_plane, values);
    require(frame.members[root_member].process_id
                != frame.members[consumer_member].process_id,
        "the two bodies retain distinct original process identities");
}

} // namespace

void run_region_frontier_staging_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    RegionFrontierStagingValues values)
{
    require(entry != nullptr,
        "test fixture receives the materialized generated entry");
    require(runtime_generation != 0U,
        "frontier runtime generation is bound");
    FrameStorage storage { kernel, layout, runtime_generation, values };
    auto& frame = storage.frame();
    const auto root_member = storage.root_member_index();
    const auto consumer_member = storage.consumer_member_index();
    const auto internal_plane = storage.internal_plane_index(
        kernel.internal_signals.front());

    storage.set_root_task(10U, 100U);
    storage.set_before_key(9U, 999U);
    const auto before_cut = storage.snapshot();
    require(entry(&frame) == RegionFrontierStatusV2::cut_before_key,
        "the generated selector yields before an earlier foreign key");
    storage.require_unchanged(before_cut,
        "cut-before-key leaves readiness, pending values, roles, stamps, and cursors untouched");

    storage.set_before_key(11U, 1U);
    const auto before_body = storage.snapshot();
    require(entry(&frame) == RegionFrontierStatusV2::need_scheduler_keys,
        "the generated producer stages work at its authenticated original key");
    require(storage.dispatch_count() == 1U
            && frame.pending_write_count == 1U
            && frame.staged_event_count != 0U,
        "the selected root body runs once and keeps its pending write");
    bool internal_roles_unchanged = true;
    for (std::size_t plane = 0U;
         plane < storage.value_plane_count(internal_plane); ++plane) {
        const auto current = storage.role_plane(internal_plane, plane, 0U);
        internal_roles_unchanged = internal_roles_unchanged
            && std::ranges::equal(current,
                storage.role_plane(internal_plane, plane, 1U))
            && std::ranges::equal(current,
                storage.role_plane(internal_plane, plane, 2U))
            && std::ranges::equal(current,
                storage.role_plane(internal_plane, plane, 3U));
    }
    require(internal_roles_unchanged,
        "member evaluation does not publish into current, previous, stored, or owner");
    require(std::all_of(frame.metadata,
                frame.metadata + frame.metadata_count,
                [](const RegionFrontierSignalMetadataV2& item) {
                    return item.value_revision == 0U
                        && item.event_valid == 0U
                        && item.transaction_valid == 0U;
                }),
        "member evaluation does not publish transaction or event metadata");
    require(before_body.dispatch_count == 0U,
        "the newly bound frame begins without member executions");

    exercise_staged_continuation(entry, storage, root_member,
        consumer_member, internal_plane, storage.values());

    FrameStorage stale { kernel, layout, runtime_generation, values };
    stale.set_root_task(20U, 200U);
    stale.frame().bound_runtime_generation += 1U;
    const auto stale_before = stale.snapshot();
    require(entry(&stale.frame()) == RegionFrontierStatusV2::stale_generation,
        "a stale runtime generation is rejected before native mutation");
    stale.require_unchanged(stale_before,
        "stale-generation decline preserves every bound frame and plane");

    FrameStorage stopped { kernel, layout, runtime_generation, values };
    stopped.set_root_task(30U, 300U);
    stopped.stop_value() = 1U;
    const auto stopped_before = stopped.snapshot();
    require(entry(&stopped.frame()) == RegionFrontierStatusV2::stopped,
        "a stop at the original callback cut leaves the member unconsumed");
    stopped.require_unchanged(stopped_before,
        "stop-before-member preserves the ready prefix and signal state");

    const auto external_slot = storage.external_input_plane_index();
    if (layout.signals[external_slot].value_kind
        == RegionFrontierValueKindV2::logic9) {
        FrameStorage malformed { kernel, layout, runtime_generation, values };
        malformed.set_root_task(40U, 400U);
        malformed.set_before_key(41U, 0U);
        const auto input_slot = malformed.external_input_plane_index();
        malformed.boundary_plane(input_slot, 0U)[0U] |= UINT64_C(1);
        malformed.boundary_plane(input_slot, 3U)[0U] |= UINT64_C(1);
        const auto malformed_before = malformed.snapshot();
        require(entry(&malformed.frame())
                    == RegionFrontierStatusV2::decline_before_mutation,
            "reserved Logic9 code 9 in external planes declines before entry stores");
        malformed.require_unchanged(malformed_before,
            "Logic9 canonical-code decline preserves all four input planes and frame state");
    }
}

void run_region_frontier_canonical_tail_guard_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    RegionFrontierStagingValues values)
{
    require(entry != nullptr && runtime_generation != 0U,
        "canonical-tail checks use a live generated entry");
    require(layout.execution_mode
            == RegionFrontierExecutionModeV2::systemverilog_active,
        "canonical-tail staging coverage uses an SV internal-role frame");

    FrameStorage accepted { kernel, layout, runtime_generation, values };
    accepted.set_root_task(80U, 800U);
    require(entry(&accepted.frame())
                == RegionFrontierStatusV2::need_scheduler_keys,
        "the unmodified partial-width frame reaches its accepted stage");

    std::size_t internal_cases { };
    std::size_t boundary_cases { };
    std::size_t pending_cases { };
    const auto role_names = std::array<std::string_view, 4U> {
        "current", "previous", "stored", "owner",
    };
    const auto check_decline = [&](const std::string& message,
                                   const auto& mutate) {
        FrameStorage malformed { kernel, layout, runtime_generation, values };
        malformed.set_root_task(80U, 800U);
        mutate(malformed);
        const auto before = malformed.snapshot();
        require(entry(&malformed.frame())
                == RegionFrontierStatusV2::decline_before_mutation,
            message);
        malformed.require_unchanged(before, message);
    };

    for (std::size_t signal_slot = 0U;
         signal_slot < layout.signal_slot_count; ++signal_slot) {
        const auto& signal = layout.signals[signal_slot];
        if (signal.width % 64U == 0U) {
            continue;
        }
        const auto tail_bit = UINT64_C(1) << (signal.width % 64U);
        const auto logic_name = signal.value_kind
                == RegionFrontierValueKindV2::logic9
            ? std::string_view { "Logic9" }
            : std::string_view { "Logic4" };
        const bool internal
            = (signal.flags
                & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
            != 0U;
        const bool boundary
            = (signal.flags
                & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
            != 0U;
        require(internal != boundary,
            "each canonical tail descriptor has one storage class");
        if (internal) {
            for (std::size_t role = 0U; role < role_names.size(); ++role) {
                for (std::size_t plane = 0U; plane < signal.plane_count; ++plane) {
                    const auto message = std::string { logic_name }
                        + " " + std::string { role_names[role] }
                        + " role plane " + std::to_string(plane)
                        + " rejects nonzero width-tail bits";
                    check_decline(message,
                        [signal_slot, role, plane, tail_bit](FrameStorage& storage) {
                            auto words = storage.mutable_role_plane(
                                signal_slot, plane, role);
                            words.back() |= tail_bit;
                        });
                    ++internal_cases;
                }
            }
        } else {
            for (std::size_t plane = 0U; plane < signal.plane_count; ++plane) {
                const auto message = std::string { logic_name }
                    + " boundary plane " + std::to_string(plane)
                    + " rejects nonzero width-tail bits";
                check_decline(message,
                    [signal_slot, plane, tail_bit](FrameStorage& storage) {
                        auto words = storage.boundary_plane(signal_slot, plane);
                        words.back() |= tail_bit;
                    });
                ++boundary_cases;
            }
        }
    }

    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        if (site.width % 64U == 0U) {
            continue;
        }
        const auto tail_bit = UINT64_C(1) << (site.width % 64U);
        for (std::size_t plane = 0U; plane < site.plane_count; ++plane) {
            const auto message = std::string {
                site.value_kind == RegionFrontierValueKindV2::logic9
                    ? "Logic9 pending plane " : "Logic4 pending plane " }
                + std::to_string(plane) + " at site "
                + std::to_string(site_index)
                + " rejects nonzero width-tail bits";
            check_decline(message,
                [pending_slot = site.pending_slot, plane, tail_bit](
                    FrameStorage& storage) {
                    auto words = storage.mutable_pending_plane(
                        pending_slot, plane);
                    words.back() |= tail_bit;
                });
            ++pending_cases;
        }
    }

    require(internal_cases != 0U && boundary_cases != 0U
            && pending_cases != 0U,
        "partial-width fixture covers internal roles, boundary planes, and pending sites");
}

void run_region_frontier_sensitivity_fanout_witness(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    RegionFrontierStagingValues values,
    const std::size_t expected_activation_events)
{
    require(entry != nullptr,
        "sensitivity fanout witness receives a generated entry");
    require(expected_activation_events <= 1U,
        "the fixture has one deduplicated reader edge for this signal");
    FrameStorage storage { kernel, layout, runtime_generation,
        std::move(values) };
    auto& frame = storage.frame();
    const auto root_member = storage.root_member_index();
    const auto consumer_member = storage.consumer_member_index();
    const auto internal_slot = storage.internal_plane_index(
        kernel.internal_signals.front());
    const auto consumer_edge = std::ranges::find_if(
        std::span { frame.fanout_edges, frame.fanout_edge_count },
        [consumer_member, internal_slot](
            const RegionFrontierFanoutEdgeV2& edge) {
            return edge.member_index == consumer_member
                && edge.signal_slot == internal_slot;
        });
    require(consumer_edge
                != std::span { frame.fanout_edges,
                    frame.fanout_edge_count }.end()
            && consumer_edge->trigger_mask != 0U,
        "the reader edge retains its runtime-provided nonzero trigger mask");

    storage.set_root_task(10U, 100U);
    storage.set_before_key(11U, 1U);
    frame.members[consumer_member].static_trigger_mask = UINT64_C(1);
    const auto consumer_mask_before
        = frame.members[consumer_member].static_trigger_mask;
    const auto consumer_edge_mask = consumer_edge->trigger_mask;
    require(entry(&frame) == RegionFrontierStatusV2::need_scheduler_keys,
        "the selected producer stages its private write and commit task");
    require(storage.dispatch_count() == 1U
            && frame.pending_write_count == 1U
            && frame.staged_event_count == 1U,
        "the producer executes once and exposes one staged private commit");

    storage.reserve_staged_events();
    const auto status = entry(&frame);
    require(frame.committed_signal_count == 1U,
        "the generated commit records a full-signal internal transaction");
    storage.verify_and_drain_internal_commit(internal_slot);
    require(frame.staged_event_count == expected_activation_events,
        "the selected-range comparison stages exactly its expected reader events");
    require(status == (expected_activation_events == 0U
                ? RegionFrontierStatusV2::quiescent
                : RegionFrontierStatusV2::need_scheduler_keys),
        "no changed selected range quiesces while a changed range requests keys");
    require(storage.dispatch_count() == 1U,
        "fanout comparison does not execute the consumer before key reservation");
    require_internal_commit(storage, internal_slot, storage.values());

    const auto& internal_signal = layout.signals[internal_slot];
    const auto& metadata = frame.metadata[internal_signal.metadata_index];
    require(metadata.value_revision == 1U
            && metadata.event_valid != 0U
            && metadata.transaction_valid != 0U,
        "a partial-range miss still commits complete role and transaction state");

    const auto consumer_flags = frame.members[consumer_member].flags;
    const auto has_flag = [consumer_flags](const std::uint32_t flag) {
        return (consumer_flags & flag) != 0U;
    };
    if (expected_activation_events == 0U) {
        require(has_flag(RegionFrontierMemberFlagsV2::waiting_on_static)
                && !has_flag(RegionFrontierMemberFlagsV2::queued)
                && !has_flag(RegionFrontierMemberFlagsV2::pending_activation)
                && frame.members[consumer_member].static_trigger_mask
                    == consumer_mask_before,
            "an unrelated-bit change leaves its reader waiting and unqueued");
    } else {
        const auto& event = frame.staged_events[0U];
        require(event.kind == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::member_activation)
                && event.descriptor_index == consumer_member
                && has_flag(RegionFrontierMemberFlagsV2::pending_activation)
                && frame.members[consumer_member].static_trigger_mask
                    == (consumer_mask_before | consumer_edge_mask),
            "a changed selected range stages its original reader exactly once");
    }
    require(frame.members[root_member].process_id
                == kernel.members.front().process,
        "the full-value producer keeps its original member identity");
}

RegionFrontierCacheBindingResult run_cache_input_binding_witness(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    RegionFrontierTestValue external_input,
    RegionFrontierTestValue initial_internal)
{
    require(entry != nullptr,
        "cache binding witness receives a materialized generated entry");
    require(kernel.inputs.size() == 2U
            && kernel.inputs[0U].value_kind == ValueKind::logic4
            && kernel.inputs[1U].value_kind == ValueKind::logic4
            && kernel.inputs[0U].width == kernel.inputs[1U].width,
        "cache binding collision uses two equal-width Logic4 input bindings");
    RegionFrontierStagingValues values;
    values.external_input = std::move(external_input);
    values.initial_internal = std::move(initial_internal);
    FrameStorage storage { kernel, layout, runtime_generation,
        std::move(values) };
    storage.set_root_task(10U, 100U);
    storage.set_before_key(11U, 1U);
    auto& frame = storage.mutable_frame();
    const auto status = entry(&frame);
    require(status == RegionFrontierStatusV2::need_scheduler_keys,
        "the cache binding probe executes one root member and stages its write");
    require(frame.pending_write_count == 1U,
        "the root has one staged internal value for cache comparison");
    const auto root_member = storage.root_member_index();
    std::size_t pending_index = layout.pending_write_capacity;
    for (std::size_t index = 0U; index < layout.pending_write_capacity; ++index) {
        const auto& write = frame.pending_writes[index];
        if ((write.flags & RegionFrontierPendingWriteFlagsV2::pending_active)
                != 0U
            && write.member_index == root_member
            && (write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_internal_target)
                != 0U) {
            pending_index = index;
            break;
        }
    }
    require(pending_index < layout.pending_write_capacity,
        "the root pending record targets its original internal signal");
    const auto pending_aval = storage.pending_aval(pending_index);
    const auto pending_bval = storage.pending_bval(pending_index);
    return RegionFrontierCacheBindingResult {
        status,
        { pending_aval.begin(), pending_aval.end() },
        { pending_bval.begin(), pending_bval.end() },
        storage.dispatch_count(),
    };
}

} // namespace fsim::compiler::test

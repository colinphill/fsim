// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_lifecycle_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using namespace runtime::simir;

constexpr std::uint64_t full_static_trigger_mask = UINT64_C(1) << 63U;
constexpr std::uint64_t first_task_stable_order = 17U;
constexpr std::uint64_t first_task_sequence = 3U;
constexpr std::uint32_t declared_task_capacity = 32U;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] std::size_t words_for(const std::uint32_t width) noexcept
{
    return (static_cast<std::size_t>(width) + 63U) / 64U;
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

[[nodiscard]] bool same_slot(const RegionFrontierSlotV2& left,
    const RegionFrontierSlotV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_cut(const RegionFrontierCutV2& left,
    const RegionFrontierCutV2& right) noexcept
{
    return left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && same_key(left.next_key, right.next_key)
        && left.kind == right.kind
        && std::equal(std::begin(left.reserved), std::end(left.reserved),
            std::begin(right.reserved));
}

[[nodiscard]] bool same_frame(const RegionFrontierFrameV2& left,
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
        && same_slot(left.slot, right.slot)
        && same_cut(left.cut, right.cut);
}

[[nodiscard]] bool same_member(const RegionFrontierMemberV2& left,
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

[[nodiscard]] bool same_task(const RegionFrontierSchedulerTaskV2& left,
    const RegionFrontierSchedulerTaskV2& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.payload == right.payload;
}

[[nodiscard]] bool same_plane(const RegionFrontierPlaneV2& left,
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

[[nodiscard]] bool same_metadata(const RegionFrontierSignalMetadataV2& left,
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
        && left.transaction_valid == right.transaction_valid
        && std::equal(std::begin(left.reserved), std::end(left.reserved),
            std::begin(right.reserved));
}

[[nodiscard]] bool same_write(const RegionFrontierPendingWriteV2& left,
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
        && left.width == right.width
        && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && std::equal(std::begin(left.value_planes),
            std::end(left.value_planes),
            std::begin(right.value_planes));
}

[[nodiscard]] bool same_event(const RegionFrontierStagedEventV2& left,
    const RegionFrontierStagedEventV2& right) noexcept
{
    return left.kind == right.kind
        && left.descriptor_index == right.descriptor_index
        && left.stable_order == right.stable_order
        && same_key(left.origin, right.origin);
}

[[nodiscard]] bool same_edge(const RegionFrontierFanoutEdgeV2& left,
    const RegionFrontierFanoutEdgeV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.member_index == right.member_index
        && left.trigger_mask == right.trigger_mask;
}

[[nodiscard]] bool same_committed_signal(
    const RegionFrontierCommittedSignalV2& left,
    const RegionFrontierCommittedSignalV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.changed == right.changed
        && left.state_changed == right.state_changed;
}

template<typename T, typename Predicate>
[[nodiscard]] bool same_records(const std::vector<T>& left,
    const std::vector<T>& right, Predicate predicate)
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), predicate);
}

struct PlaneStorage {
    std::array<std::vector<std::uint64_t>, 10U> roles;

    bool operator==(const PlaneStorage&) const = default;
};

struct WriteStorage {
    std::vector<std::uint64_t> aval;
    std::vector<std::uint64_t> bval;

    bool operator==(const WriteStorage&) const = default;
};

struct FrameSnapshot {
    RegionFrontierFrameV2 frame;
    std::vector<std::uint64_t> readiness;
    std::vector<RegionFrontierMemberV2> members;
    std::vector<RegionFrontierSchedulerTaskV2> tasks;
    std::vector<RegionFrontierPlaneV2> planes;
    std::vector<RegionFrontierSignalMetadataV2> metadata;
    std::vector<RegionFrontierFanoutEdgeV2> fanout;
    std::vector<const RegionFrontierPlaneV2*> port_planes;
    std::vector<RegionFrontierPendingWriteV2> writes;
    std::vector<RegionFrontierStagedEventV2> events;
    std::vector<RegionFrontierCommittedSignalV2> committed;
    std::vector<PlaneStorage> plane_storage;
    std::vector<WriteStorage> write_storage;
    std::uint64_t dispatch_count { };
    std::uint32_t stop_value { };
};
class FrameStorage final {
public:
    FrameStorage(const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout,
        const std::uint64_t runtime_generation)
        : kernel_ { kernel }
        , layout_ { layout }
        , readiness_ (static_cast<std::size_t>(layout.readiness_word_count) + 1U)
        , members_ (static_cast<std::size_t>(layout.member_count) + 1U)
        , tasks_ (static_cast<std::size_t>(declared_task_capacity) * 2U)
        , planes_ (static_cast<std::size_t>(layout.signal_slot_count) + 1U)
        , plane_storage_ (static_cast<std::size_t>(layout.signal_slot_count) + 1U)
        , metadata_ (static_cast<std::size_t>(layout.metadata_count) + 1U)
        , fanout_ (static_cast<std::size_t>(layout.fanout_edge_count) + 1U)
        , port_planes_ (static_cast<std::size_t>(layout.signal_slot_count) + 1U)
        , writes_ (static_cast<std::size_t>(layout.pending_write_capacity) + 1U)
        , write_storage_ (
              static_cast<std::size_t>(layout.pending_write_capacity) + 1U)
        , events_ (static_cast<std::size_t>(layout.staged_event_capacity) + 1U)
        , committed_ (
              static_cast<std::size_t>(layout.committed_signal_capacity) + 2U)
    {
        require(entry_shape_is_usable(),
            "the generated layout supplies preallocated descriptor storage");
        bind_signal_planes();
        bind_fanout();
        bind_write_sites();
        initialize_frame(runtime_generation);
    }

    [[nodiscard]] RegionFrontierFrameV2& frame() noexcept
    {
        return frame_;
    }

    [[nodiscard]] std::vector<RegionFrontierMemberV2>& members() noexcept
    {
        return members_;
    }

    [[nodiscard]] std::vector<RegionFrontierPlaneV2>& planes() noexcept
    {
        return planes_;
    }

    [[nodiscard]] std::vector<RegionFrontierFanoutEdgeV2>& fanout() noexcept
    {
        return fanout_;
    }

    [[nodiscard]] std::vector<const RegionFrontierPlaneV2*>& port_planes() noexcept
    {
        return port_planes_;
    }

    [[nodiscard]] std::vector<RegionFrontierPendingWriteV2>& writes() noexcept
    {
        return writes_;
    }

    [[nodiscard]] std::vector<RegionFrontierCommittedSignalV2>& committed() noexcept
    {
        return committed_;
    }

    [[nodiscard]] std::size_t root_member_index() const
    {
        const auto root_process = kernel_.members.front().process;
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            if (layout_.members[index].process_id == root_process) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture root maps to one compiled local member"
        };
    }

    [[nodiscard]] std::size_t member_index_for_process(
        const ProcessId process) const
    {
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            if (layout_.members[index].process_id == process) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture process maps to one compiled local member"
        };
    }

    [[nodiscard]] RegionFrontierKeyV2 key(
        const std::uint64_t stable_order,
        const std::uint64_t sequence) const noexcept
    {
        return make_key(stable_order, sequence);
    }

    void set_task(const std::size_t index,
        const RegionFrontierSchedulerTaskV2 task)
    {
        tasks_.at(index) = task;
    }

    void set_ready(const std::size_t member_index)
    {
        readiness_.at(member_index / 64U)
            |= UINT64_C(1) << (member_index % 64U);
    }

    void seal_scheduler_prefix() noexcept
    {
        set_closed_prefix();
    }

    void assign_reserved_activation(const std::size_t member_index,
        const RegionFrontierKeyV2& key_value,
        const std::uint64_t frontier_generation)
    {
        auto& member = members_.at(member_index);
        member.flags |= RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid;
        member.queued_key = key_value;
        set_ready(member_index);
        tasks_.at(0U) = RegionFrontierSchedulerTaskV2 {
            key_value.stable_order,
            key_value.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::member_activation,
                static_cast<std::uint64_t>(member_index)),
        };
        frame_.scheduler_frontier_generation = frontier_generation;
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        frame_.staged_event_count = 0U;
        set_closed_prefix();
    }

    [[nodiscard]] std::size_t boundary_plane_index() const
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            if ((layout_.signals[index].flags
                    & RegionFrontierPlaneFlagsV2::read_only_boundary_port) != 0U) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture includes a read-only boundary plane"
        };
    }

    [[nodiscard]] std::size_t internal_plane_index() const
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            if ((layout_.signals[index].flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture includes an internal mutable plane"
        };
    }

    [[nodiscard]] std::size_t first_internal_write_site() const
    {
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            if (layout_.write_sites[index].event_kind
                == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::internal_commit)) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture includes a unique internal-commit write site"
        };
    }

    [[nodiscard]] std::size_t first_internal_write_pending_slot() const
    {
        return layout_.write_sites[first_internal_write_site()].pending_slot;
    }

    [[nodiscard]] std::uint64_t dispatch_count() const noexcept
    {
        return dispatch_count_;
    }

    void prepare_activation_task()
    {
        reset_scheduler_member_state();
        const auto root = root_member_index();
        const auto key = make_key(first_task_stable_order,
            first_task_sequence);
        readiness_.at(root / 64U) |= UINT64_C(1) << (root % 64U);
        auto& member = members_.at(root);
        member.flags = RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid;
        member.queued_key = key;
        member.activation_origin = key;
        member.pending_activation_origin = key;
        tasks_[0U] = RegionFrontierSchedulerTaskV2 {
            key.stable_order,
            key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::member_activation,
                static_cast<std::uint64_t>(root)),
        };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        frame_.pending_write_count = 0U;
        frame_.staged_event_count = 0U;
        set_closed_prefix();
    }

    void prepare_internal_commit_task()
    {
        reset_scheduler_member_state();
        const auto& site = layout_.write_sites[first_internal_write_site()];
        require(site.pending_slot < layout_.pending_write_capacity,
            "the internal write site's reservation slot is in bounds");
        auto& write = writes_.at(site.pending_slot);
        const auto key = make_key(first_task_stable_order + 1U,
            first_task_sequence + 1U);
        write.flags = RegionFrontierPendingWriteFlagsV2::pending_active
            | RegionFrontierPendingWriteFlagsV2::pending_value_ready
            | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
            | RegionFrontierPendingWriteFlagsV2::pending_internal_target;
        write.commit_key = key;
        write.origin = make_key(first_task_stable_order,
            first_task_sequence);
        members_.at(site.member_index).activation_origin = write.origin;
        write.value_planes[0U][0U] = UINT64_C(1);
        write.value_planes[1U][0U] = UINT64_C(0);
        frame_.pending_write_count = 1U;
        tasks_[0U] = RegionFrontierSchedulerTaskV2 {
            key.stable_order,
            key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::internal_commit,
                site.pending_slot),
        };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        frame_.staged_event_count = 0U;
        set_closed_prefix();
    }

    [[nodiscard]] FrameSnapshot snapshot() const
    {
        return FrameSnapshot {
            frame_, readiness_, members_, tasks_, planes_, metadata_,
            fanout_, port_planes_, writes_, events_, committed_,
            plane_storage_, write_storage_, dispatch_count_, stop_value_,
        };
    }

    void require_unchanged(const FrameSnapshot& before,
        const std::string_view message) const
    {
        require(same_frame(frame_, before.frame), message);
        require(readiness_ == before.readiness, message);
        require(same_records(members_, before.members, same_member), message);
        require(same_records(tasks_, before.tasks, same_task), message);
        require(same_records(planes_, before.planes, same_plane), message);
        require(same_records(metadata_, before.metadata, same_metadata), message);
        require(same_records(fanout_, before.fanout, same_edge), message);
        require(port_planes_ == before.port_planes, message);
        require(same_records(writes_, before.writes, same_write), message);
        require(same_records(events_, before.events, same_event), message);
        require(same_records(committed_, before.committed,
                    same_committed_signal), message);
        require(plane_storage_ == before.plane_storage, message);
        require(write_storage_ == before.write_storage, message);
        require(dispatch_count_ == before.dispatch_count
                && stop_value_ == before.stop_value, message);
    }

    [[nodiscard]] std::vector<std::uint64_t>& role_words(
        const std::size_t plane, const std::size_t role) noexcept
    {
        return plane_storage_[plane].roles[role];
    }

private:
    static constexpr std::size_t boundary_aval_role = 0U;
    static constexpr std::size_t boundary_bval_role = 1U;
    static constexpr std::size_t current_aval_role = 2U;
    static constexpr std::size_t current_bval_role = 3U;
    static constexpr std::size_t previous_aval_role = 4U;
    static constexpr std::size_t previous_bval_role = 5U;
    static constexpr std::size_t stored_aval_role = 6U;
    static constexpr std::size_t stored_bval_role = 7U;
    static constexpr std::size_t owner_aval_role = 8U;
    static constexpr std::size_t owner_bval_role = 9U;

    [[nodiscard]] bool entry_shape_is_usable() const noexcept
    {
        return layout_.abi_version == kRegionFrontierAbiVersionV2
            && layout_.struct_size == sizeof(RegionFrontierLayoutV2)
            && region_frontier_layout_header_valid_v2(layout_)
            && layout_.member_count == kernel_.members.size()
            && layout_.member_count >= 2U
            && layout_.members != nullptr
            && layout_.readiness_word_count
                == (layout_.member_count + 63U) / 64U
            && layout_.signal_slot_count != 0U
            && layout_.signals != nullptr
            && layout_.metadata_count != 0U
            && layout_.fanout_edge_count != 0U
            && layout_.fanout_edges != nullptr
            && layout_.write_site_count != 0U
            && layout_.write_sites != nullptr
            && layout_.pending_write_capacity
                >= layout_.write_site_count
            && layout_.staged_event_capacity != 0U
            && layout_.committed_signal_capacity
                == layout_.pending_write_capacity
            && layout_.max_member_write_counts != nullptr
            && layout_.max_member_staged_event_counts != nullptr;
    }

    void bind_signal_planes()
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            const auto& descriptor = layout_.signals[index];
            const auto word_count = std::max<std::size_t>(
                descriptor.word_count, 4U);
            auto& storage = plane_storage_[index];
            for (std::size_t role = 0U; role < storage.roles.size(); ++role) {
                storage.roles[role].assign(word_count, 0U);
                for (std::size_t word = descriptor.word_count;
                     word < word_count; ++word) {
                    storage.roles[role][word]
                        = UINT64_C(0x5a5a000000000000)
                        | (static_cast<std::uint64_t>(index) << 8U)
                        | static_cast<std::uint64_t>(role);
                }
            }

            auto& plane = planes_[index];
            plane.signal_id = descriptor.signal_id;
            plane.owner_process_id = descriptor.owner_process_id;
            plane.value_kind = descriptor.value_kind;
            plane.plane_count = descriptor.plane_count;
            plane.width = descriptor.width;
            plane.word_count = descriptor.word_count;
            plane.flags = descriptor.flags;
            plane.metadata_index = descriptor.metadata_index;
            if ((descriptor.flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U) {
                plane.current_planes[0]
                    = storage.roles[current_aval_role].data();
                plane.current_planes[1]
                    = storage.roles[current_bval_role].data();
                plane.previous_planes[0]
                    = storage.roles[previous_aval_role].data();
                plane.previous_planes[1]
                    = storage.roles[previous_bval_role].data();
                plane.stored_planes[0]
                    = storage.roles[stored_aval_role].data();
                plane.stored_planes[1]
                    = storage.roles[stored_bval_role].data();
                plane.owner_planes[0] = storage.roles[owner_aval_role].data();
                plane.owner_planes[1] = storage.roles[owner_bval_role].data();
            } else {
                require((descriptor.flags
                            & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                        != 0U,
                    "the immutable layout identifies boundary versus internal planes");
                plane.boundary_planes[0]
                    = storage.roles[boundary_aval_role].data();
                plane.boundary_planes[1]
                    = storage.roles[boundary_bval_role].data();
                storage.roles[boundary_aval_role][0U] = UINT64_C(1);
            }
            require(region_frontier_plane_bindings_valid_v2(plane),
                "fixture backing satisfies the typed plane contract");
            port_planes_[index] = &plane;
        }
    }

    void bind_fanout()
    {
        require(kernel_.program.static_trigger_regions.empty(),
            "the fixture uses the runtime full-mask grouped-fanout path");
        for (std::size_t index = 0U; index < layout_.fanout_edge_count; ++index) {
            const auto& topology = layout_.fanout_edges[index];
            require(topology.signal_slot < layout_.signal_slot_count
                    && topology.member_index < layout_.member_count,
                "immutable fanout topology points to valid local descriptors");
            const auto process_id
                = layout_.members[topology.member_index].process_id;
            const auto member = std::ranges::find_if(kernel_.members,
                [process_id](const auto& candidate) {
                    return candidate.process == process_id;
                });
            require(member != kernel_.members.end(),
                "each topology edge maps to one original kernel member");
            const auto signal_id = layout_.signals[topology.signal_slot].signal_id;
            const auto sensitivity_count = std::ranges::count_if(
                member->sensitivities, [signal_id](const auto& sensitivity) {
                    return sensitivity.signal == signal_id;
                });
            require(sensitivity_count == 1,
                "each fixture topology edge maps to one whole-signal sensitivity");
            // This fixture has one whole-signal clause per member and no
            // static-trigger-region partition, so the runtime grouped fanout
            // binds Process::full_static_trigger_mask for each edge.
            fanout_[index] = RegionFrontierFanoutEdgeV2 {
                topology.signal_slot,
                topology.member_index,
                full_static_trigger_mask,
            };
            members_[topology.member_index].static_trigger_mask
                |= full_static_trigger_mask;
        }
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            members_[index].process_id = layout_.members[index].process_id;
            members_[index].flags
                = RegionFrontierMemberFlagsV2::waiting_on_static;
        }
    }

    void bind_write_sites()
    {
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            const auto& site = layout_.write_sites[index];
            require(site.signal_slot < layout_.signal_slot_count,
                "write site signal slot is in the immutable layout");
            const auto& signal = layout_.signals[site.signal_slot];
            require(site.pending_slot < layout_.pending_write_capacity
                    && site.member_index < layout_.member_count
                    && site.signal_slot < layout_.signal_slot_count
                    && site.value_kind == RegionFrontierValueKindV2::logic4
                    && site.plane_count
                        == kRegionFrontierLogic4PlaneCountV2
                    && site.width != 0U
                    && site.word_count == words_for(site.width)
                    && site.value_kind == signal.value_kind
                    && site.plane_count == signal.plane_count,
                "every immutable write site has a safe frame-backed slot");
            const auto backing_words = std::max<std::size_t>(
                static_cast<std::size_t>(site.word_count) + 1U, 4U);
            auto& backing = write_storage_[site.pending_slot];
            backing.aval.assign(backing_words, 0U);
            backing.bval.assign(backing_words, 0U);
            for (std::size_t word = site.word_count;
                 word < backing_words; ++word) {
                backing.aval[word] = UINT64_C(0x123456789abcdef0);
                backing.bval[word] = UINT64_C(0x0fedcba987654321);
            }
            auto& write = writes_[site.pending_slot];
            write.member_index = site.member_index;
            write.signal_slot = site.signal_slot;
            write.source_instruction = site.source_instruction;
            write.update_kind = site.update_kind;
            write.value_planes[0U] = backing.aval.data();
            write.value_planes[1U] = backing.bval.data();
            write.value_kind = site.value_kind;
            write.plane_count = site.plane_count;
            write.width = site.width;
            write.word_count = site.word_count;
        }
    }

    void initialize_frame(const std::uint64_t runtime_generation)
    {
        frame_.abi_version = kRegionFrontierAbiVersionV2;
        frame_.struct_size = sizeof(RegionFrontierFrameV2);
        frame_.value_plane_contract = kRegionFrontierValuePlaneContractV2;
        frame_.runtime_generation = runtime_generation;
        frame_.bound_runtime_generation = runtime_generation;
        frame_.certificate_generation = layout_.certificate_generation;
        frame_.component_generation = layout_.component_generation;
        frame_.scheduler_frontier_generation = 1U;
        frame_.member_count = layout_.member_count;
        frame_.scheduler_task_capacity = declared_task_capacity;
        frame_.readiness_word_count = layout_.readiness_word_count;
        frame_.signal_slot_count = layout_.signal_slot_count;
        frame_.metadata_count = layout_.metadata_count;
        frame_.fanout_edge_count = layout_.fanout_edge_count;
        frame_.committed_signal_capacity
            = layout_.committed_signal_capacity;
        frame_.pending_write_capacity = layout_.pending_write_capacity;
        frame_.staged_event_capacity = layout_.staged_event_capacity;
        frame_.current_member = UINT32_MAX;
        frame_.current_pending_write = UINT32_MAX;
        frame_.ready_words = readiness_.data();
        frame_.members = members_.data();
        frame_.scheduler_tasks = tasks_.data();
        frame_.planes = planes_.data();
        frame_.metadata = metadata_.data();
        frame_.fanout_edges = fanout_.data();
        frame_.port_planes = port_planes_.data();
        frame_.pending_writes = writes_.data();
        frame_.staged_events = events_.data();
        frame_.committed_signals = committed_.data();
        frame_.native_frontier_member_dispatches = &dispatch_count_;
        frame_.stop_requested = &stop_value_;
        frame_.slot = RegionFrontierSlotV2 {
            23U, 0U, 0U, 1U, 0U,
        };
        set_closed_prefix();
    }

    void reset_scheduler_member_state()
    {
        std::fill(readiness_.begin(), readiness_.end(), 0U);
        std::fill(tasks_.begin(), tasks_.end(), RegionFrontierSchedulerTaskV2 { });
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            auto& member = members_[index];
            member.flags = RegionFrontierMemberFlagsV2::waiting_on_static;
            member.queued_key = { };
            member.activation_origin = { };
            member.pending_activation_origin = { };
        }
        for (std::size_t index = 0U; index < layout_.pending_write_capacity; ++index) {
            writes_[index].flags = 0U;
            writes_[index].commit_key = { };
            writes_[index].origin = { };
        }
        frame_.current_member = UINT32_MAX;
        frame_.current_pending_write = UINT32_MAX;
        frame_.scheduler_frontier_generation = 1U;
        frame_.committed_signal_count = 0U;
        frame_.pending_write_count = 0U;
        frame_.staged_event_count = 0U;
        frame_.current_commit_changed = 0U;
        frame_.saved_body_pc = 0U;
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

    void set_closed_prefix() noexcept
    {
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        frame_.cut.next_key = { };
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        std::fill(std::begin(frame_.cut.reserved), std::end(frame_.cut.reserved), 0U);
    }

    const RegionConeActivationKernel& kernel_;
    const RegionFrontierLayoutV2& layout_;
    RegionFrontierFrameV2 frame_;
    std::vector<std::uint64_t> readiness_;
    std::vector<RegionFrontierMemberV2> members_;
    std::vector<RegionFrontierSchedulerTaskV2> tasks_;
    std::vector<RegionFrontierPlaneV2> planes_;
    std::vector<PlaneStorage> plane_storage_;
    std::vector<RegionFrontierSignalMetadataV2> metadata_;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_;
    std::vector<const RegionFrontierPlaneV2*> port_planes_;
    std::vector<RegionFrontierPendingWriteV2> writes_;
    std::vector<WriteStorage> write_storage_;
    std::vector<RegionFrontierStagedEventV2> events_;
    std::vector<RegionFrontierCommittedSignalV2> committed_;
    std::uint64_t dispatch_count_ { };
    std::uint32_t stop_value_ { };
};

[[nodiscard]] std::size_t member_index_for_process(
    const RegionFrontierLayoutV2& layout, const ProcessId process)
{
    for (std::size_t index = 0U; index < layout.member_count; ++index) {
        if (layout.members[index].process_id == process) {
            return index;
        }
    }
    throw std::runtime_error {
        "the production kernel process maps to a compiled frontier member"
    };
}

[[nodiscard]] std::size_t write_site_for_member(
    const RegionFrontierLayoutV2& layout, const std::size_t member,
    const RegionFrontierEventKindV2 event_kind)
{
    for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
        const auto& site = layout.write_sites[index];
        if (site.member_index == member
            && site.event_kind == static_cast<std::uint32_t>(event_kind)) {
            return index;
        }
    }
    throw std::runtime_error {
        "the compiled member retains the expected static write site"
    };
}

[[nodiscard]] std::size_t plane_for_signal(
    const RegionFrontierLayoutV2& layout, const SignalId signal)
{
    for (std::size_t index = 0U; index < layout.signal_slot_count; ++index) {
        if (layout.signals[index].signal_id == signal) {
            return index;
        }
    }
    throw std::runtime_error {
        "the kernel input maps to one immutable frontier signal plane"
    };
}

[[nodiscard]] bool entry_accepted(const RegionFrontierStatusV2 status) noexcept
{
    return status != RegionFrontierStatusV2::decline_before_mutation
        && status != RegionFrontierStatusV2::stale_generation;
}

void run_initial_queued_member_without_waiting(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_activation_task();
    const auto member_index = storage.root_member_index();
    auto& member = storage.members().at(member_index);
    require((member.flags & RegionFrontierMemberFlagsV2::waiting_on_static) == 0U,
        "an initially queued activation need not already be waiting");
    const auto queued_key = member.queued_key;

    const auto status = entry(&storage.frame());
    require(entry_accepted(status),
        "the generated entry accepts the initially queued activation");
    require(storage.frame().scheduler_task_cursor == 1U
            && storage.frame().current_member == UINT32_MAX
            && storage.dispatch_count() == 1U,
        "the generated member body completes and advances its borrowed task");
    require((member.flags & RegionFrontierMemberFlagsV2::waiting_on_static) != 0U
            && (member.flags & (RegionFrontierMemberFlagsV2::queued
                    | RegionFrontierMemberFlagsV2::queued_key_valid
                    | RegionFrontierMemberFlagsV2::executing)) == 0U,
        "body completion sets waiting_on_static and retires queued state");
    require(same_key(member.activation_origin, queued_key),
        "an activation without a pending fanout uses its exact queued key as origin");
}

void run_internal_commit_then_same_member_activation(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_internal_commit_task();
    const auto site_index = storage.first_internal_write_site();
    const auto& site = layout.write_sites[site_index];
    const auto producer = member_index_for_process(layout,
        kernel.members.front().process);
    require(site.member_index == producer,
        "the certified internal commit belongs to the queued producer");

    const auto origin_a = storage.key(first_task_stable_order,
        first_task_sequence);
    const auto commit_key = storage.key(first_task_stable_order + 1U,
        first_task_sequence + 1U);
    const auto queued_key_b = storage.key(first_task_stable_order + 2U,
        first_task_sequence + 2U);
    auto& member = storage.members().at(producer);
    auto& write = storage.writes().at(site.pending_slot);
    require(same_key(write.origin, origin_a)
            && same_key(member.activation_origin, origin_a),
        "the old active write is authenticated by its prior body origin");

    member.flags = RegionFrontierMemberFlagsV2::waiting_on_static
        | RegionFrontierMemberFlagsV2::queued
        | RegionFrontierMemberFlagsV2::queued_key_valid;
    member.queued_key = queued_key_b;
    member.pending_activation_origin = { };
    storage.set_ready(producer);
    storage.set_task(1U, RegionFrontierSchedulerTaskV2 {
        queued_key_b.stable_order,
        queued_key_b.sequence,
        encode_region_frontier_payload_v2(
            RegionFrontierEventKindV2::member_activation,
            static_cast<std::uint64_t>(producer)),
    });
    storage.frame().scheduler_task_count = 2U;
    storage.frame().scheduler_task_cursor = 0U;
    storage.frame().pending_write_count = 1U;
    storage.seal_scheduler_prefix();

    // The current word already equals the retiring internal write, while the
    // newly executed body will write zero. This makes reuse of the retired
    // static slot observable even if its owner shadow suppresses equal writes.
    storage.role_words(site.signal_slot, 2U)[0U] = UINT64_C(1);
    const auto source_input = std::ranges::find_if(kernel.inputs,
        [](const RegionConeKernelInput& input) { return !input.internal; });
    require(source_input != kernel.inputs.end(),
        "the producer body reads one boundary input plane");
    storage.role_words(plane_for_signal(layout, source_input->signal), 0U)[0U]
        = UINT64_C(0);
    const auto status = entry(&storage.frame());
    require(entry_accepted(status),
        "the generated entry accepts commit then queued same-member activation");
    require(storage.frame().scheduler_task_cursor == 2U
            && storage.frame().current_member == UINT32_MAX
            && storage.dispatch_count() == 1U,
        "the old commit retires before the already-queued body executes");
    require(storage.frame().committed_signal_count == 1U
            && storage.committed().front().signal_slot == site.signal_slot,
        "the prior internal commit remains recorded in order");
    require(storage.committed().front().changed == 0U
            && storage.committed().front().state_changed == 1U,
        "the slot-reuse setup commits role state without another value event");
    require((member.flags & RegionFrontierMemberFlagsV2::waiting_on_static) != 0U
            && (member.flags & (RegionFrontierMemberFlagsV2::queued
                    | RegionFrontierMemberFlagsV2::queued_key_valid
                    | RegionFrontierMemberFlagsV2::executing)) == 0U,
        "the requeued body reaches its static wait lifecycle state");
    require(member.static_trigger_mask == 0U
            && (storage.frame().ready_words[producer / 64U]
                    & (UINT64_C(1) << (producer % 64U))) == 0U,
        "the consumed activation clears its old static mask and readiness bit");
    require(same_key(member.activation_origin, queued_key_b),
        "without pending fanout origin the new body uses queued key B");
    require(storage.frame().pending_write_count == 1U
            && storage.frame().staged_event_count == 1U,
        "the new body reserves its static write site after commit retirement");
    require((write.flags & RegionFrontierPendingWriteFlagsV2::pending_active) != 0U
            && (write.flags
                    & RegionFrontierPendingWriteFlagsV2::pending_value_ready) != 0U
            && write.member_index == site.member_index
            && write.signal_slot == site.signal_slot
            && write.source_instruction == site.source_instruction
            && write.width == site.width
            && write.word_count == site.word_count
            && same_key(write.origin, queued_key_b),
        "the same immutable pending slot now belongs to body origin B");
    const auto& staged = storage.frame().staged_events[0U];
    require(staged.kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::internal_commit)
            && staged.descriptor_index == site.pending_slot
            && same_key(staged.origin, queued_key_b),
        "the reused slot stages a fresh commit with the new body's origin");
    require(!same_key(commit_key, queued_key_b),
        "commit ordering key and later activation key remain distinct");
}

void run_fanout_origin_survives_fresh_queue_key(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_internal_commit_task();
    const auto internal_site = layout.write_sites[
        storage.first_internal_write_site()];
    const auto consumer = member_index_for_process(layout,
        kernel.members.back().process);
    const auto cause_key = storage.writes().at(
        internal_site.pending_slot).commit_key;
    storage.role_words(internal_site.signal_slot, 2U)[0U] = UINT64_C(0);

    const auto commit_status = entry(&storage.frame());
    require(entry_accepted(commit_status),
        "the generated entry accepts the actual internal fanout commit");
    require(storage.frame().scheduler_task_cursor == 1U
            && storage.frame().committed_signal_count == 1U
            && storage.frame().staged_event_count == 1U,
        "the changing commit emits one fanout-origin member activation");
    const auto& fanout_event = storage.frame().staged_events[0U];
    require(fanout_event.kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::member_activation)
            && fanout_event.descriptor_index == consumer
            && same_key(fanout_event.origin, cause_key),
        "the generated fanout records the causal commit key");
    auto& member = storage.members().at(consumer);
    require((member.flags & RegionFrontierMemberFlagsV2::pending_activation) != 0U
            && (member.flags & RegionFrontierMemberFlagsV2::queued) == 0U
            && same_key(member.pending_activation_origin, cause_key),
        "the target retains a pending causal origin before scheduler assignment");

    // Model the existing host reservation handoff: consume that staged member
    // event, assign a fresh scheduler-authored key, and expose the next borrowed
    // prefix. The entry itself remains the production-generated function.
    const auto queued_key_b = storage.key(first_task_stable_order + 2U,
        first_task_sequence + 2U);
    storage.assign_reserved_activation(consumer, queued_key_b,
        storage.frame().scheduler_frontier_generation + 1U);
    require(same_key(member.pending_activation_origin, cause_key)
            && same_key(member.queued_key, queued_key_b)
            && !same_key(member.pending_activation_origin, member.queued_key),
        "fresh queue assignment preserves a distinct earlier fanout cause");

    const auto activation_status = entry(&storage.frame());
    require(entry_accepted(activation_status),
        "the generated entry accepts the host-keyed fanout activation");
    require(storage.frame().scheduler_task_cursor == 1U
            && storage.dispatch_count() == 1U,
        "the queued consumer body completes in the next generated step");
    require((member.flags & RegionFrontierMemberFlagsV2::pending_activation) == 0U
            && (member.flags & RegionFrontierMemberFlagsV2::waiting_on_static) != 0U,
        "the body consumes pending fanout state and returns to static wait");
    require(member.static_trigger_mask == 0U
            && (storage.frame().ready_words[consumer / 64U]
                    & (UINT64_C(1) << (consumer % 64U))) == 0U,
        "the fanout activation consumes its mask and readiness bit");
    require(same_key(member.activation_origin, cause_key)
            && !same_key(member.activation_origin, queued_key_b),
        "the generated body keeps its causal origin separate from queue key B");

    const auto boundary_site_index = write_site_for_member(layout, consumer,
        RegionFrontierEventKindV2::boundary_commit);
    const auto& boundary_site = layout.write_sites[boundary_site_index];
    const auto& boundary_write = storage.writes().at(boundary_site.pending_slot);
    require((boundary_write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_active) != 0U
            && same_key(boundary_write.origin, cause_key),
        "the actual consumer body stages its boundary write with fanout origin");
    require(!same_key(boundary_write.origin, boundary_write.commit_key)
            && (boundary_write.flags
                    & RegionFrontierPendingWriteFlagsV2::pending_key_assigned) == 0U,
        "the causal origin remains separate from an unassigned commit key");
}

void run_duplicate_activation_declines_unchanged(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_activation_task();
    const auto member_index = storage.root_member_index();
    const auto second_task_key = storage.key(first_task_stable_order + 1U,
        first_task_sequence + 1U);
    storage.set_task(1U, RegionFrontierSchedulerTaskV2 {
        second_task_key.stable_order,
        second_task_key.sequence,
        encode_region_frontier_payload_v2(
            RegionFrontierEventKindV2::member_activation,
            static_cast<std::uint64_t>(member_index)),
    });
    storage.frame().scheduler_task_count = 2U;
    storage.seal_scheduler_prefix();
    const auto before = storage.snapshot();

    const auto status = entry(&storage.frame());
    require(status == RegionFrontierStatusV2::decline_before_mutation,
        "two activations for one member cannot borrow one queued key twice");
    storage.require_unchanged(before,
        "duplicate unauthenticated member activation declines before mutation");
}

} // namespace

void run_region_frontier_lifecycle_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    require(entry != nullptr,
        "the lifecycle suite receives the production noexcept entry");
    require(runtime_generation != 0U,
        "the generated entry has a nonzero bound runtime generation");
    require(layout.abi_version == kRegionFrontierAbiVersionV2
            && layout.struct_size == sizeof(RegionFrontierLayoutV2)
            && region_frontier_layout_header_valid_v2(layout),
        "the code owner publishes the current private frontier ABI");

    run_initial_queued_member_without_waiting(kernel, entry, layout,
        runtime_generation);
    run_internal_commit_then_same_member_activation(kernel, entry, layout,
        runtime_generation);
    run_fanout_origin_survives_fresh_queue_key(kernel, entry, layout,
        runtime_generation);
    run_duplicate_activation_declines_unchanged(kernel, entry, layout,
        runtime_generation);
}

} // namespace fsim::compiler::test

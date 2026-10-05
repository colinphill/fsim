// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_round_boundary_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using namespace runtime::simir;

constexpr std::uint64_t current_round { 7U };
constexpr std::uint32_t task_capacity { 32U };
constexpr std::uint64_t full_static_trigger_mask = UINT64_C(1) << 63U;

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

[[nodiscard]] std::vector<std::byte> image_of(
    const void* const source, const std::size_t size)
{
    std::vector<std::byte> image(size);
    if (size != 0U) {
        std::memcpy(image.data(), source, size);
    }
    return image;
}

template<typename T>
[[nodiscard]] std::vector<std::byte> image_of(
    const std::vector<T>& values)
{
    return image_of(values.data(), values.size() * sizeof(T));
}

struct WriteWords {
    std::vector<std::uint64_t> aval;
    std::vector<std::uint64_t> bval;
};

using PlaneWords = std::array<std::vector<std::uint64_t>, 10U>;
using PlaneWordsImage = std::array<std::vector<std::byte>, 10U>;

struct FrameImage {
    std::vector<std::byte> frame;
    std::vector<std::byte> readiness;
    std::vector<std::byte> members;
    std::vector<std::byte> tasks;
    std::vector<std::byte> planes;
    std::vector<std::byte> metadata;
    std::vector<std::byte> fanout;
    std::vector<std::byte> port_planes;
    std::vector<std::byte> writes;
    std::vector<std::byte> events;
    std::vector<std::byte> committed;
    std::vector<PlaneWordsImage> plane_words;
    std::vector<std::pair<std::vector<std::byte>, std::vector<std::byte>>>
        write_words;
    std::uint64_t dispatch_count { };
    std::uint32_t stop_value { };

    bool operator==(const FrameImage&) const = default;
};

class RoundFrameStorage final {
public:
    RoundFrameStorage(const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout,
        const std::uint64_t runtime_generation)
        : kernel_ { kernel }
        , layout_ { layout }
        , readiness_ (layout.readiness_word_count)
        , members_ (layout.member_count)
        , tasks_ (static_cast<std::size_t>(task_capacity) * 2U)
        , planes_ (layout.signal_slot_count)
        , plane_words_ (layout.signal_slot_count)
        , metadata_ (layout.metadata_count)
        , fanout_ (layout.fanout_edge_count)
        , port_planes_ (layout.signal_slot_count)
        , writes_ (layout.pending_write_capacity)
        , write_words_ (layout.pending_write_capacity)
        , events_ (static_cast<std::size_t>(layout.staged_event_capacity) + 1U)
        , committed_ (
              static_cast<std::size_t>(layout.committed_signal_capacity) + 1U)
    {
        require(kernel_.members.size() == layout_.member_count
                && region_frontier_layout_header_valid_v2(layout_)
                && layout_.member_count >= 2U
                && layout_.readiness_word_count != 0U
                && layout_.signal_slot_count != 0U
                && layout_.metadata_count != 0U
                && layout_.write_site_count != 0U
                && layout_.pending_write_capacity >= layout_.write_site_count
                && layout_.committed_signal_capacity
                    == layout_.pending_write_capacity
                && layout_.staged_event_capacity != 0U
                && layout_.fanout_edge_count != 0U
                && layout_.members != nullptr
                && layout_.signals != nullptr
                && layout_.write_sites != nullptr
                && layout_.max_member_write_counts != nullptr
                && layout_.max_member_staged_event_counts != nullptr
                && layout_.fanout_edges != nullptr,
            "the production graph supplies a complete multi-member layout");
        bind_planes();
        bind_fanout();
        bind_write_sites();
        initialize_frame(runtime_generation);
    }

    [[nodiscard]] RegionFrontierFrameV2& frame() noexcept { return frame_; }
    [[nodiscard]] std::vector<RegionFrontierMemberV2>& members() noexcept
    {
        return members_;
    }
    [[nodiscard]] std::vector<RegionFrontierSchedulerTaskV2>& tasks() noexcept
    {
        return tasks_;
    }
    [[nodiscard]] std::vector<RegionFrontierPlaneV2>& planes() noexcept
    {
        return planes_;
    }
    [[nodiscard]] std::vector<RegionFrontierPendingWriteV2>& writes() noexcept
    {
        return writes_;
    }
    [[nodiscard]] std::vector<RegionFrontierStagedEventV2>& events() noexcept
    {
        return events_;
    }
    [[nodiscard]] std::vector<RegionFrontierCommittedSignalV2>& committed()
        noexcept
    {
        return committed_;
    }
    [[nodiscard]] std::vector<std::uint64_t>& role_words(
        const std::size_t plane, const std::size_t role) noexcept
    {
        return plane_words_[plane][role];
    }

    [[nodiscard]] std::size_t member_for_process(const ProcessId process) const
    {
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            if (layout_.members[index].process_id == process) {
                return index;
            }
        }
        throw std::runtime_error {
            "the certified source process maps to one member slot"
        };
    }

    [[nodiscard]] std::size_t site_for_member(
        const std::size_t member, const RegionFrontierEventKindV2 kind) const
    {
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            const auto& site = layout_.write_sites[index];
            if (site.member_index == member
                && site.event_kind == static_cast<std::uint32_t>(kind)) {
                return index;
            }
        }
        throw std::runtime_error {
            "the certified member has the requested static write site"
        };
    }

    [[nodiscard]] RegionFrontierKeyV2 key(
        const std::uint64_t stable_order,
        const std::uint64_t sequence,
        const std::uint64_t round) const noexcept
    {
        return RegionFrontierKeyV2 {
            frame_.slot.time,
            frame_.slot.delta,
            round,
            stable_order,
            sequence,
            frame_.slot.process_domain,
            frame_.slot.phase,
        };
    }

    void set_round(const std::uint64_t round,
        const std::uint64_t frontier_generation)
    {
        frame_.slot.systemverilog_round = round;
        frame_.scheduler_frontier_generation = frontier_generation;
        set_closed_prefix();
    }

    void prepare_retained_internal_write()
    {
        set_round(current_round, 1U);
        const auto producer = member_for_process(kernel_.members.front().process);
        internal_site_index_ = site_for_member(producer,
            RegionFrontierEventKindV2::internal_commit);
        internal_site_ = &layout_.write_sites[internal_site_index_];
        auto& write = writes_.at(internal_site_->pending_slot);
        write.flags = RegionFrontierPendingWriteFlagsV2::pending_active
            | RegionFrontierPendingWriteFlagsV2::pending_value_ready
            | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
            | RegionFrontierPendingWriteFlagsV2::pending_internal_target;
        write.commit_key = key(400U, 19U, current_round + 1U);
        write.origin = key(301U, 7U, current_round);
        members_.at(producer).activation_origin = write.origin;
        write_words_.at(internal_site_->pending_slot).aval[0U] = 1U;
        write_words_.at(internal_site_->pending_slot).bval[0U] = 0U;
        frame_.pending_write_count = 1U;
        role_words(internal_site_->signal_slot, current_aval_role)[0U] = 0U;
        role_words(internal_site_->signal_slot, current_bval_role)[0U] = 0U;
        role_words(internal_site_->signal_slot, stored_aval_role)[0U] = 0U;
        role_words(internal_site_->signal_slot, stored_bval_role)[0U] = 0U;
        role_words(internal_site_->signal_slot, owner_aval_role)[0U] = 0U;
        role_words(internal_site_->signal_slot, owner_bval_role)[0U] = 0U;
    }

    void queue_current_consumer_activation()
    {
        const auto consumer = member_for_process(kernel_.members.back().process);
        const auto queued_key = key(505U, 23U, current_round);
        auto& member = members_.at(consumer);
        member.flags = RegionFrontierMemberFlagsV2::waiting_on_static
            | RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid;
        member.queued_key = queued_key;
        member.activation_origin = queued_key;
        member.pending_activation_origin = queued_key;
        readiness_.at(consumer / 64U)
            |= UINT64_C(1) << (consumer % 64U);
        tasks_[0U] = RegionFrontierSchedulerTaskV2 {
            queued_key.stable_order,
            queued_key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::member_activation,
                static_cast<std::uint64_t>(consumer)),
        };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        set_closed_prefix();
    }

    void queue_retained_future_commit()
    {
        const auto& write = writes_.at(internal_site_->pending_slot);
        tasks_[0U] = RegionFrontierSchedulerTaskV2 {
            write.commit_key.stable_order,
            write.commit_key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::internal_commit,
                internal_site_->pending_slot),
        };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        set_closed_prefix();
    }

    [[nodiscard]] FrameImage snapshot() const
    {
        FrameImage result;
        result.frame = image_of(&frame_, sizeof(frame_));
        result.readiness = image_of(readiness_);
        result.members = image_of(members_);
        result.tasks = image_of(tasks_);
        result.planes = image_of(planes_);
        result.metadata = image_of(metadata_);
        result.fanout = image_of(fanout_);
        result.port_planes = image_of(port_planes_);
        result.writes = image_of(writes_);
        result.events = image_of(events_);
        result.committed = image_of(committed_);
        result.plane_words.reserve(plane_words_.size());
        for (const auto& plane : plane_words_) {
            PlaneWordsImage image;
            for (std::size_t role = 0U; role < plane.size(); ++role) {
                image[role] = image_of(plane[role]);
            }
            result.plane_words.push_back(std::move(image));
        }
        result.write_words.reserve(write_words_.size());
        for (const auto& words : write_words_) {
            result.write_words.emplace_back(
                image_of(words.aval), image_of(words.bval));
        }
        result.dispatch_count = dispatch_count_;
        result.stop_value = stop_value_;
        return result;
    }

    void require_unchanged(const FrameImage& before,
        const std::string_view message) const
    {
        require(snapshot() == before, message);
    }

    [[nodiscard]] std::size_t internal_site_index() const noexcept
    {
        return internal_site_index_;
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

    void bind_planes()
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            const auto& descriptor = layout_.signals[index];
            const auto backing_count = std::max<std::size_t>(
                descriptor.word_count, 4U);
            auto& words = plane_words_[index];
            for (std::size_t role = 0U; role < words.size(); ++role) {
                words[role].assign(backing_count, 0U);
                for (std::size_t word = descriptor.word_count;
                     word < backing_count; ++word) {
                    words[role][word] = UINT64_C(0x5a5a000000000000)
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
                plane.current_planes[0] = words[current_aval_role].data();
                plane.current_planes[1] = words[current_bval_role].data();
                plane.previous_planes[0] = words[previous_aval_role].data();
                plane.previous_planes[1] = words[previous_bval_role].data();
                plane.stored_planes[0] = words[stored_aval_role].data();
                plane.stored_planes[1] = words[stored_bval_role].data();
                plane.owner_planes[0] = words[owner_aval_role].data();
                plane.owner_planes[1] = words[owner_bval_role].data();
            } else {
                require((descriptor.flags
                            & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                        != 0U,
                    "the layout identifies all remaining planes as boundary");
                plane.boundary_planes[0] = words[boundary_aval_role].data();
                plane.boundary_planes[1] = words[boundary_bval_role].data();
            }
            require(region_frontier_plane_bindings_valid_v2(plane),
                "fixture backing satisfies the typed plane contract");
            port_planes_[index] = &plane;
        }
    }

    void bind_fanout()
    {
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            members_[index].process_id = layout_.members[index].process_id;
            members_[index].flags
                = RegionFrontierMemberFlagsV2::waiting_on_static;
        }
        for (std::size_t index = 0U; index < layout_.fanout_edge_count; ++index) {
            const auto& topology = layout_.fanout_edges[index];
            require(topology.member_index < layout_.member_count
                    && topology.signal_slot < layout_.signal_slot_count,
                "immutable fanout topology uses valid descriptor indices");
            fanout_[index] = RegionFrontierFanoutEdgeV2 {
                topology.signal_slot,
                topology.member_index,
                full_static_trigger_mask,
            };
            members_[topology.member_index].static_trigger_mask
                |= full_static_trigger_mask;
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
                "every static write site has preallocated typed backing");
            auto& words = write_words_[site.pending_slot];
            const auto backing_count
                = std::max<std::size_t>(site.word_count + 1U, 4U);
            words.aval.assign(backing_count, 0U);
            words.bval.assign(backing_count, 0U);
            for (std::size_t word = site.word_count;
                 word < backing_count; ++word) {
                words.aval[word] = UINT64_C(0x123456789abcdef0);
                words.bval[word] = UINT64_C(0x0fedcba987654321);
            }
            auto& write = writes_[site.pending_slot];
            write.member_index = site.member_index;
            write.signal_slot = site.signal_slot;
            write.source_instruction = site.source_instruction;
            write.update_kind = site.update_kind;
            write.value_planes[0U] = words.aval.data();
            write.value_planes[1U] = words.bval.data();
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
        frame_.scheduler_task_capacity = task_capacity;
        frame_.readiness_word_count = layout_.readiness_word_count;
        frame_.signal_slot_count = layout_.signal_slot_count;
        frame_.metadata_count = layout_.metadata_count;
        frame_.fanout_edge_count = layout_.fanout_edge_count;
        frame_.committed_signal_capacity = layout_.committed_signal_capacity;
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
            11U,
            4U,
            current_round,
            static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog),
            static_cast<std::uint32_t>(runtime::SchedulerPhase::active),
        };
        set_closed_prefix();
    }

    void set_closed_prefix() noexcept
    {
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        frame_.cut.next_key = { };
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        std::fill(std::begin(frame_.cut.reserved),
            std::end(frame_.cut.reserved), 0U);
    }

    const RegionConeActivationKernel& kernel_;
    const RegionFrontierLayoutV2& layout_;
    RegionFrontierFrameV2 frame_;
    std::vector<std::uint64_t> readiness_;
    std::vector<RegionFrontierMemberV2> members_;
    std::vector<RegionFrontierSchedulerTaskV2> tasks_;
    std::vector<RegionFrontierPlaneV2> planes_;
    std::vector<PlaneWords> plane_words_;
    std::vector<RegionFrontierSignalMetadataV2> metadata_;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_;
    std::vector<const RegionFrontierPlaneV2*> port_planes_;
    std::vector<RegionFrontierPendingWriteV2> writes_;
    std::vector<WriteWords> write_words_;
    std::vector<RegionFrontierStagedEventV2> events_;
    std::vector<RegionFrontierCommittedSignalV2> committed_;
    std::uint64_t dispatch_count_ { };
    std::uint32_t stop_value_ { };
    std::size_t internal_site_index_ { UINT32_MAX };
    const RegionFrontierWriteSiteV2* internal_site_ { };
};

[[nodiscard]] bool accepted(const RegionFrontierStatusV2 status) noexcept
{
    return status != RegionFrontierStatusV2::decline_before_mutation
        && status != RegionFrontierStatusV2::stale_generation;
}

void require_rejected_without_mutation(
    const RegionFrontierStepEntryV2 entry,
    RoundFrameStorage& storage,
    const std::string_view message)
{
    const auto before = storage.snapshot();
    const auto status = entry(&storage.frame());
    require(status == RegionFrontierStatusV2::decline_before_mutation, message);
    storage.require_unchanged(before,
        "round-invalid retained work declines before all frame mutation");
}

void run_retained_write_coexists_with_current_member(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    RoundFrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_retained_internal_write();
    storage.queue_current_consumer_activation();
    const auto site_index = storage.internal_site_index();
    const auto& site = layout.write_sites[site_index];
    const auto pending_key = storage.writes().at(site.pending_slot).commit_key;

    const auto status = entry(&storage.frame());
    require(accepted(status),
        "a next-round retained write coexists with a current-round member task");
    require(storage.frame().scheduler_task_cursor == 1U
            && storage.frame().current_member == UINT32_MAX
            && storage.frame().native_frontier_member_dispatches != nullptr
            && *storage.frame().native_frontier_member_dispatches == 1U,
        "the current-round peer body completes exactly once");
    const auto& retained = storage.writes().at(site.pending_slot);
    require((retained.flags
                & RegionFrontierPendingWriteFlagsV2::pending_active) != 0U
            && retained.commit_key.systemverilog_round == current_round + 1U
            && retained.origin.systemverilog_round == current_round
            && retained.commit_key.stable_order == pending_key.stable_order
            && retained.commit_key.sequence == pending_key.sequence,
        "the unconsumed internal descriptor keeps its next-round key and old origin");
}

void run_future_commit_key_cannot_execute_early(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    RoundFrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_retained_internal_write();
    storage.queue_retained_future_commit();
    require_rejected_without_mutation(entry, storage,
        "a next-round commit task cannot execute in the current borrowed round");
}

void run_next_round_consumes_original_commit(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    RoundFrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_retained_internal_write();
    const auto site_index = storage.internal_site_index();
    const auto& site = layout.write_sites[site_index];
    const auto producer
        = storage.member_for_process(kernel.members.front().process);
    const auto consumer
        = storage.member_for_process(kernel.members.back().process);
    const auto original_origin
        = storage.writes().at(site.pending_slot).origin;
    const auto commit_key
        = storage.writes().at(site.pending_slot).commit_key;
    storage.set_round(current_round + 1U, 2U);
    storage.queue_retained_future_commit();

    const auto status = entry(&storage.frame());
    require(accepted(status),
        "the next borrowed round consumes its retained internal commit");
    const auto& write = storage.writes().at(site.pending_slot);
    require(storage.frame().scheduler_task_cursor == 1U
            && storage.frame().current_pending_write == UINT32_MAX
            && storage.frame().pending_write_count == 0U
            && storage.frame().committed_signal_count == 1U
            && storage.frame().native_frontier_member_dispatches != nullptr
            && *storage.frame().native_frontier_member_dispatches == 0U,
        "the resumed round commits once without replaying a member body");
    require((write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_active) == 0U
            && (write.flags
                & RegionFrontierPendingWriteFlagsV2::pending_committed) != 0U
            && write.origin.systemverilog_round == current_round
            && write.commit_key.systemverilog_round == current_round + 1U
            && storage.members().at(producer).activation_origin.systemverilog_round
                == current_round,
        "the resumed commit retains its prior-round causal origin");
    const auto& committed = storage.committed().front();
    require(committed.signal_slot == site.signal_slot
            && committed.changed == 1U
            && committed.state_changed == 1U,
        "the retained value commits through the original certified write site");
    const auto metadata_index = storage.planes().at(site.signal_slot).metadata_index;
    const auto& metadata = storage.frame().metadata[metadata_index];
    require(metadata.event_valid != 0U
            && metadata.systemverilog_round == current_round + 1U
            && metadata.event_process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && metadata.event_phase
                == static_cast<std::uint32_t>(runtime::SchedulerPhase::active),
        "the value event records the borrowed round and its Active origin class");
    require(storage.frame().staged_event_count == 1U
            && storage.events().front().kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::member_activation)
            && storage.events().front().descriptor_index == consumer
            && storage.events().front().origin.systemverilog_round
                == current_round + 1U,
        "the newly triggered peer receives a fresh cause from this commit round");
    require((storage.members().at(consumer).flags
                & RegionFrontierMemberFlagsV2::pending_activation) != 0U
            && storage.members().at(consumer).pending_activation_origin
                .systemverilog_round == current_round + 1U
            && write.origin.time == original_origin.time
            && write.origin.delta == original_origin.delta
            && write.origin.stable_order == original_origin.stable_order
            && write.origin.sequence == original_origin.sequence,
        "the retained body's old cause stays distinct from the new fanout cause");
    require(commit_key.systemverilog_round == current_round + 1U,
        "the original reserved key is used by the next borrowed round");
}

void run_invalid_retained_descriptor_declines(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    const std::uint64_t invalid_round,
    const bool invalid_time,
    const bool invalid_delta,
    const std::string_view message)
{
    RoundFrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_retained_internal_write();
    storage.queue_current_consumer_activation();
    auto& write = storage.writes().at(
        layout.write_sites[storage.internal_site_index()].pending_slot);
    write.commit_key.systemverilog_round = invalid_round;
    if (invalid_time) {
        ++write.commit_key.time;
    }
    if (invalid_delta) {
        ++write.commit_key.delta;
    }
    require_rejected_without_mutation(entry, storage, message);
}

} // namespace

void run_region_frontier_round_boundary_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    require(entry != nullptr,
        "round-boundary tests receive the actual generated noexcept entry");
    require(runtime_generation != 0U
            && layout.abi_version == kRegionFrontierAbiVersionV2
            && layout.struct_size == sizeof(RegionFrontierLayoutV2)
            && region_frontier_layout_header_valid_v2(layout)
            && kernel.members.size() >= 2U,
        "the production entry and graph-certified multi-member layout are valid");

    run_retained_write_coexists_with_current_member(kernel, entry,
        layout, runtime_generation);
    run_future_commit_key_cannot_execute_early(kernel, entry,
        layout, runtime_generation);
    run_next_round_consumes_original_commit(kernel, entry,
        layout, runtime_generation);
    run_invalid_retained_descriptor_declines(kernel, entry, layout,
        runtime_generation, current_round - 1U, false, false,
        "a stale retained-write round must decline before mutation");
    run_invalid_retained_descriptor_declines(kernel, entry, layout,
        runtime_generation, current_round + 2U, false, false,
        "a two-round-ahead retained write must decline before mutation");
    run_invalid_retained_descriptor_declines(kernel, entry, layout,
        runtime_generation, current_round + 1U, true, false,
        "a retained write with a different time must decline before mutation");
    run_invalid_retained_descriptor_declines(kernel, entry, layout,
        runtime_generation, current_round + 1U, false, true,
        "a retained write with a different delta must decline before mutation");
}

} // namespace fsim::compiler::test

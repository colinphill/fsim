// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_region_frontier_trusted_entry.hpp"

#include <algorithm>
#include <cstdio>
#include <new>
#include <stdexcept>

namespace fsim::runtime::simir {

namespace {

constexpr std::uint32_t kInvalidFrontierIndex = UINT32_MAX;

[[nodiscard]] bool frontier_count_fits(const std::size_t count) noexcept
{
    return count <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool frontier_alias_count_add(
    const std::size_t additional,
    std::size_t& count) noexcept
{
    if (additional > std::numeric_limits<std::size_t>::max() - count) {
        return false;
    }
    count += additional;
    return true;
}

template <typename T>
[[nodiscard]] bool frontier_alias_byte_extent(
    const std::size_t count,
    std::size_t& bytes) noexcept
{
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        return false;
    }
    bytes = count * sizeof(T);
    return true;
}

[[nodiscard]] bool frontier_alias_range_capacity(
    const RegionFrontierLayoutV2& layout,
    std::size_t& range_count) noexcept
{
    constexpr auto internal_flag = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
    constexpr auto boundary_flag = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::read_only_boundary_port);
    if (layout.signal_slot_count == 0U || layout.signals == nullptr
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)) {
        return false;
    }

    // The thirteen top-level tuples include the frame and both optional
    // pointer slots (which retain their pointer with a zero extent if absent).
    range_count = 13U;
    for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
        const auto& signal = layout.signals[slot];
        std::size_t role_count { };
        if (signal.flags == internal_flag) {
            role_count = 4U;
        } else if (signal.flags == boundary_flag) {
            role_count = 1U;
        } else {
            return false;
        }
        const auto plane_count = static_cast<std::size_t>(signal.plane_count);
        if (plane_count == 0U
            || plane_count > std::numeric_limits<std::size_t>::max()
                / role_count
            || !frontier_alias_count_add(
                plane_count * role_count, range_count)) {
            return false;
        }
    }
    for (std::size_t site = 0U; site < layout.write_site_count; ++site) {
        const auto plane_count
            = static_cast<std::size_t>(layout.write_sites[site].plane_count);
        if (plane_count == 0U
            || !frontier_alias_count_add(plane_count, range_count)) {
            return false;
        }
    }
    return true;
}

template <typename Range>
[[nodiscard]] bool frontier_alias_interval(
    const Range& range,
    std::uint64_t& begin,
    std::uint64_t& end) noexcept
{
    if constexpr (std::numeric_limits<std::uintptr_t>::digits
            > std::numeric_limits<std::uint64_t>::digits
        || std::numeric_limits<std::size_t>::digits
            > std::numeric_limits<std::uint64_t>::digits) {
        return false;
    } else {
        if (range.bytes != 0U && range.address == nullptr) {
            return false;
        }
        begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(range.address));
        const auto byte_count = static_cast<std::uint64_t>(range.bytes);
        if (byte_count > std::numeric_limits<std::uint64_t>::max() - begin) {
            return false;
        }
        end = begin + byte_count;
        return true;
    }
}

template <typename LeftRange, typename RightRange>
[[nodiscard]] bool frontier_alias_ranges_disjoint(
    const LeftRange& left,
    const RightRange& right) noexcept
{
    std::uint64_t left_begin { };
    std::uint64_t left_end { };
    std::uint64_t right_begin { };
    std::uint64_t right_end { };
    if (!frontier_alias_interval(left, left_begin, left_end)
        || !frontier_alias_interval(right, right_begin, right_end)) {
        return false;
    }
    return left_end <= right_begin || right_end <= left_begin;
}

template <typename Range>
[[nodiscard]] bool frontier_alias_index_less(
    const std::vector<Range>& ranges,
    const std::size_t left_index,
    const std::size_t right_index) noexcept
{
    std::uint64_t left_begin { };
    std::uint64_t left_end { };
    std::uint64_t right_begin { };
    std::uint64_t right_end { };
    const bool left_valid = left_index < ranges.size()
        && frontier_alias_interval(
            ranges[left_index], left_begin, left_end);
    const bool right_valid = right_index < ranges.size()
        && frontier_alias_interval(
            ranges[right_index], right_begin, right_end);
    if (!left_valid || !right_valid) {
        return left_index < right_index;
    }
    if (left_begin != right_begin) {
        return left_begin < right_begin;
    }
    if (left_end != right_end) {
        return left_end < right_end;
    }
    if (ranges[left_index].alias_tag != ranges[right_index].alias_tag) {
        return ranges[left_index].alias_tag < ranges[right_index].alias_tag;
    }
    return left_index < right_index;
}

template <typename Range>
void frontier_alias_sift_down(
    std::vector<std::size_t>& indices,
    const std::size_t heap_size,
    const std::size_t root_index,
    const std::vector<Range>& ranges) noexcept
{
    auto root = root_index;
    while (heap_size >= 2U && root <= (heap_size - 2U) / 2U) {
        auto child = root * 2U + 1U;
        if (child + 1U < heap_size
            && frontier_alias_index_less(
                ranges, indices[child], indices[child + 1U])) {
            ++child;
        }
        if (!frontier_alias_index_less(
                ranges, indices[root], indices[child])) {
            return;
        }
        const auto saved = indices[root];
        indices[root] = indices[child];
        indices[child] = saved;
        root = child;
    }
}

template <typename Range>
void frontier_alias_heap_sort(
    std::vector<std::size_t>& indices,
    const std::vector<Range>& ranges) noexcept
{
    const auto count = indices.size();
    for (auto first_root = count / 2U; first_root != 0U;) {
        --first_root;
        frontier_alias_sift_down(
            indices, count, first_root, ranges);
    }
    for (auto heap_size = count; heap_size > 1U;) {
        --heap_size;
        const auto saved = indices[0U];
        indices[0U] = indices[heap_size];
        indices[heap_size] = saved;
        frontier_alias_sift_down(indices, heap_size, 0U, ranges);
    }
}

template <typename LeftRange, typename RightRange>
[[nodiscard]] bool frontier_alias_exception(
    const LeftRange& left,
    const RightRange& right,
    const std::uint64_t left_begin,
    const std::uint64_t left_end,
    const std::uint64_t right_begin,
    const std::uint64_t right_end) noexcept
{
    return left.address == right.address
        && left_begin == right_begin && left_end == right_end
        && left.bytes == right.bytes && left.alias_tag != 0U
        && right.alias_tag != 0U
        && (left.alias_tag >> 1U) != 0U
        && (left.alias_tag >> 1U) == (right.alias_tag >> 1U)
        && (left.alias_tag & 1U) != (right.alias_tag & 1U);
}

[[nodiscard]] bool frontier_alias_confirmation_status(
    const RegionFrontierStatusV2 status) noexcept
{
    switch (status) {
    case RegionFrontierStatusV2::need_scheduler_keys:
    case RegionFrontierStatusV2::yield_before_task:
    case RegionFrontierStatusV2::cut_before_key:
    case RegionFrontierStatusV2::quiescent:
    case RegionFrontierStatusV2::stopped:
    case RegionFrontierStatusV2::boundary_publication:
        return true;
    case RegionFrontierStatusV2::generic_update_batch_ready:
    case RegionFrontierStatusV2::decline_before_mutation:
    case RegionFrontierStatusV2::stale_generation:
        return false;
    }
    return false;
}

[[nodiscard]] bool frontier_add_words(
    const std::size_t current,
    const std::size_t additional,
    std::size_t& result) noexcept
{
    if (additional > std::numeric_limits<std::size_t>::max() - current) {
        return false;
    }
    result = current + additional;
    return true;
}

template <typename T>
[[nodiscard]] bool frontier_workspace_size_add(
    const std::size_t count,
    std::size_t& byte_count) noexcept
{
    if (count == 0U) {
        return true;
    }
    if (alignof(T) > alignof(std::max_align_t)
        || count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        return false;
    }
    const auto slice_bytes = count * sizeof(T);
    std::size_t padded_slice_bytes { };
    if (!frontier_add_words(slice_bytes, alignof(T) - 1U,
            padded_slice_bytes)) {
        return false;
    }
    std::size_t next_byte_count { };
    if (!frontier_add_words(byte_count, padded_slice_bytes,
            next_byte_count)) {
        return false;
    }
    byte_count = next_byte_count;
    return true;
}

struct FrontierWorkspaceSize final {
    template <typename T>
    [[nodiscard]] bool add(const std::size_t count) noexcept
    {
        return frontier_workspace_size_add<T>(count, byte_count);
    }

    std::size_t byte_count { };
};

} // namespace

bool Interpreter::Impl::RegionFrontierComponentRuntime::
    collect_alias_certificate_ranges(
        const RegionFrontierLayoutV2& layout) noexcept
{
    if (!alias_certificate_storage_available || owner == nullptr
        || !frame_initialized || invalidated || !backend
        || !backend->executor || execution_mode
            != RegionFrontierExecutionModeV2::systemverilog_active
        || layout.execution_mode
            != RegionFrontierExecutionModeV2::systemverilog_active
        || !region_frontier_layout_header_valid_v2(layout)
        || !region_frontier_frame_header_valid_v2(frame)) {
        return false;
    }

    const auto pointer_for = [](const auto& values) {
        return values.empty() ? nullptr : values.data();
    };
    if (frame.runtime_generation != runtime_generation
        || frame.bound_runtime_generation != runtime_generation
        || frame.certificate_generation != layout.certificate_generation
        || frame.component_generation != layout.component_generation
        || frame.member_count != members.size()
        || frame.member_count != layout.member_count
        || frame.scheduler_task_count > frame.scheduler_task_capacity
        || frame.scheduler_task_capacity != scheduler_tasks.size()
        || frame.readiness_word_count != ready_words.size()
        || frame.signal_slot_count != planes.size()
        || frame.signal_slot_count != layout.signal_slot_count
        || frame.metadata_count != metadata.size()
        || frame.metadata_count != layout.metadata_count
        || frame.fanout_edge_count != fanout_edges.size()
        || frame.pending_write_capacity != pending_writes.size()
        || frame.staged_event_capacity != staged_events.size()
        || frame.committed_signal_capacity != committed_signals.size()
        || frame.ready_words != pointer_for(ready_words)
        || frame.members != pointer_for(members)
        || frame.scheduler_tasks != pointer_for(scheduler_tasks)
        || frame.planes != pointer_for(planes)
        || frame.metadata != pointer_for(metadata)
        || frame.fanout_edges != pointer_for(fanout_edges)
        || frame.port_planes != pointer_for(port_planes)
        || frame.pending_writes != pointer_for(pending_writes)
        || frame.staged_events != pointer_for(staged_events)
        || frame.committed_signals != pointer_for(committed_signals)
        || (frame.native_frontier_member_dispatches != nullptr
            && frame.native_frontier_member_dispatches
                != &native_member_dispatches)
        || (frame.stop_requested != nullptr
            && frame.stop_requested != &stop_requested)
        || layout.signals == nullptr
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)
        || planes.size() != port_planes.size()
        || pending_writes.empty() || staged_events.empty()
        || committed_signals.empty()) {
        return false;
    }

    std::size_t cursor { };
    const auto append = [&](const void* const address,
                            const std::size_t bytes,
                            const std::uint64_t alias_tag = 0U) {
        if (cursor >= alias_candidate_ranges.size()
            || (bytes != 0U && address == nullptr)) {
            return false;
        }
        alias_candidate_ranges[cursor++] = { address, bytes, alias_tag };
        return true;
    };
    const auto append_extent = [&](const void* const address,
                                   const std::size_t count,
                                   const std::size_t element_size,
                                   const std::uint64_t alias_tag = 0U) {
        if (count != 0U
            && element_size > std::numeric_limits<std::size_t>::max() / count) {
            return false;
        }
        return append(address, count * element_size, alias_tag);
    };

    // Keep the exact order used by the generated checked entry. In particular,
    // scheduler task extent is the live task count rather than vector capacity.
    if (!append(&frame, sizeof(frame))
        || !append_extent(frame.ready_words, frame.readiness_word_count,
            sizeof(*frame.ready_words))
        || !append_extent(frame.members, frame.member_count,
            sizeof(*frame.members))
        || !append_extent(frame.scheduler_tasks, frame.scheduler_task_count,
            sizeof(*frame.scheduler_tasks))
        || !append_extent(frame.planes, frame.signal_slot_count,
            sizeof(*frame.planes))
        || !append_extent(frame.metadata, frame.metadata_count,
            sizeof(*frame.metadata))
        || !append_extent(frame.fanout_edges, frame.fanout_edge_count,
            sizeof(*frame.fanout_edges))
        || !append_extent(frame.port_planes, frame.signal_slot_count,
            sizeof(*frame.port_planes))
        || !append_extent(frame.pending_writes, frame.pending_write_capacity,
            sizeof(*frame.pending_writes))
        || !append_extent(frame.staged_events, frame.staged_event_capacity,
            sizeof(*frame.staged_events))
        || !append_extent(frame.committed_signals,
            frame.committed_signal_capacity, sizeof(*frame.committed_signals))
        || !append(frame.native_frontier_member_dispatches,
            frame.native_frontier_member_dispatches == nullptr
                ? 0U : sizeof(*frame.native_frontier_member_dispatches))
        || !append(frame.stop_requested,
            frame.stop_requested == nullptr ? 0U : sizeof(*frame.stop_requested))) {
        return false;
    }

    constexpr auto internal_flag = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
    constexpr auto boundary_flag = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::read_only_boundary_port);
    for (std::size_t slot = 0U; slot < planes.size(); ++slot) {
        const auto& descriptor = layout.signals[slot];
        const auto& plane = planes[slot];
        if (descriptor.signal_id != plane.signal_id
            || descriptor.owner_process_id != plane.owner_process_id
            || descriptor.value_kind != plane.value_kind
            || descriptor.width != plane.width
            || descriptor.word_count != plane.word_count
            || descriptor.plane_count != plane.plane_count
            || descriptor.flags != plane.flags
            || descriptor.metadata_index != plane.metadata_index
            || port_planes[slot] != &plane
            || !region_frontier_plane_bindings_valid_v2(plane)) {
            return false;
        }
        std::size_t plane_bytes { };
        if (!frontier_alias_byte_extent<std::uint64_t>(
                plane.word_count, plane_bytes)) {
            return false;
        }
        if (descriptor.flags == internal_flag) {
            for (std::size_t value_plane = 0U;
                 value_plane < plane.plane_count; ++value_plane) {
                const auto alias_key = static_cast<std::uint64_t>(slot)
                        * UINT64_C(4)
                    + static_cast<std::uint64_t>(value_plane) + UINT64_C(1);
                const auto stored_alias_tag = alias_key * UINT64_C(2);
                const auto owner_alias_tag = stored_alias_tag + UINT64_C(1);
                if (!append(plane.current_planes[value_plane], plane_bytes)
                    || !append(plane.previous_planes[value_plane], plane_bytes)
                    || !append(plane.stored_planes[value_plane], plane_bytes,
                        stored_alias_tag)
                    || !append(plane.owner_planes[value_plane], plane_bytes,
                        owner_alias_tag)) {
                    return false;
                }
            }
        } else if (descriptor.flags == boundary_flag) {
            for (std::size_t value_plane = 0U;
                 value_plane < plane.plane_count; ++value_plane) {
                if (!append(plane.boundary_planes[value_plane], plane_bytes)) {
                    return false;
                }
            }
        } else {
            return false;
        }
    }

    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        if (site.pending_slot >= pending_writes.size()
            || site.signal_slot >= planes.size()
            || site.member_index >= members.size()) {
            return false;
        }
        const auto& write = pending_writes[site.pending_slot];
        const auto& signal = layout.signals[site.signal_slot];
        const auto expected_flags
            = (signal.flags & internal_flag) != 0U
            ? static_cast<std::uint32_t>(pending_internal_target)
            : static_cast<std::uint32_t>(pending_boundary_target);
        if (write.member_index != site.member_index
            || write.signal_slot != site.signal_slot
            || write.source_instruction != site.source_instruction
            || write.update_kind != site.update_kind
            || (write.flags & (static_cast<std::uint32_t>(
                    RegionFrontierPendingWriteFlagsV2::pending_internal_target)
                    | static_cast<std::uint32_t>(
                        RegionFrontierPendingWriteFlagsV2::pending_boundary_target)))
                != expected_flags
            || write.value_kind != site.value_kind
            || write.width != site.width
            || write.word_count != site.word_count
            || write.plane_count != site.plane_count
            || !region_frontier_pending_write_bindings_valid_v2(write)) {
            return false;
        }
        std::size_t plane_bytes { };
        if (!frontier_alias_byte_extent<std::uint64_t>(
                write.word_count, plane_bytes)) {
            return false;
        }
        for (std::size_t value_plane = 0U;
             value_plane < write.plane_count; ++value_plane) {
            if (!append(write.value_planes[value_plane], plane_bytes)) {
                return false;
            }
        }
    }
    return cursor == alias_candidate_ranges.size();
}

bool Interpreter::Impl::RegionFrontierComponentRuntime::
    prove_alias_geometry_sorted() noexcept
{
    ++alias_sorted_proof_attempts;
    const auto reject = [this]() noexcept {
        ++alias_sorted_proof_failures;
        return false;
    };
    constexpr std::size_t frame_range_count = 13U;
    if (!alias_certificate_storage_available
        || !alias_certificate_pending_confirmation
        || alias_certificate_pending_task_count != frame.scheduler_task_count
        || frame.staged_event_count != 0U
        || alias_candidate_ranges.size() < frame_range_count
        || alias_candidate_ranges.size() != alias_certificate_ranges.size()
        || alias_sorted_indices.size()
            != alias_candidate_ranges.size() - frame_range_count) {
        return reject();
    }

    // The staged inventory must still be the exact pre-entry snapshot before
    // it can authorize the private entry. The public checked entry remains the
    // fallback for every unsupported or malformed geometry.
    for (std::size_t index = 0U;
         index < alias_candidate_ranges.size(); ++index) {
        if (alias_candidate_ranges[index] != alias_certificate_ranges[index]) {
            return reject();
        }
    }

    const auto range_count = alias_candidate_ranges.size();
    for (std::size_t index = 0U; index < range_count; ++index) {
        std::uint64_t begin { };
        std::uint64_t end { };
        const auto& range = alias_candidate_ranges[index];
        if (!frontier_alias_interval(range, begin, end)) {
            return reject();
        }
        if (index >= frame_range_count && range.bytes == 0U) {
            // Every signal and pending plane is nonempty in a valid V2 layout.
            // Falling back keeps any custom zero-extent acceptance with the
            // checked entry, whose frame-point semantics are different.
            return reject();
        }
    }

    const auto data_range_count = range_count - frame_range_count;
    if (data_range_count == 0U
        || data_range_count
            > std::numeric_limits<std::size_t>::max() / sizeof(std::size_t)) {
        return reject();
    }
    const RegionFrontierAliasRange scratch_range {
        alias_sorted_indices.data(), data_range_count * sizeof(std::size_t), 0U
    };
    std::uint64_t scratch_begin { };
    std::uint64_t scratch_end { };
    if (!frontier_alias_interval(scratch_range, scratch_begin, scratch_end)) {
        return reject();
    }
    for (const auto& range : alias_candidate_ranges) {
        if (!frontier_alias_ranges_disjoint(scratch_range, range)) {
            return reject();
        }
    }

    for (std::size_t data_position = 0U;
         data_position < data_range_count; ++data_position) {
        const auto data_index = frame_range_count + data_position;
        alias_sorted_indices[data_position] = data_index;
        const auto& data_range = alias_candidate_ranges[data_index];
        for (std::size_t frame_index = 0U;
             frame_index < frame_range_count; ++frame_index) {
            if (!frontier_alias_ranges_disjoint(
                    data_range, alias_candidate_ranges[frame_index])) {
                return reject();
            }
        }
    }

    // Sort only indices into the already-collected tuples. The iterative heap
    // sort uses no allocation or graph-sized stack storage.
    frontier_alias_heap_sort(alias_sorted_indices, alias_candidate_ranges);
    for (std::size_t first_position = 0U;
         first_position < data_range_count;) {
        const auto first_index = alias_sorted_indices[first_position];
        const auto& first = alias_candidate_ranges[first_index];
        std::uint64_t first_begin { };
        std::uint64_t first_end { };
        std::uint64_t cluster_end { };
        if (!frontier_alias_interval(first, first_begin, first_end)) {
            return reject();
        }
        cluster_end = first_end;

        auto cluster_end_position = first_position + 1U;
        while (cluster_end_position < data_range_count) {
            const auto next_index
                = alias_sorted_indices[cluster_end_position];
            const auto& next = alias_candidate_ranges[next_index];
            std::uint64_t next_begin { };
            std::uint64_t next_end { };
            if (!frontier_alias_interval(next, next_begin, next_end)) {
                return reject();
            }
            if (next_begin >= cluster_end) {
                break;
            }
            cluster_end = std::max(cluster_end, next_end);
            ++cluster_end_position;
        }

        const auto cluster_size = cluster_end_position - first_position;
        if (cluster_size > 1U) {
            if (cluster_size != 2U) {
                return reject();
            }
            const auto second_index = alias_sorted_indices[first_position + 1U];
            const auto& second = alias_candidate_ranges[second_index];
            std::uint64_t second_begin { };
            std::uint64_t second_end { };
            if (!frontier_alias_interval(second, second_begin, second_end)
                || !frontier_alias_exception(first, second,
                    first_begin, first_end, second_begin, second_end)) {
                return reject();
            }
        }
        first_position = cluster_end_position;
    }

    ++alias_sorted_proof_successes;
    return true;
}

Interpreter::Impl::RegionFrontierAliasCertificateContext
Interpreter::Impl::RegionFrontierComponentRuntime::
    make_alias_certificate_context(
        const RegionFrontierBackendEntry& backend_entry,
        const RegionFrontierStepEntryV2 checked_entry,
    const RegionFrontierStepEntryV2 trusted_entry) const noexcept
{
    RegionFrontierAliasCertificateContext context;
    context.owner = owner;
    context.component = component;
    context.backend = &backend_entry;
    context.executor = backend_entry.executor.get();
    context.layout = backend_entry.executor
        ? &backend_entry.executor->layout() : nullptr;
    context.frame = &frame;
    context.checked_entry = checked_entry;
    context.trusted_entry = trusted_entry;
    context.runtime_generation = runtime_generation;
    context.frame_runtime_generation = frame.runtime_generation;
    context.bound_runtime_generation = frame.bound_runtime_generation;
    context.certificate_generation = frame.certificate_generation;
    context.component_generation = frame.component_generation;
    context.execution_mode = execution_mode;
    return context;
}

void Interpreter::Impl::RegionFrontierComponentRuntime::
    record_alias_certificate_miss(
        const RegionFrontierAliasMissReason reason,
        const std::size_t tuple_index,
        const std::uint32_t context_mask,
        const std::size_t other_tuple_index) noexcept
{
    if (owner == nullptr || !owner->systemverilog_wave_profile_enabled) {
        return;
    }
    ++owner->systemverilog_wave_profile_alias_misses[
        static_cast<std::size_t>(reason)];
    if (reason == RegionFrontierAliasMissReason::unprimed
        || owner->systemverilog_wave_profile_alias_miss_rows >= 16U) {
        return;
    }
    ++owner->systemverilog_wave_profile_alias_miss_rows;
    constexpr std::array<const char*, 8U> names {
        "unprimed", "context", "collector", "task-extent-only",
        "task-extent-with-other", "pointer", "extent", "alias-tag",
    };
    const auto before = tuple_index < alias_certificate_ranges.size()
        ? alias_certificate_ranges[tuple_index] : RegionFrontierAliasRange { };
    const auto after = tuple_index < alias_candidate_ranges.size()
        ? alias_candidate_ranges[tuple_index] : RegionFrontierAliasRange { };
    const auto old_task_bytes = alias_certificate_ranges.size() > 3U
        ? alias_certificate_ranges[3U].bytes : 0U;
    std::fprintf(stderr,
        "fsim-profile: sv-frontier-alias-miss component=%zu reason=%s "
        "tuple_index=%zu other_tuple_index=%zu context_mask=%u "
        "old_address=%p new_address=%p "
        "old_bytes=%zu new_bytes=%zu old_tag=%llu new_tag=%llu "
        "cached_task_bytes=%zu actual_task_count=%u "
        "cached_runtime_generation=%llu runtime_generation=%llu\n",
        component, names[static_cast<std::size_t>(reason)], tuple_index,
        other_tuple_index, context_mask, before.address, after.address, before.bytes, after.bytes,
        static_cast<unsigned long long>(before.alias_tag),
        static_cast<unsigned long long>(after.alias_tag), old_task_bytes,
        frame.scheduler_task_count,
        static_cast<unsigned long long>(
            alias_certificate_context.runtime_generation),
        static_cast<unsigned long long>(runtime_generation));
}

bool Interpreter::Impl::RegionFrontierComponentRuntime::
    alias_certificate_common_ranges_match(
        bool& task_extent_changed,
        std::size_t& first_other_mismatch,
        bool& task_address_changed,
        bool& task_alias_tag_changed) const noexcept
{
    constexpr std::size_t task_range_index = 3U;
    task_extent_changed = false;
    first_other_mismatch = std::numeric_limits<std::size_t>::max();
    task_address_changed = false;
    task_alias_tag_changed = false;
    if (alias_candidate_ranges.size() != alias_certificate_ranges.size()) {
        return false;
    }

    for (std::size_t index = 0U;
         index < alias_candidate_ranges.size(); ++index) {
        const auto& candidate = alias_candidate_ranges[index];
        const auto& certified = alias_certificate_ranges[index];
        if (index == task_range_index) {
            task_address_changed = candidate.address != certified.address;
            task_extent_changed = candidate.bytes != certified.bytes;
            task_alias_tag_changed
                = candidate.alias_tag != certified.alias_tag;
            continue;
        }
        if (candidate != certified
            && first_other_mismatch
                == std::numeric_limits<std::size_t>::max()) {
            first_other_mismatch = index;
        }
    }
    return true;
}

bool Interpreter::Impl::RegionFrontierComponentRuntime::
    alias_certificate_matches(
        const RegionFrontierAliasCertificateContext& context) noexcept
{
    if (!alias_certificate_storage_available || !alias_certificate_valid) {
        record_alias_certificate_miss(RegionFrontierAliasMissReason::unprimed);
        clear_alias_certificate();
        return false;
    }
    if (context.layout == nullptr || alias_certificate_context != context) {
        const auto& previous = alias_certificate_context;
        const auto differs = [](const bool value, const unsigned bit) {
            return value ? UINT32_C(1) << bit : UINT32_C(0);
        };
        const auto context_mask = differs(previous.owner != context.owner, 0U)
            | differs(previous.component != context.component, 1U)
            | differs(previous.backend != context.backend, 2U)
            | differs(previous.executor != context.executor, 3U)
            | differs(previous.layout != context.layout, 4U)
            | differs(previous.frame != context.frame, 5U)
            | differs(previous.checked_entry != context.checked_entry, 6U)
            | differs(previous.trusted_entry != context.trusted_entry, 7U)
            | differs(previous.runtime_generation != context.runtime_generation, 8U)
            | differs(previous.frame_runtime_generation
                != context.frame_runtime_generation, 9U)
            | differs(previous.bound_runtime_generation
                != context.bound_runtime_generation, 10U)
            | differs(previous.certificate_generation
                != context.certificate_generation, 11U)
            | differs(previous.component_generation
                != context.component_generation, 12U)
            | differs(previous.execution_mode != context.execution_mode, 13U);
        record_alias_certificate_miss(RegionFrontierAliasMissReason::context,
            std::numeric_limits<std::size_t>::max(), context_mask);
        clear_alias_certificate();
        return false;
    }
    if (!collect_alias_certificate_ranges(*context.layout)
        || alias_candidate_ranges.size() != alias_certificate_ranges.size()) {
        record_alias_certificate_miss(RegionFrontierAliasMissReason::collector);
        clear_alias_certificate();
        return false;
    }

    bool task_extent_changed { };
    std::size_t first_other_mismatch { };
    bool task_address_changed { };
    bool task_alias_tag_changed { };
    if (!alias_certificate_common_ranges_match(task_extent_changed,
            first_other_mismatch, task_address_changed,
            task_alias_tag_changed)) {
        record_alias_certificate_miss(RegionFrontierAliasMissReason::collector);
        clear_alias_certificate();
        return false;
    }
    if (first_other_mismatch != std::numeric_limits<std::size_t>::max()
        || task_address_changed || task_alias_tag_changed) {
        auto reason = RegionFrontierAliasMissReason::task_extent_with_other;
        std::size_t tuple_index { 3U };
        auto other_tuple_index = first_other_mismatch;
        if (!task_extent_changed || first_other_mismatch
                == std::numeric_limits<std::size_t>::max()) {
            other_tuple_index = std::numeric_limits<std::size_t>::max();
            if (task_address_changed) {
                reason = RegionFrontierAliasMissReason::pointer;
                tuple_index = 3U;
            } else if (task_alias_tag_changed) {
                reason = RegionFrontierAliasMissReason::alias_tag;
                tuple_index = 3U;
            } else {
                tuple_index = first_other_mismatch;
                const auto& before = alias_certificate_ranges[tuple_index];
                const auto& after = alias_candidate_ranges[tuple_index];
                reason = before.address != after.address
                    ? RegionFrontierAliasMissReason::pointer
                    : before.bytes != after.bytes
                        ? RegionFrontierAliasMissReason::extent
                        : RegionFrontierAliasMissReason::alias_tag;
            }
        }
        record_alias_certificate_miss(reason, tuple_index, 0U,
            other_tuple_index);
        clear_alias_certificate();
        return false;
    }

    const auto task_count
        = static_cast<std::size_t>(frame.scheduler_task_count);
    if (task_count >= alias_certificate_task_count_valid.size()
        || alias_certificate_task_count_valid[task_count] == 0U) {
        // This is an unconfirmed exact count key, not proof that the byte
        // extent changed.
        record_alias_certificate_miss(
            RegionFrontierAliasMissReason::task_extent_only, 3U);
        return false;
    }
    return true;
}

bool Interpreter::Impl::RegionFrontierComponentRuntime::
    stage_alias_certificate(
        const RegionFrontierAliasCertificateContext& context) noexcept
{
    if (!alias_certificate_storage_available || context.layout == nullptr
        || frame.staged_event_count != 0U
        || !collect_alias_certificate_ranges(*context.layout)
        || alias_candidate_ranges.size() != alias_certificate_ranges.size()) {
        clear_alias_certificate();
        return false;
    }

    bool preserve_task_counts = alias_certificate_valid
        && alias_certificate_context == context;
    if (preserve_task_counts) {
        bool task_extent_changed { };
        std::size_t first_other_mismatch { };
        bool task_address_changed { };
        bool task_alias_tag_changed { };
        preserve_task_counts = alias_certificate_common_ranges_match(
            task_extent_changed, first_other_mismatch,
            task_address_changed, task_alias_tag_changed)
            && first_other_mismatch
                == std::numeric_limits<std::size_t>::max()
            && !task_address_changed && !task_alias_tag_changed;
    }
    if (!preserve_task_counts) {
        clear_alias_certificate();
        alias_certificate_context = context;
    }
    std::ranges::copy(alias_candidate_ranges, alias_certificate_ranges.begin());
    alias_certificate_pending_task_count
        = static_cast<std::size_t>(frame.scheduler_task_count);
    alias_certificate_pending_confirmation = true;
    return true;
}

bool Interpreter::Impl::RegionFrontierComponentRuntime::
    confirm_alias_certificate(
        const RegionFrontierAliasCertificateContext& context,
        const RegionFrontierStatusV2 status) noexcept
{
    if (!alias_certificate_storage_available
        || !alias_certificate_pending_confirmation
        || alias_certificate_context != context
        || !frontier_alias_confirmation_status(status)
        || context.layout == nullptr
        || frame.scheduler_task_count
            != alias_certificate_pending_task_count
        || alias_certificate_pending_task_count
            >= alias_certificate_task_count_valid.size()
        || !collect_alias_certificate_ranges(*context.layout)
        || alias_candidate_ranges.size() != alias_certificate_ranges.size()) {
        clear_alias_certificate();
        return false;
    }
    for (std::size_t index = 0U;
         index < alias_candidate_ranges.size(); ++index) {
        if (alias_candidate_ranges[index] != alias_certificate_ranges[index]) {
            clear_alias_certificate();
            return false;
        }
    }
    alias_certificate_task_count_valid[
        alias_certificate_pending_task_count] = 1U;
    alias_certificate_pending_confirmation = false;
    alias_certificate_valid = true;
    return true;
}

void Interpreter::Impl::RegionFrontierComponentRuntime::
    clear_alias_certificate() noexcept
{
    alias_certificate_task_count_valid.fill(0U);
    alias_certificate_pending_task_count = 0U;
    alias_certificate_valid = false;
    alias_certificate_pending_confirmation = false;
    alias_certificate_context = { };
}

void Interpreter::Impl::advance_process_executor_generation(
    ProcessState& process) noexcept
{
    auto& generation = process.executor_lifecycle_generation;
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        // Zero is permanently reserved as an exhausted generation. A cached
        // optional interface can never become valid again after wraparound.
        generation = 0U;
    } else if (generation != 0U) {
        ++generation;
    }
}

bool Interpreter::Impl::preflight_region_frontier_component_layout(
    const RegionRuntimeSnapshot& snapshot,
    const std::size_t component,
    const RegionConeActivationKernel& kernel,
    const RegionFrontierLayoutV2& layout,
    const char** rejection_reason) const
{
    if (rejection_reason != nullptr) {
        *rejection_reason = "unclassified";
    }
    const auto reject = [rejection_reason](const char* reason) {
        if (rejection_reason != nullptr) {
            *rejection_reason = reason;
        }
        return false;
    };

    if (!region_frontier_layout_prefix_valid_v2(
            layout.abi_version, layout.struct_size)
        || !region_frontier_layout_header_valid_v2(layout)) {
        return reject("invalid-layout-header-or-mode");
    }

    const bool generic_mode = layout.execution_mode
        == RegionFrontierExecutionModeV2::generic_deferred_update;
    const bool systemverilog_mode = layout.execution_mode
        == RegionFrontierExecutionModeV2::systemverilog_active;
    if ((!generic_mode && !systemverilog_mode)
        || layout.reserved0 != 0U || layout.reserved_capacity != 0U
        || (generic_mode
            && (kernel.program.scheduling_domain
                    != ProcessSchedulingDomain::generic
                || layout.metadata_count != 0U))
        || (systemverilog_mode
            && kernel.program.scheduling_domain
                != ProcessSchedulingDomain::systemverilog)
        || !frontier_count_fits(kernel.members.size())
        || layout.member_count != kernel.members.size()) {
        return reject("invalid-layout-header-or-mode");
    }

    if (systemverilog_mode
        && (component >= snapshot.authoritative_state_by_component.size()
            || component >= snapshot.local_wave_state_by_component.size()
            || !snapshot.authoritative_state_by_component[component]
            || !snapshot.local_wave_state_by_component[component])) {
        return reject("missing-systemverilog-state");
    }
    const RegionAuthoritativeComponentState* authoritative { };
    if (systemverilog_mode) {
        authoritative = snapshot.authoritative_state_by_component[component].get();
        if (!authoritative->valid()
            || authoritative->generation() != snapshot.generation) {
            return reject("authoritative-state-invalid-or-stale");
        }
    }

    // Validate every count and pointer before following provider-owned arrays.
    if ((layout.member_count != 0U
            && (layout.members == nullptr
                || layout.max_member_write_counts == nullptr
                || layout.max_member_staged_event_counts == nullptr))
        || layout.readiness_word_count
            != kernel.members.size() / 64U
                + static_cast<std::size_t>(kernel.members.size() % 64U != 0U)
        || layout.signal_slot_count == 0U || layout.signals == nullptr
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)
        || (layout.fanout_edge_count != 0U && layout.fanout_edges == nullptr)
        || layout.pending_write_capacity == 0U
        || layout.staged_event_capacity == 0U
        || layout.committed_signal_capacity
            != layout.pending_write_capacity) {
        return reject("invalid-layout-capacity-or-pointer");
    }
    const auto event_capacity = std::min(
        layout.staged_event_capacity,
        static_cast<std::uint32_t>(
            RegionFrontierComponentRuntime::scheduler_task_capacity));
    std::uint32_t minimum_event_capacity
        = layout.max_commit_fanout_events;
    for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
        minimum_event_capacity = std::max(minimum_event_capacity,
            layout.max_member_staged_event_counts[member]);
    }
    if (event_capacity < minimum_event_capacity) {
        return reject("invalid-layout-capacity-or-pointer");
    }

    const auto layout_signal_index = [&](const SignalId signal)
        -> std::size_t {
        std::size_t first { };
        std::size_t last = layout.signal_slot_count;
        while (first < last) {
            const auto middle = first + (last - first) / 2U;
            if (layout.signals[middle].signal_id < signal) {
                first = middle + 1U;
            } else {
                last = middle;
            }
        }
        return first < layout.signal_slot_count
                && layout.signals[first].signal_id == signal
            ? first : layout.signal_slot_count;
    };
    const auto kernel_has_signal = [&](const SignalId signal) {
        return std::ranges::any_of(kernel.inputs,
                   [signal](const RegionConeKernelInput& input) {
                       return input.signal == signal;
                   })
            || std::ranges::any_of(kernel.outputs,
                   [signal](const RegionConeOutputBinding& output) {
                       return output.signal == signal;
                   });
    };
    if (layout.signals[0U].signal_id >= snapshot.graph.signals().size()) {
        return reject("signal-slot-order-or-index-invalid");
    }
    for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
        const auto signal = layout.signals[slot].signal_id;
        if (signal >= snapshot.graph.signals().size()
            || (slot != 0U
                && layout.signals[slot - 1U].signal_id >= signal)
            || !kernel_has_signal(signal)) {
            return reject("signal-slot-order-or-index-invalid");
        }
    }
    for (const auto& input : kernel.inputs) {
        if (layout_signal_index(input.signal) == layout.signal_slot_count) {
            return reject("signal-slot-count-mismatch");
        }
    }
    for (const auto& output : kernel.outputs) {
        if (layout_signal_index(output.signal) == layout.signal_slot_count) {
            return reject("signal-slot-count-mismatch");
        }
    }

    const auto graph_signals = snapshot.graph.signals();
    std::size_t writable_signal_count { };
    for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
        const auto& layout_signal = layout.signals[slot];
        const auto& graph_signal
            = graph_signals[layout_signal.signal_id].descriptor;
        if (layout_signal.width == 0U
            || layout_signal.width != graph_signal.width
            || layout_signal.word_count
                != static_cast<std::size_t>(layout_signal.width) / 64U
                    + static_cast<std::size_t>(
                        layout_signal.width % 64U != 0U)
            || (graph_signal.value_kind != ValueKind::logic4
                && graph_signal.value_kind != ValueKind::logic9)
            || layout_signal.value_kind
                != region_frontier_value_kind_v2(graph_signal.value_kind)
            || layout_signal.plane_count
                != region_frontier_required_plane_count_v2(
                    layout_signal.value_kind)
            || !region_frontier_value_shape_valid_v2(
                layout_signal.value_kind, layout_signal.width,
                layout_signal.word_count, layout_signal.plane_count)) {
            return reject("signal-descriptor-shape-invalid");
        }
        const auto signal = static_cast<SignalId>(layout_signal.signal_id);
        const bool internal = std::ranges::find(
            kernel.internal_signals, signal) != kernel.internal_signals.end();
        const auto expected_flags = generic_mode
            ? static_cast<std::uint32_t>(read_only_boundary_port)
            : internal
                ? static_cast<std::uint32_t>(certified_internal_single_owner)
                : static_cast<std::uint32_t>(read_only_boundary_port);
        if (layout_signal.flags != expected_flags
            || (generic_mode
                && layout_signal.metadata_index != kInvalidFrontierIndex)) {
            return reject("signal-access-flags-invalid");
        }
        if (generic_mode) {
            if (internal) {
                const auto output = std::ranges::find(
                    kernel.outputs, signal,
                    &RegionConeOutputBinding::signal);
                if (output == kernel.outputs.end()
                    || std::ranges::count(kernel.outputs, signal,
                           &RegionConeOutputBinding::signal) != 1
                    || output->owner != layout_signal.owner_process_id
                    || output->offset != 0U
                    || output->width != layout_signal.width
                    || output->value_kind != graph_signal.value_kind
                    || output->domain != SignalUpdateDomain::generic
                    || output->update_kind != RegionUpdateKind::generic) {
                    return reject("generic-internal-output-binding-invalid");
                }
            } else if (layout_signal.owner_process_id
                != kInvalidFrontierIndex) {
                return reject("generic-boundary-owner-invalid");
            }
            continue;
        }
        if (internal) {
            const auto output = std::ranges::find(
                kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            if (output == kernel.outputs.end()
                || output->owner != layout_signal.owner_process_id
                || output->offset != 0U
                || output->width != layout_signal.width
                || output->value_kind != graph_signal.value_kind
                || output->domain
                    != SignalUpdateDomain::systemverilog_active
                || output->update_kind
                    != RegionUpdateKind::systemverilog_active
                || layout_signal.metadata_index != writable_signal_count
                || layout_signal.metadata_index >= layout.metadata_count) {
                return reject("systemverilog-internal-output-binding-invalid");
            }
            ++writable_signal_count;
        } else if (layout_signal.metadata_index != kInvalidFrontierIndex
            || layout_signal.owner_process_id != kInvalidFrontierIndex) {
            return reject("systemverilog-boundary-metadata-invalid");
        }
    }
    if ((generic_mode
            && (writable_signal_count != 0U || layout.metadata_count != 0U))
        || (systemverilog_mode
            && (writable_signal_count == 0U
                || writable_signal_count != layout.metadata_count))) {
        return reject("metadata-count-or-writable-signal-mismatch");
    }

    const auto graph_processes = snapshot.graph.processes();
    for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
        const auto process = kernel.members[member].process;
        if (layout.members[member].process_id != process
            || process >= processes.size()
            || process >= graph_processes.size()
            || !kernel.members[member].all_registers_definitely_defined) {
            return reject("member-index-or-register-definition-invalid");
        }
        if (!generic_mode) {
            continue;
        }
        const auto& graph_process = graph_processes[process];
        const auto view = processes.program_view(process);
        if (!graph_process.pure || graph_process.dependencies_unknown
            || graph_process.scheduling_domain
                != ProcessSchedulingDomain::generic
            || graph_process.update_kind != RegionUpdateKind::generic
            || view.scheduling_domain() != ProcessSchedulingDomain::generic
            || kernel.members[member].final_debug_state
            || std::ranges::any_of(
                kernel.members[member].register_bindings,
                [&](const RegionConeKernelRegisterBinding& binding) {
                    return (binding.value_kind != ValueKind::logic4
                            && binding.value_kind != ValueKind::logic9)
                        || binding.activation_register
                            >= kernel.program.register_count
                        || (binding.defined && binding.width == 0U);
                })) {
            return reject("generic-member-certification-invalid");
        }
    }

    std::size_t generic_snapshot_word_count { };
    if (generic_mode) {
        for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
            const auto& descriptor = layout.signals[slot];
            const auto words = static_cast<std::size_t>(descriptor.word_count);
            const auto planes = static_cast<std::size_t>(descriptor.plane_count);
            std::size_t next_word_count { };
            if (planes == 0U
                || words > std::numeric_limits<std::size_t>::max() / planes
                || !frontier_add_words(generic_snapshot_word_count,
                    words * planes, next_word_count)) {
                return reject("generic-snapshot-size-overflow");
            }
            generic_snapshot_word_count = next_word_count;
        }
    }

    if (layout.write_site_count == 0U
        || layout.write_site_count != layout.pending_write_capacity) {
        return reject("write-site-index-or-pending-slot-invalid");
    }
    std::size_t pending_plane_word_capacity { };
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        if (site.member_index >= layout.member_count
            || site.signal_slot >= layout.signal_slot_count
            || site.pending_slot >= layout.pending_write_capacity) {
            return reject("write-site-index-or-pending-slot-invalid");
        }
        for (std::size_t prior = 0U; prior < site_index; ++prior) {
            if (layout.write_sites[prior].pending_slot == site.pending_slot) {
                return reject("write-site-index-or-pending-slot-invalid");
            }
        }
        const auto& site_signal = layout.signals[site.signal_slot];
        const auto process = static_cast<ProcessId>(
            layout.members[site.member_index].process_id);
        const auto signal = static_cast<SignalId>(site_signal.signal_id);
        const auto output = std::ranges::find_if(kernel.outputs,
            [&](const RegionConeOutputBinding& candidate) {
                return candidate.owner == process
                    && candidate.signal == signal
                    && candidate.source_instruction
                        == site.source_instruction;
            });
        const bool internal_site
            = (site_signal.flags & certified_internal_single_owner) != 0U;
        const auto expected_event = generic_mode
            ? RegionFrontierEventKindV2::generic_deferred_update
            : internal_site ? RegionFrontierEventKindV2::internal_commit
                             : RegionFrontierEventKindV2::boundary_commit;
        const bool partial_boundary_site = !generic_mode && !internal_site;
        const auto output_signal_width = output != kernel.outputs.end()
            ? output->signal_width != 0U
                ? output->signal_width : output->width
            : 0U;
        const bool site_width_matches_signal = partial_boundary_site
            ? site.width != 0U && site.width <= site_signal.width
            : site.width == site_signal.width
                && site.word_count == site_signal.word_count;
        const bool output_range_matches_signal
            = output != kernel.outputs.end()
            && output_signal_width == site_signal.width
            && output->offset <= site_signal.width
            && output->width <= site_signal.width - output->offset;
        const bool output_shape_matches_site
            = output != kernel.outputs.end()
            && output->width == site.width
            && (partial_boundary_site
                ? output_range_matches_signal
                : output->offset == 0U
                    && output->width == site_signal.width);
        if (!site_width_matches_signal
            || site.value_kind != site_signal.value_kind
            || site.plane_count != site_signal.plane_count
            || !region_frontier_value_shape_valid_v2(site.value_kind,
                site.width, site.word_count, site.plane_count)
            || site.update_kind != static_cast<std::uint32_t>(generic_mode
                    ? RegionUpdateKind::generic
                    : RegionUpdateKind::systemverilog_active)
            || site.event_kind != static_cast<std::uint32_t>(expected_event)
            || !output_shape_matches_site
            || output->value_kind
                != (site.value_kind == RegionFrontierValueKindV2::logic4
                    ? ValueKind::logic4 : ValueKind::logic9)
            || output->domain != (generic_mode
                    ? SignalUpdateDomain::generic
                    : SignalUpdateDomain::systemverilog_active)
            || output->update_kind != (generic_mode
                    ? RegionUpdateKind::generic
                    : RegionUpdateKind::systemverilog_active)) {
            return reject("write-site-descriptor-or-output-binding-invalid");
        }
        if (site.plane_count == 0U
            || static_cast<std::size_t>(site.word_count)
                > std::numeric_limits<std::size_t>::max() / site.plane_count) {
            return reject("write-site-plane-word-overflow");
        }
        std::size_t next_word_count { };
        if (!frontier_add_words(pending_plane_word_capacity,
                static_cast<std::size_t>(site.word_count) * site.plane_count,
                next_word_count)) {
            return reject("pending-plane-word-count-overflow");
        }
        pending_plane_word_capacity = next_word_count;
    }

    for (std::size_t edge_index = 0U;
         edge_index < layout.fanout_edge_count; ++edge_index) {
        const auto& edge = layout.fanout_edges[edge_index];
        if (edge.signal_slot >= layout.signal_slot_count
            || edge.member_index >= kernel.members.size()) {
            return reject("fanout-edge-index-invalid");
        }
        if (generic_mode) {
            continue;
        }
        const auto signal = layout.signals[edge.signal_slot].signal_id;
        if (signal >= snapshot.prepared_successor_by_signal.size()) {
            return reject("prepared-successor-signal-out-of-range");
        }
        const auto& mapping = snapshot.prepared_successor_by_signal[signal];
        if (mapping.generation != snapshot.generation
            || mapping.component != component
            || mapping.reader_offset > snapshot.prepared_successor_readers.size()
            || mapping.reader_count
                > snapshot.prepared_successor_readers.size()
                    - mapping.reader_offset) {
            return reject("prepared-successor-mapping-invalid");
        }
        const auto process = kernel.members[edge.member_index].process;
        const auto readers
            = std::span<const RegionPreparedSuccessorReaderBinding> {
                snapshot.prepared_successor_readers }
                  .subspan(mapping.reader_offset, mapping.reader_count);
        const auto reader = std::ranges::find(readers, process,
            &RegionPreparedSuccessorReaderBinding::process);
        if (reader == readers.end() || reader->static_trigger_mask == 0U) {
            return reject("prepared-successor-reader-or-trigger-mask-missing");
        }
    }

    std::size_t authoritative_plane_word_capacity { };
    if (authoritative != nullptr) {
        for (const auto& role_counts
             : authoritative->values().component_plane_word_counts()) {
            for (const auto word_count : role_counts) {
                std::size_t next_word_count { };
                if (!frontier_add_words(authoritative_plane_word_capacity,
                        word_count, next_word_count)) {
                    return reject("authoritative-plane-word-count-overflow");
                }
                authoritative_plane_word_capacity = next_word_count;
            }
        }
    }

    FrontierWorkspaceSize workspace_size;
    if (!workspace_size.add<std::uint64_t>(layout.readiness_word_count)
        || !workspace_size.add<RegionFrontierMemberV2>(layout.member_count)
        || !workspace_size.add<RegionFrontierSchedulerTaskV2>(
            RegionFrontierComponentRuntime::scheduler_task_capacity)
        || !workspace_size.add<SchedulerBatchFrontierEntry>(
            RegionFrontierComponentRuntime::scheduler_task_capacity)
        || !workspace_size.add<RegionFrontierPlaneV2>(layout.signal_slot_count)
        || !workspace_size.add<RegionFrontierSignalMetadataV2>(
            layout.metadata_count)
        || !workspace_size.add<RegionFrontierFanoutEdgeV2>(
            layout.fanout_edge_count)
        || !workspace_size.add<const RegionFrontierPlaneV2*>(
            layout.signal_slot_count)
        || !workspace_size.add<RegionFrontierPendingWriteV2>(
            layout.pending_write_capacity)
        || !workspace_size.add<RegionFrontierStagedEventV2>(event_capacity)
        || !workspace_size.add<RegionFrontierCommittedSignalV2>(
            layout.committed_signal_capacity)
        || !workspace_size.add<std::size_t>(
            generic_mode ? layout.signal_slot_count : 0U)
        || !workspace_size.add<std::uint64_t>(generic_snapshot_word_count)
        || !workspace_size.add<std::size_t>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<std::size_t>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<std::uint64_t>(
            generic_mode ? layout.readiness_word_count : 0U)
        || !workspace_size.add<RegionFrontierComponentWorkspace::
            GenericQueuedMember>(generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<RegionFrontierComponentWorkspace::
            GenericParkedExecutorBinding>(
                generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<ProcessId>(generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<std::uint32_t>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<AuthoritativeSignalPlanes::FrontierWriteBinding>(
            writable_signal_count)
        || !workspace_size.add<std::size_t>(layout.pending_write_capacity)
        || !workspace_size.add<std::uint64_t>(pending_plane_word_capacity)
        || !workspace_size.add<SystemVerilogCompactBatchMember>(event_capacity)
        || !workspace_size.add<std::uint64_t>(event_capacity)
        || !workspace_size.add<std::uint64_t>(
            authoritative_plane_word_capacity)) {
        return reject("component-workspace-size-overflow");
    }
    return true;
}

bool Interpreter::Impl::prepare_region_frontier_component(
    RegionRuntimeSnapshot& snapshot,
    const std::size_t component,
    const RegionConeActivationKernel& kernel,
    std::shared_ptr<RegionFrontierBackendEntry> backend,
    RegionFrontierComponentRuntime& runtime,
    const char** rejection_reason)
{
    if (rejection_reason != nullptr) {
        *rejection_reason = "unclassified";
    }
    const auto reject = [rejection_reason](const char* reason) {
        if (rejection_reason != nullptr) {
            *rejection_reason = reason;
        }
        return false;
    };
    if (!backend || !backend->executor) {
        return reject("missing-backend-executor");
    }
    const auto& layout = backend->executor->layout();
    const auto entry = backend->executor->step_entry();
    const bool generic_mode = layout.execution_mode
        == RegionFrontierExecutionModeV2::generic_deferred_update;
    const bool systemverilog_mode = layout.execution_mode
        == RegionFrontierExecutionModeV2::systemverilog_active;
    if (systemverilog_mode
        && (component >= snapshot.authoritative_state_by_component.size()
            || component >= snapshot.local_wave_state_by_component.size()
            || !snapshot.authoritative_state_by_component[component]
            || !snapshot.local_wave_state_by_component[component])) {
        return reject("missing-systemverilog-state");
    }
    if (entry == nullptr
        || !region_frontier_layout_prefix_valid_v2(
            layout.abi_version, layout.struct_size)
        || !region_frontier_layout_header_valid_v2(layout)
        || layout.reserved0 != 0U || layout.reserved_capacity != 0U
        || (!generic_mode && !systemverilog_mode)
        || (generic_mode
            && (kernel.program.scheduling_domain
                    != ProcessSchedulingDomain::generic
                || layout.metadata_count != 0U))
        || (systemverilog_mode
            && kernel.program.scheduling_domain
                != ProcessSchedulingDomain::systemverilog)
        || !frontier_count_fits(kernel.members.size())
        || layout.member_count != kernel.members.size()
        || (layout.member_count != 0U
            && layout.max_member_staged_event_counts == nullptr)) {
        return reject("invalid-layout-header-or-mode");
    }

    // The layout is an optional provider boundary. Authenticate its typed
    // header and the pointer-backed per-member capacity table before reading
    // any capacity entry supplied by that backend.
    std::uint32_t minimum_event_capacity
        = layout.max_commit_fanout_events;
    for (std::size_t member = 0U;
         member < kernel.members.size()
             && member < layout.member_count;
         ++member) {
        minimum_event_capacity = std::max(minimum_event_capacity,
            layout.max_member_staged_event_counts[member]);
    }
    const auto event_capacity = std::min(
        layout.staged_event_capacity,
        static_cast<std::uint32_t>(
            RegionFrontierComponentRuntime::scheduler_task_capacity));
    if (layout.readiness_word_count
            != (kernel.members.size() + 63U) / 64U
        || layout.signal_slot_count == 0U || layout.signals == nullptr
        || layout.members == nullptr
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)
        || (layout.fanout_edge_count != 0U
            && layout.fanout_edges == nullptr)
        || (layout.member_count != 0U
            && (layout.max_member_write_counts == nullptr
                || layout.max_member_staged_event_counts == nullptr))
        || layout.pending_write_capacity == 0U
        || layout.staged_event_capacity == 0U
        || event_capacity < minimum_event_capacity
        || layout.committed_signal_capacity
            != layout.pending_write_capacity) {
        return reject("invalid-layout-capacity-or-pointer");
    }

    std::vector<SignalId> expected_signals;
    expected_signals.reserve(kernel.inputs.size() + kernel.outputs.size());
    for (const auto& input : kernel.inputs) {
        expected_signals.push_back(input.signal);
    }
    for (const auto& output : kernel.outputs) {
        expected_signals.push_back(output.signal);
    }
    std::ranges::sort(expected_signals);
    expected_signals.erase(std::ranges::unique(expected_signals).begin(),
        expected_signals.end());
    if (expected_signals.size() != layout.signal_slot_count) {
        return reject("signal-slot-count-mismatch");
    }

    const auto graph_signals = snapshot.graph.signals();
    if (generic_mode) {
        runtime.authoritative_state.reset();
    } else {
        const auto& authoritative
            = *snapshot.authoritative_state_by_component[component];
        if (!authoritative.valid()
            || authoritative.generation() != snapshot.generation) {
            return reject("authoritative-state-invalid-or-stale");
        }
        runtime.authoritative_state
            = snapshot.authoritative_state_by_component[component];
    }
    runtime.owner = this;
    runtime.component = component;
    runtime.runtime_generation = snapshot.generation;
    runtime.execution_mode = layout.execution_mode;
    runtime.backend = std::move(backend);

    std::vector<AuthoritativeSignalPlanes::FrontierWriteBinding>
        writable_signals;
    writable_signals.reserve(kernel.internal_signals.size());
    for (std::size_t slot = 0U; slot < expected_signals.size(); ++slot) {
        if (slot >= layout.signal_slot_count
            || layout.signals[slot].signal_id != expected_signals[slot]
            || expected_signals[slot] >= graph_signals.size()
            || !frontier_count_fits(slot)) {
            return reject("signal-slot-order-or-index-invalid");
        }
        const auto signal = expected_signals[slot];
        const auto& layout_signal = layout.signals[slot];
        const auto& graph_signal = graph_signals[signal].descriptor;
        if (layout_signal.width == 0U
            || layout_signal.width != graph_signal.width
            || layout_signal.word_count
                != (static_cast<std::size_t>(layout_signal.width) + 63U) / 64U
            || (graph_signal.value_kind != ValueKind::logic4
                && graph_signal.value_kind != ValueKind::logic9)
            || layout_signal.value_kind
                != region_frontier_value_kind_v2(graph_signal.value_kind)
            || layout_signal.plane_count
                != region_frontier_required_plane_count_v2(
                    layout_signal.value_kind)
            || !region_frontier_value_shape_valid_v2(
                layout_signal.value_kind, layout_signal.width,
                layout_signal.word_count, layout_signal.plane_count)) {
            return reject("signal-descriptor-shape-invalid");
        }
        const bool internal = std::ranges::find(
            kernel.internal_signals, signal) != kernel.internal_signals.end();
        const auto expected_flags = generic_mode
            ? static_cast<std::uint32_t>(read_only_boundary_port)
            : internal
                ? static_cast<std::uint32_t>(certified_internal_single_owner)
                : static_cast<std::uint32_t>(read_only_boundary_port);
        if (layout_signal.flags != expected_flags
            || (generic_mode
                && layout_signal.metadata_index != kInvalidFrontierIndex)) {
            return reject("signal-access-flags-invalid");
        }
        if (generic_mode) {
            if (internal) {
                const auto output = std::ranges::find(
                    kernel.outputs, signal,
                    &RegionConeOutputBinding::signal);
                if (output == kernel.outputs.end()
                    || std::ranges::count(kernel.outputs, signal,
                           &RegionConeOutputBinding::signal) != 1
                    || output->owner != layout_signal.owner_process_id
                    || output->offset != 0U
                    || output->width != layout_signal.width
                    || output->value_kind != graph_signal.value_kind
                    || output->domain != SignalUpdateDomain::generic
                    || output->update_kind != RegionUpdateKind::generic) {
                    return reject("generic-internal-output-binding-invalid");
                }
            } else if (layout_signal.owner_process_id
                != kInvalidFrontierIndex) {
                return reject("generic-boundary-owner-invalid");
            }
            continue;
        }
        if (internal) {
            const auto output = std::ranges::find(kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            if (output == kernel.outputs.end()
                || output->owner != layout_signal.owner_process_id
                || output->offset != 0U
                || output->width != layout_signal.width
                || output->value_kind != graph_signal.value_kind
                || output->domain
                    != SignalUpdateDomain::systemverilog_active
                || output->update_kind
                    != RegionUpdateKind::systemverilog_active
                || layout_signal.metadata_index != writable_signals.size()
                || layout_signal.metadata_index >= layout.metadata_count) {
                return reject("systemverilog-internal-output-binding-invalid");
            }
            writable_signals.push_back({ signal, output->owner });
        } else if (layout_signal.metadata_index != kInvalidFrontierIndex
            || layout_signal.owner_process_id != kInvalidFrontierIndex) {
            return reject("systemverilog-boundary-metadata-invalid");
        }
    }
    if ((generic_mode && (!writable_signals.empty()
                             || layout.metadata_count != 0U))
        || (!generic_mode
            && (writable_signals.empty()
                || writable_signals.size() != layout.metadata_count))) {
        return reject("metadata-count-or-writable-signal-mismatch");
    }

    for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
        const auto process = kernel.members[member].process;
        if (layout.members[member].process_id != process
            || process >= processes.size()
            || !kernel.members[member].all_registers_definitely_defined) {
            return reject("member-index-or-register-definition-invalid");
        }
        const auto& debug = kernel.members[member].final_debug_state;
        if (generic_mode) {
            const auto graph_process = snapshot.graph.processes()[process];
            const auto view = processes.program_view(process);
            if (!graph_process.pure || graph_process.dependencies_unknown
                || graph_process.scheduling_domain
                    != ProcessSchedulingDomain::generic
                || graph_process.update_kind != RegionUpdateKind::generic
                || view.scheduling_domain()
                    != ProcessSchedulingDomain::generic
                || debug
                || std::ranges::any_of(
                    kernel.members[member].register_bindings,
                    [](const RegionConeKernelRegisterBinding& binding) {
                        return binding.value_kind != ValueKind::logic4
                            && binding.value_kind != ValueKind::logic9;
                    })) {
                return reject("generic-member-certification-invalid");
            }
        } else if (debug) {
            auto& scope = get_process(process).cold().current_scope;
            if (scope.capacity() < debug->scope.size()) {
                scope.reserve(debug->scope.size());
            }
        }
    }

    std::size_t generic_snapshot_word_count { };
    if (generic_mode) {
        for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
            const auto& descriptor = layout.signals[slot];
            const auto words = static_cast<std::size_t>(descriptor.word_count);
            const auto planes = static_cast<std::size_t>(descriptor.plane_count);
            std::size_t next_word_count { };
            if (planes == 0U
                || words > std::numeric_limits<std::size_t>::max() / planes
                || !frontier_add_words(generic_snapshot_word_count,
                    words * planes, next_word_count)) {
                return reject("generic-snapshot-size-overflow");
            }
            generic_snapshot_word_count = next_word_count;
        }
    }

    std::size_t pending_plane_word_capacity { };
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        if (site.plane_count != 0U
            && static_cast<std::size_t>(site.word_count)
                > std::numeric_limits<std::size_t>::max() / site.plane_count) {
            return reject("write-site-plane-word-overflow");
        }
        const auto site_plane_words
            = static_cast<std::size_t>(site.word_count) * site.plane_count;
        std::size_t next_word_count { };
        if (!frontier_add_words(pending_plane_word_capacity,
                site_plane_words, next_word_count)) {
            return reject("pending-plane-word-count-overflow");
        }
        pending_plane_word_capacity = next_word_count;
    }

    AuthoritativeSignalPlanes::ComponentPlaneWordCounts
        authoritative_plane_word_counts { };
    std::size_t authoritative_plane_word_capacity { };
    if (runtime.authoritative_state) {
        authoritative_plane_word_counts = runtime.authoritative_state
            ->values().component_plane_word_counts();
        for (const auto& role_counts : authoritative_plane_word_counts) {
            for (const auto word_count : role_counts) {
                std::size_t next_word_count { };
                if (!frontier_add_words(
                        authoritative_plane_word_capacity,
                        word_count, next_word_count)) {
                    return reject("authoritative-plane-word-count-overflow");
                }
                authoritative_plane_word_capacity = next_word_count;
            }
        }
    }

    FrontierWorkspaceSize workspace_size;
    if (!workspace_size.add<std::uint64_t>(layout.readiness_word_count)
        || !workspace_size.add<RegionFrontierMemberV2>(layout.member_count)
        || !workspace_size.add<RegionFrontierSchedulerTaskV2>(
            RegionFrontierComponentRuntime::scheduler_task_capacity)
        || !workspace_size.add<SchedulerBatchFrontierEntry>(
            RegionFrontierComponentRuntime::scheduler_task_capacity)
        || !workspace_size.add<RegionFrontierPlaneV2>(layout.signal_slot_count)
        || !workspace_size.add<RegionFrontierSignalMetadataV2>(
            layout.metadata_count)
        || !workspace_size.add<RegionFrontierFanoutEdgeV2>(
            layout.fanout_edge_count)
        || !workspace_size.add<const RegionFrontierPlaneV2*>(
            layout.signal_slot_count)
        || !workspace_size.add<RegionFrontierPendingWriteV2>(
            layout.pending_write_capacity)
        || !workspace_size.add<RegionFrontierStagedEventV2>(event_capacity)
        || !workspace_size.add<RegionFrontierCommittedSignalV2>(
            layout.committed_signal_capacity)
        || !workspace_size.add<std::size_t>(
            generic_mode ? layout.signal_slot_count : 0U)
        || !workspace_size.add<std::uint64_t>(generic_snapshot_word_count)
        || !workspace_size.add<std::size_t>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<std::size_t>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<std::uint64_t>(
            generic_mode ? layout.readiness_word_count : 0U)
        || !workspace_size.add<
            RegionFrontierComponentWorkspace::GenericQueuedMember>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<
            RegionFrontierComponentWorkspace::GenericParkedExecutorBinding>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<ProcessId>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<std::uint32_t>(
            generic_mode ? layout.member_count : 0U)
        || !workspace_size.add<AuthoritativeSignalPlanes::FrontierWriteBinding>(
            writable_signals.size())
        || !workspace_size.add<std::size_t>(
            layout.pending_write_capacity)
        || !workspace_size.add<std::uint64_t>(pending_plane_word_capacity)
        || !workspace_size.add<SystemVerilogCompactBatchMember>(event_capacity)
        || !workspace_size.add<std::uint64_t>(event_capacity)
        || !workspace_size.add<std::uint64_t>(
            authoritative_plane_word_capacity)) {
        return reject("component-workspace-size-overflow");
    }
    if (!runtime.workspace_allocation.configure(workspace_size.byte_count)) {
        return reject("component-workspace-precondition-invalid");
    }

    runtime.ready_words.assign(layout.readiness_word_count, 0U);
    runtime.members.resize(layout.member_count);
    runtime.scheduler_tasks.resize(
        RegionFrontierComponentRuntime::scheduler_task_capacity);
    runtime.original_scheduler_tasks.resize(
        RegionFrontierComponentRuntime::scheduler_task_capacity);
    runtime.planes.resize(layout.signal_slot_count);
    runtime.metadata.resize(layout.metadata_count);
    runtime.generic_snapshot_plane_offsets.clear();
    runtime.generic_snapshot_words.clear();
    runtime.generic_member_task_indices.clear();
    runtime.generic_ticket_member_offsets.clear();
    runtime.generic_queued_ready_words.clear();
    runtime.generic_queued_members.clear();
    runtime.generic_parked_executors.clear();
    runtime.generic_prefix_processes.clear();
    runtime.generic_prefix_members.clear();
    if (generic_mode) {
        runtime.generic_snapshot_plane_offsets.resize(
            layout.signal_slot_count);
        std::size_t snapshot_word_offset { };
        for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
            const auto& descriptor = layout.signals[slot];
            const auto words = static_cast<std::size_t>(descriptor.word_count);
            const auto planes = static_cast<std::size_t>(descriptor.plane_count);
            std::size_t next_word_offset { };
            if (planes == 0U
                || words > std::numeric_limits<std::size_t>::max() / planes
                || !frontier_add_words(snapshot_word_offset,
                    words * planes, next_word_offset)) {
                return reject("generic-snapshot-size-overflow");
            }
            runtime.generic_snapshot_plane_offsets[slot]
                = snapshot_word_offset;
            snapshot_word_offset = next_word_offset;
        }
        if (snapshot_word_offset != generic_snapshot_word_count) {
            return reject("generic-snapshot-size-mismatch");
        }
        runtime.generic_snapshot_words.resize(generic_snapshot_word_count);
        runtime.generic_member_task_indices.resize(
            layout.member_count, std::numeric_limits<std::size_t>::max());
        runtime.generic_ticket_member_offsets.resize(
            layout.member_count, std::numeric_limits<std::size_t>::max());
        runtime.generic_queued_ready_words.assign(
            (static_cast<std::size_t>(layout.member_count) + 63U) / 64U,
            UINT64_C(0));
        runtime.generic_queued_members.assign(layout.member_count, {});
        runtime.generic_parked_executors.resize(layout.member_count);
        runtime.generic_prefix_processes.reserve(layout.member_count);
        runtime.generic_prefix_members.reserve(layout.member_count);
        for (std::size_t member = 0U; member < layout.member_count; ++member) {
            const auto process = kernel.members[member].process;
            if (process >= processes.size()) {
                return reject("generic-member-process-out-of-range");
            }
            auto& process_state = get_process(process);
            auto& parked = runtime.generic_parked_executors[member];
            parked.executor = process_state.executor.get();
            parked.capability = process_state.executor
                ? dynamic_cast<const RegionKernelParkedExecutor*>(
                    process_state.executor.get())
                : nullptr;
            parked.lifecycle_generation
                = process_state.executor_lifecycle_generation;
            for (const auto& binding
                 : kernel.members[member].register_bindings) {
                if (binding.activation_register
                        >= kernel.program.register_count
                    || (binding.value_kind != ValueKind::logic4
                        && binding.value_kind != ValueKind::logic9)
                    || (binding.defined && binding.width == 0U)) {
                    return reject("generic-member-register-binding-invalid");
                }
            }
        }
    }
    if (layout.fanout_edge_count != 0U) {
        runtime.fanout_edges.assign(layout.fanout_edges,
            layout.fanout_edges + layout.fanout_edge_count);
    }
    runtime.port_planes.resize(layout.signal_slot_count);
    runtime.pending_writes.resize(layout.pending_write_capacity);
    runtime.staged_events.resize(event_capacity);
    runtime.committed_signals.resize(
        layout.committed_signal_capacity);
    runtime.pending_plane_offsets.assign(layout.pending_write_capacity,
        std::numeric_limits<std::size_t>::max());
    runtime.writable_signals.assign(
        writable_signals.begin(), writable_signals.end());
    runtime.compact_members.resize(event_capacity);
    runtime.issued_sequences.resize(event_capacity);

    std::size_t pending_word_count { };
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        if (site.member_index >= layout.member_count
            || site.signal_slot >= layout.signal_slot_count
            || site.pending_slot >= layout.pending_write_capacity
            || runtime.pending_plane_offsets[site.pending_slot]
                != std::numeric_limits<std::size_t>::max()) {
            return reject("write-site-index-or-pending-slot-invalid");
        }
        const auto& site_signal = layout.signals[site.signal_slot];
        const auto process = static_cast<ProcessId>(
            layout.members[site.member_index].process_id);
        const auto signal = static_cast<SignalId>(site_signal.signal_id);
        const auto output = std::ranges::find_if(kernel.outputs,
            [&](const RegionConeOutputBinding& candidate) {
                return candidate.owner == process
                    && candidate.signal == signal
                    && candidate.source_instruction
                        == site.source_instruction;
            });
        const bool internal_site
            = (site_signal.flags & certified_internal_single_owner) != 0U;
        const auto expected_event = generic_mode
            ? RegionFrontierEventKindV2::generic_deferred_update
            : internal_site ? RegionFrontierEventKindV2::internal_commit
                             : RegionFrontierEventKindV2::boundary_commit;
        const bool partial_boundary_site
            = !generic_mode && !internal_site;
        const auto output_signal_width = output != kernel.outputs.end()
            ? output->signal_width != 0U
                ? output->signal_width : output->width
            : 0U;
        const bool site_width_matches_signal = partial_boundary_site
            ? site.width != 0U && site.width <= site_signal.width
            : site.width == site_signal.width
                && site.word_count == site_signal.word_count;
        const bool output_range_matches_signal
            = output != kernel.outputs.end()
            && output_signal_width == site_signal.width
            && output->offset <= site_signal.width
            && output->width <= site_signal.width - output->offset;
        const bool output_shape_matches_site
            = output != kernel.outputs.end()
            && output->width == site.width
            && (partial_boundary_site
                ? output_range_matches_signal
                : output->offset == 0U
                    && output->width == site_signal.width);
        if (!site_width_matches_signal
            || site.value_kind != site_signal.value_kind
            || site.plane_count != site_signal.plane_count
            || !region_frontier_value_shape_valid_v2(site.value_kind,
                site.width, site.word_count, site.plane_count)
            || site.update_kind != static_cast<std::uint32_t>(generic_mode
                    ? RegionUpdateKind::generic
                    : RegionUpdateKind::systemverilog_active)
            || site.event_kind != static_cast<std::uint32_t>(expected_event)
            || !output_shape_matches_site
            || output->value_kind
                != (site.value_kind == RegionFrontierValueKindV2::logic4
                    ? ValueKind::logic4 : ValueKind::logic9)
            || output->domain != (generic_mode
                    ? SignalUpdateDomain::generic
                    : SignalUpdateDomain::systemverilog_active)
            || output->update_kind != (generic_mode
                    ? RegionUpdateKind::generic
                    : RegionUpdateKind::systemverilog_active)) {
            return reject("write-site-descriptor-or-output-binding-invalid");
        }
        std::size_t next_words { };
        if (site.plane_count != 0U
            && static_cast<std::size_t>(site.word_count)
                > std::numeric_limits<std::size_t>::max() / site.plane_count) {
            return reject("write-site-plane-word-overflow");
        }
        const auto site_plane_words
            = static_cast<std::size_t>(site.word_count) * site.plane_count;
        const auto site_word_offset = pending_word_count;
        if (!frontier_add_words(pending_word_count,
                site_plane_words, next_words)) {
            return reject("pending-plane-word-count-overflow");
        }
        pending_word_count = next_words;
        runtime.pending_plane_offsets[site.pending_slot] =
            site_word_offset;
        auto& write = runtime.pending_writes[site.pending_slot];
        write.member_index = site.member_index;
        write.signal_slot = site.signal_slot;
        write.source_instruction = site.source_instruction;
        write.update_kind = site.update_kind;
        write.flags = generic_mode
            ? static_cast<std::uint32_t>(
                RegionFrontierPendingWriteFlagsV2::pending_generic_target)
            : (layout.signals[site.signal_slot].flags
                      & certified_internal_single_owner)
                ? static_cast<std::uint32_t>(pending_internal_target)
                : static_cast<std::uint32_t>(pending_boundary_target);
        write.value_kind = site.value_kind;
        write.width = site.width;
        write.word_count = site.word_count;
        write.plane_count = site.plane_count;
    }
    if (std::ranges::any_of(runtime.pending_plane_offsets,
            [](const std::size_t offset) {
                return offset == std::numeric_limits<std::size_t>::max();
            })) {
        return reject("pending-write-slot-missing");
    }
    if (pending_word_count != pending_plane_word_capacity) {
        return reject("pending-plane-word-capacity-mismatch");
    }
    runtime.pending_plane_words.resize(pending_plane_word_capacity);
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        auto& write = runtime.pending_writes[site.pending_slot];
        const auto offset = runtime.pending_plane_offsets[site.pending_slot];
        for (std::size_t plane = 0U; plane < site.plane_count; ++plane) {
            write.value_planes[plane] = runtime.pending_plane_words.data()
                + offset + plane * site.word_count;
        }
    }

    for (std::size_t edge_index = 0U;
         edge_index < runtime.fanout_edges.size(); ++edge_index) {
        auto& edge = runtime.fanout_edges[edge_index];
        if (edge.signal_slot >= layout.signal_slot_count
            || edge.member_index >= kernel.members.size()) {
            return reject("fanout-edge-index-invalid");
        }
        if (generic_mode) {
            continue;
        }
        const auto signal = layout.signals[edge.signal_slot].signal_id;
        if (signal >= snapshot.prepared_successor_by_signal.size()) {
            return reject("prepared-successor-signal-out-of-range");
        }
        const auto& mapping
            = snapshot.prepared_successor_by_signal[signal];
        if (mapping.generation != snapshot.generation
            || mapping.component != component
            || mapping.reader_offset > snapshot.prepared_successor_readers.size()
            || mapping.reader_count
                > snapshot.prepared_successor_readers.size()
                    - mapping.reader_offset) {
            return reject("prepared-successor-mapping-invalid");
        }
        const auto process = kernel.members[edge.member_index].process;
        const auto readers = std::span<const RegionPreparedSuccessorReaderBinding> {
            snapshot.prepared_successor_readers }
            .subspan(mapping.reader_offset, mapping.reader_count);
        const auto reader = std::ranges::find(readers, process,
            &RegionPreparedSuccessorReaderBinding::process);
        if (reader == readers.end() || reader->static_trigger_mask == 0U) {
            return reject("prepared-successor-reader-or-trigger-mask-missing");
        }
        edge.trigger_mask = reader->static_trigger_mask;
    }

    for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
        const auto& source = layout.signals[slot];
        auto& plane = runtime.planes[slot];
        plane.signal_id = source.signal_id;
        plane.owner_process_id = source.owner_process_id;
        plane.value_kind = source.value_kind;
        plane.width = source.width;
        plane.word_count = source.word_count;
        plane.plane_count = source.plane_count;
        plane.flags = source.flags;
        plane.metadata_index = source.metadata_index;
        runtime.port_planes[slot] = &plane;
    }
    for (std::size_t member = 0U; member < layout.member_count; ++member) {
        runtime.members[member].process_id = layout.members[member].process_id;
        runtime.members[member].static_trigger_mask
            = Process::full_static_trigger_mask;
    }

    // Plane blocks are adopted only after every provider-layout, write-site,
    // fanout, and frame-buffer check has succeeded. A V2 decline therefore
    // leaves the unpublished A4 candidate on its ordinary owning storage.
    if (authoritative_plane_word_capacity != 0U) {
        if (!runtime.authoritative_state
            || authoritative_plane_word_capacity
                > std::numeric_limits<std::size_t>::max()
                    / sizeof(std::uint64_t)) {
            return reject("authoritative-plane-storage-size-invalid");
        }
        const auto plane_bytes = authoritative_plane_word_capacity
            * sizeof(std::uint64_t);
        auto* const plane_words = static_cast<std::uint64_t*>(
            runtime.workspace_allocation.allocate(
                plane_bytes, alignof(std::uint64_t)));
        if (!runtime.workspace_allocation.owns(plane_words, plane_bytes)) {
            return reject("authoritative-plane-tail-outside-workspace");
        }
        AuthoritativeSignalPlanes::ComponentPlaneSpans
            authoritative_plane_slices { };
        auto remaining_plane_words = std::span<std::uint64_t> {
            plane_words, authoritative_plane_word_capacity };
        std::size_t plane_word_offset { };
        for (std::size_t role = 0U; role < 4U; ++role) {
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                const auto word_count
                    = authoritative_plane_word_counts[role][plane];
                if (plane_word_offset > remaining_plane_words.size()
                    || word_count > remaining_plane_words.size()
                        - plane_word_offset) {
                    return reject("authoritative-plane-slice-overflow");
                }
                authoritative_plane_slices[role][plane]
                    = remaining_plane_words.subspan(
                        plane_word_offset, word_count);
                plane_word_offset += word_count;
            }
        }
        if (plane_word_offset != authoritative_plane_word_capacity
            || !runtime.authoritative_state->values()
                    .rehome_component_planes(
                        runtime.workspace_allocation.storage_lifetime(),
                        authoritative_plane_slices)) {
            return reject("authoritative-plane-rehome-declined");
        }
    }

    auto& frame = runtime.frame;
    frame.abi_version = kRegionFrontierAbiVersionV2;
    frame.struct_size = sizeof(RegionFrontierFrameV2);
    frame.value_plane_contract = kRegionFrontierValuePlaneContractV2;
    frame.generic_update_ack_count = 0U;
    frame.runtime_generation = snapshot.generation;
    frame.bound_runtime_generation = snapshot.generation;
    frame.certificate_generation = layout.certificate_generation;
    frame.component_generation = layout.component_generation;
    frame.member_count = layout.member_count;
    frame.scheduler_task_capacity
        = RegionFrontierComponentRuntime::scheduler_task_capacity;
    frame.readiness_word_count = layout.readiness_word_count;
    frame.signal_slot_count = layout.signal_slot_count;
    frame.metadata_count = layout.metadata_count;
    frame.fanout_edge_count = layout.fanout_edge_count;
    frame.committed_signal_capacity = layout.committed_signal_capacity;
    frame.pending_write_capacity = layout.pending_write_capacity;
    frame.staged_event_capacity = event_capacity;
    frame.ready_words = runtime.ready_words.data();
    frame.members = runtime.members.data();
    frame.scheduler_tasks = runtime.scheduler_tasks.data();
    frame.planes = runtime.planes.data();
    frame.metadata = runtime.metadata.empty()
        ? nullptr : runtime.metadata.data();
    frame.fanout_edges = runtime.fanout_edges.empty()
        ? nullptr : runtime.fanout_edges.data();
    frame.port_planes = runtime.port_planes.data();
    frame.pending_writes = runtime.pending_writes.data();
    frame.staged_events = runtime.staged_events.data();
    frame.committed_signals = runtime.committed_signals.data();
    frame.native_frontier_member_dispatches
        = &runtime.native_member_dispatches;
    frame.stop_requested = &runtime.stop_requested;
    frame.current_member = kInvalidFrontierIndex;
    frame.current_pending_write = kInvalidFrontierIndex;
    runtime.frame_initialized = true;

    runtime.alias_certificate_storage_available = false;
    runtime.clear_alias_certificate();
    const auto* const trusted_capability
        = systemverilog_mode
        ? dynamic_cast<const detail::RegionFrontierTrustedEntryCapability*>(
              runtime.backend->executor.get())
        : nullptr;
    const auto trusted_view = trusted_capability != nullptr
        ? trusted_capability->trusted_entry()
        : detail::RegionFrontierTrustedEntryView { };
    std::size_t alias_range_capacity { };
    if (trusted_view.entry != nullptr && trusted_view.layout == &layout
        && frontier_alias_range_capacity(layout, alias_range_capacity)
        && alias_range_capacity >= 13U) {
        try {
            runtime.alias_certificate_ranges.resize(alias_range_capacity);
            runtime.alias_candidate_ranges.resize(alias_range_capacity);
            runtime.alias_sorted_indices.resize(alias_range_capacity - 13U);
            runtime.alias_certificate_storage_available = true;
        } catch (const std::bad_alloc&) {
            std::vector<RegionFrontierAliasRange> { }.swap(
                runtime.alias_certificate_ranges);
            std::vector<RegionFrontierAliasRange> { }.swap(
                runtime.alias_candidate_ranges);
            std::vector<std::size_t> { }.swap(
                runtime.alias_sorted_indices);
        } catch (const std::length_error&) {
            std::vector<RegionFrontierAliasRange> { }.swap(
                runtime.alias_certificate_ranges);
            std::vector<RegionFrontierAliasRange> { }.swap(
                runtime.alias_candidate_ranges);
            std::vector<std::size_t> { }.swap(
                runtime.alias_sorted_indices);
        }
    } else {
        std::vector<RegionFrontierAliasRange> { }.swap(
            runtime.alias_certificate_ranges);
        std::vector<RegionFrontierAliasRange> { }.swap(
            runtime.alias_candidate_ranges);
        std::vector<std::size_t> { }.swap(runtime.alias_sorted_indices);
    }
    return true;
}

} // namespace fsim::runtime::simir

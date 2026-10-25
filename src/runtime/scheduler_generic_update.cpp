// SPDX-License-Identifier: Apache-2.0
#include "scheduler_internal.hpp"

#include <type_traits>

namespace fsim::runtime {

bool Scheduler::prepare_generic_readiness_ticket_capacity(
    const std::size_t required_capacity)
{
    if (!impl_)
        return false;
    if (required_capacity == 0U)
        return true;

    const bool grow_tickets = required_capacity
        > impl_->generic_readiness_ticket_capacity();
    const auto queue_capacity = std::max(required_capacity,
        maximum_scheduler_batch_chunk);
    const auto prepared_queue_buffers
        = impl_->generic_readiness_queue_buffers_with_capacity(
            queue_capacity);
    const auto missing_queue_buffers
        = prepared_queue_buffers
                >= Impl::generic_readiness_queue_buffer_count
        ? 0U : Impl::generic_readiness_queue_buffer_count
            - prepared_queue_buffers;
    if (!grow_tickets && missing_queue_buffers == 0U)
        return true;
    if (!impl_->readiness_ticket_pool_growth_is_safe())
        return false;

    if (queue_capacity
        > impl_->reusable_queue_storage.front().max_size()) {
        return false;
    }

    if (grow_tickets) {
        const auto overflow_capacity
            = required_capacity - Impl::default_readiness_ticket_count;
        if (overflow_capacity
                > impl_->generic_readiness_ticket_overflow.max_size()
            || overflow_capacity
                > impl_->generic_readiness_compact_member_overflow.max_size()) {
            return false;
        }
    }

    std::array<std::vector<Entry>,
        Impl::generic_readiness_queue_buffer_count> queue_buffers;
    for (std::size_t index = 0U;
         index < missing_queue_buffers; ++index) {
        queue_buffers[index].reserve(queue_capacity);
    }

    if (grow_tickets)
        impl_->grow_generic_readiness_ticket_pool(required_capacity);
    impl_->commit_generic_readiness_queue_buffers(
        queue_buffers, missing_queue_buffers, queue_capacity);
    return true;
}

bool Scheduler::schedule_generic_next_delta_readiness_member(
    SchedulerOrderedBatchTask& ordered_task,
    const SchedulerBatchGroupKey group_key,
    const std::size_t component_member_count,
    const StableOrder stable_order,
    const std::uint64_t payload,
    std::shared_ptr<void> owner_lifetime,
    SchedulerGenericKeyReceipt* const receipt)
{
    if (receipt != nullptr)
        *receipt = { };
    if (!impl_ || impl_->discarding || impl_->trace_hook || !group_key
        || !owner_lifetime || component_member_count == 0U
        || !impl_->in_callback || !impl_->current
        || impl_->in_safe_point || impl_->in_runtime_slot_quiet_point
        || impl_->current->systemverilog_phase
        || impl_->current->executing_end_of_slot
        || impl_->current->delta == std::numeric_limits<std::uint64_t>::max()
        || impl_->next_sequence == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }

    auto& queue = impl_->current->next_delta.queues[
        phase_index(SchedulerPhase::active)];
    if (queue.prepared || queue.dispatching_ticket != nullptr)
        return false;

    auto* const existing = queue.find_generic_compact_ticket(group_key);
    if (existing != nullptr) {
        if (!existing->active || !existing->generic_readiness_owned
            || !existing->compact || existing->cursor != 0U
            || existing->ordered_task != &ordered_task
            || existing->component_member_capacity != component_member_count
            || existing->pool_index
                >= impl_->generic_readiness_ticket_capacity()
            || existing->count >= component_member_count
            || existing->ordered_owner_lifetime.get() != owner_lifetime.get()
            || existing->ordered_owner_lifetime.owner_before(owner_lifetime)
            || owner_lifetime.owner_before(
                existing->ordered_owner_lifetime)) {
            return false;
        }

        auto& members
            = impl_->generic_readiness_compact_members_at(
                existing->pool_index);
        if (members.size() != existing->count
            || members.capacity() < component_member_count) {
            return false;
        }
        const auto sequence = impl_->next_sequence;
        members.push_back({ stable_order, sequence, payload });
        const auto member_key_less = [](const auto& lhs,
                                        const auto& rhs) noexcept {
            return lhs.order < rhs.order
                || (lhs.order == rhs.order
                    && lhs.sequence < rhs.sequence);
        };
        std::sort(members.begin(), members.end(), member_key_less);
        existing->compact_members = members.data();
        existing->count = members.size();
        if (!queue.refresh_readiness_ticket_key(existing))
            std::terminate();
        ++impl_->next_sequence;
        auto& stats = impl_->generic_batch_compaction_stats;
        ++stats.members;
        ++stats.entries_elided;
        ++stats.generic_readiness_ticket_members;
        ++stats.generic_readiness_ticket_members_elided;
        if (receipt != nullptr) {
            *receipt = { true, impl_->current->time,
                impl_->current->delta + 1U, SchedulerPhase::active,
                stable_order, sequence, payload };
        }
        return true;
    }

    auto& stats = impl_->generic_batch_compaction_stats;
    impl_->reuse_queue_vector(queue.entries);
    const auto queue_size = queue.entries.size() - queue.cursor
        + queue.current_insertions.size();
    if (queue.has_cancelable && queue_size >= 64U
        && queue.pushes_since_compaction >= queue_size / 2U) {
        queue.compact();
    }
    if (queue.entries.size() > queue.entries.max_size() - 1U
        || queue.reserved_entries_capacity
            > queue.entries.max_size() - queue.entries.size() - 1U) {
        return false;
    }
    queue.entries.reserve(queue.entries.size()
        + queue.reserved_entries_capacity + 1U);

    auto* const ticket = impl_->acquire_generic_readiness_ticket(
        group_key, component_member_count);
    if (ticket == nullptr)
        return false;
    auto& members
        = impl_->generic_readiness_compact_members_at(ticket->pool_index);

    static_assert(std::is_nothrow_move_constructible_v<Entry>);
    static_assert(std::is_nothrow_constructible_v<EntryTask, BatchTicketEntry>);
    const auto sequence = impl_->next_sequence;
    Entry entry { stable_order, sequence,
        EntryTask { BatchTicketEntry { ticket } }, nullptr, { } };
    members.push_back({ stable_order, sequence, payload });
    ticket->ordered_task = &ordered_task;
    ticket->ordered_owner_lifetime = std::move(owner_lifetime);
    ticket->component_member_capacity = component_member_count;
    ticket->compact = true;
    ticket->compact_members = members.data();
    ticket->count = 1U;
    queue.push_reserved(std::move(entry));
    ++impl_->next_sequence;

    ++stats.tickets;
    ++stats.members;
    ++stats.generic_readiness_ticket_queue_insertions;
    ++stats.generic_readiness_ticket_members;
    if (receipt != nullptr) {
        *receipt = { true, impl_->current->time,
            impl_->current->delta + 1U, SchedulerPhase::active,
            stable_order, sequence, payload };
    }
    return true;
}

SchedulerBatchCompactionStats
Scheduler::generic_batch_compaction_stats() const noexcept
{
    return impl_ ? impl_->generic_batch_compaction_stats
                 : SchedulerBatchCompactionStats { };
}

} // namespace fsim::runtime

// SPDX-License-Identifier: Apache-2.0
#include "scheduler_internal.hpp"

#include <type_traits>

namespace fsim::runtime {

void Scheduler::Impl::enqueue_systemverilog_at(
    SimulationTick time, SchedulerPhase phase, Entry entry)
{
    if (time < now)
        throw std::invalid_argument("cannot schedule an event in the past");
    if (phase_index(phase) >= phase_count)
        throw std::invalid_argument("invalid scheduler phase");
    if (current && time == current->time) {
        const auto executing = current->systemverilog_phase;
        if (executing && *executing >= SchedulerPhase::reactive
            && *executing <= SchedulerPhase::re_update) {
            if (phase == SchedulerPhase::inactive)
                phase = SchedulerPhase::re_inactive;
            else if (phase == SchedulerPhase::update)
                phase = SchedulerPhase::re_update;
        }
        push_entry(current->systemverilog.queues[phase_index(phase)],
            std::move(entry));
        if (executing && phase == *executing)
            ++current_phase_revision;
        return;
    }
    push_entry(future_slot_at(time).systemverilog.queues[phase_index(phase)],
        std::move(entry));
}

bool Scheduler::Impl::begin_systemverilog_batch(bool include_postponed)
{
    auto& slot = *current;
    const auto count = include_postponed ? phase_count : phase_count - 1;
    for (std::size_t index = 0; index < count; ++index) {
        auto& pending = slot.systemverilog.queues[index];
        if (pending.empty()) {
            pending.clear_consumed();
            continue;
        }
        const auto phase = static_cast<SchedulerPhase>(index);
        const auto target_round
            = systemverilog_round_after_pending_phase(slot, phase);
        if (!target_round) {
            auto orders = pending.pending_orders();
            throw DeltaCycleLimitError(
                slot.time, options.max_delta_cycles,
                std::move(orders), recent_signals);
        }
        slot.systemverilog_round = *target_round;
        slot.last_systemverilog_phase = phase;
        if (index == phase_index(SchedulerPhase::active)) {
            // Compact only after this Active queue has accumulated and sorted
            // all work from the preceding region. Later same-phase insertions
            // remain ordinary entries and merge against each ticket's next
            // original key.
            pending.prepare();
            const auto compacted = pending.compact_batchable_groups(
                systemverilog_batch_tickets,
                systemverilog_batch_ticket_members);
            systemverilog_batch_compaction_stats.tickets
                += static_cast<std::uint64_t>(compacted.tickets);
            systemverilog_batch_compaction_stats.members
                += static_cast<std::uint64_t>(compacted.members);
            systemverilog_batch_compaction_stats.entries_elided
                += static_cast<std::uint64_t>(compacted.members
                    - compacted.tickets);
        }
        std::swap(slot.systemverilog_running, pending);
        slot.systemverilog_phase = static_cast<SchedulerPhase>(index);
        return true;
    }
    return false;
}

void Scheduler::schedule_systemverilog_at(SimulationTick time,
    SchedulerPhase phase, StableOrder order, Task task)
{
    if (impl_->discarding)
        return;
    auto entry = impl_->make_entry(order, std::move(task));
    impl_->enqueue_systemverilog_at(time, phase, std::move(entry));
}

void Scheduler::schedule_systemverilog(
    SchedulerPhase phase, StableOrder order, Task task)
{
    schedule_systemverilog_at(impl_->now, phase, order, std::move(task));
}

void Scheduler::schedule_systemverilog_batchable(SchedulerPhase phase,
    StableOrder stable_order, SchedulerBatchTask& batch_task,
    std::uint64_t batch_payload, Task fallback_task)
{
    schedule_systemverilog_group_batchable(phase, stable_order, batch_task,
        batch_payload, std::move(fallback_task), { });
}

void Scheduler::schedule_systemverilog_group_batchable(
    const SchedulerPhase phase, const StableOrder stable_order,
    SchedulerBatchTask& batch_task, const std::uint64_t batch_payload,
    Task fallback_task, const SchedulerBatchGroupKey group_key,
    SchedulerSystemVerilogKeyReceipt* const receipt)
{
    if (receipt != nullptr) {
        *receipt = { };
    }
    if (impl_->discarding)
        return;
    auto entry = impl_->make_batch_entry(
        stable_order, batch_task, batch_payload, std::move(fallback_task),
        group_key);
    const auto key_receipt = impl_->systemverilog_key_receipt(
        impl_->now, phase, stable_order, entry.sequence);
    impl_->enqueue_systemverilog_at(impl_->now, phase, std::move(entry));
    if (receipt != nullptr && key_receipt.valid) {
        *receipt = key_receipt;
    }
}

bool Scheduler::schedule_systemverilog_readiness_member(
    const SchedulerPhase phase, const StableOrder stable_order,
    SchedulerBatchTask& batch_task, const std::uint64_t batch_payload,
    Task fallback_task, const SchedulerBatchGroupKey group_key,
    SchedulerSystemVerilogKeyReceipt* const receipt)
{
    if (receipt != nullptr) {
        *receipt = { };
    }
    if (impl_->discarding)
        return false;
    if (!group_key || phase != SchedulerPhase::active) {
        schedule_systemverilog_group_batchable(phase, stable_order,
            batch_task, batch_payload, std::move(fallback_task), group_key,
            receipt);
        return false;
    }

    const auto enqueue_ordinary = [&]() {
        schedule_systemverilog_group_batchable(phase, stable_order,
            batch_task, batch_payload, std::move(fallback_task), group_key,
            receipt);
        ++impl_->systemverilog_batch_compaction_stats
              .readiness_ticket_fallback_members;
        return false;
    };

    auto* const pending = impl_->pending_systemverilog_queue(
        impl_->now, phase);
    if (pending == nullptr || pending->prepared)
        return enqueue_ordinary();

    if (auto* const ticket
        = pending->find_readiness_ticket(&batch_task, group_key)) {
        if (ticket->pool_index
            >= impl_->systemverilog_readiness_ticket_capacity()) {
            return enqueue_ordinary();
        }
        auto& members
            = impl_->systemverilog_readiness_ticket_members_at(
                ticket->pool_index);
        if (members.size() != ticket->count
            || members.size() == members.max_size()) {
            return enqueue_ordinary();
        }
        try {
            const auto required_capacity = members.size() + 1U;
            if (required_capacity > members.capacity()) {
                const auto current_capacity = members.capacity();
                auto grown_capacity = std::size_t { 1U };
                if (current_capacity != 0U) {
                    grown_capacity = current_capacity
                            > members.max_size() / 2U
                        ? members.max_size() : current_capacity * 2U;
                }
                members.reserve(std::max(required_capacity,
                    grown_capacity));
            }
        } catch (const std::bad_alloc&) {
            return enqueue_ordinary();
        }
        // The ticket is already visible in the queue. Refresh its backing
        // pointer before any later operation can throw after a relocating
        // reserve, even though no new member has been appended yet.
        ticket->members = members.data();
        auto member = impl_->make_batch_entry(stable_order,
            batch_task, batch_payload, std::move(fallback_task), group_key);
        const auto member_sequence = member.sequence;
        members.push_back(std::move(member));
        std::sort(members.begin(), members.end(),
            WorkQueue::key_less);
        ticket->members = members.data();
        ticket->count = members.size();
        if (!pending->refresh_readiness_ticket_key(ticket)) {
            throw std::logic_error {
                "pending readiness ticket lost its scheduler entry"
            };
        }
        if (receipt != nullptr) {
            *receipt = impl_->systemverilog_key_receipt(
                impl_->now, phase, stable_order, member_sequence);
        }
        auto& stats = impl_->systemverilog_batch_compaction_stats;
        ++stats.members;
        ++stats.entries_elided;
        ++stats.readiness_ticket_members;
        ++stats.readiness_ticket_members_elided;
        return true;
    }

    auto* ticket = impl_->acquire_readiness_ticket(group_key);
    if (ticket == nullptr)
        return enqueue_ordinary();
    auto& members
        = impl_->systemverilog_readiness_ticket_members_at(
            ticket->pool_index);
    try {
        members.reserve(1U);
    } catch (const std::bad_alloc&) {
        impl_->release_readiness_ticket(*ticket);
        return enqueue_ordinary();
    }
    scheduler_detail::Entry member;
    try {
        member = impl_->make_batch_entry(stable_order, batch_task,
            batch_payload, std::move(fallback_task), group_key);
    } catch (...) {
        impl_->release_readiness_ticket(*ticket);
        throw;
    }
    const auto member_order = member.order;
    const auto member_sequence = member.sequence;
    members.push_back(std::move(member));
    ticket->members = members.data();
    ticket->count = members.size();
    scheduler_detail::Entry entry;
    entry.order = ticket->members[0U].order;
    entry.sequence = ticket->members[0U].sequence;
    entry.task = scheduler_detail::BatchTicketEntry { ticket };
    try {
        impl_->enqueue_systemverilog_at(
            impl_->now, phase, std::move(entry));
    } catch (const std::bad_alloc&) {
        auto ordinary = std::move(members.front());
        const auto ordinary_order = ordinary.order;
        const auto ordinary_sequence = ordinary.sequence;
        impl_->release_readiness_ticket(*ticket);
        impl_->enqueue_systemverilog_at(
            impl_->now, phase, std::move(ordinary));
        if (receipt != nullptr) {
            *receipt = impl_->systemverilog_key_receipt(
                impl_->now, phase, ordinary_order, ordinary_sequence);
        }
        ++impl_->systemverilog_batch_compaction_stats
              .readiness_ticket_fallback_members;
        return false;
    } catch (...) {
        impl_->release_readiness_ticket(*ticket);
        throw;
    }
    if (receipt != nullptr) {
        *receipt = impl_->systemverilog_key_receipt(
            impl_->now, phase, member_order, member_sequence);
    }
    auto& stats = impl_->systemverilog_batch_compaction_stats;
    ++stats.tickets;
    ++stats.members;
    ++stats.readiness_ticket_queue_insertions;
    ++stats.readiness_ticket_members;
    return true;
}

bool Scheduler::schedule_systemverilog_readiness_group(
    const SchedulerPhase phase, SchedulerBatchTask& batch_task,
    const SchedulerBatchGroupKey group_key,
    const std::span<ReadinessBatchMember> members,
    const std::span<SchedulerSystemVerilogKeyReceipt> receipts)
{
    for (auto& receipt : receipts) {
        receipt = { };
    }
    if (!receipts.empty() && receipts.size() != members.size()) {
        return false;
    }
    auto reservation = reserve_systemverilog_group_batch(
        phase, batch_task, group_key, members.size());
    if (!reservation) {
        return false;
    }
    const auto before = impl_->systemverilog_batch_compaction_stats;
    if (!reservation.commit(members, receipts)) {
        return false;
    }
    auto& stats = impl_->systemverilog_batch_compaction_stats;
    const auto inserted_tickets = stats.tickets - before.tickets;
    stats.readiness_ticket_members
        += static_cast<std::uint64_t>(members.size());
    stats.readiness_ticket_members_elided
        += static_cast<std::uint64_t>(members.size()) - inserted_tickets;
    stats.readiness_ticket_queue_insertions += inserted_tickets;
    return true;
}

bool Scheduler::prepare_systemverilog_readiness_ticket_capacity(
    const std::size_t required_capacity)
{
    if (!impl_)
        return false;
    if (required_capacity
        <= impl_->systemverilog_readiness_ticket_capacity()) {
        return true;
    }
    if (!impl_->readiness_ticket_pool_growth_is_safe())
        return false;

    const auto overflow_capacity
        = required_capacity - Impl::default_readiness_ticket_count;
    if (overflow_capacity
            > impl_->systemverilog_readiness_ticket_overflow.max_size()
        || overflow_capacity
            > impl_->systemverilog_readiness_ticket_member_overflow.max_size()
        || overflow_capacity
            > impl_->systemverilog_readiness_compact_member_overflow.max_size()) {
        return false;
    }
    impl_->grow_systemverilog_readiness_ticket_pool(required_capacity);
    return true;
}

Scheduler::SystemVerilogGroupBatchReservation
Scheduler::reserve_systemverilog_group_batch(
    const SchedulerPhase phase, SchedulerBatchTask& batch_task,
    const SchedulerBatchGroupKey group_key,
    const std::size_t member_count)
{
    return reserve_systemverilog_group_batch_impl(
        phase, batch_task, group_key, member_count, nullptr);
}

Scheduler::SystemVerilogGroupBatchReservation
Scheduler::reserve_systemverilog_compact_group_batch(
    const SchedulerPhase phase, SchedulerOrderedBatchTask& batch_task,
    const SchedulerBatchGroupKey group_key,
    const std::size_t member_count)
{
    return reserve_systemverilog_group_batch_impl(
        phase, batch_task, group_key, member_count, &batch_task);
}

Scheduler::SystemVerilogGroupBatchReservation
Scheduler::reserve_systemverilog_group_batch_impl(
    const SchedulerPhase phase, SchedulerBatchTask& batch_task,
    const SchedulerBatchGroupKey group_key,
    const std::size_t member_count,
    SchedulerOrderedBatchTask* const ordered_task)
{
    if (!impl_ || impl_->discarding || impl_->trace_hook || !group_key
        || phase != SchedulerPhase::active || member_count == 0U
        || (ordered_task != nullptr && member_count > 64U)
        || !impl_->current
        || impl_->current->time != impl_->now
        || impl_->current->systemverilog_phase != SchedulerPhase::active
        || impl_->active_systemverilog_group_batch_reservation_id != 0U
        || impl_->next_systemverilog_group_batch_reservation_id
            == std::numeric_limits<std::uint64_t>::max()) {
        return { };
    }

    // A successful reservation must make its later no-throw commit incapable
    // of failing on sequence exhaustion. Blocking publication can happen
    // between this reservation and commit.
    if (member_count > std::numeric_limits<std::uint64_t>::max()
            - impl_->next_sequence) {
        return { };
    }

    const auto target_round = impl_->systemverilog_round_after_pending_phase(
        *impl_->current, phase);
    if (!target_round)
        return { };

    auto& pending = impl_->current->systemverilog.queues[
        static_cast<std::size_t>(SchedulerPhase::active)];
    if (pending.prepared) {
        return { };
    }

    auto* ticket = ordered_task == nullptr
        ? pending.find_readiness_ticket(&batch_task, group_key)
        : pending.find_compact_ticket(ordered_task, group_key);
    const bool new_ticket = ticket == nullptr;
    if (new_ticket) {
        ticket = impl_->acquire_readiness_ticket(group_key);
        if (ticket == nullptr) {
            return { };
        }
    } else if (ordered_task != nullptr
        && ticket->count > 64U - member_count) {
        return { };
    }

    const auto ticket_member_size = ordered_task == nullptr
        ? impl_->systemverilog_readiness_ticket_members_at(
              ticket->pool_index).size()
        : impl_->systemverilog_readiness_compact_members_at(
              ticket->pool_index).size();
    const auto ticket_member_max_size = ordered_task == nullptr
        ? impl_->systemverilog_readiness_ticket_members_at(
              ticket->pool_index).max_size()
        : impl_->systemverilog_readiness_compact_members_at(
              ticket->pool_index).max_size();
    if (ticket_member_size != ticket->count
        || member_count
            > ticket_member_max_size - ticket_member_size) {
        if (new_ticket) {
            impl_->release_readiness_ticket(*ticket);
        }
        return { };
    }

    try {
        if (ordered_task == nullptr) {
            auto& ticket_members
                = impl_->systemverilog_readiness_ticket_members_at(
                    ticket->pool_index);
            ticket_members.reserve(ticket_member_size + member_count);
            if (!new_ticket) {
                // Reserving an existing visible ticket may relocate its
                // backing slab; refresh before any subsequent operation.
                ticket->members = ticket_members.data();
            }
        } else {
            auto& compact_members
                = impl_->systemverilog_readiness_compact_members_at(
                    ticket->pool_index);
            compact_members.reserve(ticket_member_size + member_count);
            // Reserve may relocate the compact backing array. Keep the
            // visible ticket pointer current even if this reservation is
            // canceled before commit.
            ticket->compact_members = compact_members.data();
        }
        if (new_ticket) {
            // A fresh time-slot queue can be empty while the scheduler owns
            // a reusable entry buffer from a drained slot. Adopt that buffer
            // before reserving the new ticket entry, or this direct reserve
            // allocates once per slot and leaves the pooled capacity unused.
            impl_->reuse_queue_vector(pending.entries);
            const auto reserved = pending.reserved_entries_capacity;
            const auto maximum = pending.entries.max_size();
            if (reserved >= maximum
                || pending.entries.size() > maximum - reserved - 1U) {
                impl_->release_readiness_ticket(*ticket);
                return { };
            }
            pending.entries.reserve(
                pending.entries.size() + reserved + 1U);
            ++pending.reserved_entries_capacity;
        }
    } catch (...) {
        if (new_ticket) {
            impl_->release_readiness_ticket(*ticket);
        }
        return { };
    }

    const auto reservation_id
        = impl_->next_systemverilog_group_batch_reservation_id++;
    impl_->active_systemverilog_group_batch_reservation_id = reservation_id;
    impl_->active_systemverilog_group_batch_queue = &pending;
    impl_->active_systemverilog_group_batch_ticket = ticket;
    impl_->active_systemverilog_group_batch_task = &batch_task;
    impl_->active_systemverilog_group_batch_ordered_task = ordered_task;
    impl_->active_systemverilog_group_batch_key = group_key;
    impl_->active_systemverilog_group_batch_count = member_count;
    impl_->active_systemverilog_group_batch_previous_count = ticket->count;
    impl_->active_systemverilog_group_batch_new_ticket = new_ticket;
    return { this, reservation_id, member_count, ordered_task, *target_round };
}

bool Scheduler::commit_systemverilog_group_batch_reservation(
    const std::uint64_t reservation_id,
    const std::span<SystemVerilogGroupBatchMember> members,
    const std::uint64_t target_systemverilog_round,
    const std::span<SchedulerSystemVerilogKeyReceipt> receipts) noexcept
{
    for (auto& receipt : receipts) {
        receipt = { };
    }
    if (!impl_ || reservation_id == 0U
        || impl_->active_systemverilog_group_batch_reservation_id
            != reservation_id
        || impl_->active_systemverilog_group_batch_ordered_task != nullptr
        || members.size() > impl_->active_systemverilog_group_batch_count
        || (!receipts.empty() && receipts.size() != members.size())
        || impl_->discarding || impl_->trace_hook || !impl_->current
        || impl_->current->time != impl_->now
        || impl_->current->systemverilog_phase != SchedulerPhase::active) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    if (!receipts.empty()) {
        const auto current_target_round
            = impl_->systemverilog_round_after_pending_phase(
                *impl_->current, SchedulerPhase::active);
        if (!current_target_round
            || *current_target_round != target_systemverilog_round) {
            cancel_systemverilog_group_batch_reservation(reservation_id);
            return false;
        }
    }

    if (members.empty()) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return true;
    }

    for (std::size_t index = 0U; index < members.size(); ++index) {
        const bool has_task = static_cast<bool>(members[index].fallback_task);
        const bool has_descriptor
            = members[index].fallback_descriptor.invoke != nullptr;
        if (has_task == has_descriptor
            || (index != 0U
                && members[index].stable_order
                    < members[index - 1U].stable_order)) {
            cancel_systemverilog_group_batch_reservation(reservation_id);
            return false;
        }
    }
    if (members.size()
        > std::numeric_limits<std::uint64_t>::max()
            - impl_->next_sequence) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    auto* const queue = impl_->active_systemverilog_group_batch_queue;
    auto* const ticket = impl_->active_systemverilog_group_batch_ticket;
    if (queue == nullptr || ticket == nullptr
        || queue != &impl_->current->systemverilog.queues[
            static_cast<std::size_t>(SchedulerPhase::active)]
        || queue->prepared
        || !ticket->active || !ticket->readiness_owned
        || ticket->compact
        || ticket->group_key != impl_->active_systemverilog_group_batch_key
        || ticket->count
            != impl_->active_systemverilog_group_batch_previous_count
        || ticket->pool_index
            >= impl_->systemverilog_readiness_ticket_capacity()) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    const bool new_ticket
        = impl_->active_systemverilog_group_batch_new_ticket;
    if (new_ticket && (ticket->count != 0U
        || queue->reserved_entries_capacity == 0U)) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    auto& ticket_members
        = impl_->systemverilog_readiness_ticket_members_at(
            ticket->pool_index);
    if (ticket_members.size() != ticket->count
        || members.size()
            > ticket_members.capacity() - ticket_members.size()) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    static_assert(std::is_nothrow_move_constructible_v<
        scheduler_detail::Entry>);
    static_assert(std::is_nothrow_constructible_v<
        scheduler_detail::EntryTask, scheduler_detail::BatchEntry>);
    const auto first_sequence = impl_->next_sequence;
    auto* const batch_task
        = impl_->active_systemverilog_group_batch_task;
    const auto group_key = impl_->active_systemverilog_group_batch_key;
    for (std::size_t index = 0U; index < members.size(); ++index) {
        auto& member = members[index];
        ticket_members.push_back({ member.stable_order,
            first_sequence + static_cast<std::uint64_t>(index),
            scheduler_detail::EntryTask { scheduler_detail::BatchEntry {
                batch_task, member.payload,
                std::move(member.fallback_task),
                member.fallback_descriptor, group_key,
                std::move(member.batch_owner_lifetime) } },
            nullptr, { } });
    }
    std::sort(ticket_members.begin(), ticket_members.end(),
        scheduler_detail::WorkQueue::key_less);
    ticket->members = ticket_members.data();
    ticket->count = ticket_members.size();

    if (new_ticket) {
        scheduler_detail::Entry entry;
        entry.order = ticket->members[0U].order;
        entry.sequence = ticket->members[0U].sequence;
        entry.task = scheduler_detail::BatchTicketEntry { ticket };
        queue->push_reserved(std::move(entry));
        --queue->reserved_entries_capacity;
    } else if (!queue->refresh_readiness_ticket_key(ticket)) {
        std::terminate();
    }
    impl_->next_sequence += static_cast<std::uint64_t>(members.size());
    for (std::size_t index = 0U; index < receipts.size(); ++index) {
        receipts[index] = {
            true,
            impl_->now,
            impl_->current->delta,
            target_systemverilog_round,
            SchedulerPhase::active,
            members[index].stable_order,
            first_sequence + static_cast<std::uint64_t>(index),
        };
    }

    auto& stats = impl_->systemverilog_batch_compaction_stats;
    const auto elided = new_ticket ? members.size() - 1U : members.size();
    stats.members += static_cast<std::uint64_t>(members.size());
    stats.entries_elided += static_cast<std::uint64_t>(elided);
    if (new_ticket) {
        ++stats.tickets;
    }
    impl_->clear_active_systemverilog_group_batch_reservation();
    return true;
}

bool Scheduler::commit_systemverilog_compact_group_batch_reservation(
    const std::uint64_t reservation_id,
    const std::span<const SystemVerilogCompactBatchMember> members,
    SchedulerOrderedBatchTask& ordered_task,
    std::shared_ptr<void> owner_lifetime,
    const std::span<std::uint64_t> issued_sequences,
    const std::uint64_t target_systemverilog_round,
    const std::span<SchedulerSystemVerilogKeyReceipt> receipts) noexcept
{
    for (auto& receipt : receipts) {
        receipt = { };
    }
    if (!impl_ || reservation_id == 0U
        || impl_->active_systemverilog_group_batch_reservation_id
            != reservation_id
        || impl_->active_systemverilog_group_batch_ordered_task
            != &ordered_task
        || impl_->active_systemverilog_group_batch_task
            != static_cast<SchedulerBatchTask*>(&ordered_task)
        || members.size() > impl_->active_systemverilog_group_batch_count
        || (!issued_sequences.empty()
            && issued_sequences.size() != members.size())
        || (!receipts.empty() && receipts.size() != members.size())
        || !owner_lifetime || impl_->discarding || impl_->trace_hook
        || !impl_->current || impl_->current->time != impl_->now
        || impl_->current->systemverilog_phase != SchedulerPhase::active) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    if (!receipts.empty()) {
        const auto current_target_round
            = impl_->systemverilog_round_after_pending_phase(
                *impl_->current, SchedulerPhase::active);
        if (!current_target_round
            || *current_target_round != target_systemverilog_round) {
            cancel_systemverilog_group_batch_reservation(reservation_id);
            return false;
        }
    }

    if (members.empty()) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return true;
    }

    for (std::size_t index = 1U; index < members.size(); ++index) {
        if (members[index].stable_order
            < members[index - 1U].stable_order) {
            cancel_systemverilog_group_batch_reservation(reservation_id);
            return false;
        }
    }
    if (members.size()
        > std::numeric_limits<std::uint64_t>::max()
            - impl_->next_sequence) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    auto* const queue = impl_->active_systemverilog_group_batch_queue;
    auto* const ticket = impl_->active_systemverilog_group_batch_ticket;
    if (queue == nullptr || ticket == nullptr
        || queue != &impl_->current->systemverilog.queues[
            static_cast<std::size_t>(SchedulerPhase::active)]
        || queue->prepared || !ticket->active || !ticket->readiness_owned
        || ticket->group_key != impl_->active_systemverilog_group_batch_key
        || ticket->count
            != impl_->active_systemverilog_group_batch_previous_count
        || ticket->pool_index
            >= impl_->systemverilog_readiness_ticket_capacity()) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    const bool new_ticket
        = impl_->active_systemverilog_group_batch_new_ticket;
    const bool ticket_is_new_shape = !ticket->compact
        && ticket->count == 0U
        && impl_->active_systemverilog_group_batch_previous_count == 0U
        && new_ticket;
    const bool same_owner_lifetime = ticket->ordered_owner_lifetime
        && !ticket->ordered_owner_lifetime.owner_before(owner_lifetime)
        && !owner_lifetime.owner_before(ticket->ordered_owner_lifetime);
    const bool ticket_is_append_shape = ticket->compact
        && ticket->ordered_task == &ordered_task
        && ticket->cursor == 0U && ticket->count != 0U && !new_ticket
        && members.size() <= 64U
        && ticket->count <= 64U - members.size()
        && same_owner_lifetime;
    if ((!ticket_is_new_shape && !ticket_is_append_shape)
        || (new_ticket && queue->reserved_entries_capacity == 0U)) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    auto& compact_members
        = impl_->systemverilog_readiness_compact_members_at(
            ticket->pool_index);
    if (compact_members.size() != ticket->count
        || compact_members.size() > compact_members.capacity()
        || members.size()
            > compact_members.capacity() - compact_members.size()) {
        cancel_systemverilog_group_batch_reservation(reservation_id);
        return false;
    }

    const auto first_sequence = impl_->next_sequence;
    for (std::size_t index = 0U; index < members.size(); ++index) {
        compact_members.push_back({ members[index].stable_order,
            first_sequence + static_cast<std::uint64_t>(index),
            members[index].payload });
    }
    if (!new_ticket) {
        const auto member_key_less = [](const auto& lhs,
                                        const auto& rhs) noexcept {
            return lhs.order < rhs.order
                || (lhs.order == rhs.order
                    && lhs.sequence < rhs.sequence);
        };
        std::sort(compact_members.begin(), compact_members.end(),
            member_key_less);
    }
    ticket->members = nullptr;
    ticket->compact_members = compact_members.data();
    if (new_ticket) {
        ticket->ordered_task = &ordered_task;
        ticket->ordered_owner_lifetime = std::move(owner_lifetime);
        ticket->compact = true;
    }
    ticket->count = compact_members.size();

    if (new_ticket) {
        const auto& first = compact_members.front();
        scheduler_detail::Entry entry;
        entry.order = first.order;
        entry.sequence = first.sequence;
        entry.task = scheduler_detail::BatchTicketEntry { ticket };
        queue->push_reserved(std::move(entry));
        --queue->reserved_entries_capacity;
    } else if (!queue->refresh_readiness_ticket_key(ticket)) {
        std::terminate();
    }
    impl_->next_sequence += static_cast<std::uint64_t>(members.size());
    for (std::size_t index = 0U; index < issued_sequences.size(); ++index) {
        issued_sequences[index]
            = first_sequence + static_cast<std::uint64_t>(index);
    }
    for (std::size_t index = 0U; index < receipts.size(); ++index) {
        receipts[index] = {
            true,
            impl_->now,
            impl_->current->delta,
            target_systemverilog_round,
            SchedulerPhase::active,
            members[index].stable_order,
            first_sequence + static_cast<std::uint64_t>(index),
        };
    }

    auto& stats = impl_->systemverilog_batch_compaction_stats;
    const auto elided = new_ticket
        ? members.size() - 1U : members.size();
    stats.members += static_cast<std::uint64_t>(members.size());
    stats.entries_elided += static_cast<std::uint64_t>(elided);
    if (new_ticket) {
        ++stats.tickets;
    }
    impl_->clear_active_systemverilog_group_batch_reservation();
    return true;
}

void Scheduler::cancel_systemverilog_group_batch_reservation(
    const std::uint64_t reservation_id) noexcept
{
    if (!impl_ || reservation_id == 0U
        || impl_->active_systemverilog_group_batch_reservation_id
            != reservation_id) {
        return;
    }
    impl_->release_active_systemverilog_group_batch_reservation();
}

std::optional<std::uint64_t>
Scheduler::systemverilog_group_batch_target_round(
    const std::uint64_t reservation_id,
    const std::uint64_t reserved_round) const noexcept
{
    if (!impl_ || reservation_id == 0U
        || impl_->active_systemverilog_group_batch_reservation_id
            != reservation_id
        || !impl_->current || impl_->current->time != impl_->now
        || impl_->current->systemverilog_phase != SchedulerPhase::active
        || impl_->active_systemverilog_group_batch_ticket == nullptr
        || !impl_->active_systemverilog_group_batch_ticket->active) {
        return std::nullopt;
    }
    auto* const pending = &impl_->current->systemverilog.queues[
        static_cast<std::size_t>(SchedulerPhase::active)];
    if (impl_->active_systemverilog_group_batch_queue != pending
        || pending->prepared) {
        return std::nullopt;
    }
    const auto target_round = impl_->systemverilog_round_after_pending_phase(
        *impl_->current, SchedulerPhase::active);
    if (!target_round || *target_round != reserved_round) {
        return std::nullopt;
    }
    return target_round;
}

Scheduler::SystemVerilogGroupBatchReservation::~SystemVerilogGroupBatchReservation()
{
    cancel();
}

std::optional<std::uint64_t>
Scheduler::SystemVerilogGroupBatchReservation::target_systemverilog_round()
    const noexcept
{
    if (owner_ == nullptr || reservation_id_ == 0U)
        return std::nullopt;
    return owner_->systemverilog_group_batch_target_round(
        reservation_id_, target_systemverilog_round_);
}

Scheduler::SystemVerilogGroupBatchReservation::SystemVerilogGroupBatchReservation(
    SystemVerilogGroupBatchReservation&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr))
    , reservation_id_(std::exchange(other.reservation_id_, 0U))
    , count_(std::exchange(other.count_, 0U))
    , ordered_task_(std::exchange(other.ordered_task_, nullptr))
    , target_systemverilog_round_(
          std::exchange(other.target_systemverilog_round_, 0U))
{
}

Scheduler::SystemVerilogGroupBatchReservation&
Scheduler::SystemVerilogGroupBatchReservation::operator=(
    SystemVerilogGroupBatchReservation&& other) noexcept
{
    if (this != &other) {
        cancel();
        owner_ = std::exchange(other.owner_, nullptr);
        reservation_id_ = std::exchange(other.reservation_id_, 0U);
        count_ = std::exchange(other.count_, 0U);
        ordered_task_ = std::exchange(other.ordered_task_, nullptr);
        target_systemverilog_round_ = std::exchange(
            other.target_systemverilog_round_, 0U);
    }
    return *this;
}

bool Scheduler::SystemVerilogGroupBatchReservation::commit(
    const std::span<SystemVerilogGroupBatchMember> members,
    const std::span<SchedulerSystemVerilogKeyReceipt> receipts) noexcept
{
    for (auto& receipt : receipts) {
        receipt = { };
    }
    if (owner_ == nullptr || ordered_task_ != nullptr) {
        return false;
    }
    auto* const owner = std::exchange(owner_, nullptr);
    const auto id = std::exchange(reservation_id_, 0U);
    const auto target_round = std::exchange(
        target_systemverilog_round_, 0U);
    count_ = 0U;
    return owner->commit_systemverilog_group_batch_reservation(
        id, members, target_round, receipts);
}

bool Scheduler::SystemVerilogGroupBatchReservation::commit_compact(
    const std::span<const SystemVerilogCompactBatchMember> members,
    SchedulerOrderedBatchTask& ordered_task,
    std::shared_ptr<void> owner_lifetime,
    const std::span<std::uint64_t> issued_sequences,
    const std::span<SchedulerSystemVerilogKeyReceipt> receipts) noexcept
{
    for (auto& receipt : receipts) {
        receipt = { };
    }
    if (owner_ == nullptr || ordered_task_ != &ordered_task) {
        return false;
    }
    auto* const owner = std::exchange(owner_, nullptr);
    const auto id = std::exchange(reservation_id_, 0U);
    const auto target_round = std::exchange(
        target_systemverilog_round_, 0U);
    count_ = 0U;
    ordered_task_ = nullptr;
    return owner->commit_systemverilog_compact_group_batch_reservation(
        id, members, ordered_task, std::move(owner_lifetime),
        issued_sequences, target_round, receipts);
}

void Scheduler::SystemVerilogGroupBatchReservation::cancel() noexcept
{
    if (owner_ != nullptr) {
        owner_->cancel_systemverilog_group_batch_reservation(reservation_id_);
        owner_ = nullptr;
        reservation_id_ = 0U;
        count_ = 0U;
        ordered_task_ = nullptr;
        target_systemverilog_round_ = 0U;
    }
}

SchedulerBatchCompactionStats
Scheduler::systemverilog_batch_compaction_stats() const noexcept
{
    return impl_ ? impl_->systemverilog_batch_compaction_stats
                 : SchedulerBatchCompactionStats { };
}

void Scheduler::schedule_systemverilog_next_delta(
    SchedulerPhase phase, StableOrder order, Task task)
{
    // SV work always enters a pending round; the running batch is frozen.
    // This is deliberately distinct from a generic/VHDL next-delta crossing.
    schedule_systemverilog(phase, order, std::move(task));
}

void Scheduler::schedule_systemverilog_after(SimulationTick delay,
    SchedulerPhase phase, StableOrder order, Task task)
{
    if (impl_->discarding)
        return;
    if (delay > std::numeric_limits<SimulationTick>::max() - impl_->now)
        throw std::overflow_error("simulation time overflow while scheduling event");
    schedule_systemverilog_at(impl_->now + delay, phase, order, std::move(task));
}

ScheduledTaskHandle Scheduler::schedule_systemverilog_after_cancelable(
    SimulationTick delay, SchedulerPhase phase, StableOrder order, Task task)
{
    if (impl_->discarding)
        return { };
    if (delay > std::numeric_limits<SimulationTick>::max() - impl_->now)
        throw std::overflow_error("simulation time overflow while scheduling event");
    if (phase_index(phase) >= phase_count)
        throw std::invalid_argument("invalid scheduler phase");
    if (!task)
        throw std::invalid_argument("cannot schedule an empty task");
    if (impl_->next_sequence == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("scheduler insertion sequence overflow");
    const auto sequence = impl_->next_sequence++;
    const auto cancellation = impl_->cancel_slots->acquire(std::move(task));
    Entry entry { order, sequence, EntryTask { std::monostate { } },
        impl_->cancel_slots.get(), cancellation };
    try {
        impl_->enqueue_systemverilog_at(impl_->now + delay, phase, std::move(entry));
    } catch (...) {
        impl_->cancel_slots->release(cancellation);
        throw;
    }
    return ScheduledTaskHandle {
        impl_->cancel_slots, cancellation.slot, cancellation.generation };
}

void Scheduler::schedule_internal_systemverilog_at(SimulationTick time,
    SchedulerPhase phase, StableOrder order, detail::SchedulerTaskDescriptor task)
{
    if (impl_->discarding)
        return;
    auto entry = impl_->make_internal_entry(order, std::move(task));
    impl_->enqueue_systemverilog_at(time, phase, std::move(entry));
}

void Scheduler::schedule_internal_systemverilog_batch_from_frontier(
    const std::uint64_t frontier_generation,
    const std::span<const StableOrder> stable_orders,
    const std::span<const detail::SchedulerTaskDescriptor> tasks)
{
    if (stable_orders.size() != tasks.size()) {
        throw std::invalid_argument(
            "internal SystemVerilog batch order and task counts differ");
    }
    if (tasks.empty()) {
        return;
    }
    const auto& active_frontier = impl_->active_batch_frontier;
    if (!impl_->in_callback || !impl_->current || !active_frontier
        || frontier_generation == 0U
        || active_frontier->generation != frontier_generation
        || active_frontier->phase != SchedulerPhase::active
        || active_frontier->time != impl_->now
        || active_frontier->delta != impl_->current->delta
        || active_frontier->systemverilog_round
            != impl_->current->systemverilog_round
        || impl_->current->systemverilog_phase != SchedulerPhase::active) {
        throw std::logic_error(
            "internal SystemVerilog batch has no matching Active frontier");
    }
    if (tasks.size()
        > std::numeric_limits<std::uint64_t>::max() - impl_->next_sequence) {
        throw std::overflow_error(
            "scheduler insertion sequence overflow");
    }

    static_assert(std::is_nothrow_move_constructible_v<Entry>);
    static_assert(std::is_nothrow_move_assignable_v<Entry>);
    std::vector<Entry> prepared;
    prepared.reserve(tasks.size());
    for (std::size_t index = 0U; index < tasks.size(); ++index) {
        if (tasks[index].invoke == nullptr) {
            throw std::invalid_argument(
                "internal SystemVerilog batch contains an empty task");
        }
        prepared.push_back({ stable_orders[index],
            impl_->next_sequence + index,
            EntryTask { tasks[index] }, nullptr, { } });
    }

    constexpr auto active_index
        = static_cast<std::size_t>(SchedulerPhase::active);
    auto& queue = impl_->current->systemverilog.queues[active_index];
    auto& storage = queue.prepared
        ? queue.current_insertions : queue.entries;
    impl_->reuse_queue_vector(storage);
    const auto previous_capacity = storage.capacity();
    if (tasks.size() > storage.max_size() - storage.size()) {
        throw std::length_error(
            "internal SystemVerilog batch exceeds queue capacity");
    }
    const auto reserved = queue.prepared
        ? queue.reserved_insertions_capacity
        : queue.reserved_entries_capacity;
    if (tasks.size() > storage.max_size() - storage.size()
        || reserved > storage.max_size() - storage.size() - tasks.size()) {
        throw std::length_error(
            "internal SystemVerilog batch exceeds reserved queue capacity");
    }
    storage.reserve(storage.size() + reserved + tasks.size());

    // No scheduler-visible work or insertion identity changes before every
    // potentially allocating preparation step above has succeeded. With
    // reserved storage and nothrow Entry moves, queue insertion is now
    // non-allocating and cannot leave a partial batch.
    for (auto& entry : prepared) {
        queue.push_reserved(std::move(entry));
    }
    impl_->next_sequence += tasks.size();
    if (storage.capacity() != previous_capacity) {
        impl_->trace_queue_storage_growth(prepared.front(), storage.capacity());
    }
}

Scheduler::InternalSystemVerilogBatchReservation
Scheduler::reserve_internal_systemverilog_batch_from_frontier(
    const std::uint64_t frontier_generation,
    const std::size_t task_count)
{
    if (!impl_ || task_count == 0U || impl_->trace_hook || impl_->discarding) {
        return { };
    }
    const auto& active_frontier = impl_->active_batch_frontier;
    if (!impl_->in_callback || !impl_->current || !active_frontier
        || frontier_generation == 0U
        || active_frontier->generation != frontier_generation
        || active_frontier->phase != SchedulerPhase::active
        || active_frontier->time != impl_->now
        || active_frontier->delta != impl_->current->delta
        || active_frontier->systemverilog_round
            != impl_->current->systemverilog_round
        || impl_->current->systemverilog_phase != SchedulerPhase::active
        || impl_->active_internal_batch_reservation_id != 0U) {
        return { };
    }
    if (impl_->next_internal_batch_reservation_id
        == std::numeric_limits<std::uint64_t>::max()) {
        return { };
    }

    constexpr auto active_index
        = static_cast<std::size_t>(SchedulerPhase::active);
    auto& queue = impl_->current->systemverilog.queues[active_index];
    auto& storage = queue.prepared
        ? queue.current_insertions : queue.entries;
    auto& reserved = queue.prepared
        ? queue.reserved_insertions_capacity
        : queue.reserved_entries_capacity;
    impl_->reuse_queue_vector(storage);
    if (task_count > storage.max_size() - storage.size()
        || reserved > storage.max_size() - storage.size() - task_count) {
        throw std::length_error(
            "internal SystemVerilog reservation exceeds queue capacity");
    }
    storage.reserve(storage.size() + reserved + task_count);
    reserved += task_count;

    const auto reservation_id
        = impl_->next_internal_batch_reservation_id++;
    impl_->active_internal_batch_reservation_id = reservation_id;
    impl_->active_internal_batch_queue = &queue;
    impl_->active_internal_batch_reservation_count = task_count;
    impl_->active_internal_batch_uses_insertions = queue.prepared;
    impl_->active_internal_batch_frontier_generation = frontier_generation;
    return { this, reservation_id, task_count };
}

Scheduler::InternalSystemVerilogBatchReservation
Scheduler::reserve_internal_systemverilog_ordered_ticket_from_frontier(
    const std::uint64_t frontier_generation,
    const std::span<const StableOrder> stable_orders,
    const std::span<const detail::SchedulerTaskDescriptor> tasks,
    InternalSystemVerilogOrderedTicketStorage& storage,
    std::shared_ptr<void> owner_lifetime)
{
    const auto member_count = tasks.size();
    if (!impl_ || member_count == 0U || member_count > 64U
        || stable_orders.size() != member_count || !owner_lifetime
        || !storage.available()
        || impl_->trace_hook || impl_->discarding) {
        return { };
    }
    const auto& active_frontier = impl_->active_batch_frontier;
    if (!impl_->in_callback || !impl_->current || !active_frontier
        || frontier_generation == 0U
        || active_frontier->generation != frontier_generation
        || active_frontier->phase != SchedulerPhase::active
        || active_frontier->time != impl_->now
        || active_frontier->delta != impl_->current->delta
        || active_frontier->systemverilog_round
            != impl_->current->systemverilog_round
        || impl_->current->systemverilog_phase != SchedulerPhase::active
        || impl_->active_internal_batch_reservation_id != 0U
        || impl_->next_internal_batch_reservation_id
            == std::numeric_limits<std::uint64_t>::max()) {
        return { };
    }
    for (const auto& task : tasks) {
        if (task.invoke == nullptr)
            return { };
    }

    // Prepare the ordered virtual members completely before reserving queue
    // storage or consuming any scheduler sequence identities. The temporary
    // sequence is the caller's original vector index; commit translates it to
    // the contiguous insertion sequence range allocated for this ticket.
    storage.members.clear();
    storage.members.reserve(member_count);
    for (std::size_t index = 0U; index < member_count; ++index) {
        storage.members.push_back(
            { stable_orders[index], index, tasks[index] });
    }
    std::sort(storage.members.begin(), storage.members.end(),
        [](const auto& left, const auto& right) noexcept {
            return left.order != right.order
                ? left.order < right.order
                : left.sequence < right.sequence;
        });

    constexpr auto active_index
        = static_cast<std::size_t>(SchedulerPhase::active);
    auto& queue = impl_->current->systemverilog.queues[active_index];
    auto& queue_storage = queue.prepared
        ? queue.current_insertions : queue.entries;
    auto& reserved = queue.prepared
        ? queue.reserved_insertions_capacity
        : queue.reserved_entries_capacity;
    impl_->reuse_queue_vector(queue_storage);
    if (queue_storage.size() == queue_storage.max_size()
        || reserved > queue_storage.max_size() - queue_storage.size() - 1U) {
        throw std::length_error(
            "ordered SystemVerilog ticket exceeds queue capacity");
    }
    queue_storage.reserve(queue_storage.size() + reserved + 1U);
    reserved += 1U;

    const auto reservation_id
        = impl_->next_internal_batch_reservation_id++;
    storage.active = true;
    storage.cursor = 0U;
    impl_->active_internal_ordered_ticket_storage = &storage;
    impl_->active_internal_ordered_ticket_owner = std::move(owner_lifetime);
    impl_->active_internal_batch_reservation_id = reservation_id;
    impl_->active_internal_batch_queue = &queue;
    impl_->active_internal_batch_reservation_count = 1U;
    impl_->active_internal_batch_uses_insertions = queue.prepared;
    impl_->active_internal_batch_frontier_generation = frontier_generation;
    return { this, reservation_id, member_count };
}

bool Scheduler::commit_internal_systemverilog_batch_reservation(
    const std::uint64_t reservation_id,
    const std::size_t count,
    const std::span<const StableOrder> stable_orders,
    const std::span<const detail::SchedulerTaskDescriptor> tasks) noexcept
{
    if (!impl_ || reservation_id == 0U
        || impl_->active_internal_batch_reservation_id != reservation_id
        || impl_->active_internal_ordered_ticket_storage != nullptr
        || impl_->active_internal_batch_reservation_count != count
        || stable_orders.size() != count || tasks.size() != count
        || impl_->trace_hook || impl_->discarding || !impl_->current
        || !impl_->in_callback || !impl_->active_batch_frontier
        || impl_->active_batch_frontier->generation
            != impl_->active_internal_batch_frontier_generation) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }
    const auto& frontier = *impl_->active_batch_frontier;
    if (frontier.phase != SchedulerPhase::active
        || frontier.time != impl_->now
        || frontier.delta != impl_->current->delta
        || frontier.systemverilog_round
            != impl_->current->systemverilog_round
        || impl_->current->systemverilog_phase != SchedulerPhase::active) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }
    if (count > std::numeric_limits<std::uint64_t>::max()
            - impl_->next_sequence
        || std::ranges::any_of(tasks, [](const auto& task) {
               return task.invoke == nullptr;
           })) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }

    auto* const queue = impl_->active_internal_batch_queue;
    if (queue == nullptr
        || queue != &impl_->current->systemverilog.queues[
            static_cast<std::size_t>(SchedulerPhase::active)]
        || queue->prepared != impl_->active_internal_batch_uses_insertions) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }
    auto& reserved = impl_->active_internal_batch_uses_insertions
        ? queue->reserved_insertions_capacity
        : queue->reserved_entries_capacity;
    if (reserved < count) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }
    static_assert(std::is_nothrow_constructible_v<EntryTask,
        detail::SchedulerTaskDescriptor>);
    static_assert(std::is_nothrow_move_constructible_v<Entry>);
    const auto first_sequence = impl_->next_sequence;
    for (std::size_t index = 0U; index < count; ++index) {
        Entry entry { stable_orders[index], first_sequence + index,
            EntryTask { tasks[index] }, nullptr, { } };
        queue->push_reserved(std::move(entry));
    }
    impl_->next_sequence += count;
    reserved -= count;
    impl_->active_internal_batch_reservation_id = 0U;
    impl_->active_internal_batch_queue = nullptr;
    impl_->active_internal_batch_reservation_count = 0U;
    impl_->active_internal_batch_uses_insertions = false;
    impl_->active_internal_batch_frontier_generation = 0U;
    return true;
}

bool Scheduler::commit_internal_systemverilog_ordered_ticket_reservation(
    const std::uint64_t reservation_id) noexcept
{
    if (!impl_ || reservation_id == 0U
        || impl_->active_internal_batch_reservation_id != reservation_id
        || impl_->active_internal_ordered_ticket_storage == nullptr
        || impl_->active_internal_batch_reservation_count != 1U
        || impl_->trace_hook || impl_->discarding || !impl_->current
        || !impl_->in_callback || !impl_->active_batch_frontier
        || impl_->active_batch_frontier->generation
            != impl_->active_internal_batch_frontier_generation) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }
    const auto& frontier = *impl_->active_batch_frontier;
    auto* const ticket = impl_->active_internal_ordered_ticket_storage;
    if (frontier.phase != SchedulerPhase::active
        || frontier.time != impl_->now
        || frontier.delta != impl_->current->delta
        || frontier.systemverilog_round
            != impl_->current->systemverilog_round
        || impl_->current->systemverilog_phase != SchedulerPhase::active
        || !ticket->active || ticket->cursor != 0U
        || ticket->members.empty()
        || ticket->members.size()
            > std::numeric_limits<std::uint64_t>::max()
                - impl_->next_sequence) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }

    auto* const queue = impl_->active_internal_batch_queue;
    if (queue == nullptr
        || queue != &impl_->current->systemverilog.queues[
            static_cast<std::size_t>(SchedulerPhase::active)]
        || queue->prepared != impl_->active_internal_batch_uses_insertions) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }
    auto& reserved = impl_->active_internal_batch_uses_insertions
        ? queue->reserved_insertions_capacity
        : queue->reserved_entries_capacity;
    if (reserved == 0U) {
        cancel_internal_systemverilog_batch_reservation(reservation_id);
        return false;
    }

    static_assert(std::is_nothrow_constructible_v<EntryTask,
        OrderedSystemVerilogTicketEntry>);
    static_assert(std::is_nothrow_move_constructible_v<Entry>);
    const auto first_sequence = impl_->next_sequence;
    for (auto& member : ticket->members)
        member.sequence += first_sequence;
    const auto& first = ticket->members.front();
    Entry queue_entry { first.order, first.sequence,
        EntryTask { OrderedSystemVerilogTicketEntry {
            ticket, impl_->active_internal_ordered_ticket_owner } }, nullptr, { } };
    queue->push_reserved(std::move(queue_entry));
    impl_->next_sequence += ticket->members.size();
    --reserved;
    impl_->active_internal_batch_reservation_id = 0U;
    impl_->active_internal_batch_queue = nullptr;
    impl_->active_internal_batch_reservation_count = 0U;
    impl_->active_internal_batch_uses_insertions = false;
    impl_->active_internal_batch_frontier_generation = 0U;
    impl_->active_internal_ordered_ticket_storage = nullptr;
    impl_->active_internal_ordered_ticket_owner.reset();
    return true;
}

void Scheduler::cancel_internal_systemverilog_batch_reservation(
    const std::uint64_t reservation_id) noexcept
{
    if (!impl_ || reservation_id == 0U
        || impl_->active_internal_batch_reservation_id != reservation_id) {
        return;
    }
    auto* const queue = impl_->active_internal_batch_queue;
    if (queue != nullptr && impl_->current
        && queue == &impl_->current->systemverilog.queues[
            static_cast<std::size_t>(SchedulerPhase::active)]) {
        auto& reserved = impl_->active_internal_batch_uses_insertions
            ? queue->reserved_insertions_capacity
            : queue->reserved_entries_capacity;
        if (reserved >= impl_->active_internal_batch_reservation_count) {
            reserved -= impl_->active_internal_batch_reservation_count;
        } else {
            reserved = 0U;
        }
    }
    impl_->active_internal_batch_reservation_id = 0U;
    impl_->active_internal_batch_queue = nullptr;
    impl_->active_internal_batch_reservation_count = 0U;
    impl_->active_internal_batch_uses_insertions = false;
    impl_->active_internal_batch_frontier_generation = 0U;
    if (impl_->active_internal_ordered_ticket_storage != nullptr)
        impl_->active_internal_ordered_ticket_storage->retire();
    impl_->active_internal_ordered_ticket_storage = nullptr;
    impl_->active_internal_ordered_ticket_owner.reset();
}

bool Scheduler::trace_hook_installed() const noexcept
{
    return impl_ && impl_->trace_hook != nullptr;
}

Scheduler::InternalSystemVerilogBatchReservation::~InternalSystemVerilogBatchReservation()
{
    cancel();
}

Scheduler::InternalSystemVerilogBatchReservation::InternalSystemVerilogBatchReservation(
    InternalSystemVerilogBatchReservation&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr))
    , reservation_id_(std::exchange(other.reservation_id_, 0U))
    , count_(std::exchange(other.count_, 0U))
{
}

Scheduler::InternalSystemVerilogBatchReservation&
Scheduler::InternalSystemVerilogBatchReservation::operator=(
    InternalSystemVerilogBatchReservation&& other) noexcept
{
    if (this != &other) {
        cancel();
        owner_ = std::exchange(other.owner_, nullptr);
        reservation_id_ = std::exchange(other.reservation_id_, 0U);
        count_ = std::exchange(other.count_, 0U);
    }
    return *this;
}

bool Scheduler::InternalSystemVerilogBatchReservation::commit(
    const std::span<const StableOrder> stable_orders,
    const std::span<const detail::SchedulerTaskDescriptor> tasks) noexcept
{
    if (owner_ == nullptr) {
        return false;
    }
    auto* const owner = std::exchange(owner_, nullptr);
    const auto id = std::exchange(reservation_id_, 0U);
    const auto count = std::exchange(count_, 0U);
    return owner->commit_internal_systemverilog_batch_reservation(
        id, count, stable_orders, tasks);
}

bool Scheduler::InternalSystemVerilogBatchReservation::commit_ordered_ticket()
    noexcept
{
    if (owner_ == nullptr)
        return false;
    auto* const owner = std::exchange(owner_, nullptr);
    const auto id = std::exchange(reservation_id_, 0U);
    count_ = 0U;
    return owner->commit_internal_systemverilog_ordered_ticket_reservation(id);
}

void Scheduler::InternalSystemVerilogBatchReservation::cancel() noexcept
{
    if (owner_ != nullptr) {
        owner_->cancel_internal_systemverilog_batch_reservation(
            reservation_id_);
        owner_ = nullptr;
        reservation_id_ = 0U;
        count_ = 0U;
    }
}

void Scheduler::schedule_internal_systemverilog(SchedulerPhase phase,
    StableOrder order, detail::SchedulerTaskDescriptor task)
{
    schedule_internal_systemverilog_at(impl_->now, phase, order, std::move(task));
}

void Scheduler::schedule_internal_systemverilog_next_delta(SchedulerPhase phase,
    StableOrder order, detail::SchedulerTaskDescriptor task)
{
    schedule_internal_systemverilog(phase, order, std::move(task));
}

void Scheduler::schedule_end_of_time_slot(StableOrder order, Task task)
{
    if (impl_->discarding)
        return;
    auto entry = impl_->make_entry(order, std::move(task));
    if (impl_->current)
        impl_->push_entry(impl_->current->end_of_slot, std::move(entry));
    else
        impl_->push_entry(impl_->future_slot_at(impl_->now).end_of_slot,
            std::move(entry));
}

std::uint64_t Scheduler::systemverilog_round() const noexcept
{
    return impl_->current ? impl_->current->systemverilog_round : 0;
}

std::optional<SchedulerBatchFrontier>
Scheduler::current_batch_frontier() const noexcept
{
    return impl_->active_batch_frontier;
}

} // namespace fsim::runtime

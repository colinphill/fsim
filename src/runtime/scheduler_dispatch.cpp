// SPDX-License-Identifier: Apache-2.0
#include "scheduler_internal.hpp"

namespace fsim::runtime {

RunResult Scheduler::run(std::optional<SimulationTick> until)
{
    if (impl_->in_run) {
        throw std::logic_error("Scheduler::run is not reentrant");
    }
    if (until && *until < impl_->now) {
        throw std::invalid_argument("run time limit is before the current time");
    }

    struct RunGuard {
        bool& running;
        ~RunGuard() { running = false; }
    };
    impl_->in_run = true;
    RunGuard guard { impl_->in_run };
    const auto initial_callbacks = impl_->callbacks;
    auto& batch_entries = impl_->batch_entries;
    auto& batch_payloads = impl_->batch_payloads;
    struct RunScratchGuard {
        std::vector<Entry>& entries;
        std::vector<std::uint64_t>& payloads;

        ~RunScratchGuard()
        {
            entries.clear();
            payloads.clear();
        }
    } scratch_guard { batch_entries, batch_payloads };

    struct BatchSuffixInsertionReservation {
        WorkQueue* queue { };
        std::size_t count { };

        void retain(WorkQueue& target, const std::size_t reserved) noexcept
        {
            queue = &target;
            count = reserved;
        }

        void release() noexcept
        {
            if (queue == nullptr)
                return;
            assert(queue->reserved_insertions_capacity >= count);
            queue->reserved_insertions_capacity -= count;
            queue = nullptr;
            count = 0U;
        }

        ~BatchSuffixInsertionReservation() { release(); }
    };

    auto result = [&](RunStatus status) {
        if (!impl_->current && impl_->future.empty()) {
            impl_->cancellations_since_compaction = 0;
            impl_->cancellation_compaction_threshold = 64U;
        }
        return RunResult { status,
            impl_->now,
            impl_->current ? impl_->current->delta : 0,
            impl_->callbacks - initial_callbacks,
            std::nullopt };
    };

    while (true) {
        if (!impl_->current) {
            if (impl_->stop.load(std::memory_order_relaxed)) {
                return result(RunStatus::stopped);
            }
            while (!impl_->future.empty()
                && impl_->future.begin()->second.empty()) {
                impl_->recycle_future_slot(impl_->future.begin()->second);
                impl_->retain_future_node(impl_->future.begin());
            }
            if (impl_->future.empty()) {
                if (until && impl_->now < *until) {
                    impl_->now = *until;
                    return result(RunStatus::time_limit);
                }
                return result(RunStatus::completed);
            }
            if (until && impl_->future.begin()->first > *until) {
                impl_->now = *until;
                return result(RunStatus::time_limit);
            }
            impl_->load_next_slot();
            if (impl_->slot_start_hook) {
                impl_->slot_start_hook(*this);
            }
        }

        auto& slot = *impl_->current;
        if (impl_->stop.load(std::memory_order_relaxed))
            return result(RunStatus::stopped);
        // A frozen SV batch survives stop/resume. Newly scheduled work stays
        // pending until the whole batch (notably every NBA) has completed.
        bool systemverilog = slot.systemverilog_phase.has_value();
        if (!systemverilog)
            systemverilog = impl_->begin_systemverilog_batch(false);
        bool end_of_slot = false;
        if (!systemverilog && slot.phase == phase_count) {
            if (impl_->stop.load(std::memory_order_relaxed))
                return result(RunStatus::stopped);
            if (!slot.next_delta.empty()) {
                if (slot.delta + 1 >= impl_->options.max_delta_cycles) {
                    throw DeltaCycleLimitError(
                        slot.time, impl_->options.max_delta_cycles,
                        impl_->pending_next_orders(), impl_->recent_signals);
                }
                std::swap(slot.current, slot.next_delta);
                ++slot.delta;
                slot.phase = 0;
                continue;
            }
            systemverilog = impl_->begin_systemverilog_batch(true);
            if (!systemverilog) {
                if (slot.end_of_slot.empty()) {
                    if (impl_->runtime_slot_quiet_hook != nullptr) {
                        impl_->in_runtime_slot_quiet_point = true;
                        impl_->in_callback = true;
                        impl_->runtime_slot_quiet_hook(
                            impl_->runtime_slot_quiet_context);
                        impl_->in_callback = false;
                        impl_->in_runtime_slot_quiet_point = false;
                        // A runtime hook is contractually non-scheduling, but
                        // do not recycle a slot if a faulty hook left work.
                        if (!slot.next_delta.empty()
                            || !slot.current.empty()
                            || !slot.systemverilog.empty()
                            || !slot.systemverilog_running.empty()
                            || !slot.end_of_slot.empty()) {
                            continue;
                        }
                    }
                    impl_->recycle_current_slot(slot);
                    impl_->current.reset();
                    continue;
                }
                end_of_slot = true;
            }
        }
        slot.executing_end_of_slot = end_of_slot;
        auto& queue = systemverilog ? slot.systemverilog_running
            : end_of_slot ? slot.end_of_slot : slot.current.queues[slot.phase];
        const auto phase = systemverilog ? *slot.systemverilog_phase
            : end_of_slot ? SchedulerPhase::postponed
                          : static_cast<SchedulerPhase>(slot.phase);
        if (queue.empty()
            && impl_->stop.load(std::memory_order_relaxed)) {
            return result(RunStatus::stopped);
        }
        bool executed_end_callback = false;
        while (!queue.empty() && !(end_of_slot && executed_end_callback)) {
            BatchSuffixInsertionReservation suffix_reservation;
            if (impl_->stop.load(std::memory_order_relaxed)) {
                return result(RunStatus::stopped);
            }
            if (!systemverilog && !end_of_slot
                && phase == SchedulerPhase::active && impl_->current) {
                const auto ticket_dispatch
                    = queue.next_batch_ticket_dispatch();
                if (ticket_dispatch
                    && ticket_dispatch.ticket->generic_readiness_owned) {
                    auto bounded_dispatch = ticket_dispatch;
                    bounded_dispatch.count = std::min(
                        bounded_dispatch.count, maximum_scheduler_batch_chunk);
                    auto* const ticket = bounded_dispatch.ticket;
                    const auto first_member = ticket->cursor;
                    auto& first = *ticket_dispatch.queue_entry;
                    auto* const batch_task = ticket->ordered_task;
                    const bool valid_pool_index = ticket->pool_index
                        < impl_->generic_readiness_ticket_capacity();
                    const auto* const compact_members = valid_pool_index
                        ? impl_->generic_readiness_compact_members_at(
                            ticket->pool_index).data()
                        : nullptr;
                    const auto compact_member_count = valid_pool_index
                        ? impl_->generic_readiness_compact_members_at(
                            ticket->pool_index).size()
                        : 0U;
                    bool direct_shape_valid = ticket->compact
                        && ticket->active && ticket->group_key
                        && ticket->ordered_task != nullptr
                        && batch_task == ticket->ordered_task
                        && ticket->ordered_owner_lifetime != nullptr
                        && ticket->component_member_capacity != 0U
                        && ticket->count == compact_member_count
                        && ticket->count <= ticket->component_member_capacity
                        && ticket->compact_members == compact_members
                        && first_member < ticket->count
                        && first.order == ticket->member_order(first_member)
                        && first.sequence
                            == ticket->member_sequence(first_member)
                        && bounded_dispatch.count <= ticket->count - first_member
                        && bounded_dispatch.count <= 64U;
                    for (std::size_t index = first_member + 1U;
                         direct_shape_valid && index < ticket->count;
                         ++index) {
                        const auto& previous = compact_members[index - 1U];
                        const auto& current = compact_members[index];
                        direct_shape_valid
                            = previous.order < current.order
                            || (previous.order == current.order
                                && previous.sequence < current.sequence);
                    }
                    if (!direct_shape_valid)
                        throw std::logic_error(
                            "malformed Generic compact readiness ticket");

                    const bool frontier_available
                        = impl_->next_generic_batch_frontier_generation != 0U
                        && bounded_dispatch.count
                            <= impl_->batch_payloads.capacity()
                        && bounded_dispatch.count
                            <= impl_->generic_batch_frontier_entries.capacity();
                    if (!frontier_available) {
                        if (!queue.begin_batch_ticket_dispatch(
                                bounded_dispatch)) {
                            throw std::logic_error(
                                "Generic compact ticket dispatch lost its queue entry");
                        }
                        auto fallback = queue.take_batch_ticket_fallback(
                            bounded_dispatch);
                        queue.finish_batch_ticket_dispatch();
                        impl_->recycle_exhausted_readiness_ticket(ticket);
                        impl_->clear_batch_scratch();
                        auto fallback_task = fallback.take_batch_fallback();
                        fallback.complete();
                        impl_->in_callback = true;
                        impl_->trace(SchedulerTraceKind::task_begin,
                            &fallback, 1U);
                        try {
                            fallback_task(*this);
                        } catch (...) {
                            impl_->trace(SchedulerTraceKind::task_failure,
                                &fallback, 1U);
                            impl_->in_callback = false;
                            throw;
                        }
                        impl_->trace(SchedulerTraceKind::task_end,
                            &fallback, 1U);
                        ++impl_->generic_batch_compaction_stats
                              .generic_readiness_ticket_fallback_members;
                        ++impl_->callbacks;
                        impl_->in_callback = false;
                        continue;
                    }

                    batch_payloads.clear();
                    impl_->generic_batch_frontier_entries.clear();
                    for (std::size_t index = 0U;
                         index < bounded_dispatch.count;
                         ++index) {
                        const auto member_index = first_member + index;
                        const auto member_payload
                            = ticket->member_payload(member_index);
                        batch_payloads.push_back(member_payload);
                        impl_->generic_batch_frontier_entries.push_back({
                            ticket->member_order(member_index),
                            ticket->member_sequence(member_index),
                            member_payload });
                    }
                    if (!queue.begin_batch_ticket_dispatch(bounded_dispatch)) {
                        batch_payloads.clear();
                        impl_->generic_batch_frontier_entries.clear();
                        throw std::logic_error(
                            "Generic compact ticket dispatch lost its queue entry");
                    }

                    const auto generation
                        = impl_->next_generic_batch_frontier_generation;
                    impl_->next_generic_batch_frontier_generation
                        = generation
                                == std::numeric_limits<std::uint64_t>::max()
                        ? 0U : generation + 1U;
                    impl_->active_generic_batch_frontier
                        = SchedulerGenericBatchFrontier {
                            generation, slot.time, slot.delta, phase, 0U,
                            impl_->generic_batch_frontier_entries.size(),
                            impl_->generic_batch_frontier_entries,
                            ticket->group_key, first_member,
                            std::span<const SchedulerGenericTicketMember> {
                                compact_members + first_member,
                                ticket->count - first_member } };
                    ++impl_->generic_batch_compaction_stats.direct_dispatches;
                    impl_->generic_batch_compaction_stats.direct_members
                        += static_cast<std::uint64_t>(bounded_dispatch.count);
                    impl_->in_callback = true;
                    impl_->trace(SchedulerTraceKind::batch_begin,
                        &first, bounded_dispatch.count);
                    SchedulerBatchResult batch_result;
                    try {
                        batch_result = batch_task->execute(
                            *this, batch_payloads);
                    } catch (...) {
                        impl_->active_generic_batch_frontier.reset();
                        impl_->trace(SchedulerTraceKind::batch_failure,
                            &first);
                        queue.finish_batch_ticket_dispatch();
                        impl_->clear_batch_scratch();
                        impl_->in_callback = false;
                        throw;
                    }
                    impl_->active_generic_batch_frontier.reset();
                    if (batch_result.executed > bounded_dispatch.count) {
                        impl_->trace(SchedulerTraceKind::batch_failure,
                            &first);
                        queue.finish_batch_ticket_dispatch();
                        impl_->clear_batch_scratch();
                        impl_->in_callback = false;
                        throw std::logic_error(
                            "scheduler batch consumed an invalid task count");
                    }

                    const auto consumed = batch_result.executed;
                    impl_->trace(batch_result.failure
                            ? SchedulerTraceKind::batch_failure
                            : SchedulerTraceKind::batch_end,
                        &first, consumed);
                    if (consumed == 0U && !batch_result.failure) {
                        auto fallback = queue.take_batch_ticket_fallback(
                            bounded_dispatch);
                        queue.finish_batch_ticket_dispatch();
                        impl_->recycle_exhausted_readiness_ticket(ticket);
                        impl_->clear_batch_scratch();
                        auto fallback_task = fallback.take_batch_fallback();
                        fallback.complete();
                        impl_->trace(SchedulerTraceKind::task_begin,
                            &fallback, 1U);
                        try {
                            fallback_task(*this);
                        } catch (...) {
                            impl_->trace(SchedulerTraceKind::task_failure,
                                &fallback, 1U);
                            impl_->in_callback = false;
                            throw;
                        }
                        impl_->trace(SchedulerTraceKind::task_end,
                            &fallback, 1U);
                        ++impl_->generic_batch_compaction_stats
                              .generic_readiness_ticket_fallback_members;
                        ++impl_->callbacks;
                        impl_->in_callback = false;
                        continue;
                    }

                    for (std::size_t index = 0U;
                         index < consumed;
                         ++index) {
                        Entry trace_member;
                        trace_member.order = ticket->member_order(
                            first_member + index);
                        trace_member.sequence = ticket->member_sequence(
                            first_member + index);
                        impl_->trace(SchedulerTraceKind::task_end,
                            &trace_member, 1U);
                    }
                    queue.consume_batch_ticket_dispatch(
                        bounded_dispatch, consumed);
                    queue.finish_batch_ticket_dispatch();
                    impl_->recycle_exhausted_readiness_ticket(ticket);
                    impl_->clear_batch_scratch();
                    impl_->callbacks += consumed;
                    impl_->in_callback = false;
                    if (batch_result.failure)
                        std::rethrow_exception(batch_result.failure);
                    continue;
                }
            }
            if (systemverilog && !end_of_slot
                && phase == SchedulerPhase::active
                && impl_->current
                && impl_->next_batch_frontier_generation != 0U) {
                const auto ticket_dispatch
                    = queue.next_batch_ticket_dispatch();
                if (ticket_dispatch
                    && ticket_dispatch.count
                        <= impl_->batch_payloads.capacity()
                    && ticket_dispatch.count
                        <= impl_->batch_frontier_entries.capacity()) {
                    auto* const ticket = ticket_dispatch.ticket;
                    const auto first_member = ticket->cursor;
                    auto& first = *ticket_dispatch.queue_entry;
                    auto* const batch_task
                        = ticket->member_task(first_member);
                    const auto group_key = ticket->group_key;
                    bool direct_shape_valid
                        = group_key && batch_task != nullptr;
                    for (std::size_t index = 0U;
                         direct_shape_valid
                            && index < ticket_dispatch.count;
                         ++index) {
                        direct_shape_valid
                            = ticket->member_task(first_member + index)
                                == batch_task
                            && ticket->group_key == group_key;
                        if (direct_shape_valid && !ticket->compact) {
                            const auto& member
                                = ticket->members[first_member + index];
                            direct_shape_valid
                                = member.batch_group_key() == group_key
                                && member.cancel_slots == nullptr;
                        }
                    }
                    if (direct_shape_valid) {
                        batch_entries.clear();
                        batch_payloads.clear();
                        impl_->batch_frontier_entries.clear();
                        for (std::size_t index = 0U;
                             index < ticket_dispatch.count;
                             ++index) {
                            const auto payload = ticket->member_payload(
                                first_member + index);
                            batch_payloads.push_back(payload);
                            impl_->batch_frontier_entries.push_back({
                                ticket->member_order(first_member + index),
                                ticket->member_sequence(first_member + index),
                                payload });
                        }
                        if (queue.begin_batch_ticket_dispatch(
                                ticket_dispatch)) {
                            const auto generation
                                = impl_->next_batch_frontier_generation;
                            impl_->next_batch_frontier_generation
                                = generation
                                        == std::numeric_limits<std::uint64_t>::max()
                                ? 0U : generation + 1U;
                            impl_->active_batch_frontier
                                = SchedulerBatchFrontier {
                                    generation,
                                    slot.time,
                                    slot.delta,
                                    phase,
                                    slot.systemverilog_round,
                                    0U,
                                    impl_->batch_frontier_entries.size(),
                                    impl_->batch_frontier_entries,
                                };
                            ++impl_->systemverilog_batch_compaction_stats
                                  .direct_dispatches;
                            impl_->systemverilog_batch_compaction_stats
                                .direct_members += static_cast<std::uint64_t>(
                                ticket_dispatch.count);
                            impl_->in_callback = true;
                            impl_->trace(SchedulerTraceKind::batch_begin,
                                &first, ticket_dispatch.count);
                            SchedulerBatchResult batch_result;
                            try {
                                batch_result = batch_task->execute(
                                    *this, batch_payloads);
                            } catch (...) {
                                impl_->active_batch_frontier.reset();
                                impl_->active_generic_batch_frontier.reset();
                                impl_->trace(
                                    SchedulerTraceKind::batch_failure,
                                    &first);
                                queue.finish_batch_ticket_dispatch();
                                impl_->clear_batch_scratch();
                                impl_->in_callback = false;
                                throw;
                            }
                            impl_->active_batch_frontier.reset();
                            impl_->active_generic_batch_frontier.reset();
                            if (batch_result.executed
                                > ticket_dispatch.count) {
                                impl_->trace(
                                    SchedulerTraceKind::batch_failure,
                                    &first);
                                queue.finish_batch_ticket_dispatch();
                                impl_->clear_batch_scratch();
                                impl_->in_callback = false;
                                throw std::logic_error(
                                    "scheduler batch consumed an invalid task count");
                            }

                            const auto consumed = batch_result.executed;
                            impl_->trace(batch_result.failure
                                    ? SchedulerTraceKind::batch_failure
                                    : SchedulerTraceKind::batch_end,
                                &first, consumed);
                            if (consumed == 0U && !batch_result.failure) {
                                auto fallback
                                    = queue.take_batch_ticket_fallback(
                                        ticket_dispatch);
                                queue.finish_batch_ticket_dispatch();
                                impl_->recycle_exhausted_readiness_ticket(
                                    ticket);
                                impl_->clear_batch_scratch();
                                auto fallback_task
                                    = fallback.take_batch_fallback();
                                fallback.complete();
                                impl_->trace(
                                    SchedulerTraceKind::task_begin,
                                    &fallback, 1U);
                                try {
                                    fallback_task(*this);
                                } catch (...) {
                                    impl_->trace(
                                        SchedulerTraceKind::task_failure,
                                        &fallback, 1U);
                                    impl_->in_callback = false;
                                    throw;
                                }
                                impl_->trace(SchedulerTraceKind::task_end,
                                    &fallback, 1U);
                                impl_->in_callback = false;
                                ++impl_->callbacks;
                                continue;
                            }

                            for (std::size_t index = 0U;
                                 index < consumed;
                                 ++index) {
                                if (ticket->compact) {
                                    if (impl_->trace_hook) {
                                        Entry trace_member;
                                        trace_member.order
                                            = ticket->member_order(
                                                first_member + index);
                                        trace_member.sequence
                                            = ticket->member_sequence(
                                                first_member + index);
                                        impl_->trace(
                                            SchedulerTraceKind::task_end,
                                            &trace_member, 1U);
                                    }
                                } else {
                                    auto& member = ticket->members[
                                        first_member + index];
                                    impl_->trace(
                                        SchedulerTraceKind::task_end,
                                        &member, 1U);
                                    member.complete();
                                }
                            }
                            queue.consume_batch_ticket_dispatch(
                                ticket_dispatch, consumed);
                            queue.finish_batch_ticket_dispatch();
                            impl_->recycle_exhausted_readiness_ticket(ticket);
                            impl_->clear_batch_scratch();
                            impl_->callbacks += consumed;
                            impl_->in_callback = false;
                            if (batch_result.failure) {
                                std::rethrow_exception(batch_result.failure);
                            }
                            continue;
                        }
                        batch_payloads.clear();
                        impl_->batch_frontier_entries.clear();
                    }
                }
            }
            const auto* const next_entry = queue.next();
            if (next_entry != nullptr
                && next_entry->batch_task() != nullptr) {
                // Reserve before removing any logical task from the queue.
                // A queued ticket can expand one physical entry into many
                // members, so the chunk limit below is measured after each
                // pop rather than from the queue's physical entry count.
                batch_entries.clear();
                batch_payloads.clear();
                if (batch_entries.capacity()
                    < maximum_scheduler_batch_chunk) {
                    batch_entries.reserve(maximum_scheduler_batch_chunk);
                }
                if (batch_payloads.capacity()
                    < maximum_scheduler_batch_chunk) {
                    batch_payloads.reserve(maximum_scheduler_batch_chunk);
                }
                auto& insertions = queue.current_insertions;
                auto& reserved = queue.reserved_insertions_capacity;
                impl_->reuse_queue_vector(insertions);
                const auto available = insertions.max_size()
                    - insertions.size();
                if (maximum_scheduler_batch_chunk > available
                    || reserved
                        > available - maximum_scheduler_batch_chunk) {
                    throw std::length_error(
                        "scheduler batch suffix reservation exceeds queue capacity");
                }
                insertions.reserve(insertions.size() + reserved
                    + maximum_scheduler_batch_chunk);
                reserved += maximum_scheduler_batch_chunk;
                suffix_reservation.retain(queue,
                    maximum_scheduler_batch_chunk);
            }
            BatchTicket* exhausted_ticket { };
            auto entry = queue.pop(exhausted_ticket);
            impl_->recycle_exhausted_readiness_ticket(exhausted_ticket);
            executed_end_callback = end_of_slot;
            if (entry.batch_task() != nullptr) {
                auto* const batch_task = entry.batch_task();
                const auto batch_group_key = entry.batch_group_key();
                batch_entries.push_back(std::move(entry));
                while (const auto* next = queue.next()) {
                    // Batch executors consume a leading prefix. Keep every
                    // chunk within the storage reserved before the first pop,
                    // preserving the exact queue suffix for the next pass.
                    if (next->batch_task() != batch_task
                        || next->batch_group_key() != batch_group_key
                        || batch_entries.size()
                            >= maximum_scheduler_batch_chunk) {
                        break;
                    }
                    auto batch_entry = queue.pop(exhausted_ticket);
                    impl_->recycle_exhausted_readiness_ticket(
                        exhausted_ticket);
                    batch_entries.push_back(std::move(batch_entry));
                }
                for (const auto& candidate : batch_entries) {
                    batch_payloads.push_back(candidate.batch_payload());
                }

                if (systemverilog && impl_->current
                    && batch_entries.size()
                        <= impl_->batch_frontier_entries.capacity()
                    && impl_->next_batch_frontier_generation != 0U) {
                    impl_->batch_frontier_entries.clear();
                    for (const auto& candidate : batch_entries) {
                        impl_->batch_frontier_entries.push_back({ candidate.order,
                            candidate.sequence, candidate.batch_payload() });
                    }
                    const auto generation
                        = impl_->next_batch_frontier_generation;
                    impl_->next_batch_frontier_generation
                        = generation == std::numeric_limits<std::uint64_t>::max()
                        ? 0U : generation + 1U;
                    impl_->active_batch_frontier = SchedulerBatchFrontier {
                        generation,
                        slot.time,
                        slot.delta,
                        phase,
                        slot.systemverilog_round,
                        0U,
                        impl_->batch_frontier_entries.size(),
                        impl_->batch_frontier_entries,
                    };
                }
                if (!systemverilog && !end_of_slot
                    && phase == SchedulerPhase::active && impl_->current
                    && batch_entries.size()
                        <= impl_->generic_batch_frontier_entries.capacity()
                    && impl_->next_generic_batch_frontier_generation != 0U) {
                    impl_->generic_batch_frontier_entries.clear();
                    for (const auto& candidate : batch_entries) {
                        impl_->generic_batch_frontier_entries.push_back({
                            candidate.order, candidate.sequence,
                            candidate.batch_payload() });
                    }
                    const auto generation
                        = impl_->next_generic_batch_frontier_generation;
                    impl_->next_generic_batch_frontier_generation
                        = generation == std::numeric_limits<std::uint64_t>::max()
                        ? 0U : generation + 1U;
                    impl_->active_generic_batch_frontier
                        = SchedulerGenericBatchFrontier {
                            generation, slot.time, slot.delta, phase,
                            0U, impl_->generic_batch_frontier_entries.size(),
                            impl_->generic_batch_frontier_entries,
                            SchedulerBatchGroupKey { }, 0U,
                            std::span<const SchedulerGenericTicketMember> { } };
                }

                impl_->in_callback = true;
                SchedulerBatchResult batch_result;
                impl_->trace(SchedulerTraceKind::batch_begin,
                    &batch_entries.front(), batch_entries.size());
                try {
                    batch_result = batch_task->execute(
                        *this, batch_payloads);
                } catch (...) {
                    impl_->active_batch_frontier.reset();
                    impl_->active_generic_batch_frontier.reset();
                    impl_->trace(SchedulerTraceKind::batch_failure,
                        &batch_entries.front());
                    suffix_reservation.release();
                    for (auto& candidate : batch_entries) {
                        impl_->push_entry(queue, std::move(candidate));
                    }
                    impl_->clear_batch_scratch();
                    impl_->in_callback = false;
                    throw;
                }
                impl_->active_batch_frontier.reset();
                impl_->active_generic_batch_frontier.reset();
                if (batch_result.executed > batch_entries.size()) {
                    impl_->trace(SchedulerTraceKind::batch_failure,
                        &batch_entries.front());
                    suffix_reservation.release();
                    for (auto& candidate : batch_entries) {
                        impl_->push_entry(queue, std::move(candidate));
                    }
                    impl_->clear_batch_scratch();
                    impl_->in_callback = false;
                    throw std::logic_error(
                        "scheduler batch consumed an invalid task count");
                }

                const auto consumed = batch_result.executed;
                impl_->trace(batch_result.failure
                        ? SchedulerTraceKind::batch_failure
                        : SchedulerTraceKind::batch_end,
                    &batch_entries.front(), consumed);
                if (consumed == 0U && !batch_result.failure) {
                    auto fallback = std::move(batch_entries.front());
                    suffix_reservation.release();
                    for (std::size_t index = 1U;
                        index < batch_entries.size(); ++index) {
                        impl_->push_entry(queue,
                            std::move(batch_entries[index]));
                    }
                    impl_->clear_batch_scratch();
                    auto fallback_task = fallback.take_batch_fallback();
                    fallback.complete();
                    impl_->trace(SchedulerTraceKind::task_begin, &fallback, 1);
                    try {
                        fallback_task(*this);
                    } catch (...) {
                        impl_->trace(SchedulerTraceKind::task_failure, &fallback, 1);
                        impl_->in_callback = false;
                        throw;
                    }
                    impl_->trace(SchedulerTraceKind::task_end, &fallback, 1);
                    impl_->in_callback = false;
                    ++impl_->callbacks;
                    continue;
                }

                for (std::size_t index = 0U; index < consumed; ++index) {
                    impl_->trace(SchedulerTraceKind::task_end,
                        &batch_entries[index], 1);
                    batch_entries[index].complete();
                }
                suffix_reservation.release();
                for (auto index = consumed;
                    index < batch_entries.size(); ++index) {
                    impl_->push_entry(queue, std::move(batch_entries[index]));
                }
                impl_->clear_batch_scratch();
                impl_->callbacks += consumed;
                impl_->in_callback = false;
                if (batch_result.failure) {
                    std::rethrow_exception(batch_result.failure);
                }
                continue;
            }
            impl_->in_callback = true;
            impl_->trace(SchedulerTraceKind::task_begin, &entry, 1);
            try {
                if (entry.is_task_descriptor()) {
                    auto descriptor = entry.take_descriptor();
                    descriptor(*this);
                    entry.complete();
                } else {
                    auto task = entry.take_task();
                    task(*this);
                }
            } catch (...) {
                entry.complete();
                impl_->trace(SchedulerTraceKind::task_failure, &entry, 1);
                impl_->in_callback = false;
                throw;
            }
            impl_->trace(SchedulerTraceKind::task_end, &entry, 1);
            impl_->in_callback = false;
            ++impl_->callbacks;
        }
        queue.clear_consumed();
        if (systemverilog && queue.empty()) {
            impl_->systemverilog_batch_tickets.clear();
            impl_->systemverilog_batch_ticket_members.clear();
            impl_->recycle_exhausted_readiness_tickets();
        }
        impl_->recycle_queue_storage(queue);

        if (systemverilog)
            slot.systemverilog_phase.reset();
        else if (!end_of_slot)
            ++slot.phase;
        slot.executing_end_of_slot = false;
        if (impl_->safe_point_hook || !impl_->safe_point_hooks.empty()) {
            const auto safe_point_hook = impl_->safe_point_hook;
            auto& safe_point_hook_snapshot
                = impl_->safe_point_hook_snapshot;
            safe_point_hook_snapshot.clear();
            if (safe_point_hook_snapshot.capacity()
                < impl_->safe_point_hooks.size()) {
                safe_point_hook_snapshot.reserve(
                    impl_->safe_point_hooks.size());
            }
            for (const auto& [token, hook] : impl_->safe_point_hooks) {
                (void)token;
                safe_point_hook_snapshot.push_back(hook);
            }
            // Advance the phase cursor before exposing the safe point. A callback
            // that schedules into the just-completed phase must enter the next
            // delta rather than an already-consumed queue.
            impl_->in_safe_point = true;
            try {
                if (safe_point_hook) {
                    (*safe_point_hook)(*this, phase);
                }
                for (const auto& hook : safe_point_hook_snapshot) {
                    (*hook)(*this, phase);
                }
            } catch (...) {
                safe_point_hook_snapshot.clear();
                impl_->in_safe_point = false;
                throw;
            }
            safe_point_hook_snapshot.clear();
            impl_->in_safe_point = false;
        }
    }
}

} // namespace fsim::runtime

// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace fsim::runtime {

class CancellationSlots {
public:
    struct Identity {
        std::size_t slot;
        std::uint64_t generation;
    };

    [[nodiscard]] Identity acquire(Scheduler::Task task)
    {
        if (free_head_ != no_slot) {
            const auto index = free_head_;
            auto& slot = slots_[index];
            free_head_ = slot.next_free;
            ++slot.generation;
            slot.task = std::move(task);
            slot.active = true;
            return { index, slot.generation };
        }
        slots_.push_back({ 1U, true, no_slot, std::move(task) });
        return { slots_.size() - 1U, 1U };
    }

    [[nodiscard]] bool active(Identity identity) const noexcept
    {
        return identity.slot < slots_.size()
            && slots_[identity.slot].generation == identity.generation
            && slots_[identity.slot].active;
    }

    void release(Identity identity) noexcept
    {
        if (!active(identity))
            return;
        auto& slot = slots_[identity.slot];
        Scheduler::Task released_task;
        released_task.swap(slot.task);
        slot.active = false;
        if (slot.generation != std::numeric_limits<std::uint64_t>::max()) {
            slot.next_free = free_head_;
            free_head_ = identity.slot;
        }
        // A captured payload can reenter Scheduler as it is destroyed. Finish
        // all slot mutations before invoking any of its destructors.
        released_task = nullptr;
    }

    [[nodiscard]] Scheduler::Task take(Identity identity) noexcept
    {
        Scheduler::Task task;
        slots_[identity.slot].task.swap(task);
        release(identity);
        return task;
    }

private:
    static constexpr std::size_t no_slot = std::numeric_limits<std::size_t>::max();

    struct Slot {
        std::uint64_t generation;
        bool active;
        std::size_t next_free;
        Scheduler::Task task;
    };

    std::vector<Slot> slots_;
    std::size_t free_head_ = no_slot;
};

namespace scheduler_detail {

    constexpr std::size_t phase_count = 8;
    constexpr std::size_t maximum_scheduler_batch_chunk = 64U;

    inline void print_scheduler_trace(
        void*, const SchedulerTraceRecord& record) noexcept
    {
        constexpr std::array names {
            "task_begin", "task_end", "task_failure", "batch_begin",
            "batch_end", "batch_failure", "signal_transaction", "signal_change",
            "queue_storage_growth"
        };
        std::fprintf(stderr,
            "FSIM-SCHEDULER event=%s time=%llu delta=%llu phase=%s "
            "order=%llu sequence=%llu count=%zu signal=%u "
            "sv_round=%llu sv=%u end_slot=%u\n",
            names[static_cast<std::size_t>(record.kind)],
            static_cast<unsigned long long>(record.time),
            static_cast<unsigned long long>(record.delta),
            record.phase ? phase_name(*record.phase) : "none",
            static_cast<unsigned long long>(record.order),
            static_cast<unsigned long long>(record.sequence), record.count,
            static_cast<unsigned>(record.signal),
            static_cast<unsigned long long>(record.systemverilog_round),
            static_cast<unsigned>(record.systemverilog),
            static_cast<unsigned>(record.end_of_time_slot));
    }

    [[nodiscard]] constexpr std::size_t phase_index(SchedulerPhase phase)
    {
        return static_cast<std::size_t>(phase);
    }

    struct BatchFallback {
        Scheduler::Task task;
        detail::SchedulerTaskDescriptor descriptor;
        std::shared_ptr<void> owner_lifetime;

        void operator()(Scheduler& scheduler)
        {
            if (descriptor.invoke != nullptr) {
                descriptor(scheduler);
            } else if (task) {
                task(scheduler);
            } else {
                throw std::logic_error {
                    "cannot invoke an empty scheduler batch fallback"
                };
            }
        }
    };

    using CompactBatchTicketMember = SchedulerGenericTicketMember;

    struct BatchEntry {
        SchedulerBatchTask* task { };
        std::uint64_t payload { };
        Scheduler::Task fallback;
        detail::SchedulerTaskDescriptor fallback_descriptor;
        SchedulerBatchGroupKey group_key;
        std::shared_ptr<void> owner_lifetime;
    };

    struct Entry;

    struct BatchTicket {
        Entry* members { };
        std::size_t count { };
        std::size_t cursor { };
        SchedulerBatchGroupKey group_key;
        std::size_t pool_index { std::numeric_limits<std::size_t>::max() };
        bool readiness_owned { };
        bool generic_readiness_owned { };
        bool active { };
        CompactBatchTicketMember* compact_members { };
        SchedulerOrderedBatchTask* ordered_task { };
        std::shared_ptr<void> ordered_owner_lifetime;
        std::size_t component_member_capacity { };
        bool compact { };

        [[nodiscard]] StableOrder member_order(
            std::size_t index) const noexcept;
        [[nodiscard]] std::uint64_t member_sequence(
            std::size_t index) const noexcept;
        [[nodiscard]] std::uint64_t member_payload(
            std::size_t index) const noexcept;
        [[nodiscard]] SchedulerBatchTask* member_task(
            std::size_t index) const noexcept;
        [[nodiscard]] bool member_key_less(
            std::size_t index, const Entry& other) const noexcept;
        [[nodiscard]] Entry take_fallback_member(
            std::size_t index) const noexcept;
    };

    struct BatchTicketEntry {
        BatchTicket* ticket { };
    };

    struct OrderedSystemVerilogTicketEntry {
        InternalSystemVerilogOrderedTicketStorage* storage { };
        std::shared_ptr<void> owner_lifetime;
    };

    struct OrderedSystemVerilogMemberEntry {
        detail::SchedulerTaskDescriptor descriptor;
        InternalSystemVerilogOrderedTicketStorage* storage { };
        bool retires_storage { };
        std::shared_ptr<void> owner_lifetime;
    };

    struct CompactBatchDispatchEntry {
        BatchTicket* ticket { };
        std::size_t member_index { };
    };

    struct CompactBatchFallbackEntry {
        detail::SchedulerTaskDescriptor descriptor;
        std::shared_ptr<void> owner_lifetime;
    };

    using EntryTask = std::variant<Scheduler::Task, BatchEntry,
        BatchTicketEntry,
        OrderedSystemVerilogTicketEntry,
        OrderedSystemVerilogMemberEntry,
        CompactBatchDispatchEntry, CompactBatchFallbackEntry,
        detail::SchedulerTaskDescriptor, std::monostate>;

    struct Entry {
        StableOrder order { };
        std::uint64_t sequence { };
        EntryTask task;
        CancellationSlots* cancel_slots { };
        CancellationSlots::Identity cancellation { };

        [[nodiscard]] bool is_cancelled() const noexcept
        {
            return cancel_slots && !cancel_slots->active(cancellation);
        }

        [[nodiscard]] SchedulerBatchTask* batch_task() const noexcept
        {
            if (const auto* ticket
                = std::get_if<BatchTicketEntry>(&task)) {
                return ticket->ticket->member_task(
                    ticket->ticket->cursor);
            }
            if (const auto* member
                = std::get_if<CompactBatchDispatchEntry>(&task)) {
                return member->ticket->member_task(member->member_index);
            }
            const auto* batch = std::get_if<BatchEntry>(&task);
            return batch == nullptr ? nullptr : batch->task;
        }

        [[nodiscard]] std::uint64_t batch_payload() const noexcept
        {
            if (const auto* ticket
                = std::get_if<BatchTicketEntry>(&task)) {
                return ticket->ticket->member_payload(
                    ticket->ticket->cursor);
            }
            if (const auto* member
                = std::get_if<CompactBatchDispatchEntry>(&task)) {
                return member->ticket->member_payload(member->member_index);
            }
            return std::get<BatchEntry>(task).payload;
        }

        [[nodiscard]] SchedulerBatchGroupKey batch_group_key() const noexcept
        {
            if (const auto* ticket
                = std::get_if<BatchTicketEntry>(&task)) {
                return ticket->ticket->group_key;
            }
            if (const auto* member
                = std::get_if<CompactBatchDispatchEntry>(&task)) {
                return member->ticket->group_key;
            }
            const auto* batch = std::get_if<BatchEntry>(&task);
            return batch == nullptr ? SchedulerBatchGroupKey { }
                                    : batch->group_key;
        }

        [[nodiscard]] bool is_batch_ticket() const noexcept
        {
            return std::holds_alternative<BatchTicketEntry>(task);
        }


        [[nodiscard]] Entry take_ticket_member() noexcept
        {
            auto* const ticket
                = std::get<BatchTicketEntry>(task).ticket;
            assert(ticket != nullptr && ticket->cursor < ticket->count);
            auto member = ticket->compact
                ? ticket->take_fallback_member(ticket->cursor)
                : std::move(ticket->members[ticket->cursor]);
            ++ticket->cursor;
            if (ticket->cursor < ticket->count) {
                order = ticket->member_order(ticket->cursor);
                sequence = ticket->member_sequence(ticket->cursor);
            }
            return member;
        }

        [[nodiscard]] bool ticket_has_members() const noexcept
        {
            const auto* const ticket
                = std::get<BatchTicketEntry>(task).ticket;
            return ticket->cursor < ticket->count;
        }

        [[nodiscard]] bool is_ordered_systemverilog_ticket() const noexcept
        {
            return std::holds_alternative<OrderedSystemVerilogTicketEntry>(task);
        }

        [[nodiscard]] bool is_task_descriptor() const noexcept
        {
            return std::holds_alternative<detail::SchedulerTaskDescriptor>(task)
                || std::holds_alternative<OrderedSystemVerilogMemberEntry>(task)
                || std::holds_alternative<CompactBatchFallbackEntry>(task);
        }

        [[nodiscard]] Entry take_ordered_systemverilog_member() noexcept;

        [[nodiscard]] bool ordered_systemverilog_ticket_has_members() const noexcept;

        void complete() noexcept;

        [[nodiscard]] Scheduler::Task take_task() noexcept
        {
            if (cancel_slots)
                return cancel_slots->take(cancellation);
            return std::move(std::get<Scheduler::Task>(task));
        }

        [[nodiscard]] BatchFallback take_batch_fallback() noexcept
        {
            if (auto* const fallback
                = std::get_if<CompactBatchFallbackEntry>(&task)) {
                return { { }, fallback->descriptor,
                    fallback->owner_lifetime };
            }
            auto& batch = std::get<BatchEntry>(task);
            return { std::move(batch.fallback), batch.fallback_descriptor,
                std::move(batch.owner_lifetime) };
        }

        [[nodiscard]] detail::SchedulerTaskDescriptor take_descriptor() noexcept
        {
            if (const auto* const member
                = std::get_if<OrderedSystemVerilogMemberEntry>(&task)) {
                return member->descriptor;
            }
            if (const auto* const fallback
                = std::get_if<CompactBatchFallbackEntry>(&task)) {
                return fallback->descriptor;
            }
            return std::move(
                std::get<detail::SchedulerTaskDescriptor>(task));
        }
    };

    inline StableOrder BatchTicket::member_order(
        const std::size_t index) const noexcept
    {
        return compact ? compact_members[index].order : members[index].order;
    }

    inline std::uint64_t BatchTicket::member_sequence(
        const std::size_t index) const noexcept
    {
        return compact
            ? compact_members[index].sequence : members[index].sequence;
    }

    inline std::uint64_t BatchTicket::member_payload(
        const std::size_t index) const noexcept
    {
        return compact
            ? compact_members[index].payload : members[index].batch_payload();
    }

    inline SchedulerBatchTask* BatchTicket::member_task(
        const std::size_t index) const noexcept
    {
        return compact ? ordered_task : members[index].batch_task();
    }

    inline bool BatchTicket::member_key_less(
        const std::size_t index, const Entry& other) const noexcept
    {
        const auto order = member_order(index);
        const auto sequence = member_sequence(index);
        return order < other.order
            || (order == other.order && sequence < other.sequence);
    }

    inline Entry BatchTicket::take_fallback_member(
        const std::size_t index) const noexcept
    {
        assert(compact && ordered_task != nullptr
            && index < count && ordered_owner_lifetime != nullptr);
        Entry fallback;
        fallback.order = compact_members[index].order;
        fallback.sequence = compact_members[index].sequence;
        fallback.task = CompactBatchFallbackEntry {
            ordered_task->make_fallback_descriptor(
                compact_members[index].payload),
            ordered_owner_lifetime };
        assert(std::get<CompactBatchFallbackEntry>(fallback.task)
                .descriptor.invoke != nullptr);
        return fallback;
    }

    inline Entry Entry::take_ordered_systemverilog_member() noexcept
    {
        auto& ticket = std::get<OrderedSystemVerilogTicketEntry>(task);
        auto* const storage = ticket.storage;
        assert(storage != nullptr && storage->active
            && storage->cursor < storage->members.size());
        const auto index = storage->cursor++;
        auto member = std::move(storage->members[index]);
        const bool last_member = storage->cursor == storage->members.size();
        Entry result { member.order, member.sequence,
            EntryTask { OrderedSystemVerilogMemberEntry {
                member.task, storage, last_member,
                ticket.owner_lifetime } }, nullptr, { } };
        if (!last_member) {
            order = storage->members[storage->cursor].order;
            sequence = storage->members[storage->cursor].sequence;
        } else {
            task = std::monostate { };
        }
        return result;
    }

    inline bool Entry::ordered_systemverilog_ticket_has_members() const noexcept
    {
        const auto* const entry
            = std::get_if<OrderedSystemVerilogTicketEntry>(&task);
        return entry != nullptr && entry->storage != nullptr
            && entry->storage->active
            && entry->storage->cursor < entry->storage->members.size();
    }

    inline void Entry::complete() noexcept
    {
        if (cancel_slots)
            cancel_slots->release(cancellation);
        if (auto* const entry
            = std::get_if<OrderedSystemVerilogTicketEntry>(&task)) {
            if (entry->storage != nullptr)
                entry->storage->retire();
            entry->storage = nullptr;
            entry->owner_lifetime.reset();
        }
        if (auto* const member
            = std::get_if<OrderedSystemVerilogMemberEntry>(&task)) {
            if (member->retires_storage && member->storage != nullptr)
                member->storage->retire();
            member->storage = nullptr;
            member->owner_lifetime.reset();
        }
        if (auto* const batch = std::get_if<BatchEntry>(&task))
            batch->owner_lifetime.reset();
        if (auto* const fallback
            = std::get_if<CompactBatchFallbackEntry>(&task)) {
            fallback->owner_lifetime.reset();
        }
    }

    struct BatchTicketDispatch {
        BatchTicket* ticket { };
        Entry* queue_entry { };
        std::size_t count { };

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return ticket != nullptr && queue_entry != nullptr
                && count != 0U;
        }
    };

    struct WorkQueue {
        std::vector<Entry> entries;
        // Once dispatch starts, keep the sorted bulk queue fixed. Entries
        // inserted during callbacks live in this min-heap until the phase
        // ends, so a lower-key continuation never re-sorts the bulk suffix.
        std::vector<Entry> current_insertions;
        std::size_t cursor { };
        std::size_t pushes_since_compaction { };
        bool needs_sort { };
        bool prepared { };
        bool has_cancelable { };
        BatchTicket* dispatching_ticket { };
        std::size_t dispatching_ticket_count { };
        Entry compact_dispatch_scratch;
        std::size_t reserved_entries_capacity { };
        std::size_t reserved_insertions_capacity { };

        struct CompactionResult {
            std::size_t tickets { };
            std::size_t members { };
        };

        [[nodiscard]] BatchTicket* find_readiness_ticket(
            SchedulerBatchTask* const task,
            const SchedulerBatchGroupKey key) noexcept
        {
            if (prepared || !key || task == nullptr)
                return nullptr;
            const auto find = [task, key](Entry& entry) -> BatchTicket* {
                if (!entry.is_batch_ticket())
                    return nullptr;
                auto* const ticket
                    = std::get<BatchTicketEntry>(entry.task).ticket;
                return ticket != nullptr && ticket->active
                        && ticket->readiness_owned
                        && !ticket->compact
                        && ticket->cursor == 0U
                        && ticket->group_key == key
                        && ticket->count != 0U
                        && ticket->members[0U].batch_task() == task
                    ? ticket : nullptr;
            };
            for (auto& entry : entries) {
                if (auto* const ticket = find(entry))
                    return ticket;
            }
            for (auto& entry : current_insertions) {
                if (auto* const ticket = find(entry))
                    return ticket;
            }
            return nullptr;
        }

        [[nodiscard]] BatchTicket* find_compact_ticket(
            SchedulerOrderedBatchTask* const task,
            const SchedulerBatchGroupKey key) noexcept
        {
            if (prepared || !key || task == nullptr)
                return nullptr;
            const auto find = [task, key](Entry& entry) -> BatchTicket* {
                if (!entry.is_batch_ticket())
                    return nullptr;
                auto* const ticket
                    = std::get<BatchTicketEntry>(entry.task).ticket;
                return ticket != nullptr && ticket->active
                        && ticket->readiness_owned && ticket->compact
                        && ticket->cursor == 0U
                        && ticket->group_key == key
                        && ticket->ordered_task == task
                        && ticket->count != 0U
                    ? ticket : nullptr;
            };
            for (auto& entry : entries) {
                if (auto* const ticket = find(entry))
                    return ticket;
            }
            for (auto& entry : current_insertions) {
                if (auto* const ticket = find(entry))
                    return ticket;
            }
            return nullptr;
        }

        [[nodiscard]] BatchTicket* find_generic_compact_ticket(
            const SchedulerBatchGroupKey key) noexcept
        {
            if (prepared || !key)
                return nullptr;
            const auto find = [key](Entry& entry) -> BatchTicket* {
                if (!entry.is_batch_ticket())
                    return nullptr;
                auto* const ticket
                    = std::get<BatchTicketEntry>(entry.task).ticket;
                return ticket != nullptr && ticket->active
                        && ticket->generic_readiness_owned
                        && ticket->compact && ticket->cursor == 0U
                        && ticket->group_key == key
                        && ticket->count != 0U
                    ? ticket : nullptr;
            };
            for (auto& entry : entries) {
                if (auto* const ticket = find(entry))
                    return ticket;
            }
            for (auto& entry : current_insertions) {
                if (auto* const ticket = find(entry))
                    return ticket;
            }
            return nullptr;
        }

        [[nodiscard]] bool refresh_readiness_ticket_key(
            BatchTicket* const ticket) noexcept
        {
            if (prepared || ticket == nullptr || ticket->count == 0U)
                return false;
            for (auto& entry : entries) {
                if (entry.is_batch_ticket()
                    && std::get<BatchTicketEntry>(entry.task).ticket == ticket) {
                    entry.order = ticket->member_order(0U);
                    entry.sequence = ticket->member_sequence(0U);
                    needs_sort = true;
                    return true;
                }
            }
            for (auto& entry : current_insertions) {
                if (entry.is_batch_ticket()
                    && std::get<BatchTicketEntry>(entry.task).ticket == ticket) {
                    entry.order = ticket->member_order(0U);
                    entry.sequence = ticket->member_sequence(0U);
                    std::make_heap(current_insertions.begin(),
                        current_insertions.end(), key_later);
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] static bool key_less(
            const Entry& lhs, const Entry& rhs) noexcept
        {
            return lhs.order < rhs.order
                || (lhs.order == rhs.order
                    && lhs.sequence < rhs.sequence);
        }

        [[nodiscard]] static bool key_later(
            const Entry& lhs, const Entry& rhs) noexcept
        {
            return key_less(rhs, lhs);
        }

        void reposition_front_ticket() noexcept
        {
            if (cursor >= entries.size() || !entries[cursor].is_batch_ticket())
                return;
            auto first = entries.begin()
                + static_cast<std::ptrdiff_t>(cursor);
            const auto position = std::upper_bound(first + 1U,
                entries.end(), *first, key_less);
            std::rotate(first, first + 1U, position);
        }

        void discard_cancelled_insertions() noexcept
        {
            while (!current_insertions.empty()
                && current_insertions.front().is_cancelled()) {
                std::pop_heap(current_insertions.begin(),
                    current_insertions.end(), key_later);
                current_insertions.pop_back();
            }
        }

        void compact() noexcept
        {
            // Direct ticket callbacks borrow both the ticket slab and its
            // queue node. Defer cancellation compaction until dispatch ends.
            if (dispatching_ticket != nullptr)
                return;
            entries.erase(entries.begin(),
                entries.begin() + static_cast<std::ptrdiff_t>(cursor));
            cursor = 0;
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                [](const Entry& entry) { return entry.is_cancelled(); }),
                entries.end());
            current_insertions.erase(std::remove_if(
                current_insertions.begin(), current_insertions.end(),
                [](const Entry& entry) { return entry.is_cancelled(); }),
                current_insertions.end());
            std::make_heap(current_insertions.begin(),
                current_insertions.end(), key_later);
            pushes_since_compaction = 0;
            has_cancelable = std::any_of(entries.begin(), entries.end(),
                [](const Entry& entry) { return entry.cancel_slots != nullptr; })
                || std::any_of(current_insertions.begin(),
                    current_insertions.end(), [](const Entry& entry) {
                        return entry.cancel_slots != nullptr;
                    });
            if (entries.empty())
                needs_sort = false;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            for (auto index = cursor; index < entries.size(); ++index) {
                const auto& entry = entries[index];
                if (entry.is_cancelled())
                    continue;
                if (dispatching_ticket != nullptr
                    && entry.is_batch_ticket()
                    && std::get<BatchTicketEntry>(entry.task).ticket
                        == dispatching_ticket) {
                    if (dispatching_ticket->cursor
                            + dispatching_ticket_count
                        < dispatching_ticket->count) {
                        return false;
                    }
                    continue;
                }
                return false;
            }
            return std::none_of(current_insertions.begin(),
                    current_insertions.end(), [](const Entry& entry) {
                        return !entry.is_cancelled();
                    });
        }

        static void reserve_for_push(
            std::vector<Entry>& storage,
            const std::size_t reserved_capacity)
        {
            const auto maximum = storage.max_size();
            const auto size = storage.size();
            if (size >= maximum
                || reserved_capacity >= maximum - size) {
                throw std::length_error {
                    "scheduler queue entry capacity exceeded"
                };
            }

            const auto required = size + reserved_capacity + 1U;
            if (required <= storage.capacity()) {
                return;
            }

            const auto capacity = storage.capacity();
            const auto geometric_capacity = capacity == 0U
                ? std::size_t { 1U }
                : capacity > maximum - capacity
                ? required
                : capacity * 2U;
            storage.reserve(std::max(required, geometric_capacity));
        }

        void push(Entry entry)
        {
            // A cancelled task has already released its payload. Reclaim its
            // queue entry after enough pushes to amortize a scan of the
            // remaining bucket.
            const auto size = entries.size() - cursor
                + current_insertions.size();
            if (has_cancelable && size >= 64U
                && pushes_since_compaction >= size / 2U)
                compact();
            auto needs_sort_after_push = needs_sort;
            if (!prepared && !needs_sort_after_push
                && cursor < entries.size()) {
                needs_sort_after_push = key_less(entry, entries.back());
            }
            const bool cancelable = entry.cancel_slots != nullptr;
            if (prepared) {
                reserve_for_push(
                    current_insertions, reserved_insertions_capacity);
                current_insertions.push_back(std::move(entry));
                std::push_heap(current_insertions.begin(),
                    current_insertions.end(), key_later);
            } else {
                reserve_for_push(entries, reserved_entries_capacity);
                entries.push_back(std::move(entry));
                needs_sort = needs_sort_after_push;
            }
            has_cancelable |= cancelable;
            ++pushes_since_compaction;
        }

        void push_reserved(Entry entry) noexcept
        {
            const auto size = entries.size() - cursor
                + current_insertions.size();
            if (has_cancelable && size >= 64U
                && pushes_since_compaction >= size / 2U) {
                compact();
            }
            if (!prepared && !needs_sort && cursor < entries.size()) {
                const auto& last = entries.back();
                needs_sort = key_less(entry, last);
            }
            const bool cancelable = entry.cancel_slots != nullptr;
            if (prepared) {
                current_insertions.push_back(std::move(entry));
                std::push_heap(current_insertions.begin(),
                    current_insertions.end(), key_later);
            } else {
                entries.push_back(std::move(entry));
            }
            has_cancelable |= cancelable;
            ++pushes_since_compaction;
        }

        void prepare()
        {
            if (needs_sort) {
                std::sort(entries.begin() + static_cast<std::ptrdiff_t>(cursor),
                    entries.end(), key_less);
                needs_sort = false;
            }
            prepared = true;
            while (cursor < entries.size() && entries[cursor].is_cancelled()) {
                ++cursor;
            }
            discard_cancelled_insertions();
            if (has_cancelable && cursor >= 64U
                && cursor >= entries.size() / 2U)
                compact();
        }

        [[nodiscard]] CompactionResult compact_batchable_groups(
            std::vector<BatchTicket>& tickets,
            std::vector<Entry>& ticket_members)
        {
            constexpr std::size_t maximum_tickets = 64U;
            constexpr std::size_t maximum_members_per_ticket = 64U;

            if (!prepared || cursor != 0U || !current_insertions.empty()
                || !tickets.empty() || !ticket_members.empty()) {
                return { };
            }

            struct GroupRange {
                std::size_t begin { };
                std::size_t count { };
                SchedulerBatchGroupKey key;
            };
            std::array<GroupRange, maximum_tickets> groups { };
            std::size_t group_count { };
            std::size_t grouped_members { };
            auto index = cursor;
            while (index < entries.size()
                && group_count < maximum_tickets) {
                const auto& first = entries[index];
                const auto key = first.batch_group_key();
                const auto* const task = first.batch_task();
                if (!key || task == nullptr || first.cancel_slots != nullptr
                    || first.is_batch_ticket()) {
                    ++index;
                    continue;
                }

                auto end = index + 1U;
                while (end < entries.size()
                    && end - index < maximum_members_per_ticket) {
                    const auto& candidate = entries[end];
                    if (candidate.batch_task() != task
                        || candidate.batch_group_key() != key
                        || candidate.cancel_slots != nullptr
                        || candidate.is_batch_ticket()) {
                        break;
                    }
                    ++end;
                }
                const auto count = end - index;
                if (count < 2U) {
                    ++index;
                    continue;
                }

                groups[group_count++] = { index, count, key };
                grouped_members += count;
                index = end;
            }
            if (group_count == 0U)
                return { };

            try {
                tickets.reserve(group_count);
                ticket_members.reserve(grouped_members);
            } catch (const std::bad_alloc&) {
                // Compaction is an optional queue representation change.
                // Preserve every original entry if ticket storage is absent.
                return { };
            }

            static_assert(std::is_nothrow_move_constructible_v<Entry>);
            static_assert(std::is_nothrow_move_assignable_v<Entry>);
            std::size_t read = cursor;
            std::size_t write = cursor;
            std::size_t group_index { };
            while (read < entries.size()) {
                if (group_index < group_count
                    && groups[group_index].begin == read) {
                    const auto group = groups[group_index++];
                    const auto member_begin = ticket_members.size();
                    for (std::size_t member = 0U;
                        member < group.count; ++member) {
                        ticket_members.push_back(
                            std::move(entries[read + member]));
                    }
                    BatchTicket new_ticket;
                    new_ticket.members = ticket_members.data() + member_begin;
                    new_ticket.count = group.count;
                    new_ticket.group_key = group.key;
                    tickets.push_back(std::move(new_ticket));
                    auto& ticket = tickets.back();
                    Entry replacement;
                    replacement.order = ticket.members[0U].order;
                    replacement.sequence = ticket.members[0U].sequence;
                    replacement.task = BatchTicketEntry { &ticket };
                    entries[write++] = std::move(replacement);
                    read += group.count;
                    continue;
                }
                if (write != read)
                    entries[write] = std::move(entries[read]);
                ++write;
                ++read;
            }
            entries.resize(write);
            return { group_count, grouped_members };
        }

        [[nodiscard]] const Entry* next()
        {
            prepare();
            const Entry* bulk = cursor < entries.size()
                ? &entries[cursor] : nullptr;
            if (dispatching_ticket != nullptr && bulk != nullptr
                && bulk->is_batch_ticket()
                && std::get<BatchTicketEntry>(bulk->task).ticket
                    == dispatching_ticket) {
                const auto next_member = dispatching_ticket->cursor
                    + dispatching_ticket_count;
                const Entry* member = nullptr;
                if (next_member < dispatching_ticket->count) {
                    if (dispatching_ticket->compact) {
                        compact_dispatch_scratch = { };
                        compact_dispatch_scratch.order
                            = dispatching_ticket->member_order(next_member);
                        compact_dispatch_scratch.sequence
                            = dispatching_ticket->member_sequence(next_member);
                        compact_dispatch_scratch.task
                            = CompactBatchDispatchEntry {
                                dispatching_ticket, next_member };
                        member = &compact_dispatch_scratch;
                    } else {
                        member = &dispatching_ticket->members[next_member];
                    }
                }
                const Entry* following = cursor + 1U < entries.size()
                    ? &entries[cursor + 1U] : nullptr;
                bulk = member != nullptr && (following == nullptr
                        || key_less(*member, *following))
                    ? member : following;
            }
            const Entry* inserted = current_insertions.empty()
                ? nullptr : &current_insertions.front();
            if (!bulk)
                return inserted;
            if (!inserted)
                return bulk;
            return key_less(*inserted, *bulk) ? inserted : bulk;
        }

        [[nodiscard]] BatchTicketDispatch next_batch_ticket_dispatch()
        {
            prepare();
            if (dispatching_ticket != nullptr || cursor >= entries.size())
                return { };
            auto& entry = entries[cursor];
            if (entry.is_cancelled() || !entry.is_batch_ticket())
                return { };
            if (!current_insertions.empty()
                && key_less(current_insertions.front(), entry)) {
                return { };
            }
            auto* const ticket
                = std::get<BatchTicketEntry>(entry.task).ticket;
            const auto first = ticket->cursor;
            auto end = ticket->count;
            const Entry* boundary = cursor + 1U < entries.size()
                ? &entries[cursor + 1U] : nullptr;
            if (!current_insertions.empty()
                && (boundary == nullptr
                    || key_less(current_insertions.front(), *boundary))) {
                boundary = &current_insertions.front();
            }
            if (boundary != nullptr) {
                while (end > first
                    && !ticket->member_key_less(end - 1U, *boundary)) {
                    --end;
                }
            }
            if (end == first)
                return { };
            return { ticket, &entry, end - first };
        }

        [[nodiscard]] bool begin_batch_ticket_dispatch(
            const BatchTicketDispatch& dispatch) noexcept
        {
            if (dispatch.ticket == nullptr || dispatch.queue_entry == nullptr
                || dispatch.count == 0U || dispatching_ticket != nullptr
                || cursor >= entries.size()
                || &entries[cursor] != dispatch.queue_entry
                || !dispatch.queue_entry->is_batch_ticket()
                || std::get<BatchTicketEntry>(dispatch.queue_entry->task).ticket
                    != dispatch.ticket
                || dispatch.ticket->cursor + dispatch.count
                    > dispatch.ticket->count) {
                return false;
            }
            dispatching_ticket = dispatch.ticket;
            dispatching_ticket_count = dispatch.count;
            return true;
        }

        void finish_batch_ticket_dispatch() noexcept
        {
            if (dispatching_ticket != nullptr && cursor < entries.size()
                && entries[cursor].is_batch_ticket()
                && std::get<BatchTicketEntry>(entries[cursor].task).ticket
                    == dispatching_ticket) {
                reposition_front_ticket();
            }
            dispatching_ticket = nullptr;
            dispatching_ticket_count = 0U;
        }

        void consume_batch_ticket_dispatch(
            const BatchTicketDispatch& dispatch,
            const std::size_t count) noexcept
        {
            assert(dispatching_ticket == dispatch.ticket);
            assert(dispatch.queue_entry != nullptr
                && dispatch.count == dispatching_ticket_count);
            assert(count <= dispatch.count);
            auto& entry = *dispatch.queue_entry;
            auto& ticket = *dispatch.ticket;
            ticket.cursor += count;
            if (ticket.cursor < ticket.count) {
                entry.order = ticket.member_order(ticket.cursor);
                entry.sequence = ticket.member_sequence(ticket.cursor);
            } else {
                ++cursor;
            }
        }

        [[nodiscard]] Entry take_batch_ticket_fallback(
            const BatchTicketDispatch& dispatch) noexcept
        {
            assert(dispatching_ticket == dispatch.ticket);
            auto& entry = *dispatch.queue_entry;
            const auto member_index = dispatch.ticket->cursor++;
            auto fallback = dispatch.ticket->compact
                ? dispatch.ticket->take_fallback_member(member_index)
                : std::move(dispatch.ticket->members[member_index]);
            if (dispatch.ticket->cursor < dispatch.ticket->count) {
                entry.order = dispatch.ticket->member_order(
                    dispatch.ticket->cursor);
                entry.sequence = dispatch.ticket->member_sequence(
                    dispatch.ticket->cursor);
            } else {
                ++cursor;
            }
            return fallback;
        }

        Entry pop(BatchTicket*& exhausted_ticket)
        {
            exhausted_ticket = nullptr;
            prepare();
            assert(dispatching_ticket == nullptr);
            if (current_insertions.empty()
                || (cursor < entries.size()
                    && !key_less(current_insertions.front(),
                        entries[cursor]))) {
                auto& entry = entries.at(cursor);
                if (entry.is_ordered_systemverilog_ticket()) {
                    auto member = entry.take_ordered_systemverilog_member();
                    if (!entry.ordered_systemverilog_ticket_has_members()) {
                        ++cursor;
                    } else {
                        auto position = cursor;
                        while (position + 1U < entries.size()
                            && key_less(entries[position + 1U],
                                entries[position])) {
                            std::swap(entries[position],
                                entries[position + 1U]);
                            ++position;
                        }
                    }
                    return member;
                }
                if (!entry.is_batch_ticket())
                    return std::move(entries.at(cursor++));
                auto* const ticket
                    = std::get<BatchTicketEntry>(entry.task).ticket;
                auto member = entry.take_ticket_member();
                if (!entry.ticket_has_members()) {
                    ++cursor;
                    exhausted_ticket = ticket;
                } else {
                    reposition_front_ticket();
                }
                return member;
            }
            std::pop_heap(current_insertions.begin(),
                current_insertions.end(), key_later);
            auto entry = std::move(current_insertions.back());
            current_insertions.pop_back();
            if (entry.is_ordered_systemverilog_ticket()) {
                auto member = entry.take_ordered_systemverilog_member();
                if (entry.ordered_systemverilog_ticket_has_members()) {
                    current_insertions.push_back(std::move(entry));
                    std::push_heap(current_insertions.begin(),
                        current_insertions.end(), key_later);
                }
                return member;
            }
            if (entry.is_batch_ticket()) {
                auto* const ticket
                    = std::get<BatchTicketEntry>(entry.task).ticket;
                auto member = entry.take_ticket_member();
                if (entry.ticket_has_members()) {
                    current_insertions.push_back(std::move(entry));
                    std::push_heap(current_insertions.begin(),
                        current_insertions.end(), key_later);
                } else {
                    exhausted_ticket = ticket;
                }
                return member;
            }
            return entry;
        }

        void clear_consumed()
        {
            if (empty()) {
                entries.clear();
                current_insertions.clear();
                cursor = 0;
                pushes_since_compaction = 0;
                needs_sort = false;
                prepared = false;
                has_cancelable = false;
            }
        }

        void clear_discarded() noexcept
        {
            entries.clear();
            current_insertions.clear();
            cursor = 0;
            pushes_since_compaction = 0;
            needs_sort = false;
            prepared = false;
            has_cancelable = false;
            reserved_entries_capacity = 0U;
            reserved_insertions_capacity = 0U;
        }

        void cancel_pending() noexcept
        {
            for (auto index = cursor; index < entries.size(); ++index) {
                entries[index].complete();
            }
            for (auto index = 0U; index < current_insertions.size(); ++index)
                current_insertions[index].complete();
        }

        [[nodiscard]] std::vector<StableOrder> pending_orders() const
        {
            std::vector<StableOrder> result;
            result.reserve(
                entries.size() - cursor + current_insertions.size());
            for (auto index = cursor; index < entries.size(); ++index) {
                if (!entries[index].is_cancelled()) {
                    const auto& entry = entries[index];
                    if (entry.is_batch_ticket()) {
                        const auto* ticket
                            = std::get<BatchTicketEntry>(entry.task).ticket;
                        const auto first_member
                            = ticket == dispatching_ticket
                            ? ticket->cursor + dispatching_ticket_count
                            : ticket->cursor;
                        for (auto member = first_member;
                            member < ticket->count; ++member) {
                            result.push_back(
                                ticket->member_order(member));
                        }
                    } else if (entry.is_ordered_systemverilog_ticket()) {
                        const auto* ticket
                            = std::get_if<OrderedSystemVerilogTicketEntry>(
                                &entry.task);
                        for (auto member = ticket->storage->cursor;
                            member < ticket->storage->members.size(); ++member) {
                            result.push_back(
                                ticket->storage->members[member].order);
                        }
                    } else {
                        result.push_back(entry.order);
                    }
                }
            }
            for (const auto& entry : current_insertions) {
                if (entry.is_cancelled())
                    continue;
                if (entry.is_batch_ticket()) {
                    const auto* ticket
                        = std::get<BatchTicketEntry>(entry.task).ticket;
                    for (auto member = ticket->cursor;
                        member < ticket->count; ++member) {
                        result.push_back(ticket->member_order(member));
                    }
                } else if (entry.is_ordered_systemverilog_ticket()) {
                    const auto* ticket
                        = std::get_if<OrderedSystemVerilogTicketEntry>(
                            &entry.task);
                    for (auto member = ticket->storage->cursor;
                        member < ticket->storage->members.size(); ++member) {
                        result.push_back(
                            ticket->storage->members[member].order);
                    }
                } else {
                    result.push_back(entry.order);
                }
            }
            return result;
        }
    };

    struct Bucket {
        std::array<WorkQueue, phase_count> queues;

        [[nodiscard]] std::size_t compact_cancelled() noexcept
        {
            std::size_t retained = 0;
            for (auto& queue : queues) {
                queue.compact();
                retained += queue.entries.size()
                    + queue.current_insertions.size();
            }
            return retained;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return std::all_of(queues.begin(), queues.end(),
                [](const WorkQueue& queue) { return queue.empty(); });
        }

        void cancel_pending() noexcept
        {
            for (auto& queue : queues)
                queue.cancel_pending();
        }

        void clear_discarded() noexcept
        {
            for (auto& queue : queues)
                queue.clear_discarded();
        }
    };

    struct FutureSlot {
        Bucket generic;
        Bucket systemverilog;
        WorkQueue end_of_slot;

        [[nodiscard]] bool empty() const noexcept
        {
            return generic.empty() && systemverilog.empty() && end_of_slot.empty();
        }

        std::size_t compact_cancelled() noexcept
        {
            end_of_slot.compact();
            return generic.compact_cancelled() + systemverilog.compact_cancelled()
                + end_of_slot.entries.size() + end_of_slot.current_insertions.size();
        }

        void cancel_pending() noexcept
        {
            generic.cancel_pending();
            systemverilog.cancel_pending();
            end_of_slot.cancel_pending();
        }

        void clear_discarded() noexcept
        {
            generic.clear_discarded();
            systemverilog.clear_discarded();
            end_of_slot.clear_discarded();
        }
    };

    struct CurrentSlot {
        SimulationTick time { };
        std::uint64_t delta { };
        std::size_t phase { };
        Bucket current;
        Bucket next_delta;
        Bucket systemverilog;
        // New events enter the pending bucket, never the frozen running batch.
        WorkQueue systemverilog_running;
        std::optional<SchedulerPhase> systemverilog_phase;
        std::uint64_t systemverilog_round { };
        std::optional<SchedulerPhase> last_systemverilog_phase;
        WorkQueue end_of_slot;
        bool executing_end_of_slot { };
    };

    [[nodiscard]] inline std::string delta_error_message(SimulationTick time,
        std::uint64_t limit)
    {
        std::ostringstream message;
        message << "maximum delta-cycle count (" << limit
                << ") exceeded at simulation tick " << time;
        return message.str();
    }

} // namespace scheduler_detail

using namespace scheduler_detail;

struct Scheduler::Impl {
    static constexpr std::size_t current_slot_bucket_count = 3U;
    static constexpr std::size_t current_slot_auxiliary_queue_count = 2U;
    static constexpr std::size_t work_queue_vector_count = 2U;
    // Keep one reusable Entry vector for each simultaneous Generic readiness
    // queue role: source entries, batch suffix insertions, and next-delta
    // readiness entries.
    static constexpr std::size_t generic_readiness_queue_buffer_count = 3U;
    static constexpr std::size_t current_slot_work_queue_count
        = phase_count * current_slot_bucket_count
        + current_slot_auxiliary_queue_count;
    static constexpr std::size_t reusable_queue_storage_count
        = work_queue_vector_count * current_slot_work_queue_count;

    explicit Impl(SchedulerOptions scheduler_options)
        : options(scheduler_options)
    {
        if (options.max_delta_cycles == 0) {
            throw std::invalid_argument("max_delta_cycles must be greater than zero");
        }
        batch_payloads.reserve(64U);
        batch_frontier_entries.reserve(64U);
        generic_batch_frontier_entries.reserve(64U);
        recent_signals.reserve(options.recent_signal_capacity);
        const auto* trace = std::getenv("FSIM_TRACE_SCHEDULER");
        if (trace && trace[0] == '1' && trace[1] == '\0')
            trace_hook = print_scheduler_trace;
        for (std::size_t index = 0U;
            index < reusable_queue_storage_count;
            ++index) {
            free_queue_storage_slots[index]
                = reusable_queue_storage_count - index - 1U;
        }
    }

    SchedulerOptions options;
    // A CurrentSlot has three phase buckets, a frozen SV queue, and an
    // end-of-slot queue. Keep one empty vector buffer for each of those queue
    // vectors so completed slots can hand capacity to later ticks without
    // retaining tasks.
    std::array<std::vector<Entry>, reusable_queue_storage_count>
        reusable_queue_storage;
    std::array<std::size_t, reusable_queue_storage_count>
        free_queue_storage_slots { };
    std::array<std::size_t, reusable_queue_storage_count>
        available_queue_storage_slots { };
    std::size_t free_queue_storage_count
        = reusable_queue_storage_count;
    std::size_t available_queue_storage_count { };
    using FutureMap = std::map<SimulationTick, FutureSlot>;
    FutureMap future;
    FutureMap::node_type reusable_future_node;
    std::optional<CurrentSlot> current;
    SimulationTick now { };
    std::uint64_t callbacks { };
    std::uint64_t next_sequence { };
    std::uint64_t next_internal_batch_reservation_id { 1U };
    std::uint64_t active_internal_batch_reservation_id { };
    WorkQueue* active_internal_batch_queue { };
    std::size_t active_internal_batch_reservation_count { };
    bool active_internal_batch_uses_insertions { };
    std::uint64_t active_internal_batch_frontier_generation { };
    InternalSystemVerilogOrderedTicketStorage*
        active_internal_ordered_ticket_storage { };
    std::shared_ptr<void> active_internal_ordered_ticket_owner;
    std::uint64_t next_systemverilog_group_batch_reservation_id { 1U };
    std::uint64_t active_systemverilog_group_batch_reservation_id { };
    WorkQueue* active_systemverilog_group_batch_queue { };
    BatchTicket* active_systemverilog_group_batch_ticket { };
    SchedulerBatchTask* active_systemverilog_group_batch_task { };
    SchedulerOrderedBatchTask*
        active_systemverilog_group_batch_ordered_task { };
    SchedulerBatchGroupKey active_systemverilog_group_batch_key;
    std::size_t active_systemverilog_group_batch_count { };
    std::size_t active_systemverilog_group_batch_previous_count { };
    bool active_systemverilog_group_batch_new_ticket { };
    std::uint64_t next_generic_update_reservation_id { 1U };
    std::uint64_t active_generic_update_reservation_id { };
    WorkQueue* active_generic_update_queue { };
    std::size_t active_generic_update_reservation_count { };
    bool active_generic_update_uses_insertions { };
    std::uint64_t active_generic_update_frontier_generation { };
    std::uint64_t reservation_epoch { };
    std::uint64_t current_phase_revision { };
    bool in_run { };
    bool in_callback { };
    bool in_safe_point { };
    bool in_runtime_slot_quiet_point { };
    bool discarding { };
    std::atomic_bool stop { false };
    std::shared_ptr<CancellationSlots> cancel_slots
        = std::make_shared<CancellationSlots>();
    SlotStartHook slot_start_hook;
    void* runtime_slot_quiet_context { };
    RuntimeSlotQuietHook runtime_slot_quiet_hook { };
    std::shared_ptr<const SafePointHook> safe_point_hook;
    using SafePointHookEntry
        = std::pair<SafePointHookToken, std::shared_ptr<const SafePointHook>>;
    std::vector<SafePointHookEntry> safe_point_hooks;
    std::vector<std::shared_ptr<const SafePointHook>> safe_point_hook_snapshot;
    SafePointHookToken next_safe_point_hook_token { 1U };
    struct DiscardHookEntry {
        DiscardHookToken token { };
        void* context { };
        DiscardHook callback { };
    };
    std::vector<DiscardHookEntry> discard_hooks;
    DiscardHookToken next_discard_hook_token { 1U };
    std::vector<Entry> batch_entries;
    std::vector<std::uint64_t> batch_payloads;
    std::vector<SchedulerBatchFrontierEntry> batch_frontier_entries;
    std::vector<BatchTicket> systemverilog_batch_tickets;
    std::vector<Entry> systemverilog_batch_ticket_members;
    static constexpr std::size_t default_readiness_ticket_count = 64U;
    std::array<BatchTicket, default_readiness_ticket_count>
        systemverilog_readiness_tickets;
    std::array<std::vector<Entry>, default_readiness_ticket_count>
        systemverilog_readiness_ticket_members;
    std::array<std::vector<CompactBatchTicketMember>,
        default_readiness_ticket_count> systemverilog_readiness_compact_members;
    std::vector<BatchTicket> systemverilog_readiness_ticket_overflow;
    std::vector<std::vector<Entry>>
        systemverilog_readiness_ticket_member_overflow;
    std::vector<std::vector<CompactBatchTicketMember>>
        systemverilog_readiness_compact_member_overflow;
    // Physical Generic ticket count and each component's retained member
    // keys are sized from the prepared topology; only one callback offers a
    // bounded task prefix.
    std::array<BatchTicket, default_readiness_ticket_count>
        generic_readiness_tickets;
    std::array<std::vector<CompactBatchTicketMember>,
        default_readiness_ticket_count> generic_readiness_compact_members;
    std::vector<BatchTicket> generic_readiness_ticket_overflow;
    std::vector<std::vector<CompactBatchTicketMember>>
        generic_readiness_compact_member_overflow;
    SchedulerBatchCompactionStats systemverilog_batch_compaction_stats;
    SchedulerBatchCompactionStats generic_batch_compaction_stats;
    std::optional<SchedulerBatchFrontier> active_batch_frontier;
    std::uint64_t next_batch_frontier_generation { 1U };
    std::vector<SchedulerBatchFrontierEntry>
        generic_batch_frontier_entries;
    std::optional<SchedulerGenericBatchFrontier>
        active_generic_batch_frontier;
    std::uint64_t next_generic_batch_frontier_generation { 1U };
    std::vector<RuntimeSignalId> recent_signals;
    std::size_t recent_signal_cursor { };
    std::size_t cancellations_since_compaction { };
    std::size_t cancellation_compaction_threshold { 64U };
    void* trace_context { };
    TraceHook trace_hook { };

    void trace(SchedulerTraceKind kind, const Entry* entry = nullptr,
        std::size_t count = 0, RuntimeSignalId signal = 0) const noexcept
    {
        if (!trace_hook)
            return;
        const auto phase = executing_phase();
        trace_hook(trace_context, { kind, now, current ? current->delta : 0,
            phase, entry ? entry->order : 0, entry ? entry->sequence : 0,
            count, signal, current ? current->systemverilog_round : 0,
            current && current->systemverilog_phase.has_value(),
            current && current->executing_end_of_slot });
    }

    [[nodiscard]] std::optional<SchedulerPhase> executing_phase() const noexcept
    {
        if (!current)
            return std::nullopt;
        if (current->systemverilog_phase)
            return current->systemverilog_phase;
        if (current->executing_end_of_slot)
            return SchedulerPhase::postponed;
        if (current->phase < phase_count)
            return static_cast<SchedulerPhase>(current->phase);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t>
    systemverilog_round_after_pending_phase(
        const CurrentSlot& slot, const SchedulerPhase phase) const noexcept
    {
        if (phase_index(phase) >= phase_count)
            return std::nullopt;
        const bool starts_new_round = !slot.last_systemverilog_phase
            || phase_index(phase)
                <= phase_index(*slot.last_systemverilog_phase);
        if (!starts_new_round)
            return slot.systemverilog_round;
        if (slot.systemverilog_round >= options.max_delta_cycles)
            return std::nullopt;
        return slot.systemverilog_round + 1U;
    }

    [[nodiscard]] SchedulerSystemVerilogKeyReceipt
    systemverilog_key_receipt(const SimulationTick time,
        SchedulerPhase phase, const StableOrder stable_order,
        const std::uint64_t sequence) const noexcept
    {
        if (current && current->time == time) {
            const auto executing = current->systemverilog_phase;
            if (executing && *executing >= SchedulerPhase::reactive
                && *executing <= SchedulerPhase::re_update) {
                if (phase == SchedulerPhase::inactive) {
                    phase = SchedulerPhase::re_inactive;
                } else if (phase == SchedulerPhase::update) {
                    phase = SchedulerPhase::re_update;
                }
            }
            const auto target_round
                = systemverilog_round_after_pending_phase(*current, phase);
            if (!target_round) {
                return { };
            }
            return {
                true,
                time,
                current->delta,
                *target_round,
                phase,
                stable_order,
                sequence,
            };
        }

        // Before a slot is materialized, every valid SV phase queued at the
        // current time belongs to its first SystemVerilog round.
        if (time == now && phase_index(phase) < phase_count) {
            return {
                true,
                time,
                0U,
                1U,
                phase,
                stable_order,
                sequence,
            };
        }
        return { };
    }

    void enqueue_systemverilog_at(SimulationTick, SchedulerPhase, Entry);
    [[nodiscard]] bool begin_systemverilog_batch(bool include_postponed);

    [[nodiscard]] WorkQueue* pending_systemverilog_queue(
        const SimulationTick time, const SchedulerPhase phase)
    {
        if (phase_index(phase) >= phase_count)
            return nullptr;
        if (current && current->time == time) {
            return &current->systemverilog.queues[phase_index(phase)];
        }
        return &future_slot_at(time)
                    .systemverilog.queues[phase_index(phase)];
    }

    [[nodiscard]] std::size_t systemverilog_readiness_ticket_capacity()
        const noexcept
    {
        return default_readiness_ticket_count
            + systemverilog_readiness_ticket_overflow.size();
    }

    [[nodiscard]] BatchTicket& systemverilog_readiness_ticket_at(
        const std::size_t index) noexcept
    {
        assert(index < systemverilog_readiness_ticket_capacity());
        return index < default_readiness_ticket_count
            ? systemverilog_readiness_tickets[index]
            : systemverilog_readiness_ticket_overflow[
                  index - default_readiness_ticket_count];
    }

    [[nodiscard]] std::vector<Entry>&
    systemverilog_readiness_ticket_members_at(
        const std::size_t index) noexcept
    {
        assert(index < systemverilog_readiness_ticket_capacity());
        return index < default_readiness_ticket_count
            ? systemverilog_readiness_ticket_members[index]
            : systemverilog_readiness_ticket_member_overflow[
                  index - default_readiness_ticket_count];
    }

    [[nodiscard]] std::vector<CompactBatchTicketMember>&
    systemverilog_readiness_compact_members_at(
        const std::size_t index) noexcept
    {
        assert(index < systemverilog_readiness_ticket_capacity());
        return index < default_readiness_ticket_count
            ? systemverilog_readiness_compact_members[index]
            : systemverilog_readiness_compact_member_overflow[
                  index - default_readiness_ticket_count];
    }

    [[nodiscard]] std::size_t generic_readiness_ticket_capacity()
        const noexcept
    {
        return default_readiness_ticket_count
            + generic_readiness_ticket_overflow.size();
    }

    [[nodiscard]] std::size_t
    generic_readiness_queue_buffers_with_capacity(
        const std::size_t minimum_capacity) const noexcept
    {
        std::size_t count { };
        for (std::size_t index = 0U;
             index < available_queue_storage_count; ++index) {
            const auto slot = available_queue_storage_slots[index];
            if (reusable_queue_storage[slot].capacity()
                >= minimum_capacity) {
                ++count;
            }
        }
        return count;
    }

    void commit_generic_readiness_queue_buffers(
        std::array<std::vector<Entry>,
            generic_readiness_queue_buffer_count>& staged,
        const std::size_t staged_count,
        const std::size_t minimum_capacity) noexcept
    {
        static_assert(generic_readiness_queue_buffer_count
            <= reusable_queue_storage_count);
        static_assert(std::is_nothrow_swappable_v<std::vector<Entry>>);
        for (std::size_t staged_index = 0U;
             staged_index < staged_count; ++staged_index) {
            auto slot_index = available_queue_storage_count;
            for (std::size_t index = 0U;
                 index < available_queue_storage_count; ++index) {
                const auto slot = available_queue_storage_slots[index];
                if (reusable_queue_storage[slot].capacity()
                    < minimum_capacity) {
                    slot_index = index;
                    break;
                }
            }

            if (slot_index < available_queue_storage_count) {
                const auto slot = available_queue_storage_slots[slot_index];
                reusable_queue_storage[slot].swap(staged[staged_index]);
                continue;
            }

            assert(free_queue_storage_count != 0U);
            const auto slot
                = free_queue_storage_slots[--free_queue_storage_count];
            reusable_queue_storage[slot].swap(staged[staged_index]);
            available_queue_storage_slots[available_queue_storage_count++]
                = slot;
        }
    }

    [[nodiscard]] BatchTicket& generic_readiness_ticket_at(
        const std::size_t index) noexcept
    {
        assert(index < generic_readiness_ticket_capacity());
        return index < default_readiness_ticket_count
            ? generic_readiness_tickets[index]
            : generic_readiness_ticket_overflow[
                  index - default_readiness_ticket_count];
    }

    [[nodiscard]] std::vector<CompactBatchTicketMember>&
    generic_readiness_compact_members_at(
        const std::size_t index) noexcept
    {
        assert(index < generic_readiness_ticket_capacity());
        return index < default_readiness_ticket_count
            ? generic_readiness_compact_members[index]
            : generic_readiness_compact_member_overflow[
                  index - default_readiness_ticket_count];
    }

    [[nodiscard]] bool readiness_ticket_pool_growth_is_safe() const noexcept
    {
        if (in_run || in_callback || discarding
            || active_systemverilog_group_batch_reservation_id != 0U
            || active_systemverilog_group_batch_ticket != nullptr
            || active_internal_batch_reservation_id != 0U
            || active_generic_update_reservation_id != 0U
            || active_internal_ordered_ticket_storage != nullptr
            || active_internal_ordered_ticket_owner != nullptr) {
            return false;
        }

        const auto ticket_has_state = [](const BatchTicket& ticket) {
            return ticket.active || ticket.readiness_owned
                || ticket.generic_readiness_owned || ticket.group_key
                || ticket.members != nullptr
                || ticket.compact_members != nullptr
                || ticket.ordered_task != nullptr
                || ticket.ordered_owner_lifetime != nullptr
                || ticket.count != 0U || ticket.cursor != 0U;
        };
        for (const auto& ticket : systemverilog_readiness_tickets) {
            if (ticket_has_state(ticket))
                return false;
        }
        for (const auto& ticket : systemverilog_readiness_ticket_overflow) {
            if (ticket_has_state(ticket))
                return false;
        }
        for (const auto& ticket : generic_readiness_tickets) {
            if (ticket_has_state(ticket))
                return false;
        }
        for (const auto& ticket : generic_readiness_ticket_overflow) {
            if (ticket_has_state(ticket))
                return false;
        }

        const auto queue_has_ticket_reference = [](const WorkQueue& queue) {
            if (queue.dispatching_ticket != nullptr
                || queue.compact_dispatch_scratch.is_batch_ticket()) {
                return true;
            }
            const auto entries_have_ticket = [](const auto& entries) {
                return std::ranges::any_of(entries,
                    [](const Entry& entry) {
                        return entry.is_batch_ticket();
                    });
            };
            return entries_have_ticket(queue.entries)
                || entries_have_ticket(queue.current_insertions);
        };
        const auto bucket_has_ticket_reference =
            [&queue_has_ticket_reference](const Bucket& bucket) {
                return std::ranges::any_of(bucket.queues,
                    queue_has_ticket_reference);
            };
        if (current
            && (bucket_has_ticket_reference(current->current)
                || bucket_has_ticket_reference(current->next_delta)
                || bucket_has_ticket_reference(current->systemverilog)
                || queue_has_ticket_reference(
                    current->systemverilog_running)
                || queue_has_ticket_reference(current->end_of_slot))) {
            return false;
        }
        for (const auto& [time, slot] : future) {
            (void)time;
            if (bucket_has_ticket_reference(slot.generic)
                || bucket_has_ticket_reference(slot.systemverilog)
                || queue_has_ticket_reference(slot.end_of_slot)) {
                return false;
            }
        }
        return true;
    }

    void grow_systemverilog_readiness_ticket_pool(
        const std::size_t required_capacity)
    {
        static_assert(std::is_nothrow_move_assignable_v<BatchTicket>);
        static_assert(std::is_nothrow_move_assignable_v<std::vector<Entry>>);
        static_assert(std::is_nothrow_move_assignable_v<
            std::vector<CompactBatchTicketMember>>);

        const auto required_overflow
            = required_capacity - default_readiness_ticket_count;
        std::vector<BatchTicket> tickets(required_overflow);
        std::vector<std::vector<Entry>> members(required_overflow);
        std::vector<std::vector<CompactBatchTicketMember>> compact_members(
            required_overflow);

        for (std::size_t index = 0U;
             index < systemverilog_readiness_ticket_overflow.size(); ++index) {
            tickets[index] = std::move(
                systemverilog_readiness_ticket_overflow[index]);
            members[index] = std::move(
                systemverilog_readiness_ticket_member_overflow[index]);
            compact_members[index] = std::move(
                systemverilog_readiness_compact_member_overflow[index]);
        }
        systemverilog_readiness_ticket_overflow.swap(tickets);
        systemverilog_readiness_ticket_member_overflow.swap(members);
        systemverilog_readiness_compact_member_overflow.swap(
            compact_members);
    }

    void grow_generic_readiness_ticket_pool(
        const std::size_t required_capacity)
    {
        static_assert(std::is_nothrow_move_assignable_v<BatchTicket>);
        static_assert(std::is_nothrow_move_assignable_v<
            std::vector<CompactBatchTicketMember>>);

        const auto required_overflow
            = required_capacity - default_readiness_ticket_count;
        std::vector<BatchTicket> tickets(required_overflow);
        std::vector<std::vector<CompactBatchTicketMember>> members(
            required_overflow);
        for (std::size_t index = 0U;
             index < generic_readiness_ticket_overflow.size(); ++index) {
            tickets[index] = std::move(
                generic_readiness_ticket_overflow[index]);
            members[index] = std::move(
                generic_readiness_compact_member_overflow[index]);
        }
        generic_readiness_ticket_overflow.swap(tickets);
        generic_readiness_compact_member_overflow.swap(members);
    }

    [[nodiscard]] BatchTicket* acquire_readiness_ticket(
        const SchedulerBatchGroupKey key) noexcept
    {
        for (std::size_t index = 0U;
             index < systemverilog_readiness_ticket_capacity(); ++index) {
            auto& ticket = systemverilog_readiness_ticket_at(index);
            if (ticket.active)
                continue;
            auto& members
                = systemverilog_readiness_ticket_members_at(index);
            members.clear();
            systemverilog_readiness_compact_members_at(index).clear();
            ticket = BatchTicket { };
            ticket.group_key = key;
            ticket.pool_index = index;
            ticket.readiness_owned = true;
            ticket.active = true;
            return &ticket;
        }
        return nullptr;
    }

    [[nodiscard]] BatchTicket* acquire_generic_readiness_ticket(
        const SchedulerBatchGroupKey key,
        const std::size_t member_capacity)
    {
        for (std::size_t index = 0U;
             index < generic_readiness_ticket_capacity(); ++index) {
            auto& ticket = generic_readiness_ticket_at(index);
            if (ticket.active)
                continue;
            auto& members = generic_readiness_compact_members_at(index);
            members.clear();
            members.reserve(member_capacity);
            ticket = BatchTicket { };
            ticket.group_key = key;
            ticket.pool_index = index;
            ticket.generic_readiness_owned = true;
            ticket.active = true;
            return &ticket;
        }
        return nullptr;
    }

    void release_readiness_ticket(BatchTicket& ticket) noexcept
    {
        if (ticket.generic_readiness_owned) {
            if (ticket.pool_index >= generic_readiness_ticket_capacity())
                return;
            generic_readiness_compact_members_at(ticket.pool_index).clear();
            ticket.members = nullptr;
            ticket.compact_members = nullptr;
            ticket.count = 0U;
            ticket.cursor = 0U;
            ticket.group_key = { };
            ticket.ordered_task = nullptr;
            ticket.ordered_owner_lifetime.reset();
            ticket.component_member_capacity = 0U;
            ticket.compact = false;
            ticket.active = false;
            ticket.readiness_owned = false;
            ticket.generic_readiness_owned = false;
            ticket.pool_index = std::numeric_limits<std::size_t>::max();
            return;
        }
        if (!ticket.readiness_owned
            || ticket.pool_index >= systemverilog_readiness_ticket_capacity()) {
            return;
        }
        systemverilog_readiness_ticket_members_at(ticket.pool_index).clear();
        systemverilog_readiness_compact_members_at(ticket.pool_index).clear();
        ticket.members = nullptr;
        ticket.compact_members = nullptr;
        ticket.count = 0U;
        ticket.cursor = 0U;
        ticket.group_key = { };
        ticket.ordered_task = nullptr;
        ticket.ordered_owner_lifetime.reset();
        ticket.component_member_capacity = 0U;
        ticket.compact = false;
        ticket.active = false;
        ticket.readiness_owned = false;
        ticket.generic_readiness_owned = false;
        ticket.pool_index = std::numeric_limits<std::size_t>::max();
    }

    void release_active_systemverilog_group_batch_reservation() noexcept
    {
        if (active_systemverilog_group_batch_reservation_id == 0U)
            return;
        auto* const queue = active_systemverilog_group_batch_queue;
        if (active_systemverilog_group_batch_new_ticket
            && queue != nullptr && current
            && current->time == now
            && queue == &current->systemverilog.queues[
                phase_index(SchedulerPhase::active)]
            && queue->reserved_entries_capacity != 0U) {
            --queue->reserved_entries_capacity;
        }
        if (active_systemverilog_group_batch_new_ticket
            && active_systemverilog_group_batch_ticket != nullptr) {
            release_readiness_ticket(
                *active_systemverilog_group_batch_ticket);
        }
        clear_active_systemverilog_group_batch_reservation();
    }

    void clear_active_systemverilog_group_batch_reservation() noexcept
    {
        active_systemverilog_group_batch_reservation_id = 0U;
        active_systemverilog_group_batch_queue = nullptr;
        active_systemverilog_group_batch_ticket = nullptr;
        active_systemverilog_group_batch_task = nullptr;
        active_systemverilog_group_batch_ordered_task = nullptr;
        active_systemverilog_group_batch_key = { };
        active_systemverilog_group_batch_count = 0U;
        active_systemverilog_group_batch_previous_count = 0U;
        active_systemverilog_group_batch_new_ticket = false;
    }

    void recycle_exhausted_readiness_ticket(
        BatchTicket* const ticket) noexcept
    {
        if (ticket != nullptr && ticket->active
            && ticket->cursor >= ticket->count) {
            release_readiness_ticket(*ticket);
        }
    }

    void recycle_exhausted_readiness_tickets() noexcept
    {
        for (std::size_t index = 0U;
             index < systemverilog_readiness_ticket_capacity(); ++index) {
            auto& ticket = systemverilog_readiness_ticket_at(index);
            recycle_exhausted_readiness_ticket(&ticket);
        }
        for (std::size_t index = 0U;
             index < generic_readiness_ticket_capacity(); ++index) {
            auto& ticket = generic_readiness_ticket_at(index);
            recycle_exhausted_readiness_ticket(&ticket);
        }
    }

    void reset_readiness_tickets() noexcept
    {
        for (std::size_t index = 0U;
             index < systemverilog_readiness_ticket_capacity(); ++index) {
            auto& ticket = systemverilog_readiness_ticket_at(index);
            if (ticket.readiness_owned)
                release_readiness_ticket(ticket);
        }
        for (std::size_t index = 0U;
             index < generic_readiness_ticket_capacity(); ++index) {
            auto& ticket = generic_readiness_ticket_at(index);
            if (ticket.generic_readiness_owned)
                release_readiness_ticket(ticket);
        }
    }

    void reuse_queue_vector(std::vector<Entry>& queue_vector) noexcept
    {
        if (!queue_vector.empty() || queue_vector.capacity() != 0U
            || available_queue_storage_count == 0U) {
            return;
        }
        // A drained queue can have a larger fixed peak than the queue that
        // most recently returned storage. Prefer the largest available buffer
        // so the same topology does not reserve again on every later tick.
        auto largest = 0U;
        for (auto index = 1U; index < available_queue_storage_count;
             ++index) {
            if (reusable_queue_storage[
                    available_queue_storage_slots[index]].capacity()
                > reusable_queue_storage[
                    available_queue_storage_slots[largest]].capacity()) {
                largest = index;
            }
        }
        std::swap(available_queue_storage_slots[largest],
            available_queue_storage_slots[available_queue_storage_count - 1U]);
        const auto slot
            = available_queue_storage_slots[--available_queue_storage_count];
        queue_vector.swap(reusable_queue_storage[slot]);
        free_queue_storage_slots[free_queue_storage_count++] = slot;
    }

    void recycle_queue_storage(WorkQueue& queue) noexcept
    {
        queue.clear_consumed();
        if (!queue.entries.empty() || !queue.current_insertions.empty())
            return;

        recycle_queue_vector(queue.entries);
        recycle_queue_vector(queue.current_insertions);
    }

    void recycle_queue_vector(std::vector<Entry>& queue_vector) noexcept
    {
        if (!queue_vector.empty() || queue_vector.capacity() == 0U
            || free_queue_storage_count == 0U) {
            return;
        }
        const auto slot = free_queue_storage_slots[--free_queue_storage_count];
        reusable_queue_storage[slot].swap(queue_vector);
        available_queue_storage_slots[available_queue_storage_count++] = slot;
    }

    void recycle_bucket(Bucket& bucket) noexcept
    {
        for (auto& queue : bucket.queues)
            recycle_queue_storage(queue);
    }

    void recycle_future_slot(FutureSlot& slot) noexcept
    {
        recycle_bucket(slot.generic);
        recycle_bucket(slot.systemverilog);
        recycle_queue_storage(slot.end_of_slot);
    }

    [[nodiscard]] FutureSlot& future_slot_at(const SimulationTick time)
    {
        if (const auto existing = future.find(time); existing != future.end())
            return existing->second;
        if (reusable_future_node) {
            reusable_future_node.key() = time;
            return future.insert(std::move(reusable_future_node))
                .position->second;
        }
        return future[time];
    }

    void retain_future_node(const FutureMap::iterator position) noexcept
    {
        if (reusable_future_node) {
            future.erase(position);
            return;
        }
        position->second.clear_discarded();
        reusable_future_node = future.extract(position);
    }

    void recycle_current_slot(CurrentSlot& slot) noexcept
    {
        recycle_bucket(slot.current);
        recycle_bucket(slot.next_delta);
        recycle_bucket(slot.systemverilog);
        recycle_queue_storage(slot.systemverilog_running);
        recycle_queue_storage(slot.end_of_slot);
    }

    void trace_queue_storage_growth(
        const Entry& entry, const std::size_t capacity) noexcept
    {
        const auto previous_callback_state = in_callback;
        in_callback = true;
        trace(SchedulerTraceKind::queue_storage_growth, &entry, capacity);
        in_callback = previous_callback_state;
    }

    void push_entry(WorkQueue& queue, Entry entry)
    {
        reuse_queue_vector(queue.prepared
            ? queue.current_insertions : queue.entries);
        const auto previous_entries_capacity = queue.entries.capacity();
        const auto previous_insertions_capacity
            = queue.current_insertions.capacity();
        queue.push(std::move(entry));
        const auto entries_capacity = queue.entries.capacity();
        const auto insertions_capacity
            = queue.current_insertions.capacity();
        const bool entries_grew = entries_capacity > previous_entries_capacity;
        const bool insertions_grew
            = insertions_capacity > previous_insertions_capacity;
        if (entries_grew)
            trace_queue_storage_growth(entry, entries_capacity);
        if (insertions_grew)
            trace_queue_storage_growth(entry, insertions_capacity);
    }

    void note_cancellation() noexcept
    {
        ++cancellations_since_compaction;
        if (cancellations_since_compaction < cancellation_compaction_threshold)
            return;

        std::size_t retained = 0;
        if (current) {
            retained += current->current.compact_cancelled();
            retained += current->next_delta.compact_cancelled();
            retained += current->systemverilog.compact_cancelled();
            current->systemverilog_running.compact();
            current->end_of_slot.compact();
            retained += current->systemverilog_running.entries.size()
                + current->systemverilog_running.current_insertions.size()
                + current->end_of_slot.entries.size()
                + current->end_of_slot.current_insertions.size();
        }
        for (auto bucket = future.begin(); bucket != future.end();) {
            retained += bucket->second.compact_cancelled();
            if (bucket->second.empty()) {
                recycle_future_slot(bucket->second);
                bucket = future.erase(bucket);
            } else {
                ++bucket;
            }
        }
        cancellations_since_compaction = 0;
        cancellation_compaction_threshold = std::max<std::size_t>(
            64U, retained / 2U);
    }

    void discard_queued() noexcept
    {
        const auto was_discarding = discarding;
        discarding = true;
        release_active_systemverilog_group_batch_reservation();
        if (active_internal_batch_reservation_id != 0U
            && active_internal_batch_queue != nullptr && current
            && active_internal_batch_queue
                == &current->systemverilog.queues[
                    phase_index(SchedulerPhase::active)]) {
            auto& reserved = active_internal_batch_uses_insertions
                ? active_internal_batch_queue->reserved_insertions_capacity
                : active_internal_batch_queue->reserved_entries_capacity;
            if (reserved >= active_internal_batch_reservation_count) {
                reserved -= active_internal_batch_reservation_count;
            } else {
                reserved = 0U;
            }
        }
        active_internal_batch_reservation_id = 0U;
        active_internal_batch_queue = nullptr;
        active_internal_batch_reservation_count = 0U;
        active_internal_batch_uses_insertions = false;
        active_internal_batch_frontier_generation = 0U;
        if (active_internal_ordered_ticket_storage != nullptr)
            active_internal_ordered_ticket_storage->retire();
        active_internal_ordered_ticket_storage = nullptr;
        active_internal_ordered_ticket_owner.reset();
        clear_batch_scratch();
        auto discarded_current = std::move(current);
        current.reset();
        std::map<SimulationTick, FutureSlot> discarded_future;
        discarded_future.swap(future);

        if (discarded_current) {
            discarded_current->current.cancel_pending();
            discarded_current->next_delta.cancel_pending();
            discarded_current->systemverilog.cancel_pending();
            discarded_current->systemverilog_running.cancel_pending();
            discarded_current->end_of_slot.cancel_pending();
            discarded_current->current.clear_discarded();
            discarded_current->next_delta.clear_discarded();
            discarded_current->systemverilog.clear_discarded();
            discarded_current->systemverilog_running.clear_discarded();
            discarded_current->end_of_slot.clear_discarded();
            recycle_current_slot(*discarded_current);
        }
        for (auto& [time, bucket] : discarded_future) {
            (void)time;
            bucket.cancel_pending();
            bucket.clear_discarded();
            recycle_future_slot(bucket);
        }
        discarded_current.reset();
        discarded_future.clear();
        systemverilog_batch_tickets.clear();
        systemverilog_batch_ticket_members.clear();
        reset_readiness_tickets();
        cancellations_since_compaction = 0;
        cancellation_compaction_threshold = 64U;
        discarding = was_discarding;
    }

    void notify_discard_hooks() noexcept
    {
        for (const auto& hook : discard_hooks)
            hook.callback(hook.context);
    }

    void discard_queued_and_notify() noexcept
    {
        const auto was_discarding = discarding;
        discarding = true;
        discard_queued();
        notify_discard_hooks();
        discarding = was_discarding;
    }

    void release_active_internal_systemverilog_reservation() noexcept
    {
        if (active_internal_batch_reservation_id != 0U
            && active_internal_batch_queue != nullptr && current
            && active_internal_batch_queue
                == &current->systemverilog.queues[
                    phase_index(SchedulerPhase::active)]) {
            auto& reserved = active_internal_batch_uses_insertions
                ? active_internal_batch_queue->reserved_insertions_capacity
                : active_internal_batch_queue->reserved_entries_capacity;
            if (reserved >= active_internal_batch_reservation_count) {
                reserved -= active_internal_batch_reservation_count;
            } else {
                reserved = 0U;
            }
        }
        active_internal_batch_reservation_id = 0U;
        active_internal_batch_queue = nullptr;
        active_internal_batch_reservation_count = 0U;
        active_internal_batch_uses_insertions = false;
        active_internal_batch_frontier_generation = 0U;
        if (active_internal_ordered_ticket_storage != nullptr)
            active_internal_ordered_ticket_storage->retire();
        active_internal_ordered_ticket_storage = nullptr;
        active_internal_ordered_ticket_owner.reset();
    }

    void release_active_internal_generic_update_reservation() noexcept
    {
        if (active_generic_update_reservation_id != 0U
            && active_generic_update_queue != nullptr && current
            && active_generic_update_queue
                == &current->current.queues[
                    phase_index(SchedulerPhase::update)]) {
            auto& reserved = active_generic_update_uses_insertions
                ? active_generic_update_queue->reserved_insertions_capacity
                : active_generic_update_queue->reserved_entries_capacity;
            if (reserved >= active_generic_update_reservation_count) {
                reserved -= active_generic_update_reservation_count;
            } else {
                reserved = 0U;
            }
        }
        active_generic_update_reservation_id = 0U;
        active_generic_update_queue = nullptr;
        active_generic_update_reservation_count = 0U;
        active_generic_update_uses_insertions = false;
        active_generic_update_frontier_generation = 0U;
    }

    void clear_batch_scratch() noexcept
    {
        // A ticket is only valid during its matching batch callback. Clear any
        // abandoned reservation before invalidating the frontier.
        release_active_internal_systemverilog_reservation();
        release_active_internal_generic_update_reservation();
        batch_entries.clear();
        batch_payloads.clear();
        batch_frontier_entries.clear();
        active_batch_frontier.reset();
        generic_batch_frontier_entries.clear();
        active_generic_batch_frontier.reset();
    }

    [[nodiscard]] Entry make_entry(
        StableOrder order,
        Task task,
        CancellationSlots* cancellation_slots = nullptr,
        const CancellationSlots::Identity cancellation = { })
    {
        if (!task) {
            throw std::invalid_argument("cannot schedule an empty task");
        }
        if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("scheduler insertion sequence overflow");
        }
        return Entry {
            order, next_sequence++, EntryTask { std::move(task) },
            cancellation_slots, cancellation
        };
    }

    [[nodiscard]] Entry make_batch_entry(
        StableOrder order,
        SchedulerBatchTask& batch_task,
        const std::uint64_t batch_payload,
        Task fallback_task,
        SchedulerBatchGroupKey group_key = { },
        std::shared_ptr<void> owner_lifetime = { })
    {
        if (!fallback_task) {
            throw std::invalid_argument(
                "cannot schedule a batch with an empty fallback task");
        }
        if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("scheduler insertion sequence overflow");
        }
        if (!group_key)
            group_key = { };
        return Entry {
            order, next_sequence++,
            EntryTask { BatchEntry {
                &batch_task, batch_payload, std::move(fallback_task),
                { }, group_key, std::move(owner_lifetime) } },
            nullptr, { }
        };
    }

    [[nodiscard]] Entry make_internal_entry(
        StableOrder order,
        detail::SchedulerTaskDescriptor task)
    {
        if (task.invoke == nullptr) {
            throw std::invalid_argument(
                "cannot schedule an empty task descriptor");
        }
        if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("scheduler insertion sequence overflow");
        }
        return Entry {
            order, next_sequence++, EntryTask { std::move(task) }, nullptr, { }
        };
    }

    void enqueue_at(
        const SimulationTick time,
        SchedulerPhase phase,
        Entry entry,
        const bool adjust_reactive_regions = true)
    {
        if (time < now) {
            throw std::invalid_argument("cannot schedule an event in the past");
        }
        if (adjust_reactive_regions && current && time == current->time
            && current->phase < phase_count) {
            const auto current_phase
                = static_cast<SchedulerPhase>(current->phase);
            if (phase == SchedulerPhase::inactive
                && current_phase >= SchedulerPhase::reactive
                && current_phase <= SchedulerPhase::re_update) {
                phase = SchedulerPhase::re_inactive;
            } else if (phase == SchedulerPhase::update
                && current_phase >= SchedulerPhase::reactive
                && current_phase <= SchedulerPhase::re_update) {
                phase = SchedulerPhase::re_update;
            }
        }
        const auto index = phase_index(phase);
        if (index >= phase_count) {
            throw std::invalid_argument("invalid scheduler phase");
        }

        if (current && time == current->time) {
            const bool phase_finished = index < current->phase;
            auto& bucket = phase_finished ? current->next_delta : current->current;
            push_entry(bucket.queues[index], std::move(entry));
            if (!phase_finished && index == current->phase) {
                ++current_phase_revision;
            }
            return;
        }
        push_entry(future_slot_at(time).generic.queues[index],
            std::move(entry));
    }

    void enqueue_next_delta(
        const SchedulerPhase phase,
        Entry entry)
    {
        const auto index = phase_index(phase);
        if (index >= phase_count) {
            throw std::invalid_argument("invalid scheduler phase");
        }
        if (current) {
            push_entry(current->next_delta.queues[index], std::move(entry));
            return;
        }
        // Before a run begins, "next delta" means the initial delta at now.
        push_entry(future_slot_at(now).generic.queues[index],
            std::move(entry));
    }

    void load_next_slot()
    {
        auto first = future.begin();
        CurrentSlot slot;
        slot.time = first->first;
        slot.current = std::move(first->second.generic);
        slot.systemverilog = std::move(first->second.systemverilog);
        slot.end_of_slot = std::move(first->second.end_of_slot);
        retain_future_node(first);
        now = slot.time;
        current.emplace(std::move(slot));
    }

    [[nodiscard]] std::vector<StableOrder> pending_next_orders() const
    {
        std::vector<StableOrder> result;
        if (!current) {
            return result;
        }
        for (const auto& queue : current->next_delta.queues) {
            auto orders = queue.pending_orders();
            result.insert(result.end(), orders.begin(), orders.end());
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
};

} // namespace fsim::runtime

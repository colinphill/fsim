// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/scheduler.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fsim::runtime;
using fsim::runtime::SchedulerBatchFrontierEntry;
using fsim::runtime::SchedulerSystemVerilogKeyReceipt;
using fsim::tests::runtime::staging_failure_support::
    allocation_failure_was_injected;
using fsim::tests::runtime::staging_failure_support::arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::end_allocation_count;
using fsim::tests::runtime::staging_failure_support::require;

class UnusedBatch final : public SchedulerBatchTask {
public:
    SchedulerBatchResult execute(
        Scheduler&, const std::span<const std::uint64_t> payloads) override
    {
        return { payloads.size(), { } };
    }
};

class RecordingBatch final : public SchedulerBatchTask {
public:
    explicit RecordingBatch(std::vector<std::uint64_t>& visited,
        std::vector<std::uint64_t>* ordered = nullptr) noexcept
        : visited_(visited), ordered_(ordered)
    {
    }

    SchedulerBatchResult execute(Scheduler&,
        const std::span<const std::uint64_t> payloads) override
    {
        visited_.insert(visited_.end(), payloads.begin(), payloads.end());
        if (ordered_ != nullptr) {
            ordered_->insert(ordered_->end(), payloads.begin(), payloads.end());
        }
        return { payloads.size(), { } };
    }

private:
    std::vector<std::uint64_t>& visited_;
    std::vector<std::uint64_t>* ordered_ { };
};

class ExistingTicketBatch final : public SchedulerBatchTask {
public:
    ExistingTicketBatch(std::vector<std::string>& events,
        std::vector<SchedulerBatchFrontierEntry>& frontier_entries)
        : events_(events), frontier_entries_(frontier_entries)
    {
    }

    SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        const auto frontier = scheduler.current_batch_frontier();
        require(frontier && frontier->phase == SchedulerPhase::active
                && frontier->tasks.size() == payloads.size(),
            "resized ticket members retain their active frontier");
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            frontier_entries_.push_back(frontier->tasks[index]);
            events_.push_back("member-"
                + std::to_string(payloads[index]));
        }
        return { payloads.size(), { } };
    }

private:
    std::vector<std::string>& events_;
    std::vector<SchedulerBatchFrontierEntry>& frontier_entries_;
};

struct PartialPrefixOffer {
    std::uint64_t generation { };
    SimulationTick time { };
    std::uint64_t delta { };
    std::uint64_t systemverilog_round { };
    SchedulerPhase phase { SchedulerPhase::active };
    std::size_t count { };
    std::array<SchedulerBatchFrontierEntry, 3U> tasks { };
};

struct PartialPrefixFailureState {
    bool systemverilog { };
    std::size_t calls { };
    std::array<PartialPrefixOffer, 2U> offers { };
    std::array<std::size_t, 4U> executions { };
    std::array<std::size_t, 4U> fallbacks { };
    bool invalid_frontier { };
    bool callback_allocation_failed { };
    bool allocation_measurement_started { };
    std::size_t allocations_after_callback { };
};

class PartialPrefixFailureBatch final : public SchedulerBatchTask {
public:
    explicit PartialPrefixFailureBatch(PartialPrefixFailureState& state) noexcept
        : state_(state)
    {
    }

    SchedulerBatchResult execute(Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        const auto call = state_.calls++;
        if (call >= state_.offers.size()) {
            state_.invalid_frontier = true;
            return { };
        }

        auto& offer = state_.offers[call];
        if (state_.systemverilog) {
            const auto frontier = scheduler.current_batch_frontier();
            if (!frontier || frontier->phase != SchedulerPhase::active
                || frontier->tasks.size() != payloads.size()
                || payloads.size() > offer.tasks.size()) {
                state_.invalid_frontier = true;
                return { };
            }
            offer.generation = frontier->generation;
            offer.time = frontier->time;
            offer.delta = frontier->delta;
            offer.systemverilog_round = frontier->systemverilog_round;
            offer.phase = frontier->phase;
            offer.count = payloads.size();
            for (std::size_t index = 0U; index < payloads.size(); ++index) {
                offer.tasks[index] = frontier->tasks[index];
                state_.invalid_frontier |= frontier->tasks[index].payload
                    != payloads[index];
            }
        } else {
            const auto frontier = scheduler.current_generic_batch_frontier();
            if (!frontier || frontier->phase != SchedulerPhase::active
                || frontier->tasks.size() != payloads.size()
                || payloads.size() > offer.tasks.size()) {
                state_.invalid_frontier = true;
                return { };
            }
            offer.generation = frontier->generation;
            offer.time = frontier->time;
            offer.delta = frontier->delta;
            offer.phase = frontier->phase;
            offer.count = payloads.size();
            for (std::size_t index = 0U; index < payloads.size(); ++index) {
                offer.tasks[index] = frontier->tasks[index];
                state_.invalid_frontier |= frontier->tasks[index].payload
                    != payloads[index];
            }
        }

        if (call == 0U) {
            if (payloads.empty() || payloads.front() >= state_.executions.size()) {
                state_.invalid_frontier = true;
                return { };
            }

            // The first member has completed its side effect. The following
            // callback allocation fails, so only the remaining suffix may be
            // offered again after Scheduler requeues it.
            ++state_.executions[payloads.front()];
            arm_allocation_failure(0U);
            std::exception_ptr failure;
            try {
                void* const allocation = ::operator new(1U);
                ::operator delete(allocation);
            } catch (const std::bad_alloc&) {
                failure = std::current_exception();
            }
            state_.callback_allocation_failed
                = allocation_failure_was_injected() && failure != nullptr;

            // Measure only scheduler work after the injected callback failure.
            // This catches a vector growth while the accepted prefix is being
            // retired and the exact suffix is reinserted.
            begin_allocation_count();
            state_.allocation_measurement_started = true;
            return { 1U, std::move(failure) };
        }

        for (const auto payload : payloads) {
            if (payload >= state_.executions.size()) {
                state_.invalid_frontier = true;
                continue;
            }
            ++state_.executions[payload];
        }
        return { payloads.size(), { } };
    }

private:
    PartialPrefixFailureState& state_;
};

struct ColdPrepopState {
    std::size_t calls { };
    std::size_t count { };
    std::array<SchedulerBatchFrontierEntry, 3U> tasks { };
    std::array<std::size_t, 4U> executions { };
    std::array<std::size_t, 4U> fallbacks { };
    bool invalid_frontier { };
};

class ColdPrepopBatch final : public SchedulerBatchTask {
public:
    explicit ColdPrepopBatch(ColdPrepopState& state) noexcept
        : state_(state)
    {
    }

    SchedulerBatchResult execute(Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        ++state_.calls;
        const auto frontier = scheduler.current_generic_batch_frontier();
        if (!frontier || frontier->phase != SchedulerPhase::active
            || frontier->tasks.size() != payloads.size()
            || payloads.size() > state_.tasks.size()) {
            state_.invalid_frontier = true;
            return { };
        }
        state_.count = payloads.size();
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            state_.tasks[index] = frontier->tasks[index];
            state_.invalid_frontier |= frontier->tasks[index].payload
                != payloads[index];
            if (payloads[index] < state_.executions.size()) {
                ++state_.executions[payloads[index]];
            } else {
                state_.invalid_frontier = true;
            }
        }
        return { payloads.size(), { } };
    }

private:
    ColdPrepopState& state_;
};

class OrderedGroupBatch final : public SchedulerBatchTask {
public:
    OrderedGroupBatch(std::vector<std::string>& events,
        const char group_name) noexcept
        : events_(events), group_name_(group_name)
    {
    }

    SchedulerBatchResult execute(Scheduler&,
        const std::span<const std::uint64_t> payloads) override
    {
        for (const auto payload : payloads) {
            events_.push_back(std::string(1U, group_name_)
                + std::to_string(payload));
        }
        return { payloads.size(), { } };
    }

private:
    std::vector<std::string>& events_;
    char group_name_ { };
};

struct OrdinaryQueueGrowthCapture {
    std::size_t queue_capacity { };
    std::size_t growth_events { };

    static void receive(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        if (record.kind != SchedulerTraceKind::queue_storage_growth)
            return;
        auto& capture = *static_cast<OrdinaryQueueGrowthCapture*>(context);
        capture.queue_capacity = record.count;
        ++capture.growth_events;
    }
};

void test_ordinary_queue_growth_failure_preserves_work_and_order()
{
    constexpr std::size_t maximum_queued_tasks = 4096U;
    constexpr std::size_t minimum_queued_tasks = 256U;

    Scheduler scheduler;
    OrdinaryQueueGrowthCapture growth;
    scheduler.set_trace_hook(&growth, OrdinaryQueueGrowthCapture::receive);

    std::vector<std::uint64_t> executed;
    executed.reserve(maximum_queued_tasks + 1U);

    // Construct every callable before measuring scheduler queue allocations.
    // Moving these small tasks into entries must not add unrelated allocator
    // traffic to the queue-growth count.
    std::vector<Scheduler::Task> tasks;
    tasks.reserve(maximum_queued_tasks);
    for (std::size_t index = 0U;
         index < maximum_queued_tasks; ++index) {
        const auto id = static_cast<std::uint64_t>(index + 1U);
        tasks.emplace_back([&executed, id](Scheduler&) {
            executed.push_back(id);
        });
    }
    Scheduler::Task failed_task = [&executed](Scheduler&) {
        executed.push_back(std::numeric_limits<std::uint64_t>::max());
    };
    Scheduler::Task retry_task = [&executed](Scheduler&) {
        executed.push_back(0U);
    };

    std::size_t queued_tasks { };
    begin_allocation_count();
    while (queued_tasks < minimum_queued_tasks
        || queued_tasks < growth.queue_capacity) {
        require(queued_tasks < maximum_queued_tasks,
            "the ordinary queue-growth fixture stays within its bounded task set");
        scheduler.schedule_at(0U, SchedulerPhase::active,
            static_cast<StableOrder>(queued_tasks + 1U),
            std::move(tasks[queued_tasks]));
        ++queued_tasks;
    }
    const auto growth_allocations = end_allocation_count();
    require(growth.growth_events != 0U
            && queued_tasks >= minimum_queued_tasks
            && growth_allocations < queued_tasks / 4U,
        "ordinary queue growth uses bounded reallocations without fixing vector capacities");

    const auto growths_before_failure = growth.growth_events;
    bool insertion_failed { };
    arm_allocation_failure(0U);
    try {
        // Stable order 0 sets up the lower-key insertion path while the queue
        // is full. The failure must leave all already queued tasks intact.
        scheduler.schedule_at(0U, SchedulerPhase::active, 0U,
            std::move(failed_task));
    } catch (const std::bad_alloc&) {
        insertion_failed = true;
    }
    const bool allocation_was_injected
        = allocation_failure_was_injected();
    clear_allocation_failure();
    require(insertion_failed && allocation_was_injected
            && growth.growth_events == growths_before_failure
            && scheduler.has_pending(),
        "a failed ordinary queue growth publishes no partial task or growth event");

    scheduler.schedule_at(0U, SchedulerPhase::active, 0U,
        std::move(retry_task));
    require(scheduler.run().status == RunStatus::completed,
        "ordinary queue work drains after a failed growth and retry");
    require(executed.size() == queued_tasks + 1U
            && executed.front() == 0U,
        "the retry runs once before the preserved higher-key tasks");
    for (std::size_t index = 0U; index < queued_tasks; ++index) {
        require(executed[index + 1U]
                == static_cast<std::uint64_t>(index + 1U),
            "failed ordinary insertion preserves every queued task in key order");
    }
}

void test_reservation_failure_keeps_foreign_suffix_ordered()
{
    for (const std::size_t fail_allocation : { 0U, 1U }) {
        Scheduler scheduler;
        UnusedBatch batch;
        std::vector<std::string> events;
        bool reservation_declined { };
        bool failure_was_injected { };
        const SchedulerBatchGroupKey group_key { 41U, 3U };

        scheduler.schedule_systemverilog(SchedulerPhase::active, 10U,
            [&](Scheduler& current) {
                events.push_back("prefix");
                // This foreign key is already in the pending queue when
                // reservation preflight fails. Ordinary fallback tasks must
                // remain on either side of it in stable-key order.
                current.schedule_systemverilog(SchedulerPhase::active, 30U,
                    [&events](Scheduler&) {
                        events.push_back("foreign-30");
                    });

                arm_allocation_failure(fail_allocation);
                auto reservation = current.reserve_systemverilog_group_batch(
                    SchedulerPhase::active, batch, group_key, 2U);
                failure_was_injected = allocation_failure_was_injected();
                clear_allocation_failure();
                reservation_declined = !reservation;
                require(reservation_declined && failure_was_injected,
                    "every reservation allocation failure declines before exposing a member");

                current.schedule_systemverilog(SchedulerPhase::active, 20U,
                    [&events](Scheduler&) {
                        events.push_back("member-20");
                    });
                current.schedule_systemverilog(SchedulerPhase::active, 40U,
                    [&events](Scheduler&) {
                        events.push_back("member-40");
                    });
            });

        const auto result = scheduler.run();
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(result.status == RunStatus::completed
                && events == std::vector<std::string> {
                    "prefix", "member-20", "foreign-30", "member-40" },
            "ordinary fallback preserves the complete suffix and foreign ordering");
        require(reservation_declined && failure_was_injected
                && stats.tickets == 0U && stats.members == 0U
                && stats.direct_dispatches == 0U,
            "failed invisible reservation creates no grouped scheduler work");
    }
}

void test_existing_ticket_cancel_after_growth_keeps_members()
{
    Scheduler scheduler;
    std::vector<std::string> events;
    std::vector<SchedulerBatchFrontierEntry> frontier_entries;
    ExistingTicketBatch batch { events, frontier_entries };
    bool reservation_succeeded { };
    std::size_t reserve_allocations { };
    const SchedulerBatchGroupKey group_key { 13U, 1U };

    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            const auto old_fallback = [](Scheduler&) {
                throw std::runtime_error(
                    "existing ticket member unexpectedly fell back");
            };
            require(current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, 20U, batch, 20U,
                        old_fallback, group_key)
                    && current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, 30U, batch, 30U,
                        old_fallback, group_key),
                "fixture creates a visible two-member readiness ticket");

            // Grow the visible member slab to its ticket bound. Measuring the
            // actual allocation keeps this a pointer-relocation witness rather
            // than relying on a library-specific vector growth policy.
            begin_allocation_count();
            auto reservation = current.reserve_systemverilog_group_batch(
                SchedulerPhase::active, batch, group_key, 62U);
            reserve_allocations = end_allocation_count();
            reservation_succeeded = static_cast<bool>(reservation);
            require(reservation_succeeded && reserve_allocations != 0U,
                "existing visible ticket grows its member slab before cancellation");
            reservation.cancel();

            current.schedule_systemverilog(SchedulerPhase::active, 25U,
                [&events](Scheduler&) { events.push_back("foreign-25"); });
        });

    require(scheduler.run().status == RunStatus::completed
            && events == std::vector<std::string> {
                "member-20", "foreign-25", "member-30" },
        "cancelled slab growth preserves old members around a foreign queue key");
    require(frontier_entries.size() == 2U
            && frontier_entries[0U].stable_order == 20U
            && frontier_entries[1U].stable_order == 30U,
        "resized ticket dispatch retains each original stable order");
    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(reservation_succeeded && reserve_allocations != 0U
            && stats.tickets == 1U && stats.members == 2U
            && stats.direct_dispatches == 2U && stats.direct_members == 2U,
        "cancelled reservation growth adds no visible members or ticket");
}

void test_existing_ticket_growth_before_invalid_member_keeps_members()
{
    Scheduler scheduler;
    std::vector<std::string> events;
    std::vector<SchedulerBatchFrontierEntry> frontier_entries;
    std::vector<std::uint64_t> filler_payloads;
    ExistingTicketBatch batch { events, frontier_entries };
    const SchedulerBatchGroupKey group_key { 17U, 2U };

    // Calibrate the allocations made while constructing the same empty-task
    // exception without a visible readiness ticket or member-vector reserve.
    std::size_t empty_fallback_allocations { };
    bool empty_fallback_rejected { };
    begin_allocation_count();
    try {
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, 1U, batch, 1U, { }, group_key);
    } catch (const std::invalid_argument&) {
        empty_fallback_rejected = true;
    }
    empty_fallback_allocations = end_allocation_count();
    require(empty_fallback_rejected,
        "empty fallback is rejected before creating a batch entry");

    bool grew_before_exception { };
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            const auto member_fallback = [](Scheduler&) {
                throw std::runtime_error(
                    "visible readiness member unexpectedly fell back");
            };
            require(current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, 20U, batch, 20U,
                        member_fallback, group_key)
                    && current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, 40U, batch, 40U,
                        member_fallback, group_key),
                "fixture creates a visible ticket before the foreign key");
            current.schedule_systemverilog(SchedulerPhase::active, 30U,
                [&events](Scheduler&) { events.push_back("foreign-30"); });

            // Advance the visible ticket until its next member reserve grows
            // the slab. The deliberately empty fallback then throws after the
            // reserve, exercising the pointer refresh before that exception.
            for (std::size_t index = 0U; index < 62U; ++index) {
                std::size_t allocations { };
                bool rejected { };
                begin_allocation_count();
                try {
                    static_cast<void>(
                        current.schedule_systemverilog_readiness_member(
                            SchedulerPhase::active, 500U, batch, 500U, { },
                            group_key));
                } catch (const std::invalid_argument&) {
                    rejected = true;
                }
                allocations = end_allocation_count();
                require(rejected,
                    "empty fallback throws after existing-ticket lookup");
                if (allocations > empty_fallback_allocations) {
                    grew_before_exception = true;
                    break;
                }

                const auto payload = 100U + index;
                const auto order = 50U + static_cast<StableOrder>(index);
                require(current.schedule_systemverilog_readiness_member(
                            SchedulerPhase::active, order, batch, payload,
                            member_fallback, group_key),
                    "fixture advances ticket size toward its next allocation");
                filler_payloads.push_back(payload);
            }
            require(grew_before_exception,
                "a relocating reserve precedes the invalid fallback exception");
        });

    require(scheduler.run().status == RunStatus::completed,
        "ticket with an exception after growth remains dispatchable");
    std::vector<std::string> expected_events {
        "member-20", "foreign-30", "member-40" };
    for (const auto payload : filler_payloads) {
        expected_events.push_back("member-" + std::to_string(payload));
    }
    require(events == expected_events,
        "old and later ticket members retain order around the foreign entry");
    require(frontier_entries.size() == filler_payloads.size() + 2U
            && frontier_entries[0U].stable_order == 20U
            && frontier_entries[1U].stable_order == 40U,
        "the relocated visible ticket retains its original member keys");
}

void test_second_group_reservation_failure_keeps_interleaved_order()
{
    Scheduler scheduler;
    std::vector<std::string> events;
    OrderedGroupBatch first_group { events, 'A' };
    OrderedGroupBatch second_group { events, 'B' };
    bool first_group_committed { };
    bool second_group_declined { };
    bool failure_was_injected { };

    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            const auto fallback = [](Scheduler&) {
                throw std::runtime_error(
                    "committed component member unexpectedly fell back");
            };
            std::array<Scheduler::SystemVerilogGroupBatchMember, 2U>
                first_members { {
                    { 10U, 10U, fallback, { }, { } },
                    { 40U, 40U, fallback, { }, { } },
                } };
            auto first_reservation
                = current.reserve_systemverilog_group_batch(
                    SchedulerPhase::active, first_group, { 31U, 1U }, 2U);
            require(first_reservation
                    && first_reservation.commit(first_members),
                "first certified group commits before the second is staged");
            first_group_committed = true;

            current.schedule_systemverilog(SchedulerPhase::active, 30U,
                [&events](Scheduler&) { events.push_back("foreign-30"); });

            arm_allocation_failure(0U);
            auto second_reservation
                = current.reserve_systemverilog_group_batch(
                    SchedulerPhase::active, second_group, { 31U, 2U }, 2U);
            failure_was_injected = allocation_failure_was_injected();
            clear_allocation_failure();
            second_group_declined = !second_reservation;
            require(second_group_declined && failure_was_injected,
                "second group declines before exposing members on allocation failure");

            current.schedule_systemverilog(SchedulerPhase::active, 20U,
                [&events](Scheduler&) { events.push_back("B20"); });
            current.schedule_systemverilog(SchedulerPhase::active, 50U,
                [&events](Scheduler&) { events.push_back("B50"); });
        });

    require(scheduler.run().status == RunStatus::completed
            && events == std::vector<std::string> {
                "A10", "B20", "foreign-30", "A40", "B50" },
        "second-group fallback preserves each member once around foreign work");
    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(first_group_committed && second_group_declined
            && failure_was_injected && stats.tickets == 1U
            && stats.members == 2U && stats.direct_members == 2U,
        "only the first group commits while the second remains ordinary work");
}

void test_underfilled_ordinary_group_reservation()
{
    Scheduler scheduler;
    std::vector<std::string> events;
    std::vector<SchedulerBatchFrontierEntry> issued_members;
    ExistingTicketBatch batch { events, issued_members };
    const SchedulerBatchGroupKey group_key { 51U, 7U };

    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            auto empty = current.reserve_systemverilog_group_batch(
                SchedulerPhase::active, batch, group_key, 4U);
            require(empty && empty.commit(
                        std::span<Scheduler::SystemVerilogGroupBatchMember> { }),
                "an empty ordinary reservation releases its capacity without work");

            std::array<Scheduler::SystemVerilogGroupBatchMember, 1U> member { {
                { 20U, 20U,
                    [](Scheduler&) {
                        throw std::runtime_error(
                            "underfilled ordinary member used fallback");
                    }, { }, { } },
            } };
            auto underfilled = current.reserve_systemverilog_group_batch(
                SchedulerPhase::active, batch, group_key, 4U);
            require(underfilled && underfilled.commit(member),
                "an ordinary reservation commits fewer members than its capacity");
            current.schedule_systemverilog(SchedulerPhase::active, 30U,
                [&](Scheduler&) { events.push_back("foreign-30"); });
        });

    require(scheduler.run().status == RunStatus::completed
            && events == std::vector<std::string> {
                "member-20", "foreign-30" }
            && issued_members.size() == 1U
            && issued_members[0U].stable_order == 20U
            && issued_members[0U].sequence == 1U,
        "empty and underfilled ordinary reservations preserve fresh keys and foreign order");
    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(stats.tickets == 1U && stats.members == 1U
            && stats.entries_elided == 0U,
        "ordinary reservation statistics count actual members only");
}

void test_reserved_receipt_commit_allocates_nothing()
{
    Scheduler scheduler;
    UnusedBatch batch;
    std::array<SchedulerSystemVerilogKeyReceipt, 2U> receipts;
    bool committed { };
    std::size_t commit_allocations { };

    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            const auto fallback = [](Scheduler&) { };
            std::array<Scheduler::SystemVerilogGroupBatchMember, 2U> members { {
                { 10U, 101U, fallback, { }, { } },
                { 20U, 202U, fallback, { }, { } },
            } };
            auto reservation = current.reserve_systemverilog_group_batch(
                SchedulerPhase::active, batch, { 91U, 1U }, members.size());
            require(static_cast<bool>(reservation),
                "the receipt allocation fixture reserves its fixed ticket before measurement");

            begin_allocation_count();
            committed = reservation.commit(members, receipts);
            commit_allocations = end_allocation_count();
        });

    require(scheduler.run().status == RunStatus::completed
            && committed && commit_allocations == 0U,
        "a pre-reserved readiness commit publishes receipts without allocation");
    require(receipts[0U].valid && receipts[0U].time == 0U
            && receipts[0U].delta == 0U
            && receipts[0U].systemverilog_round == 2U
            && receipts[0U].phase == SchedulerPhase::active
            && receipts[0U].stable_order == 10U
            && receipts[0U].sequence == 1U
            && receipts[1U].valid && receipts[1U].time == 0U
            && receipts[1U].delta == 0U
            && receipts[1U].systemverilog_round == 2U
            && receipts[1U].phase == SchedulerPhase::active
            && receipts[1U].stable_order == 20U
            && receipts[1U].sequence == 2U,
        "allocation-free receipt publication retains exact input-indexed scheduler keys");
}

void test_warmed_large_readiness_reservation_allocates_nothing()
{
    constexpr std::size_t member_count = 129U;
    UnusedBatch batch;
    const SchedulerBatchGroupKey group_key { 92U, 11U };
    std::vector<Scheduler::SystemVerilogGroupBatchMember> members;
    std::vector<SchedulerSystemVerilogKeyReceipt> receipts(member_count);
    members.reserve(member_count);
    for (std::size_t index = 0U; index < member_count; ++index) {
        members.push_back({
            100U + index,
            static_cast<std::uint64_t>(index + 1U),
            [](Scheduler&) {
                throw std::runtime_error(
                    "warmed large readiness member unexpectedly fell back");
            },
            { },
            { },
        });
    }

    const auto schedule_group = [&](Scheduler& current) {
        return current.schedule_systemverilog_readiness_group(
            SchedulerPhase::active, batch, group_key, members, receipts);
    };
    Scheduler scheduler;
    bool first_committed { };
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            first_committed = schedule_group(current);
        });
    require(scheduler.run().status == RunStatus::completed && first_committed,
        "first large component group establishes retained ticket capacity");

    for (auto& member : members) {
        member.fallback_task = [](Scheduler&) {
            throw std::runtime_error(
                "warmed large readiness member unexpectedly fell back");
        };
    }
    bool second_committed { };
    std::size_t second_admission_allocations { };
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            begin_allocation_count();
            second_committed = schedule_group(current);
            second_admission_allocations = end_allocation_count();
        });
    require(scheduler.run().status == RunStatus::completed
            && second_committed && second_admission_allocations == 0U,
        "warmed 129-member reserve and commit reuse all capacity without allocation");
    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(stats.readiness_ticket_queue_insertions == 2U
            && stats.readiness_ticket_members == 2U * member_count
            && stats.readiness_ticket_members_elided
                == 2U * (member_count - 1U)
            && stats.readiness_ticket_fallback_members == 0U
            && stats.direct_dispatches == 2U
            && stats.direct_members == 2U,
        "large readiness slabs remain one ticket per activation after warmup");
}

void test_large_readiness_reservation_failure_is_invisible()
{
    constexpr std::size_t member_count = 129U;
    constexpr StableOrder foreign_order = 164U;
    constexpr std::uint64_t retry_payload = 1000U;
    bool reached_uninjected_success { };
    std::size_t failed_cuts { };

    for (std::size_t fail_allocation = 0U;
         fail_allocation < 16U && !reached_uninjected_success;
         ++fail_allocation) {
        std::vector<std::uint64_t> batch_visits;
        batch_visits.reserve(member_count + 1U);
        std::vector<std::uint64_t> ordered;
        ordered.reserve(member_count + 2U);
        RecordingBatch batch {
            batch_visits,
            &ordered,
        };
        std::vector<Scheduler::SystemVerilogGroupBatchMember> members;
        std::vector<SchedulerSystemVerilogKeyReceipt> receipts(member_count);
        for (auto& receipt : receipts) {
            receipt.valid = true;
        }
        members.reserve(member_count);
        for (std::size_t index = 0U; index < member_count; ++index) {
            members.push_back({
                100U + index,
                static_cast<std::uint64_t>(index + 1U),
                [](Scheduler&) {
                    throw std::runtime_error(
                        "failed large reservation member unexpectedly fell back");
                },
                { },
                { },
            });
        }
        const SchedulerBatchGroupKey group_key { 93U, 11U };
        bool attempted_reservation { };
        bool reservation_committed { };
        bool failure_injected { };
        bool sentinel_key_valid { };
        bool pristine_before_sentinel { };
        bool fallback_tasks_scheduled { };
        Scheduler scheduler;
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&](Scheduler& current) {
                current.schedule_systemverilog(SchedulerPhase::active,
                    foreign_order, [&ordered](Scheduler&) {
                        ordered.push_back(0U);
                    });
                arm_allocation_failure(fail_allocation);
                attempted_reservation
                    = current.schedule_systemverilog_readiness_group(
                        SchedulerPhase::active, batch, group_key, members,
                        receipts);
                failure_injected = allocation_failure_was_injected();
                clear_allocation_failure();

                if (failure_injected) {
                    const auto stats
                        = current.systemverilog_batch_compaction_stats();
                    bool receipts_cleared { true };
                    for (const auto& receipt : receipts) {
                        receipts_cleared = receipts_cleared && !receipt.valid;
                    }
                    pristine_before_sentinel
                        = !attempted_reservation
                        && receipts_cleared
                        && stats.readiness_ticket_queue_insertions == 0U
                        && stats.readiness_ticket_members == 0U
                        && stats.readiness_ticket_members_elided == 0U
                        && stats.readiness_ticket_fallback_members == 0U;
                    SchedulerSystemVerilogKeyReceipt sentinel_receipt;
                    require(current.schedule_systemverilog_readiness_member(
                                SchedulerPhase::active, 10U, batch,
                                retry_payload,
                                [](Scheduler&) {
                                    throw std::runtime_error(
                                        "post-failure sentinel fell back");
                                }, group_key, &sentinel_receipt),
                        "a failed large reservation releases its ticket for retry");
                    sentinel_key_valid = sentinel_receipt.valid
                        && sentinel_receipt.sequence == 2U
                        && sentinel_receipt.stable_order == 10U;
                    ++failed_cuts;
                    for (std::size_t index = 0U;
                         index < member_count; ++index) {
                        current.schedule_systemverilog(
                            SchedulerPhase::active, 100U + index,
                            [&ordered, index](Scheduler&) {
                                ordered.push_back(index + 1U);
                            });
                    }
                    fallback_tasks_scheduled = true;
                    return;
                }

                require(attempted_reservation,
                    "first uninjected large reservation succeeds");
                reservation_committed = true;
            });

        const auto result = scheduler.run();
        if (!failure_injected) {
            require(reservation_committed
                    && result.status == RunStatus::completed
                    && batch_visits.size() == member_count,
                "the first uninjected sweep terminal commits every group member");
            reached_uninjected_success = true;
            continue;
        }

        require(result.status == RunStatus::completed
                && attempted_reservation == false
                && fallback_tasks_scheduled && pristine_before_sentinel
                && sentinel_key_valid
                && batch_visits == std::vector<std::uint64_t> {
                    retry_payload,
                },
            "injected reservation failure exposes no partial member and consumes no key");
        std::vector<std::uint64_t> expected;
        expected.reserve(member_count + 2U);
        expected.push_back(retry_payload);
        for (std::size_t index = 0U; index < 64U; ++index) {
            expected.push_back(index + 1U);
        }
        expected.push_back(0U);
        for (std::size_t index = 64U; index < member_count; ++index) {
            expected.push_back(index + 1U);
        }
        require(ordered == expected,
            "ordinary fallback after a failed 129-member reserve preserves foreign ordering");
    }

    require(reached_uninjected_success && failed_cuts != 0U,
        "the allocation sweep covers each injected previsibility cut through success");
}

void test_recent_signal_capacity_is_ready_before_publication()
{
    SchedulerOptions options;
    options.recent_signal_capacity = 3U;
    Scheduler scheduler { options };

    // A native internal commit may notify the scheduler only after the
    // authoritative value has changed. That notification cannot allocate.
    arm_allocation_failure(0U);
    for (RuntimeSignalId signal = 10U; signal != 15U; ++signal) {
        scheduler.note_signal_change(signal);
    }
    const auto injected = allocation_failure_was_injected();
    clear_allocation_failure();
    require(!injected,
        "bounded recent-signal notifications allocate no storage after construction");
}

void run_partial_callback_allocation_failure_case(const bool systemverilog)
{
    Scheduler scheduler;
    PartialPrefixFailureState state;
    state.systemverilog = systemverilog;
    PartialPrefixFailureBatch batch { state };
    std::array<SchedulerSystemVerilogKeyReceipt, 3U> original_keys { };

    for (std::size_t index = 0U; index < original_keys.size(); ++index) {
        const auto payload = static_cast<std::uint64_t>(index + 1U);
        const auto order = static_cast<StableOrder>((index + 1U) * 10U);
        auto fallback = [&state, payload](Scheduler&) {
            if (payload >= state.fallbacks.size()) {
                state.invalid_frontier = true;
                return;
            }
            ++state.fallbacks[payload];
        };

        if (systemverilog) {
            // An empty group key deliberately stays on the ordinary batch
            // path instead of being compacted into a readiness ticket.
            scheduler.schedule_systemverilog_group_batchable(
                SchedulerPhase::active, order, batch, payload,
                std::move(fallback), { }, &original_keys[index]);
        } else {
            scheduler.schedule_next_delta_batchable(
                SchedulerPhase::active, order, batch, payload,
                std::move(fallback));
        }
    }

    bool callback_failure_escaped { };
    bool first_run_completed { };
    try {
        first_run_completed
            = scheduler.run().status == RunStatus::completed;
        if (state.allocation_measurement_started) {
            state.allocations_after_callback = end_allocation_count();
            state.allocation_measurement_started = false;
        }
    } catch (const std::bad_alloc&) {
        callback_failure_escaped = true;
        if (state.allocation_measurement_started) {
            state.allocations_after_callback = end_allocation_count();
            state.allocation_measurement_started = false;
        }
    } catch (...) {
        if (state.allocation_measurement_started) {
            state.allocations_after_callback = end_allocation_count();
            state.allocation_measurement_started = false;
        }
        clear_allocation_failure();
        throw;
    }
    clear_allocation_failure();

    require(callback_failure_escaped && !first_run_completed
            && state.callback_allocation_failed
            && state.calls == 1U
            && state.allocation_measurement_started == false
            && state.allocations_after_callback == 0U
            && scheduler.has_pending(),
        "callback bad_alloc retires its accepted prefix and requeues without allocation");
    require(!state.invalid_frontier && state.offers[0U].count == 3U
            && state.offers[0U].phase == SchedulerPhase::active
            && state.offers[0U].time == 0U
            && state.offers[0U].delta == 0U
            && state.offers[0U].generation != 0U,
        "ordinary batch receives the full first offer before its partial failure");

    if (systemverilog) {
        require(state.offers[0U].systemverilog_round == 1U,
            "initial SV offer retains the scheduler-issued Active round");
        for (std::size_t index = 0U; index < original_keys.size(); ++index) {
            require(original_keys[index].valid
                    && original_keys[index].time == 0U
                    && original_keys[index].delta == 0U
                    && original_keys[index].systemverilog_round == 1U
                    && original_keys[index].phase == SchedulerPhase::active
                    && original_keys[index].stable_order
                        == static_cast<StableOrder>((index + 1U) * 10U)
                    && original_keys[index].sequence == index,
                "test inputs retain scheduler receipts for every original SV key");
        }
    }

    require(scheduler.run().status == RunStatus::completed
            && !scheduler.has_pending(),
        "retry drains only the suffix after the callback allocation failure");
    require(state.calls == 2U && !state.invalid_frontier
            && state.offers[1U].count == 2U
            && state.offers[1U].generation != state.offers[0U].generation
            && state.offers[1U].time == state.offers[0U].time
            && state.offers[1U].delta == state.offers[0U].delta
            && state.offers[1U].phase == state.offers[0U].phase,
        "retry is a fresh frontier containing exactly the unconsumed suffix");
    if (systemverilog) {
        require(state.offers[1U].systemverilog_round
                == state.offers[0U].systemverilog_round,
            "retry retains the original SV round");
    }
    for (std::size_t index = 0U; index < 3U; ++index) {
        const auto& original = state.offers[0U].tasks[index];
        require(original.stable_order
                    == static_cast<StableOrder>((index + 1U) * 10U)
                && original.sequence == index
                && original.payload == index + 1U,
            "initial callback frontier matches every scheduled stable key");
        if (systemverilog) {
            require(original.stable_order
                        == original_keys[index].stable_order
                    && original.sequence == original_keys[index].sequence,
                "initial SV frontier matches the scheduler's key receipts");
        }
    }
    for (std::size_t index = 0U; index < 2U; ++index) {
        const auto& expected_suffix = state.offers[0U].tasks[index + 1U];
        const auto& retried = state.offers[1U].tasks[index];
        require(retried.stable_order == expected_suffix.stable_order
                && retried.sequence == expected_suffix.sequence
                && retried.payload == expected_suffix.payload,
            "suffix requeue preserves the exact original key and payload");
    }
    require(state.executions == std::array<std::size_t, 4U> {
                0U, 1U, 1U, 1U }
            && state.fallbacks == std::array<std::size_t, 4U> { },
        "accepted prefix and retried suffix each execute once without fallback");
    if (systemverilog) {
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.tickets == 0U && stats.direct_dispatches == 0U,
            "the SV case exercised ordinary suffix reinsertion, not a ticket cursor");
    }
}

void test_cold_prepop_allocation_failure_keeps_original_batch()
{
    Scheduler scheduler;
    ColdPrepopState state;
    ColdPrepopBatch batch { state };

    for (std::size_t index = 0U; index < state.tasks.size(); ++index) {
        const auto payload = static_cast<std::uint64_t>(index + 1U);
        const auto order = static_cast<StableOrder>((index + 1U) * 10U);
        auto fallback = [&state, payload](Scheduler&) {
            if (payload >= state.fallbacks.size()) {
                state.invalid_frontier = true;
                return;
            }
            ++state.fallbacks[payload];
        };
        scheduler.schedule_next_delta_batchable(
            SchedulerPhase::active, order, batch, payload,
            std::move(fallback));
    }

    std::size_t failed_prepop_allocations { };
    bool run_failed_before_callback { };
    // Scheduler construction leaves batch_entries cold. For this ordinary
    // batch, dispatch's first allocation is its bounded reserve before pop.
    begin_allocation_count();
    arm_allocation_failure(0U);
    try {
        static_cast<void>(scheduler.run());
    } catch (const std::bad_alloc&) {
        run_failed_before_callback = true;
    }
    failed_prepop_allocations = end_allocation_count();
    const auto failure_was_injected = allocation_failure_was_injected();
    clear_allocation_failure();

    require(run_failed_before_callback && failure_was_injected
            && failed_prepop_allocations == 1U
            && state.calls == 0U && scheduler.has_pending(),
        "cold pre-pop allocation failure leaves the complete batch queued");
    require(scheduler.run().status == RunStatus::completed
            && !scheduler.has_pending()
            && state.calls == 1U && !state.invalid_frontier
            && state.count == state.tasks.size(),
        "retry after cold reserve failure offers every original task together");
    for (std::size_t index = 0U; index < state.tasks.size(); ++index) {
        const auto& task = state.tasks[index];
        require(task.stable_order
                    == static_cast<StableOrder>((index + 1U) * 10U)
                && task.sequence == index
                && task.payload == index + 1U,
            "cold retry retains every original key and payload");
    }
    require(state.fallbacks == std::array<std::size_t, 4U> { },
        "cold pre-pop retry neither duplicates nor falls back queued work");
    require(state.executions == std::array<std::size_t, 4U> {
                0U, 1U, 1U, 1U },
        "cold retry executes each original payload exactly once");
}

void test_partial_callback_allocation_failure_requeues_exact_suffix()
{
    run_partial_callback_allocation_failure_case(false);
    run_partial_callback_allocation_failure_case(true);
}

constexpr std::size_t readiness_pool_test_capacity = 129U;
constexpr std::size_t readiness_pool_test_passes = 3U;

using ReadinessPoolMember = Scheduler::ReadinessBatchMember;

struct ReadinessPoolPassCapture {
    std::array<SchedulerSystemVerilogKeyReceipt,
        readiness_pool_test_capacity> receipts { };
    std::array<SchedulerBatchFrontierEntry,
        readiness_pool_test_capacity> dispatched_entries { };
    std::array<std::uint64_t,
        readiness_pool_test_capacity> dispatched_rounds { };
    std::array<std::size_t,
        readiness_pool_test_capacity> executions { };
    std::size_t attempted_groups { };
    std::size_t accepted_groups { };
    std::size_t declined_groups { };
    std::size_t dispatches { };
    std::size_t schedule_allocations { };
    std::uint64_t payload_base { };
    std::uint64_t order_base { };
    bool invalid_frontier { };
};

struct ReadinessPoolCapture {
    std::array<ReadinessPoolPassCapture,
        readiness_pool_test_passes> passes { };
    std::size_t active_pass { };
};

void readiness_pool_noop_fallback(Scheduler&)
{
}

class ReadinessPoolBatch final : public SchedulerBatchTask {
public:
    explicit ReadinessPoolBatch(ReadinessPoolCapture& capture) noexcept
        : capture_(capture)
    {
    }

    SchedulerBatchResult execute(Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        auto& pass = capture_.passes[capture_.active_pass];
        const auto frontier = scheduler.current_batch_frontier();
        if (!frontier || payloads.size() != 1U
            || frontier->phase != SchedulerPhase::active
            || frontier->time != scheduler.now()
            || frontier->delta != scheduler.delta()
            || frontier->cursor != 0U || frontier->end != 1U
            || frontier->tasks.size() != 1U
            || frontier->systemverilog_round == 0U) {
            pass.invalid_frontier = true;
            return { };
        }

        const auto payload = payloads.front();
        if (payload < pass.payload_base
            || payload - pass.payload_base >= pass.attempted_groups) {
            pass.invalid_frontier = true;
            return { };
        }
        const auto index = static_cast<std::size_t>(
            payload - pass.payload_base);
        const auto& entry = frontier->tasks.front();
        const auto& receipt = pass.receipts[index];
        if (entry.payload != payload
            || entry.stable_order != pass.order_base + index
            || !receipt.valid || receipt.time != frontier->time
            || receipt.delta != frontier->delta
            || receipt.systemverilog_round
                != frontier->systemverilog_round
            || receipt.phase != frontier->phase
            || receipt.stable_order != entry.stable_order
            || receipt.sequence != entry.sequence) {
            pass.invalid_frontier = true;
        }
        pass.dispatched_entries[index] = entry;
        pass.dispatched_rounds[index] = frontier->systemverilog_round;
        ++pass.executions[index];
        ++pass.dispatches;
        return { payloads.size(), { } };
    }

private:
    ReadinessPoolCapture& capture_;
};

void initialize_readiness_pool_members(
    std::array<ReadinessPoolMember, readiness_pool_test_capacity>& members,
    const std::uint64_t payload_base,
    const std::uint64_t order_base)
{
    for (std::size_t index = 0U; index < members.size(); ++index) {
        auto& member = members[index];
        member.stable_order = order_base + index;
        member.payload = payload_base + index;
        member.fallback_task = &readiness_pool_noop_fallback;
        member.fallback_descriptor = { };
        member.batch_owner_lifetime.reset();
    }
}

void verify_readiness_pool_pass(const Scheduler& scheduler,
    const ReadinessPoolCapture& capture,
    const std::size_t pass_index,
    const std::size_t expected_accepted,
    const std::uint64_t expected_ticket_total)
{
    const auto& pass = capture.passes[pass_index];
    require(!pass.invalid_frontier
            && pass.accepted_groups == expected_accepted
            && pass.declined_groups
                == pass.attempted_groups - expected_accepted
            && pass.dispatches == expected_accepted,
        "readiness growth leaves exactly the admitted groups executable");

    std::uint64_t first_sequence { };
    for (std::size_t index = 0U; index < pass.attempted_groups; ++index) {
        const auto& receipt = pass.receipts[index];
        if (index >= expected_accepted) {
            require(!receipt.valid && pass.executions[index] == 0U,
                "a group refused by the old pool publishes no receipt or callback");
            continue;
        }

        if (index == 0U) {
            first_sequence = receipt.sequence;
        }
        const auto& entry = pass.dispatched_entries[index];
        require(receipt.valid && pass.executions[index] == 1U
                && receipt.time == 0U && receipt.delta == 0U
                && receipt.phase == SchedulerPhase::active
                && receipt.systemverilog_round
                    == pass.dispatched_rounds[index]
                && receipt.systemverilog_round != 0U
                && receipt.stable_order == pass.order_base + index
                && receipt.sequence == first_sequence + index
                && entry.stable_order == receipt.stable_order
                && entry.sequence == receipt.sequence
                && entry.payload == pass.payload_base + index,
            "every admitted group dispatches once with its exact scheduler key");
    }

    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(stats.readiness_ticket_queue_insertions == expected_ticket_total
            && stats.readiness_ticket_members == expected_ticket_total
            && stats.readiness_ticket_members_elided == 0U
            && stats.readiness_ticket_fallback_members == 0U
            && stats.tickets == expected_ticket_total
            && stats.members == expected_ticket_total
            && stats.entries_elided == 0U
            && stats.direct_dispatches == expected_ticket_total
            && stats.direct_members == expected_ticket_total,
        "distinct readiness groups retain one physical ticket apiece");
}

void prepare_readiness_pool_pass(ReadinessPoolCapture& capture,
    const std::size_t pass_index,
    const std::size_t attempted_groups)
{
    auto& pass = capture.passes[pass_index];
    pass.attempted_groups = attempted_groups;
    pass.payload_base = 1000U + pass_index * 10000U;
    pass.order_base = 100U + pass_index * 1000U;
    for (auto& receipt : pass.receipts) {
        receipt.valid = true;
    }
}

void enqueue_readiness_pool_groups(Scheduler& scheduler,
    ReadinessPoolCapture& capture,
    ReadinessPoolBatch& batch,
    std::array<ReadinessPoolMember, readiness_pool_test_capacity>& members,
    const std::size_t pass_index,
    const bool measure_admission)
{
    auto& pass = capture.passes[pass_index];
    capture.active_pass = pass_index;
    if (measure_admission) {
        begin_allocation_count();
    }
    for (std::size_t index = 0U; index < pass.attempted_groups; ++index) {
        const SchedulerBatchGroupKey group_key {
            701U + pass_index,
            9000U + pass_index * readiness_pool_test_capacity
                + index + 1U,
        };
        const bool accepted = scheduler.schedule_systemverilog_readiness_group(
            SchedulerPhase::active, batch, group_key,
            std::span<ReadinessPoolMember> { &members[index], 1U },
            std::span<SchedulerSystemVerilogKeyReceipt> {
                &pass.receipts[index], 1U });
        if (accepted) {
            ++pass.accepted_groups;
        } else {
            ++pass.declined_groups;
        }
    }
    if (measure_admission) {
        pass.schedule_allocations = end_allocation_count();
    }
}

void schedule_readiness_pool_pass(Scheduler& scheduler,
    ReadinessPoolCapture& capture,
    ReadinessPoolBatch& batch,
    std::array<ReadinessPoolMember, readiness_pool_test_capacity>& members,
    const std::size_t pass_index,
    const std::size_t attempted_groups,
    const bool measure_admission)
{
    prepare_readiness_pool_pass(capture, pass_index, attempted_groups);
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&capture, &batch, &members, pass_index,
            measure_admission](Scheduler& current) {
            enqueue_readiness_pool_groups(current, capture, batch, members,
                pass_index, measure_admission);
        });
    require(scheduler.run().status == RunStatus::completed,
        "the readiness pool scheduling pass drains completely");
}

void run_readiness_pool_growth_failure_sweep(
    const std::size_t requested_capacity)
{
    constexpr std::size_t maximum_growth_attempts = 32U;
    constexpr std::size_t legacy_capacity = 64U;
    bool growth_succeeded { };
    std::size_t injected_failures { };

    for (std::size_t fail_after = 0U;
         fail_after < maximum_growth_attempts && !growth_succeeded;
         ++fail_after) {
        Scheduler scheduler;
        ReadinessPoolCapture capture;
        ReadinessPoolBatch batch { capture };
        std::array<std::array<ReadinessPoolMember,
            readiness_pool_test_capacity>, readiness_pool_test_passes> members { };
        for (std::size_t pass_index = 0U;
             pass_index < readiness_pool_test_passes; ++pass_index) {
            initialize_readiness_pool_members(members[pass_index],
                1000U + pass_index * 10000U,
                100U + pass_index * 1000U);
        }

        bool prepared { };
        bool threw_bad_alloc { };
        arm_allocation_failure(fail_after);
        try {
            prepared = scheduler
                .prepare_systemverilog_readiness_ticket_capacity(
                    requested_capacity);
        } catch (const std::bad_alloc&) {
            threw_bad_alloc = true;
        }
        const bool injected = allocation_failure_was_injected();
        clear_allocation_failure();

        if (injected) {
            require(threw_bad_alloc && !prepared,
                "each injected cold-pool allocation failure escapes before growth");
            ++injected_failures;
            schedule_readiness_pool_pass(scheduler, capture, batch,
                members[0U], 0U, legacy_capacity + 1U, false);
            verify_readiness_pool_pass(scheduler, capture, 0U,
                legacy_capacity, legacy_capacity);
            require(scheduler.prepare_systemverilog_readiness_ticket_capacity(
                        requested_capacity),
                "the unchanged scheduler can retry cold-pool growth");
            schedule_readiness_pool_pass(scheduler, capture, batch,
                members[1U], 1U, requested_capacity, false);
            verify_readiness_pool_pass(scheduler, capture, 1U,
                requested_capacity, legacy_capacity + requested_capacity);
            continue;
        }

        require(!threw_bad_alloc && prepared,
            "cold readiness pool growth succeeds after the injected prefix sweep");
        schedule_readiness_pool_pass(scheduler, capture, batch,
            members[0U], 0U, requested_capacity, false);
        verify_readiness_pool_pass(scheduler, capture, 0U,
            requested_capacity, requested_capacity);
        growth_succeeded = true;
    }

    require(growth_succeeded && injected_failures != 0U,
        "cold pool growth sweeps every allocation failure before succeeding");
}

void test_systemverilog_readiness_pool_growth_failure_is_atomic()
{
    run_readiness_pool_growth_failure_sweep(65U);
    run_readiness_pool_growth_failure_sweep(129U);
}

void test_warmed_distinct_systemverilog_readiness_groups_allocate_nothing()
{
    constexpr std::size_t group_count = readiness_pool_test_capacity;
    Scheduler scheduler;
    require(scheduler.prepare_systemverilog_readiness_ticket_capacity(65U)
            && scheduler.prepare_systemverilog_readiness_ticket_capacity(
                group_count),
        "the warmup grows the same pool from 65 to 129 component groups");

    ReadinessPoolCapture capture;
    ReadinessPoolBatch batch { capture };
    std::array<std::array<ReadinessPoolMember, group_count>,
        readiness_pool_test_passes> members { };
    for (std::size_t pass_index = 0U;
         pass_index < readiness_pool_test_passes; ++pass_index) {
        initialize_readiness_pool_members(members[pass_index],
            1000U + pass_index * 10000U,
            100U + pass_index * 1000U);
        prepare_readiness_pool_pass(capture, pass_index, group_count);
    }
    const auto first_sentinel_order
        = capture.passes[0U].order_base + group_count;
    const auto second_sentinel_order
        = capture.passes[1U].order_base + group_count;

    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&capture, &batch, &members, first_sentinel_order,
            second_sentinel_order](Scheduler& current) {
            enqueue_readiness_pool_groups(current, capture, batch,
                members[0U], 0U, false);
            // The two region queues alternate their vectors. Exercise both
            // sides before measuring the third admission pass.
            current.schedule_systemverilog(SchedulerPhase::active,
                first_sentinel_order,
                [&capture, &batch, &members, second_sentinel_order](
                    Scheduler& after_first_pass) {
                    require(capture.passes[0U].dispatches
                            == readiness_pool_test_capacity,
                        "the first sentinel follows a fully drained pass");
                    enqueue_readiness_pool_groups(after_first_pass, capture,
                        batch, members[1U], 1U, false);
                    after_first_pass.schedule_systemverilog(
                        SchedulerPhase::active, second_sentinel_order,
                        [&capture, &batch, &members](
                            Scheduler& after_second_pass) {
                            require(capture.passes[1U].dispatches
                                    == readiness_pool_test_capacity,
                                "the second sentinel follows a fully drained pass");
                            enqueue_readiness_pool_groups(after_second_pass,
                                capture, batch, members[2U], 2U, true);
                        });
                });
        });

    require(scheduler.run().status == RunStatus::completed,
        "three readiness passes drain in one reusable scheduler slot");
    for (std::size_t pass_index = 0U;
         pass_index < readiness_pool_test_passes; ++pass_index) {
        verify_readiness_pool_pass(scheduler, capture, pass_index,
            group_count, readiness_pool_test_passes * group_count);
    }
    require(capture.passes[2U].schedule_allocations == 0U,
        "warmed 129 distinct-group admission reuses every physical ticket slot");

    for (std::size_t pass_index = 1U;
         pass_index < readiness_pool_test_passes; ++pass_index) {
        const auto& previous = capture.passes[pass_index - 1U];
        const auto& current = capture.passes[pass_index];
        require(current.receipts[0U].sequence
                    > previous.receipts[group_count - 1U].sequence,
            "each warmed repeat receives fresh scheduler insertion keys");
    }
}

} // namespace

int main()
{
    try {
        test_ordinary_queue_growth_failure_preserves_work_and_order();
        test_reservation_failure_keeps_foreign_suffix_ordered();
        test_existing_ticket_cancel_after_growth_keeps_members();
        test_existing_ticket_growth_before_invalid_member_keeps_members();
        test_second_group_reservation_failure_keeps_interleaved_order();
        test_underfilled_ordinary_group_reservation();
        test_reserved_receipt_commit_allocates_nothing();
        test_warmed_large_readiness_reservation_allocates_nothing();
        test_large_readiness_reservation_failure_is_invisible();
        test_recent_signal_capacity_is_ready_before_publication();
        test_cold_prepop_allocation_failure_keeps_original_batch();
        test_partial_callback_allocation_failure_requeues_exact_suffix();
        test_systemverilog_readiness_pool_growth_failure_is_atomic();
        test_warmed_distinct_systemverilog_readiness_groups_allocate_nothing();
        std::cout << "group batch reservation failure test passed\n";
        return 0;
    } catch (const std::exception& error) {
        clear_allocation_failure();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

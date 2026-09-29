// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/scheduler.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {

namespace {

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_reserved_order_key_fifo()
{
    using namespace fsim::runtime;

    Scheduler scheduler;
    std::vector<std::string_view> events;
    const auto first_tie = scheduler.reserve_order_key(30U);
    const auto cancelled_key = scheduler.reserve_order_key(5U);
    const auto lower_order = scheduler.reserve_order_key(20U);
    const auto second_tie = scheduler.reserve_order_key(30U);

    const auto cancelled = scheduler.schedule_reserved_next_delta_cancelable(
        SchedulerPhase::active, cancelled_key,
        [&](Scheduler&) { events.push_back("cancelled"); });
    require(static_cast<bool>(cancelled),
        "a reserved cancelable task must return a live handle");
    scheduler.cancel(cancelled);
    require(!cancelled,
        "canceling a reserved task must invalidate its handle");

    // Enqueue the tied reservations in reverse sequence order. The stable
    // order still sorts first, and sequence breaks ties independently of
    // physical enqueue order.
    scheduler.schedule_reserved_next_delta(
        SchedulerPhase::active, second_tie,
        [&](Scheduler&) { events.push_back("second-tie"); });
    scheduler.schedule_reserved_next_delta(
        SchedulerPhase::active, lower_order,
        [&](Scheduler&) { events.push_back("lower-order"); });
    scheduler.schedule_reserved_next_delta(
        SchedulerPhase::active, first_tie,
        [&](Scheduler&) { events.push_back("first-tie"); });

    const auto result = scheduler.run();
    require(result.status == RunStatus::completed
            && result.callbacks_executed == 3U
            && events == std::vector<std::string_view> {
                "lower-order", "first-tie", "second-tie"
            },
        "reserved keys order by StableOrder then reservation sequence");
}

class FrontierProbeBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    explicit FrontierProbeBatch(fsim::runtime::SchedulerOrderKey key)
        : reserved_key(key)
    {
    }

    [[nodiscard]] fsim::runtime::SchedulerBatchResult execute(
        fsim::runtime::Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        try {
            (void)scheduler.next_current_order_key();
        } catch (const std::logic_error&) {
            frontier_rejected = true;
        }
        try {
            scheduler.schedule_reserved_current(
                fsim::runtime::SchedulerPhase::active, reserved_key,
                [](fsim::runtime::Scheduler&) { });
        } catch (const std::logic_error&) {
            reserved_current_rejected = true;
        }
        received.assign(payloads.begin(), payloads.end());
        return { payloads.size(), { } };
    }

    fsim::runtime::SchedulerOrderKey reserved_key;
    bool frontier_rejected { };
    bool reserved_current_rejected { };
    std::vector<std::uint64_t> received;
};

void test_current_reserved_continuation_and_frontier_guard()
{
    using namespace fsim::runtime;

    Scheduler outside_callback;
    bool outside_rejected { };
    try {
        (void)outside_callback.next_current_order_key();
    } catch (const std::logic_error&) {
        outside_rejected = true;
    }
    require(outside_rejected,
        "the current order key is unavailable outside a callback");
    const auto outside_key = outside_callback.reserve_order_key(0U);
    bool reserved_current_outside_rejected { };
    try {
        outside_callback.schedule_reserved_current(
            SchedulerPhase::active, outside_key, [](Scheduler&) { });
    } catch (const std::logic_error&) {
        reserved_current_outside_rejected = true;
    }
    require(reserved_current_outside_rejected,
        "a reserved current task requires an ordinary callback");

    Scheduler continuation;
    std::vector<std::string_view> events;
    const auto continuation_key = continuation.reserve_order_key(20U);
    const auto frontier_key = continuation.reserve_order_key(20U);
    const auto wrong_phase_key = continuation.reserve_order_key(25U);
    bool wrong_phase_rejected { };
    continuation.schedule(
        SchedulerPhase::active, 10U,
        [&](Scheduler& scheduler) {
            events.push_back("origin");
            try {
                scheduler.schedule_reserved_current(
                    SchedulerPhase::inactive, wrong_phase_key,
                    [](Scheduler&) { });
            } catch (const std::logic_error&) {
                wrong_phase_rejected = true;
            }
            require(wrong_phase_rejected,
                "a reserved current task cannot change scheduler phase");
            scheduler.schedule_reserved_current(
                SchedulerPhase::active, frontier_key,
                [&](Scheduler&) { events.push_back("frontier"); });
            const auto frontier = scheduler.next_current_order_key();
            require(frontier.has_value()
                    && frontier->order == frontier_key.order
                    && frontier->sequence == frontier_key.sequence,
                "the frontier query returns the next canonical phase key");
            scheduler.schedule_reserved_current(
                SchedulerPhase::active, continuation_key,
                [&](Scheduler&) { events.push_back("continuation"); });
        });
    const auto continuation_result = continuation.run();
    require(continuation_result.status == RunStatus::completed
            && continuation_result.callbacks_executed == 3U
            && events == std::vector<std::string_view> {
                "origin", "continuation", "frontier"
            },
        "a reserved continuation can rejoin the current phase in key order");

    Scheduler empty_frontier;
    bool empty_observed { };
    empty_frontier.schedule(
        SchedulerPhase::active, 0U,
        [&](Scheduler& scheduler) {
            empty_observed
                = !scheduler.next_current_order_key().has_value();
        });
    require(empty_frontier.run().status == RunStatus::completed
            && empty_observed,
        "an ordinary callback sees an empty frontier when no peer is queued");

    Scheduler batched;
    FrontierProbeBatch batch { batched.reserve_order_key(5U) };
    batched.schedule_next_delta_batchable(
        SchedulerPhase::active, 10U, batch, 1U,
        [](Scheduler&) { });
    batched.schedule_next_delta_batchable(
        SchedulerPhase::active, 20U, batch, 2U,
        [](Scheduler&) { });
    const auto batch_result = batched.run();
    require(batch_result.status == RunStatus::completed
            && batch_result.callbacks_executed == 2U
            && batch.frontier_rejected
            && batch.reserved_current_rejected
            && batch.received == std::vector<std::uint64_t> { 1U, 2U },
        "a prefetched batch callback cannot inspect or enqueue current work");
}

void test_reserved_key_ownership_and_reset_epoch()
{
    using namespace fsim::runtime;

    Scheduler reset_scheduler;
    const auto stale_key = reset_scheduler.reserve_order_key(12U);
    reset_scheduler.reset();
    const auto fresh_key = reset_scheduler.reserve_order_key(12U);
    bool stale_rejected { };
    try {
        reset_scheduler.schedule_reserved_next_delta(
            SchedulerPhase::active, stale_key, [](Scheduler&) { });
    } catch (const std::invalid_argument&) {
        stale_rejected = true;
    }
    require(stale_rejected,
        "reset invalidates previously reserved scheduler keys");
    bool fresh_executed { };
    reset_scheduler.schedule_reserved_next_delta(
        SchedulerPhase::active, fresh_key,
        [&](Scheduler&) { fresh_executed = true; });
    require(reset_scheduler.run().status == RunStatus::completed
            && fresh_executed,
        "a current-epoch reservation remains schedulable after reset");

    Scheduler key_owner;
    Scheduler other_scheduler;
    const auto foreign_key = key_owner.reserve_order_key(7U);
    const auto local_key = other_scheduler.reserve_order_key(7U);
    bool foreign_rejected { };
    try {
        other_scheduler.schedule_reserved_next_delta(
            SchedulerPhase::active, foreign_key, [](Scheduler&) { });
    } catch (const std::invalid_argument&) {
        foreign_rejected = true;
    }
    require(foreign_rejected,
        "a reservation cannot be enqueued by another scheduler");
    bool local_executed { };
    other_scheduler.schedule_reserved_next_delta(
        SchedulerPhase::active, local_key,
        [&](Scheduler&) { local_executed = true; });
    require(other_scheduler.run().status == RunStatus::completed
            && local_executed,
        "a scheduler still accepts its own reservation");
}

void test_reserved_next_delta_stop_and_exception_prefix()
{
    using namespace fsim::runtime;

    Scheduler stopped;
    std::vector<std::string_view> stopped_events;
    const auto first = stopped.reserve_order_key(10U);
    const auto second = stopped.reserve_order_key(20U);
    stopped.schedule(
        SchedulerPhase::active, 0U,
        [&](Scheduler& scheduler) {
            stopped_events.push_back("origin");
            scheduler.schedule_reserved_next_delta(
                SchedulerPhase::active, second,
                [&](Scheduler&) { stopped_events.push_back("second"); });
            scheduler.schedule_reserved_next_delta(
                SchedulerPhase::active, first,
                [&](Scheduler& runtime) {
                    stopped_events.push_back("first");
                    runtime.request_stop();
                });
        });
    const auto stopped_result = stopped.run();
    require(stopped_result.status == RunStatus::stopped
            && stopped_events == std::vector<std::string_view> {
                "origin", "first"
            }
            && stopped.has_pending(),
        "stopping in a reserved prefix retains the untouched suffix");
    stopped.clear_stop();
    require(stopped.run().status == RunStatus::completed
            && stopped_events == std::vector<std::string_view> {
                "origin", "first", "second"
            },
        "resuming after stop executes each reserved suffix task once");

    Scheduler failing;
    std::vector<std::string_view> failed_events;
    const auto failed_first = failing.reserve_order_key(10U);
    const auto failed_second = failing.reserve_order_key(20U);
    const auto failed_third = failing.reserve_order_key(30U);
    failing.schedule(
        SchedulerPhase::active, 0U,
        [&](Scheduler& scheduler) {
            failed_events.push_back("origin");
            scheduler.schedule_reserved_next_delta(
                SchedulerPhase::active, failed_third,
                [&](Scheduler&) { failed_events.push_back("third"); });
            scheduler.schedule_reserved_next_delta(
                SchedulerPhase::active, failed_second,
                [&](Scheduler&) { failed_events.push_back("second"); });
            scheduler.schedule_reserved_next_delta(
                SchedulerPhase::active, failed_first,
                [&](Scheduler&) {
                    failed_events.push_back("first");
                    throw std::runtime_error("reserved callback failure");
                });
        });
    bool failure_caught { };
    try {
        (void)failing.run();
    } catch (const std::runtime_error& error) {
        failure_caught
            = std::string_view { error.what() }
                == "reserved callback failure";
    }
    require(failure_caught && !failing.running()
            && failing.has_pending()
            && failed_events == std::vector<std::string_view> {
                "origin", "first"
            },
        "an exception consumes only the failing reserved callback prefix");
    require(failing.run().status == RunStatus::completed
            && failed_events == std::vector<std::string_view> {
                "origin", "first", "second", "third"
            },
        "resuming after a callback exception does not replay its prefix");
}

void test_current_insertions_among_bulk_tasks()
{
    using namespace fsim::runtime;

    Scheduler scheduler;
    std::vector<std::string> events;
    ScheduledTaskHandle cancelled_bulk;
    for (StableOrder order = 10U; order <= 1000U; order += 10U) {
        auto task = [&, order](Scheduler& current) {
            events.push_back("bulk_" + std::to_string(order));
            if (order != 10U) {
                return;
            }
            current.cancel(cancelled_bulk);
            current.schedule(SchedulerPhase::active, 20U,
                [&](Scheduler&) {
                    events.push_back("inserted_tie");
                    throw std::runtime_error("inserted task failure");
                });
            current.schedule(SchedulerPhase::active, 15U,
                [&](Scheduler& stopped) {
                    events.push_back("inserted_stop");
                    stopped.request_stop();
                });
            current.schedule(SchedulerPhase::active, 5U,
                [&](Scheduler&) { events.push_back("inserted_lower"); });
            // A cancellation sweep must preserve both the bulk suffix and
            // the surviving tasks inserted after this phase began draining.
            std::vector<ScheduledTaskHandle> cancelled;
            for (std::size_t index = 0U; index < 160U; ++index) {
                cancelled.push_back(current.schedule_after_cancelable(
                    0U, SchedulerPhase::active, 7U,
                    [](Scheduler&) {
                        throw std::runtime_error("cancelled insertion executed");
                    }));
            }
            for (const auto& handle : cancelled) {
                current.cancel(handle);
            }
            const auto next = current.next_current_order_key();
            require(next && next->order == 5U,
                "a newly inserted lower key precedes the remaining bulk tasks");
        };
        if (order == 30U) {
            cancelled_bulk = scheduler.schedule_after_cancelable(
                0U, SchedulerPhase::active, order, std::move(task));
        } else {
            scheduler.schedule(SchedulerPhase::active, order, std::move(task));
        }
    }
    require(scheduler.run().status == RunStatus::stopped
            && events == std::vector<std::string> {
                "bulk_10", "inserted_lower", "inserted_stop" },
        "stop retains bulk tasks and same-phase inserted tasks together");
    scheduler.clear_stop();
    bool caught { };
    try {
        (void)scheduler.run();
    } catch (const std::runtime_error& error) {
        caught = std::string_view(error.what()) == "inserted task failure";
    }
    require(caught && events == std::vector<std::string> {
                "bulk_10", "inserted_lower", "inserted_stop", "bulk_20", "inserted_tie" },
        "a same-order insertion follows the older bulk reservation before throwing");
    require(scheduler.run().status == RunStatus::completed,
        "a failed insertion leaves the untouched bulk suffix resumable");
    std::vector<std::string> expected {
        "bulk_10", "inserted_lower", "inserted_stop", "bulk_20", "inserted_tie"
    };
    for (StableOrder order = 40U; order <= 1000U; order += 10U) {
        expected.push_back("bulk_" + std::to_string(order));
    }
    require(events == expected && !scheduler.has_pending(),
        "cancellation and resumption neither lose nor replay either source of work");
}

class InsertionBarrierBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    explicit InsertionBarrierBatch(std::vector<std::uint64_t>& events)
        : events_(events)
    {
    }

    fsim::runtime::SchedulerBatchResult execute(fsim::runtime::Scheduler&,
        const std::span<const std::uint64_t> payloads) override
    {
        prefixes.emplace_back(payloads.begin(), payloads.end());
        events_.insert(events_.end(), payloads.begin(), payloads.end());
        return { payloads.size(), { } };
    }

    std::vector<std::vector<std::uint64_t>> prefixes;

private:
    std::vector<std::uint64_t>& events_;
};

void test_current_insertion_splits_batch_prefetch()
{
    using namespace fsim::runtime;

    Scheduler scheduler;
    std::vector<std::uint64_t> events;
    InsertionBarrierBatch batch(events);
    scheduler.schedule(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            current.schedule_next_delta(SchedulerPhase::active, 10U,
                [&](Scheduler& next) {
                    events.push_back(10U);
                    next.schedule(SchedulerPhase::active, 25U,
                        [&](Scheduler&) { events.push_back(25U); });
                });
            for (const auto order : { 20U, 30U, 40U }) {
                current.schedule_next_delta_batchable(SchedulerPhase::active,
                    order, batch, order,
                    [](Scheduler&) {
                        throw std::runtime_error("unexpected batch fallback");
                    });
            }
        });
    require(scheduler.run().status == RunStatus::completed
            && events == std::vector<std::uint64_t> { 10U, 20U, 25U, 30U, 40U }
            && batch.prefixes == std::vector<std::vector<std::uint64_t>> {
                { 20U }, { 30U, 40U } },
        "a current-phase insertion separates prefetched batches in full-key order");
}

} // namespace

void test_scheduler_private_region_scheduler_contract()
{
    test_reserved_order_key_fifo();
    test_current_reserved_continuation_and_frontier_guard();
    test_reserved_key_ownership_and_reset_epoch();
    test_reserved_next_delta_stop_and_exception_prefix();
    test_current_insertions_among_bulk_tasks();
    test_current_insertion_splits_batch_prefetch();
}

} // namespace fsim::tests::runtime

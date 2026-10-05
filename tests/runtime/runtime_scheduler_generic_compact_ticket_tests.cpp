// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using fsim::runtime::RunStatus;
using fsim::runtime::Scheduler;
using fsim::runtime::SchedulerBatchGroupKey;
using fsim::runtime::SchedulerBatchResult;
using fsim::runtime::SchedulerGenericBatchFrontier;
using fsim::runtime::SchedulerGenericKeyReceipt;
using fsim::runtime::SchedulerOrderedBatchTask;
using fsim::runtime::SchedulerPhase;
using fsim::runtime::StableOrder;

void require(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct CapturedGenericOffer {
    std::uint64_t generation { };
    std::uint64_t time { };
    std::uint64_t delta { };
    SchedulerPhase phase { SchedulerPhase::active };
    std::size_t cursor { };
    std::size_t end { };
    std::vector<fsim::runtime::SchedulerBatchFrontierEntry> tasks;
    std::vector<std::uint64_t> payloads;
    SchedulerBatchGroupKey compact_group_key { };
    std::size_t ticket_member_offset { };
    std::vector<fsim::runtime::SchedulerGenericTicketMember> ticket_members;
};


struct TraceCapture {
    std::array<fsim::runtime::SchedulerTraceRecord, 16U> records { };
    std::size_t count { };
    bool overflow { };
};

void capture_trace(void* context,
    const fsim::runtime::SchedulerTraceRecord& record) noexcept
{
    auto& capture = *static_cast<TraceCapture*>(context);
    if (capture.count == capture.records.size()) {
        capture.overflow = true;
        return;
    }
    capture.records[capture.count++] = record;
}

struct GenericTicketProbe {
    std::vector<std::uint64_t> events;
    std::vector<CapturedGenericOffer> offers;
    std::vector<SchedulerGenericKeyReceipt> receipts;
    std::weak_ptr<void> ticket_owner;
    std::size_t fallback_descriptors { };
    bool owner_expired_in_execute { };
    bool owner_expired_in_fallback { };
    bool frontier_invalid { };
};

struct GenericFallbackPayload {
    GenericTicketProbe* probe { };
    std::uint64_t payload { };
};

void dispatch_generic_ticket_fallback(
    Scheduler&, const GenericFallbackPayload& payload)
{
    payload.probe->events.push_back(10000U + payload.payload);
    payload.probe->owner_expired_in_fallback
        |= payload.probe->ticket_owner.expired();
}

class GenericCompactTask final : public SchedulerOrderedBatchTask {
public:
    using Callback = std::function<SchedulerBatchResult(
        Scheduler&, std::span<const std::uint64_t>)>;

    GenericCompactTask(GenericTicketProbe& probe, Callback callback)
        : probe_(probe)
        , callback_(std::move(callback))
    {
    }

    SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        ++execute_calls_;
        probe_.owner_expired_in_execute
            |= probe_.ticket_owner.expired();
        const auto frontier = scheduler.current_generic_batch_frontier();
        if (!frontier || frontier->phase != SchedulerPhase::active
            || frontier->tasks.size() != payloads.size()
            || frontier->cursor != 0U || frontier->end != payloads.size()) {
            probe_.frontier_invalid = true;
            return { };
        }

        CapturedGenericOffer offer;
        offer.generation = frontier->generation;
        offer.time = frontier->time;
        offer.delta = frontier->delta;
        offer.phase = frontier->phase;
        offer.cursor = frontier->cursor;
        offer.end = frontier->end;
        offer.tasks.assign(frontier->tasks.begin(), frontier->tasks.end());
        offer.payloads.assign(payloads.begin(), payloads.end());
        offer.compact_group_key = frontier->compact_group_key;
        offer.ticket_member_offset = frontier->ticket_member_offset;
        offer.ticket_members.assign(frontier->ticket_members.begin(),
            frontier->ticket_members.end());
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            if (frontier->tasks[index].payload != payloads[index])
                probe_.frontier_invalid = true;
        }
        probe_.offers.push_back(std::move(offer));
        if (callback_)
            return callback_(scheduler, payloads);
        probe_.events.insert(probe_.events.end(),
            payloads.begin(), payloads.end());
        return { payloads.size(), { } };
    }

    fsim::runtime::detail::SchedulerTaskDescriptor
    make_fallback_descriptor(const std::uint64_t payload) noexcept override
    {
        ++probe_.fallback_descriptors;
        return fsim::runtime::detail::make_scheduler_task_descriptor<
            GenericFallbackPayload,
            &dispatch_generic_ticket_fallback>({ &probe_, payload });
    }

    [[nodiscard]] std::size_t execute_calls() const noexcept
    {
        return execute_calls_;
    }

private:
    GenericTicketProbe& probe_;
    Callback callback_;
    std::size_t execute_calls_ { };
};

struct GenericTicketOwner {
    GenericTicketOwner(GenericTicketProbe& probe,
        GenericCompactTask::Callback callback)
        : owner_identity(0x47454e455249434fULL)
        , task(probe, std::move(callback))
    {
    }

    std::uint64_t owner_identity;
    GenericCompactTask task;
};

void require_distinct_ticket_owner_and_task(
    const std::shared_ptr<GenericTicketOwner>& owner)
{
    require(owner.get() != static_cast<void*>(&owner->task),
        "the owner lifetime object is distinct from its ordered task");
}

class GenericSeedBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    using Callback = std::function<SchedulerBatchResult(
        Scheduler&, std::span<const std::uint64_t>)>;

    explicit GenericSeedBatch(Callback callback)
        : callback_(std::move(callback))
    {
    }

    SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        return callback_(scheduler, payloads);
    }

private:
    Callback callback_;
};

void schedule_seed(Scheduler& scheduler, GenericSeedBatch& seed)
{
    scheduler.schedule(SchedulerPhase::active, 0U,
        [&seed](Scheduler& current) {
            current.schedule_next_delta_batchable(
                SchedulerPhase::active, 0U, seed, 77U,
                [](Scheduler&) { });
        });
}

SchedulerGenericBatchFrontier require_seed_frontier(
    Scheduler& scheduler, const std::span<const std::uint64_t> payloads)
{
    require(payloads.size() == 1U && payloads.front() == 77U,
        "the compact-ticket source callback receives its real Generic seed");
    const auto frontier = scheduler.current_generic_batch_frontier();
    require(frontier && frontier->phase == SchedulerPhase::active
            && frontier->tasks.size() == 1U
            && frontier->tasks.front().payload == payloads.front(),
        "admission runs inside the scheduler's exact Generic Active frontier");
    return *frontier;
}

bool is_empty_receipt(const SchedulerGenericKeyReceipt& receipt) noexcept
{
    return !receipt.valid && receipt.time == 0U && receipt.delta == 0U
        && receipt.phase == SchedulerPhase::active
        && receipt.stable_order == 0U && receipt.sequence == 0U
        && receipt.payload == 0U;
}

void require_receipt_matches_offer(
    const SchedulerGenericKeyReceipt& receipt,
    const CapturedGenericOffer& offer,
    const std::size_t index)
{
    require(receipt.valid && receipt.time == offer.time
            && receipt.delta == offer.delta
            && receipt.phase == offer.phase
            && receipt.stable_order == offer.tasks[index].stable_order
            && receipt.sequence == offer.tasks[index].sequence
            && receipt.payload == offer.tasks[index].payload,
        "the receipt authenticates the exact admitted Generic scheduler key");
}

void test_refusal_is_atomic_and_foreign_equal_order_splits_ticket()
{
    GenericTicketProbe probe;
    std::shared_ptr<GenericTicketOwner> owner;
    std::weak_ptr<GenericTicketOwner> weak_owner;
    SchedulerGenericKeyReceipt rejected_receipt {
        true, 88U, 99U, SchedulerPhase::update, 123U, 456U, 789U };
    bool refused { };
    bool append_refused { };
    std::uint64_t source_sequence { };
    TraceCapture trace;

    owner = std::make_shared<GenericTicketOwner>(probe,
        [&probe](Scheduler&, const std::span<const std::uint64_t> payloads) {
            probe.events.insert(probe.events.end(),
                payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
    require_distinct_ticket_owner_and_task(owner);
    weak_owner = owner;
    probe.ticket_owner = owner;

    GenericSeedBatch seed([&](Scheduler& scheduler,
                              const std::span<const std::uint64_t> payloads) {
        const auto frontier = require_seed_frontier(scheduler, payloads);
        source_sequence = frontier.tasks.front().sequence;
        const SchedulerBatchGroupKey group_key {
            frontier.generation, 0x47454e434f4d5041U };
        const auto lifetime = std::static_pointer_cast<void>(owner);

        refused = !scheduler.schedule_generic_next_delta_readiness_member(
            owner->task, group_key, 0U, 30U, 999U, lifetime,
            &rejected_receipt);
        require(refused && is_empty_receipt(rejected_receipt),
            "zero-member admission clears its receipt and publishes no member");

        SchedulerGenericKeyReceipt first;
        require(scheduler.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, 3U, 30U, 101U, lifetime, &first),
            "the first bounded Generic member is admitted");
        probe.receipts.push_back(first);
        SchedulerGenericKeyReceipt mismatched_receipt {
            true, 77U, 78U, SchedulerPhase::update, 79U, 80U, 81U };
        append_refused
            = !scheduler.schedule_generic_next_delta_readiness_member(
                owner->task, group_key, 2U, 30U, 998U, lifetime,
                &mismatched_receipt);
        require(append_refused && is_empty_receipt(mismatched_receipt),
            "a mismatched component capacity cannot append or issue a receipt");
        scheduler.schedule_next_delta(SchedulerPhase::active, 30U,
            [&probe](Scheduler&) { probe.events.push_back(900U); });
        SchedulerGenericKeyReceipt lower_order;
        require(scheduler.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, 3U, 10U, 103U, lifetime,
                    &lower_order),
            "appending a lower-order Generic key rekeys the existing ticket");
        probe.receipts.push_back(lower_order);
        SchedulerGenericKeyReceipt last;
        require(scheduler.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, 3U, 30U, 102U, lifetime, &last),
            "a later equal-order member appends after the foreign key");
        probe.receipts.push_back(last);
        owner.reset();
        scheduler.set_trace_hook(&trace, &capture_trace);
        return SchedulerBatchResult { payloads.size(), { } };
    });

    Scheduler scheduler;
    schedule_seed(scheduler, seed);
    require(scheduler.run().status == RunStatus::completed
            && refused && append_refused && !probe.frontier_invalid
            && probe.events == std::vector<std::uint64_t> {
                103U, 101U, 900U, 102U }
            && probe.offers.size() == 2U
            && probe.offers[0U].payloads
                == std::vector<std::uint64_t> { 103U, 101U }
            && probe.offers[1U].payloads
                == std::vector<std::uint64_t> { 102U }
            && probe.receipts.size() == 3U
            && weak_owner.expired(),
        "one compact ticket rekeys to a lower append and splits around a foreign equal-order key");

    require(probe.receipts[0U].stable_order == 30U
            && probe.receipts[1U].stable_order == 10U
            && probe.receipts[2U].stable_order == 30U
            && probe.receipts[0U].sequence == source_sequence + 1U
            && probe.receipts[1U].sequence
                == probe.receipts[0U].sequence + 2U
            && probe.receipts[2U].sequence
                == probe.receipts[1U].sequence + 1U,
        "refusal consumes no sequence and equal-order sequence keys retain their insertion positions");
    require_receipt_matches_offer(probe.receipts[1U], probe.offers[0U], 0U);
    require_receipt_matches_offer(probe.receipts[0U], probe.offers[0U], 1U);
    require_receipt_matches_offer(probe.receipts[2U], probe.offers[1U], 0U);
    std::array<fsim::runtime::SchedulerBatchFrontierEntry, 4U> traced_keys { };
    std::size_t traced_key_count { };
    std::size_t target_batch_begins { };
    std::size_t target_batch_ends { };
    std::size_t target_batch_failures { };
    for (std::size_t index = 0U; index < trace.count; ++index) {
        const auto& record = trace.records[index];
        if (record.delta != probe.receipts[0U].delta
            || record.phase != SchedulerPhase::active
            || record.systemverilog) {
            continue;
        }
        if (record.kind == fsim::runtime::SchedulerTraceKind::batch_begin)
            ++target_batch_begins;
        else if (record.kind == fsim::runtime::SchedulerTraceKind::batch_end)
            ++target_batch_ends;
        else if (record.kind == fsim::runtime::SchedulerTraceKind::batch_failure)
            ++target_batch_failures;
        else if (record.kind == fsim::runtime::SchedulerTraceKind::task_end) {
            if (traced_key_count < traced_keys.size()) {
                traced_keys[traced_key_count++] = {
                    record.order, record.sequence, 0U };
            }
        }
    }
    require(!trace.overflow && target_batch_begins == 2U
            && target_batch_ends == 2U && target_batch_failures == 0U
            && traced_key_count == 4U
            && traced_keys[0U].stable_order == 10U
            && traced_keys[0U].sequence == probe.receipts[1U].sequence
            && traced_keys[1U].stable_order == 30U
            && traced_keys[1U].sequence == probe.receipts[0U].sequence
            && traced_keys[2U].stable_order == 30U
            && traced_keys[2U].sequence == probe.receipts[0U].sequence + 1U
            && traced_keys[3U].stable_order == 30U
            && traced_keys[3U].sequence == probe.receipts[2U].sequence,
        "late trace installation retains compact batch boundaries and exact per-key consumed order");
    const auto stats = scheduler.generic_batch_compaction_stats();
    require(stats.tickets == 1U && stats.members == 3U
            && stats.entries_elided == 2U
            && stats.direct_dispatches == 2U
            && stats.direct_members == 3U
            && stats.generic_readiness_ticket_queue_insertions == 1U
            && stats.generic_readiness_ticket_members == 3U
            && stats.generic_readiness_ticket_members_elided == 2U
            && probe.fallback_descriptors == 0U
            && !probe.owner_expired_in_execute,
        "three admitted keys use one physical ticket across a foreign split");
}

void test_declined_prefix_uses_one_ordered_fallback()
{
    GenericTicketProbe probe;
    std::shared_ptr<GenericTicketOwner> owner;
    std::weak_ptr<GenericTicketOwner> weak_owner;
    owner = std::make_shared<GenericTicketOwner>(probe,
        [&probe](Scheduler&, const std::span<const std::uint64_t> payloads) {
            if (probe.offers.size() == 1U)
                return SchedulerBatchResult { };
            probe.events.insert(probe.events.end(),
                payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
    require_distinct_ticket_owner_and_task(owner);
    weak_owner = owner;
    probe.ticket_owner = owner;

    GenericSeedBatch seed([&](Scheduler& scheduler,
                              const std::span<const std::uint64_t> payloads) {
        const auto frontier = require_seed_frontier(scheduler, payloads);
        const SchedulerBatchGroupKey group_key {
            frontier.generation, 0x47454e46414c4c42U };
        const auto lifetime = std::static_pointer_cast<void>(owner);
        for (std::uint64_t index = 0U; index < 2U; ++index) {
            SchedulerGenericKeyReceipt receipt;
            require(scheduler.schedule_generic_next_delta_readiness_member(
                        owner->task, group_key, 2U,
                        static_cast<StableOrder>(10U + index * 10U),
                        201U + index, lifetime, &receipt),
                "declined-prefix fixture admits each Generic member");
            probe.receipts.push_back(receipt);
        }
        owner.reset();
        return SchedulerBatchResult { payloads.size(), { } };
    });

    Scheduler scheduler;
    schedule_seed(scheduler, seed);
    require(scheduler.run().status == RunStatus::completed
            && !probe.frontier_invalid
            && probe.events == std::vector<std::uint64_t> {
                10201U, 202U }
            && probe.fallback_descriptors == 1U
            && !probe.owner_expired_in_fallback
            && !probe.owner_expired_in_execute
            && weak_owner.expired(),
        "a zero-prefix decline invokes one member fallback and retains the ordered suffix and owner");
    require(probe.offers.size() == 2U
            && probe.offers[0U].payloads
                == std::vector<std::uint64_t> { 201U, 202U }
            && probe.offers[1U].payloads
                == std::vector<std::uint64_t> { 202U },
        "fallback advances only its own key before direct dispatch resumes the suffix");
}

void test_consumed_prefix_failure_retries_only_suffix()
{
    GenericTicketProbe probe;
    std::shared_ptr<GenericTicketOwner> owner;
    std::weak_ptr<GenericTicketOwner> weak_owner;
    owner = std::make_shared<GenericTicketOwner>(probe,
        [&probe](Scheduler&, const std::span<const std::uint64_t> payloads) {
            if (probe.offers.size() == 1U) {
                probe.events.push_back(payloads.front());
                return SchedulerBatchResult { 1U,
                    std::make_exception_ptr(
                        std::runtime_error("generic compact prefix failure")) };
            }
            probe.events.insert(probe.events.end(),
                payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
    require_distinct_ticket_owner_and_task(owner);
    weak_owner = owner;
    probe.ticket_owner = owner;

    GenericSeedBatch seed([&](Scheduler& scheduler,
                              const std::span<const std::uint64_t> payloads) {
        const auto frontier = require_seed_frontier(scheduler, payloads);
        const SchedulerBatchGroupKey group_key {
            frontier.generation, 0x47454e4641494c31U };
        const auto lifetime = std::static_pointer_cast<void>(owner);
        for (std::uint64_t index = 0U; index < 3U; ++index) {
            SchedulerGenericKeyReceipt receipt;
            require(scheduler.schedule_generic_next_delta_readiness_member(
                        owner->task, group_key, 3U,
                        static_cast<StableOrder>(10U + index * 10U),
                        301U + index, lifetime, &receipt),
                "failure fixture admits the full compact Generic group");
            probe.receipts.push_back(receipt);
        }
        owner.reset();
        return SchedulerBatchResult { payloads.size(), { } };
    });

    Scheduler scheduler;
    schedule_seed(scheduler, seed);
    bool failed { };
    try {
        static_cast<void>(scheduler.run());
    } catch (const std::runtime_error& error) {
        failed = std::string_view { error.what() }
            == "generic compact prefix failure";
        if (!failed)
            throw;
    }
    require(failed && probe.events == std::vector<std::uint64_t> { 301U }
            && !weak_owner.expired() && probe.offers.size() == 1U,
        "a reported Generic prefix failure retires only the accepted key and preserves the ticket owner");
    require(scheduler.run().status == RunStatus::completed
            && probe.events == std::vector<std::uint64_t> {
                301U, 302U, 303U }
            && probe.offers.size() == 2U
            && probe.offers[1U].payloads
                == std::vector<std::uint64_t> { 302U, 303U }
            && probe.fallback_descriptors == 0U
            && !probe.owner_expired_in_execute
            && weak_owner.expired(),
        "retry after a consumed-prefix exception cannot replay the accepted member");
}

void test_stop_resume_and_discard_owner_lifetime()
{
    {
        GenericTicketProbe probe;
        std::shared_ptr<GenericTicketOwner> owner;
        std::weak_ptr<GenericTicketOwner> weak_owner;
        owner = std::make_shared<GenericTicketOwner>(probe,
            [&probe](Scheduler& scheduler,
                     const std::span<const std::uint64_t> payloads) {
                if (probe.offers.size() == 1U) {
                    probe.events.push_back(payloads.front());
                    scheduler.request_stop();
                    return SchedulerBatchResult { 1U, { } };
                }
                probe.events.insert(probe.events.end(),
                    payloads.begin(), payloads.end());
                return SchedulerBatchResult { payloads.size(), { } };
            });
        require_distinct_ticket_owner_and_task(owner);
        weak_owner = owner;
        probe.ticket_owner = owner;
        GenericSeedBatch seed([&](Scheduler& scheduler,
                                  const std::span<const std::uint64_t> payloads) {
            const auto frontier = require_seed_frontier(scheduler, payloads);
            const SchedulerBatchGroupKey group_key {
                frontier.generation, 0x47454e53544f5031U };
            const auto lifetime = std::static_pointer_cast<void>(owner);
            for (std::uint64_t index = 0U; index < 3U; ++index) {
                SchedulerGenericKeyReceipt receipt;
                require(scheduler.schedule_generic_next_delta_readiness_member(
                            owner->task, group_key, 3U,
                            static_cast<StableOrder>(10U + index * 10U),
                            401U + index, lifetime, &receipt),
                    "stop fixture admits each compact Generic key");
                probe.receipts.push_back(receipt);
            }
            owner.reset();
            return SchedulerBatchResult { payloads.size(), { } };
        });

        Scheduler scheduler;
        schedule_seed(scheduler, seed);
        require(scheduler.run().status == RunStatus::stopped
                && probe.events == std::vector<std::uint64_t> { 401U }
                && !weak_owner.expired(),
            "stopping after one accepted key preserves the queued compact suffix and owner");
        scheduler.clear_stop();
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> {
                    401U, 402U, 403U }
                && probe.offers.size() == 2U
                && probe.offers[1U].payloads
                    == std::vector<std::uint64_t> { 402U, 403U }
                && !probe.owner_expired_in_execute
                && weak_owner.expired(),
            "resuming a stopped Generic ticket dispatches only its unconsumed suffix");
    }

    {
        GenericTicketProbe probe;
        std::shared_ptr<GenericTicketOwner> owner;
        std::weak_ptr<GenericTicketOwner> weak_owner;
        owner = std::make_shared<GenericTicketOwner>(probe,
            [&probe](Scheduler&, const std::span<const std::uint64_t> payloads) {
                probe.events.insert(probe.events.end(),
                    payloads.begin(), payloads.end());
                return SchedulerBatchResult { payloads.size(), { } };
            });
        require_distinct_ticket_owner_and_task(owner);
        weak_owner = owner;
        probe.ticket_owner = owner;
        GenericSeedBatch seed([&](Scheduler& scheduler,
                                  const std::span<const std::uint64_t> payloads) {
            const auto frontier = require_seed_frontier(scheduler, payloads);
            const SchedulerBatchGroupKey group_key {
                frontier.generation, 0x47454e4449534341U };
            const auto lifetime = std::static_pointer_cast<void>(owner);
            for (std::uint64_t index = 0U; index < 2U; ++index) {
                SchedulerGenericKeyReceipt receipt;
                require(scheduler.schedule_generic_next_delta_readiness_member(
                            owner->task, group_key, 2U,
                            static_cast<StableOrder>(10U + index * 10U),
                            501U + index, lifetime, &receipt),
                    "discard fixture admits each compact Generic key");
                probe.receipts.push_back(receipt);
            }
            owner.reset();
            scheduler.request_stop();
            return SchedulerBatchResult { payloads.size(), { } };
        });

        Scheduler scheduler;
        schedule_seed(scheduler, seed);
        require(scheduler.run().status == RunStatus::stopped
                && !weak_owner.expired() && probe.events.empty(),
            "queued Generic ticket owns its executor before any member dispatch");
        scheduler.discard_pending();
        require(weak_owner.expired() && probe.events.empty()
                && probe.fallback_descriptors == 0U,
            "discard releases the compact ticket owner without invoking pending members");
    }
}

struct OverflowFallbackTrace {
    StableOrder order { };
    std::size_t matches { };
    std::uint64_t sequence { };
};

void capture_overflow_fallback_trace(void* context,
    const fsim::runtime::SchedulerTraceRecord& record) noexcept
{
    auto& capture = *static_cast<OverflowFallbackTrace*>(context);
    if (record.kind == fsim::runtime::SchedulerTraceKind::task_begin
        && record.order == capture.order) {
        ++capture.matches;
        capture.sequence = record.sequence;
    }
}

void test_generic_physical_ticket_overflow_growth()
{
    constexpr StableOrder fallback_order = 1000U;
    constexpr std::uint64_t fallback_payload = 0xf001U;
    GenericTicketProbe first_probe;
    auto first_owner = std::make_shared<GenericTicketOwner>(first_probe,
        GenericCompactTask::Callback { });
    const std::weak_ptr<GenericTicketOwner> first_weak_owner = first_owner;
    first_probe.ticket_owner = first_owner;
    std::vector<SchedulerGenericKeyReceipt> first_receipts;
    first_receipts.reserve(65U);
    bool overflow_refused { };
    std::uint64_t source_sequence { };
    OverflowFallbackTrace fallback_trace { fallback_order };
    Scheduler first_scheduler;
    require(first_scheduler.prepare_generic_readiness_ticket_capacity(65U),
        "Generic pool setup prepares 65 stable physical ticket slots");

    GenericSeedBatch first_seed([&](Scheduler& scheduler,
                                    const std::span<const std::uint64_t> payloads) {
        const auto frontier = require_seed_frontier(scheduler, payloads);
        source_sequence = frontier.tasks.front().sequence;
        const auto lifetime = std::static_pointer_cast<void>(first_owner);
        for (std::uint64_t index = 0U; index < 65U; ++index) {
            SchedulerGenericKeyReceipt receipt;
            const SchedulerBatchGroupKey key {
                frontier.generation, 0x47454e4f56000000U + index };
            require(scheduler.schedule_generic_next_delta_readiness_member(
                        first_owner->task, key, 1U,
                        static_cast<StableOrder>(100U + index),
                        5000U + index, lifetime, &receipt),
                "65 distinct Generic groups fit the prepared physical pool");
            first_receipts.push_back(receipt);
        }

        SchedulerGenericKeyReceipt rejected {
            true, 11U, 12U, SchedulerPhase::update, 13U, 14U, 15U };
        overflow_refused
            = !scheduler.schedule_generic_next_delta_readiness_member(
                first_owner->task,
                { frontier.generation, 0x47454e4f5600ffffU }, 1U,
                900U, 5999U, lifetime, &rejected);
        require(overflow_refused && is_empty_receipt(rejected),
            "a 66th distinct Generic group declines without a receipt");
        scheduler.schedule_next_delta(SchedulerPhase::active, fallback_order,
            [&first_probe, fallback_payload](Scheduler&) {
                first_probe.events.push_back(fallback_payload);
            });
        scheduler.request_stop();
        scheduler.set_trace_hook(&fallback_trace,
            &capture_overflow_fallback_trace);
        first_owner.reset();
        return SchedulerBatchResult { payloads.size(), { } };
    });

    schedule_seed(first_scheduler, first_seed);
    require(first_scheduler.run().status == RunStatus::stopped
            && !first_weak_owner.expired()
            && first_receipts.size() == 65U,
        "stopping with 65 queued Generic tickets retains their shared owner");
    require(!first_scheduler.prepare_generic_readiness_ticket_capacity(129U),
        "Generic pool growth refuses to relocate queued ticket objects");
    first_scheduler.clear_stop();
    require(first_scheduler.run().status == RunStatus::completed
            && first_probe.events.size() == 66U
            && first_probe.offers.size() == 65U
            && !first_probe.frontier_invalid
            && overflow_refused && first_probe.events.back() == fallback_payload
            && fallback_trace.matches == 1U
            && fallback_trace.sequence == source_sequence + 66U
            && first_weak_owner.expired(),
        "65 Generic tickets drain before the one ordinary fallback without consuming a rejected key");
    for (std::size_t index = 0U; index < 65U; ++index) {
        require(first_probe.events[index] == 5000U + index
                && first_receipts[index].valid
                && first_receipts[index].sequence
                    == source_sequence + index + 1U,
            "overflow tickets preserve each original receipt and stable order");
        require_receipt_matches_offer(first_receipts[index],
            first_probe.offers[index], 0U);
    }
    const auto first_stats
        = first_scheduler.generic_batch_compaction_stats();
    require(first_stats.generic_readiness_ticket_queue_insertions == 65U
            && first_stats.generic_readiness_ticket_members == 65U
            && first_stats.generic_readiness_ticket_members_elided == 0U
            && first_stats.direct_members == 65U,
        "physical overflow adds tickets without changing the logical member accounting");

    first_scheduler.set_trace_hook(nullptr, nullptr);
    require(first_scheduler.prepare_generic_readiness_ticket_capacity(129U),
        "draining all queues permits a strong-guarantee growth to 129 slots");

    GenericTicketProbe second_probe;
    auto second_owner = std::make_shared<GenericTicketOwner>(second_probe,
        GenericCompactTask::Callback { });
    const std::weak_ptr<GenericTicketOwner> second_weak_owner = second_owner;
    second_probe.ticket_owner = second_owner;
    std::vector<SchedulerGenericKeyReceipt> second_receipts;
    second_receipts.reserve(129U);
    std::uint64_t second_source_sequence { };
    GenericSeedBatch second_seed([&](Scheduler& scheduler,
                                     const std::span<const std::uint64_t> payloads) {
        const auto frontier = require_seed_frontier(scheduler, payloads);
        second_source_sequence = frontier.tasks.front().sequence;
        const auto lifetime = std::static_pointer_cast<void>(second_owner);
        for (std::uint64_t index = 0U; index < 129U; ++index) {
            SchedulerGenericKeyReceipt receipt;
            const SchedulerBatchGroupKey key {
                frontier.generation, 0x47454e4f56010000U + index };
            require(scheduler.schedule_generic_next_delta_readiness_member(
                        second_owner->task, key, 1U,
                        static_cast<StableOrder>(200U + index),
                        6000U + index, lifetime, &receipt),
                "129 distinct Generic groups fit after the safe pool growth");
            second_receipts.push_back(receipt);
        }
        second_owner.reset();
        return SchedulerBatchResult { payloads.size(), { } };
    });
    schedule_seed(first_scheduler, second_seed);
    require(first_scheduler.run().status == RunStatus::completed
            && second_probe.events.size() == 129U
            && second_probe.offers.size() == 129U
            && !second_probe.frontier_invalid
            && second_weak_owner.expired(),
        "all 129 overflow Generic tickets complete after pool growth");
    for (std::size_t index = 0U; index < 129U; ++index) {
        require(second_probe.events[index] == 6000U + index
                && second_receipts[index].valid
                && second_receipts[index].stable_order == 200U + index
                && second_receipts[index].sequence
                    == second_source_sequence + index + 1U,
            "the 129-slot pool retains the exact distinct-group members");
        require_receipt_matches_offer(second_receipts[index],
            second_probe.offers[index], 0U);
    }
    const auto second_stats
        = first_scheduler.generic_batch_compaction_stats();
    require(second_stats.generic_readiness_ticket_queue_insertions == 194U
            && second_stats.generic_readiness_ticket_members == 194U
            && second_stats.generic_readiness_ticket_members_elided == 0U
            && second_stats.direct_members == 194U,
        "Generic physical capacity growth preserves distinct-group ticket accounting");
}

void test_large_generic_ticket_retains_exact_suffix_across_foreign_cut()
{
    constexpr std::size_t member_count = 129U;
    constexpr std::uint64_t first_payload = 20000U;
    constexpr StableOrder foreign_order = 65U;
    constexpr std::uint64_t foreign_payload = 90000U;

    GenericTicketProbe probe;
    std::size_t execution_calls { };
    auto owner = std::make_shared<GenericTicketOwner>(probe,
        [&probe, &execution_calls](Scheduler&,
            const std::span<const std::uint64_t> payloads) {
            if (execution_calls++ == 0U) {
                constexpr std::size_t consumed_prefix = 10U;
                probe.events.insert(probe.events.end(), payloads.begin(),
                    payloads.begin()
                        + static_cast<std::ptrdiff_t>(consumed_prefix));
                std::exception_ptr failure;
                try {
                    throw std::runtime_error {
                        "large Generic ticket consumed-prefix failure" };
                } catch (...) {
                    failure = std::current_exception();
                }
                return SchedulerBatchResult { consumed_prefix,
                    std::move(failure) };
            }
            probe.events.insert(probe.events.end(),
                payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
    const std::weak_ptr<GenericTicketOwner> weak_owner = owner;
    probe.ticket_owner = owner;
    std::vector<SchedulerGenericKeyReceipt> receipts;
    receipts.reserve(member_count);
    std::uint64_t source_sequence { };
    SchedulerBatchGroupKey expected_group { };

    Scheduler scheduler;
    require(scheduler.prepare_generic_readiness_ticket_capacity(1U),
        "one physical Generic ticket can own a component-sized key vector");
    GenericSeedBatch seed([&](Scheduler& current,
                              const std::span<const std::uint64_t> payloads) {
        const auto frontier = require_seed_frontier(current, payloads);
        source_sequence = frontier.tasks.front().sequence;
        expected_group = { frontier.generation, 0x47454e4c41524745U };
        const auto group = expected_group;
        const auto lifetime = std::static_pointer_cast<void>(owner);
        for (std::size_t index = 0U; index < member_count; ++index) {
            SchedulerGenericKeyReceipt receipt;
            const auto stable_order = static_cast<StableOrder>(index * 2U);
            const auto payload = first_payload + index;
            require(current.schedule_generic_next_delta_readiness_member(
                        owner->task, group, member_count, stable_order,
                        payload, lifetime, &receipt)
                    && receipt.valid && receipt.stable_order == stable_order
                    && receipt.payload == payload,
                "all 129 exact Generic member keys append to one component ticket");
            receipts.push_back(receipt);
        }
        current.schedule_next_delta(SchedulerPhase::active, foreign_order,
            [&probe, foreign_payload](Scheduler&) {
                probe.events.push_back(foreign_payload);
            });
        owner.reset();
        return SchedulerBatchResult { payloads.size(), { } };
    });
    schedule_seed(scheduler, seed);
    bool consumed_prefix_failure { };
    try {
        static_cast<void>(scheduler.run());
    } catch (const std::runtime_error& error) {
        consumed_prefix_failure = std::string_view(error.what())
            == "large Generic ticket consumed-prefix failure";
    }
    require(consumed_prefix_failure && !weak_owner.expired()
            && probe.offers.size() == 1U && receipts.size() == member_count
            && probe.events.size() == 10U,
        "a failed native prefix retires only its consumed original keys and keeps the full ticket owner alive");
    require(scheduler.run().status == RunStatus::completed
            && weak_owner.expired() && !probe.frontier_invalid
            && probe.offers.size() == 4U,
        "the large Generic ticket retries only the retained suffix and releases its owner");

    constexpr std::array<std::size_t, 4U> offered_counts {
        33U, 23U, 64U, 32U };
    constexpr std::array<std::size_t, 4U> offered_offsets {
        0U, 10U, 33U, 97U };
    constexpr std::array<std::size_t, 4U> retained_counts {
        129U, 119U, 96U, 32U };
    for (std::size_t offer_index = 0U;
         offer_index < probe.offers.size(); ++offer_index) {
        const auto& offer = probe.offers[offer_index];
        if (offer_index != 0U) {
            require(offer.generation
                    == probe.offers[offer_index - 1U].generation + 1U,
                "each bounded retained suffix receives a fresh consecutive scheduler frontier generation");
        }
        require(offer.generation != 0U
                && offer.compact_group_key == expected_group
                && offer.ticket_member_offset == offered_offsets[offer_index]
                && offer.ticket_members.size() == retained_counts[offer_index]
                && offer.tasks.size() == offered_counts[offer_index]
                && offer.payloads.size() == offered_counts[offer_index],
            "each borrowed frontier exposes only its bounded offer plus authentic retained ticket keys");
        for (std::size_t index = 0U; index < offer.tasks.size(); ++index) {
            const auto absolute_index = offered_offsets[offer_index] + index;
            const auto& task = offer.tasks[index];
            const auto& ticket_member = offer.ticket_members[index];
            const auto& receipt = receipts[absolute_index];
            require(task.stable_order == ticket_member.order
                    && task.sequence == ticket_member.sequence
                    && task.payload == ticket_member.payload
                    && receipt.time == offer.time
                    && receipt.delta == offer.delta
                    && receipt.phase == offer.phase
                    && receipt.stable_order == ticket_member.order
                    && receipt.sequence == ticket_member.sequence
                    && receipt.payload == ticket_member.payload
                    && offer.payloads[index] == first_payload + absolute_index,
                "the offered prefix preserves its original receipt key and payload exactly once");
        }
        for (std::size_t index = 0U; index < offer.ticket_members.size(); ++index) {
            const auto absolute_index = offered_offsets[offer_index] + index;
            const auto& key = offer.ticket_members[index];
            const auto& receipt = receipts[absolute_index];
            require(key.order == receipt.stable_order
                    && key.sequence == receipt.sequence
                    && key.payload == receipt.payload,
                "the retained suffix is scheduler-authenticated for every queued member");
        }
    }
    for (std::size_t index = 0U; index < 33U; ++index) {
        require(probe.events.at(index) == first_payload + index,
            "the failed prefix and retried prefix preserve their original payload order");
    }
    require(probe.events.at(33U) == foreign_payload,
        "the foreign Active key executes between the authentic retained prefix and suffix");
    for (std::size_t index = 33U; index < member_count; ++index) {
        require(probe.events.at(index + 1U) == first_payload + index,
            "the suffix after a foreign cut resumes at the next unconsumed original key");
    }
    require(probe.events.size() == member_count + 1U,
        "the consumed-prefix retry neither duplicates nor loses any ticket member");

    const auto stats = scheduler.generic_batch_compaction_stats();
    require(stats.generic_readiness_ticket_queue_insertions == 1U
            && stats.generic_readiness_ticket_members == member_count
            && stats.generic_readiness_ticket_members_elided
                == member_count - 1U
            && stats.direct_dispatches == 4U
            && stats.direct_members == 152U
            && stats.generic_readiness_ticket_fallback_members == 0U,
        "one 129-member physical Generic ticket is consumed as 64-bounded offered prefixes across a foreign cut");
    require(receipts.front().sequence == source_sequence + 1U
            && receipts.back().sequence == source_sequence + member_count,
        "the compact ticket retains each original scheduler sequence without fabricating suffix keys");
}

void run_generic_compact_ticket_tests()
{
    test_refusal_is_atomic_and_foreign_equal_order_splits_ticket();
    test_declined_prefix_uses_one_ordered_fallback();
    test_consumed_prefix_failure_retries_only_suffix();
    test_stop_resume_and_discard_owner_lifetime();
    test_generic_physical_ticket_overflow_growth();
    test_large_generic_ticket_retains_exact_suffix_across_foreign_cut();
}

} // namespace
} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::run_generic_compact_ticket_tests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Generic compact-ticket test failure: "
                  << error.what() << '\n';
        return 1;
    }
}

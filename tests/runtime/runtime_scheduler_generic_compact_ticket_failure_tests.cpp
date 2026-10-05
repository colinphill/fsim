// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace {

using namespace fsim::runtime;
using fsim::runtime::detail::SchedulerTaskDescriptor;
using fsim::tests::runtime::staging_failure_support::
    allocation_failure_was_injected;
using fsim::tests::runtime::staging_failure_support::arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::end_allocation_count;
using fsim::tests::runtime::staging_failure_support::require;

constexpr std::uint64_t generic_group_identity = 0x47454e454e515545ULL;
constexpr std::size_t compact_component_size = 3U;
constexpr std::uint64_t seed_payload = 77U;

struct CapturedOffer {
    std::uint64_t generation { };
    SimulationTick time { };
    std::uint64_t delta { };
    SchedulerPhase phase { SchedulerPhase::active };
    std::size_t cursor { };
    std::size_t end { };
    std::array<SchedulerBatchFrontierEntry, compact_component_size> tasks { };
    std::array<std::uint64_t, compact_component_size> payloads { };
    std::size_t count { };
};

struct AdmissionTrial {
    SchedulerGenericKeyReceipt attempted_receipt {
        true, 88U, 99U, SchedulerPhase::update, 123U, 456U, 789U };
    std::array<SchedulerGenericKeyReceipt, compact_component_size> receipts { };
    CapturedOffer offer;
    SimulationTick source_time { };
    std::uint64_t source_delta { };
    std::uint64_t source_sequence { };
    SchedulerBatchCompactionStats stats_after_failed_attempt;
    std::size_t first_attempt_allocation_calls { };
    std::size_t append_allocation_calls { };
    std::size_t task_calls { };
    std::size_t fallback_calls { };
    bool source_frontier_valid { };
    bool first_attempt_threw { };
    bool failure_was_injected { };
    bool first_attempt_returned { };
    bool failure_receipt_cleared { };
    bool source_frontier_unchanged_after_failure { };
    bool no_ticket_published_after_failure { };
    bool first_member_admitted { };
    std::array<bool, compact_component_size - 1U> appended_members_admitted { };
    bool append_threw { };
    bool invalid_offer { };
    RunStatus run_status { RunStatus::stopped };
    SchedulerBatchCompactionStats final_stats;
};

struct FallbackPayload {
    AdmissionTrial* trial { };
};

void dispatch_fallback(Scheduler&, const FallbackPayload& payload)
{
    ++payload.trial->fallback_calls;
}

class CompactAdmissionTask final : public SchedulerOrderedBatchTask {
public:
    explicit CompactAdmissionTask(AdmissionTrial& trial) noexcept
        : trial_(trial)
    {
    }

    SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        ++trial_.task_calls;
        const auto frontier = scheduler.current_generic_batch_frontier();
        if (!frontier || frontier->phase != SchedulerPhase::active
            || frontier->cursor != 0U || frontier->end != payloads.size()
            || frontier->tasks.size() != payloads.size()
            || payloads.size() != compact_component_size) {
            trial_.invalid_offer = true;
            return { };
        }

        trial_.offer.generation = frontier->generation;
        trial_.offer.time = frontier->time;
        trial_.offer.delta = frontier->delta;
        trial_.offer.phase = frontier->phase;
        trial_.offer.cursor = frontier->cursor;
        trial_.offer.end = frontier->end;
        trial_.offer.count = payloads.size();
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            trial_.offer.tasks[index] = frontier->tasks[index];
            trial_.offer.payloads[index] = payloads[index];
            trial_.invalid_offer |= frontier->tasks[index].payload
                != payloads[index];
        }
        return { payloads.size(), { } };
    }

    SchedulerTaskDescriptor make_fallback_descriptor(
        const std::uint64_t) noexcept override
    {
        return fsim::runtime::detail::make_scheduler_task_descriptor<
            FallbackPayload, &dispatch_fallback>({ &trial_ });
    }

private:
    AdmissionTrial& trial_;
};

struct TicketOwner {
    explicit TicketOwner(AdmissionTrial& trial) noexcept
        : task(trial)
    {
    }

    std::uint64_t owner_cookie { 0x47454e455249434fULL };
    CompactAdmissionTask task;
};

class SeedBatch final : public SchedulerBatchTask {
public:
    using Callback = std::function<SchedulerBatchResult(
        Scheduler&, std::span<const std::uint64_t>)>;

    explicit SeedBatch(Callback callback)
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

bool is_empty_receipt(const SchedulerGenericKeyReceipt& receipt) noexcept
{
    return !receipt.valid && receipt.time == 0U && receipt.delta == 0U
        && receipt.phase == SchedulerPhase::active
        && receipt.stable_order == 0U && receipt.sequence == 0U
        && receipt.payload == 0U;
}

bool same_frontier_entry(const SchedulerBatchFrontierEntry& lhs,
    const SchedulerBatchFrontierEntry& rhs) noexcept
{
    return lhs.stable_order == rhs.stable_order
        && lhs.sequence == rhs.sequence && lhs.payload == rhs.payload;
}

void require_receipt_matches_task(
    const SchedulerGenericKeyReceipt& receipt,
    const CapturedOffer& offer, const std::size_t index)
{
    const auto& task = offer.tasks[index];
    require(receipt.valid && receipt.time == offer.time
            && receipt.delta == offer.delta && receipt.phase == offer.phase
            && receipt.stable_order == task.stable_order
            && receipt.sequence == task.sequence
            && receipt.payload == task.payload,
        "successful receipts match the exact Generic keys offered to the task");
}

AdmissionTrial run_admission_trial(const std::size_t fail_after)
{
    Scheduler scheduler;
    AdmissionTrial trial;
    auto owner = std::make_shared<TicketOwner>(trial);
    const auto lifetime = std::static_pointer_cast<void>(owner);
    SeedBatch seed([&](Scheduler& current,
                       const std::span<const std::uint64_t> payloads) {
        const auto source = current.current_generic_batch_frontier();
        require(source && source->phase == SchedulerPhase::active
                && source->tasks.size() == 1U && payloads.size() == 1U
                && source->tasks.front().payload == payloads.front()
                && payloads.front() == seed_payload,
            "the enqueue probe runs inside its original Generic Active callback");
        trial.source_frontier_valid = true;
        trial.source_time = source->time;
        trial.source_delta = source->delta;
        trial.source_sequence = source->tasks.front().sequence;
        const auto source_task = source->tasks.front();
        const SchedulerBatchGroupKey group_key {
            source->generation, generic_group_identity };

        begin_allocation_count();
        arm_allocation_failure(fail_after);
        try {
            trial.first_attempt_returned
                = current.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, compact_component_size, 30U,
                    401U, lifetime, &trial.attempted_receipt);
        } catch (const std::bad_alloc&) {
            trial.first_attempt_threw = true;
        }
        trial.failure_was_injected = allocation_failure_was_injected();
        clear_allocation_failure();
        trial.first_attempt_allocation_calls = end_allocation_count();

        if (trial.first_attempt_threw) {
            trial.failure_receipt_cleared
                = is_empty_receipt(trial.attempted_receipt);
            const auto source_after_failure
                = current.current_generic_batch_frontier();
            trial.source_frontier_unchanged_after_failure
                = source_after_failure
                && source_after_failure->generation == source->generation
                && source_after_failure->time == source->time
                && source_after_failure->delta == source->delta
                && source_after_failure->phase == SchedulerPhase::active
                && source_after_failure->tasks.size() == 1U
                && same_frontier_entry(source_after_failure->tasks.front(),
                    source_task);
            trial.stats_after_failed_attempt
                = current.generic_batch_compaction_stats();
            trial.no_ticket_published_after_failure
                = trial.stats_after_failed_attempt
                        .generic_readiness_ticket_queue_insertions == 0U
                && trial.stats_after_failed_attempt
                        .generic_readiness_ticket_members == 0U
                && trial.stats_after_failed_attempt
                        .generic_readiness_ticket_members_elided == 0U
                && trial.stats_after_failed_attempt.tickets == 0U
                && trial.stats_after_failed_attempt.members == 0U
                && trial.stats_after_failed_attempt.entries_elided == 0U;

            trial.first_member_admitted
                = current.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, compact_component_size, 30U,
                    401U, lifetime, &trial.receipts[0U]);
        } else {
            trial.receipts[0U] = trial.attempted_receipt;
            trial.first_member_admitted = trial.first_attempt_returned;
        }

        begin_allocation_count();
        try {
            trial.appended_members_admitted[0U]
                = current.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, compact_component_size, 10U,
                    400U, lifetime, &trial.receipts[1U]);
            trial.appended_members_admitted[1U]
                = current.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, compact_component_size, 30U,
                    402U, lifetime, &trial.receipts[2U]);
        } catch (...) {
            trial.append_threw = true;
        }
        trial.append_allocation_calls = end_allocation_count();
        return SchedulerBatchResult { payloads.size(), { } };
    });

    // Before run, next-delta work means the initial delta. The fresh callback
    // target is its empty next-delta queue, so the failure sweep visits every
    // allocation in first-time ticket admission, including its compact slab.
    scheduler.schedule_next_delta_batchable(SchedulerPhase::active, 0U,
        seed, seed_payload, [](Scheduler&) { });
    trial.run_status = scheduler.run().status;
    trial.final_stats = scheduler.generic_batch_compaction_stats();
    return trial;
}

void require_valid_trial(const AdmissionTrial& trial)
{
    require(trial.run_status == RunStatus::completed
            && trial.source_frontier_valid
            && trial.first_attempt_threw == trial.failure_was_injected,
        "each cut either injects inside first admission or reaches its success terminal");
    require(trial.first_member_admitted
            && trial.appended_members_admitted[0U]
            && trial.appended_members_admitted[1U]
            && !trial.append_threw && trial.append_allocation_calls == 0U,
        "after admission, appending into the reserved component slab allocates nothing");
    require(trial.receipts[0U].valid
            && trial.receipts[0U].time == trial.source_time
            && trial.receipts[0U].delta == trial.source_delta + 1U
            && trial.receipts[0U].phase == SchedulerPhase::active
            && trial.receipts[0U].stable_order == 30U
            && trial.receipts[0U].sequence == trial.source_sequence + 1U
            && trial.receipts[0U].payload == 401U
            && trial.receipts[1U].sequence == trial.source_sequence + 2U
            && trial.receipts[1U].stable_order == 10U
            && trial.receipts[1U].payload == 400U
            && trial.receipts[2U].sequence == trial.source_sequence + 3U
            && trial.receipts[2U].stable_order == 30U
            && trial.receipts[2U].payload == 402U,
        "failed admissions consume no sequence and retries issue the original exact keys");
    require(trial.task_calls == 1U && !trial.invalid_offer
            && trial.offer.generation != 0U
            && trial.offer.time == trial.source_time
            && trial.offer.delta == trial.source_delta + 1U
            && trial.offer.phase == SchedulerPhase::active
            && trial.offer.cursor == 0U && trial.offer.end == compact_component_size
            && trial.offer.count == compact_component_size
            && trial.offer.payloads
                == std::array<std::uint64_t, compact_component_size> {
                    400U, 401U, 402U },
        "the resulting native ordered task contains only the exact intended retry keys");
    require_receipt_matches_task(trial.receipts[1U], trial.offer, 0U);
    require_receipt_matches_task(trial.receipts[0U], trial.offer, 1U);
    require_receipt_matches_task(trial.receipts[2U], trial.offer, 2U);
    require(trial.final_stats.generic_readiness_ticket_queue_insertions == 1U
            && trial.final_stats.generic_readiness_ticket_members == 3U
            && trial.final_stats.generic_readiness_ticket_members_elided == 2U
            && trial.final_stats.tickets == 1U
            && trial.final_stats.members == 3U
            && trial.final_stats.entries_elided == 2U
            && trial.final_stats.direct_dispatches == 1U
            && trial.final_stats.direct_members == 3U
            && trial.fallback_calls == 0U,
        "only the successful admission path publishes one compact ticket");

    if (trial.first_attempt_threw) {
        require(trial.failure_receipt_cleared
                && trial.source_frontier_unchanged_after_failure
                && trial.no_ticket_published_after_failure
                && trial.first_attempt_allocation_calls != 0U,
            "an injected previsibility failure clears receipt and leaves no ticket or key");
    } else {
        require(trial.first_attempt_returned,
            "the sweep stops at the first successful no-injection cut");
    }
}

void test_first_admission_failure_cut_sweep()
{
    constexpr std::size_t maximum_allocation_cut = 64U;
    std::size_t injected_cuts { };
    bool reached_success_terminal { };

    for (std::size_t fail_after = 0U;
         fail_after <= maximum_allocation_cut; ++fail_after) {
        const auto trial = run_admission_trial(fail_after);
        require_valid_trial(trial);
        if (!trial.first_attempt_threw) {
            reached_success_terminal = true;
            break;
        }
        ++injected_cuts;
    }

    require(reached_success_terminal && injected_cuts != 0U,
        "bounded allocation sweep covers each failed admission cut through the first no-injection terminal");
}

constexpr std::size_t large_component_member_count = 129U;
constexpr std::size_t large_component_offer_capacity = 3U;
constexpr std::uint64_t large_component_group_identity
    = 0x47454e4c41524746ULL;
constexpr std::uint64_t large_component_payload_identity
    = 0x6000000000000000ULL;

struct LargeAdmissionTrial {
    std::array<SchedulerGenericKeyReceipt,
        large_component_member_count> receipts { };
    std::array<std::uint64_t, large_component_offer_capacity> offer_generations { };
    std::array<std::size_t, large_component_offer_capacity> offer_counts { };
    std::array<std::size_t, large_component_offer_capacity> offer_offsets { };
    std::size_t offer_count { };
    std::size_t consumed_members { };
    std::size_t fallback_calls { };
    std::uint64_t source_sequence { };
    std::uint64_t source_time { };
    std::uint64_t source_delta { };
    SchedulerBatchGroupKey group_key { };
    std::size_t admission_allocation_calls { };
    SchedulerBatchCompactionStats stats_after_failure;
    bool first_attempt_threw { };
    bool failure_was_injected { };
    bool first_attempt_returned { };
    bool receipt_cleared { };
    bool source_frontier_unchanged { };
    bool no_ticket_published { };
    bool invalid_offer { };
};

struct LargeFallbackPayload {
    LargeAdmissionTrial* trial { };
};

void dispatch_large_fallback(Scheduler&, const LargeFallbackPayload& payload)
{
    ++payload.trial->fallback_calls;
}

class LargeCompactAdmissionTask final : public SchedulerOrderedBatchTask {
public:
    explicit LargeCompactAdmissionTask(LargeAdmissionTrial& trial) noexcept
        : trial_(trial)
    {
    }

    SchedulerBatchResult execute(Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        const auto frontier = scheduler.current_generic_batch_frontier();
        if (!frontier || frontier->phase != SchedulerPhase::active
            || frontier->cursor != 0U
            || frontier->end != payloads.size()
            || frontier->tasks.size() != payloads.size()
            || frontier->compact_group_key != trial_.group_key
            || frontier->ticket_members.empty()
            || frontier->ticket_member_offset
                != trial_.consumed_members
            || frontier->ticket_members.size()
                != large_component_member_count - trial_.consumed_members
            || payloads.size() > 64U
            || trial_.offer_count >= large_component_offer_capacity) {
            trial_.invalid_offer = true;
            return { };
        }

        const auto offer_index = trial_.offer_count++;
        trial_.offer_generations[offer_index] = frontier->generation;
        trial_.offer_offsets[offer_index]
            = frontier->ticket_member_offset;
        trial_.offer_counts[offer_index] = payloads.size();
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            const auto absolute_index
                = frontier->ticket_member_offset + index;
            const auto& task = frontier->tasks[index];
            const auto& ticket_member = frontier->ticket_members[index];
            const auto& receipt = trial_.receipts[absolute_index];
            if (task.stable_order != ticket_member.order
                || task.sequence != ticket_member.sequence
                || task.payload != ticket_member.payload
                || payloads[index] != ticket_member.payload
                || !receipt.valid || receipt.time != frontier->time
                || receipt.delta != frontier->delta
                || receipt.phase != frontier->phase
                || receipt.stable_order != ticket_member.order
                || receipt.sequence != ticket_member.sequence
                || receipt.payload != ticket_member.payload) {
                trial_.invalid_offer = true;
                return { };
            }
        }
        trial_.consumed_members += payloads.size();
        return { payloads.size(), { } };
    }

    SchedulerTaskDescriptor make_fallback_descriptor(
        const std::uint64_t) noexcept override
    {
        return fsim::runtime::detail::make_scheduler_task_descriptor<
            LargeFallbackPayload, &dispatch_large_fallback>({ &trial_ });
    }

private:
    LargeAdmissionTrial& trial_;
};

struct LargeTicketOwner {
    explicit LargeTicketOwner(LargeAdmissionTrial& trial) noexcept
        : task(trial)
    {
    }

    std::uint64_t owner_cookie { large_component_group_identity };
    LargeCompactAdmissionTask task;
};

void test_large_component_reservation_failure_is_atomic_and_retryable()
{
    bool reached_success_terminal { };
    std::size_t injected_cuts { };
    for (std::size_t fail_after = 0U; fail_after <= 8U; ++fail_after) {
        Scheduler scheduler;
        LargeAdmissionTrial trial;
        auto owner = std::make_shared<LargeTicketOwner>(trial);
        const auto lifetime = std::static_pointer_cast<void>(owner);
        SeedBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            const auto source = current.current_generic_batch_frontier();
            require(source && source->phase == SchedulerPhase::active
                    && source->tasks.size() == 1U
                    && payloads.size() == 1U
                    && source->tasks.front().payload == seed_payload,
                "large-component reservation probe has its authentic source frontier");
            trial.source_time = source->time;
            trial.source_delta = source->delta;
            trial.source_sequence = source->tasks.front().sequence;
            trial.group_key = { source->generation,
                large_component_group_identity };

            SchedulerGenericKeyReceipt attempted {
                true, 88U, 99U, SchedulerPhase::update, 123U, 456U, 789U };
            begin_allocation_count();
            arm_allocation_failure(fail_after);
            try {
                trial.first_attempt_returned
                    = current.schedule_generic_next_delta_readiness_member(
                        owner->task, trial.group_key,
                        large_component_member_count, 0U,
                        large_component_payload_identity, lifetime,
                        &attempted);
            } catch (const std::bad_alloc&) {
                trial.first_attempt_threw = true;
            }
            trial.failure_was_injected = allocation_failure_was_injected();
            clear_allocation_failure();
            trial.admission_allocation_calls = end_allocation_count();

            if (trial.first_attempt_threw) {
                trial.receipt_cleared = is_empty_receipt(attempted);
                const auto after = current.current_generic_batch_frontier();
                trial.source_frontier_unchanged = after
                    && after->generation == source->generation
                    && after->time == source->time
                    && after->delta == source->delta
                    && after->phase == source->phase
                    && after->tasks.size() == source->tasks.size()
                    && same_frontier_entry(after->tasks.front(),
                        source->tasks.front());
                trial.stats_after_failure
                    = current.generic_batch_compaction_stats();
                trial.no_ticket_published
                    = trial.stats_after_failure
                            .generic_readiness_ticket_queue_insertions == 0U
                    && trial.stats_after_failure.members == 0U
                    && trial.stats_after_failure
                            .tickets == 0U
                    && trial.stats_after_failure
                            .generic_readiness_ticket_members == 0U
                    && trial.stats_after_failure
                            .generic_readiness_ticket_members_elided == 0U
                    && trial.stats_after_failure.entries_elided == 0U;
            } else {
                trial.receipts[0U] = attempted;
            }

            if (trial.first_attempt_threw) {
                require(current.schedule_generic_next_delta_readiness_member(
                            owner->task, trial.group_key,
                            large_component_member_count, 0U,
                            large_component_payload_identity, lifetime,
                            &trial.receipts[0U]),
                    "a failed 129-member first admission can retry at the same source key");
            } else {
                require(trial.first_attempt_returned
                        && trial.receipts[0U].valid,
                    "a non-injected 129-member first admission publishes its original receipt");
            }
            for (std::size_t index = 1U;
                 index < large_component_member_count; ++index) {
                const auto payload
                    = large_component_payload_identity + index;
                require(current.schedule_generic_next_delta_readiness_member(
                            owner->task, trial.group_key,
                            large_component_member_count,
                            static_cast<StableOrder>(index), payload, lifetime,
                            &trial.receipts[index]),
                    "the retried 129-member ticket admits every remaining exact key");
            }
            return SchedulerBatchResult { payloads.size(), { } };
        });

        scheduler.schedule_next_delta_batchable(SchedulerPhase::active, 0U,
            seed, seed_payload, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::completed
                && !trial.invalid_offer
                && trial.offer_count == 3U
                && trial.consumed_members == large_component_member_count
                && trial.fallback_calls == 0U,
            "a successful/retried 129-member ticket drains in three bounded callbacks");
        require(trial.first_attempt_threw == trial.failure_was_injected,
            "each large-component cut either injects in admission or reaches its first success");
        if (trial.first_attempt_threw) {
            ++injected_cuts;
            require(trial.receipt_cleared
                    && trial.source_frontier_unchanged
                    && trial.no_ticket_published
                    && trial.admission_allocation_calls == fail_after + 1U,
                "large slab allocation failure publishes no ticket, key, or partial member state");
        } else {
            reached_success_terminal = true;
        }

        constexpr std::array<std::size_t, 3U> expected_counts {
            64U, 64U, 1U };
        constexpr std::array<std::size_t, 3U> expected_offsets {
            0U, 64U, 128U };
        for (std::size_t index = 0U; index < large_component_member_count;
             ++index) {
            const auto& receipt = trial.receipts[index];
            require(receipt.valid && receipt.time == trial.source_time
                    && receipt.delta == trial.source_delta + 1U
                    && receipt.phase == SchedulerPhase::active
                    && receipt.stable_order
                        == static_cast<StableOrder>(index)
                    && receipt.sequence
                        == trial.source_sequence + 1U + index
                    && receipt.payload
                        == large_component_payload_identity + index,
                "failure retry preserves every original 129-member receipt key");
        }
        for (std::size_t index = 0U;
             index < large_component_offer_capacity; ++index) {
            require(trial.offer_generations[index] != 0U
                    && (index == 0U
                        || trial.offer_generations[index]
                            == trial.offer_generations[index - 1U] + 1U)
                    && trial.offer_counts[index] == expected_counts[index]
                    && trial.offer_offsets[index] == expected_offsets[index],
                "the retried ticket continues from exact 64-bounded suffix offsets");
        }
        const auto stats = scheduler.generic_batch_compaction_stats();
        require(stats.generic_readiness_ticket_queue_insertions == 1U
                && stats.generic_readiness_ticket_members
                    == large_component_member_count
                && stats.generic_readiness_ticket_members_elided
                    == large_component_member_count - 1U
                && stats.direct_dispatches == 3U
                && stats.direct_members == large_component_member_count,
            "the full retried component is one physical ticket with three authentic dispatch prefixes");
        if (reached_success_terminal)
            break;
    }
    require(reached_success_terminal && injected_cuts != 0U,
        "the bounded allocation sweep includes large-ticket failure cuts and a no-injection terminal");
}


constexpr std::size_t physical_pool_max_groups = 129U;
constexpr std::size_t generic_inline_ticket_capacity = 64U;
constexpr std::size_t physical_pool_pass_count = 3U;
constexpr std::uint64_t physical_pool_seed_payload = 0x6000000000000000ULL;
constexpr std::uint64_t physical_pool_member_payload = 0x7000000000000000ULL;
constexpr std::uint64_t physical_pool_group_identity = 0x504859534943414cULL;
constexpr std::size_t physical_pool_first_order = 1000U;

struct PhysicalPoolPass {
    std::array<SchedulerGenericKeyReceipt, physical_pool_max_groups> receipts { };
    std::array<std::size_t, physical_pool_max_groups> consumed { };
    std::size_t admitted_groups { };
    std::size_t attempted_groups { };
    std::size_t task_calls { };
    std::uint64_t source_generation { };
    SimulationTick source_time { };
    std::uint64_t source_delta { };
    std::uint64_t source_sequence { };
    std::size_t allocation_calls { };
    bool overflow_receipt_cleared { };
    bool invalid { };
};

struct PhysicalPoolProbe {
    std::array<PhysicalPoolPass, physical_pool_pass_count> passes { };
    std::size_t fallback_calls { };
    bool invalid { };
};

struct PhysicalPoolFallbackPayload {
    PhysicalPoolProbe* probe { };
};

void dispatch_physical_pool_fallback(
    Scheduler&, const PhysicalPoolFallbackPayload& payload)
{
    ++payload.probe->fallback_calls;
}

class PhysicalPoolOrderedTask final : public SchedulerOrderedBatchTask {
public:
    explicit PhysicalPoolOrderedTask(PhysicalPoolProbe& probe) noexcept
        : probe_(probe)
    {
    }

    SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        if (payloads.size() != 1U) {
            probe_.invalid = true;
            return { };
        }
        const auto frontier = scheduler.current_generic_batch_frontier();
        const auto payload = payloads.front();
        if (!frontier || frontier->phase != SchedulerPhase::active
            || frontier->cursor != 0U || frontier->end != 1U
            || frontier->tasks.size() != 1U
            || payload < physical_pool_member_payload) {
            probe_.invalid = true;
            return { };
        }

        const auto relative = payload - physical_pool_member_payload;
        const auto pass_index = static_cast<std::size_t>(
            relative / physical_pool_max_groups);
        const auto member_index = static_cast<std::size_t>(
            relative % physical_pool_max_groups);
        if (pass_index >= probe_.passes.size()) {
            probe_.invalid = true;
            return { };
        }
        auto& pass = probe_.passes[pass_index];
        const auto& key = frontier->tasks.front();
        if (member_index >= pass.admitted_groups
            || key.payload != payload
            || key.stable_order
                != physical_pool_first_order + member_index) {
            pass.invalid = true;
            return { };
        }
        const auto& receipt = pass.receipts[member_index];
        if (!receipt.valid || receipt.time != frontier->time
            || receipt.delta != frontier->delta
            || receipt.phase != frontier->phase
            || receipt.stable_order != key.stable_order
            || receipt.sequence != key.sequence
            || receipt.payload != key.payload) {
            pass.invalid = true;
        }
        ++pass.consumed[member_index];
        ++pass.task_calls;
        return { 1U, { } };
    }

    SchedulerTaskDescriptor make_fallback_descriptor(
        const std::uint64_t) noexcept override
    {
        return fsim::runtime::detail::make_scheduler_task_descriptor<
            PhysicalPoolFallbackPayload,
            &dispatch_physical_pool_fallback>({ &probe_ });
    }

private:
    PhysicalPoolProbe& probe_;
};

struct PhysicalPoolOwner {
    explicit PhysicalPoolOwner(PhysicalPoolProbe& probe) noexcept
        : task(probe)
    {
    }

    std::uint64_t owner_cookie { physical_pool_group_identity };
    PhysicalPoolOrderedTask task;
};

void run_physical_pool_pass(Scheduler& scheduler,
    PhysicalPoolProbe& probe,
    const std::shared_ptr<PhysicalPoolOwner>& owner,
    const std::size_t pass_index,
    const std::size_t group_count,
    const bool reject_one_over_capacity = false,
    const bool measure_whole_run = false)
{
    require(pass_index < probe.passes.size()
            && group_count <= physical_pool_max_groups,
        "physical pool pass dimensions remain within fixed test storage");
    auto& pass = probe.passes[pass_index];
    pass.admitted_groups = group_count;
    pass.attempted_groups
        = group_count + (reject_one_over_capacity ? 1U : 0U);

    SeedBatch seed([&](Scheduler& current,
                       const std::span<const std::uint64_t> payloads) {
        const auto frontier = current.current_generic_batch_frontier();
        if (!frontier || frontier->phase != SchedulerPhase::active
            || frontier->tasks.size() != 1U || payloads.size() != 1U
            || payloads.front()
                != physical_pool_seed_payload + pass_index
            || frontier->tasks.front().payload != payloads.front()) {
            pass.invalid = true;
            return SchedulerBatchResult { };
        }
        pass.source_generation = frontier->generation;
        pass.source_time = frontier->time;
        pass.source_delta = frontier->delta;
        pass.source_sequence = frontier->tasks.front().sequence;
        const auto lifetime = std::static_pointer_cast<void>(owner);

        for (std::size_t index = 0U;
             index < pass.attempted_groups; ++index) {
            SchedulerGenericKeyReceipt receipt {
                true, 88U, 99U, SchedulerPhase::update, 123U, 456U, 789U };
            const auto member_payload = physical_pool_member_payload
                + pass_index * physical_pool_max_groups + index;
            const SchedulerBatchGroupKey group_key {
                frontier->generation,
                physical_pool_group_identity + pass_index
                    * physical_pool_max_groups + index + 1U };
            const auto admitted
                = current.schedule_generic_next_delta_readiness_member(
                    owner->task, group_key, 1U,
                    physical_pool_first_order + index,
                    member_payload, lifetime, &receipt);
            if (index < group_count) {
                if (!admitted) {
                    pass.invalid = true;
                    continue;
                }
                pass.receipts[index] = receipt;
            } else {
                pass.overflow_receipt_cleared
                    = !admitted && is_empty_receipt(receipt);
            }
        }
        return SchedulerBatchResult { payloads.size(), { } };
    });

    const bool count_allocations = measure_whole_run;
    if (count_allocations)
        begin_allocation_count();
    try {
        scheduler.schedule_next_delta_batchable(SchedulerPhase::active, 0U,
            seed, physical_pool_seed_payload + pass_index,
            [](Scheduler&) { });
        const auto run_status = scheduler.run().status;
        if (count_allocations)
            pass.allocation_calls = end_allocation_count();
        if (run_status != RunStatus::completed)
            pass.invalid = true;
    } catch (...) {
        if (count_allocations)
            pass.allocation_calls = end_allocation_count();
        throw;
    }

    require(!pass.invalid && !probe.invalid,
        "every physical Generic ticket dispatch consumes its exact receipt key");
    require(pass.source_generation != 0U
            && pass.source_time == scheduler.now()
            && pass.source_delta < std::numeric_limits<std::uint64_t>::max(),
        "physical Generic admissions retain their source Active frontier");
    if (reject_one_over_capacity) {
        require(pass.overflow_receipt_cleared,
            "the exhausted 64-slot pool rejects its 65th group without a receipt");
    }
    for (std::size_t index = 0U; index < group_count; ++index) {
        const auto& receipt = pass.receipts[index];
        require(receipt.valid && receipt.time == pass.source_time
                && receipt.delta == pass.source_delta + 1U
                && receipt.phase == SchedulerPhase::active
                && receipt.stable_order == physical_pool_first_order + index
                && receipt.sequence == pass.source_sequence + 1U + index
                && receipt.payload == physical_pool_member_payload
                    + pass_index * physical_pool_max_groups + index
                && pass.consumed[index] == 1U,
            "grown Generic pools preserve each original receipt and consume it once");
    }
    require(pass.task_calls == group_count,
        "one physical ticket dispatch runs for each distinct Generic group");
}

void test_generic_pool_growth_failure_preserves_inline_slots()
{
    // Three Entry buffers are staged before the two overflow-ticket slabs.
    // Sweep each allocation so every failed preparation can prove that the
    // old inline pool remains usable before retrying the larger capacity.
    constexpr std::array<std::size_t, 5U> allocation_cuts {
        0U, 1U, 2U, 3U, 4U
    };
    for (const auto fail_after : allocation_cuts) {
        Scheduler scheduler;
        begin_allocation_count();
        arm_allocation_failure(fail_after);
        bool threw_bad_alloc { };
        try {
            (void)scheduler.prepare_generic_readiness_ticket_capacity(65U);
        } catch (const std::bad_alloc&) {
            threw_bad_alloc = true;
        }
        const auto injected = allocation_failure_was_injected();
        clear_allocation_failure();
        const auto growth_allocations = end_allocation_count();
        require(threw_bad_alloc && injected
                && growth_allocations == fail_after + 1U,
            "each cold queue-buffer or Generic slab allocation is injected");

        PhysicalPoolProbe probe;
        auto owner = std::make_shared<PhysicalPoolOwner>(probe);
        run_physical_pool_pass(scheduler, probe, owner, 0U,
            generic_inline_ticket_capacity,
            true);

        require(scheduler.prepare_generic_readiness_ticket_capacity(65U),
            "the failed cold growth can retry after all original inline tickets drain");
        run_physical_pool_pass(scheduler, probe, owner, 1U, 65U);
        require(scheduler.prepare_generic_readiness_ticket_capacity(129U),
            "the same scheduler can safely grow the retried 65-slot pool to 129");
        run_physical_pool_pass(scheduler, probe, owner, 2U, 129U);

        require(!probe.invalid && probe.fallback_calls == 0U
                && probe.passes[1U].source_sequence
                    == probe.passes[0U].source_sequence + 65U
                && probe.passes[2U].source_sequence
                    == probe.passes[1U].source_sequence + 66U,
            "failure/retry preserves sequence order across 64, 65, and 129 groups");
    }
}

void test_warmed_generic_129_group_admission_uses_no_allocations()
{
    Scheduler scheduler;
    require(scheduler.prepare_generic_readiness_ticket_capacity(129U),
        "the fixed Generic topology prepares its 129 physical ticket slots");
    PhysicalPoolProbe probe;
    auto owner = std::make_shared<PhysicalPoolOwner>(probe);

    // Three reusable Entry buffers serve the seed entries, its batch suffix
    // insertions, and the 129 next-delta tickets. Two warm passes let the
    // fixed topology retain its peak before the measured cycle.
    run_physical_pool_pass(scheduler, probe, owner, 0U, 129U);
    run_physical_pool_pass(scheduler, probe, owner, 1U, 129U);
    run_physical_pool_pass(scheduler, probe, owner, 2U, 129U, false, true);

    const auto stats = scheduler.generic_batch_compaction_stats();
    require(probe.passes[2U].allocation_calls == 0U
            && stats.tickets == 387U && stats.members == 387U
            && stats.entries_elided == 0U
            && stats.generic_readiness_ticket_queue_insertions == 387U
            && stats.generic_readiness_ticket_members == 387U
            && stats.generic_readiness_ticket_members_elided == 0U
            && stats.generic_readiness_ticket_fallback_members == 0U
            && stats.direct_dispatches == 387U
            && stats.direct_members == 387U,
        "a warmed 129-group Generic Active admission and dispatch allocates nothing");
}

} // namespace

int main()
{
    try {
        test_first_admission_failure_cut_sweep();
        test_large_component_reservation_failure_is_atomic_and_retryable();
        test_generic_pool_growth_failure_preserves_inline_slots();
        test_warmed_generic_129_group_admission_uses_no_allocations();
        return 0;
    } catch (const std::exception& error) {
        clear_allocation_failure();
        std::cerr << "Generic compact-ticket allocation test failure: "
                  << error.what() << '\n';
        return 1;
    }
}

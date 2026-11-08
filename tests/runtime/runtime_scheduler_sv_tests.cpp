// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/scheduler_events.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using fsim::runtime::Scheduler;
using fsim::runtime::SchedulerPhase;
using fsim::runtime::RunStatus;
using fsim::runtime::SimulationTick;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void test_frozen_rounds_and_nested_inactive()
{
    Scheduler scheduler;
    std::string events;
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler& current) {
        events += 'a';
        current.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler& next) {
            events += 'c';
            require(next.delta() == 0 && next.systemverilog_round() == 2,
                "SV rounds must not advance the generic cycle");
        });
        current.schedule_systemverilog(SchedulerPhase::inactive, 0, [&](Scheduler& next) {
            events += 'd';
            next.schedule_systemverilog(SchedulerPhase::inactive, 0, [&](Scheduler&) {
                events += 'g';
            });
            next.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler&) {
                events += 'f';
            });
        });
        current.schedule_systemverilog(SchedulerPhase::inactive, 1, [&](Scheduler&) {
            events += 'e';
        });
        current.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler&) {
            events += 'h';
        });
    });
    scheduler.schedule_systemverilog(SchedulerPhase::active, 1, [&](Scheduler&) {
        events += 'b';
    });
    static_cast<void>(scheduler.run());
    require(events == "abcdefgh", "Active rounds and frozen Inactive batches precede NBA");
}

void test_complete_nba_batch_before_active()
{
    Scheduler scheduler;
    std::string events;
    unsigned value = 0;
    scheduler.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler& current) {
        events += 'a';
        value = 1;
        current.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler&) {
            events += 'c';
            require(value == 2, "derived-clock Active work sees the complete NBA batch");
        });
        current.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler&) {
            events += 'd';
        });
    });
    scheduler.schedule_systemverilog(SchedulerPhase::update, 1, [&](Scheduler&) {
        events += 'b';
        value = 2;
    });
    static_cast<void>(scheduler.run());
    require(events == "abcd", "new NBA work must not join the frozen NBA batch");
}

void test_generic_crossings_and_end_of_slot()
{
    Scheduler scheduler;
    std::string events;
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler& current) {
        events += 'a';
        current.schedule_next_delta(SchedulerPhase::active, 0, [&](Scheduler& foreign) {
            events += 'c';
            require(foreign.delta() == 1, "SV to foreign bridge keeps the next generic cycle");
            foreign.schedule_next_delta(SchedulerPhase::active, 0, [&](Scheduler& bridge) {
                bridge.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler& sv) {
                    events += 'e';
                    require(sv.delta() == 2, "foreign to SV bridge keeps the next generic cycle");
                });
            });
        });
        current.schedule_end_of_time_slot(0, [&](Scheduler& final) {
            events += 'f';
            require(final.delta() == 2, "end-of-slot waits for all language crossings");
            require(final.current_phase() == SchedulerPhase::postponed,
                "end-of-slot callbacks expose the postponed phase");
        });
    });
    scheduler.schedule(SchedulerPhase::postponed, 0, [&](Scheduler&) { events += 'b'; });
    scheduler.schedule_next_delta(SchedulerPhase::postponed, 1, [&](Scheduler&) {
        // Before run, generic next_delta has the documented initial-cycle meaning.
        events += 'B';
    });
    scheduler.schedule(SchedulerPhase::active, 0, [&](Scheduler& current) {
        current.schedule_next_delta(SchedulerPhase::postponed, 0, [&](Scheduler&) {
            events += 'd';
        });
    });
    static_cast<void>(scheduler.run());
    // Postponed work of every cycle runs once, after the slot's last delta
    // (IEEE 1800-2023 4.4.2.9, IEEE 1076-2019 14.7.5.3), in stable order.
    require(events == "acebfdB", "foreign postponed observations wait for the end of the slot");
}

struct RuntimeSlotQuietState {
    Scheduler* scheduler { };
    std::size_t calls { };
    bool inside_quiet_hook { };
    bool outside_safe_point { };
    bool phase_was_drained { };
    bool future_work_remained_at_zero { };
    unsigned completed_phase_bits { };
    std::array<SimulationTick, 2U> times { };
};

void record_runtime_slot_quiet(void* context) noexcept
{
    auto& state = *static_cast<RuntimeSlotQuietState*>(context);
    const auto index = state.calls++;
    if (index >= state.times.size()) {
        return;
    }
    state.times[index] = state.scheduler->now();
    state.inside_quiet_hook
        |= state.scheduler->at_runtime_slot_quiet_point();
    state.outside_safe_point
        |= !state.scheduler->at_safe_point();
    if (index == 0U) {
        state.phase_was_drained = state.phase_was_drained
            && !state.scheduler->current_phase().has_value();
        state.future_work_remained_at_zero
            = state.scheduler->next_pending_time() == 2U;
    }
}

void test_runtime_slot_quiet_hook()
{
    Scheduler scheduler;
    RuntimeSlotQuietState state;
    state.scheduler = &scheduler;
    scheduler.set_runtime_slot_quiet_hook(
        &state, &record_runtime_slot_quiet);
    scheduler.schedule(SchedulerPhase::active, 0U,
        [&state](Scheduler& current) {
            state.completed_phase_bits |= 1U;
            current.schedule_next_delta(SchedulerPhase::active, 0U,
                [&state](Scheduler&) {
                    state.completed_phase_bits |= 2U;
                });
        });
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&state](Scheduler&) { state.completed_phase_bits |= 4U; });
    scheduler.schedule_systemverilog(SchedulerPhase::update, 0U,
        [&state](Scheduler&) { state.completed_phase_bits |= 8U; });
    scheduler.schedule_systemverilog(SchedulerPhase::postponed, 0U,
        [&state](Scheduler&) { state.completed_phase_bits |= 16U; });
    scheduler.schedule_end_of_time_slot(0U,
        [&state](Scheduler&) {
            state.phase_was_drained = state.completed_phase_bits == 31U;
        });
    scheduler.schedule_at(2U, SchedulerPhase::active, 0U,
        [](Scheduler&) { });

    const auto first = scheduler.run(0U);
    require(first.status == RunStatus::time_limit,
        "quiet-point callback runs before a bounded run returns");
    require(state.calls == 1U && state.times[0] == 0U,
        "first quiet point follows the complete initial time slot");
    require(state.inside_quiet_hook && state.outside_safe_point
            && state.phase_was_drained,
        "quiet-point callback identifies a drained slot, not a phase point");
    require(state.future_work_remained_at_zero,
        "future timed work may remain when a slot is quiet");

    const auto second = scheduler.run(2U);
    require(second.status == RunStatus::completed,
        "quiet-point callback runs before completion after future work");
    require(state.calls == 2U && state.times[1] == 2U,
        "future slots each receive their own quiet-point callback");
    scheduler.set_runtime_slot_quiet_hook(nullptr, nullptr);
    require(!scheduler.at_runtime_slot_quiet_point(),
        "quiet-point status is scoped to callback invocation");
}

void test_observed_and_reactive_iterations()
{
    Scheduler scheduler;
    std::string events;
    scheduler.schedule_systemverilog(SchedulerPhase::observed, 0, [&](Scheduler&) {
        events += 'b';
    });
    scheduler.schedule_systemverilog(SchedulerPhase::reactive, 0, [&](Scheduler& current) {
        events += 'c';
        current.schedule_systemverilog(SchedulerPhase::inactive, 0, [&](Scheduler& next) {
            require(next.current_phase() == SchedulerPhase::re_inactive,
                "reactive zero-delay work enters Re-Inactive");
            events += 'd';
            next.schedule_systemverilog(SchedulerPhase::reactive, 0, [&](Scheduler&) {
                events += 'e';
            });
        });
        current.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler& next) {
            require(next.current_phase() == SchedulerPhase::re_update,
                "reactive NBA work enters Re-NBA");
            events += 'f';
        });
    });
    scheduler.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler&) {
        events += 'a';
    });
    scheduler.schedule_systemverilog(SchedulerPhase::postponed, 0, [&](Scheduler&) {
        events += 'g';
    });
    static_cast<void>(scheduler.run());
    require(events == "abcdefg", "observed/reactive ordering follows ordinary quiescence");
}

void test_stop_resume_and_cancellation()
{
    Scheduler scheduler;
    std::string events;
    std::uint64_t round = 0;
    scheduler.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler& current) {
        events += 'a';
        round = current.systemverilog_round();
        current.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler&) {
            events += 'c';
        });
        current.request_stop();
    });
    scheduler.schedule_systemverilog(SchedulerPhase::update, 1, [&](Scheduler& current) {
        events += 'b';
        require(current.systemverilog_round() == round,
            "stop/resume preserves the unfinished NBA batch");
    });
    auto canceled = scheduler.schedule_systemverilog_after_cancelable(
        5, SchedulerPhase::active, 0, [&](Scheduler&) { events += 'X'; });
    scheduler.cancel(canceled);
    require(!canceled, "SV cancellation invalidates its handle");
    require(scheduler.run().status == RunStatus::stopped && events == "a",
        "stop interrupts a batch between callbacks");
    scheduler.clear_stop();
    require(scheduler.run().status == RunStatus::completed && events == "abc",
        "resume drains the NBA suffix before Active");
    require(scheduler.now() == 0 && !scheduler.has_pending(),
        "canceled SV future work must not advance simulation time");
}

void test_limit_and_reset()
{
    for (auto phase : { SchedulerPhase::active, SchedulerPhase::inactive }) {
        Scheduler scheduler({ .max_delta_cycles = 4 });
        unsigned count = 0;
        Scheduler::Task repeat;
        repeat = [&](Scheduler& current) {
            ++count;
            current.schedule_systemverilog_next_delta(phase, 9, repeat);
        };
        scheduler.schedule_systemverilog(phase, 9, repeat);
        bool failed = false;
        try {
            static_cast<void>(scheduler.run());
        } catch (const fsim::runtime::DeltaCycleLimitError& error) {
            failed = error.time() == 0 && error.limit() == 4
                && error.pending_orders() == std::vector<fsim::runtime::StableOrder> { 9 };
        }
        require(failed && count == 4 && scheduler.delta() == 0,
            "same-slot SV feedback has a separate bounded round counter");
        scheduler.reset();
        require(!scheduler.has_pending() && scheduler.systemverilog_round() == 0,
            "reset discards SV work and its round identity");
    }
}

void test_failure_and_discard()
{
    Scheduler scheduler;
    std::string events;
    scheduler.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler& current) {
        events += 'a';
        current.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler&) {
            events += 'c';
        });
        throw std::runtime_error("injected callback failure");
    });
    scheduler.schedule_systemverilog(SchedulerPhase::update, 1, [&](Scheduler&) {
        events += 'b';
    });
    bool failed = false;
    try {
        static_cast<void>(scheduler.run());
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed && events == "a", "SV callback failures propagate to checked execution");
    static_cast<void>(scheduler.run());
    require(events == "abc", "failure preserves the NBA suffix before pending Active work");
    auto pending = scheduler.schedule_systemverilog_after_cancelable(
        9, SchedulerPhase::active, 0, [&](Scheduler&) { events += 'X'; });
    scheduler.schedule_end_of_time_slot(0, [&](Scheduler&) { events += 'Y'; });
    scheduler.discard_pending();
    require(!pending && !scheduler.has_pending(), "discard releases every SV queue and handle");
    static_cast<void>(scheduler.run());
    require(events == "abc", "discarded SV and end callbacks never execute");
}

void test_single_forward_iteration_limit()
{
    Scheduler scheduler({ .max_delta_cycles = 1 });
    std::string events;
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0, [&](Scheduler& current) {
        events += 'a';
        current.schedule_systemverilog(SchedulerPhase::inactive, 0, [&](Scheduler& next) {
            events += 'b';
            next.schedule_systemverilog(SchedulerPhase::update, 0, [&](Scheduler& final) {
                events += 'c';
                require(final.systemverilog_round() == 1,
                    "forward region progression stays in one SV iteration");
            });
        });
    });
    static_cast<void>(scheduler.run());
    require(events == "abc", "finite forward regions must not consume the iteration limit");
}

struct DescriptorPayload {
    unsigned* count;
};

void dispatch_descriptor(Scheduler& scheduler, const DescriptorPayload& payload)
{
    require(scheduler.now() == 7 && scheduler.delta() == 0,
        "timed SV descriptor retains its time and generic cycle");
    ++*payload.count;
}

void test_descriptors_and_end_callback_mutation()
{
    Scheduler scheduler;
    unsigned count = 0;
    const auto descriptor = fsim::runtime::detail::make_scheduler_task_descriptor<
        DescriptorPayload, dispatch_descriptor>({ &count });
    scheduler.schedule_internal_systemverilog_at(7, SchedulerPhase::active, 0, descriptor);
    scheduler.schedule_systemverilog_at(7, SchedulerPhase::active, 1, [&](Scheduler& current) {
        current.schedule_internal_systemverilog_next_delta(SchedulerPhase::active, 0, descriptor);
        current.schedule_end_of_time_slot(0, [&](Scheduler& final) {
            require(count == 2, "end callback waits for descriptor rounds");
            final.schedule_next_delta(SchedulerPhase::active, 0, [&](Scheduler&) { ++count; });
        });
        current.schedule_end_of_time_slot(1, [&](Scheduler& final) {
            require(count == 3 && final.delta() == 1,
                "host mutation from an end callback is settled before later observation");
        });
    });
    require(scheduler.run(6).status == RunStatus::time_limit && count == 0,
        "time limit preserves future SV descriptor work");
    static_cast<void>(scheduler.run());
    require(count == 3, "SV descriptors and end callbacks execute exactly once");
}

using fsim::runtime::SchedulerBatchResult;
using fsim::runtime::SchedulerBatchTask;
using fsim::runtime::SchedulerBatchGroupKey;
using fsim::runtime::SchedulerTraceKind;
using fsim::runtime::SchedulerTraceRecord;
using fsim::runtime::StableOrder;
namespace detail = fsim::runtime::detail;

struct GenericReservationTrace {
    std::uint64_t sequence { std::numeric_limits<std::uint64_t>::max() };
    std::string events;

    static void receive(
        void* context, const SchedulerTraceRecord& record) noexcept
    {
        auto& trace = *static_cast<GenericReservationTrace*>(context);
        if (record.kind == SchedulerTraceKind::task_begin
            && record.order == 9U) {
            trace.sequence = record.sequence;
        }
    }
};

class WaveBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    using Callback = std::function<fsim::runtime::SchedulerBatchResult(
        Scheduler&, std::span<const std::uint64_t>)>;

    explicit WaveBatch(Callback callback)
        : callback_(std::move(callback))
    {
    }

    fsim::runtime::SchedulerBatchResult execute(Scheduler& scheduler,
        std::span<const std::uint64_t> payloads) override
    {
        return callback_(scheduler, payloads);
    }

private:
    Callback callback_;
};

struct CompactOrderedBatchProbe {
    std::vector<std::uint64_t> events;
    std::vector<std::uint64_t> event_rounds;
    std::weak_ptr<void> owner;
    std::size_t fallback_descriptors { };
    std::size_t execute_calls { };
    bool owner_live_in_execute { };
    bool owner_live_in_fallback { };
};

struct CompactOrderedFallbackPayload {
    CompactOrderedBatchProbe* probe { };
    std::uint64_t payload { };
};

void dispatch_compact_ordered_fallback(
    Scheduler& scheduler, const CompactOrderedFallbackPayload& payload)
{
    payload.probe->events.push_back(payload.payload);
    payload.probe->event_rounds.push_back(scheduler.systemverilog_round());
    payload.probe->owner_live_in_fallback
        = !payload.probe->owner.expired();
}

class CompactOrderedBatch final
    : public fsim::runtime::SchedulerOrderedBatchTask {
public:
    using Callback = std::function<fsim::runtime::SchedulerBatchResult(
        Scheduler&, std::span<const std::uint64_t>)>;

    CompactOrderedBatch(CompactOrderedBatchProbe& probe, Callback callback)
        : probe_(probe)
        , callback_(std::move(callback))
    {
    }

    fsim::runtime::SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        ++probe_.execute_calls;
        probe_.owner_live_in_execute |= !probe_.owner.expired();
        return callback_(scheduler, payloads);
    }

    fsim::runtime::detail::SchedulerTaskDescriptor make_fallback_descriptor(
        const std::uint64_t payload) noexcept override
    {
        ++probe_.fallback_descriptors;
        return fsim::runtime::detail::make_scheduler_task_descriptor<
            CompactOrderedFallbackPayload,
            &dispatch_compact_ordered_fallback>({ &probe_, payload });
    }

private:
    CompactOrderedBatchProbe& probe_;
    Callback callback_;
};

struct SchedulerKeyReceiptTraceCapture {
    std::array<SchedulerTraceRecord, 4U> task_begins;
    std::size_t count { };

    static void receive(void* context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& capture = *static_cast<SchedulerKeyReceiptTraceCapture*>(context);
        if (record.kind == SchedulerTraceKind::task_begin
            && capture.count < capture.task_begins.size()) {
            capture.task_begins[capture.count++] = record;
        }
    }
};

struct SchedulerDiscardReceiptProbe {
    Scheduler* scheduler { };
    WaveBatch* batch { };
    fsim::runtime::SchedulerSystemVerilogKeyReceipt* receipt { };
    Scheduler::Task fallback_task;
    bool attempted { };

    static void schedule_during_discard(void* context) noexcept
    {
        auto& probe = *static_cast<SchedulerDiscardReceiptProbe*>(context);
        probe.attempted = true;
        probe.scheduler->schedule_systemverilog_group_batchable(
            SchedulerPhase::active, 0U, *probe.batch, 900U,
            std::move(probe.fallback_task), { }, probe.receipt);
    }
};

void test_sv_composite_ticket_foreign_split()
{
    Scheduler scheduler;
    std::vector<std::string> events;
    WaveBatch batch([&events](Scheduler&,
                          const std::span<const std::uint64_t> payloads) {
        require(payloads.size() == 1U,
            "foreign queue work splits a component batch run");
        events.push_back("batch" + std::to_string(payloads.front()));
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey group_key { 7U, 5U };
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 20U, batch, 20U,
        [](Scheduler&) { }, group_key);
    scheduler.schedule_systemverilog(SchedulerPhase::active, 25U,
        [&events](Scheduler&) { events.push_back("foreign"); });
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 30U, batch, 30U,
        [](Scheduler&) { }, group_key);

    require(scheduler.run().status == RunStatus::completed
            && events == std::vector<std::string> {
                "batch20", "foreign", "batch30" },
        "a preexisting foreign entry splits otherwise matching members");
    const auto compacted = scheduler.systemverilog_batch_compaction_stats();
    require(compacted.tickets == 0U && compacted.members == 0U,
        "nonadjacent component entries are not compacted across foreign work");
    require(compacted.direct_dispatches == 0U
            && compacted.direct_members == 0U,
        "foreign queue entries are never crossed by direct ticket dispatch");
}

void test_sv_readiness_tickets_interleave_without_crossing_foreign_writer()
{
    Scheduler scheduler;
    std::vector<std::string> events;
    std::vector<std::vector<std::uint64_t>> offers;
    WaveBatch batch([&](Scheduler&,
                        const std::span<const std::uint64_t> payloads) {
        offers.emplace_back(payloads.begin(), payloads.end());
        for (const auto payload : payloads) {
            events.push_back(std::to_string(payload));
        }
        return fsim::runtime::SchedulerBatchResult { payloads.size(), { } };
    });
    const fsim::runtime::SchedulerBatchGroupKey group_a { 31U, 1U };
    const fsim::runtime::SchedulerBatchGroupKey group_b { 31U, 2U };

    require(scheduler.schedule_systemverilog_readiness_member(
                SchedulerPhase::active, 3U, batch, 103U,
                [&events](Scheduler&) { events.push_back("fallback-a3"); },
                group_a)
            && scheduler.schedule_systemverilog_readiness_member(
                SchedulerPhase::active, 6U, batch, 106U,
                [&events](Scheduler&) { events.push_back("fallback-a6"); },
                group_a)
            && scheduler.schedule_systemverilog_readiness_member(
                SchedulerPhase::active, 4U, batch, 204U,
                [&events](Scheduler&) { events.push_back("fallback-b4"); },
                group_b)
            && scheduler.schedule_systemverilog_readiness_member(
                SchedulerPhase::active, 7U, batch, 207U,
                [&events](Scheduler&) { events.push_back("fallback-b7"); },
                group_b),
        "two component groups retain each member's original scheduler key");
    scheduler.schedule_systemverilog(SchedulerPhase::active, 5U,
        [&events](Scheduler&) { events.push_back("foreign-blocking-writer"); });

    require(scheduler.run().status == RunStatus::completed
            && events == std::vector<std::string> { "103", "204",
                "foreign-blocking-writer", "106", "207" }
            && offers == std::vector<std::vector<std::uint64_t>> {
                { 103U }, { 204U }, { 106U }, { 207U } },
        "interleaved component tickets yield to a foreign blocking writer between virtual keys");
    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(stats.readiness_ticket_queue_insertions == 2U
            && stats.readiness_ticket_members == 4U
            && stats.readiness_ticket_members_elided == 2U
            && stats.direct_dispatches == 4U
            && stats.direct_members == 4U,
        "both interleaved groups use one real ticket each without crossing the foreign key");
}

void test_sv_readiness_ticket_ordering_and_lifecycle()
{
    {
        Scheduler scheduler;
        std::vector<std::string> events;
        std::vector<std::vector<std::uint64_t>> offers;
        bool late_member_used_ticket { true };
        WaveBatch batch([&](Scheduler&,
                            const std::span<const std::uint64_t> payloads) {
            offers.emplace_back(payloads.begin(), payloads.end());
            for (const auto payload : payloads) {
                events.push_back("member" + std::to_string(payload));
            }
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 21U, 4U };
        require(scheduler.schedule_systemverilog_readiness_member(
                    SchedulerPhase::active, 20U, batch, 20U,
                    [&events](Scheduler&) {
                        events.push_back("fallback20");
                    }, group_key)
                && scheduler.schedule_systemverilog_readiness_member(
                    SchedulerPhase::active, 40U, batch, 40U,
                    [&events](Scheduler&) {
                        events.push_back("fallback40");
                    }, group_key),
            "same-component ready members share one pending ticket");
        scheduler.schedule_systemverilog(SchedulerPhase::active, 30U,
            [&events](Scheduler&) { events.push_back("foreign30"); });
        scheduler.schedule_systemverilog(SchedulerPhase::active, 10U,
            [&late_member_used_ticket, &events, &batch, group_key](
                Scheduler& current) {
                events.push_back("seed10");
                late_member_used_ticket
                    = current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, 25U, batch, 25U,
                        [&events](Scheduler&) {
                            events.push_back("fallback25");
                        }, group_key);
            });

        require(scheduler.run().status == RunStatus::completed
                && late_member_used_ticket
                && events == std::vector<std::string> { "seed10",
                    "member20", "foreign30", "member40", "member25" }
                && offers == std::vector<std::vector<std::uint64_t>> {
                    { 20U }, { 40U }, { 25U } },
            "a late readiness member enters the next round without crossing current foreign work");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == 2U
                && stats.readiness_ticket_members == 3U
                && stats.readiness_ticket_members_elided == 1U
                && stats.readiness_ticket_fallback_members == 0U
                && stats.direct_dispatches == 3U
                && stats.direct_members == 3U,
            "readiness statistics count one real queue insertion and its elided member");
    }

    {
        Scheduler scheduler;
        std::vector<std::uint64_t> visited;
        std::size_t calls { };
        WaveBatch batch([&](Scheduler& current,
                            const std::span<const std::uint64_t> payloads) {
            ++calls;
            if (calls == 1U) {
                visited.push_back(payloads.front());
                current.request_stop();
                return SchedulerBatchResult { 1U, { } };
            }
            visited.insert(visited.end(), payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 22U, 4U };
        for (std::uint64_t payload = 1U; payload <= 3U; ++payload) {
            require(scheduler.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, payload, batch, payload,
                        [&visited](Scheduler&) {
                            visited.push_back(99U);
                        }, group_key),
                "bounded component ticket accepts its initial readiness set");
        }
        require(scheduler.run().status == RunStatus::stopped
                && visited == std::vector<std::uint64_t> { 1U },
            "stop retains the unaccepted readiness suffix");
        scheduler.clear_stop();
        require(scheduler.run().status == RunStatus::completed
                && visited == std::vector<std::uint64_t> { 1U, 2U, 3U }
                && calls == 2U,
            "resumption consumes the readiness suffix once in original order");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == 1U
                && stats.readiness_ticket_members == 3U
                && stats.readiness_ticket_members_elided == 2U
                && stats.direct_dispatches == 2U
                && stats.direct_members == 5U,
            "stop/resume keeps one readiness ticket and accepted-prefix accounting");
    }
}

void test_sv_readiness_ticket_failure_and_large_group()
{
    {
        Scheduler scheduler;
        std::vector<std::uint64_t> visited;
        std::size_t calls { };
        WaveBatch batch([&](Scheduler&,
                            const std::span<const std::uint64_t> payloads) {
            ++calls;
            if (calls == 1U) {
                throw std::runtime_error("unaccepted readiness ticket throw");
            }
            if (calls == 2U) {
                visited.push_back(payloads.front());
                return SchedulerBatchResult { 1U,
                    std::make_exception_ptr(
                        std::runtime_error("accepted readiness prefix failure")) };
            }
            visited.insert(visited.end(), payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 23U, 7U };
        for (std::uint64_t payload = 1U; payload <= 2U; ++payload) {
            require(scheduler.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, payload, batch, payload,
                        [](Scheduler&) {
                            throw std::runtime_error(
                                "accepted readiness member used fallback");
                        }, group_key),
                "readiness ticket retains each original fallback task");
        }

        bool threw_before_acceptance { };
        try {
            static_cast<void>(scheduler.run());
        } catch (const std::runtime_error& error) {
            threw_before_acceptance = std::string_view { error.what() }
                == "unaccepted readiness ticket throw";
            if (!threw_before_acceptance)
                throw;
        }
        require(threw_before_acceptance && scheduler.has_pending()
                && visited.empty(),
            "a throwing ticket callback leaves every unaccepted member queued");

        bool threw_after_prefix { };
        try {
            static_cast<void>(scheduler.run());
        } catch (const std::runtime_error& error) {
            threw_after_prefix = std::string_view { error.what() }
                == "accepted readiness prefix failure";
            if (!threw_after_prefix)
                throw;
        }
        require(threw_after_prefix && scheduler.has_pending()
                && visited == std::vector<std::uint64_t> { 1U },
            "a reported accepted prefix is retired before its failure escapes");
        require(scheduler.run().status == RunStatus::completed
                && calls == 3U
                && visited == std::vector<std::uint64_t> { 1U, 2U },
            "retry executes only the unaccepted readiness suffix");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == 1U
                && stats.readiness_ticket_members == 2U
                && stats.readiness_ticket_members_elided == 1U
                && stats.direct_dispatches == 3U
                && stats.direct_members == 5U,
            "failure and retry retain one queue node and exact prefix counts");
    }

    {
        Scheduler scheduler;
        std::vector<std::vector<std::uint64_t>> offers;
        WaveBatch batch([&](Scheduler&,
                            const std::span<const std::uint64_t> payloads) {
            offers.emplace_back(payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 24U, 7U };
        for (std::uint64_t payload = 1U; payload <= 65U; ++payload) {
            const bool ticketed
                = scheduler.schedule_systemverilog_readiness_member(
                    SchedulerPhase::active, payload, batch, payload,
                    [](Scheduler&) {
                        throw std::runtime_error(
                            "large readiness member used fallback");
                    }, group_key);
            require(ticketed,
                "one readiness ticket accepts more than 64 component members");
        }
        require(scheduler.run().status == RunStatus::completed
                && offers.size() == 2U
                && offers[0U].size() == 64U
                && offers[1U] == std::vector<std::uint64_t> { 65U }
                && offers[0U].front() == 1U
                && offers[0U].back() == 64U,
            "one large readiness ticket preserves the 64+1 ready order");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == 1U
                && stats.readiness_ticket_members == 65U
                && stats.readiness_ticket_members_elided == 64U
                && stats.readiness_ticket_fallback_members == 0U
                && stats.direct_dispatches == 1U
                && stats.direct_members == 1U,
            "readiness accounting separates one ticket from the 64-entry dispatch scratch");
    }

    {
        Scheduler scheduler;
        std::vector<std::uint64_t> visited;
        WaveBatch batch([&](Scheduler&,
                            const std::span<const std::uint64_t> payloads) {
            visited.insert(visited.end(), payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 25U, 7U };
        for (std::uint64_t payload = 1U; payload <= 2U; ++payload) {
            require(scheduler.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, payload, batch, payload,
                        [&visited](Scheduler&) { visited.push_back(99U); },
                        group_key),
                "discard fixture builds one bounded readiness ticket");
        }
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [](Scheduler& current) { current.request_stop(); });
        require(scheduler.run().status == RunStatus::stopped,
            "stop leaves the readiness ticket available for discard");
        scheduler.discard_pending();
        scheduler.clear_stop();
        for (std::uint64_t payload = 3U; payload <= 4U; ++payload) {
            require(scheduler.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, payload, batch, payload,
                        [&visited](Scheduler&) { visited.push_back(99U); },
                        group_key),
                "discard releases its bounded ticket pool slot");
        }
        require(scheduler.run().status == RunStatus::completed
                && visited == std::vector<std::uint64_t> { 3U, 4U },
            "discard removes old ticket members without invoking their fallback");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == 2U
                && stats.readiness_ticket_members == 4U
                && stats.readiness_ticket_members_elided == 2U
                && stats.readiness_ticket_fallback_members == 0U
                && stats.direct_dispatches == 1U
                && stats.direct_members == 2U,
            "discarded readiness state is absent from later queue counts");
    }
}

void test_sv_readiness_ticket_pool_growth_refuses_queued_storage()
{
    constexpr std::size_t group_count = 65U;
    Scheduler scheduler;
    WaveBatch batch([](Scheduler&, const std::span<const std::uint64_t> payloads) {
        return fsim::runtime::SchedulerBatchResult { payloads.size(), { } };
    });
    require(scheduler.prepare_systemverilog_readiness_ticket_capacity(
                group_count),
        "the queued-storage fixture prepares an overflow ticket slot");
    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&batch](Scheduler& current) {
            for (std::size_t index = 0U; index < group_count; ++index) {
                Scheduler::ReadinessBatchMember member;
                member.stable_order = index;
                member.payload = index + 1U;
                member.fallback_task = [](Scheduler&) { };
                std::array<Scheduler::ReadinessBatchMember, 1U> members {
                    std::move(member) };
                require(current.schedule_systemverilog_readiness_group(
                            SchedulerPhase::active, batch,
                            { 92U, index + 1U }, members),
                    "all 65 queued group keys own their prepared ticket slots");
            }
            current.request_stop();
        });

    require(scheduler.run().status == RunStatus::stopped,
        "the scheduler stops with all 65 readiness tickets still queued");
    require(!scheduler.prepare_systemverilog_readiness_ticket_capacity(129U),
        "pool growth refuses to relocate a stopped overflow ticket");
    scheduler.discard_pending();
    scheduler.clear_stop();
    require(scheduler.prepare_systemverilog_readiness_ticket_capacity(129U),
        "discard releases overflow references before the pool grows again");
}

void test_sv_composite_ticket_late_observation_decline()
{
    Scheduler scheduler;
    bool component_epoch_current = true;
    std::vector<std::string> events;
    std::vector<std::vector<std::uint64_t>> offers;
    WaveBatch batch([&](Scheduler&,
                        const std::span<const std::uint64_t> payloads) {
        offers.emplace_back(payloads.begin(), payloads.end());
        if (!component_epoch_current)
            return SchedulerBatchResult { };
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey group_key { 7U, 6U };
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 20U, batch, 20U,
        [&events](Scheduler&) { events.push_back("fallback20"); }, group_key);
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 30U, batch, 30U,
        [&events](Scheduler&) { events.push_back("fallback30"); }, group_key);
    scheduler.schedule_systemverilog(SchedulerPhase::active, 10U,
        [&component_epoch_current, &events](Scheduler&) {
            events.push_back("observe_and_demote");
            component_epoch_current = false;
        });

    require(scheduler.run().status == RunStatus::completed
            && events == std::vector<std::string> {
                "observe_and_demote", "fallback20", "fallback30" }
            && offers == std::vector<std::vector<std::uint64_t>> {
                { 20U, 30U }, { 30U } },
        "late component invalidation declines a compacted ticket without reordering originals");
    const auto compacted = scheduler.systemverilog_batch_compaction_stats();
    require(compacted.tickets == 1U && compacted.members == 2U
            && compacted.entries_elided == 1U
            && compacted.direct_dispatches == 2U
            && compacted.direct_members == 3U,
        "stale group keys remain a queue hint, not a bypass of consumer validation");
}

void test_sv_composite_ticket_group_boundary()
{
    Scheduler scheduler;
    std::vector<std::vector<std::uint64_t>> batches;
    WaveBatch batch([&batches](Scheduler&,
                           const std::span<const std::uint64_t> payloads) {
        batches.emplace_back(payloads.begin(), payloads.end());
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey first_group { 8U, 1U };
    const SchedulerBatchGroupKey second_group { 8U, 2U };
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 10U, batch, 10U, [](Scheduler&) { },
        first_group);
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 20U, batch, 20U, [](Scheduler&) { },
        first_group);
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 30U, batch, 30U, [](Scheduler&) { },
        second_group);
    scheduler.schedule_systemverilog_group_batchable(
        SchedulerPhase::active, 40U, batch, 40U, [](Scheduler&) { },
        second_group);

    require(scheduler.run().status == RunStatus::completed
            && batches == std::vector<std::vector<std::uint64_t>> {
                { 10U, 20U }, { 30U, 40U } },
        "adjacent component tickets dispatch separately across group identity");
    const auto compacted = scheduler.systemverilog_batch_compaction_stats();
    require(compacted.tickets == 2U && compacted.members == 4U
            && compacted.entries_elided == 2U
            && compacted.direct_dispatches == 2U
            && compacted.direct_members == 4U,
        "each same-component contiguous run has its own bounded ticket");
}

void test_sv_composite_ticket_discard()
{
    Scheduler scheduler;
    std::size_t fallback_calls { };
    WaveBatch batch([](Scheduler&,
                       const std::span<const std::uint64_t> payloads) {
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey group_key { 11U, 2U };
    for (std::uint64_t payload = 1U; payload <= 2U; ++payload) {
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, payload + 20U, batch, payload,
            [&fallback_calls](Scheduler&) { ++fallback_calls; }, group_key);
    }
    scheduler.schedule_systemverilog(SchedulerPhase::active, 10U,
        [](Scheduler& current) { current.request_stop(); });
    require(scheduler.run().status == RunStatus::stopped,
        "stop leaves an unconsumed composite ticket pending");
    scheduler.discard_pending();
    scheduler.clear_stop();
    require(scheduler.run().status == RunStatus::completed
            && fallback_calls == 0U,
        "discard releases composite ticket storage without running suffixes");
}

void test_sv_composite_ticket_throw_before_acceptance()
{
    Scheduler scheduler;
    std::size_t calls { };
    std::size_t fallback_calls { };
    std::vector<std::uint64_t> visited;
    WaveBatch batch([&](Scheduler&,
                        const std::span<const std::uint64_t> payloads) {
        ++calls;
        if (calls == 1U)
            throw std::runtime_error("pre-accept ticket callback failure");
        visited.insert(visited.end(), payloads.begin(), payloads.end());
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey group_key { 12U, 5U };
    for (std::uint64_t payload = 1U; payload <= 2U; ++payload) {
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, payload, batch, payload,
            [&fallback_calls](Scheduler&) { ++fallback_calls; }, group_key);
    }

    bool failed { };
    try {
        static_cast<void>(scheduler.run());
    } catch (const std::runtime_error& error) {
        failed = std::string_view { error.what() }
            == "pre-accept ticket callback failure";
        if (!failed)
            throw;
    }
    require(failed && scheduler.has_pending() && visited.empty(),
        "a thrown pre-accept callback leaves the original ticket intact");
    require(scheduler.run().status == RunStatus::completed
            && calls == 2U
            && visited == std::vector<std::uint64_t> { 1U, 2U }
            && fallback_calls == 0U,
        "retry dispatches the still-unaccepted ticket exactly once");
    const auto compacted = scheduler.systemverilog_batch_compaction_stats();
    require(compacted.direct_dispatches == 2U
            && compacted.direct_members == 4U,
        "both attempts use direct ticket dispatch with original members");
}

void test_sv_composite_ticket_cancel_during_direct_dispatch()
{
    Scheduler scheduler;
    constexpr std::size_t cancelable_count = 64U;
    std::vector<fsim::runtime::ScheduledTaskHandle> cancelable;
    cancelable.reserve(cancelable_count);
    for (std::size_t index = 0U; index < cancelable_count; ++index) {
        cancelable.push_back(scheduler.schedule_systemverilog_after_cancelable(
            0U, SchedulerPhase::active,
            100U + static_cast<fsim::runtime::StableOrder>(index),
            [](Scheduler&) {
                throw std::runtime_error("cancelled task unexpectedly ran");
            }));
    }

    std::vector<std::uint64_t> visited;
    WaveBatch batch([&](Scheduler& current,
                        const std::span<const std::uint64_t> payloads) {
        require(payloads.size() == 2U,
            "active ticket remains addressable during cancellation compaction");
        for (const auto& handle : cancelable)
            current.cancel(handle);
        visited.insert(visited.end(), payloads.begin(), payloads.end());
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey group_key { 12U, 6U };
    for (std::uint64_t payload = 1U; payload <= 2U; ++payload) {
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, payload, batch, payload,
            [](Scheduler&) {
                throw std::runtime_error("accepted ticket member fell back");
            }, group_key);
    }

    require(scheduler.run().status == RunStatus::completed
            && visited == std::vector<std::uint64_t> { 1U, 2U },
        "cancellation-driven queue compaction preserves the active ticket");
    const auto compacted = scheduler.systemverilog_batch_compaction_stats();
    require(compacted.direct_dispatches == 1U
            && compacted.direct_members == 2U,
        "the compacted active queue node is dispatched only once");
}

void test_sv_batch_bounded_fallback()
{
    constexpr std::uint64_t count = 129U;
    for (const bool decline : { false, true }) {
        Scheduler scheduler;
        std::vector<std::uint64_t> visited;
        std::size_t offered { };
        std::size_t largest { };
        bool stopped { };
        WaveBatch batch([&](Scheduler& current, std::span<const std::uint64_t> payloads) {
            offered += payloads.size();
            if (payloads.size() > largest) {
                largest = payloads.size();
            }
            if (decline) {
                return fsim::runtime::SchedulerBatchResult { };
            }
            visited.insert(visited.end(), payloads.begin(), payloads.end());
            if (!stopped) {
                stopped = true;
                current.request_stop();
            }
            return fsim::runtime::SchedulerBatchResult { payloads.size(), { } };
        });
        for (std::uint64_t id = 0U; id < count; ++id) {
            scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
                id, batch, id, [&, id](Scheduler&) { visited.push_back(id); });
        }
        const auto first = scheduler.run();
        if (!decline) {
            require(first.status == RunStatus::stopped && visited.size() == 64U,
                "bounded SV batch stop preserves the unexecuted suffix");
            scheduler.clear_stop();
            require(scheduler.run().status == RunStatus::completed,
                "bounded SV batch suffix resumes");
        } else {
            require(first.status == RunStatus::completed,
                "declined SV batches complete through ordinary fallback");
        }
        require(largest == 64U && offered <= count * 64U,
            "declining SV batches must have bounded queue extraction cost");
        require(visited.size() == count,
            "bounded SV batching executes every original task once");
        for (std::uint64_t id = 0U; id < count; ++id) {
            require(visited[id] == id,
                "bounded SV batch fallback preserves stable task order");
        }
    }
}

void test_sv_batch_partial_resume()
{
    enum class Interruption { stop, failure, decline };
    for (const auto interruption : { Interruption::stop, Interruption::failure,
             Interruption::decline }) {
        Scheduler scheduler;
        std::string events;
        const auto first = [&](Scheduler& current) {
            events += 'a';
            current.schedule_systemverilog(SchedulerPhase::active, 0U,
                [&](Scheduler&) { events += 'c'; });
        };
        WaveBatch batch([&](Scheduler& current, std::span<const std::uint64_t> payloads) {
            require(current.current_phase() == SchedulerPhase::update,
                "remaining NBA work retains the frozen batch phase");
            if (payloads.front() == 1U) {
                if (interruption == Interruption::decline) {
                    return fsim::runtime::SchedulerBatchResult { };
                }
                first(current);
                if (interruption == Interruption::failure) {
                    return fsim::runtime::SchedulerBatchResult { 1U,
                        std::make_exception_ptr(std::runtime_error("wave failure")) };
                }
                current.request_stop();
                return fsim::runtime::SchedulerBatchResult { 1U, { } };
            }
            require(payloads.size() == 1U && payloads.front() == 2U,
                "accepted prefixes must not replay on resume");
            events += 'b';
            return fsim::runtime::SchedulerBatchResult { 1U, { } };
        });
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::update,
            0U, batch, 1U, first);
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::update,
            1U, batch, 2U, [](Scheduler&) {
                throw std::runtime_error("unexpected suffix fallback");
            });
        bool failed { };
        try {
            const auto result = scheduler.run();
            require(result.status == (interruption == Interruption::stop
                        ? RunStatus::stopped : RunStatus::completed),
                "batch interruption reports the requested status");
        } catch (const std::runtime_error& error) {
            failed = std::string_view { error.what() } == "wave failure";
            if (!failed)
                throw;
        }
        require(failed == (interruption == Interruption::failure),
            "only the requested batch failure escapes");
        if (interruption != Interruption::decline) {
            require(events == "a", "interruption consumes only the accepted prefix");
            scheduler.clear_stop();
            static_cast<void>(scheduler.run());
        }
        require(events == "abc", "NBA suffix precedes derived Active work after resume");
    }
}

// ---------------------------------------------------------------------------
// Scheduler event registry, save_future and restore_future (#128).

using fsim::runtime::SchedulerCancelBinder;
using fsim::runtime::SchedulerEventClass;
using fsim::runtime::SchedulerEventOwners;
using fsim::runtime::SchedulerEventReader;
using fsim::runtime::SchedulerEventWriter;
using fsim::runtime::SchedulerQuietPoint;
using fsim::runtime::SchedulerSavedState;
using fsim::runtime::ScheduledTaskHandle;

std::string& event_log()
{
    static std::string value;
    return value;
}

struct EventCounter {
    std::uint64_t total { };
};

struct EventDomain {
    static constexpr std::string_view name { "test" };
};

struct PlainEvent {
    std::uint64_t id { };
    std::uint64_t spare { };
};

void plain_event(Scheduler&, const PlainEvent& event)
{
    event_log() += 'p' + std::to_string(event.id) + ';';
}

struct ChainEvent {
    std::uint64_t id { };
    std::uint64_t last { };
};

[[nodiscard]] fsim::runtime::detail::SchedulerTaskDescriptor chain_descriptor(
    std::uint64_t id, std::uint64_t last);

void chain_event(Scheduler& scheduler, const ChainEvent& event)
{
    event_log() += 'c' + std::to_string(event.id) + ';';
    if (event.id < event.last) {
        scheduler.schedule_internal_next_delta(SchedulerPhase::active, 0U,
            chain_descriptor(event.id + 1U, event.last));
    }
}

struct OwnedEvent {
    EventCounter* owner { };
    std::uint32_t amount { };
};

void owned_event(Scheduler&, const OwnedEvent& event)
{
    event.owner->total += event.amount;
    event_log() += 'o' + std::to_string(event.owner->total) + ';';
}

struct OwnedCodec
    : fsim::runtime::OwnedFieldsSchedulerEventCodec<OwnedEvent, EventCounter,
          EventDomain, &OwnedEvent::owner, &OwnedEvent::amount> { };

struct CancelableEvent {
    EventCounter* owner { };
    std::uint64_t key { };
};

void cancelable_event(Scheduler&, const CancelableEvent& event)
{
    event_log() += 'k' + std::to_string(event.key) + ';';
}

struct CancelableCodec
    : fsim::runtime::OwnedFieldsSchedulerEventCodec<CancelableEvent,
          EventCounter, EventDomain, &CancelableEvent::owner,
          &CancelableEvent::key> {
    static std::uint64_t cancel_key(const CancelableEvent& event)
    {
        return event.key;
    }
};

struct SignedEvent {
    EventCounter* owner { };
    std::int32_t delta { };
    SchedulerPhase phase { SchedulerPhase::active };
    bool flag { };
};

void signed_event(Scheduler&, const SignedEvent& event)
{
    event_log() += 's' + std::to_string(event.delta)
        + std::string(phase_name(event.phase)) + (event.flag ? "T" : "F")
        + ';';
}

struct SignedCodec
    : fsim::runtime::OwnedFieldsSchedulerEventCodec<SignedEvent, EventCounter,
          EventDomain, &SignedEvent::owner, &SignedEvent::delta,
          &SignedEvent::phase, &SignedEvent::flag> { };

struct ClassEvent {
    std::uint64_t id { };
    std::uint64_t spare { };
};
void derived_event(Scheduler&, const ClassEvent&) { event_log() += "d;"; }
void foreign_event(Scheduler&, const ClassEvent&) { event_log() += "f;"; }
void unregistered_event(Scheduler&, const PlainEvent&) { }
void host_event(Scheduler&, const ClassEvent&) { event_log() += "h;"; }

using PlainCodec = fsim::runtime::PlainSchedulerEventCodec<PlainEvent>;
using ChainCodec = fsim::runtime::PlainSchedulerEventCodec<ChainEvent>;
using ClassCodec = fsim::runtime::PlainSchedulerEventCodec<ClassEvent>;

// Registered before main, as the runtime's own kinds are.
[[maybe_unused]] const auto registered_plain
    = fsim::runtime::register_scheduler_event_kind<PlainEvent, plain_event,
        PlainCodec>("test.plain", SchedulerEventClass::data);
[[maybe_unused]] const auto registered_chain
    = fsim::runtime::register_scheduler_event_kind<ChainEvent, chain_event,
        ChainCodec>("test.chain", SchedulerEventClass::data);
[[maybe_unused]] const auto registered_owned
    = fsim::runtime::register_scheduler_event_kind<OwnedEvent, owned_event,
        OwnedCodec>("test.owned", SchedulerEventClass::data);
[[maybe_unused]] const auto registered_cancelable
    = fsim::runtime::register_scheduler_event_kind<CancelableEvent,
        cancelable_event, CancelableCodec>(
        "test.cancelable", SchedulerEventClass::data);
[[maybe_unused]] const auto registered_signed
    = fsim::runtime::register_scheduler_event_kind<SignedEvent, signed_event,
        SignedCodec>("test.signed", SchedulerEventClass::data);
[[maybe_unused]] const auto registered_derived
    = fsim::runtime::register_scheduler_event_kind<ClassEvent, derived_event,
        ClassCodec>("test.derived", SchedulerEventClass::derived);
[[maybe_unused]] const auto registered_foreign
    = fsim::runtime::register_scheduler_event_kind<ClassEvent, foreign_event,
        ClassCodec>("test.foreign", SchedulerEventClass::foreign_callback);
[[maybe_unused]] const auto registered_host
    = fsim::runtime::register_scheduler_event_kind<ClassEvent, host_event,
        ClassCodec>("test.host", SchedulerEventClass::host_closure);

[[nodiscard]] fsim::runtime::detail::SchedulerTaskDescriptor plain_descriptor(
    const std::uint64_t id)
{
    return fsim::runtime::detail::make_scheduler_task_descriptor<PlainEvent,
        plain_event>(PlainEvent { id, 0U });
}

[[nodiscard]] fsim::runtime::detail::SchedulerTaskDescriptor chain_descriptor(
    const std::uint64_t id, const std::uint64_t last)
{
    return fsim::runtime::detail::make_scheduler_task_descriptor<ChainEvent,
        chain_event>(ChainEvent { id, last });
}

[[nodiscard]] fsim::runtime::detail::SchedulerTaskDescriptor owned_descriptor(
    EventCounter* owner, const std::uint32_t amount)
{
    return fsim::runtime::detail::make_scheduler_task_descriptor<OwnedEvent,
        owned_event>(OwnedEvent { owner, amount });
}

[[nodiscard]] fsim::runtime::detail::SchedulerTaskDescriptor
cancelable_descriptor(EventCounter* owner, const std::uint64_t key)
{
    return fsim::runtime::detail::make_scheduler_task_descriptor<
        CancelableEvent, cancelable_event>(CancelableEvent { owner, key });
}

template <void (*Dispatch)(Scheduler&, const ClassEvent&)>
[[nodiscard]] fsim::runtime::detail::SchedulerTaskDescriptor class_descriptor()
{
    return fsim::runtime::detail::make_scheduler_task_descriptor<ClassEvent,
        Dispatch>(ClassEvent { 1U, 0U });
}

void build_mixed_schedule(Scheduler& scheduler)
{
    // Same time and order, so the insertion sequence decides; an earlier
    // order at the same time; every queue the saved state names.
    scheduler.schedule_internal_at(
        20U, SchedulerPhase::active, 2U, plain_descriptor(1U));
    scheduler.schedule_internal_at(
        20U, SchedulerPhase::active, 2U, plain_descriptor(2U));
    scheduler.schedule_internal_at(
        20U, SchedulerPhase::active, 1U, plain_descriptor(3U));
    scheduler.schedule_internal_at(
        10U, SchedulerPhase::update, 0U, plain_descriptor(4U));
    scheduler.schedule_internal_systemverilog_at(
        20U, SchedulerPhase::active, 5U, plain_descriptor(5U));
    scheduler.schedule_internal_at(
        20U, SchedulerPhase::inactive, 0U, plain_descriptor(6U));
    scheduler.schedule_end_of_time_slot(
        0U, Scheduler::Task { plain_descriptor(7U) });
    scheduler.schedule_internal_at(
        30U, SchedulerPhase::postponed, 0U, plain_descriptor(8U));
    // A closure that holds a registered descriptor is saved as that kind.
    scheduler.schedule_at(20U, SchedulerPhase::active, 9U,
        Scheduler::Task { plain_descriptor(9U) });
}

void test_event_registry_order_round_trip()
{
    SchedulerEventOwners owners;
    event_log().clear();
    Scheduler original;
    build_mixed_schedule(original);
    static_cast<void>(original.run());
    const auto expected = event_log();
    require(!expected.empty(), "the mixed schedule runs");

    event_log().clear();
    Scheduler source;
    build_mixed_schedule(source);
    const auto saved = source.save_future(owners);
    require(saved.ok && saved.state.events.size() == 9U,
        "every registered pending event is saved");
    require(saved.state.next_sequence == 9U,
        "next_sequence is saved");

    Scheduler restored;
    const auto result = restored.restore_future(saved.state, owners, {});
    require(result.ok && result.restored == 9U, "the saved events restore");
    static_cast<void>(restored.run());
    require(event_log() == expected,
        "a restored scheduler runs the events in the original order");

    // The byte form round-trips too.
    const auto bytes = fsim::runtime::serialize_scheduler_state(saved.state);
    SchedulerSavedState decoded;
    std::string error;
    require(fsim::runtime::deserialize_scheduler_state(bytes, decoded, error),
        "the scheduler state decodes");
    require(decoded.events.size() == saved.state.events.size()
            && decoded.next_sequence == saved.state.next_sequence,
        "the decoded state matches");
    event_log().clear();
    Scheduler from_bytes;
    require(from_bytes.restore_future(decoded, owners, {}).ok,
        "the decoded state restores");
    static_cast<void>(from_bytes.run());
    require(event_log() == expected, "the decoded state runs identically");

    auto truncated = bytes;
    truncated.pop_back();
    require(!fsim::runtime::deserialize_scheduler_state(
                truncated, decoded, error),
        "a truncated state is rejected");
    auto trailing = bytes;
    trailing.push_back(std::byte { 0 });
    require(!fsim::runtime::deserialize_scheduler_state(
                trailing, decoded, error),
        "a state with trailing bytes is rejected");
    auto wrong_magic = bytes;
    wrong_magic[0] = std::byte { 0 };
    require(!fsim::runtime::deserialize_scheduler_state(
                wrong_magic, decoded, error),
        "a state with the wrong magic is rejected");
}

void test_event_registry_resume_after_time_limit()
{
    SchedulerEventOwners owners;
    event_log().clear();
    Scheduler original;
    build_mixed_schedule(original);
    static_cast<void>(original.run(15U));
    require(event_log() == "p7;p4;", "the first slots ran");
    const auto head = event_log();
    static_cast<void>(original.run());
    const auto expected_tail = event_log().substr(head.size());

    event_log().clear();
    Scheduler source;
    build_mixed_schedule(source);
    static_cast<void>(source.run(15U));
    event_log().clear();
    const auto saved = source.save_future(owners);
    require(saved.ok && saved.state.now == 15U,
        "a time-limit stop between slots is a save point");
    require(saved.state.events.size() == 7U, "the ran events are not saved");

    Scheduler restored;
    require(restored.restore_future(saved.state, owners, {}).ok,
        "the state restores");
    require(restored.now() == 15U, "time is restored");
    static_cast<void>(restored.run());
    require(event_log() == expected_tail,
        "the restored scheduler continues where the original stopped");
}

void test_event_registry_owner_and_field_codecs()
{
    EventCounter counter;
    SchedulerEventOwners owners;
    owners.bind("test", &counter);
    event_log().clear();
    Scheduler source;
    source.schedule_internal_at(5U, SchedulerPhase::active, 0U,
        owned_descriptor(&counter, 7U));
    source.schedule_internal_at(5U, SchedulerPhase::active, 1U,
        fsim::runtime::detail::make_scheduler_task_descriptor<SignedEvent,
            signed_event>(SignedEvent { &counter, -3,
                SchedulerPhase::reactive, true }));
    const auto saved = source.save_future(owners);
    require(saved.ok, "owned and signed events save");

    // The owner of the restarted run is another object.
    EventCounter other;
    SchedulerEventOwners restart_owners;
    restart_owners.bind("test", &other);
    Scheduler restored;
    require(restored.restore_future(saved.state, restart_owners, {}).ok,
        "owned events restore against the new owner");
    static_cast<void>(restored.run());
    require(other.total == 7U && counter.total == 0U,
        "the payload pointer is re-bound to the restarted owner");
    require(event_log() == "o7;s-3reactiveT;",
        "signed, enum and bool fields round-trip");

    // A pointer outside the owner domain refuses the save.
    EventCounter stranger;
    Scheduler refused;
    refused.schedule_internal_at(5U, SchedulerPhase::active, 0U,
        owned_descriptor(&stranger, 1U));
    const auto stranger_save = refused.save_future(owners);
    require(!stranger_save.ok && stranger_save.refusals.size() == 1U
            && stranger_save.refusals.front().find("owner domain 'test'")
                != std::string::npos,
        "a payload pointer outside its owner domain refuses the save");

    // An unbound owner domain refuses the restore, and changes nothing.
    SchedulerEventOwners unbound;
    Scheduler target;
    target.schedule_internal_at(
        3U, SchedulerPhase::active, 0U, plain_descriptor(99U));
    const auto bad = target.restore_future(saved.state, unbound, {});
    require(!bad.ok && !bad.errors.empty(),
        "an unbound owner domain refuses the restore");
    event_log().clear();
    static_cast<void>(target.run());
    require(event_log() == "p99;", "a refused restore leaves the queue alone");
}

void test_event_registry_cancelable_rebind()
{
    EventCounter counter;
    SchedulerEventOwners owners;
    owners.bind("test", &counter);
    Scheduler source;
    auto cancelled = source.schedule_after_cancelable(10U,
        SchedulerPhase::active, 0U,
        Scheduler::Task { cancelable_descriptor(&counter, 1U) });
    auto kept = source.schedule_after_cancelable(10U, SchedulerPhase::active,
        1U, Scheduler::Task { cancelable_descriptor(&counter, 2U) });
    auto doomed = source.schedule_after_cancelable(10U,
        SchedulerPhase::active, 2U,
        Scheduler::Task { cancelable_descriptor(&counter, 3U) });
    static_cast<void>(kept);
    source.cancel(cancelled);
    const auto saved = source.save_future(owners);
    require(saved.ok && saved.state.events.size() == 2U,
        "an event cancelled before the save is not saved");
    require(saved.state.events[0].cancelable
            && saved.state.events[0].cancel_key == 2U
            && saved.state.events[1].cancel_key == 3U,
        "cancelable events save their cancel keys in order");
    static_cast<void>(doomed);

    std::vector<std::pair<std::uint64_t, ScheduledTaskHandle>> handles;
    Scheduler restored;
    const SchedulerCancelBinder binder = [&](std::string_view kind,
        std::uint64_t key, ScheduledTaskHandle handle) {
        require(kind == "test.cancelable", "the binder names the kind");
        handles.emplace_back(key, handle);
    };
    require(restored.restore_future(saved.state, owners, binder).ok,
        "cancelable events restore");
    require(handles.size() == 2U && static_cast<bool>(handles[0].second),
        "each restored cancelable event gets a live handle");
    restored.cancel(handles[1].second);
    require(!static_cast<bool>(handles[1].second),
        "the rebound handle cancels the restored event");
    event_log().clear();
    static_cast<void>(restored.run());
    require(event_log() == "k2;", "only the live restored event runs");
}

void test_event_registry_refusals_and_classes()
{
    SchedulerEventOwners owners;

    Scheduler closure;
    closure.schedule_at(4U, SchedulerPhase::active, 3U, [](Scheduler&) {});
    const auto closure_save = closure.save_future(owners);
    require(!closure_save.ok && closure_save.refusals.size() == 1U
            && closure_save.refusals.front().find("unregistered closure")
                != std::string::npos
            && closure_save.refusals.front().find("time 4")
                != std::string::npos
            && closure_save.refusals.front().find("order 3")
                != std::string::npos,
        "an unregistered closure refuses the save and says where it is");
    require(closure_save.state.events.empty(),
        "a refused save returns no partial state");

    Scheduler closure_cancelable;
    static_cast<void>(closure_cancelable.schedule_after_cancelable(
        4U, SchedulerPhase::active, 0U, [](Scheduler&) {}));
    require(!closure_cancelable.save_future(owners).ok,
        "a cancelable closure refuses the save");

    Scheduler host;
    host.schedule_internal_at(6U, SchedulerPhase::active, 0U,
        class_descriptor<host_event>());
    const auto host_save = host.save_future(owners);
    require(!host_save.ok && host_save.refusals.size() == 1U
            && host_save.refusals.front().find("test.host")
                != std::string::npos
            && host_save.refusals.front().find("host-closure")
                != std::string::npos,
        "a host-closure kind refuses the save and names the kind");

    Scheduler unnamed;
    unnamed.schedule_internal_at(1U, SchedulerPhase::active, 0U,
        fsim::runtime::detail::make_scheduler_task_descriptor<PlainEvent,
            unregistered_event>(PlainEvent { 1U, 0U }));
    require(!unnamed.save_future(owners).ok,
        "a descriptor of an unregistered kind refuses the save");

    Scheduler classes;
    classes.schedule_internal_at(6U, SchedulerPhase::active, 0U,
        class_descriptor<derived_event>());
    classes.schedule_internal_at(6U, SchedulerPhase::active, 1U,
        class_descriptor<foreign_event>());
    classes.schedule_internal_at(6U, SchedulerPhase::active, 2U,
        plain_descriptor(5U));
    const auto class_save = classes.save_future(owners);
    require(class_save.ok && class_save.derived == 1U
            && class_save.foreign_dropped == 1U
            && class_save.state.events.size() == 1U,
        "derived events are left to the owner and foreign callbacks are "
        "dropped and counted");

    // Never inside a callback of an ordinary slot.
    Scheduler running;
    bool inside_refused = false;
    running.schedule_at(1U, SchedulerPhase::active, 0U, [&](Scheduler& now) {
        inside_refused = !now.at_quiescent()
            && !now.save_future(owners).ok;
    });
    static_cast<void>(running.run());
    require(inside_refused, "a save from an ordinary callback is refused");

    // A restore needs registered data kinds with matching state.
    SchedulerSavedState unknown;
    unknown.events.push_back({ 0U, fsim::runtime::SchedulerSavedRegion::generic,
        SchedulerPhase::active, 0U, 0U, "no.such.kind", false, 0U, { } });
    Scheduler target;
    const auto unknown_restore = target.restore_future(unknown, owners, {});
    require(!unknown_restore.ok && !unknown_restore.errors.empty(),
        "an unknown kind name refuses the restore");
    SchedulerSavedState wrong_class;
    wrong_class.events.push_back({ 0U,
        fsim::runtime::SchedulerSavedRegion::generic, SchedulerPhase::active,
        0U, 0U, "test.host", false, 0U, { } });
    require(!target.restore_future(wrong_class, owners, {}).ok,
        "a host-closure kind never restores");
    SchedulerSavedState trailing;
    trailing.events.push_back({ 0U, fsim::runtime::SchedulerSavedRegion::generic,
        SchedulerPhase::active, 0U, 0U, "test.plain", false, 0U,
        std::vector<std::byte>(17U) });
    require(!target.restore_future(trailing, owners, {}).ok,
        "a payload with trailing bytes refuses the restore");
}

struct QuietRequest {
    SchedulerEventOwners* owners { };
    fsim::runtime::SchedulerSaveResult result;
    bool quiescent { };
    int calls { };
};

void save_at_quiet_point(void* context, Scheduler& scheduler)
{
    auto& request = *static_cast<QuietRequest*>(context);
    ++request.calls;
    request.quiescent = scheduler.at_quiescent();
    request.result = scheduler.save_future(*request.owners);
    scheduler.request_stop();
}

void test_event_registry_quiet_points()
{
    SchedulerEventOwners owners;

    // Slot end (IEEE 1800-2023 D.9): the first slot has drained, the second is
    // pending, and the scheduler stops there with nothing to repeat.
    event_log().clear();
    Scheduler slot;
    slot.schedule_internal_at(
        10U, SchedulerPhase::active, 0U, plain_descriptor(1U));
    slot.schedule_internal_at(
        10U, SchedulerPhase::postponed, 0U, plain_descriptor(2U));
    slot.schedule_internal_at(
        20U, SchedulerPhase::active, 0U, plain_descriptor(3U));
    QuietRequest slot_request { &owners, { }, false, 0 };
    slot.request_quiet_point(
        SchedulerQuietPoint::slot_end, &slot_request, save_at_quiet_point);
    const auto stopped = slot.run();
    require(stopped.status == fsim::runtime::RunStatus::stopped
            && slot_request.calls == 1 && slot_request.quiescent,
        "the slot-end request runs once, quiescent");
    require(slot_request.result.ok
            && slot_request.result.state.now == 10U
            && slot_request.result.state.events.size() == 1U
            && slot_request.result.state.events.front().time == 20U,
        "the slot-end save holds the later slot only");
    require(event_log() == "p1;p2;", "the drained slot ran first");
    slot.clear_stop();
    static_cast<void>(slot.run());
    require(slot_request.calls == 1, "a request fires once");

    Scheduler resumed;
    require(resumed.restore_future(slot_request.result.state, owners, {}).ok,
        "the slot-end state restores");
    event_log().clear();
    static_cast<void>(resumed.run());
    require(event_log() == "p3;", "the restored run continues at the next slot");

    // Cycle end (IEEE 1076-2019 20.7): the next delta cycle is pending.
    event_log().clear();
    Scheduler cycle;
    cycle.schedule_internal(
        SchedulerPhase::active, 0U, chain_descriptor(1U, 3U));
    QuietRequest cycle_request { &owners, { }, false, 0 };
    cycle.request_quiet_point(
        SchedulerQuietPoint::cycle_end, &cycle_request, save_at_quiet_point);
    const auto cycle_stop = cycle.run();
    require(cycle_stop.status == fsim::runtime::RunStatus::stopped
            && cycle_request.calls == 1 && cycle_request.quiescent,
        "the cycle-end request runs once, quiescent");
    require(event_log() == "c1;", "one cycle ran");
    require(cycle_request.result.ok
            && cycle_request.result.state.events.size() == 1U
            && cycle_request.result.state.events.front().time == 0U,
        "the cycle-end save holds the next delta cycle's event");
    Scheduler cycle_resumed;
    require(cycle_resumed.restore_future(
                cycle_request.result.state, owners, {}).ok,
        "the cycle-end state restores");
    static_cast<void>(cycle_resumed.run());
    require(event_log() == "c1;c2;c3;",
        "the restored run finishes the delta chain");

    // An idle scheduler with SystemVerilog work pending saves it.
    Scheduler mixed;
    mixed.schedule_internal_systemverilog_at(
        0U, SchedulerPhase::active, 0U, plain_descriptor(1U));
    require(mixed.at_quiescent(), "an idle scheduler is quiescent");
    const auto idle_save = mixed.save_future(owners);
    require(idle_save.ok && idle_save.state.events.size() == 1U,
        "an idle scheduler saves its pending SystemVerilog work");
}

// A closure of the embedder refuses the save, a foreign callback is dropped
// and counted, and a closure of the current slot makes the request wait
// (deferrable). FSIM-SAVE codes lead each refusal.
void late_derived_event(Scheduler&, const ClassEvent&)
{
    event_log() += "l;";
}

// A batch that declines every member, so each runs through its fallback.
class DecliningBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    [[nodiscard]] fsim::runtime::SchedulerBatchResult execute(
        Scheduler&, std::span<const std::uint64_t>) override
    {
        return { };
    }
};

void test_event_registry_closure_wrappers()
{
    SchedulerEventOwners owners;

    Scheduler host;
    static_cast<void>(host.schedule_after_cancelable(8U,
        SchedulerPhase::active, 0U,
        fsim::runtime::host_closure_task(
            "test hook", [](Scheduler&) { })));
    const auto host_save = host.save_future(owners);
    require(!host_save.ok && !host_save.deferrable
            && host_save.refusals.size() == 1U
            && host_save.refusals.front().find("FSIM-SAVE-0004")
                != std::string::npos
            && host_save.refusals.front().find("test hook")
                != std::string::npos,
        "an embedder closure in the future refuses with FSIM-SAVE-0004");

    Scheduler foreign;
    foreign.schedule_at(8U, SchedulerPhase::active, 0U,
        fsim::runtime::foreign_callback_task([](Scheduler&) { }));
    static_cast<void>(foreign.schedule_after_cancelable(9U,
        SchedulerPhase::active, 0U,
        fsim::runtime::foreign_callback_task([](Scheduler&) { })));
    foreign.schedule_internal_at(
        9U, SchedulerPhase::active, 1U, plain_descriptor(3U));
    const auto foreign_save = foreign.save_future(owners);
    require(foreign_save.ok && foreign_save.foreign_dropped == 2U
            && foreign_save.state.events.size() == 1U,
        "foreign callbacks, cancelable or not, are dropped and counted");

    Scheduler plain;
    plain.schedule_at(5U, SchedulerPhase::active, 0U, [](Scheduler&) { });
    const auto plain_save = plain.save_future(owners);
    require(!plain_save.ok && !plain_save.deferrable
            && plain_save.refusals.front().find("FSIM-SAVE-0002")
                != std::string::npos,
        "an unregistered closure in the future refuses with FSIM-SAVE-0002");

    // A closure of the current slot at a cycle end: the request waits.
    Scheduler cycle;
    cycle.schedule_at(0U, SchedulerPhase::active, 0U, [](Scheduler& now) {
        now.schedule_next_delta(
            SchedulerPhase::active, 1U, [](Scheduler&) { });
    });
    QuietRequest request { &owners, { }, false, 0 };
    cycle.request_quiet_point(
        SchedulerQuietPoint::cycle_end, &request, save_at_quiet_point);
    static_cast<void>(cycle.run());
    require(request.calls == 1 && !request.result.ok
            && request.result.deferrable
            && request.result.refusals.front().find("FSIM-SAVE-0002")
                != std::string::npos,
        "a closure of the current slot makes the save wait");

    // A save outside a save point also waits.
    Scheduler running;
    bool inside_waits = false;
    running.schedule_at(1U, SchedulerPhase::active, 0U, [&](Scheduler& now) {
        const auto result = now.save_future(owners);
        inside_waits = !result.ok && result.deferrable
            && result.refusals.front().find("FSIM-SAVE-0001")
                != std::string::npos;
    });
    static_cast<void>(running.run());
    require(inside_waits, "a save from a callback is refused with FSIM-SAVE-0001");
}

void test_event_registry_batch_and_derived()
{
    SchedulerEventOwners owners;

    // A batched next-delta member stands for its fallback descriptor: a
    // cycle-end save records the registered event (interp.cohort_wake does
    // this in the interpreter).
    event_log().clear();
    DecliningBatch batch;
    Scheduler cycle;
    cycle.schedule_at(0U, SchedulerPhase::active, 0U, [&](Scheduler& now) {
        now.schedule_next_delta_batchable(
            SchedulerPhase::active, 1U, batch, 77U, plain_descriptor(11U));
    });
    QuietRequest request { &owners, { }, false, 0 };
    cycle.request_quiet_point(
        SchedulerQuietPoint::cycle_end, &request, save_at_quiet_point);
    static_cast<void>(cycle.run());
    require(request.result.ok && request.result.state.events.size() == 1U
            && request.result.state.events.front().kind == "test.plain",
        "a batched next-delta member saves as its fallback kind");
    Scheduler restored;
    require(restored.restore_future(request.result.state, owners, {}).ok,
        "the batched member's state restores");
    static_cast<void>(restored.run());
    require(event_log() == "p11;",
        "the restored member runs as the plain event");
    cycle.clear_stop();
    event_log().clear();
    static_cast<void>(cycle.run());
    require(event_log() == "p11;",
        "the original batch member still runs through its fallback");

    // A derived kind registered at its first scheduling site.
    const auto late = class_descriptor<late_derived_event>();
    fsim::runtime::register_derived_scheduler_event_kind(
        "test.late_derived", late.invoke);
    const auto* const kind = fsim::runtime::find_scheduler_event_kind(late.invoke);
    require(kind != nullptr && kind->klass == SchedulerEventClass::derived,
        "a derived kind is found by its invoke pointer");
    Scheduler derived;
    derived.schedule_internal_at(4U, SchedulerPhase::active, 0U, late);
    derived.schedule_internal_at(
        4U, SchedulerPhase::active, 1U, plain_descriptor(12U));
    const auto derived_save = derived.save_future(owners);
    require(derived_save.ok && derived_save.derived == 1U
            && derived_save.state.events.size() == 1U,
        "a derived kind is counted and left out of the save");
    SchedulerSavedState derived_state;
    derived_state.events.push_back({ 0U,
        fsim::runtime::SchedulerSavedRegion::generic, SchedulerPhase::active,
        0U, 0U, "test.late_derived", false, 0U, { } });
    Scheduler refuse;
    require(!refuse.restore_future(derived_state, owners, {}).ok,
        "a derived kind never restores from a save");
}

void test_event_registry_audit()
{
    // Every registered data kind round-trips a payload: owner pointer first,
    // every other byte 1 (a valid bool, enum, integer). The re-encoded bytes
    // must equal the first encoding.
    int dummy_owner { };
    std::size_t audited { };
    const auto kinds = fsim::runtime::scheduler_event_kinds();
    for (const auto* const kind : kinds) {
        require(!kind->name.empty(), "a kind has a name");
        require(fsim::runtime::find_scheduler_event_kind(kind->name) == kind
                && fsim::runtime::find_scheduler_event_kind(kind->invoke)
                    == kind,
            "a kind is found by name and by invoke pointer");
        if (kind->klass != SchedulerEventClass::data) {
            continue;
        }
        SchedulerEventOwners owners;
        owners.bind(kind->owner_domain, &dummy_owner);
        std::array<std::byte, 32> payload;
        payload.fill(std::byte { 1 });
        if (!kind->owner_domain.empty()) {
            void* const pointer = &dummy_owner;
            std::memcpy(payload.data(), &pointer, sizeof pointer);
        }
        std::vector<std::byte> first;
        SchedulerEventWriter writer { first, owners, kind->owner_domain };
        require(kind->encode(payload.data(), writer), "a kind encodes");
        std::array<std::byte, 32> decoded { };
        SchedulerEventReader reader { first, owners, kind->owner_domain };
        require(kind->decode(reader, decoded.data()) && reader.exhausted(),
            "a kind decodes what it encoded");
        std::vector<std::byte> second;
        SchedulerEventWriter again { second, owners, kind->owner_domain };
        require(kind->encode(decoded.data(), again) && first == second,
            "a kind re-encodes to the same bytes");
        ++audited;
    }
    require(audited >= 8U, "the test kinds are audited");

    // The runtime registers its kinds before main (interpreter batches A-E,
    // VPI, VHPI and UVM).
    for (const char* const name : {
             "interp.wait_timeout", "interp.process_queue_compact",
             "interp.process_queue_timed", "interp.process_timeout_update",
             "interp.process_wake", "interp.process_run_current",
             "interp.process_region_resume", "interp.process_wake_domain_cross",
             "interp.event_notify_delta", "interp.event_notify_timed",
             "interp.update_commit", "interp.vhdl_next_cycle_resolutions",
             "interp.driver_disconnect", "interp.sv_update",
             "interp.delayed_update", "interp.monitor_publish_sv",
             "interp.monitor_publish_vhdl", "interp.vcd_begin",
             "interp.timing_delayed_copy", "interp.timing_check_deadline",
             "interp.foreign_net_delay_mature", "interp.inertial_mature",
             "interp.module_path_mature", "interp.module_path_pulse_x",
             "interp.net_delay_mature", "interp.projected_transaction_mature",
             "interp.projected_group_mature", "interp.charge_decay",
             "interp.cohort_wake", "vpi.scheduled_write", "vhpi.write",
             "vhpi.transaction", "uvm.objection_drain" }) {
        require(fsim::runtime::find_scheduler_event_kind(name) != nullptr,
            name);
    }
}

} // namespace

void test_scheduler_systemverilog()
{
    test_sv_composite_ticket_foreign_split();
    test_sv_readiness_tickets_interleave_without_crossing_foreign_writer();
    test_sv_readiness_ticket_ordering_and_lifecycle();
    test_sv_readiness_ticket_failure_and_large_group();
    test_sv_readiness_ticket_pool_growth_refuses_queued_storage();
    test_sv_composite_ticket_late_observation_decline();
    test_sv_composite_ticket_group_boundary();
    test_sv_composite_ticket_discard();
    test_sv_composite_ticket_throw_before_acceptance();
    test_sv_composite_ticket_cancel_during_direct_dispatch();
    test_sv_batch_bounded_fallback();
    test_sv_batch_partial_resume();
    test_frozen_rounds_and_nested_inactive();
    test_complete_nba_batch_before_active();
    test_generic_crossings_and_end_of_slot();
    test_runtime_slot_quiet_hook();
    test_observed_and_reactive_iterations();
    test_stop_resume_and_cancellation();
    test_limit_and_reset();
    test_failure_and_discard();
    test_single_forward_iteration_limit();
    test_descriptors_and_end_callback_mutation();
    test_event_registry_order_round_trip();
    test_event_registry_resume_after_time_limit();
    test_event_registry_owner_and_field_codecs();
    test_event_registry_cancelable_rebind();
    test_event_registry_refusals_and_classes();
    test_event_registry_quiet_points();
    test_event_registry_closure_wrappers();
    test_event_registry_batch_and_derived();
    test_event_registry_audit();
}

} // namespace fsim::tests::runtime

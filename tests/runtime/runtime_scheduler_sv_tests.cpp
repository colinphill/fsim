// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
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
    require(events == "abBcdef", "foreign postponed observations remain per-cycle");
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
}

} // namespace fsim::tests::runtime

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

struct OrderedTicketOwner {
    fsim::runtime::InternalSystemVerilogOrderedTicketStorage storage;
    int value { 23 };
};

struct OrderedTicketOwnerProbe {
    std::weak_ptr<OrderedTicketOwner>* owner { };
    std::shared_ptr<OrderedTicketOwner>* snapshot_owner { };
    int release_on_id { -1 };
    bool* read_live_storage { };
    std::vector<std::uint64_t>* event_rounds { };
};

struct OrderedTicketTask {
    std::vector<int>* events { };
    OrderedTicketOwnerProbe* owner_probe { };
    int id { };
    int stop_id { };
    int throw_id { };
    std::uint8_t late_insert_order { };
};

void dispatch_ordered_ticket_task(
    Scheduler& scheduler, const OrderedTicketTask& payload)
{
    payload.events->push_back(payload.id);
    auto* const event_rounds = payload.owner_probe == nullptr
        ? nullptr : payload.owner_probe->event_rounds;
    if (event_rounds != nullptr)
        event_rounds->push_back(scheduler.systemverilog_round());
    if (payload.owner_probe != nullptr) {
        if (payload.id == payload.owner_probe->release_on_id)
            payload.owner_probe->snapshot_owner->reset();
        auto owner = payload.owner_probe->owner->lock();
        *payload.owner_probe->read_live_storage
            = owner && owner->value == 23 && owner->storage.in_use();
    }
    if (payload.late_insert_order != 0U) {
        const auto late_id
            = static_cast<int>(payload.late_insert_order);
        scheduler.schedule_systemverilog(SchedulerPhase::active,
            payload.late_insert_order,
            [events = payload.events, event_rounds, late_id](Scheduler& current) {
                events->push_back(late_id);
                if (event_rounds != nullptr)
                    event_rounds->push_back(
                        current.systemverilog_round());
            });
    }
    if (payload.id == payload.throw_id)
        throw std::runtime_error("ordered ticket member failure");
    if (payload.id == payload.stop_id)
        scheduler.request_stop();
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

struct ReservationProbe {
    std::array<int, 3U> order { };
    std::size_t count { };
    std::uint64_t next_sequence_after_commit { };
    std::uint64_t next_sequence_after_stale_commit { };
    bool committed { };
    Scheduler::InternalSystemVerilogBatchReservation escaped_ticket;
};

struct ReservationPayload {
    ReservationProbe* probe { };
};

void record_reserved_descriptor(
    Scheduler&, const ReservationPayload& payload)
{
    payload.probe->order[payload.probe->count++] = 3;
}

class ReservationBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    explicit ReservationBatch(ReservationProbe& probe) noexcept
        : probe_(probe)
    {
    }

    [[nodiscard]] fsim::runtime::SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t>) override
    {
        const auto frontier = scheduler.current_batch_frontier();
        require(frontier.has_value(),
            "reservation witness runs inside a scheduler frontier");
        {
            auto canceled
                = scheduler.reserve_internal_systemverilog_batch_from_frontier(
                    frontier->generation, 1U);
            require(static_cast<bool>(canceled),
                "the invisible capacity ticket reserves its queue entry");
            canceled.cancel();
            require(!static_cast<bool>(canceled),
                "cancel releases its ticket without making work visible");
        }

        scheduler.schedule_systemverilog(
            SchedulerPhase::active, 7U, [this](Scheduler& current) {
                (void)current;
                probe_.order[probe_.count++] = 1;
            });

        auto reservation
            = scheduler.reserve_internal_systemverilog_batch_from_frontier(
                frontier->generation, 1U);
        require(static_cast<bool>(reservation),
            "the live ticket reserves a frozen-prefix insertion");
        scheduler.schedule_systemverilog(
            SchedulerPhase::active, 7U, [this](Scheduler& current) {
                (void)current;
                probe_.order[probe_.count++] = 2;
            });
        const auto descriptor
            = fsim::runtime::detail::make_scheduler_task_descriptor<
                ReservationPayload, &record_reserved_descriptor>(
                    { &probe_ });
        const std::array<fsim::runtime::StableOrder, 1U> stable_orders { 7U };
        const std::array<fsim::runtime::detail::SchedulerTaskDescriptor, 1U>
            tasks { descriptor };
        probe_.committed = reservation.commit(stable_orders, tasks);
        require(probe_.committed,
            "the prepared entry commits atomically after nested work");
        probe_.next_sequence_after_commit
            = scheduler.reserve_order_key(9U).sequence;
        auto escaped
            = scheduler.reserve_internal_systemverilog_batch_from_frontier(
                frontier->generation, 1U);
        require(static_cast<bool>(escaped),
            "a second ticket can reserve capacity after the first commit");
        probe_.escaped_ticket = std::move(escaped);
        return { 1U, { } };
    }

private:
    ReservationProbe& probe_;
};

void test_native_batch_capacity_ticket()
{
    Scheduler scheduler;
    ReservationProbe probe;
    ReservationBatch batch { probe };
    require(!scheduler.trace_hook_installed(),
        "counter-only ticket probe does not install scheduler tracing");
    scheduler.schedule_systemverilog_batchable(
        SchedulerPhase::active, 1U, batch, 0U,
        [](Scheduler&) { throw std::runtime_error {
            "the accepted scheduler reservation fixture fell back" }; });
    require(scheduler.run().status == RunStatus::completed,
        "the ticketed wave and nested tasks complete");
    const auto descriptor
        = fsim::runtime::detail::make_scheduler_task_descriptor<
            ReservationPayload, &record_reserved_descriptor>({ &probe });
    const std::array<fsim::runtime::StableOrder, 1U> stable_orders { 7U };
    const std::array<fsim::runtime::detail::SchedulerTaskDescriptor, 1U>
        tasks { descriptor };
    require(!probe.escaped_ticket.commit(stable_orders, tasks),
        "frontier teardown invalidates a reservation that escaped its callback");
    probe.next_sequence_after_stale_commit
        = scheduler.reserve_order_key(10U).sequence;
    require(probe.committed
            && probe.next_sequence_after_commit == 4U
            && probe.next_sequence_after_stale_commit == 5U
            && probe.order == std::array<int, 3U> { 1, 2, 3 },
        "ticket cancellation, stale commit, and nested inserts preserve order and IDs");
}

using fsim::runtime::SchedulerBatchResult;
using fsim::runtime::SchedulerBatchFrontierEntry;
using fsim::runtime::SchedulerBatchTask;
using fsim::runtime::SchedulerBatchGroupKey;
using fsim::runtime::SchedulerTraceKind;
using fsim::runtime::SchedulerTraceRecord;
using fsim::runtime::StableOrder;
namespace detail = fsim::runtime::detail;

struct GenericFrontierTaskPayload {
    std::string* events { };
    char value { };
    std::vector<std::uint64_t>* deltas { };
};

void append_generic_frontier_task(
    Scheduler& scheduler, const GenericFrontierTaskPayload& payload)
{
    payload.events->push_back(payload.value);
    if (payload.deltas) {
        payload.deltas->push_back(scheduler.delta());
    }
    if (payload.value == 'c') {
        auto* const events = payload.events;
        auto* const deltas = payload.deltas;
        scheduler.schedule_next_delta(SchedulerPhase::active, 5U,
            [events, deltas](Scheduler& current) {
                if (current.delta() == 2U
                    && current.systemverilog_round() == 0U) {
                    events->push_back('d');
                    if (deltas) {
                        deltas->push_back(current.delta());
                    }
                }
            });
    }
}

struct GenericFrontierProbe {
    std::string events;
    std::vector<std::uint64_t> event_deltas;
    bool frontier_valid { };
    bool reservation_committed { };
    Scheduler::InternalGenericUpdateBatchReservation escaped_ticket;
};

class GenericFrontierBatch final : public SchedulerBatchTask {
public:
    explicit GenericFrontierBatch(GenericFrontierProbe& probe)
        : probe_(probe)
    {
    }

    SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        const auto frontier = scheduler.current_generic_batch_frontier();
        probe_.frontier_valid = frontier
            && frontier->generation != 0U
            && frontier->phase == SchedulerPhase::active
            && frontier->time == scheduler.now()
            && frontier->delta == scheduler.delta()
            && frontier->delta == 1U
            && frontier->cursor == 0U
            && frontier->end == payloads.size()
            && frontier->tasks.size() == payloads.size()
            && scheduler.current_phase() == SchedulerPhase::active
            && scheduler.current_batch_frontier() == std::nullopt
            && scheduler.systemverilog_round() == 0U;
        if (!probe_.frontier_valid || payloads.size() != 2U) {
            return { };
        }
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            const auto& entry = frontier->tasks[index];
            if (entry.payload != payloads[index]
                || entry.stable_order != index + 1U
                || (index != 0U
                    && entry.sequence <= frontier->tasks[index - 1U].sequence)) {
                probe_.frontier_valid = false;
                return { };
            }
        }

        auto reservation
            = scheduler.reserve_internal_generic_update_batch_from_frontier(
                frontier->generation, 2U);
        if (!reservation) {
            return { };
        }
        scheduler.schedule(SchedulerPhase::update, 3U,
            [this](Scheduler& current) {
                probe_.events.push_back('b');
                probe_.event_deltas.push_back(current.delta());
            });
        const std::array<StableOrder, 2U> orders { 3U, 4U };
        const std::array<detail::SchedulerTaskDescriptor, 2U> tasks {
            detail::make_scheduler_task_descriptor<
                GenericFrontierTaskPayload,
                &append_generic_frontier_task>({ &probe_.events, 'a',
                    &probe_.event_deltas }),
            detail::make_scheduler_task_descriptor<
                GenericFrontierTaskPayload,
                &append_generic_frontier_task>({ &probe_.events, 'c',
                    &probe_.event_deltas }),
        };
        probe_.reservation_committed
            = reservation.commit(orders, tasks);
        return { payloads.size(), { } };
    }

private:
    GenericFrontierProbe& probe_;
};

class GenericEscapedTicketBatch final : public SchedulerBatchTask {
public:
    explicit GenericEscapedTicketBatch(GenericFrontierProbe& probe)
        : probe_(probe)
    {
    }

    SchedulerBatchResult execute(
        Scheduler& scheduler, std::span<const std::uint64_t> payloads) override
    {
        const auto frontier = scheduler.current_generic_batch_frontier();
        if (!frontier) {
            return { };
        }
        probe_.escaped_ticket
            = scheduler.reserve_internal_generic_update_batch_from_frontier(
                frontier->generation, 1U);
        return { payloads.size(), { } };
    }

private:
    GenericFrontierProbe& probe_;
};

void test_generic_frontier_projected_update_ticket()
{
    Scheduler scheduler;
    GenericFrontierProbe probe;
    GenericFrontierBatch batch { probe };
    scheduler.schedule(SchedulerPhase::active, 0U,
        [&batch](Scheduler& current) {
            for (std::uint64_t payload = 1U; payload <= 2U; ++payload) {
                current.schedule_next_delta_batchable(
                    SchedulerPhase::active, payload, batch, payload,
                    [](Scheduler&) { });
            }
        });
    const auto result = scheduler.run();
    require(result.status == RunStatus::completed
            && probe.frontier_valid
            && probe.reservation_committed,
        "generic Active batches expose a separate generic-delta frontier and reserve Update callbacks");
    // A completed scheduler resets its delta after draining the slot. The
    // callback samples retain the exact generic-delta execution boundary.
    require(probe.events == "bacd"
            && probe.event_deltas
                == std::vector<std::uint64_t> { 1U, 1U, 1U, 2U },
        "generic Update tickets retain stable order, nested insertion sequence, and next-delta boundaries");
}

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

class GenericCancelledReservationBatch final : public SchedulerBatchTask {
public:
    SchedulerBatchResult execute(
        Scheduler& scheduler,
        std::span<const std::uint64_t> payloads) override
    {
        const auto frontier = scheduler.current_generic_batch_frontier();
        if (!frontier) {
            return { };
        }
        {
            auto reservation
                = scheduler.reserve_internal_generic_update_batch_from_frontier(
                    frontier->generation, 1U);
            require(static_cast<bool>(reservation),
                "generic Update ticket reserves from its exact Active frontier");
        }
        auto reservation
            = scheduler.reserve_internal_generic_update_batch_from_frontier(
                frontier->generation, 1U);
        require(static_cast<bool>(reservation),
            "a canceled generic Update reservation releases its queue capacity");
        auto& trace = *static_cast<GenericReservationTrace*>(trace_);
        scheduler.set_trace_hook(trace_, GenericCancelledReservationBatch::record);
        const std::array<StableOrder, 1U> orders { 8U };
        const std::array<detail::SchedulerTaskDescriptor, 1U> tasks {
            detail::make_scheduler_task_descriptor<
                GenericFrontierTaskPayload,
                &append_generic_frontier_task>({ &trace.events, 'x', nullptr })
        };
        commit_declined_ = !reservation.commit(orders, tasks);
        scheduler.schedule(SchedulerPhase::update, 9U,
            [](Scheduler&) { });
        return { payloads.size(), { } };
    }

    void attach(GenericReservationTrace& trace) noexcept { trace_ = &trace; }
    [[nodiscard]] bool commit_declined() const noexcept
    {
        return commit_declined_;
    }

private:
    static void record(void* context, const SchedulerTraceRecord& record) noexcept
    {
        GenericReservationTrace::receive(context, record);
    }

    void* trace_ { };
    bool commit_declined_ { };
};

void test_generic_frontier_ticket_cancel_and_expiry()
{
    Scheduler scheduler;
    GenericReservationTrace trace;
    GenericCancelledReservationBatch batch;
    batch.attach(trace);
    scheduler.schedule(SchedulerPhase::active, 0U,
        [&batch](Scheduler& current) {
            current.schedule_next_delta_batchable(SchedulerPhase::active,
                1U, batch, 1U, [](Scheduler&) { });
        });
    const auto result = scheduler.run();
    require(result.status == RunStatus::completed && batch.commit_declined()
            && trace.sequence == 2U && trace.events.empty(),
        "canceled and trace-rejected generic tickets expose no work or insertion sequence");

    Scheduler expired_scheduler;
    GenericFrontierProbe expired_probe;
    GenericEscapedTicketBatch expired_batch { expired_probe };
    expired_scheduler.schedule(SchedulerPhase::active, 0U,
        [&expired_batch](Scheduler& current) {
            current.schedule_next_delta_batchable(SchedulerPhase::active,
                1U, expired_batch, 1U, [](Scheduler&) { });
        });
    require(expired_scheduler.run().status == RunStatus::completed
            && static_cast<bool>(expired_probe.escaped_ticket),
        "generic Update reservation is available during the matching Active callback");
    const std::array<StableOrder, 1U> orders { 1U };
    const std::array<detail::SchedulerTaskDescriptor, 1U> tasks {
        detail::make_scheduler_task_descriptor<
            GenericFrontierTaskPayload,
            &append_generic_frontier_task>(
            { &expired_probe.events, 'x', nullptr })
    };
    require(!expired_probe.escaped_ticket.commit(orders, tasks)
            && expired_scheduler.run().status == RunStatus::completed
            && expired_probe.events.empty(),
        "a generic Update ticket expires with its Active frontier without publishing work");
}

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

class GenericChunkBatch final : public fsim::runtime::SchedulerBatchTask {
public:
    GenericChunkBatch(std::vector<std::uint64_t>& execution_order,
        std::vector<std::vector<std::uint64_t>>& offers,
        std::vector<std::uint64_t>& sequences)
        : execution_order_(execution_order)
        , offers_(offers)
        , sequences_(sequences)
    {
    }

    fsim::runtime::SchedulerBatchResult execute(Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        constexpr std::size_t expected_chunk_capacity = 64U;
        const auto frontier = scheduler.current_generic_batch_frontier();
        require(frontier && frontier->phase == SchedulerPhase::active
                && frontier->cursor == 0U
                && frontier->end == payloads.size()
                && frontier->tasks.size() == payloads.size()
                && payloads.size() <= expected_chunk_capacity,
            "generic groups expose their exact bounded Active prefix");

        std::vector<std::uint64_t> offer;
        offer.reserve(payloads.size());
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            const auto& key = frontier->tasks[index];
            require(key.payload == payloads[index]
                    && key.stable_order == payloads[index],
                "generic chunk keys retain their original payload identity");
            if (index != 0U) {
                require(frontier->tasks[index - 1U].sequence < key.sequence,
                    "generic chunks preserve insertion sequence order");
            }
            sequences_.push_back(key.sequence);
            offer.push_back(payloads[index]);
            execution_order_.push_back(payloads[index]);
        }
        offers_.push_back(std::move(offer));

        if (!foreign_inserted_) {
            foreign_inserted_ = true;
            scheduler.schedule(SchedulerPhase::active, 64U,
                [this](Scheduler&) { execution_order_.push_back(0U); });
        }
        return { payloads.size(), { } };
    }

private:
    std::vector<std::uint64_t>& execution_order_;
    std::vector<std::vector<std::uint64_t>>& offers_;
    std::vector<std::uint64_t>& sequences_;
    bool foreign_inserted_ { };
};

void test_generic_batch_chunks_preserve_foreign_order()
{
    Scheduler scheduler;
    std::vector<std::uint64_t> execution_order;
    std::vector<std::vector<std::uint64_t>> offers;
    std::vector<std::uint64_t> sequences;
    std::size_t fallback_calls { };
    GenericChunkBatch batch { execution_order, offers, sequences };

    for (std::uint64_t payload = 1U; payload <= 130U; ++payload) {
        scheduler.schedule_next_delta_batchable(SchedulerPhase::active,
            payload, batch, payload,
            [&fallback_calls](Scheduler&) { ++fallback_calls; });
    }

    require(scheduler.run().status == RunStatus::completed,
        "large generic groups complete through bounded leading prefixes");
    require(offers.size() == 3U
            && offers[0U].size() == 64U
            && offers[1U].size() == 64U
            && offers[2U] == std::vector<std::uint64_t> { 129U, 130U },
        "130 generic tasks are offered as exact 64/64/2 prefixes");
    require(sequences.size() == 130U,
        "every original generic key is observed exactly once");
    for (std::size_t index = 1U; index < sequences.size(); ++index) {
        require(sequences[index - 1U] < sequences[index],
            "original generic key sequence remains strictly increasing");
    }

    std::vector<std::uint64_t> expected;
    expected.reserve(131U);
    for (std::uint64_t payload = 1U; payload <= 64U; ++payload)
        expected.push_back(payload);
    expected.push_back(0U);
    for (std::uint64_t payload = 65U; payload <= 130U; ++payload)
        expected.push_back(payload);
    require(execution_order == expected && fallback_calls == 0U,
        "a same-phase order-64 callback precedes the retained order-65 suffix");
}

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

void test_sv_systemverilog_key_receipts()
{
    using fsim::runtime::SchedulerSystemVerilogKeyReceipt;
    const auto valid_receipt = [](const SchedulerSystemVerilogKeyReceipt& receipt,
                                  const SimulationTick time,
                                  const std::uint64_t delta,
                                  const std::uint64_t round,
                                  const SchedulerPhase phase,
                                  const std::uint64_t order,
                                  const std::uint64_t sequence) {
        return receipt.valid && receipt.time == time
            && receipt.delta == delta
            && receipt.systemverilog_round == round
            && receipt.phase == phase && receipt.stable_order == order
            && receipt.sequence == sequence;
    };

    {
        Scheduler scheduler;
        std::array<SchedulerSystemVerilogKeyReceipt, 3U> receipts;
        WaveBatch batch([&](Scheduler& current,
                            const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->time == 0U
                    && frontier->delta == 0U
                    && frontier->tasks.size() == payloads.size(),
                "ordinary receipt test exposes its actual scheduler keys");
            for (std::size_t index = 0U; index < payloads.size(); ++index) {
                const auto& task = frontier->tasks[index];
                const auto expected_phase = payloads[index] == 403U
                    ? SchedulerPhase::update : SchedulerPhase::active;
                const auto& receipt = payloads[index] == 401U
                    ? receipts[0U] : payloads[index] == 402U
                    ? receipts[1U] : receipts[2U];
                require(frontier->phase == expected_phase
                        && valid_receipt(receipt, 0U, 0U, 1U,
                            expected_phase, task.stable_order,
                            task.sequence)
                        && task.payload == payloads[index],
                    "ordinary and fallback receipts match the consumed key");
            }
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });

        receipts[0U].valid = true;
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, 42U, batch, 401U,
            [](Scheduler&) { }, { 17U, 1U }, &receipts[0U]);
        const bool grouped_before_slot
            = scheduler.schedule_systemverilog_readiness_member(
                SchedulerPhase::active, 30U, batch, 402U,
                [](Scheduler&) { }, { 17U, 2U }, &receipts[1U]);
        const bool grouped_update
            = scheduler.schedule_systemverilog_readiness_member(
                SchedulerPhase::update, 5U, batch, 403U,
                [](Scheduler&) { }, { 17U, 3U }, &receipts[2U]);
        require(grouped_before_slot && !grouped_update
                && valid_receipt(receipts[0U], 0U, 0U, 1U,
                    SchedulerPhase::active, 42U, 0U)
                && valid_receipt(receipts[1U], 0U, 0U, 1U,
                    SchedulerPhase::active, 30U, 1U)
                && valid_receipt(receipts[2U], 0U, 0U, 1U,
                    SchedulerPhase::update, 5U, 2U),
            "pre-slot readiness tickets and ordinary fallbacks return exact first-round keys");
        require(scheduler.run().status == RunStatus::completed,
            "ordinary receipt tasks complete");
    }

    {
        SchedulerKeyReceiptTraceCapture trace;
        Scheduler scheduler;
        scheduler.set_trace_hook(&trace,
            SchedulerKeyReceiptTraceCapture::receive);
        std::array<SchedulerSystemVerilogKeyReceipt, 2U> receipts;
        WaveBatch batch([](Scheduler&,
                           const std::span<const std::uint64_t>) {
            return fsim::runtime::SchedulerBatchResult { };
        });
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, 12U, batch, 501U,
            [](Scheduler&) { }, { 18U, 1U }, &receipts[0U]);
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::update, 13U, batch, 502U,
            [](Scheduler&) { }, { 18U, 2U }, &receipts[1U]);
        require(valid_receipt(receipts[0U], 0U, 0U, 1U,
                    SchedulerPhase::active, 12U, 0U)
                && valid_receipt(receipts[1U], 0U, 0U, 1U,
                    SchedulerPhase::update, 13U, 1U)
                && scheduler.run().status == RunStatus::completed
                && trace.count == 2U,
            "pre-slot Active and Update receipts match traced fallback tasks");
        for (std::size_t index = 0U; index < trace.count; ++index) {
            const auto& actual = trace.task_begins[index];
            const auto& expected = receipts[index];
            require(actual.time == expected.time
                    && actual.delta == expected.delta
                    && actual.systemverilog_round
                        == expected.systemverilog_round
                    && actual.phase == expected.phase
                    && actual.order == expected.stable_order
                    && actual.sequence == expected.sequence,
                "receipt fields match the scheduler trace key");
        }
    }

    {
        SchedulerKeyReceiptTraceCapture trace;
        Scheduler scheduler;
        scheduler.set_trace_hook(&trace,
            SchedulerKeyReceiptTraceCapture::receive);
        std::array<SchedulerSystemVerilogKeyReceipt, 2U> receipts;
        std::array<bool, 2U> ticketed { true, true };
        std::array<std::uint32_t, 2U> fallback_calls { };
        WaveBatch batch([](Scheduler&,
                           const std::span<const std::uint64_t>) {
            return fsim::runtime::SchedulerBatchResult { };
        });

        scheduler.schedule_systemverilog(SchedulerPhase::reactive, 0U,
            [&](Scheduler& current) {
                ticketed[0U]
                    = current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::inactive, 30U, batch, 601U,
                        [&](Scheduler&) { ++fallback_calls[0U]; },
                        { 20U, 1U }, &receipts[0U]);
                ticketed[1U]
                    = current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::update, 40U, batch, 602U,
                        [&](Scheduler&) { ++fallback_calls[1U]; },
                        { 20U, 2U }, &receipts[1U]);
                require(!ticketed[0U] && !ticketed[1U]
                        && valid_receipt(receipts[0U], 0U, 0U, 1U,
                            SchedulerPhase::re_inactive, 30U, 1U)
                        && valid_receipt(receipts[1U], 0U, 0U, 1U,
                            SchedulerPhase::re_update, 40U, 2U),
                    "Reactive ordinary fallbacks report normalized Re-Inactive and Re-Update keys");
            });

        require(scheduler.run().status == RunStatus::completed
                && trace.count == 3U
                && fallback_calls[0U] == 1U
                && fallback_calls[1U] == 1U
                && trace.task_begins[0U].phase == SchedulerPhase::reactive
                && trace.task_begins[1U].phase
                    == SchedulerPhase::re_inactive
                && trace.task_begins[2U].phase
                    == SchedulerPhase::re_update,
            "Reactive normalization receipts match the scheduler's executed phases");
        for (std::size_t index = 0U; index < receipts.size(); ++index) {
            const auto& actual = trace.task_begins[index + 1U];
            const auto& expected = receipts[index];
            require(actual.time == expected.time
                    && actual.delta == expected.delta
                    && actual.systemverilog_round
                        == expected.systemverilog_round
                    && actual.phase == expected.phase
                    && actual.order == expected.stable_order
                    && actual.sequence == expected.sequence,
                "normalized receipts exactly match scheduler trace keys");
        }
    }

    {
        Scheduler scheduler;
        constexpr fsim::runtime::SchedulerBatchGroupKey group_key {
            701U, 19U };
        std::array<SchedulerSystemVerilogKeyReceipt, 65U> receipts;
        SchedulerSystemVerilogKeyReceipt update_receipt;
        bool final_member_used_ticket { false };
        WaveBatch batch([&](Scheduler& current,
                            const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->tasks.size() == payloads.size(),
                "readiness receipts align with the scheduler frontier");
            for (std::size_t index = 0U; index < payloads.size(); ++index) {
                const auto payload = payloads[index];
                const bool is_update = payload == 2000U;
                const auto& receipt = is_update
                    ? update_receipt
                    : receipts[payload >= 101U && payload <= 165U
                          ? static_cast<std::size_t>(payload - 101U)
                          : 0U];
                const auto expected_round = is_update ? 1U : 2U;
                const auto expected_phase = is_update
                    ? SchedulerPhase::update : SchedulerPhase::active;
                require(frontier->phase == expected_phase
                        && valid_receipt(receipt, 0U, 0U, expected_round,
                            expected_phase, frontier->tasks[index].stable_order,
                            frontier->tasks[index].sequence)
                        && frontier->tasks[index].payload == payload,
                    "readiness receipts retain each original scheduler key");
            }
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });

        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&](Scheduler& current) {
                std::array<Scheduler::ReadinessBatchMember, 2U> initial;
                initial[0U].stable_order = 10U;
                initial[0U].payload = 101U;
                initial[0U].fallback_task = [](Scheduler&) { };
                initial[1U].stable_order = 20U;
                initial[1U].payload = 102U;
                initial[1U].fallback_task = [](Scheduler&) { };

                std::array<SchedulerSystemVerilogKeyReceipt, 1U> wrong_size;
                wrong_size[0U].valid = true;
                require(!current.schedule_systemverilog_readiness_group(
                            SchedulerPhase::active, batch, group_key,
                            initial, wrong_size)
                        && !wrong_size[0U].valid,
                    "a rejected group clears stale caller receipts");

                auto wrong_commit = current.reserve_systemverilog_group_batch(
                    SchedulerPhase::active, batch, group_key, 1U);
                Scheduler::ReadinessBatchMember one;
                one.stable_order = 5U;
                one.payload = 99U;
                one.fallback_task = [](Scheduler&) { };
                std::array<SchedulerSystemVerilogKeyReceipt, 2U>
                    wrong_commit_receipts;
                wrong_commit_receipts[0U].valid = true;
                wrong_commit_receipts[1U].valid = true;
                require(wrong_commit
                        && !wrong_commit.commit(
                            std::span<Scheduler::ReadinessBatchMember> {
                                &one, 1U }, wrong_commit_receipts)
                        && !wrong_commit_receipts[0U].valid
                        && !wrong_commit_receipts[1U].valid,
                    "a canceled reservation publishes no receipts");

                require(current.schedule_systemverilog_readiness_group(
                            SchedulerPhase::active, batch, group_key,
                            initial,
                            std::span<SchedulerSystemVerilogKeyReceipt> {
                                receipts.data(), 2U }),
                    "the initial readiness group commits atomically");

                Scheduler::ReadinessBatchMember appended;
                appended.stable_order = 30U;
                appended.payload = 103U;
                appended.fallback_task = [](Scheduler&) { };
                require(current.schedule_systemverilog_readiness_group(
                            SchedulerPhase::active, batch, group_key,
                            std::span<Scheduler::ReadinessBatchMember> {
                                &appended, 1U },
                            std::span<SchedulerSystemVerilogKeyReceipt> {
                                receipts.data() + 2U, 1U }),
                    "a reused readiness ticket appends receipts in input order");

                for (std::size_t index = 3U; index < 64U; ++index) {
                    const auto payload = 101U
                        + static_cast<std::uint64_t>(index);
                    require(current.schedule_systemverilog_readiness_member(
                                SchedulerPhase::active,
                                200U - static_cast<std::uint64_t>(index),
                                batch, payload, [](Scheduler&) { }, group_key,
                                &receipts[index]),
                        "readiness members append to the dynamic ticket");
                }
                final_member_used_ticket
                    = current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, 1000U, batch, 165U,
                        [](Scheduler&) { }, group_key, &receipts[64U]);
                const bool update_used_ticket
                    = current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::update, 5U, batch, 2000U,
                        [](Scheduler&) { }, group_key, &update_receipt);
                require(final_member_used_ticket && !update_used_ticket,
                    "the 65th Active member is ticketed while Update remains ordinary");
            });

        require(scheduler.run().status == RunStatus::completed
                && final_member_used_ticket,
            "readiness receipt reuse and 65-member completion");
        for (std::size_t index = 0U; index < receipts.size(); ++index) {
            const auto order = index < 3U
                ? 10U + static_cast<std::uint64_t>(index) * 10U
                : index < 64U
                ? 200U - static_cast<std::uint64_t>(index)
                : 1000U;
            require(valid_receipt(receipts[index], 0U, 0U, 2U,
                        SchedulerPhase::active, order,
                        static_cast<std::uint64_t>(index + 1U)),
                "reused and declined tickets preserve input-indexed issued keys");
        }
        require(valid_receipt(update_receipt, 0U, 0U, 1U,
                    SchedulerPhase::update, 5U, 66U),
            "ordinary non-Active fallback reports its exact key");
    }

    {
        Scheduler scheduler;
        CompactOrderedBatchProbe probe;
        std::array<SchedulerSystemVerilogKeyReceipt, 2U> receipts;
        std::array<std::uint64_t, 2U> issued_sequences;
        std::shared_ptr<CompactOrderedBatch> batch
            = std::make_shared<CompactOrderedBatch>(probe,
                [&](Scheduler& current,
                    const std::span<const std::uint64_t> payloads) {
                    const auto frontier = current.current_batch_frontier();
                    require(frontier && frontier->tasks.size() == 2U
                            && payloads.size() == 2U,
                        "compact receipt ticket exposes both issued keys");
                    for (std::size_t index = 0U; index < payloads.size(); ++index) {
                        require(valid_receipt(receipts[index], 0U, 0U, 2U,
                                    SchedulerPhase::active, 25U,
                                    frontier->tasks[index].sequence)
                                && frontier->tasks[index].stable_order
                                    == receipts[index].stable_order
                                && frontier->tasks[index].payload
                                    == payloads[index]
                                && receipts[index].sequence
                                    == index + 1U,
                            "compact commit receipts retain input-indexed sequences");
                    }
                    return fsim::runtime::SchedulerBatchResult {
                        payloads.size(), { } };
                });
        const std::shared_ptr<void> owner = batch;
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&](Scheduler& current) {
                const fsim::runtime::SchedulerBatchGroupKey group_key {
                    702U, 20U };
                auto reservation
                    = current.reserve_systemverilog_compact_group_batch(
                        SchedulerPhase::active, *batch, group_key, 2U);
                const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                    2U> members { { { 25U, 301U }, { 25U, 302U } } };
                require(reservation.commit_compact(members, *batch, owner,
                            issued_sequences, receipts),
                    "compact reservation publishes receipts with its key range");
            });
        require(scheduler.run().status == RunStatus::completed
                && valid_receipt(receipts[0U], 0U, 0U, 2U,
                    SchedulerPhase::active, 25U, 1U)
                && valid_receipt(receipts[1U], 0U, 0U, 2U,
                    SchedulerPhase::active, 25U, 2U)
                && issued_sequences == std::array<std::uint64_t, 2U> {
                    receipts[0U].sequence, receipts[1U].sequence },
            "compact receipt keys are captured at atomic commit");
    }

    {
        Scheduler scheduler;
        WaveBatch batch([](Scheduler&,
                           const std::span<const std::uint64_t> payloads) {
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        SchedulerSystemVerilogKeyReceipt receipt;
        receipt.valid = true;
        bool threw { };
        try {
            scheduler.schedule_systemverilog_group_batchable(
                static_cast<SchedulerPhase>(255U), 0U, batch, 1U,
                [](Scheduler&) { }, { }, &receipt);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw && !receipt.valid,
            "a throwing single-entry schedule clears a stale receipt");

        SchedulerSystemVerilogKeyReceipt queued_receipt;
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, 0U, batch, 1U,
            [](Scheduler&) { }, { }, &queued_receipt);
        require(queued_receipt.valid,
            "the discard fixture first publishes a real queued key");

        SchedulerDiscardReceiptProbe probe;
        SchedulerSystemVerilogKeyReceipt during_discard_receipt;
        during_discard_receipt.valid = true;
        probe.scheduler = &scheduler;
        probe.batch = &batch;
        probe.receipt = &during_discard_receipt;
        probe.fallback_task = [](Scheduler&) { };
        const auto discard_hook = scheduler.add_discard_hook(
            &probe, &SchedulerDiscardReceiptProbe::schedule_during_discard);
        scheduler.discard_pending();
        scheduler.remove_discard_hook(discard_hook);
        require(probe.attempted && !during_discard_receipt.valid
                && !scheduler.has_pending(),
            "a schedule attempted from inside discard cannot publish a key");

        SchedulerSystemVerilogKeyReceipt after_discard_receipt;
        after_discard_receipt.valid = true;
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, 0U, batch, 1U,
            [](Scheduler&) { }, { }, &after_discard_receipt);
        require(after_discard_receipt.valid && scheduler.has_pending(),
            "scheduling after discard returns normally and publishes its key");
        scheduler.discard_pending();
    }

    {
        Scheduler scheduler({ .max_delta_cycles = 1U });
        WaveBatch batch([](Scheduler&,
                           const std::span<const std::uint64_t> payloads) {
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        SchedulerSystemVerilogKeyReceipt receipt;
        receipt.valid = true;
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&](Scheduler& current) {
                current.schedule_systemverilog_group_batchable(
                    SchedulerPhase::active, 1U, batch, 700U,
                    [](Scheduler&) { }, { 20U, 1U }, &receipt);
                require(!receipt.valid,
                    "a next-round key past the configured limit is not issued");
            });
        bool limited { };
        try {
            static_cast<void>(scheduler.run());
        } catch (const fsim::runtime::DeltaCycleLimitError&) {
            limited = true;
        }
        require(limited && !receipt.valid,
            "max-round rejection leaves no stale or speculative receipt");
    }
}

void commit_compact_ordered_batch(Scheduler& scheduler,
    fsim::runtime::SchedulerOrderedBatchTask& batch,
    const std::shared_ptr<void>& owner,
    const std::span<const fsim::runtime::SystemVerilogCompactBatchMember> members)
{
    const auto frontier = scheduler.current_batch_frontier();
    require(frontier && frontier->phase == SchedulerPhase::active,
        "compact ticket reservation stays within its source Active callback");
    const fsim::runtime::SchedulerBatchGroupKey key {
        frontier->generation, 0x434f4d50414354U };
    auto reservation = scheduler.reserve_systemverilog_compact_group_batch(
        SchedulerPhase::active, batch, key, members.size());
    require(reservation && reservation.commit_compact(
                members, batch, owner),
        "compact ticket commits its key/payload vector atomically");
}

void test_sv_native_frontier_key_handoff()
{
    enum class Interruption { stop, failure };
    constexpr std::uint64_t internal_commit = (1ULL << 56U) | 1U;
    constexpr std::uint64_t member_activation = (2ULL << 56U) | 2U;
    constexpr std::uint64_t boundary_commit = (3ULL << 56U) | 3U;

    for (const auto interruption : { Interruption::stop,
             Interruption::failure }) {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        std::array<fsim::runtime::SchedulerBatchFrontierEntry, 2U>
            original_keys;
        bool original_keys_captured { };
        std::optional<std::uint64_t> target_round;
        std::size_t event_callbacks { };
        const auto expected_first_sequence = [&]() {
            require(original_keys_captured,
                "native output keys require a scheduler-issued source frontier");
            return original_keys.back().sequence + 1U;
        };

        WaveBatch activations([&](Scheduler& current,
                                  const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->phase == SchedulerPhase::active
                    && frontier->cursor == 0U && frontier->end == 2U
                    && frontier->tasks.size() == 2U
                    && payloads.size() == 2U
                    && payloads[0] == 1001U && payloads[1] == 1002U,
                "the original activation prefix has two exact scheduler keys");
            for (std::size_t index = 0U; index < payloads.size(); ++index) {
                require(frontier->tasks[index].stable_order == index + 1U
                        && frontier->tasks[index].payload == payloads[index],
                    "native activation inputs borrow their original keys and payloads");
                original_keys[index] = frontier->tasks[index];
            }
            require(original_keys[1].sequence
                    == original_keys[0].sequence + 1U,
                "the original activation keys retain insertion order");
            original_keys_captured = true;

            const fsim::runtime::SchedulerBatchGroupKey group_key {
                frontier->generation, 0x4e4154495645U };
            auto owner = std::static_pointer_cast<void>(batch);
            probe.owner = batch;
            require(!current.reserve_systemverilog_compact_group_batch(
                        SchedulerPhase::active, *batch, group_key, 65U),
                "an oversized native capacity reservation declines before generated output publication");
            auto empty_reservation
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, group_key, 4U);
            const auto empty_target_round
                = empty_reservation.target_systemverilog_round();
            require(empty_reservation
                    && empty_target_round && *empty_target_round == 2U
                    && empty_reservation.commit_compact(
                        std::span<const fsim::runtime::SystemVerilogCompactBatchMember> { },
                        *batch, owner)
                    && !empty_reservation.target_systemverilog_round(),
                "an empty native output batch exposes then releases its reserved round without a ticket");
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                2U> reversed { { { 20U, member_activation },
                    { 10U, internal_commit } } };
            auto rejected = current.reserve_systemverilog_compact_group_batch(
                SchedulerPhase::active, *batch, group_key, reversed.size());
            const auto rejected_target_round
                = rejected.target_systemverilog_round();
            require(rejected && rejected_target_round == empty_target_round
                    && !rejected.commit_compact(reversed, *batch, owner)
                    && !rejected.target_systemverilog_round(),
                "invalid native output order cancels its target round without publishing work");

            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                3U> members { { { 10U, internal_commit },
                    { 20U, member_activation },
                    { 30U, boundary_commit } } };
            std::array<std::uint64_t, 1U> wrong_output { 99U };
            auto wrong_output_reservation
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, group_key, 3U);
            const auto wrong_output_target_round
                = wrong_output_reservation.target_systemverilog_round();
            require(wrong_output_reservation
                    && wrong_output_target_round == empty_target_round
                    && !wrong_output_reservation.commit_compact(
                        members, *batch, owner, wrong_output)
                    && wrong_output[0U] == 99U
                    && !wrong_output_reservation.target_systemverilog_round(),
                "a mismatched issued-key output span cancels before publication");
            std::array<std::uint64_t, 3U> issued_sequences {
                99U, 99U, 99U };
            auto over_capacity
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, group_key, 2U);
            const auto over_capacity_target_round
                = over_capacity.target_systemverilog_round();
            require(over_capacity
                    && over_capacity_target_round == empty_target_round
                    && !over_capacity.commit_compact(
                        members, *batch, owner, issued_sequences)
                    && issued_sequences
                        == std::array<std::uint64_t, 3U> {
                            99U, 99U, 99U }
                    && !over_capacity.target_systemverilog_round(),
                "actual native writes exceeding capacity cancel without exposing keys");
            auto reservation
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, group_key, 5U);
            target_round = reservation.target_systemverilog_round();
            require(reservation && reservation.commit_compact(
                        members, *batch, owner, issued_sequences)
                    && target_round == empty_target_round
                    && !reservation.target_systemverilog_round()
                    && issued_sequences
                        == std::array<std::uint64_t, 3U> {
                            expected_first_sequence(),
                            expected_first_sequence() + 1U,
                            expected_first_sequence() + 2U },
                "one oversized reservation publishes only actual fresh native output keys");
            batch.reset();
            current.schedule_systemverilog(SchedulerPhase::active, 25U,
                [&probe](Scheduler& active) {
                    probe.events.push_back(25U);
                    probe.event_rounds.push_back(
                        active.systemverilog_round());
                });
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });

        batch = std::make_shared<CompactOrderedBatch>(probe,
            [&](Scheduler& current,
                const std::span<const std::uint64_t> payloads) {
                const auto frontier = current.current_batch_frontier();
                require(frontier && frontier->phase == SchedulerPhase::active
                        && frontier->cursor == 0U
                        && frontier->end == payloads.size()
                        && frontier->tasks.size() == payloads.size()
                        && target_round && *target_round == 2U
                        && frontier->systemverilog_round == *target_round,
                    "native internal events receive the scheduler-reserved frozen-round cut");
                for (std::size_t index = 0U; index < payloads.size(); ++index) {
                    require(frontier->tasks[index].payload == payloads[index],
                        "the generated event payload matches its scheduler-issued key");
                }
                ++event_callbacks;
                const auto first_sequence = expected_first_sequence();
                if (event_callbacks == 1U) {
                    require(payloads.size() == 2U
                            && payloads[0] == internal_commit
                            && payloads[1] == member_activation
                            && frontier->tasks[0].stable_order == 10U
                            && frontier->tasks[1].stable_order == 20U
                            && frontier->tasks[0].sequence == first_sequence
                            && frontier->tasks[1].sequence
                                == first_sequence + 1U,
                        "failed reservation consumes no sequence and foreign work cuts the new ticket");
                    probe.events.push_back(10U);
                    probe.event_rounds.push_back(
                        current.systemverilog_round());
                    if (interruption == Interruption::stop) {
                        current.request_stop();
                        return fsim::runtime::SchedulerBatchResult { 1U, { } };
                    }
                    return fsim::runtime::SchedulerBatchResult { 1U,
                        std::make_exception_ptr(std::runtime_error(
                            "native frontier handoff failure")) };
                }
                const auto expected_payload = event_callbacks == 2U
                    ? member_activation : boundary_commit;
                const auto expected_order = event_callbacks == 2U
                    ? 20U : 30U;
                const auto expected_sequence = first_sequence
                    + static_cast<std::uint64_t>(event_callbacks - 1U);
                require(event_callbacks <= 3U && payloads.size() == 1U
                        && payloads[0] == expected_payload
                        && frontier->tasks[0].stable_order == expected_order
                        && frontier->tasks[0].sequence == expected_sequence,
                    "partial execution retains each unconsumed exact-key suffix");
                probe.events.push_back(expected_order);
                probe.event_rounds.push_back(current.systemverilog_round());
                return fsim::runtime::SchedulerBatchResult { 1U, { } };
            });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            1U, activations, 1001U, [](Scheduler&) {
                throw std::runtime_error("original activation fell back");
            });
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            2U, activations, 1002U, [](Scheduler&) {
                throw std::runtime_error("original activation suffix fell back");
            });

        bool failed { };
        try {
            const auto result = scheduler.run();
            require(result.status == RunStatus::stopped
                    && interruption == Interruption::stop,
                "native stop preserves the partially consumed ticket");
        } catch (const std::runtime_error& error) {
            failed = std::string_view { error.what() }
                == "native frontier handoff failure";
            if (!failed)
                throw;
        }
        require(failed == (interruption == Interruption::failure)
                && probe.events == std::vector<std::uint64_t> { 10U }
                && !ticket_owner.expired(),
            "stop and failure consume only the first native event and retain the ticket");
        scheduler.clear_stop();
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> {
                    10U, 20U, 25U, 30U }
                && probe.event_rounds == std::vector<std::uint64_t> {
                    2U, 2U, 2U, 2U }
                && target_round && *target_round == 2U
                && event_callbacks == 3U
                && probe.fallback_descriptors == 0U
                && probe.owner_live_in_execute
                && ticket_owner.expired(),
            "fresh native keys preserve the partial suffix, foreign cut, and pinned lifetime");
    }
}

void test_sv_native_frontier_cross_owner_enqueue_order()
{
    CompactOrderedBatchProbe probe;
    std::shared_ptr<CompactOrderedBatch> batch;
    std::uint64_t original_sequence { };
    std::size_t event_callbacks { };
    WaveBatch activation([&](Scheduler& current,
                             const std::span<const std::uint64_t> payloads) {
        const auto frontier = current.current_batch_frontier();
        require(frontier && frontier->tasks.size() == 1U
                && payloads.size() == 1U
                && frontier->tasks[0].payload == payloads[0],
            "cross-owner native issue begins at one original activation key");
        original_sequence = frontier->tasks[0].sequence;

        current.schedule_systemverilog(SchedulerPhase::active, 20U,
            [&probe](Scheduler& active) {
                probe.events.push_back(200U);
                probe.event_rounds.push_back(active.systemverilog_round());
            });

        // Generated source order is owner 20, owner 10, owner 20. A stable
        // sort for ticket delivery changes only cross-owner sequence numbers;
        // the two equal-owner writes retain their source order. The atomic
        // reservation leaves no foreign insertion inside its sequence range.
        const std::array<fsim::runtime::SystemVerilogCompactBatchMember, 3U>
            source_order { { { 20U, 201U }, { 10U, 101U },
                { 20U, 202U } } };
        const std::array<fsim::runtime::SystemVerilogCompactBatchMember, 3U>
            delivery_order { { source_order[1], source_order[0],
                source_order[2] } };
        probe.owner = batch;
        commit_compact_ordered_batch(current, *batch,
            std::static_pointer_cast<void>(batch), delivery_order);
        batch.reset();

        current.schedule_systemverilog(SchedulerPhase::active, 20U,
            [&probe](Scheduler& active) {
                probe.events.push_back(210U);
                probe.event_rounds.push_back(active.systemverilog_round());
            });
        return fsim::runtime::SchedulerBatchResult { 1U, { } };
    });
    batch = std::make_shared<CompactOrderedBatch>(probe,
        [&](Scheduler& current,
            const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->tasks.size() == payloads.size(),
                "cross-owner native ticket carries an exact scheduler frontier");
            ++event_callbacks;
            if (event_callbacks == 1U) {
                require(payloads.size() == 1U && payloads[0] == 101U
                        && frontier->tasks[0].stable_order == 10U
                        && frontier->tasks[0].sequence
                            == original_sequence + 2U,
                    "the smaller owner dispatches before an older foreign tie");
            } else {
                require(event_callbacks == 2U && payloads.size() == 2U
                        && payloads[0] == 201U && payloads[1] == 202U
                        && frontier->tasks[0].stable_order == 20U
                        && frontier->tasks[1].stable_order == 20U
                        && frontier->tasks[0].sequence
                            == original_sequence + 3U
                        && frontier->tasks[1].sequence
                            == original_sequence + 4U,
                    "equal-owner native writes retain source order between foreign ties");
            }
            for (const auto payload : payloads) {
                probe.events.push_back(payload);
                probe.event_rounds.push_back(
                    current.systemverilog_round());
            }
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
    const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
    Scheduler scheduler;
    scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
        1U, activation, 1001U, [](Scheduler&) {
            throw std::runtime_error("cross-owner activation fell back");
        });
    require(scheduler.run().status == RunStatus::completed
            && probe.events == std::vector<std::uint64_t> {
                101U, 200U, 201U, 202U, 210U }
            && probe.event_rounds == std::vector<std::uint64_t> {
                2U, 2U, 2U, 2U, 2U }
            && event_callbacks == 2U
            && probe.fallback_descriptors == 0U
            && ticket_owner.expired(),
        "one atomic ticket preserves every cross-owner and same-owner foreign cut");
}

void test_compact_reservation_declines_at_delta_limit()
{
    Scheduler scheduler({ .max_delta_cycles = 2U });
    CompactOrderedBatchProbe compact_probe;
    CompactOrderedBatch compact_batch(compact_probe,
        [](Scheduler&, std::span<const std::uint64_t> payloads) {
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
    bool first_callback_ran { };
    bool second_callback_ran { };
    bool fallback_ran { };

    WaveBatch second([&](Scheduler& current,
                         const std::span<const std::uint64_t> payloads) {
        second_callback_ran = true;
        const auto frontier = current.current_batch_frontier();
        require(frontier && frontier->phase == SchedulerPhase::active
                && frontier->systemverilog_round == 2U
                && current.systemverilog_round() == 2U,
            "the last allowed Active round owns the compact reservation attempt");
        auto reservation = current.reserve_systemverilog_compact_group_batch(
            SchedulerPhase::active, compact_batch,
            { frontier->generation, 0x4d4158524f554e44U }, 1U);
        require(!reservation && !reservation.target_systemverilog_round(),
            "an over-limit compact reservation declines before creating a ticket or target round");

        current.schedule_systemverilog(SchedulerPhase::active, 73U,
            [&fallback_ran](Scheduler&) { fallback_ran = true; });
        return fsim::runtime::SchedulerBatchResult {
            payloads.size(), { } };
    });
    WaveBatch first([&](Scheduler& current,
                        const std::span<const std::uint64_t> payloads) {
        first_callback_ran = true;
        require(current.systemverilog_round() == 1U,
            "the seed runs before the configured Active round limit");
        current.schedule_systemverilog_batchable(SchedulerPhase::active,
            20U, second, 2U, [&fallback_ran](Scheduler&) {
                fallback_ran = true;
            });
        return fsim::runtime::SchedulerBatchResult {
            payloads.size(), { } };
    });
    scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
        10U, first, 1U, [&fallback_ran](Scheduler&) {
            fallback_ran = true;
        });

    bool limit_reported { };
    try {
        static_cast<void>(scheduler.run());
    } catch (const fsim::runtime::DeltaCycleLimitError& error) {
        limit_reported = error.time() == 0U && error.limit() == 2U
            && error.pending_orders()
                == std::vector<fsim::runtime::StableOrder> { 73U };
    }
    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(limit_reported && first_callback_ran && second_callback_ran
            && !fallback_ran && stats.tickets == 0U && stats.members == 0U
            && stats.entries_elided == 0U
            && compact_probe.execute_calls == 0U
            && compact_probe.fallback_descriptors == 0U,
        "the declined reservation leaves ordinary fallback work queued for the original delta-limit diagnostic");
}

void test_sv_compact_ordered_batch_ticket()
{
    {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        WaveBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            require(payloads.size() == 1U,
                "compact ticket seed has one original scheduler member");
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                4U> members { {
                    { 10U, 101U }, { 30U, 131U },
                    { 30U, 132U }, { 50U, 150U },
                } };
            probe.owner = batch;
            commit_compact_ordered_batch(current, *batch,
                std::static_pointer_cast<void>(batch), members);
            batch.reset();
            current.schedule_systemverilog(SchedulerPhase::active, 20U,
                [&probe](Scheduler& active) {
                    probe.events.push_back(200U);
                    probe.event_rounds.push_back(
                        active.systemverilog_round());
                });
            current.schedule_systemverilog(SchedulerPhase::active, 30U,
                [&probe](Scheduler& active) {
                    probe.events.push_back(300U);
                    probe.event_rounds.push_back(
                        active.systemverilog_round());
                });
            current.schedule_systemverilog(SchedulerPhase::active, 40U,
                [&probe](Scheduler& active) {
                    probe.events.push_back(400U);
                    probe.event_rounds.push_back(
                        active.systemverilog_round());
                });
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch = std::make_shared<CompactOrderedBatch>(probe,
            [&probe](Scheduler& current,
                     const std::span<const std::uint64_t> payloads) {
                for (const auto payload : payloads) {
                    probe.events.push_back(payload);
                    probe.event_rounds.push_back(
                        current.systemverilog_round());
                }
                return fsim::runtime::SchedulerBatchResult {
                    payloads.size(), { } };
        });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, seed, 0U, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> {
                    101U, 200U, 131U, 132U, 300U, 400U, 150U }
                && probe.event_rounds == std::vector<std::uint64_t> {
                    2U, 2U, 2U, 2U, 2U, 2U, 2U }
                && probe.execute_calls == 3U
                && probe.fallback_descriptors == 0U
                && probe.owner_live_in_execute
                && ticket_owner.expired(),
            "compact dispatch preserves virtual keys, ties, foreign cuts, and owner lifetime without fallback descriptors");
    }

    {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        std::optional<std::uint64_t> target_round;
        WaveBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->phase == SchedulerPhase::active,
                "repeated compact reservations share their source Active frontier");
            const fsim::runtime::SchedulerBatchGroupKey group_key {
                frontier->generation, 0x434f4d50414354U };
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                2U> first_members { {
                    { 30U, 131U }, { 50U, 150U },
                } };
            probe.owner = batch;
            auto first_reservation
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, group_key,
                    first_members.size());
            target_round = first_reservation.target_systemverilog_round();
            require(first_reservation && target_round
                    && *target_round == 2U
                    && first_reservation.commit_compact(first_members, *batch,
                        std::static_pointer_cast<void>(batch))
                    && !first_reservation.target_systemverilog_round(),
                "the first compact output reservation exposes its scheduler-selected target round");

            current.schedule_systemverilog(SchedulerPhase::active, 20U,
                [&probe](Scheduler& active) {
                    probe.events.push_back(200U);
                    probe.event_rounds.push_back(
                        active.systemverilog_round());
                });
            current.schedule_systemverilog(SchedulerPhase::active, 30U,
                [&probe](Scheduler& active) {
                    probe.events.push_back(300U);
                    probe.event_rounds.push_back(
                        active.systemverilog_round());
                });

            auto canceled = current.reserve_systemverilog_compact_group_batch(
                SchedulerPhase::active, *batch,
                group_key, 62U);
            require(canceled && canceled.target_systemverilog_round()
                    == target_round,
                "a repeated reservation can stage capacity beside existing members");
            canceled.cancel();
            require(!canceled.target_systemverilog_round(),
                "canceling an append invalidates its exposed target round");

            auto empty_append
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, group_key, 4U);
            const auto empty_append_target
                = empty_append.target_systemverilog_round();
            require(empty_append && empty_append_target == target_round
                    && empty_append.commit_compact(
                        std::span<const fsim::runtime::SystemVerilogCompactBatchMember> { },
                        *batch, std::static_pointer_cast<void>(batch))
                    && !empty_append.target_systemverilog_round(),
                "an empty append exposes then releases the ticket's target round");

            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                1U> rejected_member { { { 25U, 125U } } };
            auto mismatched_owner = std::make_shared<std::uint8_t>(0U);
            auto rejected = current.reserve_systemverilog_compact_group_batch(
                SchedulerPhase::active, *batch, group_key, 1U);
            const auto rejected_target_round
                = rejected.target_systemverilog_round();
            require(rejected && rejected_target_round == target_round
                    && !rejected.commit_compact(rejected_member,
                        *batch, std::static_pointer_cast<void>(mismatched_owner))
                    && !rejected.target_systemverilog_round(),
                "a repeated reservation cannot merge a different lifetime owner");

            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                3U> second_members { {
                    { 10U, 101U }, { 30U, 132U }, { 40U, 140U },
                } };
            std::array<std::uint64_t, 3U> issued_sequences { };
            auto underfilled_append
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, group_key, 4U);
            const auto underfilled_target_round
                = underfilled_append.target_systemverilog_round();
            require(underfilled_append
                    && underfilled_target_round == target_round
                    && underfilled_append.commit_compact(second_members,
                        *batch, std::static_pointer_cast<void>(batch),
                        issued_sequences)
                    && issued_sequences[1U] == issued_sequences[0U] + 1U
                    && issued_sequences[2U] == issued_sequences[1U] + 1U
                    && !underfilled_append.target_systemverilog_round(),
                "an underfilled append keeps the reserved round and issues only actual keys");
            batch.reset();
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch = std::make_shared<CompactOrderedBatch>(probe,
            [&probe, &target_round](Scheduler& current,
                     const std::span<const std::uint64_t> payloads) {
                const auto frontier = current.current_batch_frontier();
                require(frontier && frontier->phase == SchedulerPhase::active
                        && target_round
                        && frontier->systemverilog_round == *target_round,
                    "the appended ticket executes in its scheduler-reserved Active round");
                for (const auto payload : payloads) {
                    probe.events.push_back(payload);
                    probe.event_rounds.push_back(
                        current.systemverilog_round());
                }
                return fsim::runtime::SchedulerBatchResult {
                    payloads.size(), { } };
            });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, seed, 0U, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> {
                    101U, 200U, 131U, 300U, 132U, 140U, 150U }
                && probe.event_rounds == std::vector<std::uint64_t> {
                    2U, 2U, 2U, 2U, 2U, 2U, 2U }
                && probe.fallback_descriptors == 0U
                && probe.owner_live_in_execute
                && ticket_owner.expired(),
            "repeated compact reservations globally sort keys, preserve ties "
            "and foreign cuts, and retain one owner");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.tickets == 1U && stats.members == 5U
                && stats.entries_elided == 4U,
            "repeated compact reservations append to one ticket and elide four physical entries");
    }

    {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        WaveBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->phase == SchedulerPhase::active,
                "cancelled compact reservation keeps its original Active ticket");
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                2U> members { { { 20U, 201U }, { 30U, 301U } } };
            probe.owner = batch;
            commit_compact_ordered_batch(current, *batch,
                std::static_pointer_cast<void>(batch), members);

            const auto key = fsim::runtime::SchedulerBatchGroupKey {
                frontier->generation, 0x434f4d50414354U };
            auto over_capacity
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, key, 63U);
            require(!static_cast<bool>(over_capacity),
                "compact append declines before exceeding the 64-member limit");
            auto reserve_then_cancel
                = current.reserve_systemverilog_compact_group_batch(
                    SchedulerPhase::active, *batch, key, 62U);
            require(static_cast<bool>(reserve_then_cancel),
                "compact append reserves exactly the remaining member capacity");
            reserve_then_cancel.cancel();
            batch.reset();
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch = std::make_shared<CompactOrderedBatch>(probe,
            [&probe](Scheduler& current,
                     const std::span<const std::uint64_t> payloads) {
                for (const auto payload : payloads) {
                    probe.events.push_back(payload);
                    probe.event_rounds.push_back(
                        current.systemverilog_round());
                }
                return fsim::runtime::SchedulerBatchResult {
                    payloads.size(), { } };
            });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, seed, 0U, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> { 201U, 301U }
                && probe.event_rounds == std::vector<std::uint64_t> { 2U, 2U }
                && probe.fallback_descriptors == 0U
                && probe.owner_live_in_execute
                && ticket_owner.expired(),
            "canceling a relocating compact append preserves the queued ticket backing and owner");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.tickets == 1U && stats.members == 2U
                && stats.entries_elided == 1U,
            "a canceled reservation changes neither the existing compact "
            "ticket nor its accounting");
    }

    {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        WaveBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                1U> members { { { 15U, 501U } } };
            probe.owner = batch;
            commit_compact_ordered_batch(current, *batch,
                std::static_pointer_cast<void>(batch), members);
            batch.reset();
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch = std::make_shared<CompactOrderedBatch>(probe,
            [](Scheduler&, std::span<const std::uint64_t>) {
                return fsim::runtime::SchedulerBatchResult { };
            });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, seed, 0U, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> { 501U }
                && probe.execute_calls == 1U
                && probe.fallback_descriptors == 1U
                && probe.owner_live_in_fallback
                && ticket_owner.expired(),
            "one-member decline materializes one fallback descriptor and pins its owner through invocation");
    }

    {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        bool first_call { true };
        WaveBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                3U> members { {
                    { 10U, 601U }, { 20U, 602U }, { 30U, 603U },
                } };
            probe.owner = batch;
            commit_compact_ordered_batch(current, *batch,
                std::static_pointer_cast<void>(batch), members);
            batch.reset();
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch = std::make_shared<CompactOrderedBatch>(probe,
            [&probe, &first_call](Scheduler& current,
                const std::span<const std::uint64_t> payloads) {
                if (first_call) {
                    first_call = false;
                    probe.events.push_back(payloads.front());
                    current.request_stop();
                    return fsim::runtime::SchedulerBatchResult { 1U, { } };
                }
                probe.events.insert(probe.events.end(),
                    payloads.begin(), payloads.end());
                return fsim::runtime::SchedulerBatchResult {
                    payloads.size(), { } };
            });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, seed, 0U, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::stopped
                && probe.events == std::vector<std::uint64_t> { 601U }
                && !ticket_owner.expired(),
            "partial compact acceptance keeps the ordered suffix and owner alive across stop");
        scheduler.clear_stop();
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> {
                    601U, 602U, 603U }
                && probe.fallback_descriptors == 0U
                && ticket_owner.expired(),
            "resuming a partial compact ticket consumes its suffix once");
    }

    {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        bool throw_once { true };
        WaveBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                2U> members { { { 10U, 701U }, { 20U, 702U } } };
            probe.owner = batch;
            commit_compact_ordered_batch(current, *batch,
                std::static_pointer_cast<void>(batch), members);
            batch.reset();
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch = std::make_shared<CompactOrderedBatch>(probe,
            [&probe, &throw_once](Scheduler&,
                const std::span<const std::uint64_t> payloads) {
                if (throw_once) {
                    throw_once = false;
                    throw std::runtime_error("compact ticket retry");
                }
                probe.events.insert(probe.events.end(),
                    payloads.begin(), payloads.end());
                return fsim::runtime::SchedulerBatchResult {
                    payloads.size(), { } };
            });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, seed, 0U, [](Scheduler&) { });
        bool failed { };
        try {
            static_cast<void>(scheduler.run());
        } catch (const std::runtime_error& error) {
            failed = std::string_view { error.what() }
                == "compact ticket retry";
        }
        require(failed && probe.events.empty() && !ticket_owner.expired(),
            "a throwing compact attempt consumes no keys and retains the batch owner");
        require(scheduler.run().status == RunStatus::completed
                && probe.events == std::vector<std::uint64_t> {
                    701U, 702U }
                && probe.fallback_descriptors == 0U
                && ticket_owner.expired(),
            "retry after a pre-acceptance exception executes every compact member once");
    }

    {
        CompactOrderedBatchProbe probe;
        std::shared_ptr<CompactOrderedBatch> batch;
        WaveBatch seed([&](Scheduler& current,
                           const std::span<const std::uint64_t> payloads) {
            const std::array<fsim::runtime::SystemVerilogCompactBatchMember,
                2U> members { { { 10U, 801U }, { 20U, 802U } } };
            probe.owner = batch;
            commit_compact_ordered_batch(current, *batch,
                std::static_pointer_cast<void>(batch), members);
            batch.reset();
            current.request_stop();
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch = std::make_shared<CompactOrderedBatch>(probe,
            [](Scheduler&, std::span<const std::uint64_t>) {
                return fsim::runtime::SchedulerBatchResult { };
            });
        const std::weak_ptr<CompactOrderedBatch> ticket_owner = batch;
        Scheduler scheduler;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, seed, 0U, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::stopped
                && !ticket_owner.expired() && probe.events.empty(),
            "stopping before compact dispatch keeps the queued executor alive");
        scheduler.discard_pending();
        require(ticket_owner.expired() && probe.events.empty()
                && probe.fallback_descriptors == 0U,
            "discard retires the compact ticket and releases its executor owner without invoking the suffix");
    }
}

void test_sv_ordered_private_update_ticket()
{
    {
        auto owner_snapshot = std::make_shared<OrderedTicketOwner>();
        std::vector<std::uint64_t> event_rounds;
        std::weak_ptr<OrderedTicketOwner> ticket_owner = owner_snapshot;
        bool member_read_live_storage { };
        OrderedTicketOwnerProbe owner_probe {
            &ticket_owner, &owner_snapshot, -1,
            &member_read_live_storage, &event_rounds };
        Scheduler scheduler;
        std::vector<int> events;
        WaveBatch batch([&](Scheduler& current,
                            std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->phase == SchedulerPhase::active
                    && payloads.size() == 1U,
                "private ticket reservation uses its exact Active frontier");
            const std::array<fsim::runtime::StableOrder, 4U> orders {
                10U, 30U, 30U, 50U };
            const std::array<OrderedTicketTask, 4U> payloads_for_ticket {
                OrderedTicketTask {
                    &events, &owner_probe, 10, 0, 0, 5U },
                OrderedTicketTask {
                    &events, &owner_probe, 31, 0, 0, 0U },
                OrderedTicketTask {
                    &events, &owner_probe, 32, 0, 0, 0U },
                OrderedTicketTask {
                    &events, &owner_probe, 50, 0, 0, 0U },
            };
            std::array<fsim::runtime::detail::SchedulerTaskDescriptor, 4U>
                tasks;
            for (std::size_t index = 0U; index < tasks.size(); ++index) {
                tasks[index]
                    = fsim::runtime::detail::make_scheduler_task_descriptor<
                        OrderedTicketTask, &dispatch_ordered_ticket_task>(
                            payloads_for_ticket[index]);
            }
            auto owner = std::static_pointer_cast<void>(owner_snapshot);
            auto ticket = current
                .reserve_internal_systemverilog_ordered_ticket_from_frontier(
                    frontier->generation, orders, tasks,
                    owner_snapshot->storage,
                    std::move(owner));
            require(ticket && ticket.commit_ordered_ticket(),
                "ordered private update callbacks reserve and commit once");
            auto reused_storage = current
                .reserve_internal_systemverilog_ordered_ticket_from_frontier(
                    frontier->generation, orders, tasks,
                    owner_snapshot->storage,
                    std::static_pointer_cast<void>(owner_snapshot));
            require(!reused_storage,
                "a pending ticket keeps its storage unavailable for reuse");

            current.schedule_systemverilog(SchedulerPhase::active, 20U,
                [&events, &event_rounds](Scheduler& active_scheduler) {
                    events.push_back(20);
                    event_rounds.push_back(
                        active_scheduler.systemverilog_round());
                });
            current.schedule_systemverilog(SchedulerPhase::active, 30U,
                [&events, &event_rounds](Scheduler& active_scheduler) {
                    events.push_back(300);
                    event_rounds.push_back(
                        active_scheduler.systemverilog_round());
                });
            current.schedule_systemverilog(SchedulerPhase::active, 40U,
                [&events, &event_rounds](Scheduler& active_scheduler) {
                    events.push_back(40);
                    event_rounds.push_back(
                        active_scheduler.systemverilog_round());
                });
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, batch, 0U, [](Scheduler&) {
                throw std::runtime_error("private-ticket seed declined");
            });

        const auto result = scheduler.run();
        require(result.status == RunStatus::completed
                && events == std::vector<int> {
                    10, 20, 31, 32, 300, 40, 50, 5 }
                && event_rounds == std::vector<std::uint64_t> {
                    2U, 2U, 2U, 2U, 2U, 2U, 2U, 3U },
            "ticket members retain original keys around foreign work and ties");
        require(owner_snapshot->storage.available()
                && !ticket_owner.expired() && member_read_live_storage,
            "the drained ticket retires reusable storage after callbacks");
        require(owner_snapshot->storage.capacity() >= 4U,
            "drained component ticket storage retains member capacity");
        owner_snapshot.reset();
        require(ticket_owner.expired(),
            "retired ticket storage does not retain its component owner");
    }

    {
        fsim::runtime::InternalSystemVerilogOrderedTicketStorage storage;
        Scheduler scheduler;
        std::vector<int> events;
        std::weak_ptr<int> ticket_owner;
        bool frontier_sequences_preserved { };
        WaveBatch* batch_pointer { };
        WaveBatch batch([&](Scheduler& current,
                            std::span<const std::uint64_t> payloads) {
            if (payloads.front() == 0U) {
                const auto frontier = current.current_batch_frontier();
                require(frontier && frontier->tasks.size() == 1U,
                    "cancelled reservation is scoped to its source frontier");
                const std::array<fsim::runtime::StableOrder, 1U> orders { 5U };
                const OrderedTicketTask payload { &events, nullptr,
                    7, 0, 0, 0U };
                const std::array<fsim::runtime::detail::SchedulerTaskDescriptor,
                    1U> tasks {
                    fsim::runtime::detail::make_scheduler_task_descriptor<
                        OrderedTicketTask, &dispatch_ordered_ticket_task>(payload)
                };
                auto owner = std::make_shared<int>(29);
                ticket_owner = owner;
                auto ticket = current
                    .reserve_internal_systemverilog_ordered_ticket_from_frontier(
                        frontier->generation, orders, tasks, storage,
                        std::move(owner));
                require(static_cast<bool>(ticket),
                    "ordered reservation can be cancelled before publication");
                ticket.cancel();
                require(storage.available() && ticket_owner.expired(),
                    "cancellation retires storage without keeping an owner");
                const SchedulerBatchGroupKey key { 2U, 9U };
                for (std::uint64_t next_payload : { 1U, 2U }) {
                    current.schedule_systemverilog_group_batchable(
                        SchedulerPhase::active, 5U, *batch_pointer,
                        next_payload,
                        [](Scheduler&) { }, key);
                }
                return fsim::runtime::SchedulerBatchResult {
                    payloads.size(), { } };
            }
            const auto frontier = current.current_batch_frontier();
            frontier_sequences_preserved = frontier
                && frontier->tasks.size() == 2U
                && frontier->tasks[0U].sequence == 1U
                && frontier->tasks[1U].sequence == 2U;
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        batch_pointer = &batch;
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, batch, 0U, [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::completed
                && frontier_sequences_preserved,
            "cancelled tickets consume no sequence identities");
    }

    {
        auto owner_snapshot = std::make_shared<OrderedTicketOwner>();
        std::weak_ptr<OrderedTicketOwner> ticket_owner = owner_snapshot;
        bool final_member_read_live_storage { };
        OrderedTicketOwnerProbe owner_probe {
            &ticket_owner, &owner_snapshot, 3,
            &final_member_read_live_storage };
        Scheduler scheduler;
        std::vector<int> events;
        WaveBatch batch([&](Scheduler& current,
                            std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && payloads.size() == 1U,
                "snapshot replacement uses its exact Active frontier");
            const std::array<fsim::runtime::StableOrder, 3U> orders {
                10U, 20U, 30U };
            const std::array<OrderedTicketTask, 3U> payloads_for_ticket {
                OrderedTicketTask {
                    &events, &owner_probe, 1, 0, 0, 0U },
                OrderedTicketTask {
                    &events, &owner_probe, 2, 0, 0, 0U },
                OrderedTicketTask {
                    &events, &owner_probe, 3, 0, 0, 0U },
            };
            std::array<fsim::runtime::detail::SchedulerTaskDescriptor, 3U>
                tasks;
            for (std::size_t index = 0U; index < tasks.size(); ++index) {
                tasks[index]
                    = fsim::runtime::detail::make_scheduler_task_descriptor<
                        OrderedTicketTask, &dispatch_ordered_ticket_task>(
                            payloads_for_ticket[index]);
            }
            auto owner = std::static_pointer_cast<void>(owner_snapshot);
            auto ticket = current
                .reserve_internal_systemverilog_ordered_ticket_from_frontier(
                    frontier->generation, orders, tasks,
                    owner_snapshot->storage,
                    std::move(owner));
            require(ticket && ticket.commit_ordered_ticket(),
                "snapshot-replacement ticket commits before callbacks");
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, batch, 0U, [](Scheduler&) {
                throw std::runtime_error("private-ticket seed declined");
            });
        require(scheduler.run().status == RunStatus::completed
                && events == std::vector<int> { 1, 2, 3 }
                && owner_snapshot == nullptr && ticket_owner.expired()
                && final_member_read_live_storage,
            "the final callback may replace a snapshot while its ticket lease keeps payload storage alive");
    }

    enum class Interruption { stop, failure, discard };
    for (const auto interruption : { Interruption::stop,
             Interruption::failure, Interruption::discard }) {
        auto owner_snapshot = std::make_shared<OrderedTicketOwner>();
        std::weak_ptr<OrderedTicketOwner> ticket_owner = owner_snapshot;
        bool member_read_live_storage { };
        OrderedTicketOwnerProbe owner_probe {
            &ticket_owner, &owner_snapshot, -1,
            &member_read_live_storage };
        Scheduler scheduler;
        std::vector<int> events;
        WaveBatch batch([&](Scheduler& current,
                            std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && payloads.size() == 1U,
                "lifecycle ticket uses a single seed callback");
            const std::array<fsim::runtime::StableOrder, 3U> orders {
                10U, 20U, 30U };
            const std::array<OrderedTicketTask, 3U> payloads_for_ticket {
                OrderedTicketTask { &events, &owner_probe, 1, 0, 0, 0U },
                OrderedTicketTask { &events, &owner_probe, 2,
                    interruption == Interruption::stop ? 2 : 0,
                    interruption == Interruption::failure ? 2 : 0, 0U },
                OrderedTicketTask { &events, &owner_probe, 3, 0, 0, 0U },
            };
            std::array<fsim::runtime::detail::SchedulerTaskDescriptor, 3U>
                tasks;
            for (std::size_t index = 0U; index < tasks.size(); ++index) {
                tasks[index]
                    = fsim::runtime::detail::make_scheduler_task_descriptor<
                        OrderedTicketTask, &dispatch_ordered_ticket_task>(
                            payloads_for_ticket[index]);
            }
            auto owner = std::static_pointer_cast<void>(owner_snapshot);
            auto ticket = current
                .reserve_internal_systemverilog_ordered_ticket_from_frontier(
                    frontier->generation, orders, tasks,
                    owner_snapshot->storage,
                    std::move(owner));
            require(ticket && ticket.commit_ordered_ticket(),
                "lifecycle ticket commits before an interruption");
            owner_snapshot.reset();
            if (interruption == Interruption::discard)
                current.request_stop();
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            0U, batch, 0U, [](Scheduler&) {
                throw std::runtime_error("private-ticket seed declined");
            });

        if (interruption == Interruption::failure) {
            bool failed { };
            try {
                static_cast<void>(scheduler.run());
            } catch (const std::runtime_error& error) {
                failed = std::string_view { error.what() }
                    == "ordered ticket member failure";
            }
            require(failed && events == std::vector<int> { 1, 2 }
                    && !ticket_owner.expired() && member_read_live_storage,
                "a throwing member is consumed once and its suffix retains storage");
            require(scheduler.run().status == RunStatus::completed
                    && events == std::vector<int> { 1, 2, 3 }
                    && ticket_owner.expired() && member_read_live_storage,
                "throw recovery resumes without replay after snapshot replacement");
        } else if (interruption == Interruption::stop) {
            require(scheduler.run().status == RunStatus::stopped
                    && events == std::vector<int> { 1, 2 }
                    && !ticket_owner.expired() && member_read_live_storage,
                "stop preserves the unconsumed ordered-ticket suffix");
            scheduler.clear_stop();
            require(scheduler.run().status == RunStatus::completed
                    && events == std::vector<int> { 1, 2, 3 }
                    && ticket_owner.expired() && member_read_live_storage,
                "resume consumes suffix after snapshot replacement");
        } else {
            require(scheduler.run().status == RunStatus::stopped
                    && events.empty()
                    && !ticket_owner.expired(),
                "a stopped seed leaves its private ticket owned and pending");
            scheduler.discard_pending();
            require(ticket_owner.expired()
                    && events.empty(),
                "discard retires the ticket and releases its component owner");
        }
    }
}

void test_sv_composite_ticket_virtual_order()
{
    Scheduler scheduler;
    std::vector<std::string> events;
    std::vector<SchedulerBatchFrontierEntry> frontier_entries;
    WaveBatch batch([&](Scheduler& current,
                        const std::span<const std::uint64_t> payloads) {
        const auto frontier = current.current_batch_frontier();
        require(frontier && frontier->phase == SchedulerPhase::active
                && frontier->time == 0U && current.now() == 0U
                && frontier->delta == 0U && current.delta() == 0U
                && frontier->systemverilog_round == 1U
                && frontier->systemverilog_round
                    == current.systemverilog_round()
                && frontier->tasks.size() == payloads.size(),
            "virtual ticket members retain the original SV frontier");
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            frontier_entries.push_back(frontier->tasks[index]);
            events.push_back("batch" + std::to_string(payloads[index]));
        }
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey group_key { 7U, 3U };
    const auto record_event = [&events](const char* label) {
        return [&, label](Scheduler& current) {
            require(current.current_phase() == SchedulerPhase::active
                    && current.delta() == 0U
                    && current.systemverilog_round() == 2U,
                "late Active insertions enter the next frozen scheduler round");
            events.push_back(label);
        };
    };
    for (const auto payload : { 20U, 30U, 40U }) {
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, payload, batch, payload,
            [](Scheduler&) {
                throw std::runtime_error(
                    "accepted composite ticket member fell back");
            }, group_key);
    }
    scheduler.schedule_systemverilog(SchedulerPhase::active, 10U,
        [&events, record_event](Scheduler& current) {
            events.push_back("seed");
            current.schedule_systemverilog(SchedulerPhase::active, 15U,
                record_event("lower"));
            current.schedule_systemverilog(SchedulerPhase::active, 25U,
                record_event("middle1"));
            current.schedule_systemverilog(SchedulerPhase::active, 35U,
                record_event("middle2"));
            current.schedule_systemverilog(SchedulerPhase::active, 45U,
                record_event("upper"));
        });

    require(scheduler.run().status == RunStatus::completed,
        "composite ticket with late insertions completes");
    require(events == std::vector<std::string> { "seed", "batch20",
                "batch30", "batch40", "lower", "middle1", "middle2",
                "upper" },
        "the original ticket finishes before late next-round Active insertions");
    require(frontier_entries.size() == 3U
            && frontier_entries[0].stable_order == 20U
            && frontier_entries[1].stable_order == 30U
            && frontier_entries[2].stable_order == 40U
            && frontier_entries[0].sequence == 0U
            && frontier_entries[1].sequence == 1U
            && frontier_entries[2].sequence == 2U,
        "composite tickets preserve each original stable order and sequence");
    const auto compacted = scheduler.systemverilog_batch_compaction_stats();
    require(compacted.tickets == 1U && compacted.members == 3U
            && compacted.entries_elided == 2U
            && compacted.direct_dispatches == 1U
            && compacted.direct_members == 3U,
        "one contiguous component run occupies one scheduler ticket");
}

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

void test_sv_readiness_ticket_recycles_after_batch_gather()
{
    using fsim::runtime::SchedulerBatchFrontier;
    using fsim::runtime::SchedulerSystemVerilogKeyReceipt;

    constexpr std::size_t initial_ticket_count = 64U;
    constexpr std::size_t late_ticket_index = initial_ticket_count;
    constexpr std::uint64_t ordinary_payload = 500U;
    constexpr std::uint64_t gathered_ticket_payload = 501U;
    constexpr std::uint64_t late_ticket_payload = 999U;
    constexpr std::uint64_t late_ticket_order = 1000U;

    Scheduler scheduler;
    std::array<SchedulerSystemVerilogKeyReceipt,
        initial_ticket_count + 2U> receipts;
    std::array<std::weak_ptr<void>, initial_ticket_count + 1U> owners;
    std::weak_ptr<void> ordinary_owner;
    std::vector<std::uint64_t> observed_payloads;
    std::size_t fallback_calls { };
    bool first_gather_seen { };
    bool late_ticket_admitted { };

    const auto matches_key = [](const SchedulerBatchFrontier& frontier,
                                 const std::size_t index,
                                 const SchedulerSystemVerilogKeyReceipt& receipt,
                                 const std::uint64_t payload) {
        if (index >= frontier.tasks.size())
            return false;
        const auto& key = frontier.tasks[index];
        return receipt.valid && receipt.time == frontier.time
            && receipt.delta == frontier.delta
            && receipt.systemverilog_round == frontier.systemverilog_round
            && receipt.phase == frontier.phase
            && receipt.stable_order == key.stable_order
            && receipt.sequence == key.sequence
            && key.payload == payload;
    };

    WaveBatch batch([&](Scheduler& current,
                        const std::span<const std::uint64_t> payloads) {
        const auto frontier = current.current_batch_frontier();
        require(frontier && frontier->phase == SchedulerPhase::active
                && frontier->cursor == 0U
                && frontier->end == payloads.size()
                && frontier->tasks.size() == payloads.size(),
            "readiness recycle callbacks expose their exact Active keys");

        if (!first_gather_seen) {
            require(payloads.size() == 2U
                    && payloads[0U] == ordinary_payload
                    && payloads[1U] == gathered_ticket_payload
                    && matches_key(*frontier, 0U, receipts[0U],
                        ordinary_payload)
                    && matches_key(*frontier, 1U, receipts[1U],
                        gathered_ticket_payload)
                    && frontier->tasks[0U].stable_order
                        < frontier->tasks[1U].stable_order
                    && frontier->tasks[0U].sequence
                        < frontier->tasks[1U].sequence,
                "ordinary member and singleton ticket gather in original key order");
            require(!ordinary_owner.expired() && !owners[0U].expired(),
                "both gathered members retain their owners through execution");
            observed_payloads.insert(observed_payloads.end(),
                payloads.begin(), payloads.end());
            first_gather_seen = true;

            auto owner = std::make_shared<std::uint64_t>(
                late_ticket_payload);
            owners[late_ticket_index] = owner;
            late_ticket_admitted
                = current.schedule_systemverilog_readiness_member(
                    SchedulerPhase::active, late_ticket_order, batch,
                    late_ticket_payload,
                    [owner, &fallback_calls](Scheduler&) {
                        ++fallback_calls;
                    },
                    { 91U, 1000U }, &receipts[late_ticket_index + 1U]);
            owner.reset();
            require(late_ticket_admitted
                    && receipts[late_ticket_index + 1U].valid,
                "the just-exhausted slot admits a new ticket before phase drain");
            return SchedulerBatchResult { payloads.size(), { } };
        }

        require(payloads.size() == 1U,
            "remaining readiness members stay separate singleton tickets");
        const auto payload = payloads[0U];
        std::size_t owner_index { };
        std::size_t receipt_index { };
        std::uint64_t expected_order { };
        if (payload >= 521U && payload <= 583U) {
            owner_index = static_cast<std::size_t>(payload - 520U);
            receipt_index = owner_index + 1U;
            expected_order = 20U + owner_index;
        } else if (payload == late_ticket_payload) {
            owner_index = late_ticket_index;
            receipt_index = late_ticket_index + 1U;
            expected_order = late_ticket_order;
        } else {
            require(false, "unexpected readiness recycle payload");
        }
        require(matches_key(*frontier, 0U, receipts[receipt_index], payload)
                && frontier->tasks[0U].stable_order == expected_order
                && !owners[owner_index].expired(),
            "each remaining ticket keeps its receipt, order and owner");
        if (payload == 521U) {
            require(ordinary_owner.expired() && owners[0U].expired(),
                "gathered owners retire after the consumed batch is completed");
        }
        observed_payloads.push_back(payload);
        return SchedulerBatchResult { payloads.size(), { } };
    });

    scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
        [&](Scheduler& current) {
            auto ordinary_lifetime
                = std::make_shared<std::uint64_t>(ordinary_payload);
            ordinary_owner = ordinary_lifetime;
            current.schedule_systemverilog_group_batchable(
                SchedulerPhase::active, 5U, batch, ordinary_payload,
                [ordinary_lifetime, &fallback_calls](Scheduler&) {
                    ++fallback_calls;
                },
                { 91U, 1U }, &receipts[0U]);
            ordinary_lifetime.reset();

            for (std::size_t index = 0U;
                 index < initial_ticket_count; ++index) {
                const auto payload = index == 0U
                    ? gathered_ticket_payload
                    : 520U + static_cast<std::uint64_t>(index);
                const auto order = index == 0U ? 10U
                    : 20U + static_cast<std::uint64_t>(index);
                auto owner = std::make_shared<std::uint64_t>(payload);
                owners[index] = owner;
                const auto admitted
                    = current.schedule_systemverilog_readiness_member(
                        SchedulerPhase::active, order, batch, payload,
                        [owner, &fallback_calls](Scheduler&) {
                            ++fallback_calls;
                        },
                        { 91U, 1U + index }, &receipts[index + 1U]);
                owner.reset();
                require(admitted && receipts[index + 1U].valid,
                    "the initial readiness population fills all 64 slots");
            }
        });

    require(scheduler.run().status == RunStatus::completed
            && first_gather_seen && late_ticket_admitted
            && fallback_calls == 0U,
        "the ordinary-plus-ticket gather completes without fallback");

    std::vector<std::uint64_t> expected_payloads {
        ordinary_payload, gathered_ticket_payload };
    for (std::size_t index = 1U; index < initial_ticket_count; ++index) {
        expected_payloads.push_back(520U
            + static_cast<std::uint64_t>(index));
    }
    expected_payloads.push_back(late_ticket_payload);
    require(observed_payloads == expected_payloads,
        "recycled ticket execution preserves exact callback order");

    for (const auto& receipt : receipts) {
        require(receipt.valid,
            "every original and recycled readiness member has a receipt");
    }
    for (std::size_t left = 0U; left < receipts.size(); ++left) {
        for (std::size_t right = left + 1U; right < receipts.size(); ++right) {
            require(receipts[left].sequence != receipts[right].sequence,
                "readiness recycling preserves unique scheduler sequences");
        }
    }
    require(ordinary_owner.expired(),
        "the ordinary gathered member releases its captured owner");
    for (const auto& owner : owners) {
        require(owner.expired(),
            "each consumed ticket releases its captured owner");
    }

    const auto stats = scheduler.systemverilog_batch_compaction_stats();
    require(stats.readiness_ticket_queue_insertions
                == initial_ticket_count + 1U
            && stats.readiness_ticket_members == initial_ticket_count + 1U
            && stats.readiness_ticket_fallback_members == 0U
            && stats.direct_dispatches == initial_ticket_count
            && stats.direct_members == initial_ticket_count,
        "one freed physical slot admits the later ticket with no fallback");
}

void test_sv_grouped_readiness_above_sixty_four_preserves_frontiers()
{
    {
        constexpr std::size_t member_count = 65U;
        std::vector<std::vector<SchedulerBatchFrontierEntry>> frontiers;
        std::vector<std::size_t> visits;
        WaveBatch batch([&](Scheduler& current,
                            const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->phase == SchedulerPhase::active
                    && frontier->cursor == 0U
                    && frontier->end == payloads.size()
                    && frontier->tasks.size() == payloads.size(),
                "65-member readiness dispatch exposes each bounded frontier");
            frontiers.emplace_back(frontier->tasks.begin(),
                frontier->tasks.end());
            for (const auto payload : payloads) {
                visits.push_back(static_cast<std::size_t>(payload));
            }
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 71U, 9U };
        std::vector<Scheduler::SystemVerilogGroupBatchMember> members;
        std::vector<fsim::runtime::SchedulerSystemVerilogKeyReceipt> receipts(
            member_count);
        members.reserve(member_count);
        for (std::size_t index = 0U; index < member_count; ++index) {
            // Sparse orders model a selected ready subset while retaining
            // the original stable keys for every queued member.
            members.push_back({
                100U + index * 2U,
                static_cast<std::uint64_t>(index + 1U),
                [](Scheduler&) {
                    throw std::runtime_error(
                        "large readiness group unexpectedly fell back");
                },
                { },
                { },
            });
        }
        bool committed { };
        Scheduler scheduler;
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&](Scheduler& current) {
                require(current.schedule_systemverilog_readiness_group(
                            SchedulerPhase::active, batch, group_key,
                            members, receipts),
                    "one readiness group commits 65 original members");
                committed = true;
            });

        require(scheduler.run().status == RunStatus::completed && committed
                && visits.size() == member_count
                && frontiers.size() == 2U
                && frontiers[0U].size() == 64U
                && frontiers[1U].size() == 1U,
            "one 65-member ticket expands through the 64-entry dispatch scratch");
        for (std::size_t index = 0U; index < member_count; ++index) {
            const auto frontier_index = index < 64U ? 0U : 1U;
            const auto member_index = index < 64U ? index : index - 64U;
            require(visits[index] == index + 1U
                    && receipts[index].valid
                    && receipts[index].time == SimulationTick { }
                    && receipts[index].delta == 0U
                    && receipts[index].systemverilog_round == 2U
                    && receipts[index].phase == SchedulerPhase::active
                    && receipts[index].stable_order == 100U + index * 2U
                    && receipts[index].sequence == index + 1U
                    && frontiers[frontier_index][member_index].stable_order
                        == 100U + index * 2U
                    && frontiers[frontier_index][member_index].sequence
                        == receipts[index].sequence
                    && frontiers[frontier_index][member_index].payload
                        == static_cast<std::uint64_t>(index + 1U),
                "65-member ticket retains each original scheduler key");
        }
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == 1U
                && stats.readiness_ticket_members == member_count
                && stats.readiness_ticket_members_elided == member_count - 1U
                && stats.readiness_ticket_fallback_members == 0U
                && stats.direct_dispatches == 1U
                && stats.direct_members == 1U,
            "65 logical readiness members use one ticket and a 64+1 scratch split");
    }

    {
        constexpr std::size_t member_count = 129U;
        constexpr std::size_t foreign_order = 164U;
        constexpr std::size_t accepted_before_failure = 17U;
        std::vector<std::vector<SchedulerBatchFrontierEntry>> frontiers;
        std::vector<std::size_t> visits;
        std::vector<std::size_t> execution_count(member_count);
        std::size_t batch_calls { };
        WaveBatch batch([&](Scheduler& current,
                            const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->phase == SchedulerPhase::active
                    && frontier->cursor == 0U
                    && frontier->end == payloads.size()
                    && frontier->tasks.size() == payloads.size(),
                "each partial large-group retry authenticates its offered frontier");
            frontiers.emplace_back(frontier->tasks.begin(),
                frontier->tasks.end());
            const auto call = batch_calls++;
            if (call == 0U) {
                require(payloads.size() == 64U,
                    "equal-order foreign work bounds the first offered prefix");
                for (std::size_t index = 0U;
                     index < accepted_before_failure; ++index) {
                    const auto payload = payloads[index];
                    visits.push_back(static_cast<std::size_t>(payload));
                    ++execution_count[static_cast<std::size_t>(payload - 1U)];
                }
                return SchedulerBatchResult { accepted_before_failure,
                    std::make_exception_ptr(std::runtime_error(
                        "injected large readiness prefix failure")) };
            }
            for (const auto payload : payloads) {
                visits.push_back(static_cast<std::size_t>(payload));
                ++execution_count[static_cast<std::size_t>(payload - 1U)];
            }
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 72U, 9U };
        std::vector<Scheduler::SystemVerilogGroupBatchMember> members;
        std::vector<fsim::runtime::SchedulerSystemVerilogKeyReceipt> receipts(
            member_count);
        members.reserve(member_count);
        for (std::size_t index = 0U; index < member_count; ++index) {
            members.push_back({
                100U + index,
                static_cast<std::uint64_t>(index + 1U),
                [](Scheduler&) {
                    throw std::runtime_error(
                        "large readiness retry unexpectedly fell back");
                },
                { },
                { },
            });
        }
        bool committed { };
        Scheduler scheduler;
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&](Scheduler& current) {
                // The ordinary key ties with group member 65 and has an
                // earlier sequence, so the ticket may not cross it.
                current.schedule_systemverilog(SchedulerPhase::active,
                    foreign_order, [&](Scheduler&) {
                        visits.push_back(0U);
                    });
                require(current.schedule_systemverilog_readiness_group(
                            SchedulerPhase::active, batch, group_key,
                            members, receipts),
                    "one readiness group commits all 129 keys before a foreign cut");
                committed = true;
            });

        bool failed_after_prefix { };
        try {
            static_cast<void>(scheduler.run());
        } catch (const std::runtime_error& error) {
            failed_after_prefix = std::string_view { error.what() }
                == "injected large readiness prefix failure";
            if (!failed_after_prefix) {
                throw;
            }
        }
        require(committed && failed_after_prefix && scheduler.has_pending()
                && batch_calls == 1U
                && visits.size() == accepted_before_failure,
            "a thrown callback retires only its reported large prefix");
        require(scheduler.run().status == RunStatus::completed
                && batch_calls == 4U
                && visits.size() == member_count + 1U,
            "retry consumes the remaining large members around one foreign key");
        require(frontiers.size() == 4U
                && frontiers[0U].size() == 64U
                && frontiers[1U].size() == 47U
                && frontiers[2U].size() == 64U
                && frontiers[3U].size() == 1U,
            "foreign-key and failure cuts expose only each offered prefix");
        for (std::size_t index = 0U; index < member_count; ++index) {
            require(execution_count[index] == 1U
                    && receipts[index].valid
                    && receipts[index].stable_order == 100U + index
                    && receipts[index].sequence == index + 2U,
                "large retry neither loses nor duplicates an original key");
        }
        const auto verify_frontier = [&](
            const std::size_t call, const std::size_t first,
            const std::size_t count) {
            require(frontiers[call].size() == count,
                "captured large readiness prefix has its expected extent");
            for (std::size_t offset = 0U; offset < count; ++offset) {
                const auto& entry = frontiers[call][offset];
                const auto index = first + offset;
                require(entry.stable_order == 100U + index
                        && entry.sequence == index + 2U
                        && entry.payload
                            == static_cast<std::uint64_t>(index + 1U),
                    "retry frontiers reproduce original keys without rekeying");
            }
        };
        verify_frontier(0U, 0U, 64U);
        verify_frontier(1U, accepted_before_failure,
            64U - accepted_before_failure);
        verify_frontier(2U, 64U, 64U);
        verify_frontier(3U, 128U, 1U);
        std::vector<std::size_t> expected_visits;
        expected_visits.reserve(member_count + 1U);
        for (std::size_t index = 0U; index < 64U; ++index) {
            expected_visits.push_back(index + 1U);
        }
        expected_visits.push_back(0U);
        for (std::size_t index = 64U; index < member_count; ++index) {
            expected_visits.push_back(index + 1U);
        }
        require(visits == expected_visits,
            "the foreign equal-order task stays between the exact group prefixes");
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == 1U
                && stats.readiness_ticket_members == member_count
                && stats.readiness_ticket_members_elided == member_count - 1U
                && stats.readiness_ticket_fallback_members == 0U
                && stats.direct_dispatches == 3U
                && stats.direct_members == 64U + 47U + 1U,
            "partial failure and retry preserve one physical 129-member ticket");
    }
}

void test_sv_readiness_ticket_pool_capacity_growth()
{
    {
        constexpr std::size_t group_count = 65U;
        Scheduler scheduler;
        WaveBatch batch([](Scheduler&,
                               const std::span<const std::uint64_t> payloads) {
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });
        std::size_t accepted { };
        bool overflow_declined { };
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&batch, &accepted, &overflow_declined](Scheduler& current) {
                for (std::size_t index = 0U; index < group_count; ++index) {
                    Scheduler::ReadinessBatchMember member;
                    member.stable_order = index;
                    member.payload = index + 1U;
                    member.fallback_task = [](Scheduler&) { };
                    std::array<Scheduler::ReadinessBatchMember, 1U> one {
                        std::move(member) };
                    const bool queued
                        = current.schedule_systemverilog_readiness_group(
                            SchedulerPhase::active, batch,
                            { 90U, index + 1U }, one);
                    if (!queued) {
                        overflow_declined = index == 64U;
                        break;
                    }
                    ++accepted;
                }
                current.request_stop();
            });
        require(scheduler.run().status == RunStatus::stopped
                && accepted == 64U && overflow_declined,
            "standalone schedulers retain their 64-slot default pool");
        scheduler.discard_pending();
        scheduler.clear_stop();
    }

    const std::array<std::size_t, 2U> group_counts { 65U, 129U };
    for (const auto group_count : group_counts) {
        Scheduler scheduler;
        if (group_count > 65U) {
            require(scheduler
                        .prepare_systemverilog_readiness_ticket_capacity(65U),
                "the scheduler can grow beyond its inline readiness pool");
        }
        require(scheduler.prepare_systemverilog_readiness_ticket_capacity(
                    group_count),
            "the scheduler prepares one ticket slot per elaborated group");

        std::vector<std::uint64_t> visits;
        std::vector<fsim::runtime::SchedulerSystemVerilogKeyReceipt>
            receipts(group_count);
        WaveBatch batch([&visits](Scheduler& current,
                              const std::span<const std::uint64_t> payloads) {
            const auto frontier = current.current_batch_frontier();
            require(payloads.size() == 1U && frontier
                    && frontier->phase == SchedulerPhase::active
                    && frontier->cursor == 0U
                    && frontier->end == 1U
                    && frontier->tasks.size() == 1U
                    && frontier->tasks.front().payload == payloads.front(),
                "each component ticket preserves its own one-member frontier");
            visits.push_back(payloads.front());
            return fsim::runtime::SchedulerBatchResult {
                payloads.size(), { } };
        });

        bool committed { };
        scheduler.schedule_systemverilog(SchedulerPhase::active, 0U,
            [&](Scheduler& current) {
                for (std::size_t index = 0U; index < group_count; ++index) {
                    Scheduler::ReadinessBatchMember member;
                    member.stable_order = 5000U - index;
                    member.payload = index + 1U;
                    member.fallback_task = [](Scheduler&) {
                        throw std::runtime_error(
                            "prepared readiness pool unexpectedly fell back");
                    };
                    std::array<Scheduler::ReadinessBatchMember, 1U>
                        one_member { std::move(member) };
                    std::array<
                        fsim::runtime::SchedulerSystemVerilogKeyReceipt, 1U>
                        one_receipt { };
                    require(current.schedule_systemverilog_readiness_group(
                                SchedulerPhase::active, batch,
                                { 91U, index + 1U }, one_member, one_receipt),
                        "all prepared component keys receive distinct tickets");
                    receipts[index] = one_receipt.front();
                }
                committed = true;
            });

        require(scheduler.run().status == RunStatus::completed && committed
                && visits.size() == group_count,
            "65 and 129 prepared component tickets drain exactly once");
        for (std::size_t index = 0U; index < group_count; ++index) {
            const auto visit_index = group_count - index - 1U;
            require(visits[index] == visit_index + 1U
                    && receipts[visit_index].valid
                    && receipts[visit_index].stable_order
                        == 5000U - visit_index
                    && receipts[visit_index].sequence == visit_index + 1U,
                "expanded pool retains each component's original ordering key");
        }
        const auto stats = scheduler.systemverilog_batch_compaction_stats();
        require(stats.readiness_ticket_queue_insertions == group_count
                && stats.readiness_ticket_members == group_count
                && stats.readiness_ticket_members_elided == 0U
                && stats.readiness_ticket_fallback_members == 0U
                && stats.tickets == group_count
                && stats.members == group_count
                && stats.entries_elided == 0U
                && stats.direct_dispatches == group_count
                && stats.direct_members == group_count,
            "each distinct fixed-topology group owns one readiness ticket");
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

void test_sv_composite_ticket_preserves_update_phase()
{
    Scheduler scheduler;
    std::vector<std::uint64_t> payloads_seen;
    WaveBatch batch([&payloads_seen](Scheduler& current,
                                const std::span<const std::uint64_t> payloads) {
        const auto frontier = current.current_batch_frontier();
        require(frontier && frontier->phase == SchedulerPhase::update
                && frontier->systemverilog_round
                    == current.systemverilog_round(),
            "uncompacted update work retains its original SV round");
        payloads_seen.insert(payloads_seen.end(), payloads.begin(), payloads.end());
        return SchedulerBatchResult { payloads.size(), { } };
    });
    const SchedulerBatchGroupKey group_key { 8U, 4U };
    for (std::uint64_t payload = 1U; payload <= 2U; ++payload) {
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::update, payload, batch, payload,
            [](Scheduler&) { }, group_key);
    }
    require(scheduler.run().status == RunStatus::completed
            && payloads_seen == std::vector<std::uint64_t> { 1U, 2U },
        "grouped API preserves ordered Update execution");
    const auto compacted = scheduler.systemverilog_batch_compaction_stats();
    require(compacted.tickets == 0U && compacted.members == 0U,
        "component queue compaction is limited to Active work");
}

void test_sv_composite_ticket_prefix_recovery()
{
    enum class Interruption { decline, stop, failure };
    for (const auto interruption : { Interruption::decline,
             Interruption::stop, Interruption::failure }) {
        Scheduler scheduler;
        std::vector<std::uint64_t> visited;
        std::vector<std::vector<std::uint64_t>> offered;
        std::vector<std::vector<SchedulerBatchFrontierEntry>> frontiers;
        std::size_t calls { };
        std::size_t fallback_calls { };
        WaveBatch batch([&](Scheduler& current,
                            const std::span<const std::uint64_t> payloads) {
            ++calls;
            offered.emplace_back(payloads.begin(), payloads.end());
            const auto frontier = current.current_batch_frontier();
            require(frontier && frontier->tasks.size() == payloads.size(),
                "prefix recovery keeps the exact native frontier shape");
            frontiers.emplace_back(frontier->tasks.begin(),
                frontier->tasks.end());
            if (calls == 1U) {
                require(payloads.size() == 3U,
                    "the first dispatch receives the complete compacted run");
                if (interruption == Interruption::decline)
                    return SchedulerBatchResult { };
                visited.push_back(payloads.front());
                if (interruption == Interruption::stop) {
                    current.request_stop();
                    return SchedulerBatchResult { 1U, { } };
                }
                return SchedulerBatchResult { 1U,
                    std::make_exception_ptr(
                        std::runtime_error("ticketed wave failure")) };
            }
            visited.insert(visited.end(), payloads.begin(), payloads.end());
            return SchedulerBatchResult { payloads.size(), { } };
        });
        const SchedulerBatchGroupKey group_key { 9U, 4U };
        for (std::uint64_t payload = 1U; payload <= 3U; ++payload) {
            scheduler.schedule_systemverilog_group_batchable(
                SchedulerPhase::active, payload, batch, payload,
                [&, payload](Scheduler&) {
                    ++fallback_calls;
                    visited.push_back(payload);
                },
                group_key);
        }

        bool failed { };
        try {
            const auto first = scheduler.run();
            require(first.status == (interruption == Interruption::stop
                            ? RunStatus::stopped : RunStatus::completed),
                "composite ticket reports stop and decline correctly");
        } catch (const std::runtime_error& error) {
            failed = std::string_view { error.what() }
                == "ticketed wave failure";
            if (!failed)
                throw;
        }
        require(failed == (interruption == Interruption::failure),
            "only the requested composite batch failure escapes");
        if (interruption != Interruption::decline) {
            scheduler.clear_stop();
            require(scheduler.run().status == RunStatus::completed,
                "accepted-prefix suffix resumes after stop or failure");
        }
        require(visited == std::vector<std::uint64_t> { 1U, 2U, 3U },
            "decline, stop, and failure preserve exactly-once suffix order");
        require(fallback_calls
                    == (interruption == Interruption::decline ? 1U : 0U)
                && offered.size() == 2U
                && offered[0] == std::vector<std::uint64_t> { 1U, 2U, 3U }
                && offered[1] == std::vector<std::uint64_t> { 2U, 3U }
                && frontiers.size() == 2U
                && frontiers[0][0].stable_order == 1U
                && frontiers[0][0].sequence == 0U
                && frontiers[0][2].stable_order == 3U
                && frontiers[0][2].sequence == 2U
                && frontiers[1][0].stable_order == 2U
                && frontiers[1][0].sequence == 1U
                && frontiers[1][1].stable_order == 3U
                && frontiers[1][1].sequence == 2U,
            "the first fallback or accepted prefix is not replayed on suffix dispatch");
        const auto compacted = scheduler.systemverilog_batch_compaction_stats();
        require(compacted.tickets == 1U && compacted.members == 3U
                && compacted.entries_elided == 2U
                && compacted.direct_dispatches == 2U
                && compacted.direct_members == 5U,
            "prefix recovery does not lose ticket accounting");
    }
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

void test_sv_contiguous_batches()
{
    Scheduler scheduler;
    std::string events;
    std::vector<std::vector<std::uint64_t>> batches;
    WaveBatch* bound_batch { };
    WaveBatch batch([&](Scheduler& current, std::span<const std::uint64_t> payloads) {
        require(!current.current_generic_batch_frontier(),
            "SystemVerilog callbacks do not inherit generic delta frontiers");
        require(current.delta() == 0, "SV batch must preserve the generic cycle");
        batches.emplace_back(payloads.begin(), payloads.end());
        for (const auto payload : payloads) {
            events += static_cast<char>('a' + payload - 1U);
            if (payload == 1U) {
                current.schedule_systemverilog_batchable(SchedulerPhase::active,
                    0U, *bound_batch, 4U, [](Scheduler&) {
                        throw std::runtime_error("unexpected native wave fallback");
                    });
            }
        }
        require(current.systemverilog_round() == (payloads.front() == 4U ? 2U : 1U),
            "new work must not join the frozen SV batch");
        return fsim::runtime::SchedulerBatchResult { payloads.size(), { } };
    });
    bound_batch = &batch;
    for (std::uint64_t payload = 1U; payload <= 3U; ++payload) {
        scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
            payload * 2U, batch, payload, [](Scheduler&) {
                throw std::runtime_error("unexpected native wave fallback");
            });
    }
    scheduler.schedule_systemverilog(SchedulerPhase::active, 5U,
        [&](Scheduler&) { events += 'x'; });
    scheduler.schedule_systemverilog(SchedulerPhase::inactive, 0U,
        [&](Scheduler&) { events += 'i'; });
    scheduler.schedule_systemverilog(SchedulerPhase::update, 0U,
        [&](Scheduler&) { events += 'n'; });
    require(scheduler.run().status == RunStatus::completed,
        "SV contiguous batch fixture completes");
    require(events == "abxcdin"
            && batches == std::vector<std::vector<std::uint64_t>> { { 1U, 2U }, { 3U }, { 4U } },
        "batching preserves external callbacks, frozen rounds and region priority");
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
    test_generic_batch_chunks_preserve_foreign_order();
    test_sv_systemverilog_key_receipts();
    test_sv_native_frontier_key_handoff();
    test_sv_native_frontier_cross_owner_enqueue_order();
    test_compact_reservation_declines_at_delta_limit();
    test_sv_contiguous_batches();
    test_sv_compact_ordered_batch_ticket();
    test_sv_ordered_private_update_ticket();
    test_sv_composite_ticket_virtual_order();
    test_sv_composite_ticket_foreign_split();
    test_sv_readiness_tickets_interleave_without_crossing_foreign_writer();
    test_sv_readiness_ticket_ordering_and_lifecycle();
    test_sv_readiness_ticket_failure_and_large_group();
    test_sv_readiness_ticket_recycles_after_batch_gather();
    test_sv_grouped_readiness_above_sixty_four_preserves_frontiers();
    test_sv_readiness_ticket_pool_capacity_growth();
    test_sv_readiness_ticket_pool_growth_refuses_queued_storage();
    test_sv_composite_ticket_late_observation_decline();
    test_sv_composite_ticket_group_boundary();
    test_sv_composite_ticket_preserves_update_phase();
    test_sv_composite_ticket_prefix_recovery();
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
    test_native_batch_capacity_ticket();
    test_generic_frontier_projected_update_ticket();
    test_generic_frontier_ticket_cancel_and_expiry();
}

} // namespace fsim::tests::runtime

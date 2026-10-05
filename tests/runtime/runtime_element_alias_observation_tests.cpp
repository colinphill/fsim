// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Focused phase and reentrancy witnesses for retained aggregate authority.
namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct AliasFamily {
    Interpreter interpreter;
    std::size_t width;
    SignalId first;
    SignalId second;
    SignalId aggregate;
    ContainerObjectId object;

    AliasFamily(const std::size_t element_width,
        const ResolutionKind resolution,
        const bool second_one = false)
        : width(element_width)
        , first(interpreter.add_signal({
              "words[1]", PackedLogic4(width, Logic4::zero), resolution }))
        , second(interpreter.add_signal({
              "words[0]",
              PackedLogic4(width, second_one ? Logic4::one : Logic4::zero),
              resolution }))
        , aggregate(interpreter.add_signal({
              "words",
              PackedLogic4::from_msb_string(
                  std::string(width, '0')
                  + std::string(width, second_one ? '1' : '0')),
              resolution }))
    {
        ContainerType type;
        type.fixed = true;
        type.element_width = static_cast<std::uint32_t>(width);
        type.index_left = 1;
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        object = interpreter.add_container_object({
            "words",
            ContainerValue { type,
                { PackedLogic4(width, Logic4::zero),
                    PackedLogic4(width,
                        second_one ? Logic4::one : Logic4::zero) }, { } },
            std::nullopt });
        interpreter.add_container_element_signal_alias(
            { object, 0U, first, true, true });
        interpreter.add_container_element_signal_alias(
            { object, 1U, second, true, true });
        interpreter.add_container_aggregate_signal_alias(
            { object, aggregate, true, true });
    }
};

struct FamilyTransactionTrace {
    std::array<SignalId, 3U> signals;
    std::array<std::size_t, 3U> counts { };

    static void record(
        void* context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<FamilyTransactionTrace*>(context);
        if (entry.kind != SchedulerTraceKind::signal_transaction) {
            return;
        }
        for (std::size_t index = 0U;
            index < trace.signals.size();
            ++index) {
            if (entry.signal == trace.signals[index]) {
                ++trace.counts[index];
                return;
            }
        }
    }
};

struct FamilyTransactionSnapshotTrace {
    struct Snapshot {
        SimulationTick time { };
        SignalId notified { };
        std::array<SimulationTick, 3U> last_active { };
        std::array<SimulationTick, 3U> last_event { };
        bool current_values_coherent { };
    };

    std::array<SignalId, 3U> signals;
    std::array<std::size_t, 3U> counts { };
    std::array<std::array<std::size_t, 3U>, 2U> counts_by_time { };
    std::array<Snapshot, 6U> snapshots { };
    std::size_t snapshot_count { };
    ProcessExecutionContext* active_context { };
    bool query_failed { };

    static void record(
        void* context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<FamilyTransactionSnapshotTrace*>(context);
        if (entry.kind != SchedulerTraceKind::signal_transaction) {
            return;
        }
        std::size_t notified_index = trace.signals.size();
        for (std::size_t index = 0U;
            index < trace.signals.size();
            ++index) {
            if (entry.signal == trace.signals[index]) {
                notified_index = index;
                ++trace.counts[index];
                if (entry.time == 1U || entry.time == 2U) {
                    ++trace.counts_by_time[
                        static_cast<std::size_t>(entry.time - 1U)][index];
                } else {
                    trace.query_failed = true;
                }
                break;
            }
        }
        if (notified_index == trace.signals.size()) {
            return;
        }
        if (trace.active_context == nullptr
            || trace.snapshot_count >= trace.snapshots.size()) {
            trace.query_failed = true;
            return;
        }

        auto& snapshot = trace.snapshots[trace.snapshot_count++];
        snapshot.time = entry.time;
        snapshot.notified = entry.signal;
        try {
            constexpr auto element_width = std::size_t { 4U };
            constexpr auto aggregate_width = element_width * 2U;
            const std::array<Logic4Word, 3U> expected_values {
                Logic4Word { element_width, entry.time == 1U ? 0U : 0x0fU, 0U },
                Logic4Word { element_width, 0x0fU, 0U },
                Logic4Word { aggregate_width, entry.time == 1U ? 0x0fU : 0xffU, 0U },
            };
            snapshot.current_values_coherent = true;
            for (std::size_t index = 0U;
                index < trace.signals.size();
                ++index) {
                snapshot.last_active[index]
                    = trace.active_context->signal_last_active(
                        trace.signals[index]);
                snapshot.last_event[index]
                    = trace.active_context->signal_last_event(
                        trace.signals[index]);
                snapshot.current_values_coherent
                    = snapshot.current_values_coherent
                    && trace.active_context->read_signal_word(
                        trace.signals[index]) == expected_values[index];
            }
        } catch (...) {
            trace.query_failed = true;
        }
    }
};

class ScopedExecutionContext final {
public:
    ScopedExecutionContext(
        FamilyTransactionSnapshotTrace& trace,
        ProcessExecutionContext& context)
        : trace_ { trace }
    {
        trace_.active_context = &context;
    }

    ~ScopedExecutionContext()
    {
        trace_.active_context = nullptr;
    }

    ScopedExecutionContext(const ScopedExecutionContext&) = delete;
    ScopedExecutionContext& operator=(const ScopedExecutionContext&) = delete;

private:
    FamilyTransactionSnapshotTrace& trace_;
};

class WholeDepositExecutor final : public ProcessExecutor {
public:
    WholeDepositExecutor(
        Interpreter& interpreter,
        const SignalId aggregate,
        PackedLogic4 value,
        FamilyTransactionSnapshotTrace& trace)
        : interpreter_ { interpreter }
        , aggregate_ { aggregate }
        , value_ { std::move(value) }
        , trace_ { trace }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start == 0U) {
            return { 0U, 1U };
        }
        if (start == 1U || start == 2U) {
            ScopedExecutionContext bind_context { trace_, context };
            interpreter_.deposit_signal(aggregate_, value_);
            return { start, start + 1U };
        }
        throw std::logic_error { "unexpected whole-deposit resume PC" };
    }

private:
    Interpreter& interpreter_;
    SignalId aggregate_ { };
    PackedLogic4 value_;
    FamilyTransactionSnapshotTrace& trace_;
};

struct WholeWriteTrace {
    AliasFamily& family;
    PackedLogic4 expected_element;
    PackedLogic4 expected_aggregate;
    std::size_t observations { };
    bool consistent { true };
    bool query_failed { };

    static void record(void* context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<WholeWriteTrace*>(context);
        const auto& family = trace.family;
        if (entry.kind != SchedulerTraceKind::signal_change
            || (entry.signal != family.first
                && entry.signal != family.second
                && entry.signal != family.aggregate)) {
            return;
        }
        ++trace.observations;
        try {
            const auto& interpreter = family.interpreter;
            const auto& object = interpreter.container_object_value(family.object);
            trace.consistent = trace.consistent
                && interpreter.signal_value(family.first) == trace.expected_element
                && interpreter.signal_value(family.second) == trace.expected_element
                && interpreter.signal_value(family.aggregate) == trace.expected_aggregate
                && interpreter.stored_signal_value(family.aggregate)
                    == trace.expected_aggregate
                && object.elements.size() == 2U
                && object.elements[0] == trace.expected_element
                && object.elements[1] == trace.expected_element;
        } catch (...) {
            trace.query_failed = true;
        }
    }
};

void check_late_trace_whole_write(const std::size_t width,
    const ResolutionKind resolution)
{
    AliasFamily family(width, resolution);
    family.interpreter.start();
    WholeWriteTrace trace {
        family, PackedLogic4(width, Logic4::one),
        PackedLogic4(width * 2U, Logic4::one) };
    // This public path cannot rely on an application-only preparatory call.
    family.interpreter.scheduler().set_trace_hook(&trace, &WholeWriteTrace::record);
    family.interpreter.deposit_signal(family.aggregate, trace.expected_aggregate);
    family.interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(trace.observations != 0U && trace.consistent && !trace.query_failed,
        "late read-only trace queries must see the complete logical whole write");
}

void check_late_stored_observer(const std::size_t width,
    const ResolutionKind resolution)
{
    AliasFamily family(width, resolution);
    family.interpreter.start();
    const PackedLogic4 expected_element(width, Logic4::one);
    const PackedLogic4 expected_aggregate(width * 2U, Logic4::one);
    const PackedLogic4 previous_element(width, Logic4::zero);
    const PackedLogic4 previous_aggregate(width * 2U, Logic4::zero);
    std::size_t observations { };
    bool consistent = true;
    family.interpreter.set_stored_signal_change_hook(
        [&](const SignalId signal, const SimulationTick) {
            if (signal != family.first && signal != family.second
                && signal != family.aggregate) {
                return;
            }
            ++observations;
            consistent = consistent
                && family.interpreter.stored_signal_value(family.first)
                    == expected_element
                && family.interpreter.stored_signal_value(family.second)
                    == expected_element
                && family.interpreter.stored_signal_value(family.aggregate)
                    == expected_aggregate
                && family.interpreter.signal_value(family.first) == previous_element
                && family.interpreter.signal_value(family.second) == previous_element
                && family.interpreter.signal_value(family.aggregate) == previous_aggregate;
        });
    // A supported observer must not turn a legal whole write into an exception.
    family.interpreter.deposit_signal(family.aggregate, expected_aggregate);
    require(observations != 0U && consistent,
        "stored observers must see the complete new stored value before current publication");
}

void check_pending_and_independent_history(const std::size_t width,
    const ResolutionKind resolution)
{
    AliasFamily family(width, resolution);
    const auto first_last = family.interpreter.add_signal(
        { "first_last", PackedLogic4(width, Logic4::x) });
    const auto second_last = family.interpreter.add_signal(
        { "second_last", PackedLogic4(width, Logic4::x) });
    const auto aggregate_last = family.interpreter.add_signal(
        { "aggregate_last", PackedLogic4(width * 2U, Logic4::x) });
    Process observer;
    observer.id = 0U;
    observer.name = "independent_leaf_history";
    observer.register_count = 3U;
    observer.operations = {
        WaitFor { 3U },
        SignalLastValue { 0U, family.first },
        SignalLastValue { 1U, family.second },
        SignalLastValue { 2U, family.aggregate },
        WriteBlocking { first_last, 0U },
        WriteBlocking { second_last, 1U },
        WriteBlocking { aggregate_last, 2U },
        Halt { },
    };
    (void)family.interpreter.add_process(std::move(observer));
    family.interpreter.start();
    family.interpreter.schedule_signal_at(
        family.first, PackedLogic4(width, Logic4::zero), 2U);
    family.interpreter.prepare_signal_observation(family.aggregate);
    const PackedLogic4 ones(width, Logic4::one);
    const PackedLogic4 zeros(width, Logic4::zero);
    const PackedLogic4 whole_ones(width * 2U, Logic4::one);
    family.interpreter.deposit_signal(family.aggregate, whole_ones);
    require(family.interpreter.signal_value(family.first) == ones,
        "preparing observation must not mature the pending element update");
    const auto result = family.interpreter.run();
    require(result.status == RunStatus::completed && result.time == 3U
            && family.interpreter.signal_value(family.first) == zeros
            && family.interpreter.signal_value(family.second) == ones
            && family.interpreter.signal_value(first_last) == ones
            && family.interpreter.signal_value(second_last) == zeros
            && family.interpreter.signal_value(aggregate_last) == whole_ones,
        "pending leaf updates preserve scheduling and independent leaf last values");
}


void check_reentrant_whole_family(const std::size_t width,
    const bool reenter_from_stored_hook)
{
    AliasFamily family(width, ResolutionKind::sv_wire);
    const auto first_last = family.interpreter.add_signal(
        { "nested_first_last", PackedLogic4(width, Logic4::x) });
    const auto second_last = family.interpreter.add_signal(
        { "nested_second_last", PackedLogic4(width, Logic4::x) });
    const auto aggregate_last = family.interpreter.add_signal(
        { "nested_aggregate_last", PackedLogic4(width * 2U, Logic4::x) });
    Process observer;
    observer.id = 0U;
    observer.name = "nested_family_last_values";
    observer.register_count = 3U;
    observer.operations = {
        WaitFor { 1U },
        SignalLastValue { 0U, family.first },
        SignalLastValue { 1U, family.second },
        SignalLastValue { 2U, family.aggregate },
        WriteBlocking { first_last, 0U },
        WriteBlocking { second_last, 1U },
        WriteBlocking { aggregate_last, 2U },
        Halt { },
    };
    (void)family.interpreter.add_process(std::move(observer));
    family.interpreter.start();
    const auto zero = std::string(width * 2U, '0');
    const auto outer = std::string(width, '1') + std::string(width, '0');
    const auto inner = std::string(width, '0') + std::string(width, '1');
    std::vector<std::pair<std::string, std::string>> stored_snapshots;
    std::vector<std::pair<std::string, std::string>> current_snapshots;
    bool reentered { };
    bool coherent = true;
    const auto inspect_family = [&] {
        const auto current
            = family.interpreter.signal_value(family.aggregate).to_msb_string();
        const auto stored = family.interpreter.stored_signal_value(
            family.aggregate).to_msb_string();
        const auto& object
            = family.interpreter.container_object_value(family.object);
        coherent = coherent
            && family.interpreter.signal_value(family.first).to_msb_string()
                == current.substr(0U, width)
            && family.interpreter.signal_value(family.second).to_msb_string()
                == current.substr(width)
            && family.interpreter.stored_signal_value(family.first).to_msb_string()
                == stored.substr(0U, width)
            && family.interpreter.stored_signal_value(family.second).to_msb_string()
                == stored.substr(width)
            && object.elements[0].to_msb_string() == current.substr(0U, width)
            && object.elements[1].to_msb_string() == current.substr(width);
        return std::pair { stored, current };
    };
    family.interpreter.set_stored_signal_change_hook(
        [&](const SignalId signal, const SimulationTick) {
            if (signal != family.aggregate) {
                return;
            }
            stored_snapshots.push_back(inspect_family());
            if (reenter_from_stored_hook && !reentered) {
                reentered = true;
                family.interpreter.deposit_signal(
                    family.aggregate, PackedLogic4::from_msb_string(inner));
            }
        });
    family.interpreter.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4&, const SimulationTick) {
            if (signal != family.aggregate) {
                return;
            }
            current_snapshots.push_back(inspect_family());
            if (!reenter_from_stored_hook && !reentered) {
                reentered = true;
                family.interpreter.deposit_signal(
                    family.aggregate, PackedLogic4::from_msb_string(inner));
            }
        });
    family.interpreter.deposit_signal(
        family.aggregate, PackedLogic4::from_msb_string(outer));
    const auto final = inspect_family();
    const auto expected_stored
        = std::vector<std::pair<std::string, std::string>> {
              { outer, zero },
              { inner, reenter_from_stored_hook ? zero : outer },
          };
    const auto expected_current = reenter_from_stored_hook
        ? std::vector<std::pair<std::string, std::string>> {
              { inner, inner }, { inner, outer } }
        : std::vector<std::pair<std::string, std::string>> {
              { outer, outer }, { inner, inner } };
    require(reentered && coherent && stored_snapshots == expected_stored
            && current_snapshots == expected_current
            && final.first == inner
            && final.second == (reenter_from_stored_hook ? outer : inner),
        "reentrant whole-family writes preserve stored/current callback phases "
        "and never expose partial leaf state");
    const auto result = family.interpreter.run();
    const auto expected_previous = reenter_from_stored_hook ? inner : outer;
    require(result.status == RunStatus::completed && result.time == 1U
            && family.interpreter.signal_value(first_last).to_msb_string()
                == expected_previous.substr(0U, width)
            && family.interpreter.signal_value(second_last).to_msb_string()
                == expected_previous.substr(width)
            && family.interpreter.signal_value(aggregate_last).to_msb_string()
                == expected_previous,
        "nested whole-family LAST values use the live previous current phase");
}

void check_transaction_waiters_for_whole_write(const std::size_t width)
{
    AliasFamily family(width, ResolutionKind::sv_wire, true);
    const auto first_count = family.interpreter.add_signal(
        { "first_transaction_count", PackedLogic4(2U, Logic4::zero) });
    const auto second_count = family.interpreter.add_signal(
        { "second_transaction_count", PackedLogic4(2U, Logic4::zero) });
    const auto aggregate_count = family.interpreter.add_signal(
        { "aggregate_transaction_count", PackedLogic4(2U, Logic4::zero) });

    const auto add_waiter = [&](const ProcessId id,
                                const SignalId watched,
                                const SignalId counter,
                                const char* name) {
        Process waiter;
        waiter.id = id;
        waiter.name = name;
        waiter.register_count = 3U;
        waiter.static_sensitivity = { { watched, EdgeKind::transaction } };
        waiter.operations = {
            WaitSensitivity { },
            ReadSignal { 0U, counter },
            LoadConstant { 1U, PackedLogic4::from_msb_string("01") },
            Binary { BinaryOperator::add_unsigned, 2U, 0U, 1U },
            WriteBlocking { counter, 2U },
            Jump { 0U },
        };
        (void)family.interpreter.add_process(std::move(waiter));
    };
    add_waiter(0U, family.first, first_count, "first_transaction_waiter");
    add_waiter(1U, family.second, second_count, "second_transaction_waiter");
    add_waiter(2U, family.aggregate, aggregate_count,
        "aggregate_transaction_waiter");

    family.interpreter.start();
    (void)family.interpreter.run();
    FamilyTransactionTrace transaction_trace {
        { family.first, family.second, family.aggregate } };
    family.interpreter.scheduler().set_trace_hook(
        &transaction_trace, &FamilyTransactionTrace::record);
    const auto ones = PackedLogic4::from_msb_string(
        std::string(width * 2U, '1'));
    family.interpreter.deposit_signal(family.aggregate, ones);
    (void)family.interpreter.run();
    family.interpreter.deposit_signal(family.aggregate, ones);
    (void)family.interpreter.run();
    family.interpreter.scheduler().set_trace_hook(nullptr, nullptr);

    const auto expected = PackedLogic4::from_msb_string("10");
    const auto element_ones = PackedLogic4(width, Logic4::one);
    require(family.interpreter.signal_value(first_count) == expected
            && family.interpreter.signal_value(second_count) == expected
            && family.interpreter.signal_value(aggregate_count) == expected
            && transaction_trace.counts
                == std::array<std::size_t, 3U> { 2U, 2U, 2U }
            && family.interpreter.signal_value(family.first) == element_ones
            && family.interpreter.signal_value(family.second) == element_ones
            && family.interpreter.signal_value(family.aggregate) == ones,
        "whole deposits notify changed and unchanged leaf transaction waiters "
        "and the aggregate waiter exactly once per operation");
}

void check_transaction_trace_observes_family_stamps()
{
    constexpr auto width = std::size_t { 4U };
    constexpr auto never = std::numeric_limits<SimulationTick>::max();
    AliasFamily family(width, ResolutionKind::sv_wire, true);
    auto trace = FamilyTransactionSnapshotTrace {
        { family.first, family.second, family.aggregate } };

    Process writer;
    writer.id = 0U;
    writer.name = "transaction_stamp_family_writer";
    writer.operations = { WaitFor { 1U }, WaitFor { 1U }, Halt { } };
    const auto process = family.interpreter.add_process(std::move(writer));
    family.interpreter.set_process_executor(
        process,
        std::make_unique<WholeDepositExecutor>(
            family.interpreter,
            family.aggregate,
            PackedLogic4::from_msb_string("11111111"),
            trace));
    family.interpreter.scheduler().set_trace_hook(
        &trace, &FamilyTransactionSnapshotTrace::record);
    const auto result = family.interpreter.run();
    family.interpreter.scheduler().set_trace_hook(nullptr, nullptr);

    const auto expected_active
        = std::array<SimulationTick, 3U> { 0U, 0U, 0U };
    // Transaction traces run before current/event publication, matching the
    // packed publish_normalized route. The first trace sees the initial family;
    // the repeated equal write sees the event from the preceding timestamp.
    const auto first_time_events
        = std::array<SimulationTick, 3U> { never, never, never };
    const auto second_time_events
        = std::array<SimulationTick, 3U> { 1U, never, 1U };
    const auto expected_counts_by_time
        = std::array<std::array<std::size_t, 3U>, 2U> {
              std::array<std::size_t, 3U> { 1U, 1U, 1U },
              std::array<std::size_t, 3U> { 1U, 1U, 1U },
          };
    bool timestamps_coherent = trace.snapshot_count == 6U;
    bool current_values_coherent = true;
    for (std::size_t index = 0U;
        index < trace.snapshot_count;
        ++index) {
        const auto& snapshot = trace.snapshots[index];
        const auto& expected_events = snapshot.time == 1U
            ? first_time_events : second_time_events;
        timestamps_coherent = timestamps_coherent
            && (snapshot.time == 1U || snapshot.time == 2U)
            && snapshot.last_active == expected_active
            && snapshot.last_event == expected_events;
        current_values_coherent = current_values_coherent
            && snapshot.current_values_coherent;
    }
    const auto ones = PackedLogic4(width, Logic4::one);
    const auto aggregate_ones = PackedLogic4(width * 2U, Logic4::one);
    require(result.status == RunStatus::completed && result.time == 2U
            && trace.counts
                == std::array<std::size_t, 3U> { 2U, 2U, 2U }
            && trace.counts_by_time == expected_counts_by_time
            && timestamps_coherent && current_values_coherent
            && !trace.query_failed
            && family.interpreter.signal_value(family.first) == ones
            && family.interpreter.signal_value(family.second) == ones
            && family.interpreter.signal_value(family.aggregate)
                == aggregate_ones,
        "transaction callbacks observe coherent old current/event state "
        "with the complete family's transaction stamps");
}


void check_stored_hook_failure_completes_family()
{
    constexpr auto width = std::size_t { 65U };
    AliasFamily family(width, ResolutionKind::sv_wire);
    family.interpreter.start();

    const auto element_ones = PackedLogic4(width, Logic4::one);
    const auto aggregate_ones = PackedLogic4(width * 2U, Logic4::one);
    const auto element_zeros = PackedLogic4(width, Logic4::zero);
    const auto aggregate_zeros = PackedLogic4(width * 2U, Logic4::zero);
    using Snapshot = std::array<std::string, 8U>;
    std::vector<Snapshot> snapshots;
    bool throw_once { };
    family.interpreter.set_stored_signal_change_hook(
        [&](const SignalId signal, const SimulationTick) {
            if (signal != family.first && signal != family.second
                && signal != family.aggregate) {
                return;
            }
            const auto& object
                = family.interpreter.container_object_value(family.object);
            snapshots.push_back({
                family.interpreter.stored_signal_value(family.aggregate)
                    .to_msb_string(),
                family.interpreter.signal_value(family.aggregate)
                    .to_msb_string(),
                family.interpreter.stored_signal_value(family.first)
                    .to_msb_string(),
                family.interpreter.stored_signal_value(family.second)
                    .to_msb_string(),
                family.interpreter.signal_value(family.first)
                    .to_msb_string(),
                family.interpreter.signal_value(family.second)
                    .to_msb_string(),
                object.elements[0].to_msb_string(),
                object.elements[1].to_msb_string(),
            });
            if (signal == family.first && !throw_once) {
                throw_once = true;
                throw std::runtime_error { "stored observer failure" };
            }
        });

    bool propagated { };
    try {
        family.interpreter.deposit_signal(family.aggregate, aggregate_ones);
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    family.interpreter.set_stored_signal_change_hook({ });

    const Snapshot expected_snapshot {
        aggregate_ones.to_msb_string(),
        aggregate_zeros.to_msb_string(),
        element_ones.to_msb_string(),
        element_ones.to_msb_string(),
        element_zeros.to_msb_string(),
        element_zeros.to_msb_string(),
        element_zeros.to_msb_string(),
        element_zeros.to_msb_string(),
    };
    require(
        propagated && throw_once
            && snapshots == std::vector<Snapshot> {
                expected_snapshot, expected_snapshot, expected_snapshot }
            && family.interpreter.stored_signal_value(family.first)
                == element_ones
            && family.interpreter.stored_signal_value(family.second)
                == element_ones
            && family.interpreter.stored_signal_value(family.aggregate)
                == aggregate_ones
            && family.interpreter.signal_value(family.first) == element_ones
            && family.interpreter.signal_value(family.second) == element_ones
            && family.interpreter.signal_value(family.aggregate)
                == aggregate_ones
            && family.interpreter.container_object_value(family.object)
                    .elements
                == std::vector<PackedLogic4> {
                    element_ones, element_ones },
        "stored observer failures rethrow only after every family phase is coherent");

    family.interpreter.deposit_signal(family.aggregate, aggregate_zeros);
    require(
        family.interpreter.stored_signal_value(family.aggregate)
                == aggregate_zeros
            && family.interpreter.signal_value(family.aggregate)
                == aggregate_zeros
            && family.interpreter.container_object_value(family.object)
                    .elements
                == std::vector<PackedLogic4> {
                    element_zeros, element_zeros },
        "stored observer failure cleanup leaves later whole writes usable");
}

enum class ForceReleaseStage : std::uint8_t {
    force_whole,
    release_whole,
    force_partial,
    release_partial,
};

struct ForceReleaseSnapshot {
    ForceReleaseStage stage { };
    SignalId notified { };
    std::array<PackedLogic4, 3U> current;
    std::array<PackedLogic4, 3U> last;
    std::array<PackedLogic4, 3U> stored;
    std::array<SimulationTick, 3U> last_active { };
    std::array<SimulationTick, 3U> last_event { };
    std::size_t metadata_count { };
};

struct ForceReleaseResult {
    std::vector<ForceReleaseSnapshot> snapshots;
    std::array<std::array<std::size_t, 3U>, 4U> transaction_counts { };
};

struct ForceReleaseTrace {
    Interpreter* interpreter { };
    SignalId observed_signal { };
    std::array<SignalId, 3U> observed_family { };
    std::size_t observed_family_count { };
    std::array<std::array<std::size_t, 3U>, 4U> transaction_counts { };
    ProcessExecutionContext* active_context { };
    ForceReleaseStage stage { };
    bool query_failed { };
    std::vector<ForceReleaseSnapshot> snapshots;

    [[nodiscard]] bool observes(const SignalId signal) const noexcept
    {
        if (signal == observed_signal) {
            return true;
        }
        for (std::size_t index = 0U;
            index < observed_family_count;
            ++index) {
            if (signal == observed_family[index]) {
                return true;
            }
        }
        return false;
    }

    void record(const SignalId signal) noexcept
    {
        if (!observes(signal)) {
            return;
        }
        if (interpreter == nullptr || active_context == nullptr) {
            query_failed = true;
            return;
        }
        try {
            const auto metadata_count = observed_family_count == 0U
                ? 1U : observed_family_count;
            ForceReleaseSnapshot snapshot;
            snapshot.stage = stage;
            snapshot.notified = signal;
            snapshot.metadata_count = metadata_count;
            for (std::size_t index = 0U;
                index < metadata_count;
                ++index) {
                const auto metadata_signal = observed_family_count == 0U
                    ? observed_signal : observed_family[index];
                snapshot.current[index]
                    = interpreter->signal_value(metadata_signal);
                const auto last_word
                    = active_context->signal_last_value_word(metadata_signal);
                snapshot.last[index] = PackedLogic4::from_aval_bval(
                    last_word.width, last_word.aval, last_word.bval);
                snapshot.stored[index]
                    = interpreter->stored_signal_value(metadata_signal);
                snapshot.last_active[index]
                    = active_context->signal_last_active(metadata_signal);
                snapshot.last_event[index]
                    = active_context->signal_last_event(metadata_signal);
            }
            snapshots.push_back(std::move(snapshot));
        } catch (...) {
            query_failed = true;
        }
    }

    static void record_transaction(
        void* context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<ForceReleaseTrace*>(context);
        if (entry.kind != SchedulerTraceKind::signal_transaction) {
            return;
        }
        const auto stage_index = static_cast<std::size_t>(trace.stage);
        if (stage_index >= trace.transaction_counts.size()) {
            trace.query_failed = true;
            return;
        }
        for (std::size_t index = 0U;
            index < trace.observed_family_count;
            ++index) {
            if (entry.signal == trace.observed_family[index]) {
                ++trace.transaction_counts[stage_index][index];
                return;
            }
        }
        if (trace.observed_family_count == 0U
            && entry.signal == trace.observed_signal) {
            ++trace.transaction_counts[stage_index][0U];
        }
    }
};

class ScopedForceReleaseContext final {
public:
    ScopedForceReleaseContext(
        ForceReleaseTrace& trace,
        ProcessExecutionContext& context)
        : trace_ { trace }
    {
        trace_.active_context = &context;
    }

    ~ScopedForceReleaseContext()
    {
        trace_.active_context = nullptr;
    }

    ScopedForceReleaseContext(const ScopedForceReleaseContext&) = delete;
    ScopedForceReleaseContext& operator=(
        const ScopedForceReleaseContext&) = delete;

private:
    ForceReleaseTrace& trace_;
};

class ForceReleaseExecutor final : public ProcessExecutor {
public:
    ForceReleaseExecutor(
        Interpreter& interpreter,
        const SignalId signal,
        ForceReleaseTrace& trace)
        : interpreter_ { interpreter }
        , signal_ { signal }
        , trace_ { trace }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start == 0U) {
            return { 0U, 1U };
        }
        ScopedForceReleaseContext bind_context { trace_, context };
        const auto whole_ones = PackedLogic4::from_msb_string("11111111");
        const auto partial_value = PackedLogic4::from_msb_string("101010");
        if (start == 1U) {
            trace_.stage = ForceReleaseStage::force_whole;
            interpreter_.force_signal_slice(signal_, whole_ones, 0U);
        } else if (start == 2U) {
            trace_.stage = ForceReleaseStage::release_whole;
            interpreter_.release_signal_slice(signal_, 0U, 8U);
        } else if (start == 3U) {
            trace_.stage = ForceReleaseStage::force_partial;
            interpreter_.force_signal_slice(signal_, partial_value, 1U);
        } else if (start == 4U) {
            trace_.stage = ForceReleaseStage::release_partial;
            interpreter_.release_signal_slice(signal_, 1U, 6U);
        } else {
            throw std::logic_error {
                "unexpected force/release observer witness resume PC"
            };
        }
        return { start, start + 1U };
    }

private:
    Interpreter& interpreter_;
    SignalId signal_ { };
    ForceReleaseTrace& trace_;
};

ForceReleaseResult run_force_release_observation(
    Interpreter& interpreter,
    const SignalId signal,
    const std::array<SignalId, 3U> observed_family = { },
    const std::size_t observed_family_count = 0U)
{
    ForceReleaseTrace trace;
    trace.interpreter = &interpreter;
    trace.observed_signal = signal;
    trace.observed_family = observed_family;
    trace.observed_family_count = observed_family_count;
    interpreter.set_signal_change_hook(
        [&trace](const SignalId changed,
            const PackedLogic4&,
            const SimulationTick) {
            trace.record(changed);
        });
    interpreter.scheduler().set_trace_hook(
        &trace, &ForceReleaseTrace::record_transaction);

    Process writer;
    writer.id = 0U;
    writer.name = "aggregate_force_release_observation_writer";
    writer.operations = {
        WaitFor { 1U }, WaitFor { 1U }, WaitFor { 1U }, WaitFor { 1U },
        Halt { },
    };
    const auto process = interpreter.add_process(std::move(writer));
    interpreter.set_process_executor(
        process,
        std::make_unique<ForceReleaseExecutor>(
            interpreter, signal, trace));

    const auto result = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    interpreter.set_signal_change_hook({ });
    require(
        result.status == RunStatus::completed && result.time == 4U
            && !trace.query_failed,
        "force/release observer witness completes and reads its snapshots");
    return { std::move(trace.snapshots), trace.transaction_counts };
}

void check_force_release_observation_atomicity()
{
    constexpr auto element_width = std::size_t { 4U };
    const auto element_ones = PackedLogic4::from_msb_string("1111");
    const auto partial_first = PackedLogic4::from_msb_string("0101");
    const auto partial_second = PackedLogic4::from_msb_string("0100");
    const auto element_zeros = PackedLogic4::from_msb_string("0000");
    const auto zeros = PackedLogic4::from_msb_string("00000000");
    const auto whole_ones = PackedLogic4::from_msb_string("11111111");
    const auto partial_value = PackedLogic4::from_msb_string("01010100");
    const std::array<std::array<PackedLogic4, 3U>, 4U> alias_expected_current {
        std::array<PackedLogic4, 3U> {
            element_ones, element_ones, whole_ones },
        std::array<PackedLogic4, 3U> {
            element_zeros, element_zeros, zeros },
        std::array<PackedLogic4, 3U> {
            partial_first, partial_second, partial_value },
        std::array<PackedLogic4, 3U> {
            element_zeros, element_zeros, zeros },
    };
    const std::array<std::array<PackedLogic4, 3U>, 4U> alias_expected_last {
        std::array<PackedLogic4, 3U> {
            element_zeros, element_zeros, zeros },
        std::array<PackedLogic4, 3U> {
            element_ones, element_ones, whole_ones },
        std::array<PackedLogic4, 3U> {
            element_zeros, element_zeros, zeros },
        std::array<PackedLogic4, 3U> {
            partial_first, partial_second, partial_value },
    };
    const std::array<PackedLogic4, 3U> alias_expected_stored {
        element_zeros, element_zeros, zeros };
    const std::array<PackedLogic4, 4U> packed_expected_current {
        whole_ones, zeros, partial_value, zeros };
    const std::array<PackedLogic4, 4U> packed_expected_last {
        zeros, whole_ones, zeros, partial_value };

    AliasFamily alias(element_width, ResolutionKind::sv_wire);
    const auto alias_result = run_force_release_observation(
        alias.interpreter,
        alias.aggregate,
        { alias.first, alias.second, alias.aggregate },
        3U);
    const auto& alias_snapshots = alias_result.snapshots;

    Interpreter packed;
    const auto packed_signal = packed.add_signal({
        "words", zeros, ResolutionKind::sv_wire });
    const auto packed_result
        = run_force_release_observation(packed, packed_signal);
    const auto& packed_snapshots = packed_result.snapshots;

    const std::array<SignalId, 3U> alias_signals {
        alias.first, alias.second, alias.aggregate };
    std::array<std::array<std::size_t, 3U>, 4U> alias_notifications { };
    bool alias_notifications_known = true;
    for (const auto& snapshot : alias_snapshots) {
        const auto stage = static_cast<std::size_t>(snapshot.stage);
        std::size_t signal_index { };
        while (signal_index < 3U
            && snapshot.notified != alias_signals[signal_index]) {
            ++signal_index;
        }
        if (stage >= alias_notifications.size() || signal_index == 3U) {
            alias_notifications_known = false;
            continue;
        }
        ++alias_notifications[stage][signal_index];
    }

    const auto count_and_check = [&](
        const std::vector<ForceReleaseSnapshot>& snapshots,
        const std::array<PackedLogic4, 4U>& expected_current,
        const std::array<PackedLogic4, 4U>& expected_last,
        const std::array<std::array<PackedLogic4, 3U>, 4U>&
            expected_family_current,
        const std::array<std::array<PackedLogic4, 3U>, 4U>&
            expected_family_last,
        const std::array<PackedLogic4, 3U>& expected_family_stored,
        const std::size_t expected_metadata_count,
        const std::size_t expected_per_stage) {
        std::array<std::size_t, 4U> counts { };
        bool coherent = true;
        for (const auto& snapshot : snapshots) {
            const auto stage = static_cast<std::size_t>(snapshot.stage);
            if (stage >= counts.size()) {
                coherent = false;
                continue;
            }
            ++counts[stage];
            coherent = coherent
                && snapshot.metadata_count == expected_metadata_count
                && snapshot.last_active
                    == std::array<SimulationTick, 3U> { 0U, 0U, 0U }
                && snapshot.last_event
                    == std::array<SimulationTick, 3U> { 0U, 0U, 0U };
            for (std::size_t index = 0U;
                index < expected_metadata_count;
                ++index) {
                const auto expected_value = expected_metadata_count == 1U
                    ? expected_current[stage]
                    : expected_family_current[stage][index];
                const auto expected_previous = expected_metadata_count == 1U
                    ? expected_last[stage]
                    : expected_family_last[stage][index];
                const auto expected_stored = expected_metadata_count == 1U
                    ? zeros : expected_family_stored[index];
                coherent = coherent
                    && snapshot.current[index] == expected_value
                    && snapshot.last[index] == expected_previous
                    && snapshot.stored[index] == expected_stored;
            }
        }
        return coherent
            && counts == std::array<std::size_t, 4U> {
                   expected_per_stage, expected_per_stage,
                   expected_per_stage, expected_per_stage };
    };

    require(
        count_and_check(
            packed_snapshots,
            packed_expected_current,
            packed_expected_last,
            alias_expected_current,
            alias_expected_last,
            alias_expected_stored,
            1U,
            1U)
            && packed_snapshots.size() == 4U
            && packed_snapshots.front().metadata_count == 1U
            && std::ranges::all_of(
                packed_snapshots,
                [packed_signal](const ForceReleaseSnapshot& snapshot) {
                    return snapshot.notified == packed_signal;
                }),
        "packed reference observers see one complete current/LAST/stored "
        "state per force or release");
    if (!count_and_check(
            alias_snapshots,
            packed_expected_current,
            packed_expected_last,
            alias_expected_current,
            alias_expected_last,
            alias_expected_stored,
            3U,
            3U)
        || alias_snapshots.size() != 12U) {
        std::cerr << "alias snapshots=" << alias_snapshots.size()
                  << " notifications_known=" << alias_notifications_known
                  << '\n';
        for (const auto& snapshot : alias_snapshots) {
            std::cerr << "stage="
                      << static_cast<std::size_t>(snapshot.stage)
                      << " signal=" << snapshot.notified
                      << " metadata=" << snapshot.metadata_count;
            for (std::size_t index = 0U;
                index < snapshot.metadata_count; ++index) {
                std::cerr << " member=" << index
                          << " current="
                          << snapshot.current[index].to_msb_string()
                          << " last="
                          << snapshot.last[index].to_msb_string()
                          << " stored="
                          << snapshot.stored[index].to_msb_string()
                          << " active=" << snapshot.last_active[index]
                          << " event=" << snapshot.last_event[index];
            }
            std::cerr << '\n';
        }
    }
    require(
        count_and_check(
            alias_snapshots,
            packed_expected_current,
            packed_expected_last,
            alias_expected_current,
            alias_expected_last,
            alias_expected_stored,
            3U,
            3U)
            && alias_snapshots.size() == 12U
            && alias_snapshots.front().metadata_count == 3U
            && alias_notifications_known
            && alias_notifications
                == std::array<std::array<std::size_t, 3U>, 4U> {
                   std::array<std::size_t, 3U> { 1U, 1U, 1U },
                   std::array<std::size_t, 3U> { 1U, 1U, 1U },
                   std::array<std::size_t, 3U> { 1U, 1U, 1U },
                   std::array<std::size_t, 3U> { 1U, 1U, 1U },
                   }
            && alias_result.transaction_counts
                == std::array<std::array<std::size_t, 3U>, 4U> {
                       std::array<std::size_t, 3U> { 1U, 1U, 1U },
                       std::array<std::size_t, 3U> { 1U, 1U, 1U },
                       std::array<std::size_t, 3U> { 1U, 1U, 1U },
                       std::array<std::size_t, 3U> { 1U, 1U, 1U },
                   }
            && packed_result.transaction_counts
                == std::array<std::array<std::size_t, 3U>, 4U> {
                       std::array<std::size_t, 3U> { 1U, 0U, 0U },
                       std::array<std::size_t, 3U> { 1U, 0U, 0U },
                       std::array<std::size_t, 3U> { 1U, 0U, 0U },
                       std::array<std::size_t, 3U> { 1U, 0U, 0U },
                   },
        "all leaf and aggregate observers see complete family state during "
        "whole and cross-leaf partial force/release");
}

void check_force_release_object_hook_snapshots()
{
    AliasFamily alias(4U, ResolutionKind::sv_wire);
    std::vector<std::array<std::string, 4U>> snapshots;
    std::vector<SimulationTick> times;
    alias.interpreter.set_container_object_change_hook(
        [&](const ContainerObjectId object, const SimulationTick time) {
            require(object == alias.object,
                "force/release object hook retains the logical container");
            const auto& current
                = alias.interpreter.container_object_value(object);
            snapshots.push_back({
                current.elements[0U].to_msb_string(),
                current.elements[1U].to_msb_string(),
                alias.interpreter.signal_value(alias.aggregate)
                    .to_msb_string(),
                alias.interpreter.stored_signal_value(alias.aggregate)
                    .to_msb_string(),
            });
            times.push_back(time);
        });

    alias.interpreter.deposit_signal(
        alias.aggregate,
        PackedLogic4::from_msb_string("10101011"));
    alias.interpreter.force_signal(
        alias.aggregate,
        PackedLogic4::from_msb_string("01010100"));
    alias.interpreter.force_signal_slice(
        alias.aggregate,
        PackedLogic4::from_msb_string("01"), 3U);
    alias.interpreter.release_signal_slice(alias.aggregate, 3U, 2U);
    alias.interpreter.release_signal(alias.aggregate);

    const std::vector<std::array<std::string, 4U>> expected {
        { "1010", "1011", "10101011", "10101011" },
        { "0101", "0100", "01010100", "10101011" },
        { "0100", "1100", "01001100", "10101011" },
        { "1010", "1011", "10101011", "10101011" },
    };
    require(snapshots == expected
            && times == std::vector<SimulationTick> { 0U, 0U, 0U, 0U },
        "container object hooks publish each changed force/release current "
        "state once and skip a value-preserving partial release");
}

enum class ForceFollowupStage : std::uint8_t {
    release_unforced,
    force_unchanged_partial,
    release_unchanged_partial,
    force_changed_partial,
    release_changed_partial,
    release_unforced_again,
    reentrant_outer_force,
    reentrant_inner_force,
    reentrant_outer_complete,
    reentrant_release,
    reentrant_force_after_release,
    reentrant_final_release,
};

constexpr auto force_followup_stage_count = std::size_t { 12U };

struct ForceFollowupSnapshot {
    ForceFollowupStage stage { };
    SignalId notified { };
    std::size_t member_count { };
    std::array<PackedLogic4, 3U> current;
    std::array<PackedLogic4, 3U> last;
    std::array<PackedLogic4, 3U> stored;
    std::array<SimulationTick, 3U> last_active { };
    std::array<SimulationTick, 3U> last_event { };
};

struct ForceFollowupResult {
    std::vector<ForceFollowupSnapshot> operation_snapshots;
    std::vector<ForceFollowupSnapshot> change_snapshots;
    std::array<std::array<std::size_t, 3U>, force_followup_stage_count>
        transaction_counts { };
    std::array<std::array<std::size_t, 3U>, force_followup_stage_count>
        change_counts { };
    bool reentered { };
    bool callback_failed { };
};

struct ForceFollowupTrace {
    Interpreter* interpreter { };
    SignalId target { };
    std::array<SignalId, 3U> members { };
    std::size_t member_count { };
    ProcessExecutionContext* context { };
    ForceFollowupStage stage { };
    bool trigger_reentry { };
    bool reentered { };
    bool callback_failed { };
    std::vector<ForceFollowupSnapshot> operation_snapshots;
    std::vector<ForceFollowupSnapshot> change_snapshots;
    std::array<std::array<std::size_t, 3U>, force_followup_stage_count>
        transaction_counts { };
    std::array<std::array<std::size_t, 3U>, force_followup_stage_count>
        change_counts { };

    [[nodiscard]] std::size_t member_index(
        const SignalId signal) const noexcept
    {
        for (std::size_t index = 0U; index < member_count; ++index) {
            if (members[index] == signal) {
                return index;
            }
        }
        return member_count;
    }

    [[nodiscard]] ForceFollowupSnapshot capture(
        const SignalId notified) const
    {
        if (interpreter == nullptr || context == nullptr) {
            throw std::logic_error {
                "force/release follow-up context is not active"
            };
        }
        ForceFollowupSnapshot snapshot;
        snapshot.stage = stage;
        snapshot.notified = notified;
        snapshot.member_count = member_count;
        for (std::size_t index = 0U; index < member_count; ++index) {
            const auto signal = members[index];
            snapshot.current[index] = interpreter->signal_value(signal);
            const auto last = context->signal_last_value_word(signal);
            snapshot.last[index] = PackedLogic4::from_aval_bval(
                last.width, last.aval, last.bval);
            snapshot.stored[index]
                = interpreter->stored_signal_value(signal);
            snapshot.last_active[index]
                = context->signal_last_active(signal);
            snapshot.last_event[index] = context->signal_last_event(signal);
        }
        return snapshot;
    }

    void capture_operation() noexcept
    {
        try {
            operation_snapshots.push_back(capture(members[member_count - 1U]));
        } catch (...) {
            callback_failed = true;
        }
    }

    void record_change(const SignalId signal) noexcept
    {
        const auto index = member_index(signal);
        if (index == member_count) {
            return;
        }
        const auto stage_index = static_cast<std::size_t>(stage);
        if (stage_index >= force_followup_stage_count) {
            callback_failed = true;
            return;
        }
        ++change_counts[stage_index][index];
        try {
            change_snapshots.push_back(capture(signal));
        } catch (...) {
            callback_failed = true;
        }
    }

    static void record_transaction(
        void* context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<ForceFollowupTrace*>(context);
        if (entry.kind != SchedulerTraceKind::signal_transaction) {
            return;
        }
        const auto index = trace.member_index(entry.signal);
        if (index == trace.member_count) {
            return;
        }
        const auto stage_index = static_cast<std::size_t>(trace.stage);
        if (stage_index >= force_followup_stage_count) {
            trace.callback_failed = true;
            return;
        }
        ++trace.transaction_counts[stage_index][index];
        if (!trace.trigger_reentry || trace.reentered
            || trace.stage != ForceFollowupStage::reentrant_outer_force
            || index != 0U) {
            return;
        }

        trace.reentered = true;
        const auto outer_stage = trace.stage;
        trace.stage = ForceFollowupStage::reentrant_inner_force;
        try {
            trace.interpreter->force_signal_slice(
                trace.target,
                PackedLogic4::from_msb_string("00000000"),
                0U);
        } catch (...) {
            trace.callback_failed = true;
        }
        trace.stage = outer_stage;
    }
};

class ScopedForceFollowupContext final {
public:
    ScopedForceFollowupContext(
        ForceFollowupTrace& trace,
        ProcessExecutionContext& context)
        : trace_ { trace }
    {
        trace_.context = &context;
    }

    ~ScopedForceFollowupContext()
    {
        trace_.context = nullptr;
    }

    ScopedForceFollowupContext(const ScopedForceFollowupContext&) = delete;
    ScopedForceFollowupContext& operator=(
        const ScopedForceFollowupContext&) = delete;

private:
    ForceFollowupTrace& trace_;
};

class ForceTransactionEdgeExecutor final : public ProcessExecutor {
public:
    ForceTransactionEdgeExecutor(
        Interpreter& interpreter,
        const SignalId target,
        ForceFollowupTrace& trace)
        : interpreter_ { interpreter }
        , target_ { target }
        , trace_ { trace }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start == 0U) {
            return { 0U, 1U };
        }
        if (start > 6U) {
            throw std::logic_error {
                "unexpected force transaction edge resume PC"
            };
        }
        ScopedForceFollowupContext bind_context { trace_, context };
        const auto stage = static_cast<ForceFollowupStage>(start - 1U);
        trace_.stage = stage;
        if (start == 1U || start == 6U) {
            interpreter_.release_signal_slice(target_, 0U, 8U);
        } else if (start == 2U) {
            interpreter_.force_signal_slice(
                target_, PackedLogic4::from_msb_string("00"), 0U);
        } else if (start == 3U) {
            interpreter_.release_signal_slice(target_, 0U, 2U);
        } else if (start == 4U) {
            interpreter_.force_signal_slice(
                target_, PackedLogic4::from_msb_string("11"), 0U);
        } else {
            interpreter_.release_signal_slice(target_, 0U, 2U);
        }
        trace_.capture_operation();
        return { start, start + 1U };
    }

private:
    Interpreter& interpreter_;
    SignalId target_ { };
    ForceFollowupTrace& trace_;
};

class ForceTraceReentryExecutor final : public ProcessExecutor {
public:
    ForceTraceReentryExecutor(
        Interpreter& interpreter,
        const SignalId target,
        ForceFollowupTrace& trace)
        : interpreter_ { interpreter }
        , target_ { target }
        , trace_ { trace }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start == 0U) {
            return { 0U, 1U };
        }
        if (start > 4U) {
            throw std::logic_error {
                "unexpected force trace reentry resume PC"
            };
        }
        ScopedForceFollowupContext bind_context { trace_, context };
        if (start == 1U) {
            trace_.stage = ForceFollowupStage::reentrant_outer_force;
            interpreter_.force_signal_slice(
                target_, PackedLogic4::from_msb_string("11111111"), 0U);
            trace_.stage = ForceFollowupStage::reentrant_outer_complete;
        } else if (start == 2U) {
            trace_.stage = ForceFollowupStage::reentrant_release;
            interpreter_.release_signal_slice(target_, 0U, 8U);
        } else if (start == 3U) {
            trace_.stage = ForceFollowupStage::reentrant_force_after_release;
            interpreter_.force_signal_slice(
                target_, PackedLogic4::from_msb_string("11111111"), 0U);
        } else {
            trace_.stage = ForceFollowupStage::reentrant_final_release;
            interpreter_.release_signal_slice(target_, 0U, 8U);
        }
        trace_.capture_operation();
        return { start, start + 1U };
    }

private:
    Interpreter& interpreter_;
    SignalId target_ { };
    ForceFollowupTrace& trace_;
};

ForceFollowupResult run_force_followup(
    Interpreter& interpreter,
    const SignalId target,
    const std::array<SignalId, 3U> members,
    const std::size_t member_count,
    const bool reenter)
{
    ForceFollowupTrace trace;
    trace.interpreter = &interpreter;
    trace.target = target;
    trace.members = members;
    trace.member_count = member_count;
    trace.trigger_reentry = reenter;
    interpreter.set_signal_change_hook(
        [&trace](const SignalId signal,
            const PackedLogic4&,
            const SimulationTick) {
            trace.record_change(signal);
        });
    interpreter.scheduler().set_trace_hook(
        &trace, &ForceFollowupTrace::record_transaction);

    Process writer;
    writer.id = 0U;
    writer.name = reenter
        ? "force_release_trace_reentry"
        : "force_release_transaction_edges";
    if (reenter) {
        writer.operations = {
            WaitFor { 1U }, WaitFor { 1U }, WaitFor { 1U }, WaitFor { 1U },
            Halt { },
        };
    } else {
        writer.operations = {
            WaitFor { 1U }, WaitFor { 1U }, WaitFor { 1U }, WaitFor { 1U },
            WaitFor { 1U }, WaitFor { 1U }, Halt { },
        };
    }
    const auto process = interpreter.add_process(std::move(writer));
    if (reenter) {
        interpreter.set_process_executor(
            process,
            std::make_unique<ForceTraceReentryExecutor>(
                interpreter, target, trace));
    } else {
        interpreter.set_process_executor(
            process,
            std::make_unique<ForceTransactionEdgeExecutor>(
                interpreter, target, trace));
    }

    const auto result = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    interpreter.set_signal_change_hook({ });
    const auto expected_time = reenter ? 4U : 6U;
    require(result.status == RunStatus::completed
            && result.time == expected_time
            && !trace.callback_failed,
        "force/release follow-up sequence completes without callback errors");
    return {
        std::move(trace.operation_snapshots),
        std::move(trace.change_snapshots),
        trace.transaction_counts,
        trace.change_counts,
        trace.reentered,
        trace.callback_failed,
    };
}

void check_force_release_transaction_edges()
{
    constexpr auto width = std::size_t { 4U };
    constexpr auto never = std::numeric_limits<SimulationTick>::max();
    const auto zeros = PackedLogic4::from_msb_string("00000000");
    const auto low_ones = PackedLogic4::from_msb_string("00000011");
    const auto low_leaf_ones = PackedLogic4::from_msb_string("0011");
    const auto element_zeros = PackedLogic4::from_msb_string("0000");
    AliasFamily alias(width, ResolutionKind::sv_wire);
    const auto alias_result = run_force_followup(
        alias.interpreter,
        alias.aggregate,
        { alias.first, alias.second, alias.aggregate },
        3U,
        false);
    Interpreter packed;
    const auto packed_signal = packed.add_signal({
        "words", zeros, ResolutionKind::sv_wire });
    const auto packed_result = run_force_followup(
        packed,
        packed_signal,
        { packed_signal, 0U, 0U },
        1U,
        false);

    const std::array<ForceFollowupStage, 6U> stages {
        ForceFollowupStage::release_unforced,
        ForceFollowupStage::force_unchanged_partial,
        ForceFollowupStage::release_unchanged_partial,
        ForceFollowupStage::force_changed_partial,
        ForceFollowupStage::release_changed_partial,
        ForceFollowupStage::release_unforced_again,
    };
    const std::array<PackedLogic4, 6U> expected_current {
        zeros, zeros, zeros, low_ones, zeros, zeros };
    const std::array<PackedLogic4, 6U> expected_last {
        zeros, zeros, zeros, zeros, low_ones, low_ones };
    const std::array<std::array<SimulationTick, 3U>, 6U> expected_alias_active {
        std::array<SimulationTick, 3U> { never, never, never },
        std::array<SimulationTick, 3U> { never, 0U, 0U },
        std::array<SimulationTick, 3U> { never, 0U, 0U },
        std::array<SimulationTick, 3U> { never, 0U, 0U },
        std::array<SimulationTick, 3U> { never, 0U, 0U },
        std::array<SimulationTick, 3U> { never, 1U, 1U },
    };
    const std::array<std::array<SimulationTick, 3U>, 6U> expected_alias_event {
        std::array<SimulationTick, 3U> { never, never, never },
        std::array<SimulationTick, 3U> { never, never, never },
        std::array<SimulationTick, 3U> { never, never, never },
        std::array<SimulationTick, 3U> { never, 0U, 0U },
        std::array<SimulationTick, 3U> { never, 0U, 0U },
        std::array<SimulationTick, 3U> { never, 1U, 1U },
    };
    const std::array<SimulationTick, 6U> expected_packed_active {
        never, 0U, 0U, 0U, 0U, 1U };
    const std::array<SimulationTick, 6U> expected_packed_event {
        never, never, never, 0U, 0U, 1U };
    const std::array<std::array<std::size_t, 3U>, 6U> expected_alias_transactions {
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 1U, 1U },
        std::array<std::size_t, 3U> { 0U, 1U, 1U },
        std::array<std::size_t, 3U> { 0U, 1U, 1U },
        std::array<std::size_t, 3U> { 0U, 1U, 1U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
    };
    const std::array<std::array<std::size_t, 3U>, 6U> expected_packed_transactions {
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 1U, 0U, 0U },
        std::array<std::size_t, 3U> { 1U, 0U, 0U },
        std::array<std::size_t, 3U> { 1U, 0U, 0U },
        std::array<std::size_t, 3U> { 1U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
    };

    bool operation_state_matches = alias_result.operation_snapshots.size() == 6U
        && packed_result.operation_snapshots.size() == 6U;
    if (operation_state_matches) {
        for (std::size_t index = 0U; index < stages.size(); ++index) {
            const auto& alias_state
                = alias_result.operation_snapshots[index];
            const auto& packed_state
                = packed_result.operation_snapshots[index];
            operation_state_matches = operation_state_matches
                && alias_state.stage == stages[index]
                && packed_state.stage == stages[index]
                && alias_state.member_count == 3U
                && packed_state.member_count == 1U
                && alias_state.last_active
                    == expected_alias_active[index]
                && alias_state.last_event
                    == expected_alias_event[index]
                && packed_state.last_active[0U]
                    == expected_packed_active[index]
                && packed_state.last_event[0U]
                    == expected_packed_event[index]
                && alias_state.current[2U] == expected_current[index]
                && alias_state.last[2U] == expected_last[index]
                && alias_state.stored[2U] == zeros
                && packed_state.current[0U] == expected_current[index]
                && packed_state.last[0U] == expected_last[index]
                && packed_state.stored[0U] == zeros
                && alias_state.current[0U] == element_zeros
                && alias_state.current[1U]
                    == (index == 3U ? low_leaf_ones : element_zeros)
                && alias_state.stored[0U] == element_zeros
                && alias_state.stored[1U] == element_zeros
                && alias_result.transaction_counts[
                       static_cast<std::size_t>(stages[index])]
                    == expected_alias_transactions[index]
                && packed_result.transaction_counts[
                       static_cast<std::size_t>(stages[index])]
                    == expected_packed_transactions[index];
        }
    }

    bool callbacks_match = true;
    const std::array<std::array<std::size_t, 3U>, 6U> expected_alias_changes {
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 1U, 1U },
        std::array<std::size_t, 3U> { 0U, 1U, 1U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
    };
    const std::array<std::array<std::size_t, 3U>, 6U> expected_packed_changes {
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
        std::array<std::size_t, 3U> { 1U, 0U, 0U },
        std::array<std::size_t, 3U> { 1U, 0U, 0U },
        std::array<std::size_t, 3U> { 0U, 0U, 0U },
    };
    for (std::size_t stage = 0U; stage < stages.size(); ++stage) {
        callbacks_match = callbacks_match
            && alias_result.change_counts[static_cast<std::size_t>(stages[stage])]
                == expected_alias_changes[stage]
            && packed_result.change_counts[static_cast<std::size_t>(stages[stage])]
                == expected_packed_changes[stage];
    }
    require(operation_state_matches && callbacks_match,
        "partial no-op force/release transactions affect only selected leaves "
        "and match packed current/LAST/stored control");

}

void check_force_release_trace_reentry_matches_packed()
{
    constexpr auto width = std::size_t { 4U };
    const auto zeros = PackedLogic4::from_msb_string("00000000");
    const auto ones = PackedLogic4::from_msb_string("11111111");
    AliasFamily alias(width, ResolutionKind::sv_wire);
    const auto alias_result = run_force_followup(
        alias.interpreter,
        alias.aggregate,
        { alias.first, alias.second, alias.aggregate },
        3U,
        true);
    Interpreter packed;
    const auto packed_signal = packed.add_signal({
        "words", zeros, ResolutionKind::sv_wire });
    const auto packed_result = run_force_followup(
        packed,
        packed_signal,
        { packed_signal, 0U, 0U },
        1U,
        true);

    bool operation_parity = alias_result.reentered
        && packed_result.reentered
        && alias_result.operation_snapshots.size() == 4U
        && packed_result.operation_snapshots.size() == 4U;
    const std::array<PackedLogic4, 4U> expected_current {
        ones, zeros, ones, zeros };
    const std::array<PackedLogic4, 4U> expected_last {
        zeros, ones, zeros, ones };
    if (operation_parity) {
        for (std::size_t index = 0U; index < expected_current.size(); ++index) {
            const auto& alias_state
                = alias_result.operation_snapshots[index];
            const auto& packed_state
                = packed_result.operation_snapshots[index];
            operation_parity = operation_parity
                && alias_state.current[2U] == expected_current[index]
                && alias_state.last[2U] == expected_last[index]
                && alias_state.stored[2U] == zeros
                && alias_state.current[2U] == packed_state.current[0U]
                && alias_state.last[2U] == packed_state.last[0U]
                && alias_state.stored[2U] == packed_state.stored[0U]
                && alias_state.current[2U].to_msb_string()
                    == alias_state.current[0U].to_msb_string()
                        + alias_state.current[1U].to_msb_string()
                && alias_state.last[2U].to_msb_string()
                    == alias_state.last[0U].to_msb_string()
                        + alias_state.last[1U].to_msb_string()
                && alias_state.stored[2U].to_msb_string()
                    == alias_state.stored[0U].to_msb_string()
                        + alias_state.stored[1U].to_msb_string();
        }
    }

    std::vector<const ForceFollowupSnapshot*> alias_proxy_changes;
    for (const auto& snapshot : alias_result.change_snapshots) {
        if (snapshot.notified == alias.aggregate) {
            alias_proxy_changes.push_back(&snapshot);
        }
    }
    std::vector<const ForceFollowupSnapshot*> packed_changes;
    for (const auto& snapshot : packed_result.change_snapshots) {
        if (snapshot.notified == packed_signal) {
            packed_changes.push_back(&snapshot);
        }
    }
    bool callback_parity = alias_proxy_changes.size() == packed_changes.size();
    for (std::size_t index = 0U;
        index < alias_proxy_changes.size() && index < packed_changes.size();
        ++index) {
        callback_parity = callback_parity
            && alias_proxy_changes[index]->stage == packed_changes[index]->stage
            && alias_proxy_changes[index]->current[2U]
                == packed_changes[index]->current[0U]
            && alias_proxy_changes[index]->last[2U]
                == packed_changes[index]->last[0U]
            && alias_proxy_changes[index]->stored[2U]
                == packed_changes[index]->stored[0U];
    }

    const std::array<ForceFollowupStage, 5U> transaction_stages {
        ForceFollowupStage::reentrant_outer_force,
        ForceFollowupStage::reentrant_inner_force,
        ForceFollowupStage::reentrant_release,
        ForceFollowupStage::reentrant_force_after_release,
        ForceFollowupStage::reentrant_final_release,
    };
    bool transaction_parity = true;
    for (const auto stage : transaction_stages) {
        const auto index = static_cast<std::size_t>(stage);
        transaction_parity = transaction_parity
            && packed_result.transaction_counts[index][0U] == 1U
            && alias_result.transaction_counts[index][0U] == 1U
            && alias_result.transaction_counts[index][1U] == 1U
            && alias_result.transaction_counts[index][2U] == 1U;
    }
    const auto nested_index = static_cast<std::size_t>(
        ForceFollowupStage::reentrant_inner_force);
    transaction_parity = transaction_parity
        && alias_result.transaction_counts[nested_index][2U]
            == packed_result.transaction_counts[nested_index][0U];

    if (!operation_parity || !callback_parity || !transaction_parity) {
        std::cerr << "trace reentry parity operations=" << operation_parity
                  << " callbacks=" << callback_parity
                  << " transactions=" << transaction_parity << '\n';
        const auto snapshot_count = std::min(
            alias_result.operation_snapshots.size(),
            packed_result.operation_snapshots.size());
        for (std::size_t index = 0U; index < snapshot_count; ++index) {
            const auto& alias_state
                = alias_result.operation_snapshots[index];
            const auto& packed_state
                = packed_result.operation_snapshots[index];
            std::cerr << "stage="
                      << static_cast<std::size_t>(alias_state.stage)
                      << " alias="
                      << alias_state.current[2U].to_msb_string()
                      << " last="
                      << alias_state.last[2U].to_msb_string()
                      << " stored="
                      << alias_state.stored[2U].to_msb_string()
                      << " packed="
                      << packed_state.current[0U].to_msb_string()
                      << " last="
                      << packed_state.last[0U].to_msb_string()
                      << " stored="
                      << packed_state.stored[0U].to_msb_string()
                      << '\n';
        }
    }
    require(operation_parity && callback_parity && transaction_parity,
        "reentrant transaction-trace force preserves packed callback, value, "
        "stored, LAST, and force-mask behavior");
}

} // namespace

void test_element_alias_observation_atomicity()
{
    for (const auto width : std::array<std::size_t, 2U> { 4U, 65U }) {
        // Aggregate aliases require a resolved-net kind shared by all leaves.
        // Plain sv_wire keeps this witness within that registration contract.
        check_late_trace_whole_write(width, ResolutionKind::sv_wire);
        check_late_stored_observer(width, ResolutionKind::sv_wire);
        check_pending_and_independent_history(width, ResolutionKind::sv_wire);
        check_reentrant_whole_family(width, true);
        check_reentrant_whole_family(width, false);
        check_transaction_waiters_for_whole_write(width);
    }
    check_transaction_trace_observes_family_stamps();
    check_stored_hook_failure_completes_family();
}

void test_aggregate_force_release_observation_atomicity()
{
    check_force_release_observation_atomicity();
}

void test_aggregate_force_release_followup()
{
    check_force_release_transaction_edges();
    check_force_release_trace_reentry_matches_packed();
    check_force_release_object_hook_snapshots();
}

} // namespace fsim::tests::runtime

// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "runtime_fused_staging_failure_support.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

constexpr std::size_t batch_preflight_task_count = 1024U;

struct AtomicBatchTaskPayload {
    std::uint64_t* executed { };
    std::size_t* executed_count { };
    std::uint64_t value { };
};

void dispatch_atomic_batch_task(
    Scheduler&, const AtomicBatchTaskPayload& payload)
{
    if (*payload.executed_count < batch_preflight_task_count + 2U) {
        payload.executed[(*payload.executed_count)++] = payload.value;
    }
}

class AtomicBatchPreflightProbe final : public SchedulerBatchTask {
public:
    explicit AtomicBatchPreflightProbe(
        const std::optional<std::size_t> failure_index)
        : failure_index_(failure_index)
    {
    }

    SchedulerBatchResult execute(Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        const auto frontier = scheduler.current_batch_frontier();
        require(frontier.has_value() && payloads.size() == 1U,
            "atomic batch failpoint runs inside its authenticated frontier");

        const auto marker = detail::make_scheduler_task_descriptor<
            AtomicBatchTaskPayload, dispatch_atomic_batch_task>(
            { executed_.data(), &executed_count_, 0U });
        scheduler.schedule_internal_systemverilog(
            SchedulerPhase::active, 10U, marker);

        std::array<StableOrder, batch_preflight_task_count> orders { };
        orders.fill(30U);
        std::array<detail::SchedulerTaskDescriptor,
            batch_preflight_task_count> tasks { };
        for (std::size_t index = 0U; index < tasks.size(); ++index) {
            tasks[index] = detail::make_scheduler_task_descriptor<
                AtomicBatchTaskPayload, dispatch_atomic_batch_task>(
                { executed_.data(), &executed_count_, index + 1U });
        }

        if (failure_index_) {
            arm_allocation_failure(*failure_index_);
        }
        begin_allocation_count();
        try {
            scheduler.schedule_internal_systemverilog_batch_from_frontier(
                frontier->generation, orders, tasks);
        } catch (const std::bad_alloc&) {
            allocation_failed_ = true;
        }
        allocation_count_ = end_allocation_count();
        clear_allocation_failure();

        const auto tail = detail::make_scheduler_task_descriptor<
            AtomicBatchTaskPayload, dispatch_atomic_batch_task>(
            { executed_.data(), &executed_count_,
                batch_preflight_task_count + 1U });
        scheduler.schedule_internal_systemverilog(
            SchedulerPhase::active, 40U, tail);
        return { payloads.size(), { } };
    }

    static void trace(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<AtomicBatchPreflightProbe*>(context);
        if (record.kind != SchedulerTraceKind::task_begin
            || record.order == 1U
            || probe.sequence_count_ == probe.sequences_.size()) {
            return;
        }
        probe.sequences_[probe.sequence_count_++] = {
            record.order, record.sequence
        };
    }

    [[nodiscard]] bool allocation_failed() const noexcept
    {
        return allocation_failed_;
    }

    [[nodiscard]] std::size_t allocation_count() const noexcept
    {
        return allocation_count_;
    }

    [[nodiscard]] std::span<const std::uint64_t> executed() const noexcept
    {
        return { executed_.data(), executed_count_ };
    }

    [[nodiscard]] std::span<const std::pair<StableOrder, std::uint64_t>>
    sequences() const noexcept
    {
        return { sequences_.data(), sequence_count_ };
    }

private:
    std::optional<std::size_t> failure_index_;
    std::array<std::uint64_t, batch_preflight_task_count + 2U> executed_ { };
    std::size_t executed_count_ { };
    std::array<std::pair<StableOrder, std::uint64_t>,
        batch_preflight_task_count + 2U> sequences_ { };
    std::size_t sequence_count_ { };
    std::size_t allocation_count_ { };
    bool allocation_failed_ { };
};

void run_atomic_batch_preflight_case(
    const std::optional<std::size_t> failure_index,
    AtomicBatchPreflightProbe& probe)
{
    Scheduler scheduler;
    scheduler.set_trace_hook(&probe, &AtomicBatchPreflightProbe::trace);
    scheduler.schedule_systemverilog_batchable(SchedulerPhase::active,
        1U, probe, 0U, [](Scheduler&) { });
    const auto result = scheduler.run();
    require(result.status == RunStatus::completed,
        "atomic scheduler batch completes after preflight success or decline");
    if (failure_index) {
        require(probe.allocation_failed(),
            "selected scheduler batch preflight allocation must fail");
    } else {
        require(!probe.allocation_failed(),
            "unarmed scheduler batch preflight must complete");
    }
}

void require_atomic_batch_result(
    const AtomicBatchPreflightProbe& probe,
    const std::optional<std::size_t> failure_index)
{
    const auto executed = probe.executed();
    const auto sequences = probe.sequences();
    if (failure_index) {
        require(probe.allocation_failed()
                && probe.allocation_count() == *failure_index + 1U,
            "the selected batch preflight allocation fails exactly once");
        require(executed.size() == 2U
                && executed[0U] == 0U
                && executed[1U] == batch_preflight_task_count + 1U,
            "failed batch preflight leaves only surrounding tasks visible");
        require(sequences.size() == 2U
                && sequences[0U].first == 10U
                && sequences[1U].first == 40U
                && sequences[1U].second == sequences[0U].second + 1U,
            "failed batch preflight consumes no insertion sequence numbers");
        return;
    }

    require(!probe.allocation_failed()
            && probe.allocation_count() == 2U,
        "successful batch preflight performs its two planned allocations");
    require(executed.size() == batch_preflight_task_count + 2U
            && sequences.size() == executed.size(),
        "successful batch preflight publishes its entire descriptor span");
    for (std::size_t index = 0U; index < executed.size(); ++index) {
        require(executed[index] == index,
            "batch descriptors execute in scheduler stable order");
        require(sequences[index].second
                == sequences[0U].second + index,
            "batch descriptors use consecutive insertion identities");
    }
}

struct PrefixSnapshot {
    std::array<std::string, 2U> values;
    std::array<std::string, 4U> drivers;
    std::array<std::vector<Publication>, 2U> publications;
    PublicationTrace trace;

    bool operator==(const PrefixSnapshot&) const = default;
};

struct PrefixOutcome {
    bool executor_called { };
    std::array<std::size_t, 4U> executor_callbacks { };
    PrefixSnapshot final;
};

PrefixSnapshot capture_prefix_snapshot(Interpreter& interpreter,
    const std::array<SignalId, 2U>& outputs,
    const std::array<ProcessId, 4U>& owners,
    const std::array<std::vector<Publication>, 2U>& publications,
    const PublicationTrace& trace)
{
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    PrefixSnapshot snapshot;
    for (std::size_t output_index = 0U;
         output_index < outputs.size(); ++output_index) {
        snapshot.values[output_index]
            = implementation.signals.at(outputs[output_index])
                  .initial_value.to_msb_string();
        snapshot.publications[output_index] = publications[output_index];
    }
    for (std::size_t owner_index = 0U;
         owner_index < owners.size(); ++owner_index) {
        const auto signal_index = owner_index < 2U ? 0U : 1U;
        const auto* const record
            = implementation.driver_values.at(outputs[signal_index])
                  .find(owners[owner_index]);
        require(record != nullptr,
            "checked prefix fixture retains every original driver record");
        snapshot.drivers[owner_index] = record->value.to_msb_string();
    }
    snapshot.trace = trace;
    return snapshot;
}

class CheckedPrefixExecutor final : public ProcessExecutor {
public:
    CheckedPrefixExecutor(const ProcessId process,
        const SignalId input,
        const SignalId output,
        const std::uint32_t source_offset,
        const std::uint32_t output_offset,
        const std::uint32_t width,
        PrefixOutcome& outcome,
        ProcessExecutorProgramBinding binding)
        : process_ { process }
        , input_ { input }
        , output_ { output }
        , source_offset_ { source_offset }
        , output_offset_ { output_offset }
        , width_ { width }
        , outcome_ { &outcome }
        , binding_ { std::move(binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        outcome_->executor_called = true;
        ++outcome_->executor_callbacks.at(process_);
        auto value = context.read_signal(input_).extract_bits(
            source_offset_, width_);
        context.write_update_slice(
            output_, std::move(value), output_offset_);
        ProcessResumeResult result { 3U, 4U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    ProcessId process_ { };
    SignalId input_ { };
    SignalId output_ { };
    std::uint32_t source_offset_ { };
    std::uint32_t output_offset_ { };
    std::uint32_t width_ { };
    PrefixOutcome* outcome_ { };
    ProcessExecutorProgramBinding binding_;
};

PrefixOutcome run_checked_prefix_case(const bool install_executor)
{
    constexpr std::uint32_t accepted_output_width = 64U;
    constexpr std::uint32_t failing_output_width = 129U;
    constexpr std::uint32_t input_width = 65U;
    Interpreter interpreter;
    const auto input = interpreter.add_signal(
        { "input", PackedLogic4(input_width, Logic4::zero) });
    const std::array outputs {
        interpreter.add_signal({ "accepted",
            PackedLogic4(accepted_output_width, Logic4::z),
            ResolutionKind::sv_wire }),
        interpreter.add_signal({ "failing",
            PackedLogic4(failing_output_width, Logic4::z),
            ResolutionKind::sv_wire })
    };

    const auto add_owner = [&](const ProcessId id, const char* const name,
                               const SignalId output,
                               const std::uint32_t output_offset,
                               const std::uint32_t source_offset,
                               const std::uint32_t width) {
        Process process;
        process.id = id;
        process.name = name;
        process.initialize = false;
        process.register_count = 2U;
        process.scheduling_domain = ProcessSchedulingDomain::generic;
        process.static_sensitivity = { { input, EdgeKind::any } };
        process.driver_regions = {
            { output, output_offset, width, false }
        };
        process.operations = { ReadSignal { 0U, input },
            Extract { 1U, 0U, source_offset, width },
            WriteUpdateSlice { output, 1U, output_offset,
                SignalUpdateDomain::generic },
            WaitSensitivity { }, Jump { 0U } };
        return interpreter.add_process(std::move(process));
    };
    const std::array owners {
        add_owner(0U, "accepted_owner", outputs[0U], 0U, 0U, 32U),
        add_owner(1U, "accepted_sibling", outputs[0U], 32U, 32U, 32U),
        add_owner(2U, "failing_owner", outputs[1U], 0U, 0U, 64U),
        add_owner(3U, "failing_sibling", outputs[1U], 64U, 0U, 65U)
    };

    for (std::size_t output_index = 0U;
         output_index < outputs.size(); ++output_index) {
        Process observer;
        observer.id = static_cast<ProcessId>(4U + output_index);
        observer.name = output_index == 0U
            ? "accepted_observer" : "failing_observer";
        observer.initialize = false;
        observer.static_sensitivity = {
            { outputs[output_index], EdgeKind::any }
        };
        observer.operations = {
            Display { output_index == 0U ? "accepted" : "failing" },
            WaitSensitivity { }, Jump { 0U }
        };
        (void)interpreter.add_process(std::move(observer));
    }
    interpreter.schedule_signal_at(
        input, PackedLogic4(input_width, Logic4::one), 1U, 0U);

    PrefixOutcome outcome;
    std::array<std::vector<Publication>, 2U> publications;
    interpreter.set_output_hook(
        [&](ProcessId, const std::string_view text, bool,
            const SimulationTick time, const std::uint64_t delta) {
            const auto output_index = text == "accepted" ? 0U : 1U;
            if (text == "accepted" || text == "failing") {
                publications[output_index].emplace_back(time, delta,
                    interpreter.signal_value(outputs[output_index])
                        .to_msb_string());
            }
        });

    AllocationFailureWindow trace_window;
    trace_window.observed_signals = outputs;
    trace_window.executor_called = &outcome.executor_called;
    if (install_executor) {
        for (std::size_t owner_index = 0U;
             owner_index < owners.size(); ++owner_index) {
            const auto process = owners[owner_index];
            const auto& registered = interpreter.process_program(process);
            interpreter.set_process_executor(process,
                std::make_unique<CheckedPrefixExecutor>(process, input,
                    outputs[owner_index < 2U ? 0U : 1U],
                    owner_index == 1U ? 32U : 0U,
                    owner_index == 1U ? 32U
                        : owner_index == 3U ? 64U : 0U,
                    owner_index == 0U || owner_index == 1U ? 32U
                        : owner_index == 2U ? 64U : 65U,
                    outcome,
                    ProcessExecutorProgramBinding {
                        registered, registered, process }));
        }
    }
    interpreter.start();

    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "checked prefix processes reach their initial sensitivity wait");
    interpreter.scheduler().set_trace_hook(
        &trace_window, &AllocationFailureWindow::trace);

    const auto active = interpreter.run(1U);
    require(active.status == RunStatus::completed
            || active.status == RunStatus::time_limit,
        "checked prefix processes stage their writes at the input event");
    const auto finish = interpreter.run(2U);
    require(finish.status == RunStatus::completed
            || finish.status == RunStatus::time_limit,
        "checked prefix output observers finish after the Update phase");
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    outcome.final = capture_prefix_snapshot(interpreter, outputs, owners,
        publications, trace_window.publications);
    return outcome;
}

} // namespace

void test_checked_two_output_prefix_fallback()
{
    const auto checked = run_checked_prefix_case(true);
    const auto reference = run_checked_prefix_case(false);
    require(checked.executor_called
            && checked.executor_callbacks
                == std::array<std::size_t, 4U> { 1U, 1U, 1U, 1U },
        "all four original owner members execute through checked ProcessExecutor bindings");
    require(checked.final.values[0U] == std::string(64U, '1')
            && checked.final.values[1U] == std::string(129U, '1')
            && checked.final.drivers[0U]
                == std::string(32U, 'Z') + std::string(32U, '1')
            && checked.final.drivers[1U]
                == std::string(32U, '1') + std::string(32U, 'Z')
            && checked.final.drivers[2U]
                == std::string(65U, 'Z') + std::string(64U, '1')
            && checked.final.drivers[3U]
                == std::string(65U, '1') + std::string(64U, 'Z'),
        "checked two-output writes preserve every declared owner slice");
    require(checked.final.publications[0U].size() == 1U
            && checked.final.publications[1U].size() == 1U
            && checked.final.trace.count > 0U
            && !checked.final.trace.overflow,
        "both checked output observers and transaction/change trace are exercised");
    require(checked.final == reference.final,
        "checked two-output values, raw owners, observer publications, and full trace match the interpreter");
    std::cout << "checked two-output prefix fallback matches interpreter\n";
}

void test_scheduler_atomic_batch_preflight_failures()
{
    for (std::size_t failure_index = 0U; failure_index < 2U;
         ++failure_index) {
        AtomicBatchPreflightProbe probe(failure_index);
        run_atomic_batch_preflight_case(failure_index, probe);
        require_atomic_batch_result(probe, failure_index);
    }

    AtomicBatchPreflightProbe successful_probe(std::nullopt);
    run_atomic_batch_preflight_case(std::nullopt, successful_probe);
    require_atomic_batch_result(successful_probe, std::nullopt);
}

} // namespace fsim::tests::runtime

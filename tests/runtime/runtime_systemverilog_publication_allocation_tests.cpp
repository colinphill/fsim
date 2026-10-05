// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

constexpr std::size_t startup_activations = 1U;
constexpr std::size_t steady_activations = 3U;
constexpr std::size_t activation_count
    = startup_activations + steady_activations;

struct AllocationTrace {
    Interpreter* interpreter { };
    SignalId output { };
    std::array<std::size_t, activation_count> allocation_counts { };
    std::array<std::size_t, activation_count> queue_growth_counts { };
    std::size_t completed_activations { };
    std::size_t current_queue_growths { };
    std::size_t observer_notifications { };
    bool measurement_active { };
    bool output_changed { };

    static void record(
        void* const context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<AllocationTrace*>(context);
        if (!trace.measurement_active) {
            return;
        }
        if (entry.kind == SchedulerTraceKind::queue_storage_growth) {
            ++trace.current_queue_growths;
            return;
        }
        if (entry.kind == SchedulerTraceKind::signal_change
            && entry.signal == trace.output) {
            trace.output_changed = true;
            return;
        }
        if (entry.kind != SchedulerTraceKind::task_end
            || !trace.output_changed) {
            return;
        }

        trace.allocation_counts[trace.completed_activations]
            = end_allocation_count();
        trace.queue_growth_counts[trace.completed_activations]
            = trace.current_queue_growths;
        ++trace.completed_activations;
        trace.measurement_active = false;
        trace.output_changed = false;
    }
};

class TaggedPublicationExecutor final : public ProcessExecutor {
public:
    TaggedPublicationExecutor(
        const SignalId output,
        const std::size_t width,
        AllocationTrace& trace,
        ProcessExecutorProgramBinding access_binding)
        : output_(output)
        , values_ {
              PackedLogic4(width, Logic4::one),
              PackedLogic4(width, Logic4::zero),
          }
        , trace_(trace)
        , access_binding_(std::move(access_binding))
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        trace_.measurement_active = true;
        trace_.output_changed = false;
        trace_.current_queue_growths = 0U;
        begin_allocation_count();
        context.write_update_in_domain(
            output_, values_[resume_count_ % values_.size()],
            SignalUpdateDomain::systemverilog_active);
        ++resume_count_;

        ProcessResumeResult result { 1U, 2U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    SignalId output_ { };
    std::array<PackedLogic4, 2U> values_;
    AllocationTrace& trace_;
    ProcessExecutorProgramBinding access_binding_;
    std::size_t resume_count_ { };
};

[[nodiscard]] bool check_fixed_topology_publication(
    const std::size_t width,
    const ResolutionKind resolution,
    const bool with_observer)
{
    SchedulerOptions scheduler_options;
    scheduler_options.recent_signal_capacity = 0U;
    Interpreter interpreter { scheduler_options };
    const auto trigger = interpreter.add_signal(
        { "steady_trigger", PackedLogic4(1U, Logic4::zero) });
    const auto output = interpreter.add_signal(
        { "steady_output", PackedLogic4(width, Logic4::zero), resolution });

    Process process;
    process.id = 0U;
    process.name = "steady_tagged_publication";
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.static_sensitivity = { { trigger, EdgeKind::any } };
    if (resolution == ResolutionKind::sv_wire) {
        process.driver_regions = {
            { output, 0U, static_cast<std::uint32_t>(width), true },
        };
    }
    process.operations = {
        WriteUpdate {
            output, 0U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    const auto process_id = interpreter.add_process(std::move(process));

    AllocationTrace trace;
    trace.interpreter = &interpreter;
    trace.output = output;
    const auto& registered = interpreter.process_program(process_id);
    interpreter.set_process_executor(
        process_id,
        std::make_unique<TaggedPublicationExecutor>(output, width, trace,
            ProcessExecutorProgramBinding {
                registered, registered, process_id }));
    interpreter.scheduler().set_trace_hook(
        &trace, &AllocationTrace::record);
    if (with_observer) {
        interpreter.set_signal_change_hook(
            [&trace, output](
                const SignalId signal,
                const PackedLogic4&,
                SimulationTick) {
                if (signal == output) {
                    ++trace.observer_notifications;
                }
            });
    }

    const std::array trigger_values {
        PackedLogic4(1U, Logic4::one),
        PackedLogic4(1U, Logic4::zero),
    };
    for (std::size_t activation = 0U;
        activation < activation_count;
        ++activation) {
        if (activation != 0U) {
            interpreter.schedule_signal_at(
                trigger,
                trigger_values[(activation - 1U) % trigger_values.size()],
                static_cast<SimulationTick>(activation), activation);
        }

        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed,
            "each measured tagged publication drains normally");
        require(
            trace.completed_activations == activation + 1U
                && !trace.measurement_active,
            "the allocation window covers one staged and dispatched value");
    }

    const auto drained = interpreter.run();
    require(
        drained.status == RunStatus::completed,
        "the fixed-topology process drains after the final measurement");
    require(
        interpreter.signal_value(output)
            == PackedLogic4(width, Logic4::zero),
        "each measured descriptor publishes its alternating full-width value");
    if (with_observer) {
        require(
            trace.observer_notifications == activation_count,
            "the installed nonallocating observer sees every publication");
    }

    std::cerr << "tagged publication width=" << width
              << " resolution="
              << (resolution == ResolutionKind::none ? "none" : "sv_wire")
              << " observer=" << (with_observer ? "on" : "off");
    for (std::size_t activation = 0U;
        activation < activation_count;
        ++activation) {
        std::cerr << " ["
                  << (activation < startup_activations ? "startup" : "steady")
                  << ":alloc=" << trace.allocation_counts[activation]
                  << ",queue_growth=" << trace.queue_growth_counts[activation]
                  << ']';
    }
    std::cerr << '\n';

    bool steady_state_queue_growth_free = true;
    for (std::size_t activation = startup_activations;
        activation < activation_count;
        ++activation) {
        steady_state_queue_growth_free
            &= trace.queue_growth_counts[activation] == 0U;
    }
    require(trace.queue_growth_counts[0] != 0U,
        "the startup publication exposes scheduler queue growth");
    return steady_state_queue_growth_free;
}

struct QueueGrowthTrace {
    std::size_t count { };

    static void record(
        void* const context,
        const SchedulerTraceRecord& entry) noexcept
    {
        if (entry.kind == SchedulerTraceKind::queue_storage_growth) {
            ++static_cast<QueueGrowthTrace*>(context)->count;
        }
    }
};

struct QueueGrowthDiscardProbe {
    Scheduler* scheduler { };
    bool attempted { };
    bool rejected { };

    static void record(
        void* const context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& probe = *static_cast<QueueGrowthDiscardProbe*>(context);
        if (entry.kind != SchedulerTraceKind::queue_storage_growth
            || probe.attempted) {
            return;
        }
        probe.attempted = true;
        try {
            probe.scheduler->discard_pending();
        } catch (const std::logic_error&) {
            probe.rejected = true;
        }
    }
};

void test_queue_growth_trace_cannot_discard_during_enqueue()
{
    Scheduler scheduler;
    QueueGrowthDiscardProbe probe { &scheduler };
    scheduler.set_trace_hook(&probe, &QueueGrowthDiscardProbe::record);
    bool task_ran { };
    scheduler.schedule_systemverilog_at(0U, SchedulerPhase::active, 1U,
        [&](Scheduler&) { task_ran = true; });
    require(probe.attempted && probe.rejected && scheduler.has_pending(),
        "queue-growth trace cannot discard an enqueue in progress");
    require(scheduler.run().status == RunStatus::completed && task_ran,
        "the enqueue remains usable after a rejected diagnostic discard");
    scheduler.set_trace_hook(nullptr, nullptr);
}

void test_scheduler_queue_storage_lifecycle()
{
    test_queue_growth_trace_cannot_discard_during_enqueue();
    {
        Scheduler scheduler;
        QueueGrowthTrace trace;
        scheduler.set_trace_hook(&trace, &QueueGrowthTrace::record);
        bool canceled_task_ran { };
        const auto canceled = scheduler.schedule_systemverilog_after_cancelable(
            1U, SchedulerPhase::active, 1U,
            [&](Scheduler&) { canceled_task_ran = true; });
        scheduler.cancel(canceled);
        require(scheduler.run().status == RunStatus::completed
                && !canceled_task_ran,
            "a canceled future queue entry is removed without execution");
        const auto growth_after_cancel = trace.count;
        require(growth_after_cancel != 0U,
            "the canceled future slot records its initial queue allocation");

        bool resumed_task_ran { };
        scheduler.schedule_systemverilog_at(2U, SchedulerPhase::active, 2U,
            [&](Scheduler&) { resumed_task_ran = true; });
        require(scheduler.run().status == RunStatus::completed
                && resumed_task_ran && trace.count == growth_after_cancel,
            "cancel cleanup returns queue capacity for a later tick");
        scheduler.set_trace_hook(nullptr, nullptr);
    }

    {
        Scheduler scheduler;
        QueueGrowthTrace trace;
        scheduler.set_trace_hook(&trace, &QueueGrowthTrace::record);
        bool first_ran { };
        bool suffix_ran { };
        scheduler.schedule_systemverilog_at(1U, SchedulerPhase::active, 1U,
            [&](Scheduler& current) {
                first_ran = true;
                current.request_stop();
            });
        scheduler.schedule_systemverilog_at(1U, SchedulerPhase::active, 2U,
            [&](Scheduler&) { suffix_ran = true; });
        const auto stopped = scheduler.run();
        require(stopped.status == RunStatus::stopped
                && first_ran && !suffix_ran
                && scheduler.has_pending(),
            "stop retains the unconsumed scheduled suffix");
        const auto growth_before_resume = trace.count;
        scheduler.clear_stop();
        require(scheduler.run().status == RunStatus::completed
                && suffix_ran && trace.count == growth_before_resume,
            "resume executes the suffix without moving its live storage");

        const auto growth_after_resume = trace.count;
        bool later_ran { };
        scheduler.schedule_systemverilog_at(2U, SchedulerPhase::active, 3U,
            [&](Scheduler&) { later_ran = true; });
        require(scheduler.run().status == RunStatus::completed
                && later_ran && trace.count == growth_after_resume,
            "the completed stopped slot reuses capacity at the next tick");
        scheduler.set_trace_hook(nullptr, nullptr);
    }

    {
        Scheduler scheduler;
        QueueGrowthTrace trace;
        scheduler.set_trace_hook(&trace, &QueueGrowthTrace::record);
        std::vector<StableOrder> execution_order;
        scheduler.schedule_systemverilog_at(1U, SchedulerPhase::active, 1U,
            [&](Scheduler& current) {
                execution_order.push_back(1U);
                current.schedule_systemverilog(
                    SchedulerPhase::active, 2U,
                    [&](Scheduler&) { execution_order.push_back(2U); });
            });
        scheduler.schedule_systemverilog_at(1U, SchedulerPhase::active, 3U,
            [&](Scheduler&) { execution_order.push_back(3U); });
        require(scheduler.run().status == RunStatus::completed
                && execution_order
                    == std::vector<StableOrder> { 1U, 3U, 2U },
            "callback-time enqueue stays pending behind the frozen batch");
        const auto growth_after_reentrant_enqueue = trace.count;
        require(growth_after_reentrant_enqueue >= 2U,
            "running and reentrant pending queues own separate storage");

        scheduler.schedule_systemverilog_at(2U, SchedulerPhase::active, 4U,
            [&](Scheduler&) { execution_order.push_back(4U); });
        require(scheduler.run().status == RunStatus::completed
                && execution_order.back() == 4U
                && trace.count == growth_after_reentrant_enqueue,
            "both completed queues return storage for a later tick");
        scheduler.set_trace_hook(nullptr, nullptr);
    }

    {
        Scheduler scheduler;
        QueueGrowthTrace trace;
        scheduler.set_trace_hook(&trace, &QueueGrowthTrace::record);
        scheduler.schedule_systemverilog_at(7U, SchedulerPhase::active, 1U,
            [](Scheduler&) { });
        const auto growth_before_discard = trace.count;
        require(growth_before_discard != 0U,
            "pending work allocates queue storage before discard");
        scheduler.discard_pending();
        scheduler.schedule_systemverilog_at(8U, SchedulerPhase::active, 2U,
            [](Scheduler&) { });
        require(scheduler.run().status == RunStatus::completed
                && trace.count == growth_before_discard,
            "safe-point discard recycles queue capacity without retaining tasks");
        scheduler.set_trace_hook(nullptr, nullptr);
    }
}

} // namespace

void test_systemverilog_publication_allocation_gate()
{
    test_scheduler_queue_storage_lifecycle();
    bool all_routes_queue_growth_free = true;
    for (const auto width : { 1U, 65U, 129U }) {
        for (const auto resolution : {
                 ResolutionKind::none, ResolutionKind::sv_wire }) {
            all_routes_queue_growth_free
                &= check_fixed_topology_publication(width, resolution, false);
            all_routes_queue_growth_free
                &= check_fixed_topology_publication(width, resolution, true);
        }
    }
    require(
        all_routes_queue_growth_free,
        "warmed fixed-topology tagged publications reuse scheduler queue storage");
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_systemverilog_publication_allocation_gate();
        return 0;
    } catch (const std::exception& error) {
        fsim::tests::runtime::staging_failure_support::end_allocation_count();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

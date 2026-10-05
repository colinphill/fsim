// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"

#include <array>
#include <stdexcept>
#include <string_view>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct TraceCapture {
    std::array<SchedulerTraceRecord, 64> records;
    std::size_t size { };
    bool overflow { };

    static void receive(void* context, const SchedulerTraceRecord& record) noexcept
    {
        auto& capture = *static_cast<TraceCapture*>(context);
        // Queue-storage growth has its own trace coverage. These assertions
        // describe the ordering and consumed prefix of dispatched tasks.
        if (record.kind == SchedulerTraceKind::queue_storage_growth) {
            return;
        }
        if (capture.size == capture.records.size()) {
            capture.overflow = true;
            return;
        }
        capture.records[capture.size++] = record;
    }

    void attach(Scheduler& scheduler)
    {
        scheduler.set_trace_hook(this, receive);
    }
};

void test_task_trace()
{
    TraceCapture trace;
    Scheduler scheduler;
    const auto canceled = scheduler.schedule_after_cancelable(
        3, SchedulerPhase::active, 1, [](Scheduler&) { });
    scheduler.cancel(canceled);
    scheduler.schedule_at(3, SchedulerPhase::active, 7,
        [](Scheduler& current) {
            current.note_signal_transaction(12);
            current.note_signal_change(12);
            current.request_stop();
        });
    scheduler.schedule_at(3, SchedulerPhase::active, 8,
        [](Scheduler&) { throw std::runtime_error("trace failure witness"); });
    scheduler.schedule_at(3, SchedulerPhase::postponed, 9,
        [](Scheduler&) { });
    trace.attach(scheduler);

    require(scheduler.run().status == RunStatus::stopped,
        "tracing preserves stop at the next task boundary");
    require(trace.size == 4 && !trace.overflow,
        "canceled work emits no execution trace");
    require(trace.records[0].kind == SchedulerTraceKind::task_begin
            && trace.records[0].order == 7
            && trace.records[0].sequence == 1
            && trace.records[0].time == 3 && trace.records[0].delta == 0
            && trace.records[0].phase == SchedulerPhase::active,
        "task trace carries exact scheduler identity");
    require(trace.records[1].kind == SchedulerTraceKind::signal_transaction
            && trace.records[1].signal == 12
            && trace.records[2].kind == SchedulerTraceKind::signal_change
            && trace.records[2].signal == 12
            && trace.records[3].kind == SchedulerTraceKind::task_end,
        "signal records remain between ordinary task begin and end");

    scheduler.clear_stop();
    bool failed = false;
    try {
        (void)scheduler.run();
    } catch (const std::runtime_error& error) {
        failed = std::string_view { error.what() } == "trace failure witness";
    }
    require(failed && trace.size == 6
            && trace.records[4].kind == SchedulerTraceKind::task_begin
            && trace.records[5].kind == SchedulerTraceKind::task_failure
            && trace.records[5].order == 8,
        "failed tasks emit failure without a success record");
    require(scheduler.run().status == RunStatus::completed
            && trace.size == 8
            && trace.records[7].phase == SchedulerPhase::postponed
            && trace.records[7].kind == SchedulerTraceKind::task_end,
        "trace follows retained work after failure");
    scheduler.set_trace_hook(nullptr, nullptr);
    scheduler.schedule(SchedulerPhase::active, 10, [](Scheduler&) { });
    (void)scheduler.run();
    require(trace.size == 8, "detached trace receives no callbacks");
}

class PrefixBatch final : public SchedulerBatchTask {
public:
    bool decline { };
    bool fail { };

    SchedulerBatchResult execute(Scheduler& scheduler,
        std::span<const std::uint64_t>) override
    {
        if (decline)
            return { 0, {} };
        scheduler.request_stop();
        if (fail) {
            return { 1, std::make_exception_ptr(
                std::runtime_error("trace batch failure")) };
        }
        return { 1, {} };
    }
};

void test_batch_trace()
{
    for (const auto fail : { false, true }) {
        TraceCapture trace;
        PrefixBatch batch;
        batch.fail = fail;
        Scheduler scheduler;
        for (StableOrder order = 4; order <= 5; ++order) {
            scheduler.schedule_next_delta_batchable(SchedulerPhase::active,
                order, batch, order, [](Scheduler&) { });
        }
        trace.attach(scheduler);
        bool caught = false;
        try {
            require(scheduler.run().status == RunStatus::stopped,
                "batch stop is preserved by tracing");
        } catch (const std::runtime_error& error) {
            caught = std::string_view { error.what() } == "trace batch failure";
        }
        require(caught == fail && trace.size == 3
                && trace.records[0].kind == SchedulerTraceKind::batch_begin
                && trace.records[0].count == 2
                && trace.records[1].kind == (fail
                    ? SchedulerTraceKind::batch_failure : SchedulerTraceKind::batch_end)
                && trace.records[1].count == 1
                && trace.records[2].kind == SchedulerTraceKind::task_end
                && trace.records[2].order == 4,
            "batch traces report only the consumed prefix as executed");
        batch.fail = false;
        scheduler.clear_stop();
        (void)scheduler.run();
        require(trace.size == 6 && trace.records[3].count == 1
                && trace.records[5].order == 5 && !trace.overflow,
            "unconsumed batch suffix is traced only when resumed");
    }

    TraceCapture trace;
    PrefixBatch batch;
    batch.decline = true;
    Scheduler scheduler;
    scheduler.schedule_next_delta_batchable(SchedulerPhase::active,
        6, batch, 0, [](Scheduler&) { });
    trace.attach(scheduler);
    (void)scheduler.run();
    require(trace.size == 4
            && trace.records[0].kind == SchedulerTraceKind::batch_begin
            && trace.records[1].kind == SchedulerTraceKind::batch_end
            && trace.records[1].count == 0
            && trace.records[2].kind == SchedulerTraceKind::task_begin
            && trace.records[3].kind == SchedulerTraceKind::task_end
            && trace.records[2].sequence == trace.records[3].sequence,
        "declined native work traces its ordinary fallback exactly once");
}

void test_signal_publication_trace()
{
    TraceCapture trace;
    simir::Interpreter interpreter;
    const auto signal = interpreter.add_signal(
        { "trace_signal", PackedLogic4 { 1, Logic4::zero } });
    trace.attach(interpreter.scheduler());
    interpreter.schedule_signal_at(signal, PackedLogic4 { 1, Logic4::zero }, 1, 0);
    interpreter.schedule_signal_at(signal, PackedLogic4 { 1, Logic4::one }, 2, 0);
    (void)interpreter.run();
    std::size_t transactions = 0;
    std::size_t changes = 0;
    for (std::size_t index = 0; index < trace.size; ++index) {
        const auto& record = trace.records[index];
        if (record.kind == SchedulerTraceKind::signal_transaction) {
            ++transactions;
            require(record.signal == signal && record.time == transactions,
                "publication trace includes unchanged transactions");
        }
        if (record.kind == SchedulerTraceKind::signal_change) {
            ++changes;
            require(record.signal == signal && record.time == 2,
                "value-change trace excludes unchanged transactions");
        }
    }
    require(transactions == 2 && changes == 1 && !trace.overflow,
        "interpreter publication and change traces retain distinct meanings");
}

} // namespace

void test_scheduler_trace()
{
    test_task_trace();
    test_batch_trace();
    test_signal_publication_trace();
}

} // namespace fsim::tests::runtime

// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

struct PublicationCase {
    bool allocation_failed { };
    std::vector<Publication> values;
};

class PublicationExecutor final : public ProcessExecutor {
public:
    PublicationExecutor(
        const std::optional<std::size_t> allocation_failure,
        bool& failure_observed,
        std::optional<ProcessExecutorProgramBinding> access_binding)
        : allocation_failure_(allocation_failure)
        , failure_observed_(failure_observed)
        , access_binding_ { std::move(access_binding) }
        , injected_value_(1U, Logic4::x)
        , first_value_(1U, Logic4::one)
        , second_value_(1U, Logic4::zero)
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return access_binding_ ? &*access_binding_ : nullptr;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        if (allocation_failure_) {
            arm_allocation_failure(*allocation_failure_);
            try {
                context.write_update_in_domain(
                    output_, std::move(injected_value_),
                    SignalUpdateDomain::systemverilog_active);
            } catch (const std::bad_alloc&) {
                failure_observed_ = true;
            }
            clear_allocation_failure();
        }

        context.write_update_in_domain(
            output_, std::move(first_value_),
            SignalUpdateDomain::systemverilog_active);
        context.write_update_in_domain(
            output_, std::move(second_value_),
            SignalUpdateDomain::systemverilog_active);

        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

    void set_output(const SignalId output) noexcept
    {
        output_ = output;
    }

private:
    std::optional<std::size_t> allocation_failure_;
    bool& failure_observed_;
    std::optional<ProcessExecutorProgramBinding> access_binding_;
    SignalId output_ { };
    PackedLogic4 injected_value_;
    PackedLogic4 first_value_;
    PackedLogic4 second_value_;
};

PublicationCase run_case(
    const std::optional<std::size_t> allocation_failure,
    const bool preallocate)
{
    Interpreter interpreter;
    const auto output = interpreter.add_signal(
        { "output", PackedLogic4(1U, Logic4::zero) });

    Process process;
    process.id = 0U;
    process.name = "publication_pool_writer";
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 2U;
    if (preallocate) {
        process.operations = {
            WriteUpdate {
                output, 0U, SignalUpdateDomain::systemverilog_active },
            WriteUpdate {
                output, 1U, SignalUpdateDomain::systemverilog_active },
            Halt { },
        };
    } else {
        process.operations = { Halt { } };
    }
    const auto process_id = interpreter.add_process(std::move(process));

    PublicationCase result;
    bool failure_observed { };
    std::optional<ProcessExecutorProgramBinding> access_binding;
    if (preallocate) {
        // This body declares the output written by the native executor. The
        // Halt-only growth case deliberately remains opaque.
        const auto& registered = interpreter.process_program(process_id);
        access_binding.emplace(registered, registered, process_id);
    }
    auto executor = std::make_unique<PublicationExecutor>(
        allocation_failure, failure_observed, std::move(access_binding));
    executor->set_output(output);
    interpreter.set_process_executor(process_id, std::move(executor));
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == output) {
                result.values.emplace_back(
                    time, interpreter.scheduler().delta(),
                    value.to_msb_string());
            }
        });

    const auto run = interpreter.run(0U);
    require(run.status == RunStatus::completed,
        "the publication-pool process completes after staging its updates");
    result.allocation_failed = failure_observed;
    require(interpreter.signal_value(output).to_msb_string() == "0",
        "the last queued publication determines the final signal value");
    require(
        interpreter.driver_value(process_id, output).to_msb_string() == "0",
        "the queued publications retain their original process driver");
    return result;
}


class StopAfterSchedulingExecutor final : public ProcessExecutor {
public:
    StopAfterSchedulingExecutor(Interpreter& interpreter, SignalId output,
        ProcessExecutorProgramBinding access_binding)
        : interpreter_(interpreter)
        , output_(output)
        , access_binding_ { std::move(access_binding) }
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
        context.write_after_in_domain(
            output_, PackedLogic4(1U, Logic4::one), 5U,
            SignalUpdateDomain::systemverilog_active);
        context.write_after_in_domain(
            output_, PackedLogic4(1U, Logic4::zero), 5U,
            SignalUpdateDomain::systemverilog_active);
        interpreter_.scheduler().request_stop();

        ProcessResumeResult result { 4U, 5U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    Interpreter& interpreter_;
    SignalId output_ { };
    ProcessExecutorProgramBinding access_binding_;
};

void test_publication_hook_reentrant_queue()
{
    Interpreter interpreter;
    const auto output = interpreter.add_signal(
        { "hook_output", PackedLogic4(1U, Logic4::zero) });
    const auto trigger = interpreter.add_signal(
        { "hook_trigger", PackedLogic4(1U, Logic4::zero) });
    const auto followup = interpreter.add_signal(
        { "hook_followup", PackedLogic4(1U, Logic4::zero) });

    Process writer;
    writer.id = 0U;
    writer.name = "hook_publication_writer";
    writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    writer.register_count = 2U;
    writer.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        LoadConstant { 1U, PackedLogic4(1U, Logic4::zero) },
        WriteUpdate {
            output, 0U, SignalUpdateDomain::systemverilog_active },
        WriteUpdate {
            output, 1U, SignalUpdateDomain::systemverilog_active },
        Halt { },
    };
    const auto writer_id = interpreter.add_process(std::move(writer));

    Process waiter;
    waiter.id = 1U;
    waiter.name = "hook_reentrant_waiter";
    waiter.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    waiter.register_count = 1U;
    waiter.static_sensitivity = { { trigger, EdgeKind::any } };
    waiter.operations = {
        WaitSensitivity { },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteUpdate {
            followup, 0U, SignalUpdateDomain::systemverilog_active },
        Halt { },
    };
    const auto waiter_id = interpreter.add_process(std::move(waiter));

    bool discard_rejected { };
    bool pending_after_rejection { };
    bool followup_queued { };
    bool tried_discard { };
    std::vector<Publication> publications;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == output) {
                publications.emplace_back(
                    time, interpreter.scheduler().delta(),
                    "output=" + value.to_msb_string());
                if (!tried_discard
                    && value.to_msb_string() == "1") {
                    tried_discard = true;
                    try {
                        interpreter.scheduler().discard_pending();
                    } catch (const std::logic_error&) {
                        discard_rejected = true;
                    }
                    pending_after_rejection
                        = interpreter.scheduler().has_pending();
                    interpreter.schedule_signal_at(
                        trigger, PackedLogic4(1U, Logic4::one), time, 0U);
                    followup_queued = true;
                }
            } else if (signal == followup) {
                publications.emplace_back(
                    time, interpreter.scheduler().delta(),
                    "followup=" + value.to_msb_string());
            }
        });

    const auto run = interpreter.run();
    require(run.status == RunStatus::completed,
        "the publication-hook reentrant work drains normally");
    require(tried_discard && discard_rejected,
        "discard from inside a publication hook is rejected by the scheduler");
    require(pending_after_rejection,
        "the pending publication suffix remains queued after rejected discard");
    require(followup_queued,
        "the publication hook can enqueue a later signal wakeup");
    require(publications.size() == 3U,
        "both original publications and the reentrant followup are observed");
    require(std::get<2>(publications[0]) == "output=1"
            && std::get<2>(publications[1]) == "output=0"
            && std::get<2>(publications[2]) == "followup=1",
        "reentrant hook work preserves publication order and original values");
    require(interpreter.signal_value(output).to_msb_string() == "0"
            && interpreter.driver_value(writer_id, output).to_msb_string() == "0",
        "the queued suffix retains the original writer and final output value");
    require(interpreter.signal_value(followup).to_msb_string() == "1"
            && interpreter.driver_value(waiter_id, followup).to_msb_string() == "1",
        "the reentrant wakeup can reuse the tagged publication pool");
}

void test_publication_stop_resume()
{
    Interpreter interpreter;
    const auto output = interpreter.add_signal(
        { "delayed_output", PackedLogic4(1U, Logic4::zero) });

    Process process;
    process.id = 0U;
    process.name = "delayed_publication_writer";
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 2U;
    process.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        LoadConstant { 1U, PackedLogic4(1U, Logic4::zero) },
        WriteAfter {
            output, 0U, 5U, SignalUpdateDomain::systemverilog_active },
        WriteAfter {
            output, 1U, 5U, SignalUpdateDomain::systemverilog_active },
        Halt { },
    };
    const auto process_id = interpreter.add_process(std::move(process));
    const auto& registered = interpreter.process_program(process_id);
    interpreter.set_process_executor(process_id,
        std::make_unique<StopAfterSchedulingExecutor>(interpreter, output,
            ProcessExecutorProgramBinding {
                registered, registered, process_id }));

    const auto initial_driver
        = interpreter.driver_value(process_id, output).to_msb_string();
    std::vector<Publication> publications;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == output) {
                publications.emplace_back(
                    time, interpreter.scheduler().delta(),
                    value.to_msb_string());
            }
        });

    const auto stopped = interpreter.run();
    require(stopped.status == RunStatus::stopped,
        "the executor stops after queuing delayed tagged publications");
    require(interpreter.scheduler().has_pending()
            && interpreter.scheduler().next_pending_time() == 5U,
        "the stopped scheduler retains the delayed publication descriptors");
    require(interpreter.signal_value(output).to_msb_string() == "0"
            && interpreter.driver_value(process_id, output).to_msb_string()
                == initial_driver,
        "delayed publications remain invisible before their scheduled time");

    interpreter.scheduler().clear_stop();
    const auto early = interpreter.scheduler().run(4U);
    require(early.status == RunStatus::time_limit
            && interpreter.scheduler().has_pending()
            && interpreter.scheduler().next_pending_time() == 5U,
        "an early resumed run leaves later publications pending");
    require(publications.empty()
            && interpreter.signal_value(output).to_msb_string() == "0"
            && interpreter.driver_value(process_id, output).to_msb_string()
                == initial_driver,
        "the early resume does not publish delayed values");

    const auto resumed = interpreter.scheduler().run(5U);
    require(resumed.status == RunStatus::completed,
        "the second resume delivers the delayed publication queue");
    require(publications.size() == 2U
            && std::get<0>(publications[0]) == 5U
            && std::get<0>(publications[1]) == 5U
            && std::get<2>(publications[0]) == "1"
            && std::get<2>(publications[1]) == "0",
        "stop and resume preserve delayed publication time and ordering");
    require(interpreter.signal_value(output).to_msb_string() == "0"
            && interpreter.driver_value(process_id, output).to_msb_string() == "0",
        "resumed delayed publications retain their process driver");
}

} // namespace

void test_publication_pool_failure_sweep()
{
    const auto baseline = run_case(std::nullopt, true);
    const std::vector<Publication> successful_publications {
        { 0U, 0U, "1" },
        { 0U, 0U, "0" },
    };
    require(baseline.values == successful_publications,
        "preallocated descriptors preserve every same-process publication");

    const auto sweep = [&](const bool preallocate, const char* const label) {
        std::size_t failed_indices { };
        std::optional<std::size_t> first_no_failure;
        for (std::size_t index = 0U; index < 64U; ++index) {
            const auto result = run_case(index, preallocate);
            if (!result.allocation_failed) {
                require(result.values
                        == std::vector<Publication> {
                            { 0U, 0U, "X" },
                            { 0U, 0U, "1" },
                            { 0U, 0U, "0" },
                        },
                    "the first non-failing request publishes before later values");
                first_no_failure = index;
                break;
            }
            ++failed_indices;
            require(
                result.values == successful_publications,
                "failed descriptor insertion publishes no value and later writes continue");
        }

        require(failed_indices != 0U,
            "the allocator sweep reaches a real tagged-publication allocation");
        require(first_no_failure.has_value(),
            "the allocator sweep discovers the first non-failing request boundary");
        std::cout << label << " allocation requests: " << failed_indices << '\n';
    };

    sweep(false, "dynamic publication-pool growth");
    sweep(true, "preallocated publication enqueue");
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_publication_hook_reentrant_queue();
        fsim::tests::runtime::test_publication_stop_resume();
        fsim::tests::runtime::test_publication_pool_failure_sweep();
        return 0;
    } catch (const std::exception& error) {
        fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

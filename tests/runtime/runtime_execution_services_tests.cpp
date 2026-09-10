// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::tests::runtime {

namespace {

    void require(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

} // namespace

void test_simir_nested_calls()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    const auto output = interpreter.add_signal(
        { "top.call_result", PackedLogic4::from_aval_bval(8, 0, 0) });
    const CallStack stack { 0, 1, 2 };

    Process process;
    process.id = 0;
    process.name = "nested_calls";
    process.register_count = 6;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_aval_bval(32, 0, 0) },
        LoadConstant { 1, PackedLogic4::from_aval_bval(32, 0, 0) },
        LoadConstant { 2, PackedLogic4::from_aval_bval(32, 0, 0) },
        LoadConstant { 3, PackedLogic4::from_aval_bval(8, 10, 0) },
        LoadConstant { 4, PackedLogic4::from_aval_bval(8, 0, 0) },
        Call { 9, 6, stack },
        WriteBlocking { output, 4 },
        Halt { },
        Halt { },
        DebugPoint {
            DebugPointKind::call,
            SourceLocation { "nested_calls.simir", 1, 1 } },
        LoadConstant { 5, PackedLogic4::from_aval_bval(8, 0, 0) },
        Call { 15, 12, stack },
        CopyRegister { 4, 5 },
        Return { stack },
        Halt { },
        LoadConstant { 5, PackedLogic4::from_aval_bval(8, 42, 0) },
        Return { stack },
    };
    (void)interpreter.add_process(std::move(process));

    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed,
        "nested SimIR calls must complete");
    require(
        interpreter.signal_value(output).to_msb_string() == "00101010",
        "nested SimIR calls must return through the persistent call stack");

    {
        const std::string caller_value = "1" + std::string(135, '0') + "1";
        const std::string result_value = "01" + std::string(133, '1') + "10";
        Interpreter framed;
        const auto restored = framed.add_signal(
            { "top.restored_automatic", PackedLogic4 { 137, Logic4::zero } });
        const auto preserved = framed.add_signal(
            { "top.preserved_result", PackedLogic4 { 137, Logic4::zero } });
        Process automatic;
        automatic.id = 0;
        automatic.name = "automatic_frame";
        automatic.register_count = 2;
        automatic.operations = {
            LoadConstant { 0, PackedLogic4::from_msb_string(caller_value) },
            LoadConstant { 1, PackedLogic4 { 137, Logic4::zero } },
            CallableFramePush { 17, { 0, 1 }, { }, { } },
            LoadConstant { 0, PackedLogic4 { 137, Logic4::one } },
            LoadConstant { 1, PackedLogic4::from_msb_string(result_value) },
            CallableFramePop { 17, { 1 }, { }, { } },
            WriteBlocking { restored, 0 },
            WriteBlocking { preserved, 1 },
            Halt { },
        };
        (void)framed.add_process(std::move(automatic));
        const auto framed_result = framed.run();
        require(
            framed_result.status == RunStatus::completed
                && framed.signal_value(restored).to_msb_string() == caller_value
                && framed.signal_value(preserved).to_msb_string() == result_value,
            "automatic callable frames restore wide caller state and preserve wide results");
    }

    const auto word =
        [](const std::uint64_t aval, const std::uint64_t bval = 0) {
            return PackedLogic4::from_aval_bval(
                32, aval, bval);
        };
    const auto expect_call_failure =
        [&](std::vector<Operation> operations,
            const std::string_view expected) {
            Interpreter failing;
            Process invalid;
            invalid.id = 0;
            invalid.name = "invalid_call_stack";
            invalid.register_count = 2;
            invalid.operations = std::move(operations);
            (void)failing.add_process(std::move(invalid));
            try {
                (void)failing.run();
                throw std::runtime_error(
                    "invalid SimIR call stack was accepted");
            } catch (const InterpreterError& error) {
                require(
                    std::string_view { error.what() }.find(expected)
                        != std::string_view::npos,
                    "SimIR call-stack diagnostic");
            }
        };
    const CallStack one_entry_stack { 0, 1, 1 };
    expect_call_failure(
        {
            LoadConstant { 0, word(0, 1) },
            LoadConstant { 1, word(0) },
            Return { one_entry_stack },
            Halt { },
        },
        "call-stack pointer is unknown");
    expect_call_failure(
        {
            LoadConstant { 0, word(1) },
            LoadConstant { 1, word(0) },
            Call { 3, 3, one_entry_stack },
            Halt { },
        },
        "call-stack capacity is exhausted");
    expect_call_failure(
        {
            LoadConstant { 0, word(0) },
            LoadConstant { 1, word(0) },
            Return { one_entry_stack },
            Halt { },
        },
        "call-stack underflow");
    expect_call_failure(
        {
            LoadConstant { 0, word(1) },
            LoadConstant { 1, word(99) },
            Return { one_entry_stack },
            Halt { },
        },
        "call-stack return target is invalid");
}

void test_simir_alternate_executor_validation()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    class FixedExecutor final : public ProcessExecutor {
    public:
        explicit FixedExecutor(const ProcessResumeResult result)
            : result_(result)
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            InstructionIndex) override
        {
            return result_;
        }

    private:
        ProcessResumeResult result_;
    };

    Interpreter invalid_boundary;
    Process process;
    process.id = 0;
    process.name = "invalid_boundary";
    process.register_count = 1;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Halt { },
    };
    const auto process_id = invalid_boundary.add_process(std::move(process));
    invalid_boundary.set_process_executor(
        process_id,
        std::make_unique<FixedExecutor>(ProcessResumeResult { 0, 1 }));
    try {
        (void)invalid_boundary.run();
        throw std::runtime_error(
            "a non-boundary executor result was accepted");
    } catch (const InterpreterError& error) {
        require(
            error.instruction() == 0
                && std::string_view { error.what() }.find(
                       "not a kernel boundary")
                    != std::string_view::npos,
            "invalid executor boundary diagnostic");
    }

    Interpreter late_install;
    Process halt;
    halt.id = 0;
    halt.name = "late_install";
    halt.operations = { Halt { } };
    const auto halt_id = late_install.add_process(std::move(halt));
    late_install.start();
    try {
        late_install.set_process_executor(
            halt_id,
            std::make_unique<FixedExecutor>(
                ProcessResumeResult { 0, 1 }));
        throw std::runtime_error("late executor installation was accepted");
    } catch (const std::logic_error& error) {
        require(
            std::string_view { error.what() }.find("after start")
                != std::string_view::npos,
            "late executor installation diagnostic");
    }
}

void test_simir_system_command()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    const auto status = interpreter.add_signal(
        { "top.system_status", PackedLogic4(32, Logic4::zero) });
    std::vector<std::optional<std::string>> commands;
    interpreter.set_system_command_hook(
        [&commands](const std::optional<std::string_view> command) {
            commands.emplace_back(command
                    ? std::optional<std::string> { *command }
                    : std::nullopt);
            return command ? std::int32_t { -17 } : std::int32_t { 41 };
        });
    Process process;
    process.id = 0;
    process.name = "system_command";
    process.register_count = 1;
    process.string_register_count = 1;
    process.operations = {
        LoadStringConstant { 0, "command with spaces" },
        SystemCommand { StringRegisterId { 0 }, RegisterId { 0 } },
        WriteBlocking { status, 0 },
        SystemCommand { std::nullopt, std::nullopt },
        Halt { },
    };
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && commands
                == std::vector<std::optional<std::string>> {
                    std::string { "command with spaces" }, std::nullopt }
            && interpreter.signal_value(status).low_word().bval == 0 && interpreter.signal_value(status).low_word().aval == static_cast<std::uint32_t>(-17),
        "$system preserves command text, NULL calls, task discard, and raw int results");

    Interpreter unavailable;
    Process missing;
    missing.id = 0;
    missing.name = "missing_system_command";
    missing.operations = {
        SystemCommand { std::nullopt, std::nullopt }, Halt { }
    };
    (void)unavailable.add_process(std::move(missing));
    try {
        (void)unavailable.run();
        throw std::runtime_error("missing $system service was accepted");
    } catch (const InterpreterError& error) {
        require(
            std::string_view { error.what() }.find(
                "$system service is unavailable")
                != std::string_view::npos,
            "missing $system service diagnostic");
    }
}

void test_simir_stochastic_queues()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    const auto full_before = interpreter.add_signal(
        { "top.full_before", PackedLogic4(32, Logic4::zero) });
    const auto mean_interarrival = interpreter.add_signal(
        { "top.mean_interarrival", PackedLogic4(32, Logic4::zero) });
    const auto maximum_occupancy = interpreter.add_signal(
        { "top.maximum_occupancy", PackedLogic4(32, Logic4::zero) });
    const auto removed_job = interpreter.add_signal(
        { "top.removed_job", PackedLogic4(32, Logic4::zero) });
    const auto removed_information = interpreter.add_signal(
        { "top.removed_information", PackedLogic4(32, Logic4::zero) });
    const auto shortest_wait = interpreter.add_signal(
        { "top.shortest_wait", PackedLogic4(32, Logic4::zero) });
    const auto longest_wait = interpreter.add_signal(
        { "top.longest_wait", PackedLogic4(32, Logic4::zero) });
    const auto average_wait = interpreter.add_signal(
        { "top.average_wait", PackedLogic4(32, Logic4::zero) });
    const auto full_after = interpreter.add_signal(
        { "top.full_after", PackedLogic4(32, Logic4::zero) });
    const auto empty_status = interpreter.add_signal(
        { "top.empty_status", PackedLogic4(32, Logic4::zero) });

    Process process;
    process.id = 0;
    process.name = "stochastic_queues";
    process.register_count = 11;
    const auto load = [&](const RegisterId id, const std::uint32_t value) {
        process.operations.emplace_back(LoadConstant {
            id, PackedLogic4::from_aval_bval(32, value, 0) });
    };
    const auto initialize_queue = [&]() {
        StochasticQueueOperation operation;
        operation.kind = StochasticQueueKind::initialize;
        operation.queue_id = 0;
        operation.queue_type = 1;
        operation.maximum_length = 2;
        operation.status = 3;
        process.operations.emplace_back(operation);
    };
    const auto add_queue = [&]() {
        StochasticQueueOperation operation;
        operation.kind = StochasticQueueKind::add;
        operation.queue_id = 0;
        operation.job_id = 4;
        operation.information_id = 5;
        operation.status = 3;
        process.operations.emplace_back(operation);
    };
    const auto remove_queue = [&]() {
        StochasticQueueOperation operation;
        operation.kind = StochasticQueueKind::remove;
        operation.queue_id = 0;
        operation.job_id = 9;
        operation.information_id = 10;
        operation.status = 3;
        process.operations.emplace_back(operation);
    };
    const auto queue_full = [&]() {
        StochasticQueueOperation operation;
        operation.kind = StochasticQueueKind::full;
        operation.queue_id = 0;
        operation.status = 3;
        operation.result = 6;
        process.operations.emplace_back(operation);
    };
    const auto examine_queue = [&]() {
        StochasticQueueOperation operation;
        operation.kind = StochasticQueueKind::examine;
        operation.queue_id = 0;
        operation.statistic_code = 7;
        operation.statistic_value = 8;
        operation.status = 3;
        process.operations.emplace_back(operation);
    };
    load(0, 7);
    load(1, 1);
    load(2, 2);
    initialize_queue();
    load(4, 11);
    load(5, 101);
    add_queue();
    process.operations.emplace_back(WaitFor { 4 });
    load(4, 12);
    load(5, 102);
    add_queue();
    queue_full();
    process.operations.emplace_back(WriteBlocking { full_before, 6 });
    load(7, 2);
    examine_queue();
    process.operations.emplace_back(WriteBlocking { mean_interarrival, 8 });
    load(7, 3);
    examine_queue();
    process.operations.emplace_back(WriteBlocking { maximum_occupancy, 8 });
    process.operations.emplace_back(WaitFor { 6 });
    remove_queue();
    process.operations.emplace_back(WriteBlocking { removed_job, 9 });
    process.operations.emplace_back(WriteBlocking { removed_information, 10 });
    for (const auto& [code, signal] : std::array {
             std::pair { 4U, shortest_wait },
             std::pair { 5U, longest_wait },
             std::pair { 6U, average_wait } }) {
        load(7, code);
        examine_queue();
        process.operations.emplace_back(WriteBlocking { signal, 8 });
    }
    remove_queue();
    queue_full();
    process.operations.emplace_back(WriteBlocking { full_after, 6 });
    remove_queue();
    process.operations.emplace_back(WriteBlocking { empty_status, 3 });
    process.operations.emplace_back(Halt { });
    (void)interpreter.add_process(std::move(process));
    require(
        interpreter.run().status == RunStatus::completed,
        "stochastic queue execution completes");
    const auto observed = [&](const SignalId signal) {
        const auto word = interpreter.signal_value(signal).low_word();
        require(word.bval == 0, "stochastic queue result is known");
        return static_cast<std::uint32_t>(word.aval);
    };
    require(
        observed(full_before) == 1
            && observed(mean_interarrival) == 4
            && observed(maximum_occupancy) == 2
            && observed(removed_job) == 11
            && observed(removed_information) == 101
            && observed(shortest_wait) == 10
            && observed(longest_wait) == 6
            && observed(average_wait) == 10
            && observed(full_after) == 0
            && observed(empty_status) == 3,
        "stochastic FIFO operations, status codes, and statistics");
}

} // namespace fsim::tests::runtime

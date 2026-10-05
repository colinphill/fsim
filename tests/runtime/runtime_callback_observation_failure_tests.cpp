// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

constexpr std::uint32_t target_width = 129U;
constexpr std::array<std::uint32_t, 2U> owner_offsets { 0U, 64U };
constexpr std::array<std::uint32_t, 2U> owner_widths { 64U, 65U };

struct FailureProbe {
    ProcessId output_process { };
    std::size_t successful_allocations { };
    bool armed { };

    static void trace(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<FailureProbe*>(context);
        if (record.kind == SchedulerTraceKind::task_begin
            && record.time == 2U
            && record.order == probe.output_process
            && !probe.armed) {
            arm_allocation_failure(probe.successful_allocations);
            probe.armed = true;
        }
    }
};

struct FailureResult {
    bool armed { };
    bool failed { };
    bool callback_entered { };
};

FailureResult run_case(const std::size_t failure_index)
{
    Interpreter interpreter;
    const auto first_input = interpreter.add_signal({
        "first_input", PackedLogic4 { owner_widths[0U], Logic4::zero }
    });
    const auto second_input = interpreter.add_signal({
        "second_input", PackedLogic4 { owner_widths[1U], Logic4::zero }
    });
    const auto target = interpreter.add_signal({
        "failure_target", PackedLogic4 { target_width, Logic4::z },
        ResolutionKind::sv_wire
    });

    const std::array inputs { first_input, second_input };
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        Process owner;
        owner.id = static_cast<ProcessId>(index);
        owner.name = "failure_owner_" + std::to_string(index);
        owner.register_count = 1U;
        owner.static_sensitivity = { { inputs[index], EdgeKind::any } };
        owner.driver_regions = {
            { target, owner_offsets[index], owner_widths[index], false }
        };
        owner.operations = {
            ReadSignal { 0U, inputs[index] },
            WriteUpdateSlice { target, 0U, owner_offsets[index] },
            WaitSensitivity { }, Jump { 0U }
        };
        (void)interpreter.add_process(std::move(owner));
    }

    Process output;
    output.id = 2U;
    output.name = "failure_output";
    output.register_count = 1U;
    output.register_value_kinds = { ValueKind::logic4 };
    output.operations = {
        WaitFor { 2U },
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::one } },
        FormatDisplay {
            0U,
            OutputFormat::binary,
            "callback-preparation-prefix-beyond-small-string-storage-",
            "-callback-preparation-suffix",
            true,
            false },
        Halt { } };
    const auto output_process = interpreter.add_process(std::move(output));
    if (output_process != 2U) {
        throw std::runtime_error { "callback failure process identity changed" };
    }

    interpreter.schedule_signal_at(
        first_input, PackedLogic4 { owner_widths[0U], Logic4::one }, 1U, 0U);
    interpreter.schedule_signal_at(
        second_input, PackedLogic4 { owner_widths[1U], Logic4::one }, 1U, 1U);
    bool callback_entered { };
    interpreter.set_output_hook(
        [&](ProcessId, std::string_view, bool, SimulationTick, std::uint64_t) {
            callback_entered = true;
        });

    interpreter.start();

    FailureProbe probe { output_process, failure_index };
    interpreter.scheduler().set_trace_hook(&probe, &FailureProbe::trace);
    FailureResult result;
    try {
        (void)interpreter.run(2U);
    } catch (const std::bad_alloc&) {
        result.failed = true;
    }
    clear_allocation_failure();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    result.armed = probe.armed;
    result.callback_entered = callback_entered;
    if (!probe.armed) {
        throw std::runtime_error {
            "the allocation failpoint did not reach the output callback task"
        };
    }
    return result;
}

void test_callback_preparation_failure_does_not_enter_user_hook()
{
    bool saw_preparation_failure { };
    bool reached_user_hook { };
    for (std::size_t index = 0U; index < 96U && !reached_user_hook; ++index) {
        const auto result = run_case(index);
        if (result.failed && !result.callback_entered) {
            saw_preparation_failure = true;
        }
        if (!result.failed && result.callback_entered) {
            reached_user_hook = true;
        }
    }
    if (!saw_preparation_failure || !reached_user_hook) {
        throw std::runtime_error {
            "the checked fallback sweep reached callback preparation failure "
            "and the no-failure user-hook boundary"
        };
    }
}

void test_plain_display_reaches_user_hook_without_failure()
{
    Interpreter interpreter;
    Process output;
    output.id = 0U;
    output.name = "plain_display_control";
    output.operations = {
        WaitFor { 2U }, Display { "plain-control" }, Halt { } };
    (void)interpreter.add_process(std::move(output));

    bool callback_entered { };
    interpreter.set_output_hook(
        [&](ProcessId, std::string_view text, bool, SimulationTick,
            std::uint64_t) {
            callback_entered = text == "plain-control";
        });
    interpreter.start();
    if (interpreter.run().status != RunStatus::completed
        || !callback_entered) {
        throw std::runtime_error {
            "a plain Display reaches the user hook without a failpoint"
        };
    }
}

} // namespace
} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_plain_display_reaches_user_hook_without_failure();
        fsim::tests::runtime::test_callback_preparation_failure_does_not_enter_user_hook();
    } catch (const std::exception& error) {
        fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
        std::cerr << error.what() << '\n';
        return 1;
    } catch (...) {
        fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
        std::cerr << "unknown callback-observation failure\n";
        return 1;
    }
    fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
    return 0;
}

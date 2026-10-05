// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"
#include "fsim/support/environment.hpp"

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

void test_fused_static_executor_bindings();

namespace {

#if defined(FSIM_HAS_LLVM)

using namespace fsim;
using namespace fsim::runtime;
using namespace fsim::runtime::simir;

struct Snapshot {
    RunResult result;
    std::string representative_value;
    std::string safe_value;
    std::string final_value;
    std::vector<std::tuple<SimulationTick, std::uint64_t, std::string>> timeline;
};

struct PureCohortSnapshot {
    RunResult result;
    std::string output;
    std::size_t bound_cohorts { };
    std::size_t hooked_suspensions { };
    std::vector<std::tuple<SimulationTick, std::uint64_t, std::string>>
        timeline;
    std::array<std::array<std::string, 5>, 2> registers;
};

void set_test_environment(const char* name, const char* value)
{
#if defined(_WIN32)
    assert(::_putenv_s(name, value == nullptr ? "" : value) == 0);
#else
    assert((value == nullptr ? ::unsetenv(name) : ::setenv(name, value, 1))
        == 0);
#endif
}

struct MixedUpdateResult {
    std::string slot;
    std::string callback;
    std::uint64_t unchanged_word_updates { };
};

class ScopedStderrCapture final {
public:
    explicit ScopedStderrCapture(std::ostringstream& output)
        : previous_(std::cerr.rdbuf(output.rdbuf()))
    {
    }

    ~ScopedStderrCapture() { std::cerr.rdbuf(previous_); }

private:
    std::streambuf* previous_;
};

Process make_writer(const SignalId safe, const SignalId unsafe)
{
    Process process;
    process.id = 0U;
    process.name = "connected_remap_direct";
    process.register_count = 4U;
    process.register_value_kinds = {
        ValueKind::logic9, ValueKind::logic9,
        ValueKind::logic9, ValueKind::logic4
    };
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_logic9_msb_string("HHHHHHHH") },
        WriteProjected { safe, 0U, 0U, 0U, ProjectedDelayMode::inertial },
        LoadConstant { 1U, PackedLogic4::from_logic9_msb_string("WWWWWWWW") },
        WriteProjected { unsafe, 1U, 5U, 5U, ProjectedDelayMode::inertial },
        LoadConstant { 2U, PackedLogic4::from_logic9_msb_string("0") },
        LoadConstant { 3U, PackedLogic4::from_msb_string("00000000000000000000000000000000") },
        WriteProjectedDynamicSlice {
            unsafe, 2U, DynamicIndex { 3U, 7, 0, 0 },
            0U, 0U, ProjectedDelayMode::inertial },
        Halt { },
    };
    return process;
}

Snapshot run_case(const compiler::JitOptimizationLevel level, const bool compiled)
{
    const std::array<std::uint32_t, 3> widths { 8U, 8U, 8U };
    const std::array<ValueKind, 3> kinds {
        ValueKind::logic9, ValueKind::logic9, ValueKind::logic9
    };
    const std::array<ResolutionKind, 3> resolutions {
        ResolutionKind::std_logic, ResolutionKind::std_logic,
        ResolutionKind::std_logic
    };
    auto representative = make_writer(0U, 1U);
    auto candidate = make_writer(1U, 2U);
    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process("connected_remap_direct", representative, widths, kinds);
        handle = jit->lookup("connected_remap_direct");
    }
    Interpreter interpreter;
    for (const auto name : { "A", "B", "C" }) {
        const auto id = interpreter.add_signal({ name, PackedLogic4::from_logic9_msb_string("UUUUUUUU"),
            ResolutionKind::std_logic, ValueKind::logic9 });
        assert(id < 3U);
    }
    const auto process_id = interpreter.add_process(std::move(candidate));
    if (compiled) {
        auto remap = std::make_shared<app::application_detail::LlvmProcessExecutor::SignalRemap>();
        remap->push_back({ 0U, 1U });
        remap->push_back({ 1U, 2U });
        interpreter.set_process_executor(process_id,
            std::make_unique<app::application_detail::LlvmProcessExecutor>(
                *jit, *handle, interpreter.process_program(process_id),
                widths, kinds, resolutions, std::move(remap), representative.id));
    }
    Snapshot snapshot;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == 2U) {
                snapshot.timeline.emplace_back(time,
                    interpreter.scheduler().delta(), value.to_msb_string());
            }
        });
    snapshot.result = interpreter.run();
    snapshot.representative_value
        = interpreter.signal_value(0U).to_msb_string();
    snapshot.safe_value = interpreter.signal_value(1U).to_msb_string();
    snapshot.final_value = interpreter.signal_value(2U).to_msb_string();
    return snapshot;
}

PureCohortSnapshot run_pure_cohort_case(
    const compiler::JitOptimizationLevel level, const bool compiled,
    const bool enable_execution_hook_after_first = false,
    const bool retain_debug_locals = true,
    const bool redirect_after_first_output = false)
{
    const std::array<std::uint32_t, 3> widths { 2U, 2U, 2U };
    const std::array<ValueKind, 3> kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    const std::array<ResolutionKind, 3> resolutions {
        ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none
    };
    std::unique_ptr<compiler::LlvmJit> jit;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        options.require_direct_update_slots = true;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
    }
    Interpreter interpreter;
    const auto a = interpreter.add_signal({
        "pure.a", PackedLogic4::from_msb_string("ZZ") });
    const auto b = interpreter.add_signal({
        "pure.b", PackedLogic4::from_msb_string("ZZ") });
    const auto output = interpreter.add_signal({
        "pure.out", PackedLogic4::from_msb_string("00") });
    assert(a == 0U && b == 1U && output == 2U);
    std::array<app::application_detail::LlvmProcessExecutor*, 2U>
        native_executors { };
    for (std::uint32_t bit = 0U; bit < 2U; ++bit) {
        Process process;
        process.id = bit;
        process.name = "pure_bit_and_" + std::to_string(bit);
        process.register_count = 5U;
        if (retain_debug_locals) {
            for (RegisterId reg = 0U; reg < 5U; ++reg) {
                DebugLocal local;
                local.name = "r" + std::to_string(reg);
                local.type_name = "logic";
                local.register_id = reg;
                local.width = reg < 2U ? 2U : 1U;
                process.debug_locals.push_back(std::move(local));
            }
        }
        process.static_sensitivity = {
            { a, EdgeKind::any }, { b, EdgeKind::any }
        };
        process.driver_regions = { { output, bit, 1U, false } };
        process.initialize = false;
        process.operations = {
            DebugPoint { }, DebugPoint { },
            ReadSignal { 0U, a }, Extract { 2U, 0U, bit, 1U },
            ReadSignal { 1U, b }, Extract { 3U, 1U, bit, 1U },
            Binary { BinaryOperator::bit_and, 4U, 2U, 3U },
            WriteUpdateSlice { output, 4U, bit },
            WaitSensitivity { }, Jump { 0U }
        };
        if (compiled) {
            jit->add_process(process.name, process, widths, kinds);
        }
        const auto id = interpreter.add_process(std::move(process));
        assert(id == bit);
        if (compiled) {
            const auto handle = jit->lookup(
                interpreter.process_program(id).name);
            assert(handle);
            auto executor = std::make_unique<app::application_detail::
                LlvmProcessExecutor>(
                *jit, handle, interpreter.process_program(id), widths,
                kinds, resolutions, nullptr, id);
            native_executors[bit] = executor.get();
            interpreter.set_process_executor(id, std::move(executor));
        }
    }
    constexpr std::string_view logic = "01XZ";
    for (std::size_t row = 0; row < 16U; ++row) {
        const auto left = std::string(2U, logic[row / 4U]);
        const auto right = std::string(2U, logic[row % 4U]);
        const auto time = static_cast<SimulationTick>((row + 1U) * 10U);
        interpreter.schedule_signal_at(
            a, PackedLogic4::from_msb_string(left), time, 0U);
        interpreter.schedule_signal_at(
            b, PackedLogic4::from_msb_string(right), time, 0U);
    }
    PureCohortSnapshot snapshot;
    bool execution_hook_enabled { };
    bool redirected { };
    interpreter.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == output) {
                snapshot.timeline.emplace_back(
                    time, interpreter.scheduler().delta(),
                    value.to_msb_string());
                if (compiled && redirect_after_first_output
                    && !redirected) {
                    redirected = true;
                    for (auto* executor : native_executors) {
                        assert(executor != nullptr);
                        executor->redirect(9U);
                    }
                }
                if (enable_execution_hook_after_first
                    && !execution_hook_enabled) {
                    execution_hook_enabled = true;
                    interpreter.set_execution_point_hook(
                        [&](Scheduler&, const ExecutionPoint& point) {
                            if (point.kind
                                == ExecutionPointKind::process_suspend) {
                                ++snapshot.hooked_suspensions;
                            }
                        });
                }
            }
        });
    snapshot.result = interpreter.run();
    if (compiled && redirect_after_first_output) {
        assert(redirected);
    }
    snapshot.output = interpreter.signal_value(output).to_msb_string();
    snapshot.bound_cohorts = compiled
        ? jit->active_cohort_binding_count()
        : 0U;
    if (retain_debug_locals) {
        for (ProcessId process = 0U; process < 2U; ++process) {
            for (RegisterId reg = 0U; reg < 5U; ++reg) {
                snapshot.registers[process][reg]
                    = interpreter.read_debug_local(process, reg)
                          .to_msb_string();
            }
        }
    }
    return snapshot;
}

MixedUpdateResult run_mixed_update_case(
    const compiler::JitOptimizationLevel level, const bool compiled,
    const std::optional<bool> disable_after_first = std::nullopt)
{
    auto wide = PackedLogic4(130U, Logic4::zero);
    wide.set(0U, Logic4::one);
    wide.set(64U, Logic4::x);
    wide.set(129U, Logic4::z);
    Process process;
    process.id = 0U;
    process.name = "direct_slot_with_packed_callback";
    process.register_count = 2U;
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(8U, 0xA5U, 0U) },
        WriteUpdate { 0U, 0U },
        WaitFor { 1U },
        WriteUpdate { 0U, 0U },
        LoadConstant { 1U, wide },
        WriteProjected { 1U, 1U, 0U, 0U, ProjectedDelayMode::inertial },
        Halt { },
    };
    const std::array<std::uint32_t, 2> widths { 8U, 130U };
    const std::array<ValueKind, 2> kinds {
        ValueKind::logic4, ValueKind::logic4
    };
    const std::array<ResolutionKind, 2> resolutions {
        ResolutionKind::none, ResolutionKind::none
    };
    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.require_direct_update_slots = true;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process(process.name, process, widths, kinds);
        handle = jit->lookup(process.name);
        assert((jit->frame_layout(*handle).direct_update_signals
            == std::vector<SignalId> { 0U, 1U }));
    }
    MixedUpdateResult snapshot;
    std::ostringstream profile;
    {
        ScopedStderrCapture capture { profile };
        Interpreter interpreter;
        const auto slot = interpreter.add_signal(
            { "slot", PackedLogic4(8U, Logic4::zero) });
        const auto callback = interpreter.add_signal(
            { "callback", PackedLogic4(130U, Logic4::zero) });
        assert(slot == 0U && callback == 1U);
        const auto process_id = interpreter.add_process(std::move(process));
        if (compiled) {
            interpreter.set_process_executor(process_id,
                std::make_unique<app::application_detail::LlvmProcessExecutor>(
                    *jit, *handle, interpreter.process_program(process_id),
                    widths, kinds, resolutions));
        }
        const auto before_second = interpreter.run(0U);
        assert(before_second.status == RunStatus::time_limit);
        assert(interpreter.signal_value(slot).to_msb_string() == "10100101");
        if (disable_after_first) {
            set_test_environment(
                "FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION",
                *disable_after_first ? "1" : nullptr);
        }
        const auto result = interpreter.run();
        assert(result.status == RunStatus::completed);
        snapshot.slot = interpreter.signal_value(slot).to_msb_string();
        snapshot.callback = interpreter.signal_value(callback).to_msb_string();
    }
    const auto text = profile.str();
    constexpr std::string_view marker { " word_unchanged=" };
    const auto start = text.find(marker);
    assert(start != std::string::npos);
    snapshot.unchanged_word_updates = std::stoull(text.substr(
        start + marker.size()));
    return snapshot;
}

#endif

}

int fsim_application_case_connected_remap_direct()
{
#if defined(FSIM_HAS_LLVM)
    test_fused_static_executor_bindings();
    const auto previous_profile = support::environment_variable(
        "FSIM_PROFILE_NATIVE_UPDATES");
    const auto previous_suppression = support::environment_variable(
        "FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION");
    for (const auto level : { compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto pure_reference = run_pure_cohort_case(level, false);
        const auto pure_native = run_pure_cohort_case(level, true);
        assert(pure_reference.result.status == RunStatus::completed);
        assert(pure_reference.output == "XX");
        assert(!pure_reference.timeline.empty());
        assert(pure_native.result.status == pure_reference.result.status);
        assert(pure_native.result.time == pure_reference.result.time);
        assert(pure_native.result.delta == pure_reference.result.delta);
        assert(pure_native.output == pure_reference.output);
        assert(pure_native.timeline == pure_reference.timeline);
        assert(pure_native.registers == pure_reference.registers);
        if (pure_native.bound_cohorts != 1U) {
            std::cerr << "CONNECTED_REMAP_BINDINGS level="
                      << static_cast<int>(level)
                      << " bound=" << pure_native.bound_cohorts << '\n';
        }
        // Both ready processes are held by one composite native binding.
        assert(pure_native.bound_cohorts == 1U);

        const auto hooked_reference = run_pure_cohort_case(
            level, false, true);
        const auto hooked_native = run_pure_cohort_case(
            level, true, true);
        assert(hooked_reference.hooked_suspensions > 0U);
        assert(hooked_native.hooked_suspensions
            == hooked_reference.hooked_suspensions);
        assert(hooked_native.timeline == hooked_reference.timeline);
        assert(hooked_native.registers == hooked_reference.registers);
        assert(hooked_native.output == hooked_reference.output);
        assert(hooked_native.bound_cohorts == 1U);

        const auto compact_reference = run_pure_cohort_case(
            level, false, false, false);
        const auto compact_native = run_pure_cohort_case(
            level, true, false, false);
        assert(compact_reference.result.status == RunStatus::completed);
        assert(compact_native.result.status == compact_reference.result.status);
        assert(compact_native.result.time == compact_reference.result.time);
        assert(compact_native.result.delta == compact_reference.result.delta);
        assert(compact_native.output == compact_reference.output);
        assert(compact_native.timeline == compact_reference.timeline);
        // The trusted wave reuses member leases rather than holding a
        // separate wave binding alongside the existing compact cohort.
        assert(compact_native.bound_cohorts == 1U);

        const auto redirected_native = run_pure_cohort_case(
            level, true, false, false, true);
        assert(redirected_native.result.status
            == compact_reference.result.status);
        assert(redirected_native.result.time
            == compact_reference.result.time);
        assert(redirected_native.result.delta
            == compact_reference.result.delta);
        assert(redirected_native.output == compact_reference.output);
        assert(redirected_native.timeline
            == compact_reference.timeline);

        const auto compact_hooked_reference = run_pure_cohort_case(
            level, false, true, false);
        const auto compact_hooked_native = run_pure_cohort_case(
            level, true, true, false);
        assert(compact_hooked_reference.hooked_suspensions > 0U);
        assert(compact_hooked_native.hooked_suspensions
            == compact_hooked_reference.hooked_suspensions);
        assert(compact_hooked_native.result.status
            == compact_hooked_reference.result.status);
        assert(compact_hooked_native.result.time
            == compact_hooked_reference.result.time);
        assert(compact_hooked_native.result.delta
            == compact_hooked_reference.result.delta);
        assert(compact_hooked_native.output
            == compact_hooked_reference.output);
        assert(compact_hooked_native.timeline
            == compact_hooked_reference.timeline);
        // The pure wave runs before the hook is installed. Later resumes use
        // the generic path so every execution-point callback still fires.
        assert(compact_hooked_native.bound_cohorts == 1U);

        const auto reference = run_case(level, false);
        const auto native = run_case(level, true);
        assert(reference.result.status == RunStatus::completed);
        assert(reference.representative_value == "UUUUUUUU");
        assert(reference.safe_value == "HHHHHHHH");
        assert(reference.final_value == "WWWWWWW0");
        assert(reference.timeline.size() == 2U);
        assert(std::get<0>(reference.timeline[0]) == 0U);
        assert(std::get<1>(reference.timeline[0]) == 0U);
        assert(std::get<2>(reference.timeline[0]) == "UUUUUUU0");
        assert(std::get<0>(reference.timeline[1]) == 5U);
        assert(std::get<1>(reference.timeline[1]) == 0U);
        assert(std::get<2>(reference.timeline[1]) == "WWWWWWW0");
        assert(native.result.status == reference.result.status);
        assert(native.result.time == reference.result.time);
        assert(native.result.delta == reference.result.delta);
        assert(native.representative_value == reference.representative_value);
        assert(native.safe_value == reference.safe_value);
        assert(native.final_value == reference.final_value);
        assert(native.timeline == reference.timeline);

        set_test_environment("FSIM_PROFILE_NATIVE_UPDATES", "1");
        set_test_environment(
            "FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION", nullptr);
        const auto mixed_reference = run_mixed_update_case(level, false);
        const auto mixed_native = run_mixed_update_case(level, true);
        assert(mixed_reference.slot == "10100101");
        assert(mixed_reference.callback.size() == 130U);
        assert(mixed_reference.callback.front() == 'Z');
        assert(mixed_reference.callback[65U] == 'X');
        assert(mixed_reference.callback.back() == '1');
        assert(mixed_native.slot == mixed_reference.slot);
        assert(mixed_native.callback == mixed_reference.callback);
        assert(mixed_native.unchanged_word_updates == 0U);

        // The first native segment has run before each switch changes. An
        // existing executor keeps its construction-time setting; the next
        // executor observes the new value.
        const auto enabled_then_disabled
            = run_mixed_update_case(level, true, true);
        const auto newly_disabled = run_mixed_update_case(level, true);
        const auto disabled_then_enabled
            = run_mixed_update_case(level, true, false);
        const auto newly_enabled = run_mixed_update_case(level, true);
        for (const auto& result : { enabled_then_disabled,
                 newly_disabled, disabled_then_enabled, newly_enabled }) {
            assert(result.slot == mixed_reference.slot);
            assert(result.callback == mixed_reference.callback);
        }
        assert(enabled_then_disabled.unchanged_word_updates == 0U);
        assert(newly_disabled.unchanged_word_updates == 1U);
        assert(disabled_then_enabled.unchanged_word_updates == 1U);
        assert(newly_enabled.unchanged_word_updates == 0U);
    }
    set_test_environment("FSIM_PROFILE_NATIVE_UPDATES",
        previous_profile ? previous_profile->c_str() : nullptr);
    set_test_environment("FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION",
        previous_suppression ? previous_suppression->c_str() : nullptr);
#endif
    return 0;
}

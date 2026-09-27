// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

std::pair<std::string, std::string> run_mixed_update_case(
    const compiler::JitOptimizationLevel level, const bool compiled)
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
    const auto result = interpreter.run();
    assert(result.status == RunStatus::completed);
    return { interpreter.signal_value(0U).to_msb_string(),
        interpreter.signal_value(1U).to_msb_string() };
}

#endif

}

int fsim_application_case_connected_remap_direct()
{
#if defined(FSIM_HAS_LLVM)
    for (const auto level : { compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
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

        const auto mixed_reference = run_mixed_update_case(level, false);
        const auto mixed_native = run_mixed_update_case(level, true);
        assert(mixed_reference.first == "10100101");
        assert(mixed_reference.second.size() == 130U);
        assert(mixed_reference.second.front() == 'Z');
        assert(mixed_reference.second[65U] == 'X');
        assert(mixed_reference.second.back() == '1');
        assert(mixed_native == mixed_reference);
    }
#endif
    return 0;
}

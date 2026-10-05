// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_fused_branch_safety.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_masked_error_branch_knownness()
{
    const auto make_process = [](const BinaryOperator comparison) {
        Process process;
        process.register_count = 3U;
        process.operations = {
            ReadSignal { 0U, 0U },
            LoadConstant { 1U, PackedLogic4(1U, Logic4::one) },
            Binary { comparison, 2U, 0U, 1U },
            Branch { 2U, 4U, 4U, UnknownBranchPolicy::error },
            WaitSensitivity { },
            Jump { 0U },
        };
        return process;
    };
    auto case_equal = make_process(BinaryOperator::case_equal);
    require(fsim::runtime::simir::detail::masked_branch_conditions_are_proven_known(
                case_equal.operations, case_equal.register_count),
        "case equality is a known Boolean even when a source signal is X/Z");
    auto equal = make_process(BinaryOperator::equal);
    require(!fsim::runtime::simir::detail::masked_branch_conditions_are_proven_known(
                equal.operations, equal.register_count),
        "logical equality remains unknown-capable for arbitrary signal inputs");

    Process known_operands;
    known_operands.register_count = 3U;
    known_operands.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        LoadConstant { 1U, PackedLogic4(1U, Logic4::zero) },
        Binary { BinaryOperator::equal, 2U, 0U, 1U },
        Branch { 2U, 4U, 4U, UnknownBranchPolicy::error },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(fsim::runtime::simir::detail::masked_branch_conditions_are_proven_known(
                known_operands.operations, known_operands.register_count),
        "logical equality is safe when both reaching operands are known");

    Process joined;
    joined.register_count = 3U;
    joined.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        Branch { 0U, 2U, 4U, UnknownBranchPolicy::when_false },
        Binary { BinaryOperator::case_equal, 2U, 0U, 0U },
        Jump { 5U },
        ReadSignal { 2U, 0U },
        Branch { 2U, 6U, 6U, UnknownBranchPolicy::error },
        DebugPoint { },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(!fsim::runtime::simir::detail::masked_branch_conditions_are_proven_known(
                joined.operations, joined.register_count),
        "a CFG join intersects knownness and rejects one unknown incoming definition");

    joined.operations.replace(4U, LoadConstant {
        2U, PackedLogic4(1U, Logic4::zero) });
    require(fsim::runtime::simir::detail::masked_branch_conditions_are_proven_known(
                joined.operations, joined.register_count),
        "a CFG join retains knownness when both reaching definitions are known");

    const auto cohort_candidate_exists = [](
        const BinaryOperator comparison,
        const UnknownBranchPolicy branch_policy,
        const InstructionIndex true_target) {
        Interpreter interpreter;
        const auto input = interpreter.add_signal({
            "four_state_input", PackedLogic4(1U, Logic4::x) });
        std::vector<ProcessId> owners;
        for (ProcessId index = 0U; index < 2U; ++index) {
            const auto output = interpreter.add_signal({
                "output_" + std::to_string(index),
                PackedLogic4(1U, Logic4::zero) });
            Process process;
            process.id = index;
            process.name = "error_branch_owner_" + std::to_string(index);
            process.register_count = 3U;
            process.static_sensitivity = { { input, EdgeKind::any } };
            process.driver_regions = { { output, 0U, 1U, true } };
            process.operations = {
                ReadSignal { 0U, input },
                LoadConstant { 1U, PackedLogic4(1U, Logic4::one) },
                Binary { comparison, 2U, 0U, 1U },
                Branch { 2U, true_target, 4U, branch_policy },
                WriteProjected { output, 0U, 0U, 0U,
                    ProjectedDelayMode::inertial },
                WaitSensitivity { },
                Jump { 0U },
            };
            owners.push_back(interpreter.add_process(std::move(process)));
        }
        interpreter.start();
        const auto candidates = interpreter.fused_static_cohort_candidates();
        return std::ranges::any_of(candidates, [&](const auto& candidate) {
            return candidate.members == owners && candidate.masked_all_active;
        });
    };
    require(cohort_candidate_exists(BinaryOperator::case_equal,
                UnknownBranchPolicy::error, 4U),
        "runtime cohort admission accepts an error branch proven Boolean by case equality");
    require(!cohort_candidate_exists(BinaryOperator::equal,
                UnknownBranchPolicy::error, 4U),
        "runtime cohort admission rejects an unknown-capable equality branch");
    require(cohort_candidate_exists(BinaryOperator::case_equal,
                UnknownBranchPolicy::when_false, 4U),
        "runtime cohort admission preserves a forward X/Z-as-false branch");
    require(!cohort_candidate_exists(BinaryOperator::case_equal,
                UnknownBranchPolicy::when_false, 3U),
        "runtime cohort admission still rejects a backward edge without an error branch");
}

struct Observation {
    std::vector<std::tuple<SimulationTick, std::uint64_t, std::string>> events;
    std::vector<std::string> settled;
    std::vector<std::string> drivers;
    std::vector<std::vector<std::string>> driver_frames;
    FusedMaskedRegionCounters counters;
    bool first_output_seen { };
};

std::string expected_slice_driver(const std::uint32_t width,
    const std::uint32_t offset, const std::uint32_t slice_width,
    const char value)
{
    auto result = std::string(width, 'Z');
    const auto first = static_cast<std::size_t>(width)
        - static_cast<std::size_t>(offset) - slice_width;
    result.replace(first, slice_width, slice_width, value);
    return result;
}

Observation run_terminal(const std::uint32_t width, const bool fused,
    const bool terminal_first, const bool capture_output,
    const bool capture_intermediate_state)
{
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    const auto input = interpreter.add_signal(
        { "input", PackedLogic4(width, Logic4::zero) });
    const auto auxiliary = interpreter.add_signal(
        { "auxiliary", PackedLogic4(1U, Logic4::zero) });
    const auto aggregate = interpreter.add_signal(
        { "aggregate", PackedLogic4(width, Logic4::z),
            ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal(
        { "output", PackedLogic4(width, Logic4::z),
            width == 129U ? ResolutionKind::sv_wire
                          : ResolutionKind::none });
    const auto add_terminal = [&] {
        Process consumer;
        consumer.id = terminal_first ? 0U : 2U;
        consumer.name = "terminal";
        consumer.register_count = 1U;
        consumer.static_sensitivity = { { aggregate, EdgeKind::any },
            { input, EdgeKind::any } };
        consumer.driver_regions = { { output, 0U,
            width == 129U ? 0U : width, true } };
        consumer.operations = { ReadSignal { 0U, aggregate },
            WriteUpdate { output, 0U }, WaitSensitivity { }, Jump { 0U } };
        return interpreter.add_process(std::move(consumer));
    };
    const auto early_terminal = terminal_first
        ? std::optional<ProcessId> { add_terminal() } : std::nullopt;
    std::array<ProcessId, 2U> owners;
    const auto split = width / 2U;
    for (std::uint32_t index = 0U; index < 2U; ++index) {
        const auto offset = index == 0U ? 0U : split;
        const auto size = index == 0U ? split : width - split;
        Process process;
        process.id = index + (terminal_first ? 1U : 0U);
        process.name = "owner_" + std::to_string(index);
        process.register_count = 2U;
        process.static_sensitivity = { { input, EdgeKind::any } };
        if (index != 0U) {
            process.static_sensitivity.push_back(
                { auxiliary, EdgeKind::any });
        }
        process.driver_regions = { { aggregate, offset, size, false } };
        process.operations = { ReadSignal { 0U, input },
            Extract { 1U, 0U, offset, size },
            WriteUpdateSlice { aggregate, 1U, offset },
            WaitSensitivity { }, Jump { 0U } };
        owners[index] = interpreter.add_process(std::move(process));
    }
    const auto terminal = terminal_first
        ? *early_terminal : add_terminal();
    Process observer;
    observer.id = 3U;
    observer.name = "observer";
    observer.initialize = false;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = { Display { "boundary" },
        WaitSensitivity { }, Jump { 0U } };
    (void)interpreter.add_process(std::move(observer));
    Observation result;
    if (capture_output) {
        interpreter.set_output_hook([&](ProcessId, std::string_view,
                                        bool, SimulationTick time,
                                        std::uint64_t delta) {
            if (!result.first_output_seen) {
                result.first_output_seen = true;
            }
            result.events.emplace_back(time, delta,
                interpreter.signal_value(output).to_msb_string());
        });
    }
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::one), 1U, 0U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::x), 2U, 0U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::z), 2U, 2U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::zero), 3U, 0U);
    interpreter.start();
    if (fused) {
        require(interpreter.fused_masked_region_candidates().empty(),
            "retired terminal planning exposes no masked candidates");
    }
    for (SimulationTick time = 0U; time <= 3U; ++time) {
        const auto run = interpreter.run(time);
        require(run.status == RunStatus::completed
                || run.status == RunStatus::time_limit,
            "mixed sink reaches each observation time");
        if (capture_intermediate_state) {
            auto driver_frame = std::vector<std::string> { };
            driver_frame.reserve(owners.size() + 1U);
            for (const auto id : owners) {
                driver_frame.push_back(
                    interpreter.driver_value(id, aggregate).to_msb_string());
            }
            driver_frame.push_back(
                interpreter.driver_value(terminal, output).to_msb_string());
            result.driver_frames.push_back(std::move(driver_frame));
        }
        if (capture_intermediate_state) {
            result.settled.push_back(
                interpreter.signal_value(output).to_msb_string());
        }
    }
    if (!capture_intermediate_state) {
        result.settled.push_back(
            interpreter.signal_value(output).to_msb_string());
    }
    for (const auto id : owners) {
        result.drivers.push_back(
            interpreter.driver_value(id, aggregate).to_msb_string());
    }
    result.drivers.push_back(
        interpreter.driver_value(terminal, output).to_msb_string());
    result.counters = interpreter.fused_masked_region_counters();
    return result;
}

Observation run_normalized_owner(const bool fused, const bool capture_output,
    const bool capture_intermediate_state)
{
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    const auto backing = interpreter.add_signal(
        { "container_backing", PackedLogic4(130U, Logic4::z) });
    const auto input = interpreter.add_signal(
        { "input", PackedLogic4(65U, Logic4::zero) });
    const auto output = interpreter.add_signal(
        { "aggregate", PackedLogic4(130U, Logic4::z),
            ResolutionKind::sv_wire });
    ContainerType type;
    type.fixed = true;
    type.element_width = 65U;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    const auto object = interpreter.add_container_object(
        { "array", default_container_value(type), std::nullopt });
    interpreter.add_container_signal_alias({ object, backing, true, true });
    Process unrelated;
    unrelated.id = 0U;
    unrelated.name = "unrelated_process";
    unrelated.initialize = false;
    unrelated.operations = { Halt { } };
    const auto unrelated_id
        = interpreter.add_process(std::move(unrelated));
    Process high;
    high.id = 1U;
    high.name = "array_reader";
    high.register_count = 2U;
    high.container_register_count = 1U;
    high.container_register_types = { type };
    high.static_sensitivity = { { backing, EdgeKind::any } };
    high.driver_regions = { { output, 65U, 65U, false } };
    high.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        ReadContainerObject { 0U, object },
        ContainerRead { 1U, 0U, 0U, true, false, false },
        WriteUpdateSlice { output, 1U, 65U },
        WaitSensitivity { }, Jump { 0U }
    };
    const auto high_id = interpreter.add_process(std::move(high));
    Process low;
    low.id = 2U;
    low.name = "direct_reader";
    low.register_count = 1U;
    low.static_sensitivity = { { input, EdgeKind::any } };
    low.driver_regions = { { output, 0U, 65U, false } };
    low.operations = { ReadSignal { 0U, input },
        WriteUpdateSlice { output, 0U, 0U },
        WaitSensitivity { }, Jump { 0U } };
    const auto low_id = interpreter.add_process(std::move(low));
    Process observer;
    observer.id = 3U;
    observer.name = "observer";
    observer.initialize = false;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = { Display { "boundary" },
        WaitSensitivity { }, Jump { 0U } };
    (void)interpreter.add_process(std::move(observer));
    Observation result;
    if (capture_output) {
        interpreter.set_output_hook([&](ProcessId, std::string_view,
                                        bool, SimulationTick time,
                                        std::uint64_t delta) {
            if (!result.first_output_seen) {
                result.first_output_seen = true;
            }
            result.events.emplace_back(time, delta,
                interpreter.signal_value(output).to_msb_string());
        });
    }
    interpreter.schedule_signal_at(backing,
        PackedLogic4(130U, Logic4::one), 1U, 0U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(65U, Logic4::one), 1U, 1U);
    interpreter.scheduler().schedule_at(2U, SchedulerPhase::active,
        std::numeric_limits<StableOrder>::max(), [&](Scheduler&) {
            auto changed = interpreter.container_object_value(object);
            changed.elements.front() = PackedLogic4(65U, Logic4::x);
            interpreter.deposit_container_object(object, std::move(changed));
        });
    interpreter.schedule_signal_at(input,
        PackedLogic4(65U, Logic4::z), 3U, 0U);
    interpreter.start();
    if (fused) {
        const auto& original = interpreter.process_program(high_id);
        const auto& compatibility
            = interpreter.fused_masked_member_program(high_id);
        require(&compatibility == &original
                && compatibility.container_register_count == 1U
                && operation_holds<ReadContainerObject>(
                    compatibility.operations[1U])
                && operation_holds<ContainerRead>(
                    compatibility.operations[2U]),
            "the retired member accessor keeps original checked container operations");
        require(&interpreter.fused_masked_member_program(unrelated_id)
                == &interpreter.process_program(unrelated_id),
            "the compatibility accessor resolves every process to its original program");
        require(interpreter.fused_masked_region_candidates().empty(),
            "fixed-container members stay on the ordinary checked process path");
    }
    for (SimulationTick time = 0U; time <= 3U; ++time) {
        const auto run = interpreter.run(time);
        require(run.status == RunStatus::completed
                || run.status == RunStatus::time_limit,
            "the normalized-owner test reaches each observation time");
        if (capture_intermediate_state) {
            auto driver_frame = std::vector<std::string> { };
            driver_frame.reserve(2U);
            for (const auto id : { high_id, low_id }) {
                driver_frame.push_back(
                    interpreter.driver_value(id, output).to_msb_string());
            }
            result.driver_frames.push_back(std::move(driver_frame));
        }
        if (capture_intermediate_state) {
            result.settled.push_back(
                interpreter.signal_value(output).to_msb_string());
        }
    }
    if (!capture_intermediate_state) {
        result.settled.push_back(
            interpreter.signal_value(output).to_msb_string());
    }
    for (const auto id : { high_id, low_id }) {
        result.drivers.push_back(
            interpreter.driver_value(id, output).to_msb_string());
    }
    result.counters = interpreter.fused_masked_region_counters();
    return result;
}

} // namespace

void test_fused_masked_fallback_semantics()
{
    test_masked_error_branch_knownness();
    const auto require_terminal_frames = [](
        const Observation& observation, const std::uint32_t width) {
        const auto split = width / 2U;
        const auto ones = std::string(width, '1');
        const auto zeros = std::string(width, '0');
        const auto one_owner_zero = expected_slice_driver(
            width, 0U, split, '1');
        const auto one_owner_one = expected_slice_driver(
            width, split, width - split, '1');
        const auto zero_owner_zero = expected_slice_driver(
            width, 0U, split, '0');
        const auto zero_owner_one = expected_slice_driver(
            width, split, width - split, '0');
        require(observation.settled.size() == 4U
                && observation.driver_frames.size() == 4U
                && observation.settled[1U] == ones
                && observation.driver_frames[1U]
                    == std::vector<std::string> {
                        one_owner_zero, one_owner_one, ones },
            "the terminal output and both raw owners publish the first all-one wave");
        require(observation.settled[3U] == zeros
                && observation.driver_frames[3U]
                    == std::vector<std::string> {
                        zero_owner_zero, zero_owner_one, zeros },
            "the terminal output and both raw owners publish the final all-zero wave");
    };
    for (const auto width : { 65U, 129U }) {
        for (const auto terminal_first : { false, true }) {
            const auto reference
                = run_terminal(width, false, terminal_first, false, false);
            const auto candidate
                = run_terminal(width, true, terminal_first, false, false);
            require(candidate.events == reference.events
                    && candidate.settled == reference.settled
                    && candidate.drivers == reference.drivers,
                "mixed owned/normal sinks preserve exact boundary deltas and drivers");
            require(candidate.counters.masked_calls == 0U,
                "the retired terminal executor stays on checked fallback");

            const auto observed_reference
                = run_terminal(width, false, terminal_first, true, true);
            const auto observed_candidate
                = run_terminal(width, true, terminal_first, true, true);
            require_terminal_frames(observed_reference, width);
            require_terminal_frames(observed_candidate, width);
            require(!observed_reference.events.empty()
                    && observed_candidate.events == observed_reference.events
                    && observed_candidate.settled == observed_reference.settled
                    && observed_candidate.drivers == observed_reference.drivers,
                "observed mixed sinks preserve boundary values, deltas and drivers");
            require(observed_candidate.first_output_seen
                    && observed_candidate.counters.masked_calls == 0U,
                "observed mixed sinks stay on checked fallback");
        }
    }
    const auto reference = run_normalized_owner(false, false, false);
    const auto candidate = run_normalized_owner(true, false, false);
    require(candidate.events == reference.events
            && candidate.settled == reference.settled
            && candidate.drivers == reference.drivers,
        "late alias cache mutation preserves fallback values and raw drivers");
    require(candidate.counters.masked_calls == 0U,
        "normalized owned members stay on checked fallback after route retirement");

    const auto observed_reference = run_normalized_owner(false, true, true);
    const auto observed_candidate = run_normalized_owner(true, true, true);
    const auto check_alias_frames = [](const Observation& observation) {
        const auto ones = std::string(130U, '1');
        const auto high_owner_one = expected_slice_driver(130U, 65U, 65U, '1');
        const auto low_owner_one = expected_slice_driver(130U, 0U, 65U, '1');
        const auto aggregate_xz = std::string(65U, 'X')
            + std::string(65U, 'Z');
        const auto high_owner_x = expected_slice_driver(
            130U, 65U, 65U, 'X');
        const auto low_owner_z = expected_slice_driver(
            130U, 0U, 65U, 'Z');
        require(observation.settled.size() == 4U
                && observation.driver_frames.size() == 4U
                && observation.settled[1U] == ones
                && observation.driver_frames[1U]
                    == std::vector<std::string> {
                        high_owner_one, low_owner_one },
            "the aliased aggregate and raw owners publish the first all-one wave");
        require(observation.settled[3U] == aggregate_xz
                && observation.driver_frames[3U]
                    == std::vector<std::string> {
                        high_owner_x, low_owner_z },
            "the aliased aggregate and raw owners retain final X/Z slices");
    };
    check_alias_frames(observed_reference);
    check_alias_frames(observed_candidate);
    require(!observed_reference.events.empty()
            && observed_candidate.events == observed_reference.events
            && observed_candidate.settled == observed_reference.settled
            && observed_candidate.drivers == observed_reference.drivers,
        "observed normalized owners preserve boundary values, deltas and drivers");
    require(observed_candidate.first_output_seen
            && observed_candidate.counters.masked_calls == 0U,
        "observed normalized owners stay on checked fallback");
}

} // namespace fsim::tests::runtime

// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_shift_test.hpp"

#include "llvm_jit_region_frontier_staging_test.hpp"

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "llvm/region_frontier_codegen_v2.hpp"
#include "llvm/region_frontier_kernel_plan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using runtime::Logic4;
using runtime::PackedLogic4;
using runtime::RunStatus;
using runtime::simir::EdgeKind;
using runtime::simir::Interpreter;
using runtime::simir::Process;
using runtime::simir::ProcessId;
using runtime::simir::ProcessSchedulingDomain;
using runtime::simir::RegionConeActivationKernel;
using runtime::simir::RegionGraph;
using runtime::simir::RegionObservation;
using runtime::simir::RegionSignalDescriptor;
using runtime::simir::ShiftOperator;
using runtime::simir::SignalId;
using runtime::simir::SignalUpdateDomain;
using runtime::simir::ValueKind;

constexpr ProcessId graph_producer_id = 0U;
constexpr ProcessId graph_consumer_id = 1U;
constexpr ProcessId producer_id = 7U;
constexpr ProcessId consumer_id = 23U;
constexpr SignalId value_signal_id = 0U;
constexpr SignalId amount_signal_id = 1U;
constexpr SignalId internal_signal_id = 2U;
constexpr SignalId boundary_signal_id = 3U;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] RegionConeActivationKernel make_shift_kernel(
    const std::uint32_t value_width,
    const std::uint32_t amount_width,
    const ShiftOperator operation,
    const bool signed_amount)
{
    require(value_width != 0U && amount_width != 0U,
        "frontier shift fixture operands have nonzero widths");

    std::vector<RegionSignalDescriptor> signals;
    signals.reserve(4U);
    signals.emplace_back(value_width);
    signals.emplace_back(amount_width);
    signals.emplace_back(value_width);
    signals.emplace_back(value_width);
    signals[boundary_signal_id].observations = RegionObservation::current;

    Process producer;
    producer.id = graph_producer_id;
    producer.name = "frontier_shift_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 3U;
    producer.register_value_kinds.assign(3U, ValueKind::logic4);
    producer.static_sensitivity = {
        { value_signal_id, EdgeKind::any },
        { amount_signal_id, EdgeKind::any },
    };
    producer.driver_regions = {
        { internal_signal_id, 0U, 0U, true },
    };
    producer.operations = {
        runtime::simir::ReadSignal { 0U, value_signal_id },
        runtime::simir::ReadSignal { 1U, amount_signal_id },
        runtime::simir::Shift {
            operation, 2U, 0U, 1U, signed_amount,
        },
        runtime::simir::WriteUpdate {
            internal_signal_id, 2U,
            SignalUpdateDomain::systemverilog_active,
        },
        runtime::simir::WaitSensitivity { },
        runtime::simir::Jump { 0U },
    };

    Process consumer;
    consumer.id = graph_consumer_id;
    consumer.name = "frontier_shift_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    consumer.register_count = 1U;
    consumer.register_value_kinds = { ValueKind::logic4 };
    consumer.static_sensitivity = {
        { internal_signal_id, EdgeKind::any },
    };
    consumer.driver_regions = {
        { boundary_signal_id, 0U, 0U, true },
    };
    consumer.operations = {
        runtime::simir::ReadSignal { 0U, internal_signal_id },
        runtime::simir::WriteUpdate {
            boundary_signal_id, 0U,
            SignalUpdateDomain::systemverilog_active,
        },
        runtime::simir::WaitSensitivity { },
        runtime::simir::Jump { 0U },
    };

    const std::array<const Process*, 2U> programs { &producer, &consumer };
    const auto graph = RegionGraph::build(programs, signals);
    const auto& components = graph.certificate_inventory().components;
    require(components.size() == 1U
            && components.front().members
                == std::vector<ProcessId> {
                    graph_producer_id, graph_consumer_id,
                }
            && components.front().structural_internal_signal_candidates
                == std::vector<SignalId> { internal_signal_id },
        "the graph certifies the two-member shift cone");

    auto program = graph.build_compute_program(0U, programs);
    require(program.has_value(),
        "the certified shift cone builds an activation kernel");
    auto kernel = std::move(program->activation_kernel);
    for (auto& member : kernel.members) {
        if (member.process == graph_producer_id) {
            member.process = producer_id;
        } else if (member.process == graph_consumer_id) {
            member.process = consumer_id;
        }
    }
    for (auto& output : kernel.outputs) {
        if (output.owner == graph_producer_id) {
            output.owner = producer_id;
        } else if (output.owner == graph_consumer_id) {
            output.owner = consumer_id;
        }
    }
    require(kernel.internal_signals
                == std::vector<SignalId> { internal_signal_id },
        "the shift result remains the certified internal commit plane");
    require(RegionFrontierKernelPlan::try_create(kernel).has_value()
            == !signed_amount,
        "the frontier admits unsigned shift counts and declines signed counts");
    return kernel;
}

[[nodiscard]] RegionFrontierTestValue test_value(const PackedLogic4& value)
{
    return {
        std::vector<std::uint64_t>(value.aval_words().begin(),
            value.aval_words().end()),
        std::vector<std::uint64_t>(value.bval_words().begin(),
            value.bval_words().end()),
    };
}

[[nodiscard]] PackedLogic4 interpreter_shift(
    const PackedLogic4& value, const PackedLogic4& amount,
    const ShiftOperator operation)
{
    Interpreter interpreter;
    const auto value_input = interpreter.add_signal({
        "frontier.shift.value", value,
    });
    const auto amount_input = interpreter.add_signal({
        "frontier.shift.amount", amount,
    });
    const auto output = interpreter.add_signal({
        "frontier.shift.output", PackedLogic4(value.width(), Logic4::x),
    });

    Process process;
    process.id = 0U;
    process.name = "frontier_shift_interpreter_oracle";
    process.register_count = 3U;
    process.register_value_kinds.assign(3U, ValueKind::logic4);
    process.operations = {
        runtime::simir::ReadSignal { 0U, value_input },
        runtime::simir::ReadSignal { 1U, amount_input },
        runtime::simir::Shift { operation, 2U, 0U, 1U, false },
        runtime::simir::WriteBlocking { output, 2U },
        runtime::simir::Halt { },
    };
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(result.status == RunStatus::completed,
        "the interpreter shift oracle completes");
    return interpreter.signal_value(output);
}

[[nodiscard]] PackedLogic4 count_with_bits(
    const std::uint32_t width,
    const std::initializer_list<std::uint32_t> one_bits)
{
    PackedLogic4 result(width, Logic4::zero);
    for (const auto bit : one_bits) {
        require(bit < width, "count witness bits fit the count width");
        result.set(bit, Logic4::one);
    }
    return result;
}

[[nodiscard]] PackedLogic4 count_from_u32(
    const std::uint32_t width, const std::uint32_t value)
{
    PackedLogic4 result(width, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < 32U && bit < width; ++bit) {
        if (((value >> bit) & 1U) != 0U) {
            result.set(bit, Logic4::one);
        }
    }
    return result;
}

[[nodiscard]] PackedLogic4 known_source(const std::uint32_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    result.set(0U, Logic4::one);
    if (width > 1U) {
        result.set(width - 1U, Logic4::one);
    }
    for (std::uint32_t bit = 3U; bit < width; bit += 7U) {
        result.set(bit, Logic4::one);
    }
    return result;
}

[[nodiscard]] PackedLogic4 four_state_source(const std::uint32_t width)
{
    auto result = known_source(width);
    result.set(0U, Logic4::z);
    result.set(width / 2U, Logic4::x);
    if (width > 2U) {
        result.set(width - 1U, Logic4::z);
    }
    return result;
}

void run_actual_case(const RegionConeActivationKernel& kernel,
    const runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    const ShiftOperator operation, const PackedLogic4& source,
    const PackedLogic4& amount, const std::uint64_t generation)
{
    const auto expected = interpreter_shift(source, amount, operation);
    auto initial = expected;
    initial.set(0U, expected.get(0U) == Logic4::zero
            ? Logic4::one
            : Logic4::zero);
    RegionFrontierStagingValues values;
    values.external_inputs = {
        { value_signal_id, test_value(source) },
        { amount_signal_id, test_value(amount) },
    };
    values.initial_internal = test_value(initial);
    values.expected_internal = test_value(expected);
    values.initial_boundary = test_value(initial);
    values.expected_boundary = test_value(expected);
    run_region_frontier_staging_tests(kernel, entry, layout,
        generation, std::move(values));
}

void run_shift_shape(const std::uint32_t value_width,
    const std::uint32_t amount_width, const ShiftOperator operation,
    const JitOptimizationLevel optimization,
    std::uint64_t& generation)
{
    const auto kernel = make_shift_kernel(value_width, amount_width,
        operation, false);
    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    options.code_coverage_identity = "disabled";
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    const auto identity = std::string { "frontier-logic4-shift-" }
        + std::to_string(value_width) + "-"
        + std::to_string(amount_width) + "-"
        + std::to_string(static_cast<std::uint8_t>(operation)) + "-"
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, options, identity);
    require(executor != nullptr && executor->step_entry() != nullptr,
        "the certified shift kernel materializes a generated frontier entry");

    const auto run = [&](const PackedLogic4& source,
                         const PackedLogic4& amount) {
        run_actual_case(kernel, executor->step_entry(), executor->layout(),
            operation, source, amount, generation++);
    };
    const auto known = known_source(value_width);
    const auto four_state = four_state_source(value_width);
    const auto zero_count = count_with_bits(amount_width, { });
    const auto one_count = count_with_bits(amount_width, { 0U });
    const auto near_width_count
        = count_from_u32(amount_width, value_width - 1U);
    const auto width_count = count_from_u32(amount_width, value_width);
    auto high_count = count_with_bits(amount_width, { amount_width - 1U });
    high_count.set(amount_width > 128U ? 100U : 5U, Logic4::one);
    auto unknown_x_count = count_with_bits(amount_width, { 5U });
    unknown_x_count.set(0U, Logic4::x);
    auto unknown_z_count = count_with_bits(amount_width, { 3U });
    unknown_z_count.set(amount_width - 1U, Logic4::z);

    run(known, zero_count);
    run(known, one_count);
    run(four_state, near_width_count);
    run(four_state, width_count);
    run(four_state, high_count);
    run(known, unknown_x_count);
    run(known, unknown_z_count);
}

} // namespace

void run_region_frontier_shift_tests()
{
    constexpr std::array<std::uint32_t, 5U> value_widths {
        1U, 65U, 129U, 256U, 1024U,
    };
    constexpr std::array<std::uint32_t, 5U> amount_widths {
        65U, 129U, 1024U, 65U, 129U,
    };
    constexpr std::array<ShiftOperator, 6U> operations {
        ShiftOperator::logical_left,
        ShiftOperator::logical_right,
        ShiftOperator::arithmetic_right,
        ShiftOperator::arithmetic_left,
        ShiftOperator::rotate_left,
        ShiftOperator::rotate_right,
    };
    std::uint64_t generation = 1000U;
        for (const auto optimization : {
             JitOptimizationLevel::o0,
             JitOptimizationLevel::o2,
         }) {
        for (std::size_t width_index = 0U;
             width_index < value_widths.size(); ++width_index) {
            const auto width = value_widths[width_index];
            const auto amount_width = amount_widths[width_index];
            for (const auto operation : operations) {
                run_shift_shape(width, amount_width, operation,
                    optimization, generation);
            }
        }
    }

    for (const auto operation : operations) {
        const auto kernel = make_shift_kernel(65U, 129U, operation, true);
        require(!RegionFrontierKernelPlan::try_create(kernel).has_value(),
            "signed shift counts decline before native entry generation");
    }
}

} // namespace fsim::compiler::test

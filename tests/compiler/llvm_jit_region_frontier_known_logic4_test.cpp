// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_known_logic4_test.hpp"

#include "llvm_jit_region_frontier_staging_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "llvm/region_frontier_codegen_v2.hpp"
#include "llvm/region_frontier_kernel_plan.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using runtime::Logic4;
using runtime::Logic9;
using runtime::PackedLogic4;
using runtime::PackedLogic9;
using runtime::simir::RegionConeActivationKernel;
using runtime::simir::RegionGraph;
using runtime::simir::RegionSignalDescriptor;
using runtime::simir::RegionFrontierEventKindV2;
using runtime::simir::RegionFrontierPlaneFlagsV2;
using runtime::simir::RegionFrontierSignalLayoutV2;
using runtime::simir::RegionFrontierWriteSiteV2;
using runtime::simir::Process;
using runtime::simir::ProcessId;
using runtime::simir::ProcessSchedulingDomain;
using runtime::simir::RegionObservation;
using runtime::simir::SignalUpdateDomain;
using runtime::simir::SignalId;
using runtime::simir::ValueKind;
using runtime::simir::EdgeKind;
using runtime::simir::LoadConstant;
using runtime::simir::Operation;
using runtime::simir::scratch::RegionFrontierFanoutRangeSpan;
using runtime::simir::scratch::RegionFrontierFanoutSensitivityRange;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] RegionFrontierTestValue test_value(
    const PackedLogic4& value)
{
    return {
        std::vector<std::uint64_t>(value.aval_words().begin(),
            value.aval_words().end()),
        std::vector<std::uint64_t>(value.bval_words().begin(),
            value.bval_words().end()),
    };
}

[[nodiscard]] PackedLogic4 invert_value(const PackedLogic4& source)
{
    PackedLogic4 result(source.width(), Logic4::zero);
    for (std::size_t bit = 0U; bit < source.width(); ++bit) {
        result.set(bit, runtime::logic_not(source.get(bit)));
    }
    return result;
}

[[nodiscard]] RegionFrontierStagingValues staging_values(
    const PackedLogic4& source)
{
    const auto expected = invert_value(source);
    RegionFrontierStagingValues values;
    values.external_input = test_value(source);
    values.initial_internal = {
        std::vector<std::uint64_t>(source.aval_words().size(), 0U),
        std::vector<std::uint64_t>(source.bval_words().size(), 0U),
    };
    values.expected_internal = test_value(expected);
    values.initial_boundary = values.initial_internal;
    values.expected_boundary = values.expected_internal;
    return values;
}

[[nodiscard]] RegionConeActivationKernel
make_unknown_constant_frontier_kernel(const std::uint32_t width,
    const bool explicit_full_width_internal_sensitivity = false)
{
    constexpr ProcessId graph_producer_id = 0U;
    constexpr ProcessId graph_consumer_id = 1U;
    constexpr ProcessId producer_id = 7U;
    constexpr ProcessId consumer_id = 23U;
    constexpr SignalId input_id = 0U;
    constexpr SignalId internal_id = 1U;
    constexpr SignalId output_id = 2U;

    std::vector<RegionSignalDescriptor> signals;
    signals.emplace_back(width);
    signals.emplace_back(width);
    signals.emplace_back(width);
    signals[output_id].observations = RegionObservation::current;

    Process producer;
    producer.id = graph_producer_id;
    producer.name = "frontier_unknown_constant_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 2U;
    producer.register_value_kinds.assign(2U, ValueKind::logic4);
    producer.static_sensitivity = { { input_id, EdgeKind::any } };
    producer.driver_regions = { { internal_id, 0U, 0U, true } };
    producer.operations.push_back(runtime::simir::ReadSignal {
        0U, input_id,
    });
    producer.operations.push_back(runtime::simir::LoadConstant {
        1U, PackedLogic4(width, Logic4::x),
    });
    producer.operations.push_back(runtime::simir::WriteUpdate {
        internal_id, 1U, SignalUpdateDomain::systemverilog_active,
    });
    producer.operations.push_back(runtime::simir::WaitSensitivity { });
    producer.operations.push_back(runtime::simir::Jump { 0U });

    Process consumer;
    consumer.id = graph_consumer_id;
    consumer.name = "frontier_unknown_constant_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    consumer.register_count = 1U;
    consumer.register_value_kinds = { ValueKind::logic4 };
    consumer.static_sensitivity = {
        { internal_id, EdgeKind::any, 0U,
            explicit_full_width_internal_sensitivity ? width : 0U },
    };
    consumer.driver_regions = { { output_id, 0U, 0U, true } };
    consumer.operations = {
        runtime::simir::ReadSignal { 0U, internal_id },
        runtime::simir::WriteUpdate {
            output_id, 0U, SignalUpdateDomain::systemverilog_active,
        },
        runtime::simir::WaitSensitivity { },
        runtime::simir::Jump { 0U },
    };

    const std::array<const Process*, 2U> programs { &producer, &consumer };
    const auto graph = RegionGraph::build(programs, signals);
    const auto& components = graph.certificate_inventory().components;
    require(components.size() == 1U
            && components.front().structural_internal_signal_candidates
                == std::vector<SignalId> { internal_id },
        "the graph certifies the unknown-constant producer and consumer edge");
    auto program = graph.build_compute_program(0U, programs);
    require(program.has_value(),
        "the certified constant producer builds an activation kernel");
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
                == std::vector<SignalId> { internal_id },
        "the certified constant write remains the internal commit plane");
    const auto kernel_consumer = std::ranges::find(kernel.members, consumer_id,
        &runtime::simir::RegionConeKernelMember::process);
    require(kernel_consumer != kernel.members.end(),
        "the graph-built kernel retains its internal-signal consumer");
    const auto kernel_sensitivity = std::ranges::find_if(
        kernel_consumer->sensitivities,
        [internal_id](const auto& sensitivity) {
            return sensitivity.signal == internal_id;
        });
    require(kernel_sensitivity != kernel_consumer->sensitivities.end()
            && kernel_sensitivity->offset == 0U
            && kernel_sensitivity->width
                == (explicit_full_width_internal_sensitivity ? width : 0U),
        "the graph preserves the canonical or explicit full-width sensitivity tuple");
    return kernel;
}

[[nodiscard]] PackedLogic4 alternating_known_value(
    const std::uint32_t width)
{
    PackedLogic4 value(width, Logic4::zero);
    for (std::uint32_t bit = 1U; bit < width; bit += 2U) {
        value.set(bit, Logic4::one);
    }
    return value;
}

void set_internal_sensitivity_ranges(RegionConeActivationKernel& kernel,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges)
{
    require(kernel.internal_signals.size() == 1U,
        "the sensitivity-range fixture has one private signal");
    const auto internal_signal = kernel.internal_signals.front();
    const auto reader = std::ranges::find_if(kernel.members,
        [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(reader != kernel.members.end(),
        "the sensitivity-range fixture has a private-signal reader");
    std::erase_if(reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    for (const auto& [offset, width] : ranges) {
        reader->sensitivities.push_back({ internal_signal, EdgeKind::any,
            offset, width });
    }
}

[[nodiscard]] RegionFrontierTestValue test_value(const PackedLogic9& value)
{
    RegionFrontierTestValue result;
    result.value_kind = ValueKind::logic9;
    std::vector<std::uint64_t>* const planes[] = {
        &result.aval, &result.bval, &result.plane2, &result.plane3,
    };
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        const auto words = value.plane(plane);
        planes[plane]->assign(words.begin(), words.end());
    }
    return result;
}

[[nodiscard]] RegionFrontierStagingValues range_staging_values(
    const PackedLogic4& external, const PackedLogic4& initial_internal)
{
    RegionFrontierStagingValues values;
    values.external_input = test_value(external);
    values.initial_internal = test_value(initial_internal);
    values.expected_internal = test_value(external);
    PackedLogic4 boundary(external.width(), Logic4::zero);
    values.initial_boundary = test_value(boundary);
    values.expected_boundary = test_value(boundary);
    return values;
}

[[nodiscard]] RegionFrontierStagingValues range_staging_values(
    const PackedLogic9& external, const PackedLogic9& initial_internal)
{
    RegionFrontierStagingValues values;
    values.external_input = test_value(external);
    values.initial_internal = test_value(initial_internal);
    values.expected_internal = test_value(external);
    PackedLogic9 boundary(external.width(), Logic9::u);
    values.initial_boundary = test_value(boundary);
    values.expected_boundary = test_value(boundary);
    return values;
}

void run_logic4_sensitivity_case(const std::uint32_t width,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges,
    const PackedLogic4& external, const PackedLogic4& initial_internal,
    const std::size_t expected_activation_events,
    const JitOptimizationLevel optimization, const std::string_view label)
{
    auto kernel = make_certified_frontier_kernel(width,
        SignalUpdateDomain::systemverilog_active, ValueKind::logic4);
    set_internal_sensitivity_ranges(kernel, ranges);
    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    const auto identity = std::string { "frontier-internal-range-" }
        + std::string { label } + "-"
        + std::to_string(static_cast<std::uint8_t>(optimization));
    const auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, options, identity);
    require(executor != nullptr && executor->step_entry() != nullptr,
        "the range-sensitive Logic4 kernel materializes at O0 and O2");
    run_region_frontier_sensitivity_fanout_witness(kernel,
        executor->step_entry(), executor->layout(), 91U,
        range_staging_values(external, initial_internal),
        expected_activation_events);
}

void run_logic9_sensitivity_case(const std::uint32_t width,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges,
    const PackedLogic9& external, const PackedLogic9& initial_internal,
    const std::size_t expected_activation_events,
    const JitOptimizationLevel optimization, const std::string_view label)
{
    auto kernel = make_certified_frontier_kernel(width,
        SignalUpdateDomain::systemverilog_active, ValueKind::logic9);
    set_internal_sensitivity_ranges(kernel, ranges);
    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    const auto identity = std::string { "frontier-internal-range-logic9-" }
        + std::string { label } + "-"
        + std::to_string(static_cast<std::uint8_t>(optimization));
    const auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, options, identity);
    require(executor != nullptr && executor->step_entry() != nullptr,
        "the range-sensitive Logic9 kernel materializes at O0 and O2");
    run_region_frontier_sensitivity_fanout_witness(kernel,
        executor->step_entry(), executor->layout(), 92U,
        range_staging_values(external, initial_internal),
        expected_activation_events);
}

void check_internal_sensitivity_fanout_cases(
    const JitOptimizationLevel optimization)
{
    constexpr std::uint32_t width = 129U;
    constexpr std::uint32_t cross_word_offset = 63U;
    constexpr std::uint32_t cross_word_width = 3U;
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> cross_word {
        { cross_word_offset, cross_word_width },
    };

    const PackedLogic4 initial_zero(width, Logic4::zero);
    PackedLogic4 changed_outside(width, Logic4::x);
    for (std::uint32_t bit = cross_word_offset;
         bit < cross_word_offset + cross_word_width; ++bit) {
        changed_outside.set(bit, Logic4::zero);
    }
    run_logic4_sensitivity_case(width, cross_word, changed_outside,
        initial_zero, 0U, optimization,
        "cross-word-unrelated-bits");

    PackedLogic4 changed_selected(width, Logic4::zero);
    PackedLogic4 unchanged(width, Logic4::zero);
    for (std::uint32_t bit = cross_word_offset;
         bit < cross_word_offset + cross_word_width; ++bit) {
        changed_selected.set(bit, Logic4::x);
    }
    run_logic4_sensitivity_case(width, cross_word, changed_selected,
        unchanged, 1U, optimization, "cross-word-selected-x");

    PackedLogic4 changed_selected_z(width, Logic4::zero);
    for (std::uint32_t bit = cross_word_offset;
         bit < cross_word_offset + cross_word_width; ++bit) {
        changed_selected_z.set(bit, Logic4::z);
    }
    run_logic4_sensitivity_case(width, cross_word, changed_selected_z,
        unchanged, 1U, optimization, "cross-word-selected-z");

    const std::vector<std::pair<std::uint32_t, std::uint32_t>> multiple {
        { 1U, 1U }, { cross_word_offset, cross_word_width },
    };
    PackedLogic4 both_ranges_change(width, Logic4::zero);
    both_ranges_change.set(1U, Logic4::x);
    for (std::uint32_t bit = cross_word_offset;
         bit < cross_word_offset + cross_word_width; ++bit) {
        both_ranges_change.set(bit, Logic4::z);
    }
    run_logic4_sensitivity_case(width, multiple, both_ranges_change,
        unchanged, 1U, optimization, "multiple-ranges-deduplicated");

    const std::vector<std::pair<std::uint32_t, std::uint32_t>> exact_word {
        { 64U, 64U },
    };
    PackedLogic4 word_change(width, Logic4::zero);
    for (std::uint32_t bit = 64U; bit < 128U; ++bit) {
        word_change.set(bit, Logic4::x);
    }
    run_logic4_sensitivity_case(width, exact_word, word_change, unchanged,
        1U, optimization, "exact-64-bit-word");

    const std::vector<std::pair<std::uint32_t, std::uint32_t>> logic9_range {
        { cross_word_offset, cross_word_width },
    };
    PackedLogic9 logic9_initial(width, Logic9::u);
    PackedLogic9 logic9_unrelated(width, Logic9::u);
    logic9_unrelated.set(3U, Logic9::dont_care);
    run_logic9_sensitivity_case(width, logic9_range, logic9_unrelated,
        logic9_initial, 0U, optimization, "logic9-unrelated-plane");
    constexpr std::array logic9_plane_states {
        std::pair { std::string_view { "plane-0" }, Logic9::x },
        std::pair { std::string_view { "plane-1" }, Logic9::zero },
        std::pair { std::string_view { "plane-2" }, Logic9::z },
        std::pair { std::string_view { "plane-3" }, Logic9::dont_care },
    };
    for (const auto& [label, state] : logic9_plane_states) {
        PackedLogic9 logic9_selected(width, Logic9::u);
        logic9_selected.set(64U, state);
        run_logic9_sensitivity_case(width, logic9_range, logic9_selected,
            logic9_initial, 1U, optimization, label);
    }
}

void run_input_transition_cases(const std::uint32_t width,
    const JitOptimizationLevel optimization)
{
    const auto kernel = make_certified_unary_not_frontier_kernel(width);
    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    const auto identity = std::string { "frontier-known-logic4-" }
        + std::to_string(width) + "-"
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, options, identity);
    require(executor != nullptr && executor->step_entry() != nullptr,
        "the known-Logic4 activation kernel materializes one generated entry");

    const auto run_value = [&](const PackedLogic4& value) {
        auto staging = staging_values(value);
        run_region_frontier_staging_tests(kernel, executor->step_entry(),
            executor->layout(), 71U, std::move(staging));
    };

    const auto known = alternating_known_value(width);
    const PackedLogic4 unknown_x(width, Logic4::x);
    const PackedLogic4 unknown_z(width, Logic4::z);
    run_value(known);
    run_value(unknown_x);
    run_value(unknown_z);
    run_value(known);
}

void run_unknown_constant_case(const JitOptimizationLevel optimization)
{
    constexpr std::uint32_t width = 129U;
    const auto kernel = make_unknown_constant_frontier_kernel(width);
    const auto full_width_kernel
        = make_unknown_constant_frontier_kernel(width, true);
    auto whole_plan = RegionFrontierKernelPlan::try_create(kernel);
    auto full_width_plan
        = RegionFrontierKernelPlan::try_create(full_width_kernel);
    require(whole_plan.has_value() && full_width_plan.has_value(),
        "canonical and exact-full-width internal sensitivities both form V2 plans");
    require(whole_plan->cache_identity() != full_width_plan->cache_identity(),
        "the explicit internal range keeps a cache identity distinct from canonical whole");
    const auto& whole_layout = whole_plan->layout();
    const auto& full_width_layout = full_width_plan->layout();
    bool same_fanout = whole_layout.fanout_edge_count
        == full_width_layout.fanout_edge_count
        && (whole_layout.fanout_edge_count == 0U
            || (whole_layout.fanout_edges != nullptr
                && full_width_layout.fanout_edges != nullptr));
    for (std::size_t index = 0U;
         same_fanout && index < whole_layout.fanout_edge_count;
         ++index) {
        const auto& whole_edge = whole_layout.fanout_edges[index];
        const auto& full_width_edge = full_width_layout.fanout_edges[index];
        same_fanout = whole_edge.signal_slot == full_width_edge.signal_slot
            && whole_edge.member_index == full_width_edge.member_index
            && whole_edge.trigger_mask == full_width_edge.trigger_mask;
    }
    require(same_fanout,
        "canonical and exact-full-width plans encode the same full-signal successor fanout");
    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    const auto identity = std::string { "frontier-unknown-constant-" }
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = LlvmRegionFrontierExecutor::try_create(
        kernel, options, identity + "-whole");
    auto full_width_executor = LlvmRegionFrontierExecutor::try_create(
        full_width_kernel, options, identity + "-full-width");
    require(executor != nullptr && executor->step_entry() != nullptr
            && full_width_executor != nullptr
            && full_width_executor->step_entry() != nullptr,
        "canonical and exact-full-width plans materialize four-state V2 bodies");

    PackedLogic4 external(width, Logic4::zero);
    PackedLogic4 unknown(width, Logic4::x);
    RegionFrontierStagingValues values;
    values.external_input = test_value(external);
    values.initial_internal = {
        std::vector<std::uint64_t>(unknown.aval_words().size(), 0U),
        std::vector<std::uint64_t>(unknown.bval_words().size(), 0U),
    };
    values.expected_internal = test_value(unknown);
    values.initial_boundary = values.initial_internal;
    values.expected_boundary = values.expected_internal;
    run_region_frontier_staging_tests(kernel, executor->step_entry(),
        executor->layout(), 71U, values);
    run_region_frontier_staging_tests(full_width_kernel,
        full_width_executor->step_entry(), full_width_executor->layout(),
        71U, std::move(values));
}

struct BlockShape final {
    std::size_t binary_operations { };
    std::size_t input_bval_loads { };
    std::size_t input_bval_uses { };
};

[[nodiscard]] BlockShape inspect_block(const llvm::BasicBlock& block,
    const llvm::Function& function)
{
    BlockShape result;
    std::unordered_set<const llvm::Value*> bval_derived_values;
    std::vector<const llvm::Value*> bval_worklist;
    for (const auto& source_block : function) {
        for (const auto& instruction : source_block) {
            const auto* const load
                = llvm::dyn_cast<llvm::LoadInst>(&instruction);
            if (load != nullptr && load->getName() == "input.word.bval") {
                bval_derived_values.insert(load);
                bval_worklist.push_back(load);
            }
        }
    }
    while (!bval_worklist.empty()) {
        const auto* const value = bval_worklist.back();
        bval_worklist.pop_back();
        for (const auto* user : value->users()) {
            const auto* const user_instruction
                = llvm::dyn_cast<llvm::Instruction>(user);
            if (user_instruction != nullptr
                && bval_derived_values.insert(user_instruction).second) {
                bval_worklist.push_back(user_instruction);
            }
        }
    }

    for (const auto& instruction : block) {
        result.binary_operations +=
            llvm::isa<llvm::BinaryOperator>(instruction) ? 1U : 0U;
        const auto* const load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
        if (load != nullptr && load->getName() == "input.word.bval") {
            ++result.input_bval_loads;
        }
        for (unsigned int index = 0U;
             index < instruction.getNumOperands();
             ++index) {
            result.input_bval_uses += bval_derived_values.contains(
                instruction.getOperand(index)) ? 1U : 0U;
        }
    }
    return result;
}

void check_known_body_ir_shape()
{
    const auto kernel = make_certified_unary_not_frontier_kernel(129U);
    auto plan = RegionFrontierKernelPlan::try_create(kernel);
    require(plan.has_value(),
        "the certified wide unary kernel has a frontier body plan");

    llvm::LLVMContext context;
    llvm::Module module("region-frontier-known-logic4", context);
    const auto commit_emitter
        = runtime::simir::scratch::make_region_frontier_internal_commit_emitter_v2(
            plan->layout(), plan->fanout_range_spans(),
            plan->fanout_sensitivity_ranges());
    auto* const function = plan->emit_step(module,
        "region_frontier_known_logic4_shape", commit_emitter);
    require(function != nullptr
            && llvm::verifyModule(module, &llvm::errs()) == false,
        "the raw known and four-state member-body IR verifies");

    constexpr std::string_view stage_write_helper_prefix
        = "fsim.frontier.stage.write.v1.";
    std::vector<const llvm::Function*> stage_write_helpers;
    for (const auto& candidate : module) {
        const auto name = candidate.getName().str();
        if (name.rfind(stage_write_helper_prefix, 0U) == 0U) {
            stage_write_helpers.push_back(&candidate);
        }
    }
    require(stage_write_helpers.size() == 2U
            && std::ranges::all_of(stage_write_helpers,
                [](const llvm::Function* helper) {
                    return helper->getLinkage()
                            == llvm::GlobalValue::InternalLinkage
                        && helper->hasFnAttribute(llvm::Attribute::NoInline)
                        && helper->hasFnAttribute(llvm::Attribute::NoUnwind);
                }),
        "same-shape known and fallback writes share private event-specific helpers");
    std::vector<std::size_t> helper_call_counts(stage_write_helpers.size());
    for (const auto& block : *function) {
        for (const auto& instruction : block) {
            const auto* const call
                = llvm::dyn_cast<llvm::CallInst>(&instruction);
            if (call == nullptr) {
                continue;
            }
            const auto helper = std::ranges::find(stage_write_helpers,
                call->getCalledFunction());
            if (helper != stage_write_helpers.end()) {
                const auto index = static_cast<std::size_t>(
                    std::distance(stage_write_helpers.begin(), helper));
                ++helper_call_counts[index];
            }
        }
    }
    require(std::ranges::all_of(helper_call_counts,
                [](const std::size_t count) { return count == 2U; }),
        "each event-specific helper serves the matching known/fallback write sites");

    const llvm::BasicBlock* known_block { };
    const llvm::BasicBlock* fallback_block { };
    bool found_knownness_branch { };
    for (const auto& block : *function) {
        const auto name = block.getName();
        if (name == "member.known.logic4.0") {
            known_block = &block;
        } else if (name == "member.four_state.0") {
            fallback_block = &block;
        }
        for (const auto& instruction : block) {
            const auto* const branch
                = llvm::dyn_cast<llvm::BranchInst>(&instruction);
            found_knownness_branch = found_knownness_branch
                || (branch != nullptr && branch->isConditional()
                    && branch->getCondition()->hasName()
                    && branch->getCondition()->getName()
                        == "member.input.known");
        }
    }
    require(known_block != nullptr && fallback_block != nullptr
            && found_knownness_branch,
        "a per-activation bval guard selects distinct known and four-state bodies");

    const auto known_shape = inspect_block(*known_block, *function);
    const auto fallback_shape = inspect_block(*fallback_block, *function);
    require(known_shape.input_bval_loads == 0U
            && known_shape.input_bval_uses == 0U,
        "the known member body has no input bval load or bval-dependent operation");
    require(fallback_shape.binary_operations
                > known_shape.binary_operations,
        "the known Logic4 body emits fewer Boolean operations than its four-state fallback");
}

void check_stage_write_helper_domain_and_size_bound()
{
    constexpr auto helper_prefix = "fsim.frontier.stage.write.v1.";
    auto generic_kernel = make_certified_unary_not_frontier_kernel(65U,
        ValueKind::logic4, SignalUpdateDomain::generic);
    auto generic_plan = RegionFrontierKernelPlan::try_create(generic_kernel);
    require(generic_plan.has_value(),
        "the generic update fixture forms a frontier plan");
    llvm::LLVMContext generic_context;
    llvm::Module generic_module("region-frontier-generic-stage-write",
        generic_context);
    const auto generic_commit_emitter
        = runtime::simir::scratch::make_region_frontier_internal_commit_emitter_v2(
            generic_plan->layout(), generic_plan->fanout_range_spans(),
            generic_plan->fanout_sensitivity_ranges());
    auto* const generic_function = generic_plan->emit_step(generic_module,
        "region_frontier_generic_stage_write", generic_commit_emitter);
    require(generic_function != nullptr
            && llvm::verifyModule(generic_module, &llvm::errs()) == false,
        "the generic stage-write helper module verifies");
    std::size_t generic_helpers { };
    std::size_t systemverilog_helpers_in_generic { };
    const llvm::Function* generic_helper { };
    for (const auto& candidate : generic_module) {
        const auto name = candidate.getName().str();
        if (name.rfind(helper_prefix, 0U) != 0U) {
            continue;
        }
        generic_helpers += name.find(".generic.") != std::string::npos;
        systemverilog_helpers_in_generic +=
            name.find(".systemverilog.") != std::string::npos;
        if (name.find(".generic.") != std::string::npos) {
            generic_helper = &candidate;
        }
    }
    require(generic_helpers == 1U
            && systemverilog_helpers_in_generic == 0U,
        "generic execution has a distinct helper key from SystemVerilog Active");
    std::size_t generic_helper_call_count { };
    for (const auto& block : *generic_function) {
        for (const auto& instruction : block) {
            const auto* const call
                = llvm::dyn_cast<llvm::CallInst>(&instruction);
            generic_helper_call_count += call != nullptr
                && call->getCalledFunction() == generic_helper;
        }
    }
    require(generic_helper != nullptr && generic_helper_call_count == 4U,
        "the generic helper receives all internal and boundary writes at their original sites");

    constexpr std::uint32_t exact_helper_limit_width = 512U;
    auto exact_limit_kernel = make_certified_unary_not_frontier_kernel(
        exact_helper_limit_width);
    auto exact_limit_plan
        = RegionFrontierKernelPlan::try_create(exact_limit_kernel);
    require(exact_limit_plan.has_value(),
        "the exact helper payload limit remains a valid plan");
    llvm::LLVMContext exact_limit_context;
    llvm::Module exact_limit_module("region-frontier-stage-write-limit",
        exact_limit_context);
    const auto exact_limit_commit_emitter
        = runtime::simir::scratch::make_region_frontier_internal_commit_emitter_v2(
            exact_limit_plan->layout(),
            exact_limit_plan->fanout_range_spans(),
            exact_limit_plan->fanout_sensitivity_ranges());
    auto* const exact_limit_function = exact_limit_plan->emit_step(
        exact_limit_module, "region_frontier_stage_write_limit",
        exact_limit_commit_emitter);
    const auto exact_limit_helper_count = std::ranges::count_if(
        exact_limit_module, [](const llvm::Function& candidate) {
            return candidate.getName().str().rfind(
                "fsim.frontier.stage.write.v1.", 0U) == 0U;
        });
    require(exact_limit_function != nullptr && exact_limit_helper_count == 2
            && llvm::verifyModule(exact_limit_module, &llvm::errs()) == false,
        "the exact 16-word helper payload stays on the shared helper route");

    constexpr std::uint32_t just_over_helper_limit_width = 513U;
    auto large_kernel = make_certified_unary_not_frontier_kernel(
        just_over_helper_limit_width);
    auto large_plan = RegionFrontierKernelPlan::try_create(large_kernel);
    require(large_plan.has_value(),
        "the payload above the helper argument bound remains a valid plan");
    llvm::LLVMContext large_context;
    llvm::Module large_module("region-frontier-large-stage-write",
        large_context);
    const auto large_commit_emitter
        = runtime::simir::scratch::make_region_frontier_internal_commit_emitter_v2(
            large_plan->layout(), large_plan->fanout_range_spans(),
            large_plan->fanout_sensitivity_ranges());
    auto* const large_function = large_plan->emit_step(large_module,
        "region_frontier_large_stage_write", large_commit_emitter);
    require(large_function != nullptr
            && llvm::verifyModule(large_module, &llvm::errs()) == false,
        "the large inline stage-write module verifies");
    const auto has_stage_write_helper = std::ranges::any_of(large_module,
        [](const llvm::Function& candidate) {
            return candidate.getName().str().rfind(
                "fsim.frontier.stage.write.v1.", 0U) == 0U;
        });
    require(!has_stage_write_helper,
        "payloads exceeding the generic helper bound keep the inline lowering");
}

void check_unknown_constant_ir_shape()
{
    const auto kernel = make_unknown_constant_frontier_kernel(129U);
    const auto producer = std::ranges::find(kernel.members, 7U,
        &runtime::simir::RegionConeKernelMember::process);
    require(producer != kernel.members.end(),
        "the graph-built unknown-constant producer is a local member");
    const auto producer_index = static_cast<std::size_t>(
        std::distance(kernel.members.begin(), producer));
    auto plan = RegionFrontierKernelPlan::try_create(kernel);
    require(plan.has_value(),
        "the certified unknown-constant body has a frontier plan");

    llvm::LLVMContext context;
    llvm::Module module("region-frontier-unknown-constant", context);
    const auto commit_emitter
        = runtime::simir::scratch::make_region_frontier_internal_commit_emitter_v2(
            plan->layout(), plan->fanout_range_spans(),
            plan->fanout_sensitivity_ranges());
    auto* const function = plan->emit_step(module,
        "region_frontier_unknown_constant_shape", commit_emitter);
    require(function != nullptr
            && llvm::verifyModule(module, &llvm::errs()) == false,
        "the unknown-constant four-state member IR verifies");

    const auto known_producer_name = "member.known.logic4."
        + std::to_string(producer_index);
    const auto has_known_producer = std::ranges::any_of(*function,
        [&](const llvm::BasicBlock& block) {
            return block.getName() == known_producer_name;
        });
    require(!has_known_producer,
        "an unknown Logic4 constant cannot enter the known-Logic4 member body");
}

void check_sensitivity_range_plan_contract()
{
    constexpr std::uint32_t bus_width = 73U;
    constexpr std::uint32_t range_offset = 61U;
    constexpr std::uint32_t range_width = 9U;
    auto kernel = make_unknown_constant_frontier_kernel(bus_width);
    auto whole_plan = RegionFrontierKernelPlan::try_create(kernel);
    require(whole_plan.has_value(),
        "the whole-signal internal dependency remains admitted as a control");
    const std::string whole_identity { whole_plan->cache_identity() };

    const auto external_input = std::ranges::find_if(kernel.inputs,
        [](const auto& input) {
            return !input.internal && input.value_kind == ValueKind::logic4;
        });
    require(external_input != kernel.inputs.end()
            && external_input->width == bus_width
            && std::ranges::none_of(kernel.outputs,
                [&](const auto& output) {
                    return output.signal == external_input->signal;
                }),
        "the range candidate is a testbench boundary input, not a kernel output");
    const auto external_signal = external_input->signal;
    const auto root = std::ranges::find_if(kernel.members,
        [external_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [external_signal](const auto& sensitivity) {
                    return sensitivity.signal == external_signal;
                });
        });
    require(root != kernel.members.end(),
        "the boundary input owns a selected producer member");
    const auto root_sensitivity = std::ranges::find_if(root->sensitivities,
        [external_signal](const auto& sensitivity) {
            return sensitivity.signal == external_signal;
        });
    require(root_sensitivity != root->sensitivities.end(),
        "the producer retains its source boundary sensitivity");

    const auto make_boundary_output_kernel = [&](const SignalUpdateDomain domain,
                                                const std::uint32_t offset,
                                                const std::uint32_t width) {
        auto candidate = make_certified_frontier_kernel(bus_width, domain);
        const auto candidate_input = std::ranges::find_if(
            candidate.inputs, [](const auto& input) {
                return !input.internal
                    && input.value_kind == ValueKind::logic4;
            });
        require(candidate_input != candidate.inputs.end()
                && candidate_input->width == bus_width,
            "the boundary read/write shape retains its full-width input");
        const auto candidate_signal = candidate_input->signal;
        const auto candidate_output = std::ranges::find_if(
            candidate.outputs, [&](const auto& output) {
                return std::ranges::find(candidate.internal_signals,
                           output.signal)
                    == candidate.internal_signals.end();
            });
        require(candidate_output != candidate.outputs.end()
                && candidate_output->width == bus_width
                && candidate_output->offset == 0U,
            "the boundary read/write shape has one whole boundary output");
        candidate_output->signal = candidate_signal;
        const auto candidate_reader = std::ranges::find_if(
            candidate.members, [candidate_signal](const auto& member) {
                return std::ranges::any_of(member.sensitivities,
                    [candidate_signal](const auto& sensitivity) {
                        return sensitivity.signal == candidate_signal;
                    });
            });
        require(candidate_reader != candidate.members.end(),
            "the boundary read/write shape retains its selected reader");
        const auto candidate_sensitivity = std::ranges::find_if(
            candidate_reader->sensitivities,
            [candidate_signal](const auto& sensitivity) {
                return sensitivity.signal == candidate_signal;
            });
        require(candidate_sensitivity != candidate_reader->sensitivities.end(),
            "the boundary read/write shape retains its source sensitivity");
        candidate_sensitivity->offset = offset;
        candidate_sensitivity->width = width;
        return std::pair { std::move(candidate), candidate_signal };
    };

    auto [whole_boundary_output, boundary_output_signal]
        = make_boundary_output_kernel(
            SignalUpdateDomain::systemverilog_active, 0U, 0U);
    auto whole_boundary_output_plan
        = RegionFrontierKernelPlan::try_create(whole_boundary_output);
    require(whole_boundary_output_plan.has_value(),
        "the whole-signal boundary read/write shape remains admitted");
    const std::string whole_boundary_output_identity {
        whole_boundary_output_plan->cache_identity() };

    auto [exact_boundary_output, exact_boundary_signal]
        = make_boundary_output_kernel(
            SignalUpdateDomain::systemverilog_active, 0U, bus_width);
    require(exact_boundary_signal == boundary_output_signal,
        "whole and explicit boundary plans address the same physical signal");
    const auto exact_boundary_reader = std::ranges::find_if(
        exact_boundary_output.members,
        [exact_boundary_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [exact_boundary_signal](const auto& sensitivity) {
                    return sensitivity.signal == exact_boundary_signal;
                });
        });
    require(exact_boundary_reader != exact_boundary_output.members.end(),
        "the explicit boundary tuple retains its selected reader");
    const auto exact_boundary_sensitivity = std::ranges::find_if(
        exact_boundary_reader->sensitivities,
        [exact_boundary_signal](const auto& sensitivity) {
            return sensitivity.signal == exact_boundary_signal;
        });
    require(exact_boundary_sensitivity
                != exact_boundary_reader->sensitivities.end()
            && exact_boundary_sensitivity->offset == 0U
            && exact_boundary_sensitivity->width == bus_width,
        "the explicit boundary tuple remains offset zero and full width");
    auto exact_boundary_output_plan
        = RegionFrontierKernelPlan::try_create(exact_boundary_output);
    require(exact_boundary_output_plan.has_value()
            && exact_boundary_output_plan->cache_identity()
                != whole_boundary_output_identity,
        "an exact full-width boundary read/write sensitivity is admitted with a distinct identity");

    const auto require_boundary_output_rejected
        = [&](const std::uint32_t offset, const std::uint32_t width,
              const std::string_view reason) {
              auto candidate = make_boundary_output_kernel(
                  SignalUpdateDomain::systemverilog_active, offset, width);
              RegionFrontierRejectedSensitivity rejection;
              require(!RegionFrontierKernelPlan::try_create(candidate.first,
                          nullptr, &rejection).has_value(), reason);
              require(rejection.present
                      && rejection.signal == candidate.second
                      && rejection.edge
                          == static_cast<std::uint32_t>(EdgeKind::any)
                      && rejection.offset == offset
                      && rejection.width == width
                      && rejection.matching_input_present
                      && rejection.matching_input_width == bus_width
                      && !rejection.matching_input_internal,
                  "the boundary-output decline identifies the exact input range");
          };
    require_boundary_output_rejected(1U, bus_width - 1U,
        "a nonzero-offset boundary subrange remains rejected when the signal is written");
    require_boundary_output_rejected(0U, bus_width - 1U,
        "a gapped boundary subrange remains rejected when the signal is written");
    require_boundary_output_rejected(0U, bus_width + 1U,
        "an oversized boundary sensitivity remains rejected when the signal is written");

    auto [generic_boundary_output, generic_boundary_signal]
        = make_boundary_output_kernel(
            SignalUpdateDomain::generic, 0U, bus_width);
    RegionFrontierRejectedSensitivity generic_rejection;
    require(!RegionFrontierKernelPlan::try_create(generic_boundary_output,
                nullptr, &generic_rejection).has_value(),
        "the exact full-width boundary-output exception is not admitted for Generic updates");
    require(generic_rejection.present
            && generic_rejection.signal == generic_boundary_signal
            && generic_rejection.offset == 0U
            && generic_rejection.width == bus_width
            && generic_rejection.matching_input_present
            && !generic_rejection.matching_input_internal,
        "the Generic decline remains attributed to the exact boundary sensitivity");

    root_sensitivity->offset = range_offset;
    root_sensitivity->width = range_width;
    auto ranged_plan = RegionFrontierKernelPlan::try_create(kernel);
    require(ranged_plan.has_value(),
        "an in-bounds partial Any sensitivity on an unwritten external input is admitted");
    require(ranged_plan->cache_identity() != whole_identity,
        "the admitted range shape must have a distinct cache identity from whole-signal sensitivity");

    auto range_also_written = kernel;
    auto overlapping_output = range_also_written.outputs.front();
    overlapping_output.signal = external_signal;
    range_also_written.outputs.push_back(overlapping_output);
    RegionFrontierRejectedSensitivity overlap_rejection;
    require(!RegionFrontierKernelPlan::try_create(range_also_written,
                nullptr, &overlap_rejection).has_value(),
        "a ranged input that is also written by the selected kernel is rejected");
    require(overlap_rejection.present
            && overlap_rejection.member_process == root->process
            && overlap_rejection.signal == external_signal
            && overlap_rejection.edge
                == static_cast<std::uint32_t>(EdgeKind::any)
            && overlap_rejection.offset == range_offset
            && overlap_rejection.width == range_width
            && overlap_rejection.matching_input_present
            && overlap_rejection.matching_input_width == bus_width
            && !overlap_rejection.matching_input_internal,
        "the output-overlap decline is attributed to the exact external ranged sensitivity");

    const auto internal_signal = kernel.internal_signals.front();
    auto partial_internal = kernel;
    const auto internal_reader = std::ranges::find_if(
        partial_internal.members, [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(internal_reader != partial_internal.members.end(),
        "the private internal edge has a certified whole-signal reader");
    const auto internal_sensitivity = std::ranges::find_if(
        internal_reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    require(internal_sensitivity != internal_reader->sensitivities.end()
            && internal_sensitivity->offset == 0U
            && internal_sensitivity->width == 0U,
        "the internal edge starts as the canonical whole-signal sensitivity");
    internal_sensitivity->offset = 63U;
    internal_sensitivity->width = 3U;
    const auto partial_internal_plan
        = RegionFrontierKernelPlan::try_create(partial_internal);
    require(partial_internal_plan.has_value(),
        "an in-bounds private internal range crossing a word boundary is admitted");
    require(partial_internal_plan->fanout_range_spans().size()
                == partial_internal_plan->layout().fanout_edge_count
            && partial_internal_plan->fanout_sensitivity_ranges().size() == 1U
            && partial_internal_plan->fanout_sensitivity_ranges().front().offset
                == 63U
            && partial_internal_plan->fanout_sensitivity_ranges().front().width
                == 3U,
        "the private edge maps to its exact flattened sensitivity range");

    auto generic_internal = make_certified_frontier_kernel(bus_width,
        SignalUpdateDomain::generic, ValueKind::logic4);
    const auto generic_whole_plan
        = RegionFrontierKernelPlan::try_create(generic_internal);
    require(generic_whole_plan.has_value(),
        "the Generic whole-signal private dependency remains admitted");
    auto generic_full_internal = generic_internal;
    const auto generic_full_reader = std::ranges::find_if(
        generic_full_internal.members, [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(generic_full_reader != generic_full_internal.members.end(),
        "the Generic exact-full control retains its internal reader");
    const auto generic_full_sensitivity = std::ranges::find_if(
        generic_full_reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    require(generic_full_sensitivity != generic_full_reader->sensitivities.end(),
        "the Generic exact-full control retains its source tuple");
    generic_full_sensitivity->offset = 0U;
    generic_full_sensitivity->width = bus_width;
    require(RegionFrontierKernelPlan::try_create(generic_full_internal).has_value(),
        "Generic continues to admit an explicit exact-full internal sensitivity");

    const auto generic_reader = std::ranges::find_if(
        generic_internal.members, [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(generic_reader != generic_internal.members.end(),
        "the Generic partial-range control retains its internal reader");
    const auto generic_sensitivity = std::ranges::find_if(
        generic_reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    require(generic_sensitivity != generic_reader->sensitivities.end(),
        "the Generic partial-range control retains its source tuple");
    generic_sensitivity->offset = 1U;
    generic_sensitivity->width = 1U;
    RegionFrontierRejectedSensitivity generic_partial_rejection;
    require(!RegionFrontierKernelPlan::try_create(generic_internal, nullptr,
                &generic_partial_rejection).has_value(),
        "Generic continues to reject private partial sensitivity ranges");
    require(generic_partial_rejection.present
            && generic_partial_rejection.signal == internal_signal
            && generic_partial_rejection.offset == 1U
            && generic_partial_rejection.width == 1U
            && generic_partial_rejection.matching_input_present
            && generic_partial_rejection.matching_input_width == bus_width
            && generic_partial_rejection.matching_input_internal,
        "the Generic decline reports its exact private-signal sensitivity");

    auto exact_full_width_internal = kernel;
    const auto exact_width_reader = std::ranges::find_if(
        exact_full_width_internal.members,
        [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(exact_width_reader != exact_full_width_internal.members.end(),
        "the explicit-range positive retains the certified internal reader");
    const auto exact_width_sensitivity = std::ranges::find_if(
        exact_width_reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    require(exact_width_sensitivity != exact_width_reader->sensitivities.end(),
        "the explicit-range positive retains its internal sensitivity");
    exact_width_sensitivity->offset = 0U;
    exact_width_sensitivity->width = bus_width;
    const auto exact_width_plan
        = RegionFrontierKernelPlan::try_create(exact_full_width_internal);
    require(exact_width_plan.has_value()
            && exact_width_plan->cache_identity() != whole_identity,
        "exact full-width internal coverage is admitted under its distinct explicit tuple");

    auto zero_width_internal = kernel;
    const auto zero_width_reader = std::ranges::find_if(
        zero_width_internal.members,
        [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(zero_width_reader != zero_width_internal.members.end(),
        "the malformed internal range retains its certified reader");
    const auto zero_width_sensitivity = std::ranges::find_if(
        zero_width_reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    require(zero_width_sensitivity != zero_width_reader->sensitivities.end(),
        "the malformed internal range retains its sensitivity");
    zero_width_sensitivity->offset = 1U;
    zero_width_sensitivity->width = 0U;
    require(!RegionFrontierKernelPlan::try_create(zero_width_internal).has_value(),
        "a noncanonical zero-width internal sensitivity remains rejected");

    auto out_of_bounds_internal = kernel;
    const auto out_of_bounds_reader = std::ranges::find_if(
        out_of_bounds_internal.members,
        [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(out_of_bounds_reader != out_of_bounds_internal.members.end(),
        "the out-of-bounds internal range retains its certified reader");
    const auto out_of_bounds_sensitivity = std::ranges::find_if(
        out_of_bounds_reader->sensitivities,
        [internal_signal](const auto& sensitivity) {
            return sensitivity.signal == internal_signal;
        });
    require(out_of_bounds_sensitivity
                != out_of_bounds_reader->sensitivities.end(),
        "the out-of-bounds internal range retains its sensitivity");
    out_of_bounds_sensitivity->offset = bus_width - 1U;
    out_of_bounds_sensitivity->width = 2U;
    require(!RegionFrontierKernelPlan::try_create(out_of_bounds_internal).has_value(),
        "an internal range extending beyond its input remains rejected");

    auto malformed_external = kernel;
    const auto malformed_root = std::ranges::find_if(
        malformed_external.members, [external_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [external_signal](const auto& sensitivity) {
                    return sensitivity.signal == external_signal;
                });
        });
    require(malformed_root != malformed_external.members.end(),
        "the copied candidate retains its boundary producer member");
    const auto malformed_sensitivity = std::ranges::find_if(
        malformed_root->sensitivities,
        [external_signal](const auto& sensitivity) {
            return sensitivity.signal == external_signal;
        });
    require(malformed_sensitivity != malformed_root->sensitivities.end(),
        "the copied candidate retains its external range sensitivity");
    malformed_sensitivity->offset = bus_width - 1U;
    malformed_sensitivity->width = 2U;
    require(!RegionFrontierKernelPlan::try_create(malformed_external).has_value(),
        "a partial external sensitivity extending beyond its input is rejected");

    auto zero_width_range = kernel;
    const auto noncanonical_root = std::ranges::find_if(
        zero_width_range.members, [external_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [external_signal](const auto& sensitivity) {
                    return sensitivity.signal == external_signal;
                });
        });
    require(noncanonical_root != zero_width_range.members.end(),
        "the noncanonical candidate retains its boundary producer member");
    const auto noncanonical_sensitivity = std::ranges::find_if(
        noncanonical_root->sensitivities,
        [external_signal](const auto& sensitivity) {
            return sensitivity.signal == external_signal;
        });
    require(noncanonical_sensitivity != noncanonical_root->sensitivities.end(),
        "the noncanonical candidate retains its external range sensitivity");
    noncanonical_sensitivity->offset = range_offset;
    noncanonical_sensitivity->width = 0U;
    require(!RegionFrontierKernelPlan::try_create(zero_width_range).has_value(),
        "a noncanonical zero-width sensitivity cannot masquerade as a range");
}

void check_partial_boundary_output_plan_contract()
{
    constexpr std::uint32_t value_width = 65U;
    constexpr std::uint32_t target_width = 129U;
    constexpr std::uint32_t target_offset = 32U;
    auto kernel = make_certified_frontier_kernel(value_width,
        SignalUpdateDomain::systemverilog_active);
    const auto boundary_output = std::ranges::find_if(kernel.outputs,
        [&](const auto& output) {
            return std::ranges::find(kernel.internal_signals, output.signal)
                == kernel.internal_signals.end();
        });
    require(boundary_output != kernel.outputs.end()
            && boundary_output->width == value_width
            && boundary_output->offset == 0U,
        "the positive starts from one full-width SV boundary output");
    const auto boundary_signal = boundary_output->signal;
    boundary_output->offset = target_offset;
    boundary_output->signal_width = target_width;

    const auto plan = RegionFrontierKernelPlan::try_create(kernel);
    require(plan.has_value(),
        "an in-bounds nonzero-offset SV boundary output forms a V2 plan");
    const auto& layout = plan->layout();
    const auto signal = std::ranges::find_if(
        std::span { layout.signals, layout.signal_slot_count },
        [boundary_signal](const RegionFrontierSignalLayoutV2& candidate) {
            return candidate.signal_id == boundary_signal;
        });
    require(signal != std::span { layout.signals, layout.signal_slot_count }.end()
            && signal->width == target_width
            && signal->word_count == (target_width + 63U) / 64U
            && (signal->flags
                    & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                != 0U,
        "the boundary plane keeps full target width independently of the RHS slice");
    const auto signal_slot = static_cast<std::uint32_t>(
        signal - std::span { layout.signals, layout.signal_slot_count }.begin());
    const auto write_site = std::ranges::find_if(
        std::span { layout.write_sites, layout.write_site_count },
        [signal_slot](const RegionFrontierWriteSiteV2& candidate) {
            return candidate.signal_slot == signal_slot;
        });
    require(write_site
            != std::span { layout.write_sites, layout.write_site_count }.end()
            && write_site->width == value_width
            && write_site->word_count == (value_width + 63U) / 64U
            && write_site->event_kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::boundary_commit),
        "the immutable boundary write site retains RHS slice width and commit kind");

    const auto offset_zero_boundary = [&] {
        auto whole = make_certified_frontier_kernel(value_width,
            SignalUpdateDomain::systemverilog_active);
        const auto output = std::ranges::find_if(whole.outputs,
            [&](const auto& candidate) {
                return std::ranges::find(whole.internal_signals,
                           candidate.signal)
                    == whole.internal_signals.end();
            });
        require(output != whole.outputs.end(),
            "the cache comparison retains its boundary output");
        output->signal_width = target_width;
        return whole;
    }();
    const auto offset_zero_plan
        = RegionFrontierKernelPlan::try_create(offset_zero_boundary);
    require(offset_zero_plan.has_value()
            && offset_zero_plan->cache_identity() != plan->cache_identity(),
        "offset-zero and nonzero-offset outputs have distinct plan identities");

    auto larger_target = kernel;
    const auto larger_target_output = std::ranges::find_if(
        larger_target.outputs, [&](const auto& output) {
            return output.signal == boundary_signal;
        });
    require(larger_target_output != larger_target.outputs.end(),
        "the target-width identity control retains its boundary output");
    larger_target_output->signal_width = target_width + 8U;
    const auto larger_target_plan
        = RegionFrontierKernelPlan::try_create(larger_target);
    require(larger_target_plan.has_value()
            && larger_target_plan->cache_identity() != plan->cache_identity(),
        "full target width participates in the plan identity independently of slice width");

    auto missing_target_width = kernel;
    const auto missing_width_output = std::ranges::find_if(
        missing_target_width.outputs, [&](const auto& output) {
            return output.signal == boundary_signal;
        });
    require(missing_width_output != missing_target_width.outputs.end(),
        "the missing-width negative retains its boundary output");
    missing_width_output->signal_width = 0U;
    require(!RegionFrontierKernelPlan::try_create(missing_target_width).has_value(),
        "a nonzero-offset boundary output requires an explicit full target width");

    auto out_of_bounds = kernel;
    const auto out_of_bounds_output = std::ranges::find_if(
        out_of_bounds.outputs, [&](const auto& output) {
            return output.signal == boundary_signal;
        });
    require(out_of_bounds_output != out_of_bounds.outputs.end(),
        "the bounds negative retains its boundary output");
    out_of_bounds_output->offset = target_width - value_width + 1U;
    require(!RegionFrontierKernelPlan::try_create(out_of_bounds).has_value(),
        "a boundary slice extending beyond the full target width is rejected");

    auto partial_internal = make_certified_frontier_kernel(value_width,
        SignalUpdateDomain::systemverilog_active);
    const auto internal_output = std::ranges::find_if(
        partial_internal.outputs, [&](const auto& output) {
            return std::ranges::find(partial_internal.internal_signals,
                       output.signal)
                != partial_internal.internal_signals.end();
        });
    require(internal_output != partial_internal.outputs.end(),
        "the negative retains its private single-owner output");
    internal_output->width = value_width / 2U;
    internal_output->signal_width = value_width;
    require(!RegionFrontierKernelPlan::try_create(partial_internal).has_value(),
        "a partial-width SV private internal output remains rejected");

    for (const auto optimization : {
             JitOptimizationLevel::o0,
             JitOptimizationLevel::o2,
         }) {
        LlvmJitOptions options;
        options.optimization = optimization;
        options.cache_directory.clear();
        options.debug_instrumentation = false;
        options.require_direct_update_slots = true;
        options.require_direct_read_signals = true;
        const auto identity = std::string { "frontier-partial-boundary-" }
            + std::to_string(static_cast<std::uint8_t>(optimization));
        const auto executor = LlvmRegionFrontierExecutor::try_create(
            kernel, options, identity);
        require(executor != nullptr && executor->step_entry() != nullptr,
            "the partial boundary kernel compiles at O0 and O2");
        const auto& compiled_layout = executor->layout();
        const auto compiled_signal = std::ranges::find_if(
            std::span { compiled_layout.signals,
                compiled_layout.signal_slot_count },
            [boundary_signal](const RegionFrontierSignalLayoutV2& candidate) {
                return candidate.signal_id == boundary_signal;
            });
        require(compiled_signal
                    != std::span { compiled_layout.signals,
                        compiled_layout.signal_slot_count }.end()
                && compiled_signal->width == target_width,
            "the generated O0/O2 layout allocates the full boundary plane");
    }
}

void check_structural_census_identity()
{
    constexpr std::uint32_t width = 73U;
    constexpr SignalId signal_rename = 100U;
    constexpr ProcessId process_rename = 1000U;
    auto kernel = make_unknown_constant_frontier_kernel(width);
    auto plan = RegionFrontierKernelPlan::try_create(kernel);
    require(plan.has_value(),
        "the structural census starts from a certified two-member kernel");
    const auto census = plan->structural_census_identity();
    require(census.has_value() && !census->empty(),
        "a fully represented plan exposes its separate census digest");
    const std::string baseline_census { *census };
    const std::string baseline_physical_identity { plan->cache_identity() };

    const auto repeated_plan = RegionFrontierKernelPlan::try_create(kernel);
    require(repeated_plan.has_value()
            && repeated_plan->structural_census_identity().has_value()
            && *repeated_plan->structural_census_identity()
                == baseline_census,
        "repeated structural census generation is deterministic");

    auto renamed = kernel;
    const auto rename_signal = [=](const SignalId signal) {
        return static_cast<SignalId>(signal + signal_rename);
    };
    const auto rename_process = [=](const ProcessId process) {
        return static_cast<ProcessId>(process + process_rename);
    };
    for (auto& input : renamed.inputs) {
        input.signal = rename_signal(input.signal);
    }
    for (auto& signal : renamed.internal_signals) {
        signal = rename_signal(signal);
    }
    for (auto& member : renamed.members) {
        member.process = rename_process(member.process);
        for (auto& sensitivity : member.sensitivities) {
            sensitivity.signal = rename_signal(sensitivity.signal);
        }
    }
    for (auto& output : renamed.outputs) {
        output.owner = rename_process(output.owner);
        output.signal = rename_signal(output.signal);
    }
    const auto renamed_plan = RegionFrontierKernelPlan::try_create(renamed);
    require(renamed_plan.has_value()
            && renamed_plan->cache_identity() != baseline_physical_identity
            && renamed_plan->structural_census_identity().has_value()
            && *renamed_plan->structural_census_identity() == baseline_census,
        "order-preserving physical ProcessId/SignalId renames match only the census identity");

    auto reordered_signal_rename = kernel;
    const auto reorder_signal = [](const SignalId signal) {
        if (signal == 0U) {
            return SignalId { 2U };
        }
        if (signal == 2U) {
            return SignalId { 0U };
        }
        return signal;
    };
    for (auto& input : reordered_signal_rename.inputs) {
        input.signal = reorder_signal(input.signal);
    }
    for (auto& signal : reordered_signal_rename.internal_signals) {
        signal = reorder_signal(signal);
    }
    for (auto& member : reordered_signal_rename.members) {
        for (auto& sensitivity : member.sensitivities) {
            sensitivity.signal = reorder_signal(sensitivity.signal);
        }
    }
    for (auto& output : reordered_signal_rename.outputs) {
        output.signal = reorder_signal(output.signal);
    }
    const auto reordered_signal_plan
        = RegionFrontierKernelPlan::try_create(reordered_signal_rename);
    require(reordered_signal_plan.has_value()
            && reordered_signal_plan->structural_census_identity().has_value()
            && *reordered_signal_plan->structural_census_identity()
                != baseline_census,
        "signal renames that reorder source input/slot tables may conservatively differ");

    auto changed_constant = kernel;
    bool replaced_constant { };
    for (std::size_t index = 0U;
         index < changed_constant.program.operations.size(); ++index) {
        auto operation = changed_constant.program.operations.expanded(index);
        const auto* constant = operation_get_if<LoadConstant>(&operation);
        if (constant == nullptr) {
            continue;
        }
        const auto replacement = LoadConstant {
            constant->destination, PackedLogic4(width, Logic4::one) };
        changed_constant.program.operations.replace(index,
            Operation { replacement });
        replaced_constant = true;
        break;
    }
    const auto changed_constant_plan
        = RegionFrontierKernelPlan::try_create(changed_constant);
    require(replaced_constant && changed_constant_plan.has_value()
            && changed_constant_plan->structural_census_identity().has_value()
            && *changed_constant_plan->structural_census_identity()
                != baseline_census,
        "a constant-value change remains visible in the structural census");

    auto changed_range = kernel;
    set_internal_sensitivity_ranges(changed_range, { { 3U, 17U } });
    const auto changed_range_plan
        = RegionFrontierKernelPlan::try_create(changed_range);
    require(changed_range_plan.has_value()
            && changed_range_plan->structural_census_identity().has_value()
            && *changed_range_plan->structural_census_identity()
                != baseline_census,
        "a sensitivity-range change remains visible in the structural census");

    auto changed_order = kernel;
    std::ranges::reverse(changed_order.members);
    const auto changed_order_plan
        = RegionFrontierKernelPlan::try_create(changed_order);
    require(changed_order_plan.has_value()
            && changed_order_plan->structural_census_identity().has_value()
            && *changed_order_plan->structural_census_identity()
                != baseline_census,
        "member order and its process-reference relation remain significant");

    auto changed_owner_alias = kernel;
    require(changed_owner_alias.outputs.size() == 2U,
        "the owner-alias control has two distinct writer/output pairs");
    const auto private_signal = changed_owner_alias.internal_signals.front();
    const auto boundary_input = std::ranges::find_if(
        changed_owner_alias.inputs, [](const auto& input) {
            return !input.internal;
        });
    require(boundary_input != changed_owner_alias.inputs.end(),
        "the owner-alias control retains a boundary input");
    const auto external_signal = boundary_input->signal;
    const auto private_reader = std::ranges::find_if(
        changed_owner_alias.members,
        [private_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [private_signal](const auto& sensitivity) {
                    return sensitivity.signal == private_signal;
                });
        });
    require(private_reader != changed_owner_alias.members.end(),
        "the owner-alias control retains its private reader");
    for (auto& sensitivity : private_reader->sensitivities) {
        if (sensitivity.signal == private_signal) {
            sensitivity.signal = external_signal;
        }
    }
    std::swap(changed_owner_alias.outputs[0U].signal,
        changed_owner_alias.outputs[1U].signal);
    const auto changed_owner_plan
        = RegionFrontierKernelPlan::try_create(changed_owner_alias);
    require(changed_owner_plan.has_value()
            && changed_owner_plan->structural_census_identity().has_value()
            && *changed_owner_plan->structural_census_identity()
                != baseline_census,
        "changing the process-to-output owner association changes the census");

    const auto systemverilog_kernel = make_certified_frontier_kernel(width,
        SignalUpdateDomain::systemverilog_active);
    const auto generic_kernel = make_certified_frontier_kernel(width,
        SignalUpdateDomain::generic);
    const auto systemverilog_plan
        = RegionFrontierKernelPlan::try_create(systemverilog_kernel);
    const auto generic_plan = RegionFrontierKernelPlan::try_create(generic_kernel);
    require(systemverilog_plan.has_value() && generic_plan.has_value()
            && systemverilog_plan->structural_census_identity().has_value()
            && generic_plan->structural_census_identity().has_value()
            && *systemverilog_plan->structural_census_identity()
                != *generic_plan->structural_census_identity(),
        "scheduling-domain changes remain visible in the structural census");

    auto changed_topology = kernel;
    const auto internal_signal = changed_topology.internal_signals.front();
    const auto external_input = std::ranges::find_if(
        changed_topology.inputs, [](const auto& input) {
            return !input.internal;
        });
    require(external_input != changed_topology.inputs.end(),
        "the topology control retains an external input");
    const auto alternate_signal = external_input->signal;
    const auto topology_reader = std::ranges::find_if(
        changed_topology.members, [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(topology_reader != changed_topology.members.end(),
        "the topology control retains an internal dependency");
    for (auto& sensitivity : topology_reader->sensitivities) {
        if (sensitivity.signal == internal_signal) {
            sensitivity.signal = alternate_signal;
        }
    }
    const auto changed_topology_plan
        = RegionFrontierKernelPlan::try_create(changed_topology);
    require(changed_topology_plan.has_value()
            && changed_topology_plan->structural_census_identity().has_value()
            && *changed_topology_plan->structural_census_identity()
                != baseline_census,
        "changing a member's sensitivity topology changes the census");
}

void check_internal_fanout_sidecar_contract()
{
    constexpr std::uint32_t width = 129U;
    auto kernel = make_unknown_constant_frontier_kernel(width);
    const auto internal_signal = kernel.internal_signals.front();
    const auto reader = std::ranges::find_if(kernel.members,
        [internal_signal](const auto& member) {
            return std::ranges::any_of(member.sensitivities,
                [internal_signal](const auto& sensitivity) {
                    return sensitivity.signal == internal_signal;
                });
        });
    require(reader != kernel.members.end(),
        "the sidecar fixture has a certified private reader");
    const auto sensitivity = std::ranges::find_if(reader->sensitivities,
        [internal_signal](const auto& candidate) {
            return candidate.signal == internal_signal;
        });
    require(sensitivity != reader->sensitivities.end(),
        "the sidecar fixture has its source sensitivity");
    sensitivity->offset = 63U;
    sensitivity->width = 3U;
    const auto plan = RegionFrontierKernelPlan::try_create(kernel);
    require(plan.has_value() && plan->layout().fanout_edge_count != 0U,
        "the malformed-sidecar checks use a real nonempty fanout layout");

    const auto reject_sidecars = [&](
        const std::vector<RegionFrontierFanoutRangeSpan>& spans,
        const std::vector<RegionFrontierFanoutSensitivityRange>& ranges,
        const std::string_view reason) {
        bool rejected { };
        try {
            (void)runtime::simir::scratch::
                make_region_frontier_internal_commit_emitter_v2(
                    plan->layout(), spans, ranges);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, reason);
    };

    const std::vector<RegionFrontierFanoutRangeSpan> valid_spans {
        plan->fanout_range_spans().begin(),
        plan->fanout_range_spans().end(),
    };
    const std::vector<RegionFrontierFanoutSensitivityRange> valid_ranges {
        plan->fanout_sensitivity_ranges().begin(),
        plan->fanout_sensitivity_ranges().end(),
    };
    require(valid_spans.size() == plan->layout().fanout_edge_count
            && valid_ranges.size() == 1U,
        "each private edge has one exact validated range");
    (void)runtime::simir::scratch::make_region_frontier_internal_commit_emitter_v2(
        plan->layout(), valid_spans, valid_ranges);

    reject_sidecars({}, {},
        "a nonempty fanout layout rejects omitted range sidecars");
    auto missing_edge = valid_spans;
    missing_edge.pop_back();
    reject_sidecars(missing_edge, valid_ranges,
        "the sidecar must describe every fanout edge");
    auto invalid_span = valid_spans;
    invalid_span.front().first_range = 1U;
    reject_sidecars(invalid_span, valid_ranges,
        "fanout spans must be contiguous from the first range");
    invalid_span = valid_spans;
    invalid_span.front().range_count = 0U;
    reject_sidecars(invalid_span, valid_ranges,
        "fanout spans reject empty sensitivity lists");
    invalid_span = valid_spans;
    invalid_span.front().range_count = 2U;
    reject_sidecars(invalid_span, valid_ranges,
        "fanout spans reject ranges beyond the flattened sidecar");
    auto invalid_range = valid_ranges;
    invalid_range.front().width = 0U;
    reject_sidecars(valid_spans, invalid_range,
        "a sensitivity range rejects zero width");
    invalid_range = valid_ranges;
    invalid_range.front().offset = width - 1U;
    invalid_range.front().width = 2U;
    reject_sidecars(valid_spans, invalid_range,
        "a sensitivity range rejects an end beyond the signal width");
    invalid_range = valid_ranges;
    invalid_range.push_back(valid_ranges.front());
    reject_sidecars(valid_spans, invalid_range,
        "the flattened sidecar rejects unreferenced trailing ranges");
}

} // namespace

void run_region_frontier_known_logic4_tests()
{
    constexpr std::array widths {
        1U, 65U, 129U, 256U, 512U, 513U, 1024U,
    };
    for (const auto width : widths) {
        for (const auto optimization : {
                 JitOptimizationLevel::o0,
                 JitOptimizationLevel::o2,
             }) {
            run_input_transition_cases(width, optimization);
        }
    }
    for (const auto optimization : {
             JitOptimizationLevel::o0,
             JitOptimizationLevel::o2,
         }) {
        run_unknown_constant_case(optimization);
    }
    check_known_body_ir_shape();
    check_stage_write_helper_domain_and_size_bound();
    check_unknown_constant_ir_shape();
    check_sensitivity_range_plan_contract();
    check_internal_fanout_sidecar_contract();
    check_partial_boundary_output_plan_contract();
    check_structural_census_identity();
    for (const auto optimization : {
             JitOptimizationLevel::o0,
             JitOptimizationLevel::o2,
         }) {
        check_internal_sensitivity_fanout_cases(optimization);
    }
}

} // namespace fsim::compiler::test

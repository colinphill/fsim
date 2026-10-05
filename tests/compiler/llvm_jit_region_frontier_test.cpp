// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit_region_frontier.hpp"
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "llvm/region_frontier_codegen_v2.hpp"
#include "llvm/region_frontier_kernel_plan.hpp"
#include "llvm_jit_region_frontier_adversarial_test.hpp"
#include "llvm_jit_region_frontier_cache_test.hpp"
#include "llvm_jit_region_frontier_concatenate_test.hpp"
#include "llvm_jit_region_frontier_capacity_test.hpp"
#include "llvm_jit_region_frontier_extract_test.hpp"
#include "llvm_jit_region_frontier_lifecycle_test.hpp"
#include "llvm_jit_region_frontier_logic9_words_test.hpp"
#include "llvm_jit_region_frontier_issue_helper_test.hpp"
#include "llvm_jit_region_frontier_logic9_issue_helper_test.hpp"
#include "llvm_jit_region_frontier_known_logic4_test.hpp"
#include "llvm_jit_region_frontier_preparation_test.hpp"
#include "llvm_jit_region_frontier_round_boundary_test.hpp"
#include "llvm_jit_region_frontier_shift_test.hpp"
#include "llvm_jit_region_frontier_shared_body_cache_test.hpp"
#include "llvm_jit_region_frontier_shared_identity_test.hpp"
#include "llvm_jit_region_frontier_staging_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"
#include "llvm_jit_region_frontier_test_access.hpp"
#include "llvm_jit_region_frontier_private_access.hpp"

#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::compiler::test;

constexpr SignalId source_signal_id = 0U;
constexpr SignalId internal_signal_id = 1U;
constexpr SignalId boundary_signal_id = 2U;
constexpr ProcessId producer_process_id = 7U;
constexpr ProcessId consumer_process_id = 23U;

[[nodiscard]] RegionFrontierValueKindV2 frontier_kind(
    const ValueKind kind)
{
    return kind == ValueKind::logic9
        ? RegionFrontierValueKindV2::logic9
        : RegionFrontierValueKindV2::logic4;
}

[[nodiscard]] std::uint32_t frontier_plane_count(
    const ValueKind kind)
{
    return kind == ValueKind::logic9
        ? kRegionFrontierLogic9PlaneCountV2
        : kRegionFrontierLogic4PlaneCountV2;
}

struct Logic4Code final {
    std::uint64_t aval { };
    std::uint64_t bval { };
};

constexpr std::array<Logic4Code, 4U> logic4_codes {{
    { 0U, 0U }, // 0
    { 1U, 0U }, // 1
    { 1U, 1U }, // X
    { 0U, 1U }, // Z
}};

constexpr std::array<std::array<std::uint8_t, 4U>, 4U> bit_and_codes {{
    {{ 0U, 0U, 0U, 0U }},
    {{ 0U, 1U, 2U, 2U }},
    {{ 0U, 2U, 2U, 2U }},
    {{ 0U, 2U, 2U, 2U }},
}};

constexpr std::array<std::array<std::uint8_t, 4U>, 4U> bit_or_codes {{
    {{ 0U, 1U, 2U, 2U }},
    {{ 1U, 1U, 1U, 1U }},
    {{ 2U, 1U, 2U, 2U }},
    {{ 2U, 1U, 2U, 2U }},
}};

constexpr std::array<std::array<std::uint8_t, 4U>, 4U> bit_xor_codes {{
    {{ 0U, 1U, 2U, 2U }},
    {{ 1U, 0U, 2U, 2U }},
    {{ 2U, 2U, 2U, 2U }},
    {{ 2U, 2U, 2U, 2U }},
}};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] std::size_t words_for(const std::uint32_t width) noexcept
{
    return (static_cast<std::size_t>(width) + 63U) / 64U;
}

[[nodiscard]] std::uint64_t word_mask(const std::uint32_t width,
    const std::size_t word) noexcept
{
    const auto tail = width % 64U;
    if (tail == 0U || word + 1U < words_for(width)) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << tail) - 1U;
}

void set_bit(std::vector<std::uint64_t>& words,
    const std::uint32_t bit, const bool value)
{
    const auto word = static_cast<std::size_t>(bit / 64U);
    const auto mask = UINT64_C(1) << (bit % 64U);
    if (value) {
        words.at(word) |= mask;
    } else {
        words.at(word) &= ~mask;
    }
}

[[nodiscard]] std::uint8_t logic4_code(
    const std::uint64_t aval, const std::uint64_t bval) noexcept
{
    if (bval == 0U) {
        return aval == 0U ? 0U : 1U;
    }
    return aval == 0U ? 3U : 2U;
}

void set_logic4_bit(RegionFrontierTestValue& value,
    const std::uint32_t bit, const std::uint8_t code)
{
    const auto word = static_cast<std::size_t>(bit / 64U);
    const auto mask = UINT64_C(1) << (bit % 64U);
    const auto& state = logic4_codes.at(code);
    if ((state.aval & 1U) != 0U) {
        value.aval.at(word) |= mask;
    } else {
        value.aval.at(word) &= ~mask;
    }
    if ((state.bval & 1U) != 0U) {
        value.bval.at(word) |= mask;
    } else {
        value.bval.at(word) &= ~mask;
    }
}

[[nodiscard]] RegionFrontierTestValue repeated_logic4(
    const std::uint32_t width, const std::uint8_t code)
{
    RegionFrontierTestValue result {
        std::vector<std::uint64_t>(words_for(width), 0U),
        std::vector<std::uint64_t>(words_for(width), 0U),
    };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        set_logic4_bit(result, bit, code);
    }
    return result;
}

[[nodiscard]] RegionFrontierTestValue repeated_logic9(
    const std::uint32_t width, const std::uint8_t code)
{
    RegionFrontierTestValue result;
    result.value_kind = ValueKind::logic9;
    const auto word_count = words_for(width);
    result.aval.assign(word_count, 0U);
    result.bval.assign(word_count, 0U);
    result.plane2.assign(word_count, 0U);
    result.plane3.assign(word_count, 0U);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        const auto word = static_cast<std::size_t>(bit / 64U);
        const auto mask = UINT64_C(1) << (bit % 64U);
        const std::array<std::vector<std::uint64_t>*, 4U> planes {
            &result.aval, &result.bval, &result.plane2, &result.plane3,
        };
        for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
            if ((code & (1U << plane)) != 0U) {
                planes[plane]->at(word) |= mask;
            }
        }
    }
    return result;
}

[[nodiscard]] RegionFrontierSignalInput logic4_input(
    const SignalId signal, const std::uint32_t width,
    const std::uint8_t code)
{
    return { signal, repeated_logic4(width, code) };
}

[[nodiscard]] std::uint8_t get_logic4_bit(
    const RegionFrontierTestValue& value, const std::uint32_t bit) noexcept
{
    const auto word = static_cast<std::size_t>(bit / 64U);
    const auto shift = bit % 64U;
    const auto aval = (value.aval[word] >> shift) & 1U;
    const auto bval = (value.bval[word] >> shift) & 1U;
    return logic4_code(aval, bval);
}

[[nodiscard]] RegionFrontierStagingValues operation_values(
    const std::uint32_t width,
    std::vector<RegionFrontierSignalInput> inputs,
    RegionFrontierTestValue expected)
{
    RegionFrontierStagingValues values;
    values.external_inputs = std::move(inputs);
    values.expected_internal = std::move(expected);
    values.expected_boundary = values.expected_internal;
    const auto plane_count = values.expected_internal.value_kind
            == ValueKind::logic9
        ? 4U : 2U;
    bool expected_is_zero = true;
    for (std::size_t plane = 0U; plane < plane_count; ++plane) {
        const auto& words = plane == 0U ? values.expected_internal.aval
            : plane == 1U ? values.expected_internal.bval
            : plane == 2U ? values.expected_internal.plane2
            : values.expected_internal.plane3;
        expected_is_zero = expected_is_zero
            && std::all_of(words.begin(), words.end(),
                [](const std::uint64_t word) { return word == 0U; });
    }
    const auto initial = values.expected_internal.value_kind == ValueKind::logic9
        ? repeated_logic9(width,
            static_cast<std::uint8_t>(expected_is_zero ? 1U : 0U))
        : repeated_logic4(width,
            static_cast<std::uint8_t>(expected_is_zero ? 1U : 0U));
    values.initial_internal = initial;
    values.initial_boundary = initial;
    return values;
}

void check_kernel_layout(
    const RegionConeActivationKernel& kernel,
    const std::uint32_t input_width,
    const RegionFrontierLayoutV2& layout)
{
    require(kernel.members.size() == 2U
            && kernel.members[0U].process == producer_process_id
            && kernel.members[1U].process == consumer_process_id,
        "fixture uses nonconsecutive ProcessIds and local member indices");
    require(kernel.inputs.size() == 2U && kernel.outputs.size() == 2U
            && kernel.internal_signals
                == std::vector<SignalId> { internal_signal_id },
        "fixture is one external source, one internal commit, and one boundary output");

    const auto producer_output = std::ranges::find_if(kernel.outputs,
        [](const RegionConeOutputBinding& output) {
            return output.owner == producer_process_id;
        });
    const auto consumer_output = std::ranges::find_if(kernel.outputs,
        [](const RegionConeOutputBinding& output) {
            return output.owner == consumer_process_id;
        });
    require(producer_output != kernel.outputs.end()
            && consumer_output != kernel.outputs.end(),
        "both original process owners retain their output bindings");
    const auto internal_width = producer_output->width;
    const auto internal_kind = producer_output->value_kind;
    const auto frontier_internal_kind = frontier_kind(internal_kind);
    const auto internal_plane_count = frontier_plane_count(internal_kind);
    require(producer_output->signal == internal_signal_id
            && producer_output->domain
                == SignalUpdateDomain::systemverilog_active
            && producer_output->update_kind
                == RegionUpdateKind::systemverilog_active,
        "the internal write remains an SV Active output");
    require(consumer_output->signal == boundary_signal_id
            && consumer_output->width == internal_width
            && consumer_output->domain
                == SignalUpdateDomain::systemverilog_active
            && consumer_output->update_kind
                == RegionUpdateKind::systemverilog_active,
        "the consumer write remains an SV Active boundary output");

    require(layout.abi_version == kRegionFrontierAbiVersionV2
            && layout.struct_size == sizeof(RegionFrontierLayoutV2)
            && region_frontier_layout_header_valid_v2(layout)
            && layout.member_count == kernel.members.size()
            && layout.signal_slot_count == 3U
            && layout.metadata_count == 1U
            && layout.write_site_count == kernel.outputs.size()
            && layout.pending_write_capacity >= layout.write_site_count
            && layout.members != nullptr && layout.signals != nullptr
            && layout.write_sites != nullptr,
        "the immutable JIT layout exactly covers the two-member fixture");

    bool found_external_input { };
    bool found_internal_commit { };
    bool found_boundary_output { };
    for (std::size_t index = 0U; index < layout.signal_slot_count; ++index) {
        const auto& plane = layout.signals[index];
        found_external_input = found_external_input
            || (plane.signal_id == source_signal_id
                && plane.value_kind == frontier_internal_kind
                && plane.plane_count == internal_plane_count
                && plane.width == input_width
                && plane.owner_process_id == UINT32_MAX
                && plane.metadata_index == UINT32_MAX
                && (plane.flags
                    & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                    != 0U);
        found_internal_commit = found_internal_commit
            || (plane.signal_id == internal_signal_id
                && plane.value_kind == frontier_internal_kind
                && plane.plane_count == internal_plane_count
                && plane.width == internal_width
                && plane.owner_process_id == producer_process_id
                && plane.metadata_index < layout.metadata_count
                && (plane.flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                    != 0U);
        found_boundary_output = found_boundary_output
            || (plane.signal_id == boundary_signal_id
                && plane.value_kind == frontier_internal_kind
                && plane.plane_count == internal_plane_count
                && plane.width == internal_width
                && plane.owner_process_id == UINT32_MAX
                && plane.metadata_index == UINT32_MAX
                && (plane.flags
                    & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                    != 0U);
    }
    require(found_external_input && found_internal_commit && found_boundary_output,
        "source, internal, and boundary signal roles remain distinct");

    std::array<bool, 2U> found_write_sites { false, false };
    for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
        const auto& site = layout.write_sites[index];
        require(site.signal_slot < layout.signal_slot_count
                && site.member_index < layout.member_count,
            "write descriptor indices are valid local indices");
        const auto& plane = layout.signals[site.signal_slot];
        const auto owner = layout.members[site.member_index].process_id;
        const auto candidate = std::ranges::find_if(kernel.outputs,
            [&](const RegionConeOutputBinding& output) {
                return output.owner == owner
                    && output.signal == plane.signal_id
                    && output.source_instruction == site.source_instruction
                    && output.width == site.width
                    && frontier_kind(output.value_kind) == site.value_kind;
            });
        require(candidate != kernel.outputs.end()
                && site.update_kind == static_cast<std::uint32_t>(
                    RegionUpdateKind::systemverilog_active)
                && site.word_count == words_for(site.width)
                && site.value_kind == plane.value_kind
                && site.plane_count == plane.plane_count,
            "every generated site retains original owner, source, and word geometry");
        if (owner == producer_process_id) {
            require(plane.signal_id == internal_signal_id,
                "the producer output targets its certified internal slot");
            found_write_sites[0U] = true;
        } else if (owner == consumer_process_id) {
            require(plane.signal_id == boundary_signal_id,
                "the consumer output targets the read-only boundary slot");
            found_write_sites[1U] = true;
        }
    }
    require(found_write_sites[0U] && found_write_sites[1U],
        "the layout includes both source write sites");

}

[[nodiscard]] std::unique_ptr<fsim::compiler::LlvmRegionFrontierExecutor>
make_executor(const RegionConeActivationKernel& kernel,
    const fsim::compiler::JitOptimizationLevel optimization,
    const std::string_view identity)
{
    fsim::compiler::LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    options.require_direct_update_slots = true;
    options.require_direct_read_signals = true;
    return fsim::compiler::LlvmRegionFrontierExecutor::try_create(
        kernel, options, identity);
}

void check_compiled_copy(const std::uint32_t width,
    const fsim::compiler::JitOptimizationLevel optimization)
{
    auto kernel = make_certified_frontier_kernel(width);
    const auto identity = std::string { "frontier-copy-" }
        + std::to_string(width) + "-"
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = make_executor(kernel, optimization, identity);
    require(executor != nullptr,
        "production lowering materializes the certified copy kernel");
    require(executor->step_entry() != nullptr,
        "the copy kernel's generated entry is callable");
    check_kernel_layout(kernel, width, executor->layout());
    run_region_frontier_staging_tests(kernel, executor->step_entry(),
        executor->layout(), 71U);
    if (width == 65U || width == 129U) {
        run_region_frontier_canonical_tail_guard_tests(kernel,
            executor->step_entry(), executor->layout(), 71U);
    }
    if (width == 65U) {
        run_region_frontier_write_site_shape_guard_tests(kernel,
            executor->step_entry(), executor->layout(), 71U);
    }
    if (width == 65U) {
        run_region_frontier_alias_prevalidated_entry_tests(kernel,
            executor->step_entry(),
            fsim::compiler::llvm_detail::RegionFrontierPrivateAccess::trusted_entry(
                *executor),
            executor->layout(), 71U);
    }
    if (width == 65U) {
        run_region_frontier_prefix_guard_tests(executor->step_entry());
    }
    if (width == 65U
        && optimization == fsim::compiler::JitOptimizationLevel::o2) {
        run_region_frontier_adversarial_tests(kernel,
            executor->step_entry(), executor->layout(), 71U);
        run_region_frontier_lifecycle_tests(kernel,
            executor->step_entry(), executor->layout(), 71U);
        run_region_frontier_round_boundary_tests(kernel,
            executor->step_entry(), executor->layout(), 71U);
    }
}

void check_generic_frontier_entry(
    const fsim::compiler::JitOptimizationLevel optimization)
{
    auto kernel = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic);
    const auto identity = std::string { "generic-deferred-frontier-" }
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = make_executor(kernel, optimization, identity);
    require(executor != nullptr,
        "the typed V2 executor accepts exact whole-signal Generic Logic4 writes");

    const auto& layout = executor->layout();
    require(layout.execution_mode
                == RegionFrontierExecutionModeV2::generic_deferred_update
            && layout.member_count == kernel.members.size()
            && layout.signal_slot_count != 0U
            && layout.signals != nullptr
            && layout.write_site_count == kernel.outputs.size()
            && layout.write_sites != nullptr
            && layout.metadata_count == 0U
            && layout.reserved_capacity == 0U
            && layout.max_commit_fanout_events == 0U,
        "the Generic plan exposes a deferred-update-only typed layout");

    std::vector<RegionFrontierPlaneV2> planes(layout.signal_slot_count);
    std::vector<std::vector<std::uint64_t>> boundary_aval(
        layout.signal_slot_count);
    std::vector<std::vector<std::uint64_t>> boundary_bval(
        layout.signal_slot_count);
    std::vector<const RegionFrontierPlaneV2*> port_planes(
        layout.signal_slot_count, nullptr);
    for (std::size_t slot = 0U; slot < layout.signal_slot_count; ++slot) {
        const auto& descriptor = layout.signals[slot];
        require(descriptor.flags
                == RegionFrontierPlaneFlagsV2::read_only_boundary_port
                && descriptor.value_kind == RegionFrontierValueKindV2::logic4
                && descriptor.plane_count == kRegionFrontierLogic4PlaneCountV2,
            "every Generic signal binding is read-only Logic4 boundary data");
        boundary_aval[slot].assign(descriptor.word_count, 0U);
        boundary_bval[slot].assign(descriptor.word_count, 0U);
        if (descriptor.signal_id == source_signal_id) {
            require(descriptor.width == 65U && descriptor.word_count == 2U,
                "the Generic source binding retains its two-word tail shape");
            boundary_aval[slot] = {
                UINT64_C(0x0123456789abcdef), UINT64_C(1),
            };
            boundary_bval[slot] = {
                UINT64_C(0x8000000000000002), UINT64_C(1),
            };
        }

        auto& plane = planes[slot];
        plane.signal_id = descriptor.signal_id;
        plane.owner_process_id = descriptor.owner_process_id;
        plane.value_kind = descriptor.value_kind;
        plane.width = descriptor.width;
        plane.word_count = descriptor.word_count;
        plane.plane_count = descriptor.plane_count;
        plane.flags = descriptor.flags;
        plane.metadata_index = descriptor.metadata_index;
        plane.boundary_planes[0U] = boundary_aval[slot].data();
        plane.boundary_planes[1U] = boundary_bval[slot].data();
        require(region_frontier_plane_bindings_valid_v2(plane),
            "the Generic frame binds only read-only boundary value planes");
        port_planes[slot] = &plane;
    }

    std::vector<std::uint64_t> ready_words(layout.readiness_word_count, 0U);
    std::vector<RegionFrontierMemberV2> members(layout.member_count);
    std::vector<RegionFrontierSchedulerTaskV2> tasks(1U);
    std::vector<RegionFrontierFanoutEdgeV2> fanout_edges(
        layout.fanout_edge_count);
    for (std::size_t edge = 0U; edge < fanout_edges.size(); ++edge) {
        fanout_edges[edge] = layout.fanout_edges[edge];
    }
    std::vector<RegionFrontierPendingWriteV2> pending_writes(
        layout.pending_write_capacity);
    std::vector<std::vector<std::uint64_t>> pending_aval(
        layout.pending_write_capacity);
    std::vector<std::vector<std::uint64_t>> pending_bval(
        layout.pending_write_capacity);
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        auto& pending = pending_writes[site.pending_slot];
        pending_aval[site.pending_slot].assign(site.word_count, 0U);
        pending_bval[site.pending_slot].assign(site.word_count, 0U);
        pending.member_index = site.member_index;
        pending.signal_slot = site.signal_slot;
        pending.source_instruction = site.source_instruction;
        pending.update_kind = site.update_kind;
        pending.value_kind = site.value_kind;
        pending.width = site.width;
        pending.word_count = site.word_count;
        pending.plane_count = site.plane_count;
        pending.value_planes[0U] = pending_aval[site.pending_slot].data();
        pending.value_planes[1U] = pending_bval[site.pending_slot].data();
    }
    std::vector<RegionFrontierStagedEventV2> staged_events(
        layout.staged_event_capacity);
    std::vector<RegionFrontierCommittedSignalV2> committed_signals(
        layout.committed_signal_capacity);
    std::uint64_t dispatch_count { };

    const auto producer = std::find_if(kernel.members.begin(),
        kernel.members.end(), [](const RegionConeKernelMember& member) {
            return member.process == producer_process_id;
        });
    require(producer != kernel.members.end(),
        "the generic fixture retains its certified source producer");
    const auto producer_index = static_cast<std::uint32_t>(
        producer - kernel.members.begin());
    const std::uint64_t stable_order = UINT64_C(29);
    const std::uint64_t sequence = UINT64_C(41);
    const RegionFrontierSlotV2 slot {
        UINT64_C(13), UINT64_C(7), UINT64_C(0),
        static_cast<std::uint32_t>(ProcessSchedulingDomain::generic),
        static_cast<std::uint32_t>(fsim::runtime::SchedulerPhase::active),
    };
    const RegionFrontierKeyV2 activation_key {
        slot.time, slot.delta, slot.systemverilog_round,
        stable_order, sequence, slot.process_domain, slot.phase,
    };
    auto& producer_member = members.at(producer_index);
    producer_member.process_id = layout.members[producer_index].process_id;
    producer_member.flags = RegionFrontierMemberFlagsV2::queued
        | RegionFrontierMemberFlagsV2::queued_key_valid;
    producer_member.queued_key = activation_key;
    producer_member.activation_origin = activation_key;
    ready_words.at(producer_index / 64U)
        |= UINT64_C(1) << (producer_index % 64U);
    for (std::size_t index = 0U; index < members.size(); ++index) {
        if (index != producer_index) {
            members[index].process_id = layout.members[index].process_id;
            members[index].flags
                = RegionFrontierMemberFlagsV2::waiting_on_static;
        }
    }
    tasks[0U] = {
        stable_order,
        sequence,
        encode_region_frontier_payload_v2(
            RegionFrontierEventKindV2::member_activation, producer_index),
    };

    RegionFrontierFrameV2 frame;
    frame.struct_size = sizeof(RegionFrontierFrameV2);
    frame.runtime_generation = UINT64_C(17);
    frame.bound_runtime_generation = frame.runtime_generation;
    frame.certificate_generation = layout.certificate_generation;
    frame.component_generation = layout.component_generation;
    frame.scheduler_frontier_generation = UINT64_C(53);
    frame.member_count = layout.member_count;
    frame.scheduler_task_count = 1U;
    frame.scheduler_task_capacity = 1U;
    frame.readiness_word_count = layout.readiness_word_count;
    frame.signal_slot_count = layout.signal_slot_count;
    frame.metadata_count = layout.metadata_count;
    frame.fanout_edge_count = layout.fanout_edge_count;
    frame.committed_signal_capacity = layout.committed_signal_capacity;
    frame.pending_write_capacity = layout.pending_write_capacity;
    frame.staged_event_capacity = layout.staged_event_capacity;
    frame.current_member = UINT32_MAX;
    frame.current_pending_write = UINT32_MAX;
    frame.ready_words = ready_words.data();
    frame.members = members.data();
    frame.scheduler_tasks = tasks.data();
    frame.planes = planes.data();
    frame.fanout_edges = fanout_edges.data();
    frame.port_planes = port_planes.data();
    frame.pending_writes = pending_writes.data();
    frame.staged_events = staged_events.data();
    frame.committed_signals = committed_signals.data();
    frame.native_frontier_member_dispatches = &dispatch_count;
    frame.slot = slot;
    frame.cut.scheduler_frontier_generation
        = frame.scheduler_frontier_generation;
    frame.cut.kind = RegionFrontierCutKindV2::closed_prefix;

    const auto boundary_aval_before = boundary_aval;
    const auto boundary_bval_before = boundary_bval;
    const auto status = executor->step_entry()(&frame);
    require(status == RegionFrontierStatusV2::generic_update_batch_ready,
        "the generated Generic body reports its deferred Update batch");
    require(frame.scheduler_task_cursor == 1U
            && frame.pending_write_count == 1U
            && frame.staged_event_count == 1U
            && frame.committed_signal_count == 0U
            && frame.generic_update_ack_count == 0U
            && dispatch_count == 1U
            && boundary_aval == boundary_aval_before
            && boundary_bval == boundary_bval_before,
        "the entry consumes one body and leaves host staging unacknowledged");
    require(region_frontier_generic_batch_ready_counts_valid_v2(
                frame.pending_write_count, frame.staged_event_count,
                frame.generic_update_ack_count),
        "the nonempty deferred batch has matching rows and no premature ACK");

    const auto write_site = std::find_if(layout.write_sites,
        layout.write_sites + layout.write_site_count,
        [&](const RegionFrontierWriteSiteV2& site) {
            return site.member_index == producer_index;
        });
    require(write_site != layout.write_sites + layout.write_site_count,
        "the producer has its exact immutable generic write site");
    const auto& pending = pending_writes.at(write_site->pending_slot);
    const auto& event = staged_events.front();
    const auto same_key = [](const RegionFrontierKeyV2& left,
                             const RegionFrontierKeyV2& right) noexcept {
        return left.time == right.time && left.delta == right.delta
            && left.systemverilog_round == right.systemverilog_round
            && left.stable_order == right.stable_order
            && left.sequence == right.sequence
            && left.process_domain == right.process_domain
            && left.phase == right.phase;
    };
    require(region_frontier_generic_write_descriptor_matches_v2(event,
                pending, *write_site, layout.signals[write_site->signal_slot],
                layout.members[producer_index], producer_index,
                write_site->signal_slot, activation_key, slot)
            && event.kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::generic_deferred_update)
            && event.descriptor_index == write_site->pending_slot
            && event.stable_order == stable_order
            && same_key(event.origin, activation_key)
            && same_key(pending.origin, activation_key)
            && region_frontier_key_is_zero_v2(pending.commit_key)
            && pending.flags == (RegionFrontierPendingWriteFlagsV2::pending_active
                | RegionFrontierPendingWriteFlagsV2::pending_value_ready
                | RegionFrontierPendingWriteFlagsV2::pending_generic_target),
        "the generated event and private row preserve the original Generic key");
    const auto input_slot = std::find_if(layout.signals,
        layout.signals + layout.signal_slot_count,
        [](const RegionFrontierSignalLayoutV2& signal) {
            return signal.signal_id == source_signal_id;
        });
    require(input_slot != layout.signals + layout.signal_slot_count,
        "the generic input has a read-only port slot");
    const auto input_index = static_cast<std::size_t>(
        input_slot - layout.signals);
    require(pending.value_planes[0U][0U] == boundary_aval[input_index][0U]
            && pending.value_planes[0U][1U] == boundary_aval[input_index][1U]
            && pending.value_planes[1U][0U] == boundary_bval[input_index][0U]
            && pending.value_planes[1U][1U] == boundary_bval[input_index][1U],
        "the generated Logic4 row preserves both 65-bit aval/bval words");
}

void check_remapped_frontier_binding(
    const SignalUpdateDomain domain)
{
    const bool generic = domain == SignalUpdateDomain::generic;
    auto kernel = make_certified_frontier_kernel(65U, domain);
    kernel = remap_frontier_physical_ids(std::move(kernel), 100U, 1000U);
    auto executor = make_executor(kernel,
        fsim::compiler::JitOptimizationLevel::o2,
        generic ? "frontier-remapped-generic-binding"
                : "frontier-remapped-sv-binding");
    require(executor != nullptr,
        "a physically remapped kernel materializes an exact V2 entry");
    require(executor->step_entry() != nullptr
            && fsim::compiler::llvm_detail::RegionFrontierTestAccess::
                shared_body_address(*executor) != 0U,
        "the remapped wrapper calls a real shared native body");
    require(executor->layout().members[0U].process_id >= 1000U,
        "the remapped layout uses a physical ProcessId distinct from scheduler order");
    if (generic) {
        run_region_frontier_generic_stable_order_test(kernel,
            executor->step_entry(), executor->layout(), 83U);
    } else {
        run_region_frontier_shared_binding_guard_tests(kernel,
            executor->step_entry(), executor->layout(), 83U);
    }
}

void check_generic_ineligible_shapes()
{
    const auto rejects = [](RegionConeActivationKernel kernel,
                            const std::string_view identity) {
        fsim::compiler::LlvmJitOptions options;
        options.debug_instrumentation = false;
        options.require_direct_update_slots = true;
        options.require_direct_read_signals = true;
        return fsim::compiler::LlvmRegionFrontierExecutor::try_create(
            kernel, options, identity) == nullptr;
    };

    auto vhdl_projected = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic);
    vhdl_projected.outputs.front().update_kind
        = RegionUpdateKind::vhdl_projected;
    require(rejects(std::move(vhdl_projected), "generic-vhdl-projected-decline"),
        "Generic admission still rejects projected VHDL output updates");

    auto systemc = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic);
    systemc.outputs.front().update_kind = RegionUpdateKind::systemc;
    require(rejects(std::move(systemc), "generic-systemc-decline"),
        "Generic admission still rejects SystemC output updates");

    auto slice = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic);
    slice.outputs.front().offset = 1U;
    require(rejects(std::move(slice), "generic-slice-decline"),
        "Generic admission still rejects projected or partial output slices");

    auto logic9_vhdl = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic, ValueKind::logic9);
    logic9_vhdl.outputs.front().update_kind = RegionUpdateKind::vhdl_projected;
    require(rejects(std::move(logic9_vhdl), "generic-logic9-vhdl-decline"),
        "Generic Logic9 admission rejects projected VHDL updates");

    auto logic9_systemc = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic, ValueKind::logic9);
    logic9_systemc.outputs.front().update_kind = RegionUpdateKind::systemc;
    require(rejects(std::move(logic9_systemc),
                "generic-logic9-systemc-decline"),
        "Generic Logic9 admission rejects SystemC updates");

    auto logic9_slice = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic, ValueKind::logic9);
    logic9_slice.outputs.front().offset = 1U;
    require(rejects(std::move(logic9_slice), "generic-logic9-slice-decline"),
        "Generic Logic9 admission rejects partial signal slices");

    auto logic9_delayed = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic, ValueKind::logic9);
    logic9_delayed.outputs.front().projected_delay = 1U;
    require(rejects(std::move(logic9_delayed),
                "generic-logic9-delay-decline"),
        "Generic Logic9 admission rejects delayed projected writes");

}

[[nodiscard]] RegionFrontierStagingValues reduction_values(
    const std::uint32_t width,
    const std::vector<std::uint64_t>& input_aval,
    const std::vector<std::uint64_t>& input_bval,
    const std::uint64_t expected_aval,
    const std::uint64_t expected_bval)
{
    RegionFrontierStagingValues values;
    values.external_input = { input_aval, input_bval };
    values.expected_internal = { { expected_aval }, { expected_bval } };
    values.expected_boundary = values.expected_internal;
    const bool result_is_one = expected_aval == 1U && expected_bval == 0U;
    values.initial_internal = { { result_is_one ? 0U : 1U }, { 0U } };
    values.initial_boundary = values.initial_internal;
    require(input_aval.size() == words_for(width)
            && input_bval.size() == words_for(width),
        "reduction inputs retain every partial or full word");
    return values;
}

void run_reduction_case(const std::uint32_t width,
    const RegionConeActivationKernel& kernel,
    const fsim::compiler::LlvmRegionFrontierExecutor& executor,
    const std::vector<std::uint64_t>& input_aval,
    const std::vector<std::uint64_t>& input_bval,
    const std::uint64_t expected_aval,
    const std::uint64_t expected_bval)
{
    auto values = reduction_values(width, input_aval, input_bval,
        expected_aval, expected_bval);
    run_region_frontier_staging_tests(kernel, executor.step_entry(),
        executor.layout(), 71U, std::move(values));
}

void run_operator_case(const std::uint32_t width,
    const RegionConeActivationKernel& kernel,
    const fsim::compiler::LlvmRegionFrontierExecutor& executor,
    std::vector<RegionFrontierSignalInput> inputs,
    RegionFrontierTestValue expected)
{
    auto values = operation_values(width, std::move(inputs),
        std::move(expected));
    run_region_frontier_staging_tests(kernel, executor.step_entry(),
        executor.layout(), 71U, std::move(values));
}

[[nodiscard]] std::uint8_t binary_result_code(
    const BinaryOperator operation,
    const std::uint8_t lhs,
    const std::uint8_t rhs)
{
    switch (operation) {
    case BinaryOperator::bit_and:
        return bit_and_codes.at(lhs).at(rhs);
    case BinaryOperator::bit_or:
        return bit_or_codes.at(lhs).at(rhs);
    case BinaryOperator::bit_xor:
        return bit_xor_codes.at(lhs).at(rhs);
    default:
        throw std::invalid_argument { "test oracle supports bitwise binary ops" };
    }
}

[[nodiscard]] RegionFrontierTestValue patterned_logic4(
    const std::uint32_t width, const std::uint32_t seed)
{
    RegionFrontierTestValue result {
        std::vector<std::uint64_t>(words_for(width), 0U),
        std::vector<std::uint64_t>(words_for(width), 0U),
    };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        const auto code = static_cast<std::uint8_t>(
            (bit * seed + seed) % logic4_codes.size());
        set_logic4_bit(result, bit, code);
    }
    return result;
}

[[nodiscard]] std::uint8_t get_logic9_bit(
    const RegionFrontierTestValue& value, const std::uint32_t bit) noexcept
{
    const auto word = static_cast<std::size_t>(bit / 64U);
    const auto shift = bit % 64U;
    const std::array<const std::vector<std::uint64_t>*, 4U> planes {
        &value.aval, &value.bval, &value.plane2, &value.plane3,
    };
    std::uint8_t result { };
    for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
        result |= static_cast<std::uint8_t>(
            (((*planes[plane])[word] >> shift) & 1U) << plane);
    }
    return result;
}

void set_logic9_bit(RegionFrontierTestValue& value,
    const std::uint32_t bit, const std::uint8_t code)
{
    const auto word = static_cast<std::size_t>(bit / 64U);
    const auto mask = UINT64_C(1) << (bit % 64U);
    const std::array<std::vector<std::uint64_t>*, 4U> planes {
        &value.aval, &value.bval, &value.plane2, &value.plane3,
    };
    for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
        auto& plane_word = planes[plane]->at(word);
        if ((code & (1U << plane)) != 0U) {
            plane_word |= mask;
        } else {
            plane_word &= ~mask;
        }
    }
}

[[nodiscard]] RegionFrontierTestValue patterned_logic9(
    const std::uint32_t width, const std::uint32_t seed)
{
    auto result = repeated_logic9(width, 0U);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        set_logic9_bit(result, bit, static_cast<std::uint8_t>(
            (bit * seed + seed) % 9U));
    }
    return result;
}

[[nodiscard]] std::uint8_t logic9_binary_result(
    const BinaryOperator operation,
    const std::uint8_t lhs,
    const std::uint8_t rhs)
{
    using fsim::runtime::Logic9;
    const auto left = static_cast<Logic9>(lhs);
    const auto right = static_cast<Logic9>(rhs);
    switch (operation) {
    case BinaryOperator::bit_and:
        return static_cast<std::uint8_t>(fsim::runtime::logic_and(left, right));
    case BinaryOperator::bit_or:
        return static_cast<std::uint8_t>(fsim::runtime::logic_or(left, right));
    case BinaryOperator::bit_xor:
        return static_cast<std::uint8_t>(fsim::runtime::logic_xor(left, right));
    default:
        throw std::invalid_argument { "Logic9 witness covers bitwise operators" };
    }
}

[[nodiscard]] RegionFrontierTestValue patterned_logic9_binary_result(
    const RegionFrontierTestValue& lhs,
    const RegionFrontierTestValue& rhs,
    const BinaryOperator operation,
    const std::uint32_t width)
{
    auto result = repeated_logic9(width, 0U);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        set_logic9_bit(result, bit, logic9_binary_result(operation,
            get_logic9_bit(lhs, bit), get_logic9_bit(rhs, bit)));
    }
    return result;
}

[[nodiscard]] RegionFrontierTestValue patterned_logic9_unary_not_result(
    const RegionFrontierTestValue& source, const std::uint32_t width)
{
    auto result = repeated_logic9(width, 0U);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        set_logic9_bit(result, bit, static_cast<std::uint8_t>(
            fsim::runtime::logic_not(static_cast<fsim::runtime::Logic9>(
                get_logic9_bit(source, bit)))));
    }
    return result;
}

[[nodiscard]] const std::vector<std::uint64_t>& logic9_plane_words(
    const RegionFrontierTestValue& value, const std::size_t plane)
{
    const std::array<const std::vector<std::uint64_t>*, 4U> planes {
        &value.aval, &value.bval, &value.plane2, &value.plane3,
    };
    return *planes.at(plane);
}

[[nodiscard]] bool same_frontier_key(
    const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_frontier_slot(
    const RegionFrontierSlotV2& left,
    const RegionFrontierSlotV2& right) noexcept
{
    return left.time == right.time && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_frontier_cut(
    const RegionFrontierCutV2& left,
    const RegionFrontierCutV2& right) noexcept
{
    if (left.scheduler_frontier_generation
            != right.scheduler_frontier_generation
        || !same_frontier_key(left.next_key, right.next_key)
        || left.kind != right.kind) {
        return false;
    }
    for (std::size_t index = 0U; index < 7U; ++index) {
        if (left.reserved[index] != right.reserved[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_frontier_frame(
    const RegionFrontierFrameV2& left,
    const RegionFrontierFrameV2& right) noexcept
{
    return left.abi_version == right.abi_version
        && left.struct_size == right.struct_size
        && left.value_plane_contract == right.value_plane_contract
        && left.generic_update_ack_count == right.generic_update_ack_count
        && left.runtime_generation == right.runtime_generation
        && left.bound_runtime_generation == right.bound_runtime_generation
        && left.certificate_generation == right.certificate_generation
        && left.component_generation == right.component_generation
        && left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && left.member_count == right.member_count
        && left.scheduler_task_count == right.scheduler_task_count
        && left.scheduler_task_cursor == right.scheduler_task_cursor
        && left.scheduler_task_capacity == right.scheduler_task_capacity
        && left.readiness_word_count == right.readiness_word_count
        && left.signal_slot_count == right.signal_slot_count
        && left.metadata_count == right.metadata_count
        && left.fanout_edge_count == right.fanout_edge_count
        && left.committed_signal_capacity == right.committed_signal_capacity
        && left.committed_signal_count == right.committed_signal_count
        && left.pending_write_capacity == right.pending_write_capacity
        && left.pending_write_count == right.pending_write_count
        && left.staged_event_capacity == right.staged_event_capacity
        && left.staged_event_count == right.staged_event_count
        && left.current_member == right.current_member
        && left.current_pending_write == right.current_pending_write
        && left.current_commit_changed == right.current_commit_changed
        && left.saved_body_pc == right.saved_body_pc
        && left.ready_words == right.ready_words
        && left.members == right.members
        && left.scheduler_tasks == right.scheduler_tasks
        && left.planes == right.planes
        && left.metadata == right.metadata
        && left.fanout_edges == right.fanout_edges
        && left.port_planes == right.port_planes
        && left.pending_writes == right.pending_writes
        && left.staged_events == right.staged_events
        && left.committed_signals == right.committed_signals
        && left.native_frontier_member_dispatches
            == right.native_frontier_member_dispatches
        && left.stop_requested == right.stop_requested
        && same_frontier_slot(left.slot, right.slot)
        && same_frontier_cut(left.cut, right.cut);
}

[[nodiscard]] bool same_frontier_member(
    const RegionFrontierMemberV2& left,
    const RegionFrontierMemberV2& right) noexcept
{
    return left.process_id == right.process_id && left.flags == right.flags
        && left.static_trigger_mask == right.static_trigger_mask
        && same_frontier_key(left.queued_key, right.queued_key)
        && same_frontier_key(left.activation_origin, right.activation_origin)
        && same_frontier_key(left.pending_activation_origin,
            right.pending_activation_origin);
}

[[nodiscard]] bool same_frontier_pending_write(
    const RegionFrontierPendingWriteV2& left,
    const RegionFrontierPendingWriteV2& right) noexcept
{
    if (left.member_index != right.member_index
        || left.signal_slot != right.signal_slot
        || left.source_instruction != right.source_instruction
        || left.update_kind != right.update_kind
        || left.flags != right.flags || left.reserved != right.reserved
        || !same_frontier_key(left.commit_key, right.commit_key)
        || !same_frontier_key(left.origin, right.origin)
        || left.value_kind != right.value_kind || left.width != right.width
        || left.word_count != right.word_count
        || left.plane_count != right.plane_count) {
        return false;
    }
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        if (left.value_planes[plane] != right.value_planes[plane]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_frontier_event(
    const RegionFrontierStagedEventV2& left,
    const RegionFrontierStagedEventV2& right) noexcept
{
    return left.kind == right.kind
        && left.descriptor_index == right.descriptor_index
        && left.stable_order == right.stable_order
        && same_frontier_key(left.origin, right.origin);
}

[[nodiscard]] bool same_frontier_plane(
    const RegionFrontierPlaneV2& left,
    const RegionFrontierPlaneV2& right) noexcept
{
    if (left.signal_id != right.signal_id
        || left.owner_process_id != right.owner_process_id
        || left.value_kind != right.value_kind || left.width != right.width
        || left.word_count != right.word_count
        || left.plane_count != right.plane_count || left.flags != right.flags
        || left.metadata_index != right.metadata_index) {
        return false;
    }
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        if (left.boundary_planes[plane] != right.boundary_planes[plane]
            || left.current_planes[plane] != right.current_planes[plane]
            || left.previous_planes[plane] != right.previous_planes[plane]
            || left.stored_planes[plane] != right.stored_planes[plane]
            || left.owner_planes[plane] != right.owner_planes[plane]) {
            return false;
        }
    }
    return true;
}

class GenericLogic9EntryFrame final {
public:
    using PlaneWords = std::array<std::vector<std::uint64_t>, 4U>;

    GenericLogic9EntryFrame(const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout,
        const std::vector<RegionFrontierSignalInput>& inputs)
        : layout_ref(layout)
        , planes(layout.signal_slot_count)
        , boundary_words(layout.signal_slot_count)
        , port_planes(layout.signal_slot_count, nullptr)
        , ready_words(layout.readiness_word_count, 0U)
        , members(layout.member_count)
        , tasks(1U)
        , fanout_edges(layout.fanout_edge_count)
        , pending_writes(layout.pending_write_capacity)
        , pending_words(layout.pending_write_capacity)
        , staged_events(layout.staged_event_capacity)
        , committed_signals(layout.committed_signal_capacity)
    {
        require(layout.execution_mode
                    == RegionFrontierExecutionModeV2::generic_deferred_update
                && layout.member_count == kernel.members.size()
                && layout.signal_slot_count != 0U
                && layout.signals != nullptr
                && layout.write_site_count == kernel.outputs.size()
                && layout.write_sites != nullptr
                && layout.members != nullptr
                && layout.pending_write_capacity >= layout.write_site_count
                && layout.staged_event_capacity >= layout.write_site_count,
            "the Logic9 fixture uses a typed Generic deferred-update plan");

        for (std::size_t signal_slot = 0U;
             signal_slot < layout.signal_slot_count; ++signal_slot) {
            const auto& descriptor = layout.signals[signal_slot];
            require(descriptor.value_kind == RegionFrontierValueKindV2::logic9
                    && descriptor.plane_count
                        == kRegionFrontierLogic9PlaneCountV2
                    && descriptor.flags
                        == RegionFrontierPlaneFlagsV2::read_only_boundary_port,
                "Generic Logic9 bindings are readonly four-plane values");
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                boundary_words[signal_slot][plane].assign(
                    descriptor.word_count, 0U);
            }
            const auto input = std::find_if(inputs.begin(), inputs.end(),
                [&](const RegionFrontierSignalInput& candidate) {
                    return candidate.signal == descriptor.signal_id;
                });
            if (input != inputs.end()) {
                require(input->value.value_kind == ValueKind::logic9,
                    "Generic Logic9 inputs preserve their typed source kind");
                for (std::size_t plane = 0U; plane < 4U; ++plane) {
                    const auto& words = logic9_plane_words(input->value, plane);
                    require(words.size() == descriptor.word_count,
                        "Generic Logic9 input planes cover the certified words");
                    boundary_words[signal_slot][plane] = words;
                }
            }

            auto& plane = planes[signal_slot];
            plane.signal_id = descriptor.signal_id;
            plane.owner_process_id = descriptor.owner_process_id;
            plane.value_kind = descriptor.value_kind;
            plane.width = descriptor.width;
            plane.word_count = descriptor.word_count;
            plane.plane_count = descriptor.plane_count;
            plane.flags = descriptor.flags;
            plane.metadata_index = descriptor.metadata_index;
            for (std::size_t value_plane = 0U; value_plane < 4U;
                 ++value_plane) {
                plane.boundary_planes[value_plane]
                    = boundary_words[signal_slot][value_plane].data();
            }
            require(region_frontier_plane_bindings_valid_v2(plane),
                "Generic frames expose only valid readonly Logic9 planes");
            port_planes[signal_slot] = &plane;
        }

        const auto producer = std::find_if(kernel.members.begin(),
            kernel.members.end(), [](const RegionConeKernelMember& member) {
                return member.process == producer_process_id;
            });
        require(producer != kernel.members.end(),
            "the Generic Logic9 frame has the certified producer member");
        producer_index = static_cast<std::uint32_t>(
            producer - kernel.members.begin());
        for (std::size_t index = 0U; index < members.size(); ++index) {
            members[index].process_id = layout.members[index].process_id;
            members[index].flags
                = RegionFrontierMemberFlagsV2::waiting_on_static;
        }

        activation_key = {
            UINT64_C(13), UINT64_C(7), UINT64_C(0),
            UINT64_C(29), UINT64_C(41),
            kRegionFrontierGenericDomainV2, kRegionFrontierActivePhaseV2,
        };
        slot = { activation_key.time, activation_key.delta,
            activation_key.systemverilog_round,
            activation_key.process_domain, activation_key.phase };
        auto& producer_member = members[producer_index];
        producer_member.flags = RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid;
        producer_member.queued_key = activation_key;
        producer_member.activation_origin = activation_key;
        ready_words[producer_index / 64U]
            |= UINT64_C(1) << (producer_index % 64U);
        tasks[0U] = { activation_key.stable_order, activation_key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::member_activation,
                producer_index) };

        if (layout.fanout_edge_count != 0U) {
            require(layout.fanout_edges != nullptr,
                "the shared Generic frame has backing for certified fanout topology");
            for (std::size_t edge = 0U; edge < fanout_edges.size(); ++edge) {
                fanout_edges[edge] = layout.fanout_edges[edge];
            }
        }
        for (std::size_t site_index = 0U;
             site_index < layout.write_site_count; ++site_index) {
            const auto& site = layout.write_sites[site_index];
            require(site.pending_slot < pending_writes.size()
                    && site.plane_count == 4U,
                "each Generic Logic9 site owns four private output planes");
            auto& pending = pending_writes[site.pending_slot];
            auto& words = pending_words[site.pending_slot];
            const auto sentinel = repeated_logic9(site.width, 8U);
            pending.member_index = site.member_index;
            pending.signal_slot = site.signal_slot;
            pending.source_instruction = site.source_instruction;
            pending.update_kind = site.update_kind;
            pending.value_kind = site.value_kind;
            pending.width = site.width;
            pending.word_count = site.word_count;
            pending.plane_count = site.plane_count;
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                words[plane] = logic9_plane_words(sentinel, plane);
                pending.value_planes[plane] = words[plane].data();
            }
        }
        for (auto& event : staged_events) {
            event.kind = UINT32_C(0xfedcba98);
            event.descriptor_index = UINT32_C(0x87654321);
            event.stable_order = UINT64_C(0x123456789abcdef0);
            event.origin = activation_key;
        }

        frame.struct_size = sizeof(RegionFrontierFrameV2);
        frame.runtime_generation = UINT64_C(17);
        frame.bound_runtime_generation = frame.runtime_generation;
        frame.certificate_generation = layout.certificate_generation;
        frame.component_generation = layout.component_generation;
        frame.scheduler_frontier_generation = UINT64_C(53);
        frame.member_count = layout.member_count;
        frame.scheduler_task_count = 1U;
        frame.scheduler_task_capacity = 1U;
        frame.readiness_word_count = layout.readiness_word_count;
        frame.signal_slot_count = layout.signal_slot_count;
        frame.metadata_count = layout.metadata_count;
        frame.fanout_edge_count = layout.fanout_edge_count;
        frame.committed_signal_capacity = layout.committed_signal_capacity;
        frame.pending_write_capacity = layout.pending_write_capacity;
        frame.staged_event_capacity = layout.staged_event_capacity;
        frame.current_member = UINT32_MAX;
        frame.current_pending_write = UINT32_MAX;
        frame.ready_words = ready_words.data();
        frame.members = members.data();
        frame.scheduler_tasks = tasks.data();
        frame.planes = planes.data();
        frame.fanout_edges = fanout_edges.empty()
            ? nullptr : fanout_edges.data();
        frame.port_planes = port_planes.data();
        frame.pending_writes = pending_writes.data();
        frame.staged_events = staged_events.data();
        frame.committed_signals = committed_signals.empty()
            ? nullptr : committed_signals.data();
        frame.native_frontier_member_dispatches = &dispatch_count;
        frame.slot = slot;
        frame.cut.scheduler_frontier_generation
            = frame.scheduler_frontier_generation;
        frame.cut.kind = RegionFrontierCutKindV2::closed_prefix;
    }

    [[nodiscard]] std::size_t boundary_slot(const SignalId signal) const
    {
        for (std::size_t index = 0U; index < planes.size(); ++index) {
            if (planes[index].signal_id == signal) {
                return index;
            }
        }
        throw std::runtime_error { "Generic Logic9 signal slot is absent" };
    }

    const RegionFrontierLayoutV2& layout_ref;
    std::vector<RegionFrontierPlaneV2> planes;
    std::vector<PlaneWords> boundary_words;
    std::vector<const RegionFrontierPlaneV2*> port_planes;
    std::vector<std::uint64_t> ready_words;
    std::vector<RegionFrontierMemberV2> members;
    std::vector<RegionFrontierSchedulerTaskV2> tasks;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_edges;
    std::vector<RegionFrontierPendingWriteV2> pending_writes;
    std::vector<PlaneWords> pending_words;
    std::vector<RegionFrontierStagedEventV2> staged_events;
    std::vector<RegionFrontierCommittedSignalV2> committed_signals;
    std::uint64_t dispatch_count { };
    std::uint32_t producer_index { };
    RegionFrontierKeyV2 activation_key;
    RegionFrontierSlotV2 slot;
    RegionFrontierFrameV2 frame;
};

[[nodiscard]] std::unique_ptr<llvm::orc::LLJIT>
make_pending_slot_mapping_jit(
    const fsim::compiler::JitOptimizationLevel optimization)
{
    auto target = llvm::cantFail(
        llvm::orc::JITTargetMachineBuilder::detectHost());
    target.setCodeGenOptLevel(optimization
            == fsim::compiler::JitOptimizationLevel::o0
            ? llvm::CodeGenOptLevel::None
            : llvm::CodeGenOptLevel::Default);
    llvm::orc::LLJITBuilder builder;
    builder.setNumCompileThreads(0U);
    builder.setJITTargetMachineBuilder(std::move(target));
    return llvm::cantFail(builder.create());
}

void optimize_pending_slot_mapping_module(llvm::Module& module,
    const fsim::compiler::JitOptimizationLevel optimization)
{
    if (optimization == fsim::compiler::JitOptimizationLevel::o0) {
        return;
    }
    llvm::LoopAnalysisManager loop_analyses;
    llvm::FunctionAnalysisManager function_analyses;
    llvm::CGSCCAnalysisManager cgscc_analyses;
    llvm::ModuleAnalysisManager module_analyses;
    llvm::PassBuilder pass_builder;
    pass_builder.registerModuleAnalyses(module_analyses);
    pass_builder.registerCGSCCAnalyses(cgscc_analyses);
    pass_builder.registerFunctionAnalyses(function_analyses);
    pass_builder.registerLoopAnalyses(loop_analyses);
    pass_builder.crossRegisterProxies(loop_analyses, function_analyses,
        cgscc_analyses, module_analyses);
    auto pipeline = pass_builder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::O2);
    pipeline.run(module, module_analyses);
}

void check_generic_permuted_pending_slots(
    const fsim::compiler::JitOptimizationLevel optimization)
{
    auto kernel = make_certified_frontier_kernel(65U,
        SignalUpdateDomain::generic, ValueKind::logic9);
    auto plan = fsim::compiler::RegionFrontierKernelPlan::try_create(kernel);
    require(plan.has_value(),
        "a certified Generic kernel supplies the pending-slot test layout");

    auto layout = plan->layout();
    std::vector<RegionFrontierSignalLayoutV2> signals(layout.signals,
        layout.signals + layout.signal_slot_count);
    std::vector<RegionFrontierWriteSiteV2> sites(layout.write_sites,
        layout.write_sites + layout.write_site_count);
    const auto producer = std::find_if(kernel.members.begin(),
        kernel.members.end(), [](const RegionConeKernelMember& member) {
            return member.process == producer_process_id;
        });
    const auto consumer = std::find_if(kernel.members.begin(),
        kernel.members.end(), [](const RegionConeKernelMember& member) {
            return member.process == consumer_process_id;
        });
    require(producer != kernel.members.end()
            && consumer != kernel.members.end(),
        "the Generic fixture has distinct producer and consumer members");
    const auto producer_index = static_cast<std::uint32_t>(
        producer - kernel.members.begin());
    const auto consumer_index = static_cast<std::uint32_t>(
        consumer - kernel.members.begin());
    const auto producer_site = std::find_if(sites.begin(), sites.end(),
        [&](const RegionFrontierWriteSiteV2& site) {
            return site.member_index == producer_index;
        });
    const auto consumer_site = std::find_if(sites.begin(), sites.end(),
        [&](const RegionFrontierWriteSiteV2& site) {
            return site.member_index == consumer_index;
        });
    require(producer_site != sites.end() && consumer_site != sites.end()
            && producer_site->width == 65U
            && consumer_site->width == 65U,
        "the canonical Generic sites start with distinct owners and shapes");

    auto& consumer_signal = signals.at(consumer_site->signal_slot);
    consumer_signal.width = 64U;
    consumer_signal.word_count = 1U;
    consumer_site->width = 64U;
    consumer_site->word_count = 1U;
    std::swap(producer_site->pending_slot, consumer_site->pending_slot);
    require(producer_site->pending_slot == 1U
            && consumer_site->pending_slot == 0U,
        "the Generic test permutes distinct 65-bit and 64-bit slots");
    layout.signals = signals.data();
    layout.write_sites = sites.data();

    constexpr std::string_view symbol {
        "fsim_generic_permuted_pending_slots"
    };
    auto jit = make_pending_slot_mapping_jit(optimization);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>(std::string { symbol },
        *context);
    module->setDataLayout(jit->getDataLayout());
    module->setTargetTriple(jit->getTargetTriple());
    const fsim::runtime::simir::scratch::EmitCertifiedMemberBodyV2
        emit_member = [](llvm::IRBuilder<>&, const std::size_t,
            llvm::Value*) { };
    const fsim::runtime::simir::scratch::EmitCertifiedInternalCommitV2
        emit_internal_commit = [](llvm::IRBuilder<>&, llvm::Value*,
            llvm::Value*) { };
    auto zero_capacity_layout = layout;
    zero_capacity_layout.pending_write_capacity = 0U;
    bool zero_capacity_rejected { };
    try {
        (void)fsim::runtime::simir::scratch::emit_region_frontier_loop_v2(
            *module, "fsim_generic_zero_pending_capacity",
            zero_capacity_layout, emit_member, emit_internal_commit);
    } catch (const std::invalid_argument&) {
        zero_capacity_rejected = true;
    }
    require(zero_capacity_rejected,
        "a nonempty Generic write-site table rejects zero pending capacity");
    require(fsim::runtime::simir::scratch::emit_region_frontier_loop_v2(
                *module, std::string { symbol }, layout,
                emit_member, emit_internal_commit) != nullptr,
        "the generic entry emits for a valid permuted pending-slot layout");
    std::string verification_error;
    llvm::raw_string_ostream verification_stream { verification_error };
    require(!llvm::verifyModule(*module, &verification_stream),
        "the pending-slot mapping entry verifies before optimization");
    optimize_pending_slot_mapping_module(*module, optimization);
    verification_error.clear();
    require(!llvm::verifyModule(*module, &verification_stream),
        "the pending-slot mapping entry verifies after optimization");
    llvm::cantFail(jit->addIRModule(llvm::orc::ThreadSafeModule {
        std::move(module), std::move(context),
    }));

    const std::vector<RegionFrontierSignalInput> no_inputs;
    GenericLogic9EntryFrame fixture { kernel, layout, no_inputs };
    const auto entry = llvm::cantFail(jit->lookup(std::string { symbol }))
        .toPtr<RegionFrontierStepEntryV2>();
    require(entry(&fixture.frame) == RegionFrontierStatusV2::quiescent
            && fixture.frame.scheduler_task_cursor == 1U
            && fixture.frame.pending_write_count == 0U
            && fixture.frame.staged_event_count == 0U
            && fixture.frame.generic_update_ack_count == 0U
            && fixture.dispatch_count == 1U,
        "preflight maps each Generic write shape through its declared pending slot");
}

void check_generic_logic9_entry_success(
    const RegionConeActivationKernel& kernel,
    const fsim::compiler::LlvmRegionFrontierExecutor& executor,
    const std::vector<RegionFrontierSignalInput>& inputs,
    const RegionFrontierTestValue& expected)
{
    const auto& layout = executor.layout();
    GenericLogic9EntryFrame fixture { kernel, layout, inputs };
    const auto boundary_before = fixture.boundary_words;
    const auto status = executor.step_entry()(&fixture.frame);
    require(status == RegionFrontierStatusV2::generic_update_batch_ready
            && fixture.frame.scheduler_task_cursor == 1U
            && fixture.frame.pending_write_count == 1U
            && fixture.frame.staged_event_count == 1U
            && fixture.frame.committed_signal_count == 0U
            && fixture.frame.generic_update_ack_count == 0U
            && fixture.dispatch_count == 1U,
        "Generic Logic9 entry stages one detached event and no ACK/commit");
    require(fixture.boundary_words == boundary_before,
        "Generic Logic9 entry leaves every readonly source and output plane unchanged");
    require(region_frontier_generic_batch_ready_counts_valid_v2(
                fixture.frame.pending_write_count,
                fixture.frame.staged_event_count,
                fixture.frame.generic_update_ack_count),
        "the Generic Logic9 output is a complete unacknowledged batch");

    const auto site = std::find_if(layout.write_sites,
        layout.write_sites + layout.write_site_count,
        [&](const RegionFrontierWriteSiteV2& candidate) {
            return candidate.member_index == fixture.producer_index;
        });
    require(site != layout.write_sites + layout.write_site_count
            && site->signal_slot < layout.signal_slot_count,
        "the Generic producer has one immutable four-plane write site");
    const auto& pending = fixture.pending_writes.at(site->pending_slot);
    const auto& event = fixture.staged_events.front();
    require(region_frontier_generic_write_descriptor_matches_v2(event,
                pending, *site, layout.signals[site->signal_slot],
                layout.members[fixture.producer_index], fixture.producer_index,
                site->signal_slot, fixture.activation_key, fixture.slot)
            && event.kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::generic_deferred_update)
            && event.descriptor_index == site->pending_slot
            && event.stable_order == fixture.activation_key.stable_order
            && same_frontier_key(event.origin, fixture.activation_key)
            && same_frontier_key(pending.origin, fixture.activation_key)
            && region_frontier_key_is_zero_v2(pending.commit_key)
            && pending.flags == kRegionFrontierGenericWriteFlagsV2,
        "the Generic Logic9 descriptor authenticates exact site and source key");
    auto bad_site = *site;
    bad_site.value_kind = static_cast<RegionFrontierValueKindV2>(2U);
    auto bad_pending = pending;
    bad_pending.value_planes[3U] = nullptr;
    require(!region_frontier_generic_write_descriptor_matches_v2(event,
                pending, bad_site, layout.signals[site->signal_slot],
                layout.members[fixture.producer_index], fixture.producer_index,
                site->signal_slot, fixture.activation_key, fixture.slot)
            && !region_frontier_generic_write_descriptor_matches_v2(event,
                bad_pending, *site, layout.signals[site->signal_slot],
                layout.members[fixture.producer_index], fixture.producer_index,
                site->signal_slot, fixture.activation_key, fixture.slot),
        "the Generic descriptor helper rejects unknown kinds and missing L9 planes");
    require(expected.value_kind == ValueKind::logic9
            && expected.aval.size() == site->word_count
            && expected.bval.size() == site->word_count
            && expected.plane2.size() == site->word_count
            && expected.plane3.size() == site->word_count,
        "the expected Logic9 result retains all exact word planes");
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        const auto& expected_words = logic9_plane_words(expected, plane);
        require(fixture.pending_words.at(site->pending_slot)[plane]
                    == expected_words,
            "the generated Generic Logic9 value preserves all four planes");
        for (std::size_t word = 0U; word < expected_words.size(); ++word) {
            require((expected_words[word] & ~word_mask(site->width, word)) == 0U,
                "the generated Logic9 output has canonical padding bits");
        }
    }
}

template<typename Mutator>
void check_generic_logic9_declines_unchanged(
    const RegionConeActivationKernel& kernel,
    const fsim::compiler::LlvmRegionFrontierExecutor& executor,
    const std::vector<RegionFrontierSignalInput>& inputs,
    Mutator&& mutate)
{
    const auto& layout = executor.layout();
    GenericLogic9EntryFrame fixture { kernel, layout, inputs };
    mutate(fixture);
    const auto boundary_before = fixture.boundary_words;
    const auto ready_before = fixture.ready_words;
    const auto planes_before = fixture.planes;
    const auto port_planes_before = fixture.port_planes;
    const auto members_before = fixture.members;
    const auto tasks_before = fixture.tasks;
    const auto pending_before = fixture.pending_writes;
    const auto pending_words_before = fixture.pending_words;
    const auto events_before = fixture.staged_events;
    const auto committed_before = fixture.committed_signals;
    const auto frame_before = fixture.frame;

    const auto status = executor.step_entry()(&fixture.frame);
    require(status == RegionFrontierStatusV2::decline_before_mutation,
        "malformed Logic9 input or tail declines before generated work");
    require(fixture.frame.scheduler_task_cursor == 0U
            && fixture.frame.pending_write_count == 0U
            && fixture.frame.staged_event_count == 0U
            && fixture.frame.committed_signal_count == 0U
            && fixture.frame.generic_update_ack_count == 0U
            && fixture.frame.current_member == UINT32_MAX
            && fixture.frame.current_pending_write == UINT32_MAX
            && fixture.frame.current_commit_changed == 0U
            && fixture.dispatch_count == 0U,
        "a malformed Logic9 buffer leaves counts, cursor, and dispatch untouched");
    require(fixture.ready_words == ready_before
            && fixture.boundary_words == boundary_before
            && fixture.pending_words == pending_words_before
            && fixture.port_planes == port_planes_before,
        "a malformed Logic9 buffer leaves every value and readiness plane unchanged");
    for (std::size_t index = 0U; index < fixture.planes.size(); ++index) {
        require(same_frontier_plane(fixture.planes[index], planes_before[index]),
            "preflight decline preserves readonly plane descriptors");
    }
    for (std::size_t index = 0U; index < fixture.members.size(); ++index) {
        require(same_frontier_member(fixture.members[index], members_before[index]),
            "preflight decline preserves queued member keys and flags");
    }
    for (std::size_t index = 0U; index < fixture.tasks.size(); ++index) {
        require(fixture.tasks[index].stable_order == tasks_before[index].stable_order
                && fixture.tasks[index].sequence == tasks_before[index].sequence
                && fixture.tasks[index].payload == tasks_before[index].payload,
            "preflight decline preserves the borrowed task descriptor");
    }
    for (std::size_t index = 0U; index < fixture.pending_writes.size(); ++index) {
        require(same_frontier_pending_write(
                    fixture.pending_writes[index], pending_before[index]),
            "preflight decline preserves every private pending descriptor");
    }
    for (std::size_t index = 0U; index < fixture.staged_events.size(); ++index) {
        require(same_frontier_event(
                    fixture.staged_events[index], events_before[index]),
            "preflight decline preserves the staged event storage");
    }
    require(fixture.committed_signals.size() == committed_before.size(),
        "preflight decline preserves the commit-log allocation");
    for (std::size_t index = 0U;
         index < fixture.committed_signals.size(); ++index) {
        require(fixture.committed_signals[index].signal_slot
                    == committed_before[index].signal_slot
                && fixture.committed_signals[index].changed
                    == committed_before[index].changed
                && fixture.committed_signals[index].state_changed
                    == committed_before[index].state_changed,
            "preflight decline leaves committed-log rows untouched");
    }
    require(same_frontier_frame(fixture.frame, frame_before),
        "preflight decline leaves every frame header, pointer, and cut field unchanged");
}

void check_generic_logic9_entries(
    const fsim::compiler::JitOptimizationLevel optimization)
{
    constexpr std::array widths {
        std::uint32_t { 1U }, std::uint32_t { 65U },
        std::uint32_t { 129U },
        std::uint32_t { 256U },
        std::uint32_t { 1024U },
    };
    const auto optimization_suffix
        = std::to_string(static_cast<std::uint8_t>(optimization));

    for (const auto width : widths) {
        auto copy_kernel = make_certified_frontier_kernel(width,
            SignalUpdateDomain::generic, ValueKind::logic9);
        auto copy_executor = make_executor(copy_kernel, optimization,
            "generic-logic9-copy-" + std::to_string(width)
                + "-" + optimization_suffix);
        require(copy_executor != nullptr,
            "Generic whole Logic9 CopyRegister materializes at O0 and O2");
        const auto source = patterned_logic9(width, 1U);
        check_generic_logic9_entry_success(copy_kernel, *copy_executor,
            { { source_signal_id, source } }, source);
        if (width == 1U) {
            for (std::uint8_t code = 0U; code < 9U; ++code) {
                const auto scalar = repeated_logic9(1U, code);
                check_generic_logic9_entry_success(copy_kernel, *copy_executor,
                    { { source_signal_id, scalar } }, scalar);
            }
            for (std::uint8_t code = 9U; code < 16U; ++code) {
                check_generic_logic9_declines_unchanged(copy_kernel,
                    *copy_executor,
                    { { source_signal_id, repeated_logic9(1U, code) } },
                    [](GenericLogic9EntryFrame&) { });
                check_generic_logic9_declines_unchanged(copy_kernel,
                    *copy_executor,
                    { { source_signal_id, repeated_logic9(1U, 1U) } },
                    [=](GenericLogic9EntryFrame& fixture) {
                        const auto site = std::find_if(
                            fixture.layout_ref.write_sites,
                            fixture.layout_ref.write_sites
                                + fixture.layout_ref.write_site_count,
                            [&](const RegionFrontierWriteSiteV2& candidate) {
                                return candidate.member_index
                                    == fixture.producer_index;
                            });
                        require(site != fixture.layout_ref.write_sites
                                + fixture.layout_ref.write_site_count,
                            "the Generic Logic9 output site is present");
                        const auto invalid = repeated_logic9(1U, code);
                        for (std::size_t plane = 0U; plane < 4U; ++plane) {
                            fixture.pending_words[site->pending_slot][plane][0U]
                                = logic9_plane_words(invalid, plane)[0U];
                        }
                    });
            }
        }

        auto not_kernel = make_certified_unary_not_frontier_kernel(width,
            ValueKind::logic9, SignalUpdateDomain::generic);
        auto not_executor = make_executor(not_kernel, optimization,
            "generic-logic9-not-" + std::to_string(width)
                + "-" + optimization_suffix);
        require(not_executor != nullptr,
            "Generic whole Logic9 UnaryNot materializes at O0 and O2");
        check_generic_logic9_entry_success(not_kernel, *not_executor,
            { { source_signal_id, source } },
            patterned_logic9_unary_not_result(source, width));
        if (width == 1U) {
            for (std::uint8_t code = 0U; code < 9U; ++code) {
                const auto scalar = repeated_logic9(1U, code);
                check_generic_logic9_entry_success(not_kernel, *not_executor,
                    { { source_signal_id, scalar } },
                    patterned_logic9_unary_not_result(scalar, 1U));
            }
        }

        const auto rhs = patterned_logic9(width, 5U);
        for (const auto operation : {
                 BinaryOperator::bit_and,
                 BinaryOperator::bit_or,
                 BinaryOperator::bit_xor,
             }) {
            auto binary_kernel = make_certified_binary_frontier_kernel(width,
                operation, ValueKind::logic9, SignalUpdateDomain::generic);
            const auto operation_id
                = std::to_string(static_cast<std::uint8_t>(operation));
            auto binary_executor = make_executor(binary_kernel, optimization,
                "generic-logic9-binary-" + operation_id + "-"
                    + std::to_string(width) + "-" + optimization_suffix);
            require(binary_executor != nullptr,
                "Generic whole Logic9 Binary materializes at O0 and O2");
            check_generic_logic9_entry_success(binary_kernel, *binary_executor,
                { { source_signal_id, source }, { 1U, rhs } },
                patterned_logic9_binary_result(source, rhs, operation, width));
            if (width == 1U) {
                for (std::uint8_t left = 0U; left < 9U; ++left) {
                    for (std::uint8_t right = 0U; right < 9U; ++right) {
                        const auto left_value = repeated_logic9(1U, left);
                        const auto right_value = repeated_logic9(1U, right);
                        check_generic_logic9_entry_success(binary_kernel,
                            *binary_executor,
                            { { source_signal_id, left_value },
                                { 1U, right_value } },
                            repeated_logic9(1U, logic9_binary_result(
                                operation, left, right)));
                    }
                }
            }
        }

        if (width == 65U || width == 129U) {
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                check_generic_logic9_declines_unchanged(copy_kernel,
                    *copy_executor, { { source_signal_id, source } },
                    [=](GenericLogic9EntryFrame& fixture) {
                        const auto slot = fixture.boundary_slot(source_signal_id);
                        fixture.boundary_words[slot][plane].back()
                            |= UINT64_C(1) << (width % 64U);
                    });
            }
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                check_generic_logic9_declines_unchanged(copy_kernel,
                    *copy_executor, { { source_signal_id, source } },
                    [=](GenericLogic9EntryFrame& fixture) {
                        const auto site = std::find_if(
                            fixture.layout_ref.write_sites,
                            fixture.layout_ref.write_sites
                                + fixture.layout_ref.write_site_count,
                            [&](const RegionFrontierWriteSiteV2& candidate) {
                                return candidate.member_index
                                    == fixture.producer_index;
                            });
                        require(site != fixture.layout_ref.write_sites
                                + fixture.layout_ref.write_site_count,
                            "the Generic Logic9 output site is present");
                        fixture.pending_words[site->pending_slot][plane].back()
                            |= UINT64_C(1) << (width % 64U);
                    });
            }
        }
    }
}

[[nodiscard]] RegionFrontierTestValue patterned_binary_result(
    const RegionFrontierTestValue& lhs,
    const RegionFrontierTestValue& rhs,
    const BinaryOperator operation,
    const std::uint32_t width)
{
    RegionFrontierTestValue result {
        std::vector<std::uint64_t>(words_for(width), 0U),
        std::vector<std::uint64_t>(words_for(width), 0U),
    };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        set_logic4_bit(result, bit, binary_result_code(operation,
            get_logic4_bit(lhs, bit), get_logic4_bit(rhs, bit)));
    }
    return result;
}

[[nodiscard]] RegionFrontierTestValue patterned_unary_not_result(
    const RegionFrontierTestValue& source, const std::uint32_t width)
{
    RegionFrontierTestValue result {
        std::vector<std::uint64_t>(words_for(width), 0U),
        std::vector<std::uint64_t>(words_for(width), 0U),
    };
    constexpr std::array<std::uint8_t, 4U> not_codes { 1U, 0U, 2U, 2U };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        set_logic4_bit(result, bit,
            not_codes.at(get_logic4_bit(source, bit)));
    }
    return result;
}

void check_compiled_reductions(const std::uint32_t width,
    const fsim::compiler::JitOptimizationLevel optimization)
{
    const auto count = words_for(width);
    std::vector<std::uint64_t> all_ones(count, 0U);
    std::vector<std::uint64_t> all_unknown_aval(count, 0U);
    std::vector<std::uint64_t> all_unknown_bval(count, 0U);
    std::vector<std::uint64_t> all_z_aval(count, 0U);
    std::vector<std::uint64_t> all_z_bval(count, 0U);
    for (std::size_t word = 0U; word < count; ++word) {
        all_ones[word] = word_mask(width, word);
        all_unknown_aval[word] = word_mask(width, word);
        all_unknown_bval[word] = word_mask(width, word);
        all_z_bval[word] = word_mask(width, word);
    }
    const std::vector<std::uint64_t> known(count, 0U);
    const std::array operations {
        ReductionOperator::bit_and,
        ReductionOperator::bit_or,
        ReductionOperator::bit_xor,
    };
    for (const auto operation : operations) {
        auto kernel = make_certified_reduction_frontier_kernel(width, operation);
        const auto identity = std::string { "frontier-reduction-" }
            + std::to_string(static_cast<std::uint8_t>(operation)) + "-"
            + std::to_string(width) + "-"
            + std::to_string(static_cast<std::uint8_t>(optimization));
        auto executor = make_executor(kernel, optimization, identity);
        require(executor != nullptr,
            "the generated reduction kernel has a verified executable body");
        check_kernel_layout(kernel, width, executor->layout());

        if (operation == ReductionOperator::bit_and) {
            run_reduction_case(width, kernel, *executor,
                all_ones, known, 1U, 0U);
        } else if (operation == ReductionOperator::bit_or) {
            run_reduction_case(width, kernel, *executor,
                known, known, 0U, 0U);
        } else {
            run_reduction_case(width, kernel, *executor,
                known, known, 0U, 0U);
        }

        if (operation == ReductionOperator::bit_and) {
            run_reduction_case(width, kernel, *executor,
                known, known, 0U, 0U);
        }

        run_reduction_case(width, kernel, *executor,
            all_unknown_aval, all_unknown_bval, 1U, 1U);
        run_reduction_case(width, kernel, *executor,
            all_z_aval, all_z_bval, 1U, 1U);

        if (width > 1U && operation == ReductionOperator::bit_and) {
            auto unknown_and_zero = all_ones;
            auto unknown_and_zero_bval = known;
            set_bit(unknown_and_zero, 0U, false);
            set_bit(unknown_and_zero, width - 1U, false);
            set_bit(unknown_and_zero_bval, width - 1U, true);
            run_reduction_case(width, kernel, *executor,
                unknown_and_zero, unknown_and_zero_bval, 0U, 0U);
        }

        if (width > 1U && operation == ReductionOperator::bit_or) {
            auto unknown_or_one = known;
            auto unknown_or_one_bval = known;
            set_bit(unknown_or_one, 0U, true);
            set_bit(unknown_or_one_bval, width - 1U, true);
            run_reduction_case(width, kernel, *executor,
                unknown_or_one, unknown_or_one_bval, 1U, 0U);
        }

        if (width > 1U && operation == ReductionOperator::bit_xor) {
            auto even_parity = known;
            set_bit(even_parity, 0U, true);
            set_bit(even_parity, width - 1U, true);
            run_reduction_case(width, kernel, *executor,
                even_parity, known, 0U, 0U);
        }

        if (operation == ReductionOperator::bit_xor) {
            auto odd_parity = known;
            set_bit(odd_parity, width - 1U, true);
            run_reduction_case(width, kernel, *executor,
                odd_parity, known, 1U, 0U);

            auto unknown_parity_bval = known;
            set_bit(unknown_parity_bval, width - 1U, true);
            run_reduction_case(width, kernel, *executor,
                known, unknown_parity_bval, 1U, 1U);
        }
    }
}

void check_compiled_unary_not(const fsim::compiler::JitOptimizationLevel optimization)
{
    auto kernel = make_certified_unary_not_frontier_kernel(1U);
    const auto identity = std::string { "frontier-unary-not-1-" }
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = make_executor(kernel, optimization, identity);
    require(executor != nullptr,
        "the generated UnaryNot kernel has a verified executable body");
    for (std::uint8_t input = 0U; input < logic4_codes.size(); ++input) {
        constexpr std::array<std::uint8_t, 4U> not_codes { 1U, 0U, 2U, 2U };
        run_operator_case(1U, kernel, *executor,
            { logic4_input(0U, 1U, input) },
            repeated_logic4(1U, not_codes.at(input)));
    }

    constexpr std::uint32_t width = 65U;
    auto wide_kernel = make_certified_unary_not_frontier_kernel(width);
    const auto wide_identity = std::string { "frontier-unary-not-65-" }
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto wide_executor = make_executor(wide_kernel, optimization, wide_identity);
    require(wide_executor != nullptr,
        "the generated wide UnaryNot kernel has a verified executable body");
    const auto source = patterned_logic4(width, 3U);
    const auto expected = patterned_unary_not_result(source, width);
    run_operator_case(width, wide_kernel, *wide_executor,
        { RegionFrontierSignalInput { 0U, source } }, expected);
}

void check_compiled_binary_truth_tables(
    const fsim::compiler::JitOptimizationLevel optimization)
{
    constexpr std::array operations {
        BinaryOperator::bit_and,
        BinaryOperator::bit_or,
        BinaryOperator::bit_xor,
    };
    for (const auto operation : operations) {
        auto kernel = make_certified_binary_frontier_kernel(1U, operation);
        const auto identity = std::string { "frontier-binary-truth-" }
            + std::to_string(static_cast<std::uint8_t>(operation)) + "-"
            + std::to_string(static_cast<std::uint8_t>(optimization));
        auto executor = make_executor(kernel, optimization, identity);
        require(executor != nullptr,
            "the generated bitwise Binary kernel has a verified executable body");
        for (std::uint8_t lhs = 0U; lhs < logic4_codes.size(); ++lhs) {
            for (std::uint8_t rhs = 0U; rhs < logic4_codes.size(); ++rhs) {
                const auto result = binary_result_code(operation, lhs, rhs);
                run_operator_case(1U, kernel, *executor,
                    { logic4_input(0U, 1U, lhs),
                        logic4_input(1U, 1U, rhs) },
                    repeated_logic4(1U, result));
            }
        }

        constexpr std::uint32_t width = 65U;
        auto wide_kernel = make_certified_binary_frontier_kernel(width,
            operation);
        const auto wide_identity = std::string { "frontier-binary-wide-65-" }
            + std::to_string(static_cast<std::uint8_t>(operation)) + "-"
            + std::to_string(static_cast<std::uint8_t>(optimization));
        auto wide_executor = make_executor(wide_kernel, optimization,
            wide_identity);
        require(wide_executor != nullptr,
            "the generated 65-bit Binary kernel has a verified executable body");
        const auto lhs = patterned_logic4(width, 3U);
        const auto rhs = patterned_logic4(width, 5U);
        const auto expected = patterned_binary_result(lhs, rhs, operation, width);
        run_operator_case(width, wide_kernel, *wide_executor,
            { RegionFrontierSignalInput { 0U, lhs },
                RegionFrontierSignalInput { 1U, rhs } }, expected);
    }
}

void check_compiled_logic9_subset(
    const fsim::compiler::JitOptimizationLevel optimization)
{
    for (const std::uint32_t width : {
             std::uint32_t { 1U }, std::uint32_t { 64U },
             std::uint32_t { 65U },
             std::uint32_t { 129U }, std::uint32_t { 256U },
             std::uint32_t { 1024U },
         }) {
        const auto source = patterned_logic9(width, 1U);
        const auto rhs = patterned_logic9(width, 2U);
        const auto check_logic9_layout = [](
            const RegionConeActivationKernel& kernel,
            const RegionFrontierLayoutV2& layout) {
            require(kernel.members.size() == 2U && kernel.outputs.size() == 2U
                    && kernel.internal_signals.size() == 1U
                    && region_frontier_layout_header_valid_v2(layout)
                    && layout.member_count == kernel.members.size()
                    && layout.write_site_count == kernel.outputs.size()
                    && layout.signals != nullptr && layout.write_sites != nullptr,
                "the Logic9 kernel retains a typed two-member output layout");
            for (std::size_t index = 0U; index < layout.signal_slot_count; ++index) {
                const auto& signal = layout.signals[index];
                require(signal.value_kind == RegionFrontierValueKindV2::logic9
                        && signal.plane_count == kRegionFrontierLogic9PlaneCountV2
                        && signal.word_count == words_for(signal.width),
                    "each Logic9 signal retains four planes and exact width geometry");
            }
            for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
                const auto& site = layout.write_sites[index];
                require(site.signal_slot < layout.signal_slot_count
                        && site.value_kind == RegionFrontierValueKindV2::logic9
                        && site.plane_count == kRegionFrontierLogic9PlaneCountV2
                        && site.value_kind == layout.signals[site.signal_slot].value_kind
                        && site.plane_count
                            == layout.signals[site.signal_slot].plane_count,
                    "each Logic9 write site retains all four output planes");
            }
        };

        {
            auto kernel = make_certified_frontier_kernel(width,
                SignalUpdateDomain::systemverilog_active, ValueKind::logic9);
            const auto identity = std::string { "frontier-logic9-copy-" }
                + std::to_string(width) + "-"
                + std::to_string(static_cast<std::uint8_t>(optimization));
            auto executor = make_executor(kernel, optimization, identity);
            require(executor != nullptr,
                "the V2 Logic9 CopyRegister kernel materializes at O0 and O2");
            check_logic9_layout(kernel, executor->layout());
            auto values = operation_values(width,
                { RegionFrontierSignalInput { source_signal_id, source } },
                source);
            if (width == 65U || width == 129U) {
                run_region_frontier_canonical_tail_guard_tests(kernel,
                    executor->step_entry(), executor->layout(), 71U, values);
            }
            run_region_frontier_staging_tests(kernel, executor->step_entry(),
                executor->layout(), 71U, std::move(values));
        }

        {
            auto kernel = make_certified_unary_not_frontier_kernel(
                width, ValueKind::logic9);
            const auto identity = std::string { "frontier-logic9-not-" }
                + std::to_string(width) + "-"
                + std::to_string(static_cast<std::uint8_t>(optimization));
            auto executor = make_executor(kernel, optimization, identity);
            require(executor != nullptr,
                "the V2 Logic9 UnaryNot kernel materializes at O0 and O2");
            check_kernel_layout(kernel, width, executor->layout());
            const auto expected = patterned_logic9_unary_not_result(source, width);
            auto values = operation_values(width,
                { RegionFrontierSignalInput { source_signal_id, source } },
                expected);
            run_region_frontier_staging_tests(kernel, executor->step_entry(),
                executor->layout(), 71U, std::move(values));
        }

        for (const auto operation : {
                 BinaryOperator::bit_and,
                 BinaryOperator::bit_or,
                 BinaryOperator::bit_xor,
             }) {
            auto kernel = make_certified_binary_frontier_kernel(
                width, operation, ValueKind::logic9);
            const auto identity = std::string { "frontier-logic9-binary-" }
                + std::to_string(width) + "-"
                + std::to_string(static_cast<std::uint8_t>(operation)) + "-"
                + std::to_string(static_cast<std::uint8_t>(optimization));
            auto executor = make_executor(kernel, optimization, identity);
            require(executor != nullptr,
                "the V2 Logic9 bitwise Binary kernel materializes at O0 and O2");
            check_logic9_layout(kernel, executor->layout());
            const auto expected = patterned_logic9_binary_result(
                source, rhs, operation, width);
            auto values = operation_values(width,
                { RegionFrontierSignalInput { source_signal_id, source },
                    RegionFrontierSignalInput { 1U, rhs } },
                expected);
            run_region_frontier_staging_tests(kernel, executor->step_entry(),
                executor->layout(), 71U, std::move(values));
        }
    }
}

[[nodiscard]] std::uint8_t conditional_select_result(
    const std::uint8_t condition,
    const std::uint8_t when_true,
    const std::uint8_t when_false) noexcept
{
    if (condition == 0U) {
        return when_false;
    }
    if (condition == 1U) {
        return when_true;
    }
    return when_true == when_false ? when_true : 2U;
}

void check_compiled_conditional_select(
    const fsim::compiler::JitOptimizationLevel optimization)
{
    auto kernel = make_certified_conditional_select_frontier_kernel(1U);
    const auto identity = std::string { "frontier-conditional-select-1-" }
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto executor = make_executor(kernel, optimization, identity);
    require(executor != nullptr,
        "the generated ConditionalSelect kernel has a verified executable body");
    for (std::uint8_t condition = 0U;
         condition < logic4_codes.size(); ++condition) {
        for (std::uint8_t when_true = 0U;
             when_true < logic4_codes.size(); ++when_true) {
            for (std::uint8_t when_false = 0U;
                 when_false < logic4_codes.size(); ++when_false) {
                const auto result = conditional_select_result(
                    condition, when_true, when_false);
                run_operator_case(1U, kernel, *executor,
                    { logic4_input(0U, 1U, condition),
                        logic4_input(1U, 1U, when_true),
                        logic4_input(2U, 1U, when_false) },
                    repeated_logic4(1U, result));
            }
        }
    }

    constexpr std::uint32_t width = 65U;
    auto wide_kernel = make_certified_conditional_select_frontier_kernel(width);
    const auto wide_identity = std::string { "frontier-conditional-select-65-" }
        + std::to_string(static_cast<std::uint8_t>(optimization));
    auto wide_executor = make_executor(wide_kernel, optimization, wide_identity);
    require(wide_executor != nullptr,
        "the generated 65-bit ConditionalSelect kernel has a verified executable body");
    const auto z_arm = repeated_logic4(width, 3U);
    run_operator_case(width, wide_kernel, *wide_executor,
        { logic4_input(0U, 1U, 2U),
            RegionFrontierSignalInput { 1U, z_arm },
            RegionFrontierSignalInput { 2U, z_arm } }, z_arm);
    run_operator_case(width, wide_kernel, *wide_executor,
        { logic4_input(0U, 1U, 3U),
            logic4_input(1U, width, 1U),
            logic4_input(2U, width, 0U) },
        repeated_logic4(width, 2U));
}

} // namespace

int main()
{
    try {
        check_generic_ineligible_shapes();
        fsim::compiler::test::run_region_frontier_preparation_tests();
        run_region_frontier_cache_tests();
        run_region_frontier_shared_body_cache_tests();
        run_region_frontier_shared_identity_tests();
        // Adapter-dependent until the runtime issue helper consumes the V2
        // typed frame; retain this real scheduler-issue gate across migration.
        run_region_frontier_issue_helper_tests();
        run_region_frontier_logic9_issue_helper_tests();
        run_region_frontier_logic9_word_lowering_tests();
        run_region_frontier_known_logic4_tests();
        run_region_frontier_extract_tests();
        run_region_frontier_concatenate_tests();
        run_region_frontier_event_capacity_tests();
        run_region_frontier_shift_tests();
        check_remapped_frontier_binding(
            SignalUpdateDomain::systemverilog_active);
        check_remapped_frontier_binding(SignalUpdateDomain::generic);
        for (const auto optimization : {
                 fsim::compiler::JitOptimizationLevel::o0,
                 fsim::compiler::JitOptimizationLevel::o2,
             }) {
            check_generic_frontier_entry(optimization);
            check_generic_permuted_pending_slots(optimization);
            check_generic_logic9_entries(optimization);
            check_compiled_unary_not(optimization);
            check_compiled_binary_truth_tables(optimization);
            check_compiled_logic9_subset(optimization);
            check_compiled_conditional_select(optimization);
        }
        constexpr std::array widths {
            std::uint32_t { 1U }, std::uint32_t { 64U },
            std::uint32_t { 65U }, std::uint32_t { 129U },
            std::uint32_t { 256U },
            std::uint32_t { 1024U },
        };
        for (const auto width : widths) {
            for (const auto optimization : {
                     fsim::compiler::JitOptimizationLevel::o0,
                     fsim::compiler::JitOptimizationLevel::o2,
                 }) {
                check_compiled_copy(width, optimization);
                check_compiled_reductions(width, optimization);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "LLVM native region frontier test failure: "
                  << error.what() << '\n';
        return 1;
    }
}

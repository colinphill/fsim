// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_context.hpp"
#include "llvm_jit_coverage.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/ErrorHandling.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string>
#include <type_traits>
#include <variant>

namespace fsim::compiler::llvm_detail {
using namespace runtime::simir;

void lower_process_operations(ProcessLoweringContext& state)
{
    [[maybe_unused]] auto&& module = state.module;
    [[maybe_unused]] auto&& symbol = state.symbol;
    [[maybe_unused]] auto&& process = state.process;
    [[maybe_unused]] auto&& signal_widths = state.signal_widths;
    [[maybe_unused]] auto&& signal_value_kinds = state.signal_value_kinds;
    [[maybe_unused]] auto&& direct_read_signals = state.direct_read_signals;
    [[maybe_unused]] auto&& direct_update_signals = state.direct_update_signals;
    [[maybe_unused]] auto&& validated = state.validated;
    [[maybe_unused]] auto&& debug_instrumentation = state.debug_instrumentation;
    [[maybe_unused]] auto&& require_direct_update_slots = state.require_direct_update_slots;
    [[maybe_unused]] auto&& context = state.context;
    [[maybe_unused]] auto&& i32 = state.i32;
    [[maybe_unused]] auto&& i64 = state.i64;
    [[maybe_unused]] auto&& pointer = state.pointer;
    [[maybe_unused]] auto&& runtime_type = state.runtime_type;
    [[maybe_unused]] auto&& direct_update_slot_type = state.direct_update_slot_type;
    [[maybe_unused]] auto&& frame_type = state.frame_type;
    [[maybe_unused]] auto&& function = state.function;
    [[maybe_unused]] auto&& builder = state.builder;
    [[maybe_unused]] auto&& runtime_argument = state.runtime_argument;
    [[maybe_unused]] auto&& frame_argument = state.frame_argument;
    [[maybe_unused]] auto&& context_pointer = state.context_pointer;
    [[maybe_unused]] auto&& read_callback = state.read_callback;
    [[maybe_unused]] auto&& write_callback = state.write_callback;
    [[maybe_unused]] auto&& assert_callback = state.assert_callback;
    [[maybe_unused]] auto&& code_coverage_hit_counters = state.code_coverage_hit_counters;
    [[maybe_unused]] auto&& code_coverage_counter_values = state.code_coverage_counter_values;
    [[maybe_unused]] auto&& code_coverage_hit_count = state.code_coverage_hit_count;
    [[maybe_unused]] auto&& code_coverage_counter_count = state.code_coverage_counter_count;
    [[maybe_unused]] auto&& record_code_coverage_counter = state.record_code_coverage_counter;
    [[maybe_unused]] auto&& record_code_coverage_counter_type = state.record_code_coverage_counter_type;
    [[maybe_unused]] auto&& direct_update_slots = state.direct_update_slots;
    [[maybe_unused]] auto&& direct_update_active_words = state.direct_update_active_words;
    [[maybe_unused]] auto&& static_trigger_mask = state.static_trigger_mask;
    [[maybe_unused]] auto&& direct_signal_aval = state.direct_signal_aval;
    [[maybe_unused]] auto&& direct_signal_bval = state.direct_signal_bval;
    [[maybe_unused]] auto&& direct_signal_logic9_plane0 = state.direct_signal_logic9_plane0;
    [[maybe_unused]] auto&& direct_signal_logic9_plane1 = state.direct_signal_logic9_plane1;
    [[maybe_unused]] auto&& direct_signal_logic9_plane2 = state.direct_signal_logic9_plane2;
    [[maybe_unused]] auto&& direct_signal_logic9_plane3 = state.direct_signal_logic9_plane3;
    [[maybe_unused]] auto&& direct_read_signal_map = state.direct_read_signal_map;
    [[maybe_unused]] auto&& direct_read_signal_count = state.direct_read_signal_count;
    [[maybe_unused]] auto&& direct_signal_count = state.direct_signal_count;
    [[maybe_unused]] auto&& direct_wide_signal_aval = state.direct_wide_signal_aval;
    [[maybe_unused]] auto&& direct_wide_signal_bval = state.direct_wide_signal_bval;
    [[maybe_unused]] auto&& direct_wide_signal_logic9_plane2 = state.direct_wide_signal_logic9_plane2;
    [[maybe_unused]] auto&& direct_wide_signal_logic9_plane3 = state.direct_wide_signal_logic9_plane3;
    [[maybe_unused]] auto&& direct_wide_signal_offsets = state.direct_wide_signal_offsets;
    [[maybe_unused]] auto&& direct_wide_signal_offset_count = state.direct_wide_signal_offset_count;
    [[maybe_unused]] auto&& direct_wide_word_count = state.direct_wide_word_count;
    [[maybe_unused]] auto&& write_update_callback = state.write_update_callback;
    [[maybe_unused]] auto&& write_after_callback = state.write_after_callback;
    [[maybe_unused]] auto&& write_blocking_slice_callback = state.write_blocking_slice_callback;
    [[maybe_unused]] auto&& write_update_slice_callback = state.write_update_slice_callback;
    [[maybe_unused]] auto&& write_after_slice_callback = state.write_after_slice_callback;
    [[maybe_unused]] auto&& force_signal_slice_callback = state.force_signal_slice_callback;
    [[maybe_unused]] auto&& force_signal_slice_logic9_callback = state.force_signal_slice_logic9_callback;
    [[maybe_unused]] auto&& release_signal_slice_callback = state.release_signal_slice_callback;
    [[maybe_unused]] auto&& force_driver_signal_slice_callback = state.force_driver_signal_slice_callback;
    [[maybe_unused]] auto&& force_driver_signal_slice_logic9_callback = state.force_driver_signal_slice_logic9_callback;
    [[maybe_unused]] auto&& release_driver_signal_slice_callback = state.release_driver_signal_slice_callback;
    [[maybe_unused]] auto&& runtime_flags = state.runtime_flags;
    [[maybe_unused]] auto&& signal_event_callback = state.signal_event_callback;
    [[maybe_unused]] auto&& signal_last_value_callback = state.signal_last_value_callback;
    [[maybe_unused]] auto&& signal_last_event_callback = state.signal_last_event_callback;
    [[maybe_unused]] auto&& signal_active_callback = state.signal_active_callback;
    [[maybe_unused]] auto&& signal_last_active_callback = state.signal_last_active_callback;
    [[maybe_unused]] auto&& signal_driving_callback = state.signal_driving_callback;
    [[maybe_unused]] auto&& signal_driving_value_callback = state.signal_driving_value_callback;
    [[maybe_unused]] auto&& signal_driving_value_logic9_callback = state.signal_driving_value_logic9_callback;
    [[maybe_unused]] auto&& read_simulation_time_callback = state.read_simulation_time_callback;
    [[maybe_unused]] auto&& vital_timing_check_callback = state.vital_timing_check_callback;
    [[maybe_unused]] auto&& vital_delay_callback = state.vital_delay_callback;
    [[maybe_unused]] auto&& output_callback = state.output_callback;
    [[maybe_unused]] auto&& postponed_output_callback = state.postponed_output_callback;
    [[maybe_unused]] auto&& report_callback = state.report_callback;
    [[maybe_unused]] auto&& formatted_output_callback = state.formatted_output_callback;
    [[maybe_unused]] auto&& time_output_callback = state.time_output_callback;
    [[maybe_unused]] auto&& monitor_install_callback = state.monitor_install_callback;
    [[maybe_unused]] auto&& monitor_control_callback = state.monitor_control_callback;
    [[maybe_unused]] auto&& random_value_callback = state.random_value_callback;
    [[maybe_unused]] auto&& write_inertial_callback = state.write_inertial_callback;
    [[maybe_unused]] auto&& write_inertial_slice_callback = state.write_inertial_slice_callback;
    [[maybe_unused]] auto&& exact_signal_callback = state.exact_signal_callback;
    [[maybe_unused]] auto&& read_signal_packed_callback = state.read_signal_packed_callback;
    [[maybe_unused]] auto&& write_signal_packed_callback = state.write_signal_packed_callback;
    [[maybe_unused]] auto&& read_signal_dynamic_part_callback = state.read_signal_dynamic_part_callback;
    [[maybe_unused]] auto&& write_projected_callback = state.write_projected_callback;
    [[maybe_unused]] auto&& write_projected_slice_callback = state.write_projected_slice_callback;
    [[maybe_unused]] auto&& write_projected_waveform_callback = state.write_projected_waveform_callback;
    [[maybe_unused]] auto&& write_projected_waveform_slice_callback = state.write_projected_waveform_slice_callback;
    [[maybe_unused]] auto&& read_logic9_callback = state.read_logic9_callback;
    [[maybe_unused]] auto&& write_logic9_callback = state.write_logic9_callback;
    [[maybe_unused]] auto&& write_update_logic9_callback = state.write_update_logic9_callback;
    [[maybe_unused]] auto&& write_after_logic9_callback = state.write_after_logic9_callback;
    [[maybe_unused]] auto&& write_blocking_slice_logic9_callback = state.write_blocking_slice_logic9_callback;
    [[maybe_unused]] auto&& write_update_slice_logic9_callback = state.write_update_slice_logic9_callback;
    [[maybe_unused]] auto&& write_after_slice_logic9_callback = state.write_after_slice_logic9_callback;
    [[maybe_unused]] auto&& signal_last_value_logic9_callback = state.signal_last_value_logic9_callback;
    [[maybe_unused]] auto&& write_inertial_logic9_callback = state.write_inertial_logic9_callback;
    [[maybe_unused]] auto&& write_inertial_slice_logic9_callback = state.write_inertial_slice_logic9_callback;
    [[maybe_unused]] auto&& write_projected_logic9_callback = state.write_projected_logic9_callback;
    [[maybe_unused]] auto&& write_projected_slice_logic9_callback = state.write_projected_slice_logic9_callback;
    [[maybe_unused]] auto&& write_projected_waveform_logic9_callback = state.write_projected_waveform_logic9_callback;
    [[maybe_unused]] auto&& write_projected_waveform_slice_logic9_callback = state.write_projected_waveform_slice_logic9_callback;
    [[maybe_unused]] auto&& write_formatted_logic9_callback = state.write_formatted_logic9_callback;
    [[maybe_unused]] auto&& read_type = state.read_type;
    [[maybe_unused]] auto&& write_type = state.write_type;
    [[maybe_unused]] auto&& assert_type = state.assert_type;
    [[maybe_unused]] auto&& write_after_type = state.write_after_type;
    [[maybe_unused]] auto&& write_slice_type = state.write_slice_type;
    [[maybe_unused]] auto&& write_after_slice_type = state.write_after_slice_type;
    [[maybe_unused]] auto&& release_slice_type = state.release_slice_type;
    [[maybe_unused]] auto&& write_inertial_type = state.write_inertial_type;
    [[maybe_unused]] auto&& write_inertial_slice_type = state.write_inertial_slice_type;
    [[maybe_unused]] auto&& exact_signal_type = state.exact_signal_type;
    [[maybe_unused]] auto&& read_signal_packed_type = state.read_signal_packed_type;
    [[maybe_unused]] auto&& read_signal_dynamic_part_type = state.read_signal_dynamic_part_type;
    [[maybe_unused]] auto&& write_signal_packed_type = state.write_signal_packed_type;
    [[maybe_unused]] auto&& write_projected_type = state.write_projected_type;
    [[maybe_unused]] auto&& write_projected_slice_type = state.write_projected_slice_type;
    [[maybe_unused]] auto&& projected_element_type = state.projected_element_type;
    [[maybe_unused]] auto&& write_projected_waveform_type = state.write_projected_waveform_type;
    [[maybe_unused]] auto&& write_projected_waveform_slice_type = state.write_projected_waveform_slice_type;
    [[maybe_unused]] auto&& signal_event_type = state.signal_event_type;
    [[maybe_unused]] auto&& signal_last_value_type = state.signal_last_value_type;
    [[maybe_unused]] auto&& signal_last_event_type = state.signal_last_event_type;
    [[maybe_unused]] auto&& signal_active_type = state.signal_active_type;
    [[maybe_unused]] auto&& signal_last_active_type = state.signal_last_active_type;
    [[maybe_unused]] auto&& signal_driving_type = state.signal_driving_type;
    [[maybe_unused]] auto&& signal_driving_value_type = state.signal_driving_value_type;
    [[maybe_unused]] auto&& read_simulation_time_type = state.read_simulation_time_type;
    [[maybe_unused]] auto&& vital_timing_check_type = state.vital_timing_check_type;
    [[maybe_unused]] auto&& vital_delay_type = state.vital_delay_type;
    [[maybe_unused]] auto&& output_type = state.output_type;
    [[maybe_unused]] auto&& report_type = state.report_type;
    [[maybe_unused]] auto&& formatted_output_type = state.formatted_output_type;
    [[maybe_unused]] auto&& time_output_type = state.time_output_type;
    [[maybe_unused]] auto&& random_value_type = state.random_value_type;
    [[maybe_unused]] auto&& read_logic9_type = state.read_logic9_type;
    [[maybe_unused]] auto&& write_logic9_type = state.write_logic9_type;
    [[maybe_unused]] auto&& write_after_logic9_type = state.write_after_logic9_type;
    [[maybe_unused]] auto&& write_slice_logic9_type = state.write_slice_logic9_type;
    [[maybe_unused]] auto&& write_after_slice_logic9_type = state.write_after_slice_logic9_type;
    [[maybe_unused]] auto&& write_inertial_logic9_type = state.write_inertial_logic9_type;
    [[maybe_unused]] auto&& write_inertial_slice_logic9_type = state.write_inertial_slice_logic9_type;
    [[maybe_unused]] auto&& write_projected_logic9_type = state.write_projected_logic9_type;
    [[maybe_unused]] auto&& write_projected_slice_logic9_type = state.write_projected_slice_logic9_type;
    [[maybe_unused]] auto&& logic9_projected_element_type = state.logic9_projected_element_type;
    [[maybe_unused]] auto&& write_projected_waveform_logic9_type = state.write_projected_waveform_logic9_type;
    [[maybe_unused]] auto&& write_projected_waveform_slice_logic9_type = state.write_projected_waveform_slice_logic9_type;
    [[maybe_unused]] auto&& formatted_output_logic9_type = state.formatted_output_logic9_type;
    [[maybe_unused]] auto&& native_callables = state.native_callables;
    [[maybe_unused]] auto&& lowering_plan = state.lowering_plan;
    [[maybe_unused]] auto&& register_aval = state.register_aval;
    [[maybe_unused]] auto&& register_bval = state.register_bval;
    [[maybe_unused]] auto&& register_initialized = state.register_initialized;
    [[maybe_unused]] auto&& i8 = state.i8;
    [[maybe_unused]] auto&& registers = state.registers;
    [[maybe_unused]] auto&& frame_registers = state.frame_registers;
    [[maybe_unused]] auto&& read_bval_slot = state.read_bval_slot;
    [[maybe_unused]] auto&& logic9_word_slot = state.logic9_word_slot;
    [[maybe_unused]] auto&& container_result_aval_slot = state.container_result_aval_slot;
    [[maybe_unused]] auto&& container_result_bval_slot = state.container_result_bval_slot;
    [[maybe_unused]] auto&& store_logic9_word = state.store_logic9_word;
    [[maybe_unused]] auto&& load_logic9_word = state.load_logic9_word;
    [[maybe_unused]] auto&& return_result = state.return_result;
    [[maybe_unused]] auto&& elided_operations = state.elided_operations;
    [[maybe_unused]] auto&& fused_affine_dynamic_extracts = state.fused_affine_dynamic_extracts;
    [[maybe_unused]] auto&& constant_part_select_sources = state.constant_part_select_sources;
    [[maybe_unused]] auto&& dynamic_part_signal_sources = state.dynamic_part_signal_sources;
    [[maybe_unused]] auto&& fused_container_object_reads = state.fused_container_object_reads;
    [[maybe_unused]] auto&& instruction_blocks = state.instruction_blocks;
    [[maybe_unused]] auto&& instruction_regions = state.instruction_regions;
    [[maybe_unused]] auto&& static_trigger_region_entries = state.static_trigger_region_entries;
    [[maybe_unused]] auto&& ssa_callable_returns = state.ssa_callable_returns;
    [[maybe_unused]] auto&& invalid_pc = state.invalid_pc;
    auto return_targets = static_return_targets(process);
    std::erase_if(return_targets, [&](const InstructionIndex target) {
        return target >= lowering_plan.operations.size()
            || !lowering_plan.operations[target];
    });
    auto native_return_targets = native_callables.return_targets;
    std::erase_if(native_return_targets, [&](const InstructionIndex target) {
        return target >= lowering_plan.operations.size()
            || !lowering_plan.operations[target];
    });
    std::vector<std::uint32_t> code_coverage_hit_slots;
    std::uint32_t code_coverage_hit_count_value { };
    if (validated.uses_code_coverage) {
        code_coverage_hit_slots.assign(
            process.operations.size(),
            std::numeric_limits<std::uint32_t>::max());
        for (std::size_t index = 0;
             index < process.operations.size(); ++index) {
            if (fsim::runtime::simir::operation_holds<CodeCoverageHit>(
                    process.operations[index])) {
                code_coverage_hit_slots[index]
                    = code_coverage_hit_count_value++;
            }
        }
    }
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (!lowering_plan.operations[index]
            || elided_operations[index]) {
            continue;
        }
        const auto instruction = static_cast<InstructionIndex>(index);
        const auto next_instruction = static_cast<InstructionIndex>(index + 1U);
        builder.SetInsertPoint(instruction_blocks[index]);
        if (const auto* trigger_region
            = static_trigger_region_entries[index]) {
            auto* execute_region = llvm::BasicBlock::Create(
                context,
                "static.trigger.execute." + std::to_string(index),
                function);
            const auto accepted_mask = trigger_region->mask
                | Process::full_static_trigger_mask;
            builder.CreateCondBr(
                builder.CreateICmpNE(
                    builder.CreateAnd(
                        static_trigger_mask,
                        constant_i64(context, accepted_mask)),
                    constant_i64(context, 0U)),
                execute_region,
                instruction_blocks[trigger_region->end]);
            builder.SetInsertPoint(execute_region);
        }
        auto* const last_block_before_lowering = &function->back();
        const auto branch_to_next = [&] {
            builder.CreateBr(instruction_blocks[index + 1]);
        };
        const auto runtime_error_if =
            [&](llvm::Value* condition,
                const JitGeneratedRuntimeErrorReason reason,
                const std::string_view label) {
                auto* error_block = llvm::BasicBlock::Create(
                    context,
                    std::string { label } + ".error."
                        + std::to_string(index),
                    function);
                auto* continue_block = llvm::BasicBlock::Create(
                    context,
                    std::string { label } + ".continue."
                        + std::to_string(index),
                    function);
                builder.CreateCondBr(
                    condition, error_block, continue_block);
                builder.SetInsertPoint(error_block);
                return_result(
                    FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR,
                    instruction,
                    static_cast<std::uint64_t>(reason),
                    FSIM_JIT_FRAME_STATE_RUNTIME_ERROR,
                    static_cast<std::uint32_t>(reason));
                builder.SetInsertPoint(continue_block);
            };
        const auto synchronize_uses_to_frame = [&] {
            for (const auto register_id :
                validated.instruction_uses[index]) {
                const auto& source = registers[register_id];
                const auto& destination = frame_registers[register_id];
                if (source.aval_base == destination.aval_base
                    && source.word_offset == destination.word_offset) {
                    continue;
                }
                store_register(
                    builder,
                    frame_registers,
                    register_id,
                    load_register(builder, registers, register_id));
            }
        };
        const auto execute_exact_signal = [&] {
            synchronize_uses_to_frame();
            auto* status = builder.CreateCall(
                exact_signal_type,
                exact_signal_callback,
                { context_pointer,
                    llvm::ConstantInt::get(i32, process.id),
                    llvm::ConstantInt::get(i32, instruction),
                    frame_argument });
            runtime_error_if(
                builder.CreateICmpNE(
                    status, llvm::ConstantInt::get(i32, 0)),
                JitGeneratedRuntimeErrorReason::signal_callback_failure,
                "signal.operation");
            for (const auto register_id :
                validated.instruction_definitions[index]) {
                const auto& source = frame_registers[register_id];
                const auto& destination = registers[register_id];
                if (source.aval_base == destination.aval_base
                    && source.word_offset == destination.word_offset) {
                    continue;
                }
                store_register(
                    builder,
                    registers,
                    register_id,
                    load_register(builder, frame_registers, register_id));
            }
            branch_to_next();
        };
        const auto read_wide_signal = [&](const ReadSignal& operation) {
            const auto& slot = registers[operation.destination];
            auto* aval = builder.CreateGEP(
                i64,
                slot.aval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.aval");
            auto* bval = builder.CreateGEP(
                i64,
                slot.bval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.bval");
            auto* const null_pointer = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(pointer));
            llvm::Value* plane2 = null_pointer;
            llvm::Value* plane3 = null_pointer;
            if (slot.kind == ValueKind::logic9) {
                plane2 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane2_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.logic9.plane2");
                plane3 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane3_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.logic9.plane3");
            }
            const auto direct = std::ranges::find(
                direct_read_signals, operation.signal);
            const auto direct_kind_compatible
                = operation.signal < signal_value_kinds.size()
                ? signal_value_kinds[operation.signal] == slot.kind
                : slot.kind == ValueKind::logic4;
            if (direct_kind_compatible
                && direct != direct_read_signals.end()
                && direct_wide_signal_aval != nullptr
                && direct_wide_signal_bval != nullptr
                && direct_wide_signal_offsets != nullptr
                && direct_wide_signal_offset_count != nullptr
                && direct_wide_word_count != nullptr
                && direct_read_signal_map != nullptr
                && direct_read_signal_count != nullptr) {
                auto* current_function = builder.GetInsertBlock()->getParent();
                auto* map_block = llvm::BasicBlock::Create(
                    context, "wide.signal.direct.map", current_function);
                auto* bounds_block = llvm::BasicBlock::Create(
                    context, "wide.signal.direct.bounds", current_function);
                auto* direct_block = llvm::BasicBlock::Create(
                    context, "wide.signal.direct", current_function);
                auto* callback_block = llvm::BasicBlock::Create(
                    context, "wide.signal.callback", current_function);
                auto* merge_block = llvm::BasicBlock::Create(
                    context, "wide.signal.merge", current_function);
                const auto nonnull = [&](llvm::Value* value) {
                    return builder.CreateICmpNE(
                        value,
                        llvm::ConstantPointerNull::get(
                            llvm::cast<llvm::PointerType>(value->getType())));
                };
                const auto direct_slot = static_cast<std::uint32_t>(
                    std::distance(direct_read_signals.begin(), direct));
                auto* pointers_available = builder.CreateAnd(
                    builder.CreateAnd(
                        builder.CreateAnd(
                            nonnull(direct_wide_signal_aval),
                            nonnull(direct_wide_signal_bval)),
                        nonnull(direct_wide_signal_offsets)),
                    nonnull(direct_read_signal_map));
                if (slot.kind == ValueKind::logic9) {
                    pointers_available = builder.CreateAnd(
                        pointers_available,
                        builder.CreateAnd(
                            nonnull(direct_wide_signal_logic9_plane2),
                            nonnull(direct_wide_signal_logic9_plane3)));
                }
                builder.CreateCondBr(
                    builder.CreateAnd(
                        pointers_available,
                        builder.CreateICmpULT(
                            llvm::ConstantInt::get(i32, direct_slot),
                            direct_read_signal_count)),
                    map_block,
                    callback_block);

                builder.SetInsertPoint(map_block);
                auto* actual = builder.CreateLoad(
                    i32,
                    builder.CreateInBoundsGEP(
                        i32,
                        direct_read_signal_map,
                        llvm::ConstantInt::get(i32, direct_slot)),
                    "wide.signal.actual");
                builder.CreateCondBr(
                    builder.CreateICmpULT(
                        actual, direct_wide_signal_offset_count),
                    bounds_block,
                    callback_block);

                builder.SetInsertPoint(bounds_block);
                auto* word_offset = builder.CreateLoad(
                    i32,
                    builder.CreateInBoundsGEP(
                        i32, direct_wide_signal_offsets, actual),
                    "wide.signal.word_offset");
                const auto words = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(slot.width) + 63U) / 64U);
                auto* available = builder.CreateICmpULE(
                    word_offset, direct_wide_word_count);
                auto* remaining = builder.CreateSub(
                    direct_wide_word_count, word_offset);
                auto* enough = builder.CreateICmpUGE(
                    remaining, llvm::ConstantInt::get(i32, words));
                builder.CreateCondBr(
                    builder.CreateAnd(available, enough),
                    direct_block,
                    callback_block);

                builder.SetInsertPoint(direct_block);
                auto* wide_aval = builder.CreateInBoundsGEP(
                    i64, direct_wide_signal_aval, word_offset);
                auto* wide_bval = builder.CreateInBoundsGEP(
                    i64, direct_wide_signal_bval, word_offset);
                const auto bytes = static_cast<std::uint64_t>(words) * 8U;
                builder.CreateMemCpy(
                    aval, llvm::Align(8),
                    wide_aval, llvm::Align(8), bytes);
                builder.CreateMemCpy(
                    bval, llvm::Align(8),
                    wide_bval, llvm::Align(8), bytes);
                if (slot.kind == ValueKind::logic9) {
                    auto* wide_plane2 = builder.CreateInBoundsGEP(
                        i64,
                        direct_wide_signal_logic9_plane2,
                        word_offset);
                    auto* wide_plane3 = builder.CreateInBoundsGEP(
                        i64,
                        direct_wide_signal_logic9_plane3,
                        word_offset);
                    builder.CreateMemCpy(
                        plane2, llvm::Align(8),
                        wide_plane2, llvm::Align(8), bytes);
                    builder.CreateMemCpy(
                        plane3, llvm::Align(8),
                        wide_plane3, llvm::Align(8), bytes);
                }
                builder.CreateBr(merge_block);

                builder.SetInsertPoint(callback_block);
                auto* status = builder.CreateCall(
                    read_signal_packed_type,
                    read_signal_packed_callback,
                    { context_pointer,
                        llvm::ConstantInt::get(i32, operation.signal),
                        llvm::ConstantInt::get(i32, slot.width),
                        aval,
                        bval,
                        plane2,
                        plane3 });
                runtime_error_if(
                    builder.CreateICmpNE(
                        status, llvm::ConstantInt::get(i32, 0)),
                    JitGeneratedRuntimeErrorReason::signal_callback_failure,
                    "wide.signal.read");
                builder.CreateBr(merge_block);
                builder.SetInsertPoint(merge_block);
            } else {
                auto* status = builder.CreateCall(
                    read_signal_packed_type,
                    read_signal_packed_callback,
                    { context_pointer,
                        llvm::ConstantInt::get(i32, operation.signal),
                        llvm::ConstantInt::get(i32, slot.width),
                        aval,
                        bval,
                        plane2,
                        plane3 });
                runtime_error_if(
                    builder.CreateICmpNE(
                        status, llvm::ConstantInt::get(i32, 0)),
                    JitGeneratedRuntimeErrorReason::signal_callback_failure,
                    "wide.signal.read");
            }
            if (slot.initialized_base != nullptr) {
                auto* initialized = builder.CreateGEP(
                    i8,
                    slot.initialized_base,
                    llvm::ConstantInt::get(i32, slot.index),
                    "wide.signal.initialized");
                builder.CreateStore(
                    llvm::ConstantInt::get(i8, 1), initialized);
            }
            branch_to_next();
        };
        const auto write_wide_signal = [&](const std::uint32_t signal,
                                           const RegisterId source,
                                           const std::uint32_t offset,
                                           const std::uint32_t mode,
                                           const runtime::SimulationTick delay) {
            const auto& slot = registers[source];
            auto* aval = builder.CreateGEP(
                i64,
                slot.aval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.write.aval");
            auto* bval = builder.CreateGEP(
                i64,
                slot.bval_base,
                constant_i64(context, slot.word_offset),
                "wide.signal.write.bval");
            auto* const null_pointer = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(pointer));
            llvm::Value* plane2 = null_pointer;
            llvm::Value* plane3 = null_pointer;
            if (slot.kind == ValueKind::logic9) {
                plane2 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane2_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.write.logic9.plane2");
                plane3 = builder.CreateGEP(
                    i64,
                    slot.logic9_plane3_base,
                    constant_i64(context, slot.word_offset),
                    "wide.signal.write.logic9.plane3");
            }
            auto* status = builder.CreateCall(
                write_signal_packed_type,
                write_signal_packed_callback,
                { context_pointer,
                    llvm::ConstantInt::get(i32, signal),
                    llvm::ConstantInt::get(i32, offset),
                    llvm::ConstantInt::get(i32, slot.width),
                    llvm::ConstantInt::get(i32, mode),
                    constant_i64(context, delay),
                    aval,
                    bval,
                    plane2,
                    plane3 });
            runtime_error_if(
                builder.CreateICmpNE(
                    status, llvm::ConstantInt::get(i32, 0)),
                JitGeneratedRuntimeErrorReason::signal_callback_failure,
                "wide.signal.write");
            branch_to_next();
        };
        const auto dynamic_offset =
            [&](const DynamicIndex& selection) -> llvm::Value* {
            const auto selected = coerce_value_kind(
                builder,
                load_register(
                    builder, registers, selection.index),
                ValueKind::logic4);
            runtime_error_if(
                builder.CreateICmpNE(
                    builder.CreateAnd(
                        selected.bval,
                        constant_i64(
                            context,
                            std::numeric_limits<std::uint32_t>::max())),
                    constant_i64(context, 0)),
                JitGeneratedRuntimeErrorReason::dynamic_index_unknown,
                "dynamic.index.unknown");
            auto* signed_index = builder.CreateSExt(
                builder.CreateTrunc(selected.aval, i32), i64);
            auto* left = llvm::ConstantInt::getSigned(
                i64, selection.left);
            auto* right = llvm::ConstantInt::getSigned(
                i64, selection.right);
            auto* lower = selection.left <= selection.right ? left : right;
            auto* upper = selection.left <= selection.right ? right : left;
            runtime_error_if(
                builder.CreateOr(
                    builder.CreateICmpSLT(signed_index, lower),
                    builder.CreateICmpSGT(signed_index, upper)),
                JitGeneratedRuntimeErrorReason::dynamic_index_range,
                "dynamic.index.range");
            auto* distance = builder.CreateSelect(
                builder.CreateICmpSGE(signed_index, right),
                builder.CreateSub(signed_index, right),
                builder.CreateSub(right, signed_index));
            return builder.CreateAdd(
                distance,
                constant_i64(context, selection.base_offset),
                "dynamic.index.offset");
        };
        const auto dynamic_offset_i32 =
            [&](const DynamicIndex& selection) {
                return builder.CreateTrunc(
                    dynamic_offset(selection), i32);
            };
        const auto emit_dynamic_slice =
            [&](const std::uint32_t signal,
                const RegisterId source_register,
                llvm::Value* offset,
                llvm::Value* logic4_callback,
                llvm::Value* logic9_callback) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, source_register),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_slice_logic9_type,
                        logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot });
                } else {
                    builder.CreateCall(
                        write_slice_type,
                        logic4_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval });
                }
                branch_to_next();
            };
        const auto emit_dynamic_part_slice =
            [&](const std::uint32_t signal,
                const RegisterId source_register,
                const DynamicPartIndex& selection,
                llvm::Value* logic4_callback,
                llvm::Value* logic9_callback,
                const std::optional<runtime::SimulationTick> delay) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[signal];
                const auto write = lower_dynamic_part_write(
                    builder,
                    context,
                    i32,
                    i64,
                    registers,
                    source_register,
                    selection,
                    signal_kind);
                auto* write_block = llvm::BasicBlock::Create(
                    context,
                    "dynamic.part.write." + std::to_string(index),
                    function);
                builder.CreateCondBr(
                    builder.CreateICmpNE(
                        write.width, llvm::ConstantInt::get(i32, 0)),
                    write_block,
                    instruction_blocks[index + 1]);
                builder.SetInsertPoint(write_block);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, write.value);
                    if (delay) {
                        builder.CreateCall(
                            write_after_slice_logic9_type,
                            logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(i32, signal),
                                write.offset,
                                write.width,
                                logic9_word_slot,
                                constant_i64(context, *delay) });
                    } else {
                        builder.CreateCall(
                            write_slice_logic9_type,
                            logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(i32, signal),
                                write.offset,
                                write.width,
                                logic9_word_slot });
                    }
                } else if (delay) {
                    builder.CreateCall(
                        write_after_slice_type,
                        logic4_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            write.offset,
                            write.width,
                            write.value.aval,
                            write.value.bval,
                            constant_i64(context, *delay) });
                } else {
                    builder.CreateCall(
                        write_slice_type,
                        logic4_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, signal),
                            write.offset,
                            write.width,
                            write.value.aval,
                            write.value.bval });
                }
                branch_to_next();
            };
        const auto emit_dynamic_after_slice =
            [&](const WriteAfterDynamicSlice& operation,
                llvm::Value* offset) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[operation.signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, operation.source),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_after_slice_logic9_type,
                        write_after_slice_logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot,
                            constant_i64(context, operation.delay) });
                } else {
                    builder.CreateCall(
                        write_after_slice_type,
                        write_after_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delay) });
                }
                branch_to_next();
            };
        const auto emit_dynamic_inertial_slice =
            [&](const WriteInertialDynamicSlice& operation,
                llvm::Value* offset) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[operation.signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, operation.source),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_inertial_slice_logic9_type,
                        write_inertial_slice_logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot,
                            constant_i64(
                                context, operation.delays.rise),
                            constant_i64(
                                context, operation.delays.fall),
                            constant_i64(
                                context, operation.delays.turnoff) });
                } else {
                    builder.CreateCall(
                        write_inertial_slice_type,
                        write_inertial_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(
                                context, operation.delays.rise),
                            constant_i64(
                                context, operation.delays.fall),
                            constant_i64(
                                context, operation.delays.turnoff) });
                }
                branch_to_next();
            };
        const auto emit_dynamic_projected_slice =
            [&](const WriteProjectedDynamicSlice& operation,
                llvm::Value* offset) {
                const auto signal_kind = signal_value_kinds.empty()
                    ? ValueKind::logic4
                    : signal_value_kinds[operation.signal];
                const auto source = coerce_value_kind(
                    builder,
                    load_register(
                        builder, registers, operation.source),
                    signal_kind);
                if (signal_kind == ValueKind::logic9) {
                    store_logic9_word(logic9_word_slot, source);
                    builder.CreateCall(
                        write_projected_slice_logic9_type,
                        write_projected_slice_logic9_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            logic9_word_slot,
                            constant_i64(context, operation.delay),
                            constant_i64(
                                context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                } else {
                    builder.CreateCall(
                        write_projected_slice_type,
                        write_projected_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delay),
                            constant_i64(
                                context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                }
                branch_to_next();
            };
        ValueOperationLowerer value_lowerer {
            builder,
            registers,
            context,
            i32,
            i64,
            branch_to_next,
            runtime_error_if,
            dynamic_offset,
            constant_part_select_sources[index],
            dynamic_part_signal_sources[index],
            context_pointer,
            process.id,
            instruction,
            read_signal_dynamic_part_callback,
            read_signal_dynamic_part_type,
            logic9_word_slot
        };
        SignalOperationLowerer signal_lowerer {
            builder,
            registers,
            signal_widths,
            signal_value_kinds,
            direct_read_signals,
            direct_update_signals,
            direct_update_slot_type,
            direct_update_slots,
            direct_update_active_words,
            require_direct_update_slots,
            context,
            i32,
            i64,
            context_pointer,
            direct_signal_aval,
            direct_signal_bval,
            direct_signal_logic9_plane0,
            direct_signal_logic9_plane1,
            direct_signal_logic9_plane2,
            direct_signal_logic9_plane3,
            direct_read_signal_map,
            direct_read_signal_count,
            direct_signal_count,
            process.id,
            instruction,
            read_callback,
            read_logic9_callback,
            write_callback,
            write_update_callback,
            write_after_callback,
            write_logic9_callback,
            write_update_logic9_callback,
            write_after_logic9_callback,
            write_blocking_slice_callback,
            write_update_slice_callback,
            write_after_slice_callback,
            write_blocking_slice_logic9_callback,
            write_update_slice_logic9_callback,
            write_after_slice_logic9_callback,
            force_signal_slice_callback,
            force_signal_slice_logic9_callback,
            release_signal_slice_callback,
            force_driver_signal_slice_callback,
            force_driver_signal_slice_logic9_callback,
            release_driver_signal_slice_callback,
            write_projected_waveform_callback,
            write_projected_waveform_logic9_callback,
            write_projected_callback,
            write_projected_logic9_callback,
            write_inertial_callback,
            write_inertial_logic9_callback,
            signal_event_callback,
            signal_last_value_callback,
            signal_last_value_logic9_callback,
            signal_last_event_callback,
            signal_active_callback,
            signal_last_active_callback,
            signal_driving_callback,
            signal_driving_value_callback,
            signal_driving_value_logic9_callback,
            read_simulation_time_callback,
            vital_timing_check_callback,
            vital_delay_callback,
            read_type,
            read_logic9_type,
            write_type,
            write_after_type,
            write_logic9_type,
            write_after_logic9_type,
            write_slice_type,
            write_after_slice_type,
            write_slice_logic9_type,
            write_after_slice_logic9_type,
            release_slice_type,
            write_projected_waveform_type,
            write_projected_waveform_logic9_type,
            write_projected_type,
            write_projected_logic9_type,
            write_inertial_type,
            write_inertial_logic9_type,
            signal_event_type,
            signal_last_value_type,
            signal_last_event_type,
            signal_active_type,
            signal_last_active_type,
            signal_driving_type,
            signal_driving_value_type,
            read_simulation_time_type,
            vital_timing_check_type,
            vital_delay_type,
            projected_element_type,
            logic9_projected_element_type,
            read_bval_slot,
            logic9_word_slot,
            branch_to_next,
            store_logic9_word,
            load_logic9_word,
            runtime_error_if,
            dynamic_offset
        };
        OutputOperationLowerer output_lowerer {
            builder,
            context,
            i32,
            context_pointer,
            process.id,
            instruction,
            index,
            symbol,
            output_type,
            time_output_type,
            report_type,
            output_callback,
            postponed_output_callback,
            time_output_callback,
            monitor_install_callback,
            monitor_control_callback,
            report_callback,
            branch_to_next,
            return_result
        };
        llvm::Value* ssa_callable_return = nullptr;
        if (native_callables.call_operations[index]) {
            const auto& call = fsim::runtime::simir::operation_get<Call>(
                process.operations[index]);
            const auto found = ssa_callable_returns.find(call.target);
            if (found != ssa_callable_returns.end()) {
                ssa_callable_return = found->second;
            }
        } else if (native_callables.return_operations[index]
            && native_callables.return_entries[index]) {
            const auto found = ssa_callable_returns.find(
                *native_callables.return_entries[index]);
            if (found != ssa_callable_returns.end()) {
                ssa_callable_return = found->second;
            }
        }
        ControlFlowOperationLowerer control_lowerer {
            builder,
            registers,
            context,
            i8,
            i32,
            i64,
            register_aval,
            register_bval,
            register_initialized,
            instruction_blocks,
            return_targets,
            native_return_targets,
            frame_type,
            frame_argument,
            native_callables.call_operations[index],
            native_callables.return_operations[index],
            native_callables.frame_operations[index],
            ssa_callable_return,
            invalid_pc,
            function,
            instruction,
            index,
            runtime_error_if,
            return_result
        };
        if (const auto& fused = fused_affine_dynamic_extracts[index]; fused) {
            const auto index_value = coerce_value_kind(
                builder,
                load_register(builder, registers, fused->index),
                ValueKind::logic4);
            runtime_error_if(
                builder.CreateICmpNE(
                    builder.CreateAnd(
                        index_value.bval,
                        constant_i64(
                            context,
                            std::numeric_limits<std::uint32_t>::max())),
                    constant_i64(context, 0U)),
                JitGeneratedRuntimeErrorReason::integer_operand_unknown,
                "affine.dynamic.extract.index.unknown");
            auto* signed_index = builder.CreateSExt(
                builder.CreateTrunc(index_value.aval, i32), i64);
            auto* selected_first = builder.CreateAdd(
                builder.CreateMul(
                    signed_index,
                    llvm::ConstantInt::getSigned(i64, fused->scale)),
                llvm::ConstantInt::getSigned(
                    i64, fused->constant_offset));
            auto* selected_last = builder.CreateAdd(
                selected_first,
                constant_i64(context, fused->width - 1U));
            auto* integer_minimum = llvm::ConstantInt::getSigned(
                i64, std::numeric_limits<std::int32_t>::min());
            auto* integer_maximum = llvm::ConstantInt::getSigned(
                i64, std::numeric_limits<std::int32_t>::max());
            runtime_error_if(
                builder.CreateOr(
                    builder.CreateOr(
                        builder.CreateICmpSLT(
                            selected_first, integer_minimum),
                        builder.CreateICmpSGT(
                            selected_first, integer_maximum)),
                    builder.CreateOr(
                        builder.CreateICmpSLT(
                            selected_last, integer_minimum),
                        builder.CreateICmpSGT(
                            selected_last, integer_maximum))),
                JitGeneratedRuntimeErrorReason::integer_overflow,
                "affine.dynamic.extract.index.overflow");
            auto* normalized_first = builder.CreateAdd(
                builder.CreateSub(
                    selected_first,
                    llvm::ConstantInt::getSigned(
                        i64, fused->source_right)),
                constant_i64(context, fused->source_base_offset));
            const auto source
                = load_register(builder, registers, fused->source);
            const auto work_width = std::max(source.width, fused->width);
            auto* work_type = packed_integer_type(context, work_width);
            auto* packed_result_type
                = packed_integer_type(context, fused->width);
            auto* zero_i64 = constant_i64(context, 0U);
            auto* work_width_i64 = constant_i64(context, work_width);
            auto* negative = builder.CreateICmpSLT(
                normalized_first, zero_i64);
            auto* magnitude = builder.CreateSelect(
                negative,
                builder.CreateNeg(normalized_first),
                normalized_first);
            auto* shift_in_range = builder.CreateICmpULT(
                magnitude, work_width_i64);
            auto* safe_shift = builder.CreateSelect(
                shift_in_range,
                magnitude,
                constant_i64(context, work_width - 1U));
            auto* packed_shift = builder.CreateZExtOrTrunc(
                safe_shift, work_type);
            const auto shift_plane = [&](llvm::Value* plane) {
                auto* widened = builder.CreateZExtOrTrunc(plane, work_type);
                auto* shifted = builder.CreateSelect(
                    negative,
                    builder.CreateShl(widened, packed_shift),
                    builder.CreateLShr(widened, packed_shift));
                shifted = builder.CreateSelect(
                    shift_in_range,
                    shifted,
                    llvm::ConstantInt::get(work_type, 0U));
                return builder.CreateZExtOrTrunc(
                    shifted, packed_result_type);
            };

            auto* result_width_i64 = constant_i64(context, fused->width);
            auto* lower_invalid = builder.CreateSelect(
                negative,
                builder.CreateSelect(
                    builder.CreateICmpULT(magnitude, result_width_i64),
                    magnitude,
                    result_width_i64),
                zero_i64);
            auto* upper_valid = builder.CreateSub(
                constant_i64(context, source.width), normalized_first);
            upper_valid = builder.CreateSelect(
                builder.CreateICmpSLT(upper_valid, zero_i64),
                zero_i64,
                builder.CreateSelect(
                    builder.CreateICmpSGT(
                        upper_valid, result_width_i64),
                    result_width_i64,
                    upper_valid));
            auto* has_valid = builder.CreateICmpULT(
                lower_invalid, upper_valid);
            auto* maximum_mask_shift
                = constant_i64(context, fused->width - 1U);
            auto* lower_shift = builder.CreateSelect(
                builder.CreateICmpULT(
                    lower_invalid, result_width_i64),
                lower_invalid,
                maximum_mask_shift);
            auto* upper_shift_count = builder.CreateSub(
                result_width_i64, upper_valid);
            auto* upper_shift = builder.CreateSelect(
                builder.CreateICmpULT(
                    upper_shift_count, result_width_i64),
                upper_shift_count,
                maximum_mask_shift);
            auto* result_mask = packed_mask(context, fused->width);
            auto* valid_mask = builder.CreateAnd(
                builder.CreateShl(
                    result_mask,
                    builder.CreateZExtOrTrunc(
                        lower_shift, packed_result_type)),
                builder.CreateLShr(
                    result_mask,
                    builder.CreateZExtOrTrunc(
                        upper_shift, packed_result_type)));
            valid_mask = builder.CreateSelect(
                has_valid,
                valid_mask,
                llvm::ConstantInt::get(packed_result_type, 0U));
            auto* invalid_mask = builder.CreateXor(valid_mask, result_mask);
            const auto destination_kind
                = registers[fused->destination].kind;
            const auto finish_plane = [&](llvm::Value* plane,
                                          const bool invalid_one) {
                auto* selected = builder.CreateAnd(
                    shift_plane(plane), valid_mask);
                return invalid_one
                    ? builder.CreateOr(selected, invalid_mask)
                    : selected;
            };
            store_register(
                builder,
                registers,
                fused->destination,
                EncodedValue {
                    finish_plane(source.aval, true),
                    finish_plane(
                        source.bval,
                        destination_kind != ValueKind::logic9),
                    fused->width,
                    finish_plane(source.logic9_plane2, false),
                    finish_plane(source.logic9_plane3, false),
                    destination_kind });
            builder.CreateBr(instruction_blocks[fused->resume]);
            continue;
        }
        fsim::runtime::simir::visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, LoadConstant>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
                    if (signal_widths[operation.signal] > 64) {
                        if (operation.delay == 0U
                            && operation.rejection == 0U
                            && operation.mode
                                == runtime::simir::ProjectedDelayMode::inertial) {
                            write_wide_signal(
                                operation.signal,
                                operation.source,
                                0U,
                                FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE,
                                0U);
                        } else {
                            execute_exact_signal();
                        }
                    } else {
                        signal_lowerer.lower(operation);
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                    } else {
                        signal_lowerer.lower(operation);
                    }
                } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
                    if (operation.kind
                        != runtime::simir::SignalReadKind::current) {
                        return_result(
                            FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                            instruction,
                            0,
                            FSIM_JIT_FRAME_STATE_READY,
                            next_instruction);
                    } else if (signal_widths[operation.signal] > 64) {
                        read_wide_signal(operation);
                    } else {
                        signal_lowerer.lower(operation);
                    }
                } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, ReadSimulationTime>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, VitalTimingCheck>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
                    // The VITAL runtime owns persistent per-call-site state and
                    // therefore reads its operands from the executor frame.
                    // Publish only this operation's live inputs before crossing
                    // that boundary; ordinary transient JIT registers remain
                    // local to the generated function.
                    synchronize_uses_to_frame();
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalLastActive>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDriving>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDrivingValue>) {
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
                    value_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, ConvertToTwoState>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Reduction>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, CountBits>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Shift>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Extract>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Insert>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Binary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
                    value_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            0,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
                    if (signal_widths[operation.signal] > 64) {
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (std::ranges::find(
                                direct_update_signals, operation.signal)
                            != direct_update_signals.end()) {
                            if (!signal_lowerer.begin_direct_update(
                                    operation.signal, 0U, source)) {
                                throw LlvmJitError(
                                    "wide update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            0,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            0,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER,
                            operation.delay);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);

                }
            },
            process.operations[index]);
        OperationLoweringContext operation_context {
            state, index, instruction, next_instruction, branch_to_next,
            runtime_error_if, synchronize_uses_to_frame, execute_exact_signal,
            read_wide_signal, write_wide_signal, dynamic_offset,
            dynamic_offset_i32, emit_dynamic_slice, emit_dynamic_part_slice,
            emit_dynamic_after_slice, emit_dynamic_inertial_slice,
            emit_dynamic_projected_slice, value_lowerer, signal_lowerer,
            output_lowerer, control_lowerer, code_coverage_hit_slots
        };
        lower_suffix_operation(
            operation_context, process.operations[index]);
        for (auto block = std::next(last_block_before_lowering->getIterator());
             block != function->end(); ++block) {
            instruction_regions[index].push_back(&*block);
        }
    }
}

} // namespace fsim::compiler::llvm_detail

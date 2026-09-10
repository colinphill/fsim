// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_context.hpp"
#include "llvm_jit_coverage.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/Support/ErrorHandling.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string>
#include <type_traits>
#include <variant>

namespace fsim::compiler::llvm_detail {
using namespace runtime::simir;

void lower_suffix_operation(
    OperationLoweringContext& operation_context,
    const runtime::simir::Operation& selected_operation)
{
    auto& state = operation_context.process;
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
    [[maybe_unused]] auto&& index = operation_context.index;
    [[maybe_unused]] auto&& instruction = operation_context.instruction;
    [[maybe_unused]] auto&& next_instruction = operation_context.next_instruction;
    [[maybe_unused]] auto&& branch_to_next = operation_context.branch_to_next;
    [[maybe_unused]] auto&& runtime_error_if = operation_context.runtime_error_if;
    [[maybe_unused]] auto&& synchronize_uses_to_frame = operation_context.synchronize_uses_to_frame;
    [[maybe_unused]] auto&& execute_exact_signal = operation_context.execute_exact_signal;
    [[maybe_unused]] auto&& read_wide_signal = operation_context.read_wide_signal;
    [[maybe_unused]] auto&& write_wide_signal = operation_context.write_wide_signal;
    [[maybe_unused]] auto&& dynamic_offset = operation_context.dynamic_offset;
    [[maybe_unused]] auto&& dynamic_offset_i32 = operation_context.dynamic_offset_i32;
    [[maybe_unused]] auto&& emit_dynamic_slice = operation_context.emit_dynamic_slice;
    [[maybe_unused]] auto&& emit_dynamic_part_slice = operation_context.emit_dynamic_part_slice;
    [[maybe_unused]] auto&& emit_dynamic_after_slice = operation_context.emit_dynamic_after_slice;
    [[maybe_unused]] auto&& emit_dynamic_inertial_slice = operation_context.emit_dynamic_inertial_slice;
    [[maybe_unused]] auto&& emit_dynamic_projected_slice = operation_context.emit_dynamic_projected_slice;
    [[maybe_unused]] auto&& value_lowerer = operation_context.value_lowerer;
    [[maybe_unused]] auto&& signal_lowerer = operation_context.signal_lowerer;
    [[maybe_unused]] auto&& output_lowerer = operation_context.output_lowerer;
    [[maybe_unused]] auto&& control_lowerer = operation_context.control_lowerer;
    [[maybe_unused]] auto&& code_coverage_hit_slots
        = operation_context.code_coverage_hit_slots;
    fsim::runtime::simir::visit_operation(
        [&](const auto& operation) {
            using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
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
                                    operation.signal,
                                    operation.offset,
                                    source)) {
                                throw LlvmJitError(
                                    "wide slice-update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE,
                            operation.delay);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
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
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, source.width),
                                logic9_word_slot,
                                constant_i64(
                                    context, operation.delays.rise),
                                constant_i64(
                                    context, operation.delays.fall),
                                constant_i64(
                                    context, operation.delays.turnoff) });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        write_inertial_slice_type,
                        write_inertial_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            llvm::ConstantInt::get(
                                i32, operation.offset),
                            llvm::ConstantInt::get(
                                i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delays.rise),
                            constant_i64(context, operation.delays.fall),
                            constant_i64(
                                context, operation.delays.turnoff) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    const auto source = coerce_value_kind(
                        builder,
                        load_register(
                            builder, registers, operation.source),
                        signal_kind);
                    if (signal_kind == ValueKind::logic9
                        && operation.delay == 0U
                        && operation.rejection == 0U
                        && operation.mode
                            == runtime::simir::ProjectedDelayMode::inertial
                        && signal_lowerer.begin_direct_update(
                            operation.signal, operation.offset, source)
                        && require_direct_update_slots) {
                        return;
                    }
                    if (signal_kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, source);
                        builder.CreateCall(
                            write_projected_slice_logic9_type,
                            write_projected_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, source.width),
                                logic9_word_slot,
                                constant_i64(
                                    context, operation.delay),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        write_projected_slice_type,
                        write_projected_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            llvm::ConstantInt::get(
                                i32, operation.offset),
                            llvm::ConstantInt::get(
                                i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delay),
                            constant_i64(context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    if (signal_kind == ValueKind::logic9) {
                        auto* array_type = llvm::ArrayType::get(
                            logic9_projected_element_type,
                            operation.elements.size());
                        auto* storage = builder.CreateAlloca(
                            array_type,
                            nullptr,
                            "projected.logic9.slice.waveform");
                        for (std::size_t element_index = 0;
                            element_index < operation.elements.size();
                            ++element_index) {
                            const auto& element = operation.elements[element_index];
                            auto source = coerce_value_kind(
                                builder,
                                load_register(
                                    builder,
                                    registers,
                                    element.source),
                                ValueKind::logic9);
                            auto* slot = builder.CreateInBoundsGEP(
                                array_type,
                                storage,
                                { llvm::ConstantInt::get(i32, 0),
                                    llvm::ConstantInt::get(
                                        i32,
                                        static_cast<std::uint32_t>(
                                            element_index)) });
                            store_logic9_word(
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    0),
                                source);
                            builder.CreateStore(
                                constant_i64(context, element.delay),
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    1));
                        }
                        const auto first = load_register(
                            builder,
                            registers,
                            operation.elements.front().source);
                        builder.CreateCall(
                            write_projected_waveform_slice_logic9_type,
                            write_projected_waveform_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, first.width),
                                storage,
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.elements.size())),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    auto* array_type = llvm::ArrayType::get(
                        projected_element_type, operation.elements.size());
                    auto* storage = builder.CreateAlloca(
                        array_type, nullptr, "projected.slice.waveform");
                    for (std::size_t element_index = 0;
                        element_index < operation.elements.size();
                        ++element_index) {
                        const auto& element = operation.elements[element_index];
                        const auto source = load_register(builder, registers, element.source);
                        auto* slot = builder.CreateInBoundsGEP(
                            array_type,
                            storage,
                            { llvm::ConstantInt::get(i32, 0),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(element_index)) });
                        builder.CreateStore(
                            source.aval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 0));
                        builder.CreateStore(
                            source.bval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 1));
                        builder.CreateStore(
                            constant_i64(context, element.delay),
                            builder.CreateStructGEP(
                                projected_element_type, slot, 2));
                    }
                    const auto first = load_register(
                        builder, registers, operation.elements.front().source);
                    builder.CreateCall(
                        write_projected_waveform_slice_type,
                        write_projected_waveform_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, operation.signal),
                            llvm::ConstantInt::get(i32, operation.offset),
                            llvm::ConstantInt::get(i32, first.width),
                            storage,
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.elements.size())),
                            constant_i64(context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_slice(
                        operation.signal,
                        operation.source,
                        dynamic_offset_i32(operation.selection),
                        write_blocking_slice_callback,
                        write_blocking_slice_logic9_callback);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        auto* offset = dynamic_offset_i32(operation.selection);
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (std::ranges::find(
                                direct_update_signals, operation.signal)
                            != direct_update_signals.end()) {
                            if (!signal_lowerer.begin_direct_update(
                                    operation.signal,
                                    offset,
                                    llvm::ConstantInt::get(i32, source.width),
                                    source)) {
                                throw LlvmJitError(
                                    "wide dynamic-update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        execute_exact_signal();
                        return;
                    }
                    auto* offset = dynamic_offset_i32(operation.selection);
                    if (std::ranges::find(
                            direct_update_signals, operation.signal)
                        == direct_update_signals.end()) {
                        emit_dynamic_slice(
                            operation.signal,
                            operation.source,
                            offset,
                            write_update_slice_callback,
                            write_update_slice_logic9_callback);
                    } else {
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (!signal_lowerer.begin_direct_update(
                                operation.signal,
                                offset,
                                llvm::ConstantInt::get(i32, source.width),
                                source)) {
                            throw LlvmJitError(
                                "dynamic update accumulator layout mismatch");
                        }
                        if (require_direct_update_slots) {
                            return;
                        }
                        builder.CreateCall(
                            write_slice_type,
                            write_update_slice_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                offset,
                                llvm::ConstantInt::get(i32, source.width),
                                source.aval,
                                source.bval });
                        branch_to_next();
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_after_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_part_slice(
                        operation.signal,
                        operation.source,
                        operation.selection,
                        write_blocking_slice_callback,
                        write_blocking_slice_logic9_callback,
                        std::nullopt);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        if (signal_widths[operation.signal] > 64
                            && operation.selection.width <= 64U) {
                            const auto write = lower_dynamic_part_write(
                                builder,
                                context,
                                i32,
                                i64,
                                registers,
                                operation.source,
                                operation.selection,
                                ValueKind::logic4);
                            auto* write_block = llvm::BasicBlock::Create(
                                context,
                                "dynamic.part.wide.update."
                                    + std::to_string(index),
                                function);
                            builder.CreateCondBr(
                                builder.CreateICmpNE(
                                    write.width,
                                    llvm::ConstantInt::get(i32, 0)),
                                write_block,
                                instruction_blocks[index + 1]);
                            builder.SetInsertPoint(write_block);
                            if (std::ranges::find(
                                    direct_update_signals,
                                    operation.signal)
                                != direct_update_signals.end()) {
                                if (!signal_lowerer.begin_direct_update(
                                        operation.signal,
                                        write.offset,
                                        write.width,
                                        write.value)) {
                                    throw LlvmJitError(
                                        "wide dynamic part-update accumulator "
                                        "layout mismatch");
                                }
                                if (require_direct_update_slots) {
                                    return;
                                }
                            }
                        }
                        execute_exact_signal();
                        return;
                    }
                    if (std::ranges::find(
                            direct_update_signals, operation.signal)
                        == direct_update_signals.end()) {
                        emit_dynamic_part_slice(
                            operation.signal,
                            operation.source,
                            operation.selection,
                            write_update_slice_callback,
                            write_update_slice_logic9_callback,
                            std::nullopt);
                    } else {
                        const auto write = lower_dynamic_part_write(
                            builder,
                            context,
                            i32,
                            i64,
                            registers,
                            operation.source,
                            operation.selection,
                            ValueKind::logic4);
                        auto* write_block = llvm::BasicBlock::Create(
                            context,
                            "dynamic.part.update." + std::to_string(index),
                            function);
                        builder.CreateCondBr(
                            builder.CreateICmpNE(
                                write.width,
                                llvm::ConstantInt::get(i32, 0)),
                            write_block,
                            instruction_blocks[index + 1]);
                        builder.SetInsertPoint(write_block);
                        if (!signal_lowerer.begin_direct_update(
                                operation.signal,
                                write.offset,
                                write.width,
                                write.value)) {
                            throw LlvmJitError(
                                "dynamic part-update accumulator layout mismatch");
                        }
                        if (require_direct_update_slots) {
                            return;
                        }
                        builder.CreateCall(
                            write_slice_type,
                            write_update_slice_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                write.offset,
                                write.width,
                                write.value.aval,
                                write.value.bval });
                        branch_to_next();
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_part_slice(
                        operation.signal,
                        operation.source,
                        operation.selection,
                        write_after_slice_callback,
                        write_after_slice_logic9_callback,
                        operation.delay);
                } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_inertial_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         WriteInertialDynamicPartSlice>) {
                    execute_exact_signal();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
                    emit_dynamic_projected_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    auto* offset = dynamic_offset_i32(operation.selection);
                    if (signal_kind == ValueKind::logic9) {
                        auto* array_type = llvm::ArrayType::get(
                            logic9_projected_element_type,
                            operation.elements.size());
                        auto* storage = builder.CreateAlloca(
                            array_type,
                            nullptr,
                            "projected.logic9.dynamic.slice.waveform");
                        for (std::size_t element_index = 0;
                            element_index < operation.elements.size();
                            ++element_index) {
                            const auto& element = operation.elements[element_index];
                            auto source = coerce_value_kind(
                                builder,
                                load_register(
                                    builder,
                                    registers,
                                    element.source),
                                ValueKind::logic9);
                            auto* slot = builder.CreateInBoundsGEP(
                                array_type,
                                storage,
                                { llvm::ConstantInt::get(i32, 0),
                                    llvm::ConstantInt::get(
                                        i32,
                                        static_cast<std::uint32_t>(
                                            element_index)) });
                            store_logic9_word(
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    0),
                                source);
                            builder.CreateStore(
                                constant_i64(context, element.delay),
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    1));
                        }
                        const auto first = load_register(
                            builder,
                            registers,
                            operation.elements.front().source);
                        builder.CreateCall(
                            write_projected_waveform_slice_logic9_type,
                            write_projected_waveform_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                offset,
                                llvm::ConstantInt::get(
                                    i32, first.width),
                                storage,
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.elements.size())),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    auto* array_type = llvm::ArrayType::get(
                        projected_element_type,
                        operation.elements.size());
                    auto* storage = builder.CreateAlloca(
                        array_type,
                        nullptr,
                        "projected.dynamic.slice.waveform");
                    for (std::size_t element_index = 0;
                        element_index < operation.elements.size();
                        ++element_index) {
                        const auto& element = operation.elements[element_index];
                        const auto source = load_register(
                            builder, registers, element.source);
                        auto* slot = builder.CreateInBoundsGEP(
                            array_type,
                            storage,
                            { llvm::ConstantInt::get(i32, 0),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        element_index)) });
                        builder.CreateStore(
                            source.aval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 0));
                        builder.CreateStore(
                            source.bval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 1));
                        builder.CreateStore(
                            constant_i64(context, element.delay),
                            builder.CreateStructGEP(
                                projected_element_type, slot, 2));
                    }
                    const auto first = load_register(
                        builder,
                        registers,
                        operation.elements.front().source);
                    builder.CreateCall(
                        write_projected_waveform_slice_type,
                        write_projected_waveform_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, first.width),
                            storage,
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.elements.size())),
                            constant_i64(
                                context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, Assert>) {
                    const auto condition = load_register(builder, registers, operation.condition);
                    auto* known = builder.CreateICmpEQ(
                        condition.bval, constant_i64(context, 0));
                    auto* one = builder.CreateICmpEQ(condition.aval,
                        constant_i64(context, 1));
                    auto* passed = builder.CreateAnd(known, one);
                    auto* failed_block = llvm::BasicBlock::Create(
                        context, "assert.failed." + std::to_string(index),
                        function);
                    builder.CreateCondBr(
                        passed, instruction_blocks[index + 1], failed_block);

                    builder.SetInsertPoint(failed_block);
                    if (operation.severity
                        == runtime::simir::AssertionSeverity::failure) {
                        const auto message = operation.message.empty()
                            ? std::string { "assertion failed" }
                            : operation.message.str();
                        auto* message_pointer = builder.CreateGlobalString(
                            message,
                            symbol + ".assert." + std::to_string(index));
                        builder.CreateCall(
                            assert_type,
                            assert_callback,
                            {
                                context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                                message_pointer,
                                constant_i64(context, message.size()),
                            });
                        return_result(
                            FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED,
                            instruction,
                            0,
                            FSIM_JIT_FRAME_STATE_ASSERTION_FAILED,
                            instruction);
                    } else {
                        builder.CreateCall(
                            report_type,
                            report_callback,
                            {
                                context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                            });
                        builder.CreateBr(
                            instruction_blocks[index + 1]);
                    }
                } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
                    if (!debug_instrumentation) {
                        branch_to_next();
                        return;
                    }
                    auto* enabled = builder.CreateICmpNE(
                        builder.CreateAnd(
                            runtime_flags,
                            llvm::ConstantInt::get(
                                i32, FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS)),
                        llvm::ConstantInt::get(i32, 0));
                    auto* enabled_block = llvm::BasicBlock::Create(
                        context,
                        "debug.enabled." + std::to_string(index),
                        function);
                    builder.CreateCondBr(
                        enabled, enabled_block,
                        instruction_blocks[index + 1]);
                    builder.SetInsertPoint(enabled_block);
                    return_result(
                        FSIM_JIT_RESUME_STATUS_DEBUG_POINT, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Display>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
                    const auto value = load_register(builder, registers, operation.source);
                    if (value.kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, value);
                        builder.CreateCall(
                            formatted_output_logic9_type,
                            write_formatted_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                                llvm::ConstantInt::get(i32, value.width),
                                logic9_word_slot });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        formatted_output_type,
                        formatted_output_callback,
                        {
                            context_pointer,
                            llvm::ConstantInt::get(i32, process.id),
                            llvm::ConstantInt::get(i32, instruction),
                            llvm::ConstantInt::get(i32, value.width),
                            value.aval,
                            value.bval,
                        });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
                    output_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, TimeFormatControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        CoverageDatabaseControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, StochasticQueueOperation>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, CoverageSample>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, CoverageQuery>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, VhdlPslApi>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, VhdlAssertApi>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageAccess>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, CodeCoverageHit>) {
                    lower_code_coverage_hit({
                        builder,
                        context,
                        i32,
                        i64,
                        context_pointer,
                        code_coverage_hit_counters,
                        code_coverage_counter_values,
                        code_coverage_hit_count,
                        code_coverage_counter_count,
                        record_code_coverage_counter,
                        record_code_coverage_counter_type,
                        process.id,
                        instruction,
                        code_coverage_hit_slots[index],
                        runtime_error_if,
                        branch_to_next
                    });
                } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
                    const auto zero = constant_i64(context, 0);
                    const auto maximum = operation.maximum
                        ? load_register(
                              builder, registers, *operation.maximum)
                        : EncodedValue { zero, zero, 32 };
                    const auto minimum = operation.minimum
                        ? load_register(
                              builder, registers, *operation.minimum)
                        : EncodedValue { zero, zero, 32 };
                    builder.CreateStore(zero, read_bval_slot);
                    auto* aval = builder.CreateCall(
                        random_value_type,
                        random_value_callback,
                        {
                            context_pointer,
                            llvm::ConstantInt::get(i32, process.id),
                            llvm::ConstantInt::get(i32, instruction),
                            maximum.aval,
                            maximum.bval,
                            minimum.aval,
                            minimum.bval,
                            read_bval_slot,
                        });
                    auto* bval = builder.CreateLoad(
                        i64, read_bval_slot, "random.bval");
                    store_register(
                        builder,
                        registers,
                        operation.destination,
                        EncodedValue { aval, bval, 32 });
                    branch_to_next();
                } else if constexpr (
                    std::is_same_v<OperationType, RandomDistribution>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, VhdlEnvironmentTime>
                    || std::is_same_v<OperationType,
                        VhdlEnvironmentTimeToString>
                    || std::is_same_v<OperationType,
                        VhdlEnvironmentDirectory>
                    || std::is_same_v<OperationType,
                        VhdlEnvironmentGetenv>
                    || std::is_same_v<OperationType,
                        VhdlEnvironmentCallPath>
                    || std::is_same_v<OperationType,
                        VhdlEnvironmentGetCallPath>
                    || std::is_same_v<OperationType,
                        VhdlReflectionApi>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Report>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, StringReport>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Jump>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Call>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Return>) {
                    control_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePush>) {
                    control_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePop>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Branch>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    operation_group_contains_v<OperationType, ClassOperationGroup>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_FOREVER, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Yield>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_YIELDED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Fork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_FORK_END, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_DISABLE_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSelf>
                    || std::is_same_v<OperationType, ProcessStatusQuery>
                    || std::is_same_v<OperationType, ProcessCompleted>
                    || std::is_same_v<OperationType, ProcessAwait>
                    || std::is_same_v<OperationType, ProcessKill>
                    || std::is_same_v<OperationType, ProcessSuspend>
                    || std::is_same_v<OperationType, ProcessResume>
                    || std::is_same_v<OperationType, ProcessGetRandState>
                    || std::is_same_v<OperationType, ProcessSetRandState>
                    || std::is_same_v<OperationType, ProcessSrandom>
                    || std::is_same_v<OperationType, MailboxCreate>
                    || std::is_same_v<OperationType, MailboxPut>
                    || std::is_same_v<OperationType, MailboxGet>
                    || std::is_same_v<OperationType, MailboxNum>
                    || std::is_same_v<OperationType, SemaphoreCreate>
                    || std::is_same_v<OperationType, SemaphoreGet>
                    || std::is_same_v<OperationType, SemaphorePut>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Pause>) {
                    if (operation.status) {
                        synchronize_uses_to_frame();
                    }
                    return_result(
                        FSIM_JIT_RESUME_STATUS_PAUSED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Stop>) {
                    if (operation.status) {
                        synchronize_uses_to_frame();
                    }
                    return_result(
                        FSIM_JIT_RESUME_STATUS_STOPPED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_STOPPED, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Halt>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_COMPLETED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_COMPLETED, next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, LoadConstant>
                    || std::is_same_v<OperationType, WriteProjectedWaveform>
                    || std::is_same_v<OperationType, WriteProjected>
                    || std::is_same_v<OperationType, WriteInertial>
                    || std::is_same_v<OperationType, ReadSignal>
                    || std::is_same_v<OperationType, SignalEvent>
                    || std::is_same_v<OperationType, SignalLastValue>
                    || std::is_same_v<OperationType, SignalLastEvent>
                    || std::is_same_v<OperationType, ReadSimulationTime>
                    || std::is_same_v<OperationType, VitalTimingCheck>
                    || std::is_same_v<OperationType, VitalDelay>
                    || std::is_same_v<OperationType, SignalActive>
                    || std::is_same_v<OperationType, SignalLastActive>
                    || std::is_same_v<OperationType, SignalDriving>
                    || std::is_same_v<OperationType, SignalDrivingValue>
                    || std::is_same_v<OperationType, CopyRegister>
                    || std::is_same_v<OperationType, ConvertToTwoState>
                    || std::is_same_v<OperationType, UnaryNot>
                    || std::is_same_v<OperationType, LogicalNot>
                    || std::is_same_v<OperationType, LogicalBinary>
                    || std::is_same_v<OperationType, Reduction>
                    || std::is_same_v<OperationType, CountOnes>
                    || std::is_same_v<OperationType, CountBits>
                    || std::is_same_v<OperationType, Shift>
                    || std::is_same_v<OperationType, Extract>
                    || std::is_same_v<OperationType, DynamicExtract>
                    || std::is_same_v<OperationType, DynamicPartSelect>
                    || std::is_same_v<OperationType, Insert>
                    || std::is_same_v<OperationType, DynamicInsert>
                    || std::is_same_v<OperationType, DynamicPartInsert>
                    || std::is_same_v<OperationType, Concatenate>
                    || std::is_same_v<OperationType, Binary>
                    || std::is_same_v<OperationType, IntegerUnary>
                    || std::is_same_v<OperationType, IntegerBinary>
                    || std::is_same_v<OperationType, IntegerCheck>
                    || std::is_same_v<OperationType, ConditionalSelect>
                    || std::is_same_v<OperationType, WriteBlocking>
                    || std::is_same_v<OperationType, WriteUpdate>
                    || std::is_same_v<OperationType, WriteAfter>
                    || std::is_same_v<OperationType, WriteBlockingSlice>) {
                    // Lowered by the prefix dispatcher.
                } else {
                    if constexpr (
                        requires(FileOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        FileOperationLowerer file_lowerer {
                            builder,
                            registers,
                            frame_registers,
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            runtime_error_if,
                            branch_to_next
                        };
                        file_lowerer.lower(operation);
                    } else if constexpr (
                        requires(StringOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        StringOperationLowerer string_lowerer {
                            module,
                            builder,
                            registers,
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            runtime_error_if,
                            branch_to_next
                        };
                        string_lowerer.lower(operation);
                    } else if constexpr (
                        requires(ContainerOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        ContainerOperationLowerer container_lowerer {
                            builder,
                            registers,
                            frame_registers,
                            validated.instruction_uses[index],
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            process.container_register_types,
                            container_result_aval_slot,
                            container_result_bval_slot,
                            fused_container_object_reads[index],
                            runtime_error_if,
                            branch_to_next
                        };
                        container_lowerer.lower(operation);
                    } else {
                        llvm_unreachable(
                            "unsupported operations were rejected before lowering");
                    }
                }
            },
        selected_operation);
}

} // namespace fsim::compiler::llvm_detail

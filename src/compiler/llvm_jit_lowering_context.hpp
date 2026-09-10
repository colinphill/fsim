// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_lowering_internal.hpp"

#include <llvm/ADT/STLFunctionalExtras.h>

#include <map>
#include <optional>
#include <span>
#include <vector>

namespace fsim::compiler::llvm_detail {

struct NativeCallablePlan {
    std::vector<bool> call_operations;
    std::vector<bool> return_operations;
    std::vector<bool> frame_operations;
    std::vector<std::optional<runtime::simir::InstructionIndex>> return_entries;
    std::vector<runtime::simir::InstructionIndex> return_targets;
    std::vector<std::pair<runtime::simir::InstructionIndex,
        runtime::simir::InstructionIndex>> regions;
};

struct FusedAffineDynamicExtract {
    runtime::simir::RegisterId destination { };
    runtime::simir::RegisterId source { };
    runtime::simir::RegisterId index { };
    std::int64_t scale { };
    std::int64_t constant_offset { };
    std::int64_t source_right { };
    std::uint32_t source_base_offset { };
    std::uint32_t width { };
    std::size_t resume { };
};

[[nodiscard]] NativeCallablePlan analyze_native_callables(
    const runtime::simir::Process& process);
[[nodiscard]] std::vector<runtime::simir::InstructionIndex>
static_return_targets(const runtime::simir::Process& process);
[[nodiscard]] std::size_t coalesce_linear_blocks(llvm::Function& function);

struct ProcessLoweringContext {
    llvm::Module & module;
    const std::string & symbol;
    const runtime::simir::Process& process;
    std::span<const std::uint32_t> signal_widths;
    std::span<const runtime::simir::ValueKind> signal_value_kinds;
    std::span<const runtime::simir::SignalId> direct_read_signals;
    std::span<const runtime::simir::SignalId> direct_update_signals;
    const ValidatedProcess & validated;
    bool debug_instrumentation;
    bool require_direct_update_slots;
    llvm::LLVMContext& context;
    llvm::IntegerType* i32;
    llvm::IntegerType* i64;
    llvm::PointerType* pointer;
    llvm::StructType * runtime_type;
    llvm::StructType* direct_update_slot_type;
    llvm::StructType* frame_type;
    llvm::Function* function;
    llvm::IRBuilder<>& builder;
    llvm::Value* runtime_argument;
    llvm::Value* frame_argument;
    llvm::Value* context_pointer;
    llvm::Value* read_callback;
    llvm::Value* write_callback;
    llvm::Value* assert_callback;
    llvm::Value * code_coverage_hit_counters;
    llvm::Value * code_coverage_counter_values;
    llvm::Value * code_coverage_hit_count;
    llvm::Value * code_coverage_counter_count;
    llvm::Value * record_code_coverage_counter;
    llvm::FunctionType * record_code_coverage_counter_type;
    llvm::Value * direct_update_slots;
    llvm::Value * direct_update_active_words;
    llvm::Value * static_trigger_mask;
    llvm::Value * direct_signal_aval;
    llvm::Value * direct_signal_bval;
    llvm::Value * direct_signal_logic9_plane0;
    llvm::Value * direct_signal_logic9_plane1;
    llvm::Value * direct_signal_logic9_plane2;
    llvm::Value * direct_signal_logic9_plane3;
    llvm::Value * direct_read_signal_map;
    llvm::Value * direct_read_signal_count;
    llvm::Value * direct_signal_count;
    llvm::Value * direct_wide_signal_aval;
    llvm::Value * direct_wide_signal_bval;
    llvm::Value * direct_wide_signal_logic9_plane2;
    llvm::Value * direct_wide_signal_logic9_plane3;
    llvm::Value * direct_wide_signal_offsets;
    llvm::Value * direct_wide_signal_offset_count;
    llvm::Value * direct_wide_word_count;
    llvm::Value * write_update_callback;
    llvm::Value * write_after_callback;
    llvm::Value * write_blocking_slice_callback;
    llvm::Value * write_update_slice_callback;
    llvm::Value * write_after_slice_callback;
    llvm::Value * force_signal_slice_callback;
    llvm::Value * force_signal_slice_logic9_callback;
    llvm::Value * release_signal_slice_callback;
    llvm::Value * force_driver_signal_slice_callback;
    llvm::Value * force_driver_signal_slice_logic9_callback;
    llvm::Value * release_driver_signal_slice_callback;
    llvm::Value * runtime_flags;
    llvm::Value * signal_event_callback;
    llvm::Value * signal_last_value_callback;
    llvm::Value * signal_last_event_callback;
    llvm::Value * signal_active_callback;
    llvm::Value * signal_last_active_callback;
    llvm::Value * signal_driving_callback;
    llvm::Value * signal_driving_value_callback;
    llvm::Value * signal_driving_value_logic9_callback;
    llvm::Value * read_simulation_time_callback;
    llvm::Value * vital_timing_check_callback;
    llvm::Value * vital_delay_callback;
    llvm::Value * output_callback;
    llvm::Value * postponed_output_callback;
    llvm::Value * report_callback;
    llvm::Value * formatted_output_callback;
    llvm::Value * time_output_callback;
    llvm::Value * monitor_install_callback;
    llvm::Value * monitor_control_callback;
    llvm::Value * random_value_callback;
    llvm::Value * write_inertial_callback;
    llvm::Value * write_inertial_slice_callback;
    llvm::Value * exact_signal_callback;
    llvm::Value * read_signal_packed_callback;
    llvm::Value * write_signal_packed_callback;
    llvm::Value* read_signal_dynamic_part_callback;
    llvm::Value * write_projected_callback;
    llvm::Value * write_projected_slice_callback;
    llvm::Value * write_projected_waveform_callback;
    llvm::Value * write_projected_waveform_slice_callback;
    llvm::Value * read_logic9_callback;
    llvm::Value * write_logic9_callback;
    llvm::Value * write_update_logic9_callback;
    llvm::Value * write_after_logic9_callback;
    llvm::Value * write_blocking_slice_logic9_callback;
    llvm::Value * write_update_slice_logic9_callback;
    llvm::Value * write_after_slice_logic9_callback;
    llvm::Value * signal_last_value_logic9_callback;
    llvm::Value * write_inertial_logic9_callback;
    llvm::Value * write_inertial_slice_logic9_callback;
    llvm::Value * write_projected_logic9_callback;
    llvm::Value * write_projected_slice_logic9_callback;
    llvm::Value * write_projected_waveform_logic9_callback;
    llvm::Value * write_projected_waveform_slice_logic9_callback;
    llvm::Value * write_formatted_logic9_callback;
    llvm::FunctionType* read_type;
    llvm::FunctionType* write_type;
    llvm::FunctionType* assert_type;
    llvm::FunctionType* write_after_type;
    llvm::FunctionType* write_slice_type;
    llvm::FunctionType* write_after_slice_type;
    llvm::FunctionType* release_slice_type;
    llvm::FunctionType* write_inertial_type;
    llvm::FunctionType* write_inertial_slice_type;
    llvm::FunctionType* exact_signal_type;
    llvm::FunctionType* read_signal_packed_type;
    llvm::FunctionType* read_signal_dynamic_part_type;
    llvm::FunctionType* write_signal_packed_type;
    llvm::FunctionType* write_projected_type;
    llvm::FunctionType* write_projected_slice_type;
    llvm::StructType* projected_element_type;
    llvm::FunctionType* write_projected_waveform_type;
    llvm::FunctionType* write_projected_waveform_slice_type;
    llvm::FunctionType* signal_event_type;
    llvm::FunctionType* signal_last_value_type;
    llvm::FunctionType* signal_last_event_type;
    llvm::FunctionType* signal_active_type;
    llvm::FunctionType* signal_last_active_type;
    llvm::FunctionType* signal_driving_type;
    llvm::FunctionType* signal_driving_value_type;
    llvm::FunctionType* read_simulation_time_type;
    llvm::FunctionType* vital_timing_check_type;
    llvm::FunctionType* vital_delay_type;
    llvm::FunctionType* output_type;
    llvm::FunctionType* report_type;
    llvm::FunctionType* formatted_output_type;
    llvm::FunctionType* time_output_type;
    llvm::FunctionType* random_value_type;
    llvm::FunctionType* read_logic9_type;
    llvm::FunctionType* write_logic9_type;
    llvm::FunctionType* write_after_logic9_type;
    llvm::FunctionType* write_slice_logic9_type;
    llvm::FunctionType* write_after_slice_logic9_type;
    llvm::FunctionType* write_inertial_logic9_type;
    llvm::FunctionType* write_inertial_slice_logic9_type;
    llvm::FunctionType* write_projected_logic9_type;
    llvm::FunctionType* write_projected_slice_logic9_type;
    llvm::StructType* logic9_projected_element_type;
    llvm::FunctionType* write_projected_waveform_logic9_type;
    llvm::FunctionType* write_projected_waveform_slice_logic9_type;
    llvm::FunctionType* formatted_output_logic9_type;
    const NativeCallablePlan& native_callables;
    const ProcessLoweringPlan& lowering_plan;
    llvm::Value * register_aval;
    llvm::Value * register_bval;
    llvm::Value * register_initialized;
    llvm::IntegerType* i8;
    std::vector<RegisterSlot>& registers;
    std::vector<RegisterSlot>& frame_registers;
    llvm::Value* read_bval_slot;
    llvm::Value* logic9_word_slot;
    llvm::Value* container_result_aval_slot;
    llvm::Value* container_result_bval_slot;
    llvm::function_ref<void(llvm::Value*, EncodedValue)> store_logic9_word;
    llvm::function_ref<EncodedValue(llvm::Value*, std::uint32_t)> load_logic9_word;
    llvm::function_ref<void(std::uint32_t, std::uint32_t, std::uint64_t, std::uint32_t, std::uint32_t)> return_result;
    std::vector<bool>& elided_operations;
    std::vector<std::optional<FusedAffineDynamicExtract>>& fused_affine_dynamic_extracts;
    std::vector<const runtime::PackedLogic4 *>& constant_part_select_sources;
    std::vector<std::optional<runtime::simir::SignalId>>& dynamic_part_signal_sources;
    std::vector<std::uint32_t>& fused_container_object_reads;
    std::vector<llvm::BasicBlock *>& instruction_blocks;
    std::vector<std::vector<llvm::BasicBlock *>>& instruction_regions;
    std::vector<const runtime::simir::Process::StaticTriggerRegion *>& static_trigger_region_entries;
    std::map<runtime::simir::InstructionIndex, llvm::AllocaInst *>& ssa_callable_returns;
    llvm::BasicBlock* invalid_pc;
};

struct OperationLoweringContext {
    ProcessLoweringContext& process;
    std::size_t index;
    runtime::simir::InstructionIndex instruction;
    runtime::simir::InstructionIndex next_instruction;
    llvm::function_ref<void()> branch_to_next;
    llvm::function_ref<void(llvm::Value*, JitGeneratedRuntimeErrorReason,
        std::string_view)> runtime_error_if;
    llvm::function_ref<void()> synchronize_uses_to_frame;
    llvm::function_ref<void()> execute_exact_signal;
    llvm::function_ref<void(const runtime::simir::ReadSignal&)> read_wide_signal;
    llvm::function_ref<void(std::uint32_t, runtime::simir::RegisterId,
        std::uint32_t, std::uint32_t, runtime::SimulationTick)> write_wide_signal;
    llvm::function_ref<llvm::Value*(const runtime::simir::DynamicIndex&)>
        dynamic_offset;
    llvm::function_ref<llvm::Value*(const runtime::simir::DynamicIndex&)>
        dynamic_offset_i32;
    llvm::function_ref<void(std::uint32_t, runtime::simir::RegisterId,
        llvm::Value*, llvm::Value*, llvm::Value*)> emit_dynamic_slice;
    llvm::function_ref<void(std::uint32_t, runtime::simir::RegisterId,
        const runtime::simir::DynamicPartIndex&, llvm::Value*, llvm::Value*,
        std::optional<runtime::SimulationTick>)> emit_dynamic_part_slice;
    llvm::function_ref<void(const runtime::simir::WriteAfterDynamicSlice&,
        llvm::Value*)> emit_dynamic_after_slice;
    llvm::function_ref<void(const runtime::simir::WriteInertialDynamicSlice&,
        llvm::Value*)> emit_dynamic_inertial_slice;
    llvm::function_ref<void(const runtime::simir::WriteProjectedDynamicSlice&,
        llvm::Value*)> emit_dynamic_projected_slice;
    ValueOperationLowerer& value_lowerer;
    SignalOperationLowerer& signal_lowerer;
    OutputOperationLowerer& output_lowerer;
    ControlFlowOperationLowerer& control_lowerer;
    const std::vector<std::uint32_t>& code_coverage_hit_slots;
};

void lower_process_operations(ProcessLoweringContext& context);
void lower_suffix_operation(
    OperationLoweringContext& context,
    const runtime::simir::Operation& operation);

} // namespace fsim::compiler::llvm_detail

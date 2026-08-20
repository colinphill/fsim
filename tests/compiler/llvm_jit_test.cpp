// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

int main() {
  using namespace fsim::tests::compiler;
  // FSIM-CONFORMANCE CF-COMMON-LLVM-001 source=SRC-LLVM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-CACHE-001 source=SRC-LLVM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-LLVM-N01 source=SRC-LLVM expectation=reject
  assert(!LlvmJit::llvm_version().empty());
  const auto native_o0 =
      LlvmJit::native_host_identity(JitOptimizationLevel::o0);
  const auto native_o1 =
      LlvmJit::native_host_identity(JitOptimizationLevel::o1);
  const auto native_o2 =
      LlvmJit::native_host_identity(JitOptimizationLevel::o2);
  assert(native_o0.fingerprint.size() == 64);
  assert(native_o1.fingerprint.size() == 64);
  assert(native_o2.fingerprint.size() == 64);
  assert(native_o0.fingerprint != native_o1.fingerprint);
  assert(native_o1.fingerprint != native_o2.fingerprint);
  assert(native_o0.fingerprint != native_o2.fingerprint);
  assert(!native_o2.target.empty());
  assert(!native_o2.data_layout.empty());
  assert(!native_o2.cpu.empty());
  assert(
      LlvmJit::native_host_identity(JitOptimizationLevel::o2)
      == native_o2);
  run_at_level(JitOptimizationLevel::o0, "arithmetic_o0");
  run_at_level(JitOptimizationLevel::o1, "arithmetic_o1");
  run_at_level(JitOptimizationLevel::o2, "arithmetic_o2");
  test_scalar_truth_tables_and_64_bits();
  test_systemverilog_scalar_transport_at_level(
      JitOptimizationLevel::o0, "systemverilog_scalar_o0");
  test_systemverilog_scalar_transport_at_level(
      JitOptimizationLevel::o2, "systemverilog_scalar_o2");
  test_wide_register_frame_at_level(
      JitOptimizationLevel::o0, "wide_register_frame_o0");
  test_wide_register_frame_at_level(
      JitOptimizationLevel::o2, "wide_register_frame_o2");
  test_wide_transient_register_frame_at_level(
      JitOptimizationLevel::o0, "wide_transient_register_frame_o0");
  test_wide_transient_register_frame_at_level(
      JitOptimizationLevel::o2, "wide_transient_register_frame_o2");
  test_optimized_frame_initialization_elision_at_level(
      JitOptimizationLevel::o0, "optimized_frame_initialization_o0");
  test_optimized_frame_initialization_elision_at_level(
      JitOptimizationLevel::o2, "optimized_frame_initialization_o2");
  test_wide_signal_read_at_level(
      JitOptimizationLevel::o0, "wide_signal_read_o0");
  test_wide_signal_read_at_level(
      JitOptimizationLevel::o2, "wide_signal_read_o2");
  test_wide_signal_write_at_level(
      JitOptimizationLevel::o0, "wide_signal_write_o0");
  test_wide_signal_write_at_level(
      JitOptimizationLevel::o2, "wide_signal_write_o2");
  test_wide_container_operations_at_level(
      JitOptimizationLevel::o0, "wide_container_operations_o0");
  test_wide_container_operations_at_level(
      JitOptimizationLevel::o2, "wide_container_operations_o2");
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o0, "fused_container_object_read_o0");
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o2, "fused_container_object_read_o2");
  test_wide_value_operations_at_level(
      JitOptimizationLevel::o0, "wide_value_operations_o0");
  test_wide_value_operations_at_level(
      JitOptimizationLevel::o2, "wide_value_operations_o2");
  test_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o0, "constant_dynamic_part_select_o0");
  test_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o2, "constant_dynamic_part_select_o2");
  test_logic4_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o0, "logic4_constant_dynamic_part_select_o0");
  test_logic4_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o2, "logic4_constant_dynamic_part_select_o2");
  test_affine_dynamic_extract_fusion_at_level(
      JitOptimizationLevel::o0, "affine_dynamic_extract_fusion_o0");
  test_affine_dynamic_extract_fusion_at_level(
      JitOptimizationLevel::o2, "affine_dynamic_extract_fusion_o2");
  test_fused_dynamic_part_signal_read_at_level(
      JitOptimizationLevel::o0, "fused_dynamic_part_signal_read_o0");
  test_fused_dynamic_part_signal_read_at_level(
      JitOptimizationLevel::o2, "fused_dynamic_part_signal_read_o2");
  test_conditional_select_at_level(
      JitOptimizationLevel::o0, "conditional_select_o0");
  test_conditional_select_at_level(
      JitOptimizationLevel::o2, "conditional_select_o2");
  test_wildcard_case_matching_at_level(
      JitOptimizationLevel::o0, "wildcard_case_o0");
  test_wildcard_case_matching_at_level(
      JitOptimizationLevel::o2, "wildcard_case_o2");
  test_comparisons_at_level(
      JitOptimizationLevel::o0, "comparisons_o0");
  test_comparisons_at_level(
      JitOptimizationLevel::o2, "comparisons_o2");
  test_logical_binary_at_level(
      JitOptimizationLevel::o0, "logical_binary_o0");
  test_logical_binary_at_level(
      JitOptimizationLevel::o2, "logical_binary_o2");
  test_reduction_and_shift_at_level(
      JitOptimizationLevel::o0, "reduction_shift_o0");
  test_reduction_and_shift_at_level(
      JitOptimizationLevel::o2, "reduction_shift_o2");
  test_signed_shift_counts_at_level(
      JitOptimizationLevel::o0, "signed_shift_counts_o0");
  test_signed_shift_counts_at_level(
      JitOptimizationLevel::o2, "signed_shift_counts_o2");
  test_unsigned_arithmetic_at_level(
      JitOptimizationLevel::o0, "unsigned_arithmetic_o0");
  test_unsigned_arithmetic_at_level(
      JitOptimizationLevel::o2, "unsigned_arithmetic_o2");
  test_signed_arithmetic_at_level(
      JitOptimizationLevel::o0, "signed_arithmetic_o0");
  test_signed_arithmetic_at_level(
      JitOptimizationLevel::o2, "signed_arithmetic_o2");
  test_extract_and_concatenate_at_level(
      JitOptimizationLevel::o0, "extract_concatenate_o0");
  test_extract_and_concatenate_at_level(
      JitOptimizationLevel::o2, "extract_concatenate_o2");
  test_insert_and_partial_writes_at_level(
      JitOptimizationLevel::o0, "insert_partial_writes_o0");
  test_insert_and_partial_writes_at_level(
      JitOptimizationLevel::o2, "insert_partial_writes_o2");
  test_dynamic_packed_indices_at_level(
      JitOptimizationLevel::o0, "dynamic_packed_indices_o0");
  test_dynamic_packed_indices_at_level(
      JitOptimizationLevel::o2, "dynamic_packed_indices_o2");
  test_initialized_bval_slot(JitOptimizationLevel::o0, "initialized_bval_o0");
  test_initialized_bval_slot(JitOptimizationLevel::o2, "initialized_bval_o2");
  test_direct_signal_read_at_level(
      JitOptimizationLevel::o0, "direct_signal_read_o0");
  test_direct_signal_read_at_level(
      JitOptimizationLevel::o2, "direct_signal_read_o2");
  test_direct_update_accumulator_at_level(
      JitOptimizationLevel::o0, "direct_update_accumulator_o0");
  test_direct_update_accumulator_at_level(
      JitOptimizationLevel::o2, "direct_update_accumulator_o2");
  test_static_trigger_regions_at_level(
      JitOptimizationLevel::o0, "static_trigger_regions_o0");
  test_static_trigger_regions_at_level(
      JitOptimizationLevel::o2, "static_trigger_regions_o2");
  test_debug_point_instrumentation();
  test_control_flow_at_level(JitOptimizationLevel::o0, "control_flow_o0");
  test_control_flow_at_level(JitOptimizationLevel::o2, "control_flow_o2");
  test_native_callable_regions();
  test_checked_integer_at_level(
      JitOptimizationLevel::o0, "checked_integer_o0");
  test_checked_integer_at_level(
      JitOptimizationLevel::o2, "checked_integer_o2");
  test_scheduled_callbacks_at_level(
      JitOptimizationLevel::o0, "scheduled_o0");
  test_scheduled_callbacks_at_level(
      JitOptimizationLevel::o2, "scheduled_o2");
  test_inertial_callbacks_at_level(
      JitOptimizationLevel::o0, "inertial_o0");
  test_inertial_callbacks_at_level(
      JitOptimizationLevel::o2, "inertial_o2");
  test_projected_callbacks_at_level(
      JitOptimizationLevel::o0, "projected_o0");
  test_projected_callbacks_at_level(
      JitOptimizationLevel::o2, "projected_o2");
  test_scheduling_differential_at_level(
      JitOptimizationLevel::o0, "scheduled_diff_o0");
  test_scheduling_differential_at_level(
      JitOptimizationLevel::o2, "scheduled_diff_o2");
  test_resumable_at_level(JitOptimizationLevel::o0, "resume_o0");
  test_resumable_at_level(JitOptimizationLevel::o2, "resume_o2");
  test_process_cohort_resume_at_level(
      JitOptimizationLevel::o0, "cohort_resume_o0");
  test_process_cohort_resume_at_level(
      JitOptimizationLevel::o2, "cohort_resume_o2");
  test_class_service_boundaries_at_level(
      JitOptimizationLevel::o0, "class_boundary_o0");
  test_class_service_boundaries_at_level(
      JitOptimizationLevel::o2, "class_boundary_o2");
  test_signal_waits_at_level(
      JitOptimizationLevel::o0, "signal_wait_o0");
  test_signal_waits_at_level(
      JitOptimizationLevel::o2, "signal_wait_o2");
  test_display_at_level(
      JitOptimizationLevel::o0, "display_o0");
  test_display_at_level(
      JitOptimizationLevel::o2, "display_o2");
  test_strings_at_level(
      JitOptimizationLevel::o0, "strings_o0");
  test_strings_at_level(
      JitOptimizationLevel::o2, "strings_o2");
  test_logic9_at_level(
      JitOptimizationLevel::o0, "logic9_o0");
  test_logic9_at_level(
      JitOptimizationLevel::o2, "logic9_o2");
  test_vital_timing_at_level(
      JitOptimizationLevel::o0, "vital_timing_o0");
  test_vital_timing_at_level(
      JitOptimizationLevel::o2, "vital_timing_o2");
  test_vital_delay_at_level(
      JitOptimizationLevel::o0, "vital_delay_o0");
  test_vital_delay_at_level(
      JitOptimizationLevel::o2, "vital_delay_o2");
  test_persistent_object_cache();
  test_process_control_cache_identity();
  test_rejections();
  std::cout << "LLVM JIT tests passed with LLVM " << LlvmJit::llvm_version()
            << '\n';
}

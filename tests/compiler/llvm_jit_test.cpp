// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

int main() {
  using namespace fsim::tests::compiler;
  assert(!LlvmJit::llvm_version().empty());
  run_at_level(JitOptimizationLevel::o0, "arithmetic_o0");
  run_at_level(JitOptimizationLevel::o2, "arithmetic_o2");
  test_scalar_truth_tables_and_64_bits();
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
  test_debug_point_instrumentation();
  test_control_flow_at_level(JitOptimizationLevel::o0, "control_flow_o0");
  test_control_flow_at_level(JitOptimizationLevel::o2, "control_flow_o2");
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
  test_persistent_object_cache();
  test_process_control_cache_identity();
  test_rejections();
  std::cout << "LLVM JIT tests passed with LLVM " << LlvmJit::llvm_version()
            << '\n';
}

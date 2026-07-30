// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main() {
  using namespace fsim::tests::runtime;
  try {
    test_logic();
    test_packed_values();
    test_scheduler_phase_order();
    test_scheduler_stop_resume();
    test_scheduler_time_limit_before_future_event();
    test_scheduler_safe_point_scheduling();
    test_scheduler_delta_limit();
    test_simir();
    test_simir_permanent_wait();
    test_simir_update_coalescing();
    test_resolved_driver_slots();
    test_simir_expressions_and_edges();
    test_simir_noninitializing_static_process();
    test_simir_wide_truth_and_comparison();
    test_simir_wildcard_case_matching();
    test_simir_wildcard_equality();
    test_simir_wide_reduction_and_shift();
    test_simir_signed_shift_counts();
    test_simir_wide_unsigned_arithmetic();
    test_simir_wide_signed_arithmetic();
    test_checked_vhdl_integer_operations();
    test_simir_wide_extract_and_concatenate();
    test_simir_insert_and_partial_writes();
    test_simir_dynamic_packed_indices();
    test_simir_force_release();
    test_simir_design_stop_identity();
    test_simir_pause_resume_lifecycle();
    test_simir_final_process_lifecycle();
    test_simir_alternate_executor_context_and_boundaries();
    test_simir_alternate_executor_dynamic_wait();
    test_simir_timed_dynamic_wait_rearm();
    test_simir_nested_calls();
    test_simir_mutable_strings();
    test_simir_alternate_executor_scheduled_word_writes();
    test_simir_alternate_executor_zero_delay_and_frame();
    test_simir_alternate_executor_cpp_exception_containment();
    test_simir_alternate_executor_validation();
    test_simir_alternate_executor_event_replacement_and_cancel();
    test_simir_alternate_executor_notify_delayed();
    test_simir_alternate_executor_primitive_channel_updates();
    test_simir_alternate_executor_signal_event_window();
    test_simir_alternate_executor_event_lists();
    test_simir_assertion_metadata();
    test_simir_execution_point_ordering();
    test_simir_display_output();
    test_deterministic_random_values();
    test_transition_delay_selection();
    test_simir_inertial_transition_writes();
    test_simir_projected_writes();
    test_vcd();
  } catch (const std::exception& error) {
    std::cerr << "runtime test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "runtime tests passed\n";
  return EXIT_SUCCESS;
}

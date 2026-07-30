// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::tests::runtime {

void test_logic();
void test_packed_values();
void test_scheduler_phase_order();
void test_scheduler_stop_resume();
void test_scheduler_time_limit_before_future_event();
void test_scheduler_safe_point_scheduling();
void test_scheduler_delta_limit();
void test_simir();
void test_simir_permanent_wait();
void test_simir_update_coalescing();
void test_resolved_driver_slots();
void test_simir_expressions_and_edges();
void test_simir_noninitializing_static_process();
void test_simir_wide_truth_and_comparison();
void test_simir_wildcard_case_matching();
void test_simir_wildcard_equality();
void test_simir_wide_reduction_and_shift();
void test_simir_signed_shift_counts();
void test_simir_wide_unsigned_arithmetic();
void test_simir_wide_signed_arithmetic();
void test_checked_vhdl_integer_operations();
void test_simir_wide_extract_and_concatenate();
void test_simir_insert_and_partial_writes();
void test_simir_dynamic_packed_indices();
void test_simir_force_release();
void test_simir_design_stop_identity();
void test_simir_pause_resume_lifecycle();
void test_simir_final_process_lifecycle();
void test_simir_alternate_executor_context_and_boundaries();
void test_simir_alternate_executor_dynamic_wait();
void test_simir_timed_dynamic_wait_rearm();
void test_simir_nested_calls();
void test_simir_mutable_strings();
void test_simir_text_files();
void test_simir_containers();
void test_simir_alternate_executor_scheduled_word_writes();
void test_simir_alternate_executor_zero_delay_and_frame();
void test_simir_alternate_executor_cpp_exception_containment();
void test_simir_alternate_executor_validation();
void test_simir_alternate_executor_event_replacement_and_cancel();
void test_simir_alternate_executor_notify_delayed();
void test_simir_alternate_executor_primitive_channel_updates();
void test_simir_alternate_executor_signal_event_window();
void test_simir_alternate_executor_event_lists();
void test_simir_assertion_metadata();
void test_simir_execution_point_ordering();
void test_simir_display_output();
void test_deterministic_random_values();
void test_transition_delay_selection();
void test_simir_inertial_transition_writes();
void test_simir_projected_writes();
void test_vcd();

} // namespace fsim::tests::runtime

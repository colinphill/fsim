// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main() {
  using namespace fsim::tests::runtime;
  try {
    // FSIM-CONFORMANCE CF-COMMON-SCHEDULER-001 source=SRC-COCOTB expectation=execute
    // FSIM-CONFORMANCE CF-COMMON-SIMIR-001 source=SRC-FSIM expectation=execute
    // FSIM-CONFORMANCE CF-COMMON-FAILURE-001 source=SRC-COCOTB expectation=contain
    // FSIM-CONFORMANCE CF-COMMON-VCD-001 source=SRC-COCOTB expectation=execute
    test_logic();
    test_packed_values();
    test_systemverilog_scalar_values();
    test_systemverilog_dpi_scalar_marshalling();
    test_systemverilog_dpi_real_string_chandle_marshalling();
    test_systemverilog_dpi_composite_marshalling();
  test_systemverilog_dpi_open_arrays();
  test_systemverilog_dpi_scopes();
  test_systemverilog_dpi_callbacks();
  test_systemverilog_dpi_tasks();
  test_systemverilog_dpi_plugin_planning();
  test_systemverilog_dpi_engine_differential();
  test_vhdl_vhpi_host_abi();
  test_vhdl_vhpi_error_and_handles();
  test_vhdl_vhpi_hierarchy_and_names();
  test_vhdl_vhpi_types_and_constraints();
  test_vhdl_vhpi_scalar_values();
  test_vhdl_vhpi_composite_values();
  test_vhdl_vhpi_special_values();
  test_vhdl_vhpi_drivers();
  test_vhdl_vhpi_writes();
  test_vhdl_vhpi_time();
  test_vhdl_vhpi_callbacks();
  test_vhdl_vhpi_foreign();
  test_vhdl_vhpi_associations();
  test_vhdl_vhpi_io();
  test_vhdl_vhpi_checkpoint_restart_and_artifact();
  test_vhdl_vhpi_reference_plugins();
  test_vhdl_vhpi_negative_matrix();
  test_systemverilog_vpi_host_abi();
  test_systemverilog_vpi_objects_and_errors();
  test_systemverilog_vpi_scalar_type_properties();
  test_systemverilog_vpi_recursive_type_descriptors();
  test_systemverilog_vpi_checked_value_reads();
  test_systemverilog_vpi_checked_writes();
  test_systemverilog_vpi_time_service();
  test_systemverilog_vpi_callbacks();
  test_systemverilog_vpi_callback_lifecycle();
  test_systemverilog_vpi_checkpoint_restart_and_artifact();
  test_systemverilog_vpi_reference_plugins();
  test_systemverilog_vpi_control();
  test_systemverilog_vpi_reset_and_finish_control();
  test_systemverilog_vpi_io_descriptors();
  test_systemverilog_vpi_io_diagnostics_and_teardown();
  test_systemverilog_vpi_system_registration_and_execution();
  test_systemverilog_vpi_system_failures();
  test_systemverilog_vpi_system_handles_and_reentry();
  test_systemverilog_vpi_system_registration_lifecycle();
    test_systemverilog_scalar_text_and_time();
    test_systemverilog_scalar_execution_surfaces();
    test_systemverilog_chandle_registry();
    test_systemverilog_unicode_strings();
    test_scheduler_phase_order();
    test_scheduler_stop_resume();
    test_scheduler_ownership_and_failure_containment();
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
    test_simir_vhdl_matching_equality();
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
    test_simir_fork_process_lifecycle();
    test_simir_synchronization_objects();
    test_simir_alternate_executor_context_and_boundaries();
    test_simir_alternate_executor_dynamic_wait();
    test_simir_timed_dynamic_wait_rearm();
    test_simir_nested_calls();
    test_simir_mutable_strings();
    test_simir_text_files();
    test_simir_containers();
    test_systemverilog_class_heap();
    test_systemverilog_class_methods();
    test_systemverilog_constraint_solver();
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
    test_simir_runtime_value_delays();
    test_simir_inertial_transition_writes();
    test_simir_module_paths();
    test_simir_module_timing_checks();
    test_simir_projected_writes();
    test_vital_timing_checks();
    test_vital_delay_scheduling();
    test_vital_memory_declaration();
    test_vital_memory_path_delays();
    test_vcd();
  } catch (const std::exception& error) {
    std::cerr << "runtime test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "runtime tests passed\n";
  return EXIT_SUCCESS;
}

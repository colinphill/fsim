// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <iostream>

int main() {
  using namespace fsim::tests::elaboration;
  test_specialization_and_packages();
  test_systemverilog_typed_constants();
  test_systemverilog_string_constants();
  test_systemverilog_type_parameters();
  test_generate_elaboration();
  test_mixed_language_and_systemc();
  test_process_and_wait_lowering();
  test_systemverilog_fork_lowering();
  test_systemverilog_function_lowering();
  test_systemverilog_task_lowering();
  test_systemverilog_file_lowering();
  test_systemverilog_container_lowering();
  test_systemverilog_aggregate_containers();
  test_systemverilog_static_slice_calls();
  test_systemverilog_static_slice_ordering();
  test_systemverilog_static_slice_ports();
  test_systemverilog_interfaces();
  test_case_and_expression_lowering();
  test_systemverilog_case_qualifiers();
  test_systemverilog_case_matches();
  test_systemverilog_case_inside_lowering();
  test_systemverilog_membership_lowering();
  test_numeric_and_system_function_lowering();
  test_selection_and_assignment_lowering();
  test_assertion_types_and_random_lowering();
  test_vhdl_interface_type_generics();
  test_vhdl_interface_function_generics();
  test_vhdl_callable_overloads();
  test_vhdl_interface_procedure_generics();
  test_vhdl_interface_package_generics();
  test_vhdl_generic_subprograms();
  test_vhdl_components();
  test_vhdl_configurations();
  std::cout << "elaborator tests passed\n";
}

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
  test_systemverilog_function_lowering();
  test_case_and_expression_lowering();
  test_numeric_and_system_function_lowering();
  test_selection_and_assignment_lowering();
  test_assertion_types_and_random_lowering();
  test_vhdl_interface_type_generics();
  std::cout << "elaborator tests passed\n";
}

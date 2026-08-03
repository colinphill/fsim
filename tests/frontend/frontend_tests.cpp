// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

} // namespace

int main() {
  using namespace fsim::frontend;
  using namespace fsim::tests::frontend;
  try {
    require(
        PackedRange{
            std::numeric_limits<std::int64_t>::max(),
            std::numeric_limits<std::int64_t>::min(),
            true}
                .width()
            == 0,
        "unrepresentable 2^64-element range must not overflow");
    test_vhdl_vertical_slice();
    test_vhdl_falling_edge_guard();
    test_vhdl_instance_diagnostics();
    test_vhdl_generics();
    test_vhdl_record_types();
    test_vhdl_select_and_concatenation_expressions();
    test_signed_type_and_expression_nodes();
    test_vhdl_runtime_integer_nodes();
    test_vhdl_subtype_declarations();
    test_vhdl_array_type_declarations();
    test_vhdl_access_protected_physical_hir();
    test_vhdl_nested_composite_hir();
    test_vhdl_enumeration_declarations();
    test_vhdl_enumeration_attributes();
    test_vhdl_enumeration_subtype_ranges();
    test_vhdl_case_generate_enumeration_choices();
    test_exponentiation_expression_nodes();
    test_systemverilog_procedural_updates();
    test_systemverilog_final_procedures();
    test_verilog_stop_task();
    test_vhdl_conditional_assignments();
    test_vhdl_array_attributes();
    test_vhdl_selected_assignments();
    test_vhdl_delay_mechanisms();
    test_vhdl_ordered_waveforms();
    test_vhdl_batch119_retained_surface();
    test_vhdl_case_statements();
    test_vhdl_sequential_for_loops();
    test_systemverilog_vertical_slice();
    test_systemverilog_preprocessor();
    test_systemverilog_public_conformance_frontend();
    test_systemverilog_line_directive();
    test_non_ansi_verilog_ports();
    test_diagnostics_and_spans();
    test_vhdl_context_diagnostics();
    test_vhdl_package_constants();
    test_ignored_initializers_are_rejected();
    test_duplicate_declarations_are_rejected();
    test_systemverilog_timescale_context();
    test_systemverilog_time_declarations();
    test_systemverilog_delay_triples();
    test_systemverilog_procedural_assignment_controls();
    test_systemverilog_compiler_directives();
    test_systemverilog_parameters();
    test_systemverilog_packages();
    test_systemverilog_interfaces();
    test_vhdl_function_declarations();
    test_vhdl_procedure_declarations();
    test_vhdl_generic_subprogram_declarations();
    test_vhdl_component_declarations();
    test_vhdl_configurations();
    test_vhdl_package_generics();
    test_systemverilog_function_declarations();
    test_systemverilog_task_declarations();
    test_systemverilog_callable_closure_declarations();
    test_immediate_assertions();
    test_vhdl_literal_report();
    test_process_variable_declarations();
    test_systemverilog_procedural_block_scopes();
    test_procedural_wait_statements();
    test_wildcard_and_always_comb_processes();
    test_systemverilog_case_statements();
    test_systemverilog_procedural_for_loops();
    test_verilog_repeat_statements();
    test_runtime_loop_statements();
    test_loop_control_statements();
    test_systemverilog_do_while_statements();
    test_fork_process_statements();
    test_systemverilog_conditional_expression();
    test_systemverilog_comparison_expressions();
    test_systemverilog_membership_expressions();
    test_systemverilog_arithmetic_expressions();
    test_gate_primitives();
    test_systemverilog_select_and_concatenation_expressions();
    test_conditional_statement_trees();
    test_conditional_generate_hierarchy();
    test_systemverilog_named_events();
    test_verilog_literal_display();
    test_systemverilog_random_functions();
    test_systemverilog_text_files();
    test_systemverilog_containers();
    std::cout << "frontend tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "frontend test failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <iostream>

namespace {

void test_process_program_ownership_transfer()
{
    const auto parsed = fsim::frontend::parse_text(
        "process_ownership_transfer.sv",
        "module process_ownership_transfer; initial begin #1; end endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "process_ownership_transfer");
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->processes().size() == 1U);
    const auto process_name = elaborated.design->processes().front().name;
    const auto operation_count
        = elaborated.design->processes().front().operations.size();

    auto interpreter
        = std::move(*elaborated.design).create_interpreter();
    assert(elaborated.design->processes().empty());
    assert(interpreter->process_program(0U).name == process_name);
    assert(interpreter->process_program(0U).operations.size()
        == operation_count);
    const auto result = interpreter->run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    assert(result.time == 1U);
}

} // namespace

int main()
{
    using namespace fsim::tests::elaboration;
    test_process_program_ownership_transfer();
    test_specialization_and_packages();
    test_verilog_specify_specialization();
    test_systemverilog_typed_constants();
    test_systemverilog_constant_function_memoization_dependencies();
    test_systemverilog_aliases();
    test_systemverilog_string_constants();
    test_systemverilog_type_parameters();
    test_systemverilog_hierarchy_configuration();
    test_generate_elaboration();
    test_verilog_defparam_elaboration();
    test_mixed_language_and_systemc();
    test_multi_library_resolution();
    test_verilog_udp_resolution();
    test_verilog_strength_hierarchy();
    test_multiple_root_elaboration();
    test_mixed_language_conversions();
    test_mixed_language_construction();
    test_mixed_language_driver_ownership();
    test_process_and_wait_lowering();
    test_systemverilog_fork_lowering();
    test_systemverilog_function_lowering();
    test_systemverilog_task_lowering();
    test_systemverilog_file_lowering();
    test_systemverilog_container_lowering();
    test_systemverilog_cross_language_container_bridge();
    test_systemverilog_composite_container_types();
    test_systemverilog_aggregate_containers();
    test_systemverilog_static_slice_calls();
    test_systemverilog_static_slice_ordering();
    test_systemverilog_static_slice_ports();
    test_systemverilog_interfaces();
    test_systemverilog_program_instances();
    test_systemverilog_public_conformance_elaboration();
    test_msvc_debug_elaboration_portability();
    test_case_and_expression_lowering();
    test_systemverilog_case_qualifiers();
    test_systemverilog_case_matches();
    test_systemverilog_case_inside_lowering();
    test_systemverilog_membership_lowering();
    test_numeric_and_system_function_lowering();
    test_selection_and_assignment_lowering();
    test_vhdl_dynamic_slices();
    test_vhdl_recursive_composite_layout();
    test_vhdl_revision_expression_elaboration();
    test_vhdl_access_type_storage();
    test_vhdl_protected_type_storage();
    test_vhdl_physical_type_execution();
    test_vhdl_qualified_expressions_and_conversions();
    test_vhdl_aggregate_choice_closure();
    test_vhdl_attribute_closure();
    test_vhdl_composite_operation_closure();
    test_assertion_types_and_random_lowering();
    test_vhdl_interface_type_generics();
    test_vhdl_interface_function_generics();
    test_vhdl_callable_overloads();
    test_vhdl_interface_procedure_generics();
    test_vhdl_procedure_waits();
    test_vhdl_interface_package_generics();
    test_vhdl_predefined_environment_profiles();
    test_vhdl_generic_subprograms();
    test_vhdl_generic_associations();
    test_vhdl_components();
    test_vhdl_configurations();
    test_vhdl_matching_statements();
    test_vhdl_discrete_case_choices();
    test_vhdl_concurrent_assignments();
    std::cout << "elaborator tests passed\n";
}

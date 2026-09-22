// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void test_process_program_ownership_transfer()
{
    const auto parsed = fsim::frontend::parse_text(
        "process_ownership_transfer.sv",
        "module process_ownership_transfer; initial begin #1; end endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    auto elaborated = fsim::tests::elaboration::compile_and_elaborate(
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
    const auto* selected = std::getenv("FSIM_ELABORATION_TEST");
    std::size_t executed { };
    const auto run = [&](const std::string_view name, const auto function) {
        if (selected == nullptr || name == selected) {
            ++executed;
            function();
        }
    };
#define FSIM_RUN_TEST(function) run(#function, function)
    FSIM_RUN_TEST(test_process_program_ownership_transfer);
    FSIM_RUN_TEST(test_specialization_and_packages);
    FSIM_RUN_TEST(test_verilog_specify_specialization);
    FSIM_RUN_TEST(test_systemverilog_typed_constants);
    FSIM_RUN_TEST(
        test_systemverilog_constant_function_memoization_dependencies);
    FSIM_RUN_TEST(test_systemverilog_2023_utility_system_callables);
    FSIM_RUN_TEST(test_systemverilog_aliases);
    FSIM_RUN_TEST(test_systemverilog_string_constants);
    FSIM_RUN_TEST(test_systemverilog_type_parameters);
    FSIM_RUN_TEST(test_systemverilog_hierarchy_configuration);
    FSIM_RUN_TEST(test_generate_elaboration);
    FSIM_RUN_TEST(test_verilog_defparam_elaboration);
    FSIM_RUN_TEST(test_mixed_language_and_systemc);
    FSIM_RUN_TEST(test_multi_library_resolution);
    FSIM_RUN_TEST(test_verilog_udp_resolution);
    FSIM_RUN_TEST(test_verilog_strength_hierarchy);
    FSIM_RUN_TEST(test_multiple_root_elaboration);
    FSIM_RUN_TEST(test_mixed_language_conversions);
    FSIM_RUN_TEST(test_mixed_language_construction);
    FSIM_RUN_TEST(test_mixed_language_driver_ownership);
    FSIM_RUN_TEST(test_process_and_wait_lowering);
    FSIM_RUN_TEST(test_direct_hir_lowering);
    FSIM_RUN_TEST(test_systemverilog_fork_lowering);
    FSIM_RUN_TEST(test_systemverilog_function_lowering);
    FSIM_RUN_TEST(test_systemverilog_task_lowering);
    FSIM_RUN_TEST(test_systemverilog_file_lowering);
    FSIM_RUN_TEST(test_systemverilog_container_lowering);
    FSIM_RUN_TEST(
        test_systemverilog_cross_language_container_bridge);
    FSIM_RUN_TEST(test_systemverilog_composite_container_types);
    FSIM_RUN_TEST(test_systemverilog_aggregate_containers);
    FSIM_RUN_TEST(test_systemverilog_static_slice_calls);
    FSIM_RUN_TEST(test_systemverilog_static_slice_ordering);
    FSIM_RUN_TEST(test_systemverilog_static_slice_ports);
    FSIM_RUN_TEST(test_systemverilog_interfaces);
    FSIM_RUN_TEST(test_systemverilog_program_instances);
    FSIM_RUN_TEST(
        test_systemverilog_public_conformance_elaboration);
    FSIM_RUN_TEST(test_msvc_debug_elaboration_portability);
    FSIM_RUN_TEST(test_case_and_expression_lowering);
    FSIM_RUN_TEST(test_systemverilog_case_qualifiers);
    FSIM_RUN_TEST(test_systemverilog_case_matches);
    FSIM_RUN_TEST(test_systemverilog_case_inside_lowering);
    FSIM_RUN_TEST(test_systemverilog_membership_lowering);
    FSIM_RUN_TEST(test_numeric_and_system_function_lowering);
    FSIM_RUN_TEST(test_selection_and_assignment_lowering);
    FSIM_RUN_TEST(test_vhdl_dynamic_slices);
    FSIM_RUN_TEST(test_vhdl_recursive_composite_layout);
    FSIM_RUN_TEST(test_vhdl_revision_expression_elaboration);
    FSIM_RUN_TEST(test_vhdl_access_type_storage);
    FSIM_RUN_TEST(test_vhdl_protected_type_storage);
    FSIM_RUN_TEST(test_vhdl_physical_type_execution);
    FSIM_RUN_TEST(test_vhdl_qualified_expressions_and_conversions);
    FSIM_RUN_TEST(test_vhdl_aggregate_choice_closure);
    FSIM_RUN_TEST(test_vhdl_attribute_closure);
    FSIM_RUN_TEST(test_vhdl_composite_operation_closure);
    FSIM_RUN_TEST(test_assertion_types_and_random_lowering);
    FSIM_RUN_TEST(test_vhdl_interface_type_generics);
    FSIM_RUN_TEST(test_vhdl_interface_function_generics);
    FSIM_RUN_TEST(test_vhdl_callable_overloads);
    FSIM_RUN_TEST(test_vhdl_interface_procedure_generics);
    FSIM_RUN_TEST(test_vhdl_procedure_waits);
    FSIM_RUN_TEST(test_vhdl_interface_package_generics);
    FSIM_RUN_TEST(test_vhdl_predefined_environment_profiles);
    FSIM_RUN_TEST(test_vhdl_generic_subprograms);
    FSIM_RUN_TEST(test_vhdl_generic_associations);
    FSIM_RUN_TEST(test_vhdl_components);
    FSIM_RUN_TEST(test_vhdl_configurations);
    FSIM_RUN_TEST(test_vhdl_matching_statements);
    FSIM_RUN_TEST(test_vhdl_discrete_case_choices);
    FSIM_RUN_TEST(test_vhdl_concurrent_assignments);
#undef FSIM_RUN_TEST
    if (executed == 0U) {
        std::cerr << "unknown elaboration test filter: " << selected << '\n';
        return 2;
    }
    std::cout << "elaborator tests passed\n";
}

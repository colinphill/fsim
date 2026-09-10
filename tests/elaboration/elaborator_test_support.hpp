// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::tests::elaboration {

[[nodiscard]] bool has_diagnostic(
    const fsim::elaboration::ElaborationResult& result,
    std::string_view code);

class TestSystemCFactoryProvider final
    : public fsim::elaboration::SystemCFactoryProvider {
public:
    std::vector<fsim::elaboration::SystemCConstructionParameter>
        parameters;
    fsim::elaboration::SystemCInstanceDescription prototype;
    std::vector<std::pair<std::string, std::int64_t>>
        last_values;
    std::uint64_t next_handle { 10'000 };
    std::string schema_failure;
    std::string construction_failure;
    std::vector<fsim::elaboration::SystemCFactoryCandidate>
        factory_candidates;
    std::vector<std::string> available_libraries;

    std::vector<fsim::elaboration::SystemCFactoryCandidate>
    candidates() const override;

    std::vector<std::string> libraries() const override;

    std::optional<std::vector<
        fsim::elaboration::SystemCConstructionParameter>>
    schema(
        std::string_view, std::string& error) override;

    std::optional<fsim::elaboration::SystemCInstanceDescription>
    instantiate(
        const std::string_view path,
        const std::string_view target,
        const std::span<
            const std::pair<std::string, std::int64_t>>
            values,
        std::string& error) override;
};

void test_specialization_and_packages();
void test_verilog_specify_specialization();
void test_systemverilog_typed_constants();
void test_systemverilog_constant_function_memoization_dependencies();
void test_systemverilog_2023_utility_system_callables();
void test_systemverilog_aliases();
void test_systemverilog_string_constants();
void test_systemverilog_type_parameters();
void test_systemverilog_hierarchy_configuration();
void test_generate_elaboration();
void test_verilog_defparam_elaboration();
void test_mixed_language_and_systemc();
void test_multi_library_resolution();
void test_verilog_udp_resolution();
void test_verilog_strength_hierarchy();
void test_multiple_root_elaboration();
void test_mixed_language_conversions();
void test_mixed_language_construction();
void test_mixed_language_driver_ownership();
void test_process_and_wait_lowering();
void test_systemverilog_fork_lowering();
void test_systemverilog_function_lowering();
void test_systemverilog_task_lowering();
void test_systemverilog_file_lowering();
void test_systemverilog_container_lowering();
void test_systemverilog_cross_language_container_bridge();
void test_systemverilog_composite_container_types();
void test_systemverilog_aggregate_containers();
void test_systemverilog_static_slice_calls();
void test_systemverilog_static_slice_ordering();
void test_systemverilog_static_slice_ports();
void test_systemverilog_interfaces();
void test_systemverilog_program_instances();
void test_systemverilog_public_conformance_elaboration();
void test_msvc_debug_elaboration_portability();
void test_case_and_expression_lowering();
void test_systemverilog_case_qualifiers();
void test_systemverilog_case_matches();
void test_systemverilog_case_inside_lowering();
void test_systemverilog_membership_lowering();
void test_numeric_and_system_function_lowering();
void test_selection_and_assignment_lowering();
void test_vhdl_dynamic_slices();
void test_vhdl_recursive_composite_layout();
void test_vhdl_revision_expression_elaboration();
void test_vhdl_access_type_storage();
void test_vhdl_protected_type_storage();
void test_vhdl_physical_type_execution();
void test_vhdl_qualified_expressions_and_conversions();
void test_vhdl_aggregate_choice_closure();
void test_vhdl_attribute_closure();
void test_vhdl_composite_operation_closure();
void test_assertion_types_and_random_lowering();
void test_vhdl_interface_type_generics();
void test_vhdl_interface_function_generics();
void test_vhdl_callable_overloads();
void test_vhdl_interface_procedure_generics();
void test_vhdl_procedure_waits();
void test_vhdl_interface_package_generics();
void test_vhdl_predefined_environment_profiles();
void test_vhdl_generic_subprograms();
void test_vhdl_generic_associations();
void test_vhdl_components();
void test_vhdl_configurations();
void test_vhdl_matching_statements();
void test_vhdl_discrete_case_choices();
void test_vhdl_concurrent_assignments();

} // namespace fsim::tests::elaboration

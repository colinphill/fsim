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

inline bool has_diagnostic(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

class TestSystemCFactoryProvider final
    : public fsim::elaboration::SystemCFactoryProvider {
public:
    std::vector<fsim::elaboration::SystemCConstructionParameter>
        parameters;
    fsim::elaboration::SystemCInstanceDescription prototype;
    std::vector<std::pair<std::string, std::int64_t>>
        last_values;
    std::uint64_t next_handle{10'000};
    std::string schema_failure;
    std::string construction_failure;
    std::vector<fsim::elaboration::SystemCFactoryCandidate>
        factory_candidates;
    std::vector<std::string> available_libraries;

    std::vector<fsim::elaboration::SystemCFactoryCandidate>
    candidates() const override {
        return factory_candidates;
    }

    std::vector<std::string> libraries() const override {
        auto result = available_libraries;
        for (const auto& candidate : factory_candidates) {
            if (std::ranges::find(result, candidate.library)
                == result.end()) {
                result.push_back(candidate.library);
            }
        }
        return result;
    }

    std::optional<std::vector<
        fsim::elaboration::SystemCConstructionParameter>>
    schema(
        std::string_view,
        std::string& error) override {
        if (!schema_failure.empty()) {
            error = schema_failure;
            return std::nullopt;
        }
        error.clear();
        return parameters;
    }

    std::optional<fsim::elaboration::SystemCInstanceDescription>
    instantiate(
        const std::string_view path,
        const std::string_view target,
        const std::span<
            const std::pair<std::string, std::int64_t>> values,
        std::string& error) override {
        if (!construction_failure.empty()) {
            error = construction_failure;
            return std::nullopt;
        }
        error.clear();
        auto result = prototype;
        result.path = path;
        result.target = target;
        result.handle = next_handle++;
        result.construction_values.assign(
            values.begin(), values.end());
        last_values = result.construction_values;
        const auto width = std::find_if(
            values.begin(),
            values.end(),
            [](const auto& value) {
                return value.first == "WIDTH";
            });
        if (width != values.end() && width->second > 0) {
            for (auto& port : result.ports) {
                if (port.name == "value" && width->second > 1) {
                    port.type.packed_range =
                        fsim::frontend::PackedRange{
                            width->second - 1, 0, true};
                }
            }
        }
        return result;
    }
};

void test_specialization_and_packages();
void test_verilog_specify_specialization();
void test_systemverilog_typed_constants();
void test_systemverilog_string_constants();
void test_systemverilog_type_parameters();
void test_generate_elaboration();
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
void test_systemverilog_aggregate_containers();
void test_systemverilog_static_slice_calls();
void test_systemverilog_static_slice_ordering();
void test_systemverilog_static_slice_ports();
void test_systemverilog_interfaces();
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
void test_vhdl_generic_subprograms();
void test_vhdl_generic_associations();
void test_vhdl_components();
void test_vhdl_configurations();
void test_vhdl_matching_statements();
void test_vhdl_discrete_case_choices();
void test_vhdl_concurrent_assignments();

} // namespace fsim::tests::elaboration

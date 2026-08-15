// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <string_view>

using ApplicationCase = int (*)();

struct NamedApplicationCase {
    std::string_view name;
    ApplicationCase run;
};

#if FSIM_APPLICATION_SHARD == 0
int fsim_application_case_core();
int fsim_application_case_core_simulation();
int fsim_application_case_core_mixed();
int fsim_application_case_core_multiple_roots();
int fsim_application_case_core_preprocessing_cli();
int fsim_application_case_core_non_project_cli();
int fsim_application_case_specialization();
int fsim_application_case_artifact_phases();
int fsim_application_case_classes();
int fsim_application_case_systemc_matrix();
int fsim_application_case_expressions();
int fsim_application_case_sv_hierarchy();
int fsim_application_case_scoped_locals();
int fsim_application_case_specify();
int fsim_application_case_call_safe_points();
int fsim_application_case_named_events();
int fsim_application_case_fork();
int fsim_application_case_display();
int fsim_application_case_random();
int fsim_application_case_plusargs();
int fsim_application_case_system_command();
int fsim_application_case_stochastic_queues();
int fsim_application_case_pla();
int fsim_application_case_vcd_control();
int fsim_application_case_coverage();
int fsim_application_case_assertions();
int fsim_application_case_line_directives();
int fsim_application_case_time();
int fsim_application_case_delay_modes();
int fsim_application_case_transition_delays();
int fsim_application_case_procedural_assignments();
int fsim_application_case_resolution();

constexpr NamedApplicationCase application_cases[] = {
    { "core", fsim_application_case_core },
    { "core_simulation", fsim_application_case_core_simulation },
    { "core_mixed", fsim_application_case_core_mixed },
    { "core_multiple_roots", fsim_application_case_core_multiple_roots },
    { "core_preprocessing_cli",
        fsim_application_case_core_preprocessing_cli },
    { "core_non_project_cli", fsim_application_case_core_non_project_cli },
    { "specialization", fsim_application_case_specialization },
    { "artifact_phases", fsim_application_case_artifact_phases },
    { "classes", fsim_application_case_classes },
    { "systemc_matrix", fsim_application_case_systemc_matrix },
    { "expressions", fsim_application_case_expressions },
    { "sv_hierarchy", fsim_application_case_sv_hierarchy },
    { "scoped_locals", fsim_application_case_scoped_locals },
    { "specify", fsim_application_case_specify },
    { "call_safe_points", fsim_application_case_call_safe_points },
    { "named_events", fsim_application_case_named_events },
    { "fork", fsim_application_case_fork },
    { "display", fsim_application_case_display },
    { "random", fsim_application_case_random },
    { "plusargs", fsim_application_case_plusargs },
    { "system_command", fsim_application_case_system_command },
    { "stochastic_queues", fsim_application_case_stochastic_queues },
    { "pla", fsim_application_case_pla },
    { "vcd_control", fsim_application_case_vcd_control },
    { "coverage", fsim_application_case_coverage },
    { "assertions", fsim_application_case_assertions },
    { "line_directives", fsim_application_case_line_directives },
    { "time", fsim_application_case_time },
    { "delay_modes", fsim_application_case_delay_modes },
    { "transition_delays", fsim_application_case_transition_delays },
    { "procedural_assignments", fsim_application_case_procedural_assignments },
    { "resolution", fsim_application_case_resolution },
};
#elif FSIM_APPLICATION_SHARD == 1
int fsim_application_case_sv_parameter_sizing();
int fsim_application_case_sv_type_parameters();
int fsim_application_case_sv_string_parameters();
int fsim_application_case_sv_interfaces();
int fsim_application_case_sv_functions();
int fsim_application_case_sv_tasks();
int fsim_application_case_sv_callable_closure();
int fsim_application_case_sv_suspending_tasks();
int fsim_application_case_sv_mutable_strings();
int fsim_application_case_sv_containers();
int fsim_application_case_synchronization();
int fsim_application_case_ordering_interactions();
int fsim_application_case_sv_container_capacity();
int fsim_application_case_sv_files();
int fsim_application_case_sv_preprocessor_generate();
int fsim_application_case_sv_aggregate_multidimensional();
int fsim_application_case_sv_conformance();

constexpr NamedApplicationCase application_cases[] = {
    { "sv_parameter_sizing", fsim_application_case_sv_parameter_sizing },
    { "sv_type_parameters", fsim_application_case_sv_type_parameters },
    { "sv_string_parameters", fsim_application_case_sv_string_parameters },
    { "sv_interfaces", fsim_application_case_sv_interfaces },
    { "sv_functions", fsim_application_case_sv_functions },
    { "sv_tasks", fsim_application_case_sv_tasks },
    { "sv_callable_closure", fsim_application_case_sv_callable_closure },
    { "sv_suspending_tasks", fsim_application_case_sv_suspending_tasks },
    { "sv_mutable_strings", fsim_application_case_sv_mutable_strings },
    { "sv_containers", fsim_application_case_sv_containers },
    { "synchronization", fsim_application_case_synchronization },
    { "ordering_interactions", fsim_application_case_ordering_interactions },
    { "sv_container_capacity", fsim_application_case_sv_container_capacity },
    { "sv_files", fsim_application_case_sv_files },
    { "sv_preprocessor_generate", fsim_application_case_sv_preprocessor_generate },
    { "sv_aggregate_multidimensional",
        fsim_application_case_sv_aggregate_multidimensional },
    { "sv_conformance", fsim_application_case_sv_conformance },
};
#elif FSIM_APPLICATION_SHARD == 2
int fsim_application_case_vhdl_type_generics();
int fsim_application_case_vhdl_function_generics();
int fsim_application_case_vhdl_overloads();
int fsim_application_case_vhdl_procedure_generics();
int fsim_application_case_vhdl_procedure_waits();
int fsim_application_case_vhdl_package_generics();
int fsim_application_case_vhdl_generic_subprograms();
int fsim_application_case_vhdl_configurations();
int fsim_application_case_vhdl_analysis_order();
int fsim_application_case_vhdl_psl();
int fsim_application_case_vhdl_components();
int fsim_application_case_vhdl_integer_shifts();
int fsim_application_case_vhdl_logic9();
int fsim_application_case_vhdl_numeric();
int fsim_application_case_vhdl_fixed();
int fsim_application_case_vhdl_float();
int fsim_application_case_vhdl_ieee_integration();
int fsim_application_case_vhdl_vital_delays();
int fsim_application_case_vhdl_records();
int fsim_application_case_vhdl_package_records();

constexpr NamedApplicationCase application_cases[] = {
    { "vhdl_type_generics", fsim_application_case_vhdl_type_generics },
    { "vhdl_function_generics", fsim_application_case_vhdl_function_generics },
    { "vhdl_overloads", fsim_application_case_vhdl_overloads },
    { "vhdl_procedure_generics", fsim_application_case_vhdl_procedure_generics },
    { "vhdl_procedure_waits", fsim_application_case_vhdl_procedure_waits },
    { "vhdl_package_generics", fsim_application_case_vhdl_package_generics },
    { "vhdl_generic_subprograms",
        fsim_application_case_vhdl_generic_subprograms },
    { "vhdl_configurations", fsim_application_case_vhdl_configurations },
    { "vhdl_analysis_order", fsim_application_case_vhdl_analysis_order },
    { "vhdl_psl", fsim_application_case_vhdl_psl },
    { "vhdl_components", fsim_application_case_vhdl_components },
    { "vhdl_integer_shifts", fsim_application_case_vhdl_integer_shifts },
    { "vhdl_logic9", fsim_application_case_vhdl_logic9 },
    { "vhdl_numeric", fsim_application_case_vhdl_numeric },
    { "vhdl_fixed", fsim_application_case_vhdl_fixed },
    { "vhdl_float", fsim_application_case_vhdl_float },
    { "vhdl_ieee_integration", fsim_application_case_vhdl_ieee_integration },
    { "vhdl_vital_delays", fsim_application_case_vhdl_vital_delays },
    { "vhdl_records", fsim_application_case_vhdl_records },
    { "vhdl_package_records", fsim_application_case_vhdl_package_records },
};
#elif FSIM_APPLICATION_SHARD == 3
int fsim_application_case_vhdl_record_aggregates();
int fsim_application_case_vhdl_subtypes();
int fsim_application_case_vhdl_enumerations();
int fsim_application_case_vhdl_arrays();
int fsim_application_case_vhdl_attributes();
int fsim_application_case_vhdl_composite_operations();
int fsim_application_case_vhdl_advanced_types();
int fsim_application_case_vhdl_projected();
int fsim_application_case_systemc_datatypes();
int fsim_application_case_systemc_tlm1();
int fsim_application_case_mixed_conversions();
int fsim_application_case_typed_boundaries();
#if FSIM_APPLICATION_WITH_TCL
int fsim_application_case_tcl();
#endif

constexpr NamedApplicationCase application_cases[] = {
    { "vhdl_record_aggregates", fsim_application_case_vhdl_record_aggregates },
    { "vhdl_subtypes", fsim_application_case_vhdl_subtypes },
    { "vhdl_enumerations", fsim_application_case_vhdl_enumerations },
    { "vhdl_arrays", fsim_application_case_vhdl_arrays },
    { "vhdl_attributes", fsim_application_case_vhdl_attributes },
    { "vhdl_composite_operations",
        fsim_application_case_vhdl_composite_operations },
    { "vhdl_advanced_types",
        fsim_application_case_vhdl_advanced_types },
    { "vhdl_projected", fsim_application_case_vhdl_projected },
    { "systemc_datatypes", fsim_application_case_systemc_datatypes },
    { "systemc_tlm1", fsim_application_case_systemc_tlm1 },
    { "mixed_conversions", fsim_application_case_mixed_conversions },
    { "typed_boundaries", fsim_application_case_typed_boundaries },
#if FSIM_APPLICATION_WITH_TCL
    { "tcl", fsim_application_case_tcl },
#endif
};
#else
#error "Unsupported application-test shard"
#endif

int main(const int argc, const char* const argv[])
{
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <application-case>\n";
        return 2;
    }

    const std::string_view requested { argv[1] };
    for (const auto& application_case : application_cases) {
        if (application_case.name == requested) {
            return application_case.run();
        }
    }

    std::cerr << "unknown application case '" << requested << "'\n";
    return 2;
}

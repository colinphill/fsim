# SPDX-License-Identifier: Apache-2.0

add_subdirectory(runtime)

add_executable(
  fsim_semantic_tests
  semantic/semantic_model_test.cpp
)
fsim_configure_test(fsim_semantic_tests)
target_link_libraries(fsim_semantic_tests PRIVATE fsim_semantic)
add_test(NAME fsim.semantic COMMAND fsim_semantic_tests)
set_tests_properties(
  fsim.semantic
  PROPERTIES LABELS "fast;semantic;hir;designir;source"
)

add_executable(
  fsim_systemverilog_hir_application_tests
  app/systemverilog_hir_application_test.cpp
)
fsim_configure_test(fsim_systemverilog_hir_application_tests)
target_link_libraries(
  fsim_systemverilog_hir_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.systemverilog_hir
  COMMAND fsim_systemverilog_hir_application_tests
)
set_tests_properties(
  fsim.application.systemverilog_hir
  PROPERTIES LABELS "fast;semantic;hir;systemverilog"
)

add_executable(
  fsim_frontend_tests
  frontend/frontend_tests.cpp
  frontend/frontend_preprocessing_tests.cpp
  frontend/frontend_sv_conformance_tests.cpp
  frontend/frontend_vhdl_type_tests.cpp
  frontend/frontend_vhdl_statement_tests.cpp
  frontend/frontend_vhdl_revision_profile_tests.cpp
  frontend/frontend_vhdl_attribute_tests.cpp
  frontend/frontend_vhdl_time_file_tests.cpp
  frontend/frontend_vhdl_psl_tests.cpp
  frontend/frontend_declaration_tests.cpp
  frontend/frontend_chandle_tests.cpp
  frontend/frontend_coverage_tests.cpp
  frontend/frontend_dpi_tests.cpp
  frontend/frontend_interface_tests.cpp
  frontend/frontend_hierarchy_tests.cpp
  frontend/frontend_package_tests.cpp
  frontend/frontend_callable_tests.cpp
  frontend/frontend_class_tests.cpp
  frontend/frontend_vhdl_subprogram_tests.cpp
  frontend/frontend_vhdl_generic_subprogram_tests.cpp
  frontend/frontend_vhdl_component_tests.cpp
  frontend/frontend_vhdl_configuration_tests.cpp
  frontend/frontend_vhdl_package_generic_tests.cpp
  frontend/frontend_statement_tests.cpp
  frontend/frontend_strength_tests.cpp
  frontend/frontend_specify_tests.cpp
  frontend/frontend_udp_tests.cpp
  frontend/frontend_generate_output_tests.cpp
  frontend/frontend_file_tests.cpp
  frontend/frontend_container_tests.cpp
)
fsim_configure_test(fsim_frontend_tests)
target_link_libraries(fsim_frontend_tests PRIVATE fsim_frontend)
if(FSIM_UVM_1_2_SOURCE_DIR AND FSIM_UVM_2020_3_1_SOURCE_DIR)
  target_compile_definitions(
    fsim_frontend_tests
    PRIVATE
      "FSIM_TEST_UVM_1_2_SOURCE_DIR=\"${FSIM_UVM_1_2_SOURCE_DIR}\""
      "FSIM_TEST_UVM_2020_3_1_SOURCE_DIR=\"${FSIM_UVM_2020_3_1_SOURCE_DIR}\""
  )
endif()

add_test(NAME fsim.frontend COMMAND fsim_frontend_tests)

add_executable(
  fsim_sdf_frontend_tests
  frontend/sdf_lexer_test.cpp
)
fsim_configure_test(fsim_sdf_frontend_tests)
target_link_libraries(fsim_sdf_frontend_tests PRIVATE fsim_frontend)
add_test(NAME fsim.frontend.sdf COMMAND fsim_sdf_frontend_tests)
set_tests_properties(
  fsim.frontend.sdf
  PROPERTIES LABELS "fast;frontend;sdf;lexical;positive;negative;coordinates;resources"
)
add_executable(
  fsim_sdf_parser_tests
  frontend/sdf_parser_test.cpp
)
fsim_configure_test(fsim_sdf_parser_tests)
target_link_libraries(fsim_sdf_parser_tests PRIVATE fsim_frontend)
add_test(NAME fsim.frontend.sdf_parser COMMAND fsim_sdf_parser_tests)
set_tests_properties(
  fsim.frontend.sdf_parser
  PROPERTIES LABELS "fast;frontend;sdf;parser;headers;positive;negative;coordinates;revision"
)
add_executable(
  fsim_sdf_construct_parser_tests
  frontend/sdf_construct_parser_test.cpp
)
fsim_configure_test(fsim_sdf_construct_parser_tests)
target_link_libraries(fsim_sdf_construct_parser_tests PRIVATE fsim_frontend)
add_test(
  NAME fsim.frontend.sdf_constructs
  COMMAND fsim_sdf_construct_parser_tests
)
set_tests_properties(
  fsim.frontend.sdf_constructs
  PROPERTIES LABELS "fast;frontend;sdf;parser;constructs;positive;negative;coordinates;revision"
)
add_executable(
  fsim_sdf21_adapter_tests
  frontend/sdf21_adapter_test.cpp
)
fsim_configure_test(fsim_sdf21_adapter_tests)
target_link_libraries(fsim_sdf21_adapter_tests PRIVATE fsim_frontend)
add_test(NAME fsim.frontend.sdf21 COMMAND fsim_sdf21_adapter_tests)
set_tests_properties(
  fsim.frontend.sdf21
  PROPERTIES LABELS "fast;frontend;sdf;parser;adapter;sdf21;positive;negative;coordinates;revision"
)
add_executable(
  fsim_sdf30_adapter_tests
  frontend/sdf30_adapter_test.cpp
)
fsim_configure_test(fsim_sdf30_adapter_tests)
target_link_libraries(fsim_sdf30_adapter_tests PRIVATE fsim_frontend)
add_test(NAME fsim.frontend.sdf30 COMMAND fsim_sdf30_adapter_tests)

add_executable(
  fsim_sdf_normalization_tests
  frontend/sdf_normalization_test.cpp
)
fsim_configure_test(fsim_sdf_normalization_tests)
target_link_libraries(fsim_sdf_normalization_tests PRIVATE fsim_frontend)
add_test(
  NAME fsim.frontend.sdf_normalization
  COMMAND fsim_sdf_normalization_tests
)
add_executable(
  fsim_sdf_ir_tests
  frontend/sdf_ir_test.cpp
)
fsim_configure_test(fsim_sdf_ir_tests)
target_link_libraries(fsim_sdf_ir_tests PRIVATE fsim_frontend)
add_test(NAME fsim.frontend.sdf_ir COMMAND fsim_sdf_ir_tests)
set_tests_properties(
  fsim.frontend.sdf30
  PROPERTIES LABELS "fast;frontend;sdf;parser;adapter;sdf30;positive;negative;coordinates;revision"
)
set_tests_properties(
  fsim.frontend.sdf_normalization
  PROPERTIES LABELS "fast;frontend;sdf;normalization;positive;negative;coordinates;revision;resources"
)
set_tests_properties(
  fsim.frontend.sdf_ir
  PROPERTIES LABELS "fast;frontend;sdf;ir;positive;negative;coordinates;revision;resources"
)

add_executable(
  fsim_sdf_annotation_scope_tests
  app/sdf_annotation_scope_test.cpp
)
fsim_configure_test(fsim_sdf_annotation_scope_tests)
target_link_libraries(
  fsim_sdf_annotation_scope_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_scope
  COMMAND fsim_sdf_annotation_scope_tests
)
set_tests_properties(
  fsim.application.sdf_scope
  PROPERTIES LABELS "fast;application;sdf;scope;positive;negative;multi-root;revision"
)

add_executable(
  fsim_sdf_cell_resolution_tests
  app/sdf_cell_resolution_test.cpp
)
fsim_configure_test(fsim_sdf_cell_resolution_tests)
target_link_libraries(
  fsim_sdf_cell_resolution_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_cell_resolution
  COMMAND fsim_sdf_cell_resolution_tests
)
set_tests_properties(
  fsim.application.sdf_cell_resolution
  PROPERTIES LABELS "fast;application;sdf;resolution;positive;negative;multi-root;revision;resources"
)

add_executable(
  fsim_sdf_endpoint_resolution_tests
  app/sdf_endpoint_resolution_test.cpp
)
fsim_configure_test(fsim_sdf_endpoint_resolution_tests)
target_link_libraries(
  fsim_sdf_endpoint_resolution_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_endpoint_resolution
  COMMAND fsim_sdf_endpoint_resolution_tests
)
set_tests_properties(
  fsim.application.sdf_endpoint_resolution
  PROPERTIES LABELS "fast;application;sdf;resolution;endpoints;positive;negative;mixed-language;revision;resources"
)

add_executable(
  fsim_sdf_mapping_validation_tests
  app/sdf_mapping_validation_test.cpp
)
fsim_configure_test(fsim_sdf_mapping_validation_tests)
target_link_libraries(
  fsim_sdf_mapping_validation_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_mapping_validation
  COMMAND fsim_sdf_mapping_validation_tests
)
set_tests_properties(
  fsim.application.sdf_mapping_validation
  PROPERTIES LABELS "fast;application;sdf;validation;mapping;positive;negative;mixed-language;revision;resources"
)

add_executable(
  fsim_sdf_value_policy_tests
  app/sdf_value_policy_test.cpp
)
fsim_configure_test(fsim_sdf_value_policy_tests)
target_link_libraries(
  fsim_sdf_value_policy_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_value_policy
  COMMAND fsim_sdf_value_policy_tests
)
set_tests_properties(
  fsim.application.sdf_value_policy
  PROPERTIES LABELS "fast;application;sdf;timing;value-policy;positive;negative;coordinates;resources"
)

add_executable(
  fsim_sdf_target_plan_tests
  app/sdf_target_plan_test.cpp
)
fsim_configure_test(fsim_sdf_target_plan_tests)
target_link_libraries(
  fsim_sdf_target_plan_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_target_plan
  COMMAND fsim_sdf_target_plan_tests
)
set_tests_properties(
  fsim.application.sdf_target_plan
  PROPERTIES LABELS "fast;application;sdf;timing;target-plan;positive;negative;coordinates;atomic;resources"
)

add_executable(
  fsim_sdf_vital_target_plan_tests
  app/sdf_vital_target_plan_test.cpp
)
fsim_configure_test(fsim_sdf_vital_target_plan_tests)
target_link_libraries(
  fsim_sdf_vital_target_plan_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_target_plan
  COMMAND fsim_sdf_vital_target_plan_tests
)
set_tests_properties(
  fsim.application.sdf_vital_target_plan
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;timing;target-plan;positive;negative;coordinates;atomic;resources"
)

add_executable(
  fsim_sdf_vital_models_tests
  app/sdf_vital_models_test.cpp
)
fsim_configure_test(fsim_sdf_vital_models_tests)
target_link_libraries(
  fsim_sdf_vital_models_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_models
  COMMAND fsim_sdf_vital_models_tests
)
set_tests_properties(
  fsim.application.sdf_vital_models
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;timing;models;wrappers;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_vital_precedence_tests
  app/sdf_vital_precedence_test.cpp
)
fsim_configure_test(fsim_sdf_vital_precedence_tests)
target_link_libraries(
  fsim_sdf_vital_precedence_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_precedence
  COMMAND fsim_sdf_vital_precedence_tests
)
set_tests_properties(
  fsim.application.sdf_vital_precedence
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;timing;precedence;selection;repeated;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_vital_scheduling_tests
  app/sdf_vital_scheduling_test.cpp
)
fsim_configure_test(fsim_sdf_vital_scheduling_tests)
target_link_libraries(
  fsim_sdf_vital_scheduling_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_scheduling
  COMMAND fsim_sdf_vital_scheduling_tests
)
set_tests_properties(
  fsim.application.sdf_vital_scheduling
  PROPERTIES LABELS "application;sdf;vhdl;vital;timing;scheduler;transport;inertial;pulse;logic9;interpreter;llvm;delta;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_vital_timing_checks_tests
  app/sdf_vital_timing_checks_test.cpp
)
fsim_configure_test(fsim_sdf_vital_timing_checks_tests)
target_link_libraries(
  fsim_sdf_vital_timing_checks_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_timing_checks
  COMMAND fsim_sdf_vital_timing_checks_tests
)
set_tests_properties(
  fsim.application.sdf_vital_timing_checks
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;timing;checks;setup-hold;recovery-removal;period-pulse;skew;violations;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_path_timing_tests
  app/sdf_path_timing_test.cpp
)
fsim_configure_test(fsim_sdf_path_timing_tests)
target_link_libraries(
  fsim_sdf_path_timing_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_path_timing
  COMMAND fsim_sdf_path_timing_tests
)
set_tests_properties(
  fsim.application.sdf_path_timing
  PROPERTIES LABELS "fast;application;sdf;timing;iopath;specify;absolute;incremental;conditional;edge;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_interconnect_timing_tests
  app/sdf_interconnect_timing_test.cpp
)
fsim_configure_test(fsim_sdf_interconnect_timing_tests)
target_link_libraries(
  fsim_sdf_interconnect_timing_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_interconnect_timing
  COMMAND fsim_sdf_interconnect_timing_tests
)
set_tests_properties(
  fsim.application.sdf_interconnect_timing
  PROPERTIES LABELS "fast;application;sdf;timing;interconnect;port;mipd;device;drivers;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_mixed_verilog_tests
  app/sdf_mixed_verilog_test.cpp
)
fsim_configure_test(fsim_sdf_mixed_verilog_tests)
target_link_libraries(
  fsim_sdf_mixed_verilog_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_mixed_verilog
  COMMAND fsim_sdf_mixed_verilog_tests
)
set_tests_properties(
  fsim.application.sdf_mixed_verilog
  PROPERTIES LABELS "fast;application;sdf;vhdl;verilog;mixed;timing;conversion;drivers;loads;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_mixed_systemverilog_tests
  app/sdf_mixed_systemverilog_test.cpp
)
fsim_configure_test(fsim_sdf_mixed_systemverilog_tests)
target_link_libraries(
  fsim_sdf_mixed_systemverilog_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_mixed_systemverilog
  COMMAND fsim_sdf_mixed_systemverilog_tests
)
set_tests_properties(
  fsim.application.sdf_mixed_systemverilog
  PROPERTIES LABELS "fast;application;sdf;vhdl;systemverilog;mixed;interface;program;class;assertion;package;region;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_mixed_systemc_tests
  app/sdf_mixed_systemc_test.cpp
)
fsim_configure_test(fsim_sdf_mixed_systemc_tests)
target_link_libraries(
  fsim_sdf_mixed_systemc_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_mixed_systemc
  COMMAND fsim_sdf_mixed_systemc_tests
)
set_tests_properties(
  fsim.application.sdf_mixed_systemc
  PROPERTIES LABELS "fast;application;sdf;vhdl;systemc;mixed;proxy;logic9;arbitrary-width;time;delta;channel;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_mixed_resolution_tests
  app/sdf_mixed_resolution_test.cpp
)
fsim_configure_test(fsim_sdf_mixed_resolution_tests)
target_link_libraries(
  fsim_sdf_mixed_resolution_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_mixed_resolution
  COMMAND fsim_sdf_mixed_resolution_tests
)
set_tests_properties(
  fsim.application.sdf_mixed_resolution
  PROPERTIES LABELS "fast;application;sdf;vhdl;verilog;systemverilog;systemc;mixed;multi-root;library;nested;direction;identity;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_foreign_interfaces_tests
  app/sdf_foreign_interfaces_test.cpp
)
fsim_configure_test(fsim_sdf_foreign_interfaces_tests)
target_link_libraries(
  fsim_sdf_foreign_interfaces_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_foreign_interfaces
  COMMAND fsim_sdf_foreign_interfaces_tests
)
set_tests_properties(
  fsim.application.sdf_foreign_interfaces
  PROPERTIES LABELS "fast;application;sdf;vhdl;verilog;systemverilog;mixed;vhpi;vpi;identity;enumeration;callback;control;disabled;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_vital_observability_tests
  app/sdf_vital_observability_test.cpp
)
fsim_configure_test(fsim_sdf_vital_observability_tests)
target_link_libraries(
  fsim_sdf_vital_observability_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_observability
  COMMAND fsim_sdf_vital_observability_tests
)
set_tests_properties(
  fsim.application.sdf_vital_observability
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;mixed;debugger;callback;trace;vcd;pending-event;violation;time;delta;region;disabled;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_vital_archive_tests
  app/sdf_vital_archive_test.cpp
)
fsim_configure_test(fsim_sdf_vital_archive_tests)
target_link_libraries(
  fsim_sdf_vital_archive_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_archive
  COMMAND fsim_sdf_vital_archive_tests
)
set_tests_properties(
  fsim.application.sdf_vital_archive
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;mixed;artifact;object;design;library;relocation;cache;checkpoint;replay;roundtrip;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_vital_phases_tests
  app/sdf_vital_phases_test.cpp
)
fsim_configure_test(fsim_sdf_vital_phases_tests)
target_link_libraries(
  fsim_sdf_vital_phases_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_phases
  COMMAND fsim_sdf_vital_phases_tests
)
set_tests_properties(
  fsim.application.sdf_vital_phases
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;mixed;project;cli;tcl;c-api;cpp-api;non-project;cold;warm;relocated;interpreter;llvm;summary;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_vital_corpus_tests
  app/sdf_vital_corpus_test.cpp
)
fsim_configure_test(fsim_sdf_vital_corpus_tests)
target_link_libraries(
  fsim_sdf_vital_corpus_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_corpus
  COMMAND fsim_sdf_vital_corpus_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sdf/vital"
)
set_tests_properties(
  fsim.application.sdf_vital_corpus
  PROPERTIES LABELS "fast;application;sdf;vhdl;vital;mixed-language;corpus;revision;standard-cell;primitive;state-table;memory;wrapper;verilog;systemverilog;systemc;interpreter;llvm;cold;warm;relocated;positive;artifact"
)

add_executable(
  fsim_sdf_delay_modes_tests
  app/sdf_delay_modes_test.cpp
)
fsim_configure_test(fsim_sdf_delay_modes_tests)
target_link_libraries(
  fsim_sdf_delay_modes_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_delay_modes
  COMMAND fsim_sdf_delay_modes_tests
)
set_tests_properties(
  fsim.application.sdf_delay_modes
  PROPERTIES LABELS "fast;application;sdf;timing;delay-list;absolute;incremental;transition;ordering;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_primary_timing_checks_tests
  app/sdf_primary_timing_checks_test.cpp
)
fsim_configure_test(fsim_sdf_primary_timing_checks_tests)
target_link_libraries(
  fsim_sdf_primary_timing_checks_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_primary_timing_checks
  COMMAND fsim_sdf_primary_timing_checks_tests
)
set_tests_properties(
  fsim.application.sdf_primary_timing_checks
  PROPERTIES LABELS "fast;application;sdf;timing;setup;hold;setuphold;recovery;removal;recrem;events;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_secondary_timing_checks_tests
  app/sdf_secondary_timing_checks_test.cpp
)
fsim_configure_test(fsim_sdf_secondary_timing_checks_tests)
target_link_libraries(
  fsim_sdf_secondary_timing_checks_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_secondary_timing_checks
  COMMAND fsim_sdf_secondary_timing_checks_tests
)
set_tests_properties(
  fsim.application.sdf_secondary_timing_checks
  PROPERTIES LABELS "fast;application;sdf;timing;skew;timeskew;fullskew;width;period;nochange;state;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_condition_timing_tests
  app/sdf_condition_timing_test.cpp
)
fsim_configure_test(fsim_sdf_condition_timing_tests)
target_link_libraries(
  fsim_sdf_condition_timing_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_condition_timing
  COMMAND fsim_sdf_condition_timing_tests
)
set_tests_properties(
  fsim.application.sdf_condition_timing
  PROPERTIES LABELS "fast;application;sdf;timing;conditions;edges;notifier;four-state;same-tick;negative;positive;atomic;resources"
)

add_executable(
  fsim_sdf_pulse_timing_tests
  app/sdf_pulse_timing_test.cpp
)
fsim_configure_test(fsim_sdf_pulse_timing_tests)
target_link_libraries(
  fsim_sdf_pulse_timing_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_pulse_timing
  COMMAND fsim_sdf_pulse_timing_tests
)
set_tests_properties(
  fsim.application.sdf_pulse_timing
  PROPERTIES LABELS "fast;application;sdf;timing;pathpulse;percentage;global;retain;four-state;cancellation;threshold;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_precedence_tests
  app/sdf_precedence_test.cpp
)
fsim_configure_test(fsim_sdf_precedence_tests)
target_link_libraries(
  fsim_sdf_precedence_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_precedence
  COMMAND fsim_sdf_precedence_tests
)
set_tests_properties(
  fsim.application.sdf_precedence
  PROPERTIES LABELS "fast;application;sdf;timing;precedence;delay-mode;disable;pathpulse;retain;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_scheduling_tests
  app/sdf_scheduling_test.cpp
)
fsim_configure_test(fsim_sdf_scheduling_tests)
target_link_libraries(
  fsim_sdf_scheduling_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_scheduling
  COMMAND fsim_sdf_scheduling_tests
)
set_tests_properties(
  fsim.application.sdf_scheduling
  PROPERTIES LABELS "fast;application;sdf;timing;scheduler;interpreter;llvm;inertial;cancellation;delta;same-time;ordering;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_drive_timing_tests
  app/sdf_drive_timing_test.cpp
)
fsim_configure_test(fsim_sdf_drive_timing_tests)
target_link_libraries(
  fsim_sdf_drive_timing_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_drive_timing
  COMMAND fsim_sdf_drive_timing_tests
)
set_tests_properties(
  fsim.application.sdf_drive_timing
  PROPERTIES LABELS "fast;application;sdf;timing;driver;strength;resolved-net;switch;mos;cmos;tran;force;release;scalar;vector;interpreter;llvm;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_reannotation_tests
  app/sdf_reannotation_test.cpp
)
fsim_configure_test(fsim_sdf_reannotation_tests)
target_link_libraries(
  fsim_sdf_reannotation_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_reannotation
  COMMAND fsim_sdf_reannotation_tests
)
set_tests_properties(
  fsim.application.sdf_reannotation
  PROPERTIES LABELS "fast;application;sdf;timing;reannotation;multi-file;wildcard;multi-root;precedence;safe-point;pending-event;timing-check;ordering;positive;negative;atomic;rollback;resources"
)

add_executable(
  fsim_sdf_vital_reannotation_tests
  app/sdf_vital_reannotation_test.cpp
)
fsim_configure_test(fsim_sdf_vital_reannotation_tests)
target_link_libraries(
  fsim_sdf_vital_reannotation_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_vital_reannotation
  COMMAND fsim_sdf_vital_reannotation_tests
)
set_tests_properties(
  fsim.application.sdf_vital_reannotation
  PROPERTIES LABELS "fast;application;sdf;vital;reannotation;multi-file;wildcard;multi-root;generic;precedence;safe-point;pending-event;timing-check;ordering;positive;negative;atomic;rollback;resources"
)

add_executable(
  fsim_sdf_control_tests
  app/sdf_control_test.cpp
)
fsim_configure_test(fsim_sdf_control_tests)
target_link_libraries(
  fsim_sdf_control_tests
  PRIVATE fsim_application fsim_cli fsim_api
)
add_test(
  NAME fsim.application.sdf_control
  COMMAND fsim_sdf_control_tests
)
set_tests_properties(
  fsim.application.sdf_control
  PROPERTIES LABELS "fast;application;sdf;control;cli;c-api;cpp-api;phase;report;identity;positive;negative;atomic;rollback;resources"
)

add_executable(
  fsim_sdf_effective_archive_tests
  app/sdf_effective_archive_test.cpp
)
fsim_configure_test(fsim_sdf_effective_archive_tests)
target_link_libraries(
  fsim_sdf_effective_archive_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_effective_archive
  COMMAND fsim_sdf_effective_archive_tests
)
set_tests_properties(
  fsim.application.sdf_effective_archive
  PROPERTIES LABELS "fast;application;sdf;effective;archive;object;design;library;cache;checkpoint;replay;relocation;schema;checksum;policy;provenance;identity;positive;negative;atomic;rollback;resources"
)

add_executable(
  fsim_sdf_observability_tests
  app/sdf_observability_test.cpp
)
fsim_configure_test(fsim_sdf_observability_tests)
target_link_libraries(
  fsim_sdf_observability_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_observability
  COMMAND fsim_sdf_observability_tests
)
set_tests_properties(
  fsim.application.sdf_observability
  PROPERTIES LABELS "fast;application;sdf;observability;debugger;callback;trace;vpi;vcd;region;ordering;source;identity;disabled;allocation;positive;negative;atomic;resources"
)

add_executable(
  fsim_sdf_application_corpus_tests
  app/sdf_application_corpus_test.cpp
)
fsim_configure_test(fsim_sdf_application_corpus_tests)
target_link_libraries(
  fsim_sdf_application_corpus_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_application_corpus
  COMMAND fsim_sdf_application_corpus_tests
)
set_tests_properties(
  fsim.application.sdf_application_corpus
  PROPERTIES LABELS "fast;application;sdf;corpus;standard-cell;primitive;interconnect;pulse;timing-check;interpreter;llvm;project;non-project;artifact;cache;checkpoint;relocation;linux;windows;positive;negative;resources"
)

add_executable(
  fsim_sdf_schema_tests
  app/sdf_schema_test.cpp
)
fsim_configure_test(fsim_sdf_schema_tests)
target_link_libraries(
  fsim_sdf_schema_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_schema
  COMMAND fsim_sdf_schema_tests
)
set_tests_properties(
  fsim.application.sdf_schema
  PROPERTIES LABELS "fast;application;sdf;schema;positive;negative;revision;roundtrip;resources"
)

add_executable(
  fsim_sdf_artifact_identity_tests
  app/sdf_artifact_identity_test.cpp
)
fsim_configure_test(fsim_sdf_artifact_identity_tests)
target_link_libraries(
  fsim_sdf_artifact_identity_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sdf_artifact_identity
  COMMAND fsim_sdf_artifact_identity_tests
)
set_tests_properties(
  fsim.application.sdf_artifact_identity
  PROPERTIES LABELS "fast;application;sdf;artifact;cache;positive;negative;revision;roundtrip;resources"
)

add_executable(
  fsim_sdf_corpus_tests
  app/sdf_corpus_test.cpp
)
fsim_configure_test(fsim_sdf_corpus_tests)
target_compile_definitions(
  fsim_sdf_corpus_tests
  PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(fsim_sdf_corpus_tests PRIVATE fsim_application)
add_test(NAME fsim.application.sdf_corpus COMMAND fsim_sdf_corpus_tests)
set_tests_properties(
  fsim.application.sdf_corpus
  PROPERTIES LABELS "fast;application;sdf;corpus;positive;revision;normalization;resolution;mixed-language;roundtrip"
)

add_executable(fsim_project_tests project/project_config_test.cpp)
fsim_configure_test(fsim_project_tests)
target_link_libraries(fsim_project_tests PRIVATE fsim_project)
add_test(NAME fsim.project COMMAND fsim_project_tests)

add_executable(fsim_library_tests library/library_artifact_test.cpp)
fsim_configure_test(fsim_library_tests)
target_link_libraries(fsim_library_tests PRIVATE fsim_library)
add_test(NAME fsim.library.artifact COMMAND fsim_library_tests)
set_tests_properties(
  fsim.library.artifact
  PROPERTIES LABELS "fast;library;artifact;source"
)

add_executable(fsim_object_artifact_tests artifact/object_artifact_test.cpp)
fsim_configure_test(fsim_object_artifact_tests)
target_link_libraries(fsim_object_artifact_tests PRIVATE fsim_library)
add_test(NAME fsim.artifact.object COMMAND fsim_object_artifact_tests)
set_tests_properties(
  fsim.artifact.object
  PROPERTIES LABELS "fast;artifact;compile;source"
)

add_executable(fsim_design_artifact_tests artifact/design_artifact_test.cpp)
fsim_configure_test(fsim_design_artifact_tests)
target_link_libraries(
  fsim_design_artifact_tests
  PRIVATE fsim_library fsim_runtime fsim_systemc
)
add_test(NAME fsim.artifact.design COMMAND fsim_design_artifact_tests)
set_tests_properties(
  fsim.artifact.design
  PROPERTIES LABELS "fast;artifact;elaborate"
)

add_executable(fsim_cache_tests compiler/cache_test.cpp)
fsim_configure_test(fsim_cache_tests)
target_link_libraries(fsim_cache_tests PRIVATE fsim_object_cache)
add_test(NAME fsim.cache COMMAND fsim_cache_tests)

add_test(
  NAME fsim.diagnostics-catalog
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckDiagnosticCatalog.cmake"
)
add_test(
  NAME fsim.uvm-phase-tlm-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckUvmPhaseTlmMatrix.cmake"
)
set_tests_properties(
  fsim.uvm-phase-tlm-matrix
  PROPERTIES LABELS "fast;uvm;phase;tlm;matrix;cmake"
)

add_test(
  NAME fsim.source-line-budget
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSourceLineBudget.cmake"
)

add_test(
  NAME fsim.ieee-package-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckIeeePackageInventory.cmake"
)
add_test(
  NAME fsim.v1-vhdl-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1VhdlMatrix.cmake"
)
add_test(
  NAME fsim.vhdl-psl-gap-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVhdlPslGapInventory.cmake"
)
add_test(
  NAME fsim.vhdl-standard-mode-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVhdlStandardModeInventory.cmake"
)
add_test(
  NAME fsim.verilog-systemverilog-standard-mode-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVerilogSystemVerilogStandardModeInventory.cmake"
)
add_test(
  NAME fsim.sdf-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSdfInventory.cmake"
)
add_test(
  NAME fsim.sdf-application-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSdfApplicationInventory.cmake"
)
add_test(
  NAME fsim.sdf-vital-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSdfVitalInventory.cmake"
)
add_test(
  NAME fsim.fst-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckFstInventory.cmake"
)
add_test(
  NAME fsim.systemc-accellera-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSystemCAccelleraInventory.cmake"
)
add_test(
  NAME fsim.systemc-accellera-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSystemCAccelleraPortabilityContract.cmake"
)
add_test(
  NAME fsim.systemc-upstream-provenance
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/tests/systemc/systemc_upstream_provenance_test.cmake"
)
add_test(
  NAME fsim.systemc.accellera_closure
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/systemc-accellera-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunAccelleraSystemCClosure.cmake"
)
set_tests_properties(
  fsim.systemc.accellera_closure
  PROPERTIES
    LABELS "systemc;accellera;tlm;closure;recursive-closure;retained-log;linux;windows"
    TIMEOUT 7200
)
add_test(
  NAME fsim.application-regression-dedup-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckApplicationTestDeduplication.cmake"
)
add_test(
  NAME fsim.fst-release-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckFstReleaseAudit.cmake"
)
add_test(
  NAME fsim.sdf-application-release-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSdfApplicationReleaseAudit.cmake"
)
add_test(
  NAME fsim.sdf-vital-release-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSdfVitalReleaseAudit.cmake"
)
add_test(
  NAME fsim.sdf-closure
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdf-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunSdfClosure.cmake"
)
set_tests_properties(
  fsim.sdf-closure
  PROPERTIES
    LABELS "sdf;closure;recursive-closure;negative;resources;artifact;cache;interpreter;llvm"
    TIMEOUT 1200
)
add_test(
  NAME fsim.sdf-application-closure
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdf-application-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunSdfApplicationClosure.cmake"
)
set_tests_properties(
  fsim.sdf-application-closure
  PROPERTIES
    LABELS "sdf;application;closure;recursive-closure;corpus;negative;resources;artifact;cache;interpreter;llvm;linux;windows"
    TIMEOUT 1200
)
add_test(
  NAME fsim.sdf-vital-closure
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdf-vital-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunSdfVitalClosure.cmake"
)
set_tests_properties(
  fsim.sdf-vital-closure
  PROPERTIES
    LABELS "sdf;vhdl;vital;mixed-language;closure;recursive-closure;corpus;revision;positive;negative;ambiguity;mismatch;unsupported;overflow;resources;multiple-root;artifact;interpreter;llvm;linux;windows;retained-log"
    TIMEOUT 1200
)
add_test(
  NAME fsim.vhdl-standard-mode-closure-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/vhdl-standard-mode-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunVhdlStandardModeClosureMatrix.cmake"
)
add_test(
  NAME fsim.verilog-systemverilog-standard-mode-closure-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/verilog-systemverilog-standard-mode-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunVerilogSystemVerilogStandardModeClosureMatrix.cmake"
)
add_test(
  NAME fsim.verilog-gap-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVerilogGapInventory.cmake"
)
add_test(
  NAME fsim.systemverilog-gap-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSystemVerilogGapInventory.cmake"
)
add_test(
  NAME fsim.systemverilog-closure-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSystemVerilogClosureAudit.cmake"
)
add_test(
  NAME fsim.systemverilog-closure-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/systemverilog-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunSystemVerilogClosureMatrix.cmake"
)
add_test(
  NAME fsim.verilog-closure-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVerilogClosureAudit.cmake"
)
add_test(
  NAME fsim.verilog-documentation
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVerilogDocumentation.cmake"
)
add_test(
  NAME fsim.verilog-closure-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DFSIM_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/verilog-closure-evidence"
    -DFSIM_DEDUPLICATED_CTEST=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/RunVerilogClosureMatrix.cmake"
)
add_test(
  NAME fsim.vhdl-psl-closure-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVhdlPslClosureAudit.cmake"
)
add_test(
  NAME fsim.vhdl-psl-documentation
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckVhdlPslDocumentation.cmake"
)
add_test(
  NAME fsim.v1-mixed-conversion-matrix
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1MixedConversionMatrix.cmake"
)
add_test(
  NAME fsim.v1-legality-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1LegalityAudit.cmake"
)
add_test(
  NAME fsim.v1-conformance-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ConformanceAudit.cmake"
)
add_test(
  NAME fsim.uvm-source-harness
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckUvmSourceHarness.cmake"
)
add_test(
  NAME fsim.uvm-platform-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckUvmPlatformContract.cmake"
)
add_test(
  NAME fsim.uvm-conformance-inventory
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckUvmConformanceInventory.cmake"
)
add_test(
  NAME fsim.uvm-closure-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckUvmClosureAudit.cmake"
)
add_test(
  NAME fsim.uvm-documentation
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckUvmDocumentation.cmake"
)
add_test(
  NAME fsim.v1-conformance-corpus
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ConformanceCorpus.cmake"
)
add_test(
  NAME fsim.v1-portability-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1PortabilityAudit.cmake"
)
add_test(
  NAME fsim.v1-portability-corpus
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1PortabilityCorpus.cmake"
)
add_test(
  NAME fsim.v1-release-audit
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ReleaseAudit.cmake"
)
add_test(
  NAME fsim.v1-systemverilog-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1SystemVerilogRelease.cmake"
)
add_test(
  NAME fsim.v1-vhdl-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1VhdlRelease.cmake"
)
add_test(
  NAME fsim.v1-mixed-systemc-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1MixedSystemCRelease.cmake"
)
add_test(
  NAME fsim.v1-differential-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1DifferentialRelease.cmake"
)
add_test(
  NAME fsim.v1-public-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1PublicRelease.cmake"
)
add_test(
  NAME fsim.v1-inventory-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1InventoryRelease.cmake"
)
add_test(
  NAME fsim.v1-resource-release
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ResourceRelease.cmake"
)
add_test(
  NAME fsim.v1-release-candidate
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckV1ReleaseCandidate.cmake"
)
if(WIN32)
  set(FSIM_INSTALLED_API_LIBRARY_DIR "${CMAKE_INSTALL_BINDIR}")
  set(FSIM_INSTALLED_SYSTEMC_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}")
else()
  set(FSIM_INSTALLED_API_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}")
  set(FSIM_INSTALLED_SYSTEMC_RUNTIME_DIR "${CMAKE_INSTALL_LIBDIR}")
endif()
add_test(
  NAME fsim.installed-public-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DFSIM_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-public-contract"
    "-DFSIM_CONFIG=$<CONFIG>"
    "-DFSIM_BINDIR=${CMAKE_INSTALL_BINDIR}"
    "-DFSIM_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
    "-DFSIM_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}"
    "-DFSIM_DOCDIR=${CMAKE_INSTALL_DOCDIR}"
    "-DFSIM_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim>"
    "-DFSIM_VHDL_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-vhdl>"
    "-DFSIM_SV_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-sv>"
    "-DFSIM_ELAB_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-elab>"
    "-DFSIM_RUN_EXECUTABLE_NAME=$<TARGET_FILE_NAME:fsim-run>"
    "-DFSIM_API_LIBRARY_NAME=$<TARGET_FILE_NAME:fsim_api>"
    "-DFSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_NAME=$<TARGET_FILE_NAME:fsim_systemc_plugin_exports>"
    "-DFSIM_SYSTEMC_ACCELERA_LIBRARY_NAME=$<TARGET_FILE_NAME:fsim_systemc_accellera_runtime>"
    "-DFSIM_SYSTEMC_UPSTREAM_LIBRARY_NAME=$<TARGET_FILE_NAME:${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>"
    "-DFSIM_SYSTEMC_RUNTIME_DIR=${FSIM_INSTALLED_SYSTEMC_RUNTIME_DIR}"
    "-DFSIM_SYSTEMC_SOURCE_ROOT=${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}"
    "-DFSIM_API_LIBRARY_DIR=${FSIM_INSTALLED_API_LIBRARY_DIR}"
    "-DFSIM_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
    "-DFSIM_CMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DFSIM_CMAKE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
    "-DFSIM_CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DFSIM_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DFSIM_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
    "-DFSIM_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
    "-DFSIM_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake"
)
add_test(
  NAME fsim.msvc-debug-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckMsvcDebugContract.cmake"
)
add_test(
  NAME fsim.msvc-release-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckMsvcReleaseContract.cmake"
)
add_test(
  NAME fsim.windows-llvm-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckWindowsLlvmContract.cmake"
)
add_test(
  NAME fsim.systemc-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSystemCPortabilityContract.cmake"
)
add_test(
  NAME fsim.tool-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckToolPortabilityContract.cmake"
)
add_test(
  NAME fsim.fst-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckFstPortabilityContract.cmake"
)
add_test(
  NAME fsim.resource-portability-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckResourcePortabilityContract.cmake"
)
set_tests_properties(
  fsim.v1-vhdl-matrix
  PROPERTIES LABELS "fast;vhdl;matrix;cmake"
)
set_tests_properties(
  fsim.vhdl-psl-gap-inventory
  PROPERTIES LABELS "fast;vhdl;psl;conformance;inventory;positive;negative;resources;cmake"
)
set_tests_properties(
  fsim.vhdl-standard-mode-inventory
  PROPERTIES LABELS "fast;vhdl;conformance;inventory;corpus;positive;negative;semantic;execution;diagnostics;width;packages;provenance;resources;cmake"
)
set_tests_properties(
  fsim.verilog-systemverilog-standard-mode-inventory
  PROPERTIES LABELS "fast;verilog;systemverilog;conformance;inventory;corpus;closure;positive;negative;semantic;execution;diagnostics;width;artifact;cache;mixed;provenance;resources;cmake"
)
set_tests_properties(
  fsim.sdf-inventory
  PROPERTIES LABELS "fast;sdf;conformance;inventory;positive;negative;revision;coordinates;artifact;roundtrip;diagnostics;resources;cmake"
)
set_tests_properties(
  fsim.sdf-application-inventory
  PROPERTIES LABELS "fast;sdf;verilog;systemverilog;timing;inventory;positive;negative;interpreter;llvm;phase;artifact;diagnostics;resources;cmake"
)
set_tests_properties(
  fsim.sdf-vital-inventory
  PROPERTIES LABELS "fast;sdf;vhdl;vital;mixed-language;timing;inventory;positive;negative;interpreter;llvm;phase;artifact;diagnostics;resources;cmake"
)
set_tests_properties(
  fsim.fst-inventory
  PROPERTIES LABELS "fast;fst;vcd;trace;inventory;positive;negative;equivalence;phase;artifact;diagnostics;resources;cmake"
)
set_tests_properties(
  fsim.systemc-accellera-inventory
  PROPERTIES LABELS "fast;systemc;accellera;tlm;backend;inventory;positive;negative;protocol;phase;artifact;diagnostics;resources;cmake"
)
set_tests_properties(
  fsim.systemc-accellera-portability-contract
  PROPERTIES LABELS "fast;systemc;accellera;compatibility;abi;cache;install;positive;negative;phase;artifact;diagnostics;resources;cmake"
)
set_tests_properties(
  fsim.systemc-upstream-provenance
  PROPERTIES LABELS "fast;systemc;accellera;upstream;provenance;license;sbom;offline;positive;negative;resources;cmake"
)
set_tests_properties(
  fsim.application-regression-dedup-contract
  PROPERTIES LABELS "fast;application;regression;deduplication;cmake"
)
set_tests_properties(
  fsim.fst-release-audit
  PROPERTIES LABELS "fast;fst;vcd;trace;release;audit;documentation;inventory;diagnostics;resources;license;portability"
)
set_tests_properties(
  fsim.sdf-vital-release-audit
  PROPERTIES LABELS "fast;sdf;vhdl;vital;mixed-language;release;audit;documentation;inventory;diagnostics;resources;license;portability"
)
set_tests_properties(
  fsim.vhdl-standard-mode-closure-matrix
  PROPERTIES
    LABELS "slow;vhdl;closure;recursive-closure;release;positive;negative;llvm;artifact;cache;debug;trace;mixed;packages;provenance;portability;resources"
    RUN_SERIAL TRUE
    TIMEOUT 7200
)
set_tests_properties(
  fsim.verilog-systemverilog-standard-mode-closure-matrix
  PROPERTIES
    LABELS "slow;verilog;systemverilog;closure;recursive-closure;release;positive;negative;llvm;artifact;cache;debug;trace;mixed;provenance;portability;resources"
    RUN_SERIAL TRUE
    TIMEOUT 7200
)
set_tests_properties(
  fsim.verilog-gap-inventory
  PROPERTIES LABELS "fast;verilog;conformance;inventory;positive;negative;width;resources;cmake"
)
set_tests_properties(
  fsim.systemverilog-gap-inventory
  PROPERTIES LABELS "fast;systemverilog;conformance;inventory;positive;negative;width;resources;cmake"
)
set_tests_properties(
  fsim.systemverilog-closure-audit
  PROPERTIES LABELS "fast;systemverilog;closure;release;inventory;width;artifact;cache;debug;trace;mixed;uvm;portability;resources;cmake"
)
set_tests_properties(
  fsim.systemverilog-closure-matrix
  PROPERTIES
    LABELS "slow;systemverilog;closure;recursive-closure;release;positive;negative;llvm;artifact;cache;debug;trace;mixed;uvm;portability;resources"
    RUN_SERIAL TRUE
    TIMEOUT 7200
)
set_tests_properties(
  fsim.verilog-closure-audit
  PROPERTIES LABELS "fast;verilog;closure;release;inventory;width;artifact;cache;debug;trace;mixed;portability;resources;cmake"
)
set_tests_properties(
  fsim.verilog-documentation
  PROPERTIES LABELS "fast;verilog;documentation;installed;release;width;cmake"
)
set_tests_properties(
  fsim.verilog-closure-matrix
  PROPERTIES
    LABELS "slow;verilog;closure;recursive-closure;release;positive;negative;llvm;artifact;cache;debug;trace;mixed;portability;resources"
    RUN_SERIAL TRUE
    TIMEOUT 7200
)
set_tests_properties(
  fsim.vhdl-psl-closure-audit
  PROPERTIES LABELS "fast;vhdl;psl;closure;release;artifact;cache;debug;trace;portability;resources;cmake"
)
set_tests_properties(
  fsim.vhdl-psl-documentation
  PROPERTIES LABELS "fast;vhdl;psl;documentation;installed;release;cmake"
)
set_tests_properties(
  fsim.v1-mixed-conversion-matrix
  PROPERTIES LABELS "fast;mixed-language;conversions;matrix;cmake"
)
set_tests_properties(
  fsim.v1-legality-audit
  PROPERTIES LABELS "fast;legality;matrix;cmake"
)
set_tests_properties(
  fsim.v1-conformance-audit
  PROPERTIES LABELS "fast;conformance;license;provenance;cmake"
)
set_tests_properties(
  fsim.uvm-source-harness
  PROPERTIES LABELS "fast;systemverilog;uvm;license;provenance;cmake"
)
set_tests_properties(
  fsim.uvm-platform-contract
  PROPERTIES LABELS "fast;systemverilog;uvm;abi;portability;windows;linux;resources;cmake"
)
set_tests_properties(
  fsim.uvm-conformance-inventory
  PROPERTIES LABELS "fast;systemverilog;uvm;conformance;inventory;positive;negative;cmake"
)
set_tests_properties(
  fsim.uvm-closure-audit
  PROPERTIES LABELS "fast;systemverilog;uvm;release;conformance;diagnostics;source;resources;provenance;cmake"
)
set_tests_properties(
  fsim.uvm-documentation
  PROPERTIES LABELS "fast;systemverilog;uvm;documentation;release;installed;portability;cmake"
)
set_tests_properties(
  fsim.v1-conformance-corpus
  PROPERTIES LABELS "fast;conformance;coverage;provenance;cmake"
)
set_tests_properties(
  fsim.v1-portability-audit
  PROPERTIES LABELS "fast;portability;windows;linux;cmake"
)
set_tests_properties(
  fsim.v1-portability-corpus
  PROPERTIES LABELS "fast;portability;windows;linux;matrix;differential;cmake"
)
set_tests_properties(
  fsim.v1-release-audit
  PROPERTIES LABELS "fast;release;legality;conformance;portability;matrix;cmake"
)
set_tests_properties(
  fsim.v1-systemverilog-release
  PROPERTIES LABELS "fast;release;systemverilog;legality;matrix;cmake"
)
set_tests_properties(
  fsim.v1-vhdl-release
  PROPERTIES LABELS "fast;release;vhdl;legality;matrix;cmake"
)
set_tests_properties(
  fsim.v1-mixed-systemc-release
  PROPERTIES LABELS "fast;release;mixed-language;systemc;abi;matrix;cmake"
)
set_tests_properties(
  fsim.v1-differential-release
  PROPERTIES LABELS "fast;release;differential;llvm;cache;debugger;vcd;cmake"
)
set_tests_properties(
  fsim.v1-public-release
  PROPERTIES LABELS "fast;release;api;abi;cli;tcl;files;portability;cmake"
)
set_tests_properties(
  fsim.v1-inventory-release
  PROPERTIES LABELS "fast;release;diagnostics;source;license;conformance;provenance;cmake"
)
set_tests_properties(
  fsim.v1-resource-release
  PROPERTIES LABELS "fast;release;portability;windows;linux;resources;cmake"
)
set_tests_properties(
  fsim.v1-release-candidate
  PROPERTIES LABELS "fast;release;matrix;coverage;evidence;cmake"
)
set_tests_properties(
  fsim.installed-public-contract
  PROPERTIES LABELS "fast;release;install;api;abi;cli;packages;portability;cmake"
)
set_tests_properties(
  fsim.msvc-debug-contract
  PROPERTIES LABELS "fast;portability;windows;msvc;debug;cmake"
)
set_tests_properties(
  fsim.msvc-release-contract
  PROPERTIES LABELS "fast;portability;windows;msvc;release;cmake"
)
set_tests_properties(
  fsim.windows-llvm-contract
  PROPERTIES LABELS "fast;portability;windows;llvm;jit;cache;debugger;cmake"
)
set_tests_properties(
  fsim.systemc-portability-contract
  PROPERTIES LABELS "fast;portability;windows;linux;systemc;abi;compiler;cache;cmake"
)
set_tests_properties(
  fsim.tool-portability-contract
  PROPERTIES LABELS "fast;portability;windows;linux;api;cli;tcl;debugger;vcd;files;cmake"
)
set_tests_properties(
  fsim.fst-portability-contract
  PROPERTIES LABELS "fast;fst;reader;negative;determinism;portability;windows;linux;files;cmake"
)
set_tests_properties(
  fsim.resource-portability-contract
  PROPERTIES LABELS "fast;portability;windows;linux;resources;memory;timeouts;cmake"
)
set_tests_properties(
  fsim.ieee-package-inventory
  PROPERTIES LABELS "fast;vhdl;packages;license;cmake"
)

add_test(
  NAME fsim.tcl.version-selection
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/tcl-version-selection"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckTclVersionSelection.cmake"
)
set_tests_properties(
  fsim.tcl.version-selection
  PROPERTIES LABELS "fast;tcl;cmake"
)

add_executable(
  fsim_jit_runtime_c_tests
  compiler/jit_runtime_c_test.c
)
target_compile_features(fsim_jit_runtime_c_tests PRIVATE c_std_11)
set_target_properties(fsim_jit_runtime_c_tests PROPERTIES C_EXTENSIONS OFF)
target_include_directories(
  fsim_jit_runtime_c_tests
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_jit_runtime_c_tests)
fsim_configure_test_platform(fsim_jit_runtime_c_tests)
add_test(NAME fsim.jit-runtime-c COMMAND fsim_jit_runtime_c_tests)

add_executable(
  fsim_elaboration_tests
  elaboration/elaborator_test.cpp
  elaboration/elaborator_specialization_test.cpp
  elaboration/elaborator_specify_test.cpp
  elaboration/elaborator_sv_constant_test.cpp
  elaboration/elaborator_sv_alias_test.cpp
  elaboration/elaborator_sv_string_test.cpp
  elaboration/elaborator_sv_type_parameter_test.cpp
  elaboration/elaborator_sv_hierarchy_test.cpp
  elaboration/elaborator_generate_test.cpp
  elaboration/elaborator_defparam_test.cpp
  elaboration/elaborator_mixed_test.cpp
  elaboration/elaborator_library_search_test.cpp
  elaboration/elaborator_udp_test.cpp
  elaboration/elaborator_strength_test.cpp
  elaboration/elaborator_multiple_root_test.cpp
  elaboration/elaborator_mixed_conversion_test.cpp
  elaboration/elaborator_mixed_construction_test.cpp
  elaboration/elaborator_mixed_driver_test.cpp
  elaboration/elaborator_process_test.cpp
  elaboration/elaborator_fork_test.cpp
  elaboration/elaborator_function_test.cpp
  elaboration/elaborator_task_test.cpp
  elaboration/elaborator_case_qualifier_test.cpp
  elaboration/elaborator_case_match_test.cpp
  elaboration/elaborator_expression_test.cpp
  elaboration/elaborator_numeric_test.cpp
  elaboration/elaborator_selection_test.cpp
  elaboration/elaborator_vhdl_dynamic_slice_test.cpp
  elaboration/elaborator_vhdl_composite_test.cpp
  elaboration/elaborator_vhdl_conversion_test.cpp
  elaboration/elaborator_vhdl_aggregate_choice_test.cpp
  elaboration/elaborator_vhdl_attribute_test.cpp
  elaboration/elaborator_vhdl_composite_operation_test.cpp
  elaboration/elaborator_types_test.cpp
  elaboration/elaborator_vhdl_textio_test.cpp
  elaboration/elaborator_type_generic_test.cpp
  elaboration/elaborator_vhdl_function_test.cpp
  elaboration/elaborator_vhdl_overload_test.cpp
  elaboration/elaborator_vhdl_procedure_test.cpp
  elaboration/elaborator_vhdl_wait_test.cpp
  elaboration/elaborator_vhdl_package_test.cpp
  elaboration/elaborator_vhdl_generic_subprogram_test.cpp
  elaboration/elaborator_vhdl_generic_association_test.cpp
  elaboration/elaborator_vhdl_component_test.cpp
  elaboration/elaborator_vhdl_configuration_test.cpp
  elaboration/elaborator_vhdl_matching_test.cpp
  elaboration/elaborator_vhdl_case_test.cpp
  elaboration/elaborator_vhdl_concurrent_test.cpp
  elaboration/elaborator_sv_file_test.cpp
  elaboration/elaborator_sv_container_test.cpp
  elaboration/elaborator_sv_container_bridge_test.cpp
  elaboration/elaborator_sv_composite_container_test.cpp
  elaboration/elaborator_sv_aggregate_container_test.cpp
  elaboration/elaborator_sv_slice_call_test.cpp
  elaboration/elaborator_sv_slice_order_test.cpp
  elaboration/elaborator_sv_slice_port_test.cpp
  elaboration/elaborator_sv_interface_test.cpp
  elaboration/elaborator_sv_conformance_test.cpp
)
fsim_configure_test(fsim_elaboration_tests)
target_link_libraries(fsim_elaboration_tests PRIVATE fsim_elaboration)
add_test(NAME fsim.elaboration COMMAND fsim_elaboration_tests)

add_executable(
  fsim_sv_container_elaboration_tests
  elaboration/elaborator_sv_container_main.cpp
  elaboration/elaborator_sv_container_test.cpp
  elaboration/elaborator_sv_container_bridge_test.cpp
  elaboration/elaborator_sv_composite_container_test.cpp
  elaboration/elaborator_sv_aggregate_container_test.cpp
  elaboration/elaborator_sv_slice_call_test.cpp
  elaboration/elaborator_sv_slice_order_test.cpp
  elaboration/elaborator_sv_slice_port_test.cpp
)
fsim_configure_test(fsim_sv_container_elaboration_tests)
target_link_libraries(
  fsim_sv_container_elaboration_tests PRIVATE fsim_elaboration)
add_test(
  NAME fsim.elaboration.sv-container
  COMMAND fsim_sv_container_elaboration_tests)

add_executable(
  fsim_systemc_shared_runtime_tests
  systemc/systemc_shared_runtime_test.cpp
)
fsim_configure_test(fsim_systemc_shared_runtime_tests)
target_include_directories(
  fsim_systemc_shared_runtime_tests
  SYSTEM PRIVATE "${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}/src"
)
target_compile_definitions(
  fsim_systemc_shared_runtime_tests
  PRIVATE
    "FSIM_EXPECTED_SYSTEMC_RUNTIME_IDENTITY=\"${FSIM_SYSTEMC_RUNTIME_IDENTITY}\""
)
target_link_libraries(
  fsim_systemc_shared_runtime_tests
  PRIVATE fsim_systemc_accellera_runtime SystemC::systemc
)
add_test(
  NAME fsim.systemc.shared_runtime
  COMMAND fsim_systemc_shared_runtime_tests
)
set_tests_properties(
  fsim.systemc.shared_runtime
  PROPERTIES LABELS "fast;systemc;accellera;runtime;shared;identity;positive;negative"
)

add_executable(
  fsim_systemc_compatibility_tests
  systemc/systemc_compatibility_test.cpp
)
fsim_configure_test(fsim_systemc_compatibility_tests)
target_link_libraries(
  fsim_systemc_compatibility_tests PRIVATE fsim_systemc_headers
)
add_test(
  NAME fsim.systemc.compatibility
  COMMAND fsim_systemc_compatibility_tests
)
set_tests_properties(
  fsim.systemc.compatibility
  PROPERTIES
    LABELS "fast;systemc;accellera;compatibility;abi;cache;positive;negative"
)
add_executable(
  fsim_systemc_kernel_backend_protocol_tests
  systemc/kernel_backend_protocol_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_protocol_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_protocol_tests PRIVATE fsim_systemc
)
add_test(
  NAME fsim.systemc.kernel_backend_protocol
  COMMAND fsim_systemc_kernel_backend_protocol_tests
)
set_tests_properties(
  fsim.systemc.kernel_backend_protocol
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;protocol;identity;positive;negative;resources"
)
add_library(
  fsim_test_accellera_session_plugin
  MODULE
  systemc/kernel_backend_session_plugin.cpp
)
target_compile_features(
  fsim_test_accellera_session_plugin PRIVATE cxx_std_20
)
target_link_libraries(
  fsim_test_accellera_session_plugin
  PRIVATE fsim_systemc_plugin_exports
)
fsim_enable_warnings(fsim_test_accellera_session_plugin)
set_target_properties(
  fsim_test_accellera_session_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
add_executable(
  fsim_systemc_kernel_backend_session_tests
  systemc/kernel_backend_session_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_session_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_session_tests PRIVATE fsim_systemc
)
add_dependencies(
  fsim_systemc_kernel_backend_session_tests
  fsim_test_accellera_session_plugin
)
add_test(
  NAME fsim.systemc.kernel_backend_session
  COMMAND
    fsim_systemc_kernel_backend_session_tests
    "$<TARGET_FILE:fsim_test_accellera_session_plugin>"
)
set_tests_properties(
  fsim.systemc.kernel_backend_session
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;session;lifecycle;rollback;positive;negative;resources"
)
add_library(
  fsim_test_accellera_execution_plugin
  MODULE
  systemc/kernel_backend_execution_plugin.cpp
)
target_compile_features(
  fsim_test_accellera_execution_plugin PRIVATE cxx_std_20
)
target_link_libraries(
  fsim_test_accellera_execution_plugin
  PRIVATE fsim_systemc_plugin_exports
)
fsim_enable_warnings(fsim_test_accellera_execution_plugin)
set_target_properties(
  fsim_test_accellera_execution_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
add_library(
  fsim_test_accellera_value_codec_plugin
  MODULE
  systemc/kernel_backend_value_codec_plugin.cpp
)
target_compile_features(
  fsim_test_accellera_value_codec_plugin PRIVATE cxx_std_20
)
target_link_libraries(
  fsim_test_accellera_value_codec_plugin
  PRIVATE fsim_systemc_plugin_exports
)
fsim_enable_warnings(fsim_test_accellera_value_codec_plugin)
set_target_properties(
  fsim_test_accellera_value_codec_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
add_executable(
  fsim_systemc_kernel_backend_execution_tests
  systemc/kernel_backend_execution_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_execution_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_execution_tests PRIVATE fsim_systemc
)
add_dependencies(
  fsim_systemc_kernel_backend_execution_tests
  fsim_test_accellera_execution_plugin
  fsim_test_accellera_value_codec_plugin
)
add_test(
  NAME fsim.systemc.kernel_backend_execution
  COMMAND
    fsim_systemc_kernel_backend_execution_tests
    "$<TARGET_FILE:fsim_test_accellera_execution_plugin>"
    "$<TARGET_FILE:fsim_test_accellera_value_codec_plugin>"
)
set_tests_properties(
  fsim.systemc.kernel_backend_execution
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;execution;ordering;rollback;positive;negative;resources"
)
add_executable(
  fsim_systemc_kernel_backend_loopback_tests
  systemc/kernel_backend_loopback_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_loopback_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_loopback_tests PRIVATE fsim_systemc
)
add_dependencies(
  fsim_systemc_kernel_backend_loopback_tests
  fsim_test_accellera_execution_plugin
)
add_test(
  NAME fsim.systemc.kernel_backend_loopback
  COMMAND
    fsim_systemc_kernel_backend_loopback_tests
    "$<TARGET_FILE:fsim_test_accellera_execution_plugin>"
)
set_tests_properties(
  fsim.systemc.kernel_backend_loopback
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;loopback;protocol;replay;ordering;containment;positive;negative;resources"
)
add_executable(
  fsim_systemc_kernel_backend_synchronization_tests
  systemc/kernel_backend_synchronization_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_synchronization_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_synchronization_tests PRIVATE fsim_systemc
)
add_dependencies(
  fsim_systemc_kernel_backend_synchronization_tests
  fsim_test_accellera_execution_plugin
)
add_test(
  NAME fsim.systemc.kernel_backend_synchronization
  COMMAND
    fsim_systemc_kernel_backend_synchronization_tests
    "$<TARGET_FILE:fsim_test_accellera_execution_plugin>"
)
set_tests_properties(
  fsim.systemc.kernel_backend_synchronization
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;synchronization;safe-point;mixed-language;ordering;containment;positive;negative;resources"
)
add_executable(
  fsim_systemc_kernel_backend_value_codec_tests
  systemc/kernel_backend_value_codec_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_value_codec_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_value_codec_tests PRIVATE fsim_systemc
)
add_test(
  NAME fsim.systemc.kernel_backend_value_codec
  COMMAND fsim_systemc_kernel_backend_value_codec_tests
)
set_tests_properties(
  fsim.systemc.kernel_backend_value_codec
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;value;codec;wide;four-state;nine-state;enum;time;positive;negative;resources"
)
add_executable(
  fsim_systemc_tlm1_backend_tests
  systemc/tlm1_backend_test.cpp
)
fsim_configure_test(fsim_systemc_tlm1_backend_tests)
target_link_libraries(
  fsim_systemc_tlm1_backend_tests PRIVATE fsim_systemc
)
add_test(
  NAME fsim.systemc.tlm1_backend
  COMMAND fsim_systemc_tlm1_backend_tests
)
set_tests_properties(
  fsim.systemc.tlm1_backend
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;tlm;tlm1;fifo;blocking;nonblocking;transport;analysis;identity;protocol;positive;negative;resources"
)
add_executable(
  fsim_systemc_tlm2_backend_tests
  systemc/tlm2_backend_test.cpp
)
fsim_configure_test(fsim_systemc_tlm2_backend_tests)
target_link_libraries(
  fsim_systemc_tlm2_backend_tests PRIVATE fsim_systemc
)
add_test(
  NAME fsim.systemc.tlm2_backend
  COMMAND fsim_systemc_tlm2_backend_tests
)
set_tests_properties(
  fsim.systemc.tlm2_backend
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;tlm;tlm2;socket;blocking;nonblocking;dmi;debug;extensions;quantum;identity;protocol;positive;negative;resources"
)
add_executable(
  fsim_systemc_kernel_backend_inventory_tests
  systemc/kernel_backend_inventory_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_inventory_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_inventory_tests PRIVATE fsim_systemc
)
add_test(
  NAME fsim.systemc.kernel_backend_inventory
  COMMAND fsim_systemc_kernel_backend_inventory_tests
)
set_tests_properties(
  fsim.systemc.kernel_backend_inventory
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;inventory;signal;buffer;clock;resolved;primitive;identity;protocol;positive;negative;resources"
)
add_executable(
  fsim_systemc_kernel_backend_binding_inventory_tests
  systemc/kernel_backend_binding_inventory_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_binding_inventory_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_binding_inventory_tests PRIVATE fsim_systemc
)
add_test(
  NAME fsim.systemc.kernel_backend_binding_inventory
  COMMAND fsim_systemc_kernel_backend_binding_inventory_tests
)
set_tests_properties(
  fsim.systemc.kernel_backend_binding_inventory
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;inventory;port;export;hierarchy;multi-bind;mixed-language;alias;identity;protocol;positive;negative;resources"
)
add_executable(
  fsim_systemc_kernel_backend_observation_tests
  systemc/kernel_backend_observation_test.cpp
)
fsim_configure_test(fsim_systemc_kernel_backend_observation_tests)
target_link_libraries(
  fsim_systemc_kernel_backend_observation_tests PRIVATE fsim_systemc
)
add_test(
  NAME fsim.systemc.kernel_backend_observation
  COMMAND fsim_systemc_kernel_backend_observation_tests
)
set_tests_properties(
  fsim.systemc.kernel_backend_observation
  PROPERTIES
    LABELS "fast;systemc;accellera;backend;debugger;safe-point;inventory;report;tlm;transaction;custom-channel;protocol;positive;negative;resources"
)

add_executable(
  fsim_systemc_accellera_corpus_tests
  systemc/accellera_systemc_corpus_test.cpp
)
fsim_configure_test(fsim_systemc_accellera_corpus_tests)
target_link_libraries(
  fsim_systemc_accellera_corpus_tests
  PRIVATE fsim_systemc_accellera_runtime SystemC::systemc
)
add_test(
  NAME fsim.systemc.accellera_corpus
  COMMAND fsim_systemc_accellera_corpus_tests
)
set_tests_properties(
  fsim.systemc.accellera_corpus
  PROPERTIES
    PASS_REGULAR_EXPRESSION "FSIM SystemC corpus PASS"
    FAIL_REGULAR_EXPRESSION "Assertion failed;Fatal:;Error:"
    LABELS "systemc;accellera;corpus;upstream;multiple-roots;arbitrary-width;port;export;channel;clock;event;tlm;tlm1;tlm2;throughput;memory;backpressure;teardown;positive;resources;linux;windows"
)

set(FSIM_SYSTEMC_UPSTREAM_CORPUS_ROOT
  "${FSIM_SYSTEMC_OFFICIAL_SOURCE_DIR}")
function(fsim_add_systemc_upstream_corpus_test target test_name source pass_regex)
  set(upstream_source "${FSIM_SYSTEMC_UPSTREAM_CORPUS_ROOT}/${source}")
  set_source_files_properties(
    "${upstream_source}"
    PROPERTIES COMPILE_DEFINITIONS "sc_main=fsim_systemc_upstream_entry"
  )
  add_executable(
    "${target}"
    "${upstream_source}"
    systemc/accellera_upstream_sc_main.cpp
  )
  target_compile_features("${target}" PRIVATE cxx_std_20)
  target_link_libraries(
    "${target}" PRIVATE fsim_systemc_accellera_runtime SystemC::systemc
  )
  add_test(NAME "${test_name}" COMMAND "${target}")
  set_tests_properties(
    "${test_name}"
    PROPERTIES
      PASS_REGULAR_EXPRESSION "${pass_regex}"
      FAIL_REGULAR_EXPRESSION "Assertion failed;Fatal:;Error:"
      LABELS "systemc;accellera;upstream;corpus;positive;linux;windows"
  )
endfunction()

fsim_add_systemc_upstream_corpus_test(
  fsim_systemc_upstream_simple_fifo
  fsim.systemc.upstream.simple_fifo
  examples/sysc/simple_fifo/simple_fifo.cpp
  "today!"
)
fsim_add_systemc_upstream_corpus_test(
  fsim_systemc_upstream_event_list
  fsim.systemc.upstream.event_list
  tests/systemc/1666-2011-compliance/event_list/event_list.cpp
  "Success"
)
fsim_add_systemc_upstream_corpus_test(
  fsim_systemc_upstream_tlm_fifo
  fsim.systemc.upstream.tlm_fifo
  tests/tlm/tlm_fifo_get_if/tlm_fifo_get_if.cpp
  "consumer get: 19"
)

add_test(
  NAME fsim.systemc.shared-runtime-contract
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DFSIM_TARGET_MANIFEST=${FSIM_SYSTEMC_RUNTIME_TARGET_MANIFEST}"
    "-DFSIM_BRIDGE_LIBRARY=$<TARGET_FILE:fsim_systemc_accellera_runtime>"
    "-DFSIM_UPSTREAM_LIBRARY=$<TARGET_FILE:${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSystemCSharedRuntime.cmake"
)
set_tests_properties(
  fsim.systemc.shared-runtime-contract
  PROPERTIES LABELS "fast;systemc;accellera;runtime;shared;targets;cmake;pkgconfig;install"
)

add_executable(
  fsim_systemc_abi_c_tests
  systemc/systemc_abi_c_test.c
)
target_compile_features(fsim_systemc_abi_c_tests PRIVATE c_std_11)
set_target_properties(
  fsim_systemc_abi_c_tests
  PROPERTIES C_EXTENSIONS OFF
)
target_include_directories(
  fsim_systemc_abi_c_tests
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_systemc_abi_c_tests)
fsim_configure_test_platform(fsim_systemc_abi_c_tests)
add_test(NAME fsim.systemc.abi-c COMMAND fsim_systemc_abi_c_tests)

function(fsim_configure_macro_systemc_plugin target)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_link_libraries(${target} PRIVATE fsim_systemc_plugin_exports)
  fsim_enable_warnings(${target})
  set_target_properties(
    ${target}
    PROPERTIES
      PREFIX ""
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES
  )
endfunction()

add_library(
  fsim_test_macro_systemc_plugin
  MODULE
    systemc/macro_plugin.cpp
    systemc/macro_plugin_extra.cpp
)
fsim_configure_macro_systemc_plugin(
  fsim_test_macro_systemc_plugin)

add_library(
  fsim_test_duplicate_macro_systemc_plugin
  MODULE
    systemc/macro_plugin.cpp
    systemc/macro_plugin_extra.cpp
)
target_compile_definitions(
  fsim_test_duplicate_macro_systemc_plugin
  PRIVATE FSIM_TEST_DUPLICATE_EXPORT=1
)
fsim_configure_macro_systemc_plugin(
  fsim_test_duplicate_macro_systemc_plugin)

add_library(
  fsim_test_throwing_systemc_plugin
  MODULE
  systemc/throwing_plugin.cpp
)

add_library(
  fsim_test_error_systemc_plugin
  MODULE
  systemc/error_plugin.cpp
)
target_compile_features(fsim_test_error_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_error_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_error_systemc_plugin)
set_target_properties(
  fsim_test_error_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(
  fsim_test_empty_systemc_plugin
  MODULE
  systemc/empty_plugin.cpp
)

add_library(
  fsim_test_no_factory_systemc_plugin
  MODULE
  systemc/no_factory_plugin.cpp
)
target_compile_features(
  fsim_test_no_factory_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_no_factory_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_no_factory_systemc_plugin)
set_target_properties(
  fsim_test_no_factory_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_compile_features(fsim_test_empty_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_empty_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_empty_systemc_plugin)
set_target_properties(
  fsim_test_empty_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_compile_features(fsim_test_throwing_systemc_plugin PRIVATE cxx_std_20)
target_include_directories(
  fsim_test_throwing_systemc_plugin
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
fsim_enable_warnings(fsim_test_throwing_systemc_plugin)
set_target_properties(
  fsim_test_throwing_systemc_plugin
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_executable(fsim_systemc_plugin_tests systemc/plugin_loader_test.cpp)
fsim_configure_test(fsim_systemc_plugin_tests)
target_link_libraries(fsim_systemc_plugin_tests PRIVATE fsim_systemc)
add_test(
  NAME fsim.systemc.plugin
  COMMAND
    fsim_systemc_plugin_tests
    "$<TARGET_FILE:fsim_test_throwing_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_error_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_empty_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_macro_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_duplicate_macro_systemc_plugin>"
    "$<TARGET_FILE:fsim_test_no_factory_systemc_plugin>"
)

add_executable(
  fsim_systemc_compiler_tests
  systemc/plugin_compiler_test.cpp
)
fsim_configure_test(fsim_systemc_compiler_tests)
target_link_libraries(fsim_systemc_compiler_tests PRIVATE fsim_systemc)
add_test(NAME fsim.systemc.compiler COMMAND fsim_systemc_compiler_tests)

add_executable(
  fsim_systemc_incremental_tests
  systemc/incremental_compiler_test.cpp
)
fsim_configure_test(fsim_systemc_incremental_tests)
target_link_libraries(fsim_systemc_incremental_tests PRIVATE fsim_systemc)
add_test(
  NAME fsim.systemc.incremental
  COMMAND fsim_systemc_incremental_tests
)

add_executable(
  fsim_systemc_matrix_tests
  systemc/plugin_matrix_test.cpp
)
fsim_configure_test(fsim_systemc_matrix_tests)
set(FSIM_TEST_CXX_OPTIONS "")
if(CMAKE_CXX_COMPILER_ARG1)
  separate_arguments(
    FSIM_TEST_CXX_COMPILER_ARG1_OPTIONS
    NATIVE_COMMAND "${CMAKE_CXX_COMPILER_ARG1}"
  )
  list(APPEND
    FSIM_TEST_CXX_OPTIONS
    ${FSIM_TEST_CXX_COMPILER_ARG1_OPTIONS}
  )
endif()
if(CMAKE_CXX_FLAGS)
  separate_arguments(
    FSIM_TEST_CXX_GLOBAL_OPTIONS
    NATIVE_COMMAND "${CMAKE_CXX_FLAGS}"
  )
  list(APPEND FSIM_TEST_CXX_OPTIONS ${FSIM_TEST_CXX_GLOBAL_OPTIONS})
endif()
set(FSIM_TEST_CXX_COMPILER_PATH "${CMAKE_CXX_COMPILER}")
if(NOT WIN32 AND FSIM_TEST_CXX_OPTIONS)
  set(FSIM_TEST_CXX_WRAPPER_COMMAND "${CMAKE_CXX_COMPILER}")
  set(FSIM_TEST_CXX_WRAPPER_ARGUMENTS "")
  foreach(FSIM_TEST_CXX_OPTION IN LISTS FSIM_TEST_CXX_OPTIONS)
    string(REPLACE "\\" "\\\\" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION}")
    string(REPLACE "\"" "\\\"" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION_ESCAPED}")
    string(REPLACE "$" "\\$" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION_ESCAPED}")
    string(REPLACE "`" "\\`" FSIM_TEST_CXX_OPTION_ESCAPED "${FSIM_TEST_CXX_OPTION_ESCAPED}")
    string(APPEND
      FSIM_TEST_CXX_WRAPPER_ARGUMENTS
      " \"${FSIM_TEST_CXX_OPTION_ESCAPED}\""
    )
  endforeach()
  set(
    FSIM_TEST_CXX_COMPILER_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/fsim-test-cxx-compiler"
  )
  configure_file(
    "${PROJECT_SOURCE_DIR}/tests/systemc/test_cxx_compiler.in"
    "${FSIM_TEST_CXX_COMPILER_PATH}"
    @ONLY
  )
  file(
    CHMOD "${FSIM_TEST_CXX_COMPILER_PATH}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
  )
endif()
target_compile_definitions(
  fsim_systemc_matrix_tests
  PRIVATE
    FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    FSIM_TEST_CXX_COMPILER="${FSIM_TEST_CXX_COMPILER_PATH}"
)
target_link_libraries(fsim_systemc_matrix_tests PRIVATE fsim_systemc)
add_test(NAME fsim.systemc.matrix COMMAND fsim_systemc_matrix_tests)
set_tests_properties(
  fsim.systemc.matrix
  PROPERTIES LABELS "systemc;compiler;cache;lifecycle;accellera;portability"
)

if(FSIM_WITH_LLVM)
  add_executable(fsim_llvm_tests compiler/llvm_jit_test.cpp)
  target_sources(
    fsim_llvm_tests
    PRIVATE
      compiler/llvm_jit_scheduling_test.cpp
      compiler/llvm_jit_value_test.cpp
      compiler/llvm_jit_cache_test.cpp
      compiler/llvm_jit_vhdl_profile_cache_test.cpp
      compiler/llvm_jit_control_cache_test.cpp
      compiler/llvm_jit_slice_cache_test.cpp
      compiler/llvm_jit_logic9_test.cpp
      compiler/llvm_jit_display_test.cpp
      compiler/llvm_jit_string_test.cpp
  )
  fsim_configure_test(fsim_llvm_tests)
  target_link_libraries(
    fsim_llvm_tests
    PRIVATE
      fsim_llvm
      fsim_object_cache
  )
  add_test(NAME fsim.llvm COMMAND fsim_llvm_tests)
endif()

if(TARGET fsim_application)
  add_executable(
    fsim_vpi_application_tests
    app/vpi_application_test.cpp
  )
  fsim_configure_test(fsim_vpi_application_tests)
  target_link_libraries(
    fsim_vpi_application_tests PRIVATE fsim_application)
  add_test(
    NAME fsim.application.vpi
    COMMAND fsim_vpi_application_tests
  )
  set_tests_properties(
    fsim.application.vpi
    PROPERTIES
      LABELS "fast;application;verilog;vpi;hierarchy;value;callback;control;llvm"
  )

  add_executable(
    fsim_uvm_phase_tlm_application_tests
    app/uvm_phase_tlm_application_test.cpp
    app/uvm_conformance_inventory.cpp
    app/uvm_phase_tlm_register_probe.cpp
    app/uvm_phase_tlm_sequence_probe.cpp
    app/uvm_process_limits.cpp
  )
  fsim_configure_test(fsim_uvm_phase_tlm_application_tests)
  target_compile_definitions(
    fsim_uvm_phase_tlm_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_uvm_phase_tlm_application_tests PRIVATE fsim_application)
  if(FSIM_UVM_1_2_SOURCE_DIR AND FSIM_UVM_2020_3_1_SOURCE_DIR)
    add_test(
      NAME fsim.application.uvm_phase_tlm.1_2
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim_uvm_phase_tlm_application_tests>"
        "-DFSIM_UVM_ROOT=${FSIM_UVM_1_2_SOURCE_DIR}"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/uvm-phase-tlm-example/uvm-1.2"
        "-DFSIM_RELEASE=uvm-1.2"
        -P "${PROJECT_SOURCE_DIR}/cmake/RunUvmPhaseTlmExample.cmake"
    )
    add_test(
      NAME fsim.application.uvm_phase_tlm.2020_3_1
      COMMAND
        "${CMAKE_COMMAND}"
        "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim_uvm_phase_tlm_application_tests>"
        "-DFSIM_UVM_ROOT=${FSIM_UVM_2020_3_1_SOURCE_DIR}"
        "-DFSIM_WORK_DIR=${PROJECT_BINARY_DIR}/uvm-phase-tlm-example/uvm-2020.3.1"
        "-DFSIM_RELEASE=uvm-2020.3.1"
        -P "${PROJECT_SOURCE_DIR}/cmake/RunUvmPhaseTlmExample.cmake"
    )
    set_tests_properties(
      fsim.application.uvm_phase_tlm.1_2
      fsim.application.uvm_phase_tlm.2020_3_1
      PROPERTIES
        LABELS "slow;application;systemverilog;uvm;artifact;llvm;debug;trace"
        RUN_SERIAL TRUE
        TIMEOUT 7200
    )
  endif()

  add_executable(
    fsim_trace_api_application_tests
    app/trace_api_application_test.cpp
  )
  fsim_configure_test(fsim_trace_api_application_tests)
  target_link_libraries(
    fsim_trace_api_application_tests
    PRIVATE fsim_application fsim_cli fsim_api
  )
  add_test(
    NAME fsim.application.trace_api
    COMMAND fsim_trace_api_application_tests
  )
  set_tests_properties(
    fsim.application.trace_api
    PROPERTIES LABELS "fast;trace;vcd;fst;api"
  )

  add_executable(
    fsim_trace_archive_application_tests
    app/trace_archive_application_test.cpp
  )
  fsim_configure_test(fsim_trace_archive_application_tests)
  target_link_libraries(
    fsim_trace_archive_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.trace_archive
    COMMAND fsim_trace_archive_application_tests
  )
  set_tests_properties(
    fsim.application.trace_archive
    PROPERTIES LABELS "fast;trace;vcd;fst;artifact"
  )

  add_executable(
    fsim_trace_format_application_tests
    app/trace_format_application_test.cpp
  )
  fsim_configure_test(fsim_trace_format_application_tests)
  target_link_libraries(
    fsim_trace_format_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.trace_format
    COMMAND fsim_trace_format_application_tests
  )
  set_tests_properties(
    fsim.application.trace_format
    PROPERTIES LABELS "fast;trace;vcd;fst;systemverilog"
  )

  add_executable(
    fsim_trace_hierarchy_application_tests
    app/trace_hierarchy_application_test.cpp
  )
  fsim_configure_test(fsim_trace_hierarchy_application_tests)
  target_include_directories(
    fsim_trace_hierarchy_application_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/src/app"
  )
  target_link_libraries(
    fsim_trace_hierarchy_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.trace_hierarchy
    COMMAND fsim_trace_hierarchy_application_tests
  )
  set_tests_properties(
    fsim.application.trace_hierarchy
    PROPERTIES LABELS "fast;trace;vcd;fst;verilog;systemverilog;vhdl;systemc;mixed-language;hierarchy;provenance;alias"
  )

  add_executable(
    fsim_trace_observation_application_tests
    app/trace_observation_application_test.cpp
  )
  fsim_configure_test(fsim_trace_observation_application_tests)
  target_include_directories(
    fsim_trace_observation_application_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/src/app"
  )
  target_link_libraries(
    fsim_trace_observation_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.trace_observation
    COMMAND fsim_trace_observation_application_tests
  )
  set_tests_properties(
    fsim.application.trace_observation
    PROPERTIES LABELS "fast;trace;vcd;fst;callbacks;uvm;class;container;coverage;assertion;sdf;atomic;negative"
  )

  add_executable(
    fsim_systemc_trace_application_tests
    app/systemc_trace_application_test.cpp
  )
  fsim_configure_test(fsim_systemc_trace_application_tests)
  target_include_directories(
    fsim_systemc_trace_application_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/src/app"
  )
  target_link_libraries(
    fsim_systemc_trace_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.systemc_trace
    COMMAND fsim_systemc_trace_application_tests
  )
  set_tests_properties(
    fsim.application.systemc_trace
    PROPERTIES LABELS "fast;systemc;accellera;trace;vcd;fst;dirty;post-update;selection;snapshot;alias;wide;backpressure;lifecycle"
  )

  add_executable(
    fsim_trace_control_application_tests
    app/trace_control_application_test.cpp
  )
  fsim_configure_test(fsim_trace_control_application_tests)
  target_include_directories(
    fsim_trace_control_application_tests
    PRIVATE "${PROJECT_SOURCE_DIR}/src/app"
  )
  target_link_libraries(
    fsim_trace_control_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.trace_control
    COMMAND fsim_trace_control_application_tests
  )
  set_tests_properties(
    fsim.application.trace_control
    PROPERTIES LABELS "fast;trace;vcd;fst;selection;debugger;tcl;snapshot;lifecycle;deterministic;negative"
  )

  add_executable(
    fsim_fst_corpus_tests
    app/fst_corpus_test.cpp
  )
  fsim_configure_test(fsim_fst_corpus_tests)
  target_link_libraries(
    fsim_fst_corpus_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.fst_corpus
    COMMAND
      fsim_fst_corpus_tests
      "${PROJECT_SOURCE_DIR}/tests/fixtures/fst"
  )
  set_tests_properties(
    fsim.application.fst_corpus
    PROPERTIES LABELS "fast;trace;vcd;fst;corpus;reader;equivalence;values;alias;mixed-language;selection;callbacks;phase;artifact;relocation;compression;negative;lifecycle"
  )
  add_test(
    NAME fsim.fst-closure
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DFSIM_BINARY_DIR=${PROJECT_BINARY_DIR}"
      "-DFSIM_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
      "-DFSIM_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/fst-closure-evidence"
      -DFSIM_DEDUPLICATED_CTEST=ON
      -P "${PROJECT_SOURCE_DIR}/cmake/RunFstClosure.cmake"
  )
  set_tests_properties(
    fsim.fst-closure
    PROPERTIES
      LABELS "fast;fst;closure;corpus;retained-logs;vcd;reader;equivalence;phase;artifact;relocation;negative;portability"
      TIMEOUT 7200
  )

  option(
    FSIM_MERGE_APPLICATION_TESTS
    "Merge application cases into a small number of selectable test hosts"
    ON
  )

  if(FSIM_MERGE_APPLICATION_TESTS)
    set(
      fsim_application_test_hosts
      fsim_application_tests
      fsim_application_tests_1
      fsim_application_tests_2
      fsim_application_tests_3
    )
    foreach(application_shard RANGE 0 3)
      list(GET fsim_application_test_hosts ${application_shard} test_host)
      add_executable(
        ${test_host}
        app/application_case_dispatch.cpp
      )
      fsim_configure_test(${test_host})
      target_compile_definitions(
        ${test_host}
        PRIVATE
          FSIM_APPLICATION_SHARD=${application_shard}
          FSIM_APPLICATION_WITH_TCL=$<BOOL:${FSIM_WITH_TCL}>
          FSIM_MERGED_APPLICATION_TESTS=1
          FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
      )
      target_link_libraries(
        ${test_host}
        PRIVATE
          fsim_application
          fsim_cli
      )
    endforeach()

    function(
      fsim_add_application_case
      application_shard
      case_name
      case_source
      test_name
    )
      list(GET fsim_application_test_hosts ${application_shard} test_host)
      set_property(
        SOURCE "${case_source}"
        APPEND PROPERTY
          COMPILE_DEFINITIONS
            "main=fsim_application_case_${case_name}"
      )
      target_sources(${test_host} PRIVATE "${case_source}")
      add_test(
        NAME "${test_name}"
        COMMAND ${test_host} "${case_name}"
      )
      if(ARGN)
        set_tests_properties(
          "${test_name}"
          PROPERTIES LABELS "${ARGN}"
        )
      endif()
    endfunction()

    fsim_add_application_case(
      0 core app/application_test.cpp fsim.application)
    add_test(
      NAME fsim.application.core_simulation
      COMMAND fsim_application_tests core_simulation
    )
    add_test(
      NAME fsim.application.core_mixed
      COMMAND fsim_application_tests core_mixed
    )
    add_test(
      NAME fsim.application.core_multiple_roots
      COMMAND fsim_application_tests core_multiple_roots
    )
    add_test(
      NAME fsim.application.core_preprocessing_cli
      COMMAND fsim_application_tests core_preprocessing_cli
    )
    add_test(
      NAME fsim.application.core_non_project_cli
      COMMAND fsim_application_tests core_non_project_cli
    )
    set_tests_properties(
      fsim.application.core_simulation
      fsim.application.core_mixed
      fsim.application.core_multiple_roots
      fsim.application.core_preprocessing_cli
      fsim.application.core_non_project_cli
      PROPERTIES LABELS "application;core;sanitizer-isolation"
    )
    fsim_add_application_case(
      0 specialization app/application_specialization_case.cpp
      fsim.application.specialization
      fast specialization hierarchy cache llvm)
    fsim_add_application_case(
      0 artifact_phases app/application_artifact_case.cpp
      fsim.application.artifact_phases
      fast artifact portability uvm)
    fsim_add_application_case(
      0 classes app/application_classes_case.cpp
      fsim.application.classes
      class uvm artifact llvm debug cache)
    fsim_add_application_case(
      0 systemc_matrix app/application_systemc_matrix_test.cpp
      fsim.application.systemc_matrix
      systemc compiler cache lifecycle threads llvm debug portability)
    target_sources(
      fsim_application_tests
      PRIVATE
        app/application_test_cli.cpp
        app/application_test_artifact_phases.cpp
        app/application_test_fixture.cpp
        app/application_test_mixed.cpp
        app/application_test_multiple_roots.cpp
        app/application_test_non_project_cli.cpp
        app/application_test_simulation.cpp
        app/application_test_classes.cpp
        app/application_test_uvm_virtual.cpp
        app/application_test_source_updates.cpp
        app/application_test_sources_common.cpp
        app/application_test_sources_mixed.cpp
        app/application_test_sources_systemc.cpp
        app/application_test_specialization.cpp
        app/application_test_support.cpp
        app/application_test_systemc.cpp
        app/application_test_systemc_scheduling.cpp
    )
    fsim_add_application_case(
      0 expressions app/expression_application_test.cpp
      fsim.application.expressions fast expressions)
    fsim_add_application_case(
      0 sv_hierarchy app/sv_hierarchy_application_test.cpp
      fsim.application.sv_hierarchy fast systemverilog hierarchy)
    fsim_add_application_case(
      0 scoped_locals app/scoped_local_application_test.cpp
      fsim.application.scoped_locals fast scoped-locals)
    fsim_add_application_case(
      0 specify app/specify_application_test.cpp
      fsim.application.specify
      fast specify timing systemverilog vhdl hierarchy llvm debugger vcd)
    fsim_add_application_case(
      0 call_safe_points app/call_safe_point_application_test.cpp
      fsim.application.call_safe_points fast call-safe-points)
    fsim_add_application_case(
      0 named_events app/named_event_application_test.cpp
      fsim.application.named_events fast named-events)
    fsim_add_application_case(
      0 fork app/fork_application_test.cpp
      fsim.application.fork fast fork systemverilog cache debugger)
    fsim_add_application_case(
      0 display app/display_application_test.cpp
      fsim.application.display fast display)
    fsim_add_application_case(
      0 random app/random_application_test.cpp
      fsim.application.random fast random)
    fsim_add_application_case(
      0 plusargs app/plusarg_application_test.cpp
      fsim.application.plusargs fast plusargs)
    fsim_add_application_case(
      0 system_command app/system_command_application_test.cpp
      fsim.application.system_command fast system-command)
    fsim_add_application_case(
      0 stochastic_queues app/stochastic_queue_application_test.cpp
      fsim.application.stochastic_queues fast stochastic-queues)
    fsim_add_application_case(
      0 pla app/pla_application_test.cpp
      fsim.application.pla fast pla)
    fsim_add_application_case(
      0 vcd_control app/vcd_control_application_test.cpp
      fsim.application.vcd_control fast vcd systemverilog)
    fsim_add_application_case(
      0 coverage app/coverage_application_test.cpp
      fsim.application.coverage fast coverage)
    fsim_add_application_case(
      0 assertions app/assertion_application_test.cpp
      fsim.application.assertions fast assertions)
    fsim_add_application_case(
      0 line_directives app/line_directive_application_test.cpp
      fsim.application.line_directives fast line-directives)
    fsim_add_application_case(
      0 time app/time_application_test.cpp
      fsim.application.time fast time)
    fsim_add_application_case(
      0 delay_modes app/delay_mode_application_test.cpp
      fsim.application.delay_modes fast delay-modes)
    fsim_add_application_case(
      0 transition_delays app/transition_delay_application_test.cpp
      fsim.application.transition_delays fast transition-delays)
    fsim_add_application_case(
      0 procedural_assignments app/procedural_assignment_application_test.cpp
      fsim.application.procedural_assignments fast procedural-assignments)
    fsim_add_application_case(
      0 resolution app/resolution_application_test.cpp
      fsim.application.resolution fast resolution mixed-language)

    fsim_add_application_case(
      1 sv_parameter_sizing app/sv_parameter_sizing_application_test.cpp
      fsim.application.sv_parameter_sizing fast parameters systemverilog)
    fsim_add_application_case(
      1 sv_type_parameters app/sv_type_parameter_application_test.cpp
      fsim.application.sv_type_parameters fast parameters systemverilog)
    fsim_add_application_case(
      1 sv_string_parameters app/sv_string_parameter_application_test.cpp
      fsim.application.sv_string_parameters fast parameters strings systemverilog)
    fsim_add_application_case(
      1 sv_interfaces app/sv_interface_application_test.cpp
      fsim.application.sv_interfaces
      fast interfaces packages systemverilog cache debugger vcd)
    fsim_add_application_case(
      1 sv_functions app/sv_function_application_test.cpp
      fsim.application.sv_functions fast functions systemverilog cache)
    fsim_add_application_case(
      1 sv_tasks app/sv_task_application_test.cpp
      fsim.application.sv_tasks fast tasks systemverilog cache)
    fsim_add_application_case(
      1 sv_callable_closure app/sv_callable_closure_application_test.cpp
      fsim.application.sv_callable_closure
      fast functions tasks systemverilog cache debugger vcd)
    fsim_add_application_case(
      1 sv_suspending_tasks app/sv_suspending_task_application_test.cpp
      fsim.application.sv_suspending_tasks
      fast tasks timing systemverilog cache)
    fsim_add_application_case(
      1 sv_mutable_strings app/sv_mutable_string_application_test.cpp
      fsim.application.sv_mutable_strings
      fast strings tasks timing systemverilog cache debugger)
    fsim_add_application_case(
      1 sv_containers app/sv_container_application_test.cpp
      fsim.application.sv_containers
      fast containers tasks timing systemverilog cache)
    fsim_add_application_case(
      1 synchronization app/synchronization_application_test.cpp
      fsim.application.synchronization
      fast synchronization mailbox semaphore systemverilog cache)
    fsim_add_application_case(
      1 ordering_interactions app/ordering_interaction_application_test.cpp
      fsim.application.ordering_interactions
      fast named-events containers ordering shuffle systemverilog cache)
    add_test(
      NAME fsim.application.sv_container_capacity
      COMMAND fsim_application_tests_1 sv_container_capacity)
    set_tests_properties(
      fsim.application.sv_container_capacity
      PROPERTIES
        LABELS "fast;containers;systemverilog;llvm"
        TIMEOUT 120)
    fsim_add_application_case(
      1 sv_files app/sv_file_application_test.cpp
      fsim.application.sv_files
      fast files strings tasks timing systemverilog cache debugger)
    fsim_add_application_case(
      1 sv_preprocessor_generate
      app/sv_preprocessor_generate_application_test.cpp
      fsim.application.sv_preprocessor_generate
      fast preprocessor generate systemverilog)
    fsim_add_application_case(
      1 sv_aggregate_multidimensional
      app/sv_aggregate_multidimensional_application_test.cpp
      fsim.application.sv_aggregate_multidimensional
      fast aggregates containers systemverilog cache debugger)
    fsim_add_application_case(
      1 sv_conformance app/sv_conformance_application_test.cpp
      fsim.application.sv_conformance
      fast conformance systemverilog expressions statements processes
      functions tasks timing assertions strings files containers memories
      cache llvm)

    fsim_add_application_case(
      2 vhdl_type_generics app/vhdl_type_generic_application_test.cpp
      fsim.application.vhdl_type_generics fast generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_function_generics app/vhdl_function_generic_application_test.cpp
      fsim.application.vhdl_function_generics
      fast functions generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_overloads app/vhdl_overload_application_test.cpp
      fsim.application.vhdl_overloads
      fast functions procedures packages vhdl cache)
    fsim_add_application_case(
      2 vhdl_procedure_generics app/vhdl_procedure_generic_application_test.cpp
      fsim.application.vhdl_procedure_generics
      fast procedures generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_procedure_waits app/vhdl_procedure_wait_application_test.cpp
      fsim.application.vhdl_procedure_waits
      fast procedures timing vhdl llvm)
    fsim_add_application_case(
      2 vhdl_package_generics app/vhdl_package_generic_application_test.cpp
      fsim.application.vhdl_package_generics
      fast packages functions procedures generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_generic_subprograms
      app/vhdl_generic_subprogram_application_test.cpp
      fsim.application.vhdl_generic_subprograms
      fast functions procedures generics vhdl cache hierarchy)
    fsim_add_application_case(
      2 vhdl_configurations app/vhdl_configuration_application_test.cpp
      fsim.application.vhdl_configurations
      fast configurations vhdl cache hierarchy debugger)
    fsim_add_application_case(
      2 vhdl_analysis_order app/vhdl_analysis_order_application_test.cpp
      fsim.application.vhdl_analysis_order
      fast analysis-order packages contexts configurations vhdl)
    fsim_add_application_case(
      2 vhdl_psl app/vhdl_psl_application_test.cpp
      fsim.application.vhdl_psl
      fast vhdl psl timing llvm cache)
    fsim_add_application_case(
      2 vhdl_components app/vhdl_component_application_test.cpp
      fsim.application.vhdl_components
      fast components vhdl cache hierarchy debugger)
    fsim_add_application_case(
      2 vhdl_integer_shifts app/vhdl_integer_shift_application_test.cpp
      fsim.application.vhdl_integer_shifts fast vhdl integer expressions)
    fsim_add_application_case(
      2 vhdl_logic9 app/vhdl_logic9_application_test.cpp
      fsim.application.vhdl_logic9 fast vhdl logic9 resolution)
    fsim_add_application_case(
      2 vhdl_numeric app/vhdl_numeric_application_test.cpp
      fsim.application.vhdl_numeric fast vhdl packages numeric)
    fsim_add_application_case(
      2 vhdl_fixed app/vhdl_fixed_application_test.cpp
      fsim.application.vhdl_fixed fast vhdl packages fixed)
    fsim_add_application_case(
      2 vhdl_float app/vhdl_float_application_test.cpp
      fsim.application.vhdl_float fast vhdl packages floating)
    fsim_add_application_case(
      2 vhdl_ieee_integration app/vhdl_ieee_integration_application_test.cpp
      fsim.application.vhdl_ieee_integration
      fast vhdl packages contexts integration)
    fsim_add_application_case(
      2 vhdl_vital_delays app/vhdl_vital_delay_application_test.cpp
      fsim.application.vhdl_vital_delays
      fast vhdl vital timing delay llvm cache vcd)
    fsim_add_application_case(
      2 vhdl_records app/vhdl_record_application_test.cpp
      fsim.application.vhdl_records fast vhdl records logic9)
    fsim_add_application_case(
      2 vhdl_package_records app/vhdl_package_record_application_test.cpp
      fsim.application.vhdl_package_records fast vhdl records packages logic9)

    fsim_add_application_case(
      3 vhdl_record_aggregates app/vhdl_record_aggregate_application_test.cpp
      fsim.application.vhdl_record_aggregates
      fast vhdl records aggregates packages logic9)
    fsim_add_application_case(
      3 vhdl_subtypes app/vhdl_subtype_application_test.cpp
      fsim.application.vhdl_subtypes
      fast vhdl subtypes packages hierarchy integer logic9)
    fsim_add_application_case(
      3 vhdl_enumerations app/vhdl_enumeration_application_test.cpp
      fsim.application.vhdl_enumerations
      fast vhdl enumerations packages hierarchy cache vcd)
    fsim_add_application_case(
      3 vhdl_arrays app/vhdl_array_application_test.cpp
      fsim.application.vhdl_arrays
      fast vhdl arrays packages hierarchy cache vcd)
    fsim_add_application_case(
      3 vhdl_attributes app/vhdl_attribute_application_test.cpp
      fsim.application.vhdl_attributes
      fast vhdl attributes arrays enumerations integer cache)
    fsim_add_application_case(
      3 vhdl_composite_operations
      app/vhdl_composite_operation_application_test.cpp
      fsim.application.vhdl_composite_operations
      fast vhdl records arrays expressions cache)
    fsim_add_application_case(
      3 vhdl_advanced_types
      app/vhdl_advanced_type_application_test.cpp
      fsim.application.vhdl_advanced_types
      fast vhdl access protected physical cache debugger vcd)
    fsim_add_application_case(
      3 vhdl_projected app/vhdl_projected_application_test.cpp
      fsim.application.vhdl_projected fast vhdl projected-waveforms)
    fsim_add_application_case(
      3 systemc_datatypes app/systemc_datatype_application_test.cpp
      fsim.application.systemc_datatypes fast systemc datatypes)
    fsim_add_application_case(
      3 systemc_tlm1 app/systemc_tlm_application_test.cpp
      fsim.application.systemc_tlm1
      fast systemc accellera tlm tlm1 tlm2 fifo socket blocking nonblocking
      dmi debug extensions quantum transport analysis cache)
    fsim_add_application_case(
      3 mixed_conversions app/mixed_conversion_application_test.cpp
      fsim.application.mixed_conversions
      fast mixed-language conversions cache debugger vcd)
    fsim_add_application_case(
      3 typed_boundaries app/typed_boundary_application_test.cpp
      fsim.application.typed_boundaries
      fast semantic hir designir vhdl systemverilog mixed-language systemc
      cache debugger vcd portability)

    set_tests_properties(
      fsim.application.scoped_locals
      PROPERTIES TIMEOUT 60
    )
    set_tests_properties(
      fsim.application.systemc_matrix
      PROPERTIES TIMEOUT 7200
    )
    set_tests_properties(
      fsim.application.sv_containers
      PROPERTIES TIMEOUT 1200
    )
    set_tests_properties(
      fsim.application.vhdl_psl
      PROPERTIES TIMEOUT 1200
    )
    set_tests_properties(
      fsim.application.artifact_phases
      fsim.application.typed_boundaries
      PROPERTIES TIMEOUT 1200
    )
    if(
      WIN32
      AND (
        MSVC
        OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
      )
    )
      set_tests_properties(
        fsim.application
        PROPERTIES TIMEOUT 600
      )
    endif()

    if(FSIM_WITH_TCL)
      fsim_add_application_case(
        3 tcl app/tcl_application_test.cpp
        fsim.application.tcl fast tcl)
      if(FSIM_WITH_BUNDLED_TCL)
        set_tests_properties(
          fsim.application.tcl
          PROPERTIES
            ENVIRONMENT
              "FSIM_TCL_LIBRARY=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
        )
        add_test(
          NAME fsim.tcl.fetched-relocation
          COMMAND
            "${CMAKE_COMMAND}"
            "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
            "-DFSIM_TCL_LIBRARY_DIR=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
            "-DFSIM_TCL_LIBRARY_VERSION=${FSIM_TCL_LIBRARY_VERSION}"
            "-DFSIM_STAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/fetched-tcl-stage"
            "-DFSIM_SYSTEMC_BRIDGE_LIBRARY=$<TARGET_FILE:fsim_systemc_accellera_runtime>"
            "-DFSIM_SYSTEMC_UPSTREAM_LIBRARY=$<TARGET_FILE:${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>"
            -P
            "${PROJECT_SOURCE_DIR}/cmake/CheckFetchedTclRelocation.cmake"
        )
        set_tests_properties(
          fsim.tcl.fetched-relocation
          PROPERTIES LABELS "fast;tcl;cmake"
        )
      endif()
    endif()
  else()
  add_executable(
    fsim_expression_application_tests
    app/expression_application_test.cpp
  )
  fsim_configure_test(fsim_expression_application_tests)
  target_link_libraries(
    fsim_expression_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.expressions
    COMMAND fsim_expression_application_tests
  )
  set_tests_properties(
    fsim.application.expressions
    PROPERTIES LABELS "fast;expressions"
  )

  add_executable(
    fsim_sv_hierarchy_application_tests
    app/sv_hierarchy_application_test.cpp
  )
  fsim_configure_test(fsim_sv_hierarchy_application_tests)
  target_link_libraries(
    fsim_sv_hierarchy_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_hierarchy
    COMMAND fsim_sv_hierarchy_application_tests
  )
  set_tests_properties(
    fsim.application.sv_hierarchy
    PROPERTIES LABELS "fast;systemverilog;hierarchy"
  )

  add_executable(
    fsim_sv_parameter_sizing_application_tests
    app/sv_parameter_sizing_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_parameter_sizing_application_tests
  )
  target_link_libraries(
    fsim_sv_parameter_sizing_application_tests
    PRIVATE fsim_application
  )
add_test(
  NAME fsim.application.sv_parameter_sizing
  COMMAND fsim_sv_parameter_sizing_application_tests
)
  set_tests_properties(
    fsim.application.sv_parameter_sizing
  PROPERTIES LABELS "fast;parameters;systemverilog"
)

add_executable(
  fsim_sv_type_parameter_application_tests
  app/sv_type_parameter_application_test.cpp
)
fsim_configure_test(fsim_sv_type_parameter_application_tests)
target_link_libraries(
  fsim_sv_type_parameter_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sv_type_parameters
  COMMAND fsim_sv_type_parameter_application_tests
)
set_tests_properties(
  fsim.application.sv_type_parameters
  PROPERTIES LABELS "fast;parameters;systemverilog"
)

add_executable(
  fsim_sv_string_parameter_application_tests
  app/sv_string_parameter_application_test.cpp
)
fsim_configure_test(fsim_sv_string_parameter_application_tests)
target_link_libraries(
  fsim_sv_string_parameter_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.sv_string_parameters
  COMMAND fsim_sv_string_parameter_application_tests
)
set_tests_properties(
  fsim.application.sv_string_parameters
  PROPERTIES LABELS "fast;parameters;strings;systemverilog"
)

add_executable(
  fsim_vhdl_type_generic_application_tests
  app/vhdl_type_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_type_generic_application_tests)
target_link_libraries(
  fsim_vhdl_type_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_type_generics
  COMMAND fsim_vhdl_type_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_type_generics
  PROPERTIES LABELS "fast;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_function_generic_application_tests
  app/vhdl_function_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_function_generic_application_tests)
target_link_libraries(
  fsim_vhdl_function_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_function_generics
  COMMAND fsim_vhdl_function_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_function_generics
  PROPERTIES LABELS "fast;functions;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_overload_application_tests
  app/vhdl_overload_application_test.cpp
)
fsim_configure_test(fsim_vhdl_overload_application_tests)
target_link_libraries(
  fsim_vhdl_overload_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_overloads
  COMMAND fsim_vhdl_overload_application_tests
)
set_tests_properties(
  fsim.application.vhdl_overloads
  PROPERTIES LABELS "fast;functions;procedures;packages;vhdl;cache"
)

add_executable(
  fsim_vhdl_procedure_generic_application_tests
  app/vhdl_procedure_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_procedure_generic_application_tests)
target_link_libraries(
  fsim_vhdl_procedure_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_procedure_generics
  COMMAND fsim_vhdl_procedure_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_procedure_generics
  PROPERTIES LABELS "fast;procedures;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_procedure_wait_application_tests
  app/vhdl_procedure_wait_application_test.cpp
)
fsim_configure_test(fsim_vhdl_procedure_wait_application_tests)
target_link_libraries(
  fsim_vhdl_procedure_wait_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_procedure_waits
  COMMAND fsim_vhdl_procedure_wait_application_tests
)
set_tests_properties(
  fsim.application.vhdl_procedure_waits
  PROPERTIES LABELS "fast;procedures;timing;vhdl;llvm"
)

add_executable(
  fsim_vhdl_package_generic_application_tests
  app/vhdl_package_generic_application_test.cpp
)
fsim_configure_test(fsim_vhdl_package_generic_application_tests)
target_link_libraries(
  fsim_vhdl_package_generic_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_package_generics
  COMMAND fsim_vhdl_package_generic_application_tests
)
set_tests_properties(
  fsim.application.vhdl_package_generics
  PROPERTIES
    LABELS "fast;packages;functions;procedures;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_generic_subprogram_application_tests
  app/vhdl_generic_subprogram_application_test.cpp
)
fsim_configure_test(
  fsim_vhdl_generic_subprogram_application_tests)
target_link_libraries(
  fsim_vhdl_generic_subprogram_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_generic_subprograms
  COMMAND fsim_vhdl_generic_subprogram_application_tests
)
set_tests_properties(
  fsim.application.vhdl_generic_subprograms
  PROPERTIES
    LABELS "fast;functions;procedures;generics;vhdl;cache;hierarchy"
)

add_executable(
  fsim_vhdl_configuration_application_tests
  app/vhdl_configuration_application_test.cpp
)
fsim_configure_test(
  fsim_vhdl_configuration_application_tests)
target_link_libraries(
  fsim_vhdl_configuration_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_configurations
  COMMAND fsim_vhdl_configuration_application_tests
)
set_tests_properties(
  fsim.application.vhdl_configurations
  PROPERTIES
    LABELS "fast;configurations;vhdl;cache;hierarchy;debugger"
)

add_executable(
  fsim_vhdl_component_application_tests
  app/vhdl_component_application_test.cpp
)
fsim_configure_test(
  fsim_vhdl_component_application_tests)
target_link_libraries(
  fsim_vhdl_component_application_tests
  PRIVATE fsim_application
)
add_test(
  NAME fsim.application.vhdl_components
  COMMAND fsim_vhdl_component_application_tests
)
set_tests_properties(
  fsim.application.vhdl_components
  PROPERTIES
    LABELS "fast;components;vhdl;cache;hierarchy;debugger"
)

  add_executable(
    fsim_scoped_local_application_tests
    app/scoped_local_application_test.cpp
  )
  fsim_configure_test(fsim_scoped_local_application_tests)
  target_link_libraries(
    fsim_scoped_local_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.scoped_locals
    COMMAND fsim_scoped_local_application_tests
  )
  set_tests_properties(
    fsim.application.scoped_locals
    PROPERTIES
      LABELS "fast;scoped-locals"
      TIMEOUT 120
  )

  add_executable(
    fsim_specify_application_tests
    app/specify_application_test.cpp
  )
  fsim_configure_test(fsim_specify_application_tests)
  target_link_libraries(
    fsim_specify_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.specify
    COMMAND fsim_specify_application_tests
  )
  set_tests_properties(
    fsim.application.specify
    PROPERTIES
      LABELS "fast;specify;timing;systemverilog;vhdl;hierarchy;llvm;debugger;vcd"
  )

  add_executable(
    fsim_call_safe_point_application_tests
    app/call_safe_point_application_test.cpp
  )
  fsim_configure_test(fsim_call_safe_point_application_tests)
  target_link_libraries(
    fsim_call_safe_point_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.call_safe_points
    COMMAND fsim_call_safe_point_application_tests
  )
  set_tests_properties(
    fsim.application.call_safe_points
    PROPERTIES LABELS "fast;call-safe-points"
  )

  add_executable(
    fsim_sv_interface_application_tests
    app/sv_interface_application_test.cpp
  )
  fsim_configure_test(fsim_sv_interface_application_tests)
  target_link_libraries(
    fsim_sv_interface_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_interfaces
    COMMAND fsim_sv_interface_application_tests
  )
  set_tests_properties(
    fsim.application.sv_interfaces
    PROPERTIES
      LABELS "fast;interfaces;packages;systemverilog;cache;debugger;vcd"
  )

  add_executable(
    fsim_sv_function_application_tests
    app/sv_function_application_test.cpp
  )
  fsim_configure_test(fsim_sv_function_application_tests)
  target_link_libraries(
    fsim_sv_function_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_functions
    COMMAND fsim_sv_function_application_tests
  )
  set_tests_properties(
    fsim.application.sv_functions
    PROPERTIES
      LABELS "fast;functions;systemverilog;cache"
  )

  add_executable(
    fsim_sv_task_application_tests
    app/sv_task_application_test.cpp
  )
  fsim_configure_test(fsim_sv_task_application_tests)
  target_link_libraries(
    fsim_sv_task_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_tasks
    COMMAND fsim_sv_task_application_tests
  )
  set_tests_properties(
    fsim.application.sv_tasks
    PROPERTIES
      LABELS "fast;tasks;systemverilog;cache"
  )

  add_executable(
    fsim_sv_callable_closure_application_tests
    app/sv_callable_closure_application_test.cpp
  )
  fsim_configure_test(fsim_sv_callable_closure_application_tests)
  target_link_libraries(
    fsim_sv_callable_closure_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_callable_closure
    COMMAND fsim_sv_callable_closure_application_tests
  )
  set_tests_properties(
    fsim.application.sv_callable_closure
    PROPERTIES
      LABELS "fast;functions;tasks;systemverilog;cache;debugger;vcd"
  )

  add_executable(
    fsim_sv_suspending_task_application_tests
    app/sv_suspending_task_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_suspending_task_application_tests
  )
  target_link_libraries(
    fsim_sv_suspending_task_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_suspending_tasks
    COMMAND fsim_sv_suspending_task_application_tests
  )
  set_tests_properties(
    fsim.application.sv_suspending_tasks
    PROPERTIES
      LABELS "fast;tasks;timing;systemverilog;cache"
  )

  add_executable(
    fsim_sv_mutable_string_application_tests
    app/sv_mutable_string_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_mutable_string_application_tests
  )
  target_link_libraries(
    fsim_sv_mutable_string_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_mutable_strings
    COMMAND fsim_sv_mutable_string_application_tests
  )
  set_tests_properties(
    fsim.application.sv_mutable_strings
    PROPERTIES
      LABELS "fast;strings;tasks;timing;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_sv_container_application_tests
    app/sv_container_application_test.cpp
  )
  fsim_configure_test(fsim_sv_container_application_tests)
  target_link_libraries(
    fsim_sv_container_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_containers
    COMMAND fsim_sv_container_application_tests
  )
  add_test(
    NAME fsim.application.sv_container_capacity
    COMMAND fsim_sv_container_application_tests capacity
  )
  set_tests_properties(
    fsim.application.sv_container_capacity
    PROPERTIES
      LABELS "fast;containers;systemverilog;llvm"
      TIMEOUT 60
  )
  set_tests_properties(
    fsim.application.sv_containers
    PROPERTIES
      LABELS "fast;containers;tasks;timing;systemverilog;cache"
      # LLVM-disabled sanitizer builds retain one complete semantic/debugger
      # pass and avoid redundant compiled/O2 aliases. Twenty minutes preserves
      # hosted-runner headroom while keeping future stalls diagnostic.
      TIMEOUT 1200
  )

  add_executable(
    fsim_sv_file_application_tests
    app/sv_file_application_test.cpp
  )
  fsim_configure_test(fsim_sv_file_application_tests)
  target_link_libraries(
    fsim_sv_file_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_files
    COMMAND fsim_sv_file_application_tests
  )
  set_tests_properties(
    fsim.application.sv_files
    PROPERTIES
      LABELS "fast;files;strings;tasks;timing;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_named_event_application_tests
    app/named_event_application_test.cpp
  )
  fsim_configure_test(fsim_named_event_application_tests)
  target_link_libraries(
    fsim_named_event_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.named_events
    COMMAND fsim_named_event_application_tests
  )
  set_tests_properties(
    fsim.application.named_events
    PROPERTIES LABELS "fast;named-events"
  )

  add_executable(
    fsim_fork_application_tests
    app/fork_application_test.cpp
  )
  fsim_configure_test(fsim_fork_application_tests)
  target_link_libraries(
    fsim_fork_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.fork
    COMMAND fsim_fork_application_tests
  )
  set_tests_properties(
    fsim.application.fork
    PROPERTIES LABELS "fast;fork;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_synchronization_application_tests
    app/synchronization_application_test.cpp
  )
  fsim_configure_test(fsim_synchronization_application_tests)
  target_link_libraries(
    fsim_synchronization_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.synchronization
    COMMAND fsim_synchronization_application_tests
  )
  set_tests_properties(
    fsim.application.synchronization
    PROPERTIES
      LABELS "fast;synchronization;mailbox;semaphore;systemverilog;cache"
  )

  add_executable(
    fsim_ordering_interaction_application_tests
    app/ordering_interaction_application_test.cpp
  )
  fsim_configure_test(fsim_ordering_interaction_application_tests)
  target_link_libraries(
    fsim_ordering_interaction_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.ordering_interactions
    COMMAND fsim_ordering_interaction_application_tests
  )
  set_tests_properties(
    fsim.application.ordering_interactions
    PROPERTIES
      LABELS "fast;named-events;containers;ordering;shuffle;systemverilog;cache"
  )

  add_executable(
    fsim_display_application_tests
    app/display_application_test.cpp
  )
  fsim_configure_test(fsim_display_application_tests)
  target_link_libraries(
    fsim_display_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.display
    COMMAND fsim_display_application_tests
  )
  set_tests_properties(
    fsim.application.display
    PROPERTIES LABELS "fast;display"
  )

  add_executable(
    fsim_random_application_tests
    app/random_application_test.cpp
  )
  fsim_configure_test(fsim_random_application_tests)
  target_link_libraries(
    fsim_random_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.random
    COMMAND fsim_random_application_tests
  )
  set_tests_properties(
    fsim.application.random
    PROPERTIES LABELS "fast;random"
  )

  add_executable(
    fsim_plusarg_application_tests
    app/plusarg_application_test.cpp
  )
  fsim_configure_test(fsim_plusarg_application_tests)
  target_link_libraries(
    fsim_plusarg_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.plusargs
    COMMAND fsim_plusarg_application_tests
  )
  set_tests_properties(
    fsim.application.plusargs
    PROPERTIES LABELS "fast;plusargs"
  )

  add_executable(
    fsim_system_command_application_tests
    app/system_command_application_test.cpp
  )
  fsim_configure_test(fsim_system_command_application_tests)
  target_link_libraries(
    fsim_system_command_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.system_command
    COMMAND fsim_system_command_application_tests
  )
  set_tests_properties(
    fsim.application.system_command
    PROPERTIES LABELS "fast;system-command"
  )

  add_executable(
    fsim_stochastic_queue_application_tests
    app/stochastic_queue_application_test.cpp
  )
  fsim_configure_test(fsim_stochastic_queue_application_tests)
  target_link_libraries(
    fsim_stochastic_queue_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.stochastic_queues
    COMMAND fsim_stochastic_queue_application_tests
  )
  set_tests_properties(
    fsim.application.stochastic_queues
    PROPERTIES LABELS "fast;stochastic-queues"
  )

  add_executable(
    fsim_pla_application_tests
    app/pla_application_test.cpp
  )
  fsim_configure_test(fsim_pla_application_tests)
  target_link_libraries(
    fsim_pla_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.pla
    COMMAND fsim_pla_application_tests
  )
  set_tests_properties(
    fsim.application.pla
    PROPERTIES LABELS "fast;pla"
  )

  add_executable(
    fsim_vcd_control_application_tests
    app/vcd_control_application_test.cpp
  )
  fsim_configure_test(fsim_vcd_control_application_tests)
  target_link_libraries(
    fsim_vcd_control_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vcd_control
    COMMAND fsim_vcd_control_application_tests
  )
  set_tests_properties(
    fsim.application.vcd_control
    PROPERTIES LABELS "fast;vcd;systemverilog"
  )

  add_executable(
    fsim_coverage_application_tests
    app/coverage_application_test.cpp
  )
  fsim_configure_test(fsim_coverage_application_tests)
  target_link_libraries(
    fsim_coverage_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.coverage
    COMMAND fsim_coverage_application_tests
  )
  set_tests_properties(
    fsim.application.coverage
    PROPERTIES LABELS "fast;coverage"
  )

  add_executable(
    fsim_assertion_application_tests
    app/assertion_application_test.cpp
  )
  fsim_configure_test(fsim_assertion_application_tests)
  target_link_libraries(
    fsim_assertion_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.assertions
    COMMAND fsim_assertion_application_tests
  )
  set_tests_properties(
    fsim.application.assertions
    PROPERTIES LABELS "fast;assertions"
  )

  add_executable(
    fsim_line_directive_application_tests
    app/line_directive_application_test.cpp
  )
  fsim_configure_test(fsim_line_directive_application_tests)
  target_link_libraries(
    fsim_line_directive_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.line_directives
    COMMAND fsim_line_directive_application_tests
  )
  set_tests_properties(
    fsim.application.line_directives
    PROPERTIES LABELS "fast;line-directives"
  )

  add_executable(
    fsim_sv_preprocessor_generate_application_tests
    app/sv_preprocessor_generate_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_preprocessor_generate_application_tests
  )
  target_link_libraries(
    fsim_sv_preprocessor_generate_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_preprocessor_generate
    COMMAND fsim_sv_preprocessor_generate_application_tests
  )
  set_tests_properties(
    fsim.application.sv_preprocessor_generate
    PROPERTIES LABELS "fast;preprocessor;generate;systemverilog"
  )

  add_executable(
    fsim_sv_aggregate_multidimensional_application_tests
    app/sv_aggregate_multidimensional_application_test.cpp
  )
  fsim_configure_test(
    fsim_sv_aggregate_multidimensional_application_tests
  )
  target_link_libraries(
    fsim_sv_aggregate_multidimensional_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.sv_aggregate_multidimensional
    COMMAND fsim_sv_aggregate_multidimensional_application_tests
  )
  set_tests_properties(
    fsim.application.sv_aggregate_multidimensional
    PROPERTIES LABELS "fast;aggregates;containers;systemverilog;cache;debugger"
  )

  add_executable(
    fsim_time_application_tests
    app/time_application_test.cpp
  )
  fsim_configure_test(fsim_time_application_tests)
  target_link_libraries(
    fsim_time_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.time
    COMMAND fsim_time_application_tests
  )
  set_tests_properties(
    fsim.application.time
    PROPERTIES LABELS "fast;time"
  )

  add_executable(
    fsim_delay_mode_application_tests
    app/delay_mode_application_test.cpp
  )
  fsim_configure_test(fsim_delay_mode_application_tests)
  target_link_libraries(
    fsim_delay_mode_application_tests
    PRIVATE
      fsim_application
      fsim_cli
  )
  add_test(
    NAME fsim.application.delay_modes
    COMMAND fsim_delay_mode_application_tests
  )
  set_tests_properties(
    fsim.application.delay_modes
    PROPERTIES LABELS "fast;delay-modes"
  )

  add_executable(
    fsim_transition_delay_application_tests
    app/transition_delay_application_test.cpp
  )
  fsim_configure_test(fsim_transition_delay_application_tests)
  target_link_libraries(
    fsim_transition_delay_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.transition_delays
    COMMAND fsim_transition_delay_application_tests
  )
  set_tests_properties(
    fsim.application.transition_delays
    PROPERTIES LABELS "fast;transition-delays"
  )

  add_executable(
    fsim_procedural_assignment_application_tests
    app/procedural_assignment_application_test.cpp
  )
  fsim_configure_test(fsim_procedural_assignment_application_tests)
  target_link_libraries(
    fsim_procedural_assignment_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.procedural_assignments
    COMMAND fsim_procedural_assignment_application_tests
  )
  set_tests_properties(
    fsim.application.procedural_assignments
    PROPERTIES LABELS "fast;procedural-assignments"
  )

  add_executable(
    fsim_resolution_application_tests
    app/resolution_application_test.cpp
  )
  fsim_configure_test(fsim_resolution_application_tests)
  target_link_libraries(
    fsim_resolution_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.resolution
    COMMAND fsim_resolution_application_tests
  )
  set_tests_properties(
    fsim.application.resolution
    PROPERTIES LABELS "fast;resolution;mixed-language"
  )

  add_executable(
    fsim_mixed_conversion_application_tests
    app/mixed_conversion_application_test.cpp
  )
  fsim_configure_test(fsim_mixed_conversion_application_tests)
  target_link_libraries(
    fsim_mixed_conversion_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.mixed_conversions
    COMMAND fsim_mixed_conversion_application_tests
  )
  set_tests_properties(
    fsim.application.mixed_conversions
    PROPERTIES
      LABELS "fast;mixed-language;conversions;cache;debugger;vcd"
  )

  add_executable(
    fsim_typed_boundary_application_tests
    app/typed_boundary_application_test.cpp
  )
  fsim_configure_test(fsim_typed_boundary_application_tests)
  target_compile_definitions(
    fsim_typed_boundary_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_typed_boundary_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.typed_boundaries
    COMMAND fsim_typed_boundary_application_tests
  )
  set_tests_properties(
    fsim.application.typed_boundaries
    PROPERTIES
      LABELS
        "fast;semantic;hir;designir;vhdl;systemverilog;mixed-language;systemc;cache;debugger;vcd;portability"
      TIMEOUT 1200
  )

  add_executable(
    fsim_systemc_datatype_application_tests
    app/systemc_datatype_application_test.cpp
  )
  fsim_configure_test(fsim_systemc_datatype_application_tests)
  target_compile_definitions(
    fsim_systemc_datatype_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_systemc_datatype_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.systemc_datatypes
    COMMAND fsim_systemc_datatype_application_tests
  )
  set_tests_properties(
    fsim.application.systemc_datatypes
    PROPERTIES LABELS "fast;systemc;datatypes"
  )

  add_executable(
    fsim_systemc_tlm1_application_tests
    app/systemc_tlm_application_test.cpp
  )
  fsim_configure_test(fsim_systemc_tlm1_application_tests)
  target_compile_definitions(
    fsim_systemc_tlm1_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_systemc_tlm1_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.systemc_tlm1
    COMMAND fsim_systemc_tlm1_application_tests
  )
  set_tests_properties(
    fsim.application.systemc_tlm1
    PROPERTIES
      LABELS "fast;systemc;accellera;tlm;tlm1;tlm2;fifo;socket;blocking;nonblocking;dmi;debug;extensions;quantum;transport;analysis;cache"
  )

  add_executable(
    fsim_vhdl_integer_shift_application_tests
    app/vhdl_integer_shift_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_integer_shift_application_tests
  )
  target_link_libraries(
    fsim_vhdl_integer_shift_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_integer_shifts
    COMMAND fsim_vhdl_integer_shift_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_integer_shifts
    PROPERTIES LABELS "fast;vhdl;integer;expressions"
  )

  add_executable(
    fsim_vhdl_logic9_application_tests
    app/vhdl_logic9_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_logic9_application_tests
  )
  target_link_libraries(
    fsim_vhdl_logic9_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_logic9
    COMMAND fsim_vhdl_logic9_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_logic9
    PROPERTIES LABELS "fast;vhdl;logic9;resolution"
  )

  add_executable(
    fsim_vhdl_record_application_tests
    app/vhdl_record_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_record_application_tests
  )
  target_link_libraries(
    fsim_vhdl_record_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_records
    COMMAND fsim_vhdl_record_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_records
    PROPERTIES LABELS "fast;vhdl;records;logic9"
  )

  add_executable(
    fsim_vhdl_package_record_application_tests
    app/vhdl_package_record_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_package_record_application_tests
  )
  target_link_libraries(
    fsim_vhdl_package_record_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_package_records
    COMMAND fsim_vhdl_package_record_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_package_records
    PROPERTIES LABELS "fast;vhdl;records;packages;logic9"
  )

  add_executable(
    fsim_vhdl_record_aggregate_application_tests
    app/vhdl_record_aggregate_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_record_aggregate_application_tests
  )
  target_link_libraries(
    fsim_vhdl_record_aggregate_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_record_aggregates
    COMMAND fsim_vhdl_record_aggregate_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_record_aggregates
    PROPERTIES LABELS "fast;vhdl;records;aggregates;packages;logic9"
  )

  add_executable(
    fsim_vhdl_subtype_application_tests
    app/vhdl_subtype_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_subtype_application_tests
  )
  target_link_libraries(
    fsim_vhdl_subtype_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_subtypes
    COMMAND fsim_vhdl_subtype_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_subtypes
    PROPERTIES LABELS "fast;vhdl;subtypes;packages;hierarchy;integer;logic9"
  )

  add_executable(
    fsim_vhdl_enumeration_application_tests
    app/vhdl_enumeration_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_enumeration_application_tests
  )
  target_link_libraries(
    fsim_vhdl_enumeration_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_enumerations
    COMMAND fsim_vhdl_enumeration_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_enumerations
    PROPERTIES LABELS "fast;vhdl;enumerations;packages;hierarchy;cache;vcd"
  )

  add_executable(
    fsim_vhdl_array_application_tests
    app/vhdl_array_application_test.cpp
  )
  fsim_configure_test(
    fsim_vhdl_array_application_tests
  )
  target_link_libraries(
    fsim_vhdl_array_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_arrays
    COMMAND fsim_vhdl_array_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_arrays
    PROPERTIES LABELS "fast;vhdl;arrays;packages;hierarchy;cache;vcd"
  )

  add_executable(
    fsim_vhdl_attribute_application_tests
    app/vhdl_attribute_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_attribute_application_tests)
  target_link_libraries(
    fsim_vhdl_attribute_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_attributes
    COMMAND fsim_vhdl_attribute_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_attributes
    PROPERTIES LABELS "fast;vhdl;attributes;arrays;enumerations;integer;cache"
  )

  add_executable(
    fsim_vhdl_composite_operation_application_tests
    app/vhdl_composite_operation_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_composite_operation_application_tests)
  target_link_libraries(
    fsim_vhdl_composite_operation_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_composite_operations
    COMMAND fsim_vhdl_composite_operation_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_composite_operations
    PROPERTIES LABELS "fast;vhdl;records;arrays;expressions;cache"
  )

  add_executable(
    fsim_vhdl_projected_application_tests
    app/vhdl_projected_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_projected_application_tests)
  target_link_libraries(
    fsim_vhdl_projected_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_projected
    COMMAND fsim_vhdl_projected_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_projected
    PROPERTIES LABELS "fast;vhdl;projected-waveforms"
  )

  add_executable(
    fsim_vhdl_advanced_type_application_tests
    app/vhdl_advanced_type_application_test.cpp
  )
  fsim_configure_test(fsim_vhdl_advanced_type_application_tests)
  target_link_libraries(
    fsim_vhdl_advanced_type_application_tests
    PRIVATE fsim_application
  )
  add_test(
    NAME fsim.application.vhdl_advanced_types
    COMMAND fsim_vhdl_advanced_type_application_tests
  )
  set_tests_properties(
    fsim.application.vhdl_advanced_types
    PROPERTIES
      LABELS "fast;vhdl;access;protected;physical;cache;debugger;vcd"
  )

  add_executable(
    fsim_application_tests
    app/application_test.cpp
    app/application_test_artifact_phases.cpp
    app/application_test_cli.cpp
    app/application_test_fixture.cpp
    app/application_test_mixed.cpp
    app/application_test_multiple_roots.cpp
    app/application_test_non_project_cli.cpp
    app/application_test_simulation.cpp
    app/application_test_classes.cpp
    app/application_test_uvm_virtual.cpp
    app/application_test_source_updates.cpp
    app/application_test_sources_common.cpp
    app/application_test_sources_mixed.cpp
    app/application_test_sources_systemc.cpp
    app/application_test_specialization.cpp
    app/application_test_support.cpp
    app/application_test_systemc.cpp
    app/application_test_systemc_scheduling.cpp
  )
  fsim_configure_test(fsim_application_tests)
  if(
    WIN32
    AND (
      MSVC
      OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
  )
    # The monolithic application regression retains many large project and
    # type values in one unoptimized fixture frame. Give MSVC-compatible Debug
    # builds the same bounded stack budget as the accumulated frontend and
    # elaboration matrices.
    target_link_options(
      fsim_application_tests
      PRIVATE "$<$<CONFIG:Debug>:/STACK:8388608>"
    )
  endif()
  target_compile_definitions(
    fsim_application_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(fsim_application_tests PRIVATE fsim_application)
  add_test(NAME fsim.application COMMAND fsim_application_tests)
  set_tests_properties(fsim.application PROPERTIES TIMEOUT 1200)
  if(
    WIN32
    AND (
      MSVC
      OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
  )
    set_tests_properties(
      fsim.application
      PROPERTIES TIMEOUT 600
    )
  endif()

  if(FSIM_WITH_TCL)
    add_executable(
      fsim_tcl_application_tests
      app/tcl_application_test.cpp
    )
    fsim_configure_test(fsim_tcl_application_tests)
    target_link_libraries(
      fsim_tcl_application_tests
      PRIVATE fsim_application
    )
    add_test(
      NAME fsim.application.tcl
      COMMAND fsim_tcl_application_tests
    )
    set_tests_properties(
      fsim.application.tcl
      PROPERTIES LABELS "fast;tcl"
    )
    if(FSIM_WITH_BUNDLED_TCL)
      set_tests_properties(
        fsim.application.tcl
        PROPERTIES
          ENVIRONMENT
            "FSIM_TCL_LIBRARY=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
      )
      add_test(
        NAME fsim.tcl.fetched-relocation
        COMMAND
          "${CMAKE_COMMAND}"
          "-DFSIM_EXECUTABLE=$<TARGET_FILE:fsim>"
          "-DFSIM_TCL_LIBRARY_DIR=${FSIM_FETCHED_TCL_LIBRARY_DIR}"
          "-DFSIM_TCL_LIBRARY_VERSION=${FSIM_TCL_LIBRARY_VERSION}"
          "-DFSIM_STAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/fetched-tcl-stage"
          "-DFSIM_SYSTEMC_BRIDGE_LIBRARY=$<TARGET_FILE:fsim_systemc_accellera_runtime>"
          "-DFSIM_SYSTEMC_UPSTREAM_LIBRARY=$<TARGET_FILE:${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>"
          -P
          "${PROJECT_SOURCE_DIR}/cmake/CheckFetchedTclRelocation.cmake"
      )
      set_tests_properties(
        fsim.tcl.fetched-relocation
        PROPERTIES LABELS "fast;tcl;cmake"
      )
    endif()
  endif()

  endif()

  add_executable(
    fsim_api_tests
    api/api_test.cpp
    api/api_mapped_library_test.cpp
    api/api_multiple_root_test.cpp
  )
  fsim_configure_test(fsim_api_tests)
  target_compile_definitions(
    fsim_api_tests
    PRIVATE FSIM_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  )
  target_link_libraries(
    fsim_api_tests PRIVATE fsim_api fsim_application)
  add_test(NAME fsim.api COMMAND fsim_api_tests)
  set_tests_properties(fsim.api PROPERTIES TIMEOUT 120)
  if(WIN32)
    add_custom_command(
      TARGET fsim_api_tests
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:fsim_api>"
        "$<TARGET_FILE_DIR:fsim_api_tests>"
      VERBATIM
    )
  endif()

  add_executable(fsim_api_header_c_test api/api_header_c_test.c)
  target_compile_features(fsim_api_header_c_test PRIVATE c_std_11)
  target_link_libraries(fsim_api_header_c_test PRIVATE fsim_api)
  set_target_properties(
    fsim_api_header_c_test
    PROPERTIES LINKER_LANGUAGE CXX
  )
  fsim_configure_test_platform(fsim_api_header_c_test)
  add_test(NAME fsim.api.c_header COMMAND fsim_api_header_c_test)
  if(WIN32)
    add_custom_command(
      TARGET fsim_api_header_c_test
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:fsim_api>"
        "$<TARGET_FILE_DIR:fsim_api_header_c_test>"
      VERBATIM
    )
  endif()
endif()

# Recursive closure drivers remain directly invokable when retained evidence is
# requested. In an ordinary CTest regression their witnesses are fixtures, so
# every underlying test executes once and the driver only records fixture
# completion instead of starting nested CTest processes.
set(FSIM_RECURSIVE_CLOSURE_TESTS
  fsim.systemc.accellera_closure
  fsim.sdf-closure
  fsim.sdf-application-closure
  fsim.sdf-vital-closure
  fsim.vhdl-standard-mode-closure-matrix
  fsim.verilog-systemverilog-standard-mode-closure-matrix
  fsim.systemverilog-closure-matrix
  fsim.verilog-closure-matrix
  fsim.fst-closure)

function(fsim_append_fixture_setup FSIM_TEST FSIM_FIXTURE)
  if(TEST "${FSIM_TEST}")
    set_property(
      TEST "${FSIM_TEST}"
      APPEND PROPERTY FIXTURES_SETUP "${FSIM_FIXTURE}")
  endif()
endfunction()

function(fsim_require_fixture_witnesses FSIM_DRIVER FSIM_FIXTURE)
  if(NOT TEST "${FSIM_DRIVER}")
    return()
  endif()
  set_tests_properties(
    "${FSIM_DRIVER}" PROPERTIES FIXTURES_REQUIRED "${FSIM_FIXTURE}")
  foreach(FSIM_WITNESS IN LISTS ARGN)
    fsim_append_fixture_setup("${FSIM_WITNESS}" "${FSIM_FIXTURE}")
  endforeach()
endfunction()

function(fsim_require_fixture_regex FSIM_DRIVER FSIM_FIXTURE FSIM_REGEX)
  if(NOT TEST "${FSIM_DRIVER}")
    return()
  endif()
  set_tests_properties(
    "${FSIM_DRIVER}" PROPERTIES FIXTURES_REQUIRED "${FSIM_FIXTURE}")
  get_property(FSIM_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
  foreach(FSIM_WITNESS IN LISTS FSIM_REGISTERED_TESTS)
    list(FIND FSIM_RECURSIVE_CLOSURE_TESTS "${FSIM_WITNESS}"
      FSIM_CLOSURE_INDEX)
    if(FSIM_CLOSURE_INDEX EQUAL -1 AND FSIM_WITNESS MATCHES "${FSIM_REGEX}")
      fsim_append_fixture_setup("${FSIM_WITNESS}" "${FSIM_FIXTURE}")
    endif()
  endforeach()
endfunction()

fsim_require_fixture_regex(
  fsim.systemc.accellera_closure fsim_systemc_accellera_closure_witnesses
  "^fsim\\.(systemc\\.(accellera_corpus|upstream\\.(simple_fifo|event_list|tlm_fifo)|shared_runtime|compatibility|kernel_backend_.*|tlm[12]_backend|shared-runtime-contract|abi-c|plugin|compiler|incremental|matrix)|systemc-accellera-(inventory|portability-contract)|systemc-upstream-provenance|systemc-portability-contract|installed-public-contract|resource-portability-contract|application\\.(systemc_trace|systemc_matrix|systemc_datatypes|systemc_tlm1|typed_boundaries|artifact_phases))$")
fsim_require_fixture_regex(
  fsim.sdf-closure fsim_sdf_closure_witnesses
  "^fsim\\.(frontend\\.sdf.*|application\\.sdf_.*|application$|application\\.artifact_phases$|project$|library\\.artifact$|artifact\\.design$|diagnostics-catalog$|source-line-budget$|sdf-inventory$|resource-portability-contract$)")
fsim_require_fixture_regex(
  fsim.sdf-application-closure fsim_sdf_application_closure_witnesses
  "^fsim\\.(application\\.(sdf_(application_corpus|path_timing|interconnect_timing|mixed_verilog|mixed_systemverilog|mixed_systemc|mixed_resolution|foreign_interfaces|vital_observability|vital_archive|vital_phases|delay_modes|primary_timing_checks|secondary_timing_checks|condition_timing|pulse_timing|precedence|scheduling|drive_timing|reannotation|control|effective_archive|observability)|specify|resolution|vpi|vcd_control)|library\\.artifact|artifact\\.(object|design)|cache|api|api\\.c_header|diagnostics-catalog|source-line-budget|sdf-application-inventory|resource-portability-contract)$")
fsim_require_fixture_witnesses(
  fsim.sdf-vital-closure fsim_sdf_vital_closure_witnesses
  fsim.application.sdf_vital_corpus
  fsim.application.sdf_vital_models
  fsim.application.sdf_mixed_resolution
  fsim.application.sdf_vital_archive
  fsim.application.sdf_vital_phases
  fsim.resource-portability-contract)
fsim_require_fixture_witnesses(
  fsim.vhdl-standard-mode-closure-matrix
  fsim_vhdl_standard_mode_closure_witnesses
  fsim.vhdl-standard-mode-inventory
  fsim.frontend
  fsim.application.vhdl_ieee_integration
  fsim.application.vhdl_numeric
  fsim.application.vhdl_logic9
  fsim.application.artifact_phases
  fsim.llvm
  fsim.api
  fsim.api.c_header
  fsim.tcl.version-selection
  fsim.msvc-debug-contract
  fsim.msvc-release-contract
  fsim.windows-llvm-contract
  fsim.tool-portability-contract
  fsim.resource-portability-contract)
fsim_require_fixture_witnesses(
  fsim.verilog-systemverilog-standard-mode-closure-matrix
  fsim_verilog_systemverilog_standard_mode_closure_witnesses
  fsim.verilog-systemverilog-standard-mode-inventory
  fsim.frontend
  fsim.application.sv_conformance
  fsim.application.typed_boundaries
  fsim.application.artifact_phases
  fsim.llvm
  fsim.application.vpi
  fsim.application.tcl
  fsim.api
  fsim.api.c_header
  fsim.runtime
  fsim.msvc-debug-contract
  fsim.msvc-release-contract
  fsim.windows-llvm-contract
  fsim.tool-portability-contract
  fsim.resource-portability-contract)
fsim_require_fixture_witnesses(
  fsim.systemverilog-closure-matrix
  fsim_systemverilog_closure_witnesses
  fsim.semantic
  fsim.application.systemverilog_hir
  fsim.frontend
  fsim.library.artifact
  fsim.llvm
  fsim.application.vpi
  fsim.application.artifact_phases
  fsim.application.classes
  fsim.application.expressions
  fsim.application.sv_containers
  fsim.application.assertions
  fsim.application.typed_boundaries
  fsim.uvm-phase-tlm-matrix
  fsim.runtime)
fsim_require_fixture_witnesses(
  fsim.verilog-closure-matrix fsim_verilog_closure_witnesses
  fsim.application.systemverilog_hir
  fsim.frontend
  fsim.library.artifact
  fsim.elaboration
  fsim.llvm
  fsim.application.vpi
  fsim.application
  fsim.application.specialization
  fsim.application.artifact_phases
  fsim.application.expressions
  fsim.application.specify
  fsim.application.fork
  fsim.application.display
  fsim.application.line_directives
  fsim.application.transition_delays
  fsim.application.procedural_assignments
  fsim.application.resolution
  fsim.application.ordering_interactions
  fsim.application.sv_files
  fsim.application.typed_boundaries
  fsim.api
  fsim.api.c_header
  fsim.runtime)
fsim_require_fixture_witnesses(
  fsim.fst-closure fsim_fst_closure_witnesses
  fsim.application.fst_corpus
  fsim.application.trace_control
  fsim.application.trace_observation
  fsim.application.vcd_control
  fsim.application.core_non_project_cli
  fsim.application.trace_archive
  fsim.artifact.design
  fsim.runtime.fst_reader
  fsim.fst-portability-contract)

# Windows does not search sibling build directories for dependent DLLs. Every
# build-tree executable links the one process-wide fsim bridge, which in turn
# loads the one official SystemC runtime, so expose both target directories to
# CTest without copying runtime binaries into each test directory.
if(WIN32)
  function(fsim_configure_windows_test_runtime directory)
    get_property(directory_tests DIRECTORY "${directory}" PROPERTY TESTS)
    if(directory_tests)
      set_tests_properties(
        ${directory_tests}
        DIRECTORY "${directory}"
        PROPERTIES
          ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:fsim_systemc_accellera_runtime>;PATH=path_list_prepend:$<TARGET_FILE_DIR:${FSIM_SYSTEMC_UPSTREAM_RUNTIME_TARGET}>")
    endif()
    get_property(child_directories
      DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child_directory IN LISTS child_directories)
      fsim_configure_windows_test_runtime("${child_directory}")
    endforeach()
  endfunction()
  fsim_configure_windows_test_runtime("${CMAKE_CURRENT_SOURCE_DIR}")
endif()

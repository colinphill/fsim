# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ROOT "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_FOOTPRINT "${FSIM_SOURCE_DIR}/cmake/FsimDebugFootprint.cmake")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_RUNTIME_TEST_CMAKE
  "${FSIM_SOURCE_DIR}/tests/runtime/CMakeLists.txt")
set(FSIM_FUZZ_CMAKE "${FSIM_SOURCE_DIR}/tests/fuzz/CMakeLists.txt")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_SCOPED "${FSIM_SOURCE_DIR}/tests/app/scoped_local_application_test.cpp")
set(FSIM_SYSTEMC "${FSIM_SOURCE_DIR}/tests/app/application_systemc_matrix_test.cpp")
set(FSIM_COVERAGE "${FSIM_SOURCE_DIR}/src/frontend/coverage_sampling.cpp")
set(FSIM_CODE_COVERAGE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/code_coverage.hpp")
set(FSIM_CODE_COVERAGE_MODEL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/code_coverage.cpp")
set(FSIM_CODE_COVERAGE_MODEL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/code_coverage_model_test.cpp")
set(FSIM_CODE_COVERAGE_SOURCE
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/coverage_source_identity.hpp")
set(FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/frontend/coverage_source_identity.cpp")
set(FSIM_CODE_COVERAGE_SOURCE_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/coverage_source_identity_test.cpp")
set(FSIM_COVERAGE_SOURCE_CONTROL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/coverage_source_control.hpp")
set(FSIM_COVERAGE_SOURCE_CONTROL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/frontend/coverage_source_control.cpp")
set(FSIM_COVERAGE_SOURCE_CONTROL_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_source_control_test.cpp")
set(FSIM_COVERAGE_EXTERNAL_EXCLUSIONS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_external_exclusions.hpp")
set(FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_external_exclusions.cpp")
set(FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_external_exclusions_test.cpp")
set(FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_APPLICATION
  "${FSIM_SOURCE_DIR}/src/app/application_build.cpp")
set(FSIM_COVERAGE_EXCLUSION_PERSISTENCE
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_exclusion_persistence.hpp")
set(FSIM_COVERAGE_EXCLUSION_PERSISTENCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_exclusion_persistence.cpp")
set(FSIM_COVERAGE_EXCLUSION_PERSISTENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_exclusion_persistence_test.cpp")
set(FSIM_COVERAGE_EXCLUSION_REPORT
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_exclusion_report.hpp")
set(FSIM_COVERAGE_EXCLUSION_REPORT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_exclusion_report.cpp")
set(FSIM_COVERAGE_EXCLUSION_REPORT_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_exclusion_report_test.cpp")
set(FSIM_COVERAGE_REPORT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_report_model.hpp")
set(FSIM_COVERAGE_REPORT_MODEL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_report_model.cpp")
set(FSIM_COVERAGE_REPORT_MODEL_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_report_model_test.cpp")
set(FSIM_COVERAGE_REPORT_RENDER
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_report_render.hpp")
set(FSIM_COVERAGE_REPORT_RENDER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_report_render.cpp")
set(FSIM_COVERAGE_REPORT_RENDER_INTERNAL
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_report_render_internal.hpp")
set(FSIM_COVERAGE_REPORT_RENDER_HTML
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_report_render_html.cpp")
set(FSIM_COVERAGE_REPORT_RENDER_JSON
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_report_render_json.cpp")
set(FSIM_COVERAGE_REPORT_RENDER_TEXT
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_report_render_text.cpp")
set(FSIM_COVERAGE_REPORT_RENDER_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_report_render_test.cpp")
set(FSIM_COVERAGE_REPORT_PROJECTION
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_report_projection.hpp")
set(FSIM_COVERAGE_REPORT_PROJECTION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_report_projection.cpp")
set(FSIM_COVERAGE_REPORT_PROJECTION_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_report_projection_test.cpp")
set(FSIM_COVERAGE_COMMAND
  "${FSIM_SOURCE_DIR}/src/app/application_coverage_command.cpp")
set(FSIM_COVERAGE_COMMAND_CLI
  "${FSIM_SOURCE_DIR}/include/fsim/cli/driver.hpp")
set(FSIM_COVERAGE_COMMAND_CLI_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/cli/driver.cpp")
set(FSIM_COVERAGE_COMMAND_TEST
  "${FSIM_SOURCE_DIR}/tests/app/coverage_command_test.cpp")
set(FSIM_COVERAGE_DATABASE_ROBUSTNESS_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_database_robustness_test.cpp")
set(FSIM_CODE_COVERAGE_POINT
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/coverage_point_identity.hpp")
set(FSIM_CODE_COVERAGE_POINT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/frontend/coverage_point_identity.cpp")
set(FSIM_CODE_COVERAGE_POINT_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/coverage_point_identity_test.cpp")
set(FSIM_VERILOG_COVERAGE_POINTS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/verilog_coverage_points.hpp")
set(FSIM_VERILOG_COVERAGE_POINTS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/verilog_coverage_points.cpp")
set(FSIM_VERILOG_COVERAGE_POINTS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/verilog_coverage_points_test.cpp")
set(FSIM_VERILOG_COVERAGE_CONDITIONS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/verilog_coverage_conditions.hpp")
set(FSIM_VERILOG_COVERAGE_CONDITIONS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/verilog_coverage_conditions.cpp")
set(FSIM_VERILOG_COVERAGE_CONDITIONS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/verilog_coverage_conditions_test.cpp")
set(FSIM_COVERAGE_CONDITIONS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_conditions.hpp")
set(FSIM_COVERAGE_CONDITIONS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_conditions.cpp")
set(FSIM_VHDL_COVERAGE_CONDITIONS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/vhdl_coverage_conditions.hpp")
set(FSIM_VHDL_COVERAGE_CONDITIONS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/vhdl_coverage_conditions.cpp")
set(FSIM_VHDL_COVERAGE_CONDITIONS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/vhdl_coverage_conditions_test.cpp")
set(FSIM_COVERAGE_CONDITION_EVALUATION
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/coverage_condition_evaluation.hpp")
set(FSIM_COVERAGE_CONDITION_EVALUATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/coverage_condition_evaluation.cpp")
set(FSIM_COVERAGE_CONDITION_EVALUATION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/coverage_condition_evaluation_test.cpp")
set(FSIM_COVERAGE_CONDITION_OUTCOMES
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/coverage_condition_outcomes.hpp")
set(FSIM_COVERAGE_CONDITION_OUTCOMES_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/coverage_condition_outcomes.cpp")
set(FSIM_COVERAGE_CONDITION_OUTCOMES_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/coverage_condition_outcomes_test.cpp")
set(FSIM_COVERAGE_EXPRESSION
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/coverage_expression.hpp")
set(FSIM_COVERAGE_EXPRESSION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/coverage_expression.cpp")
set(FSIM_COVERAGE_EXPRESSION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/coverage_expression_test.cpp")
set(FSIM_COVERAGE_TOGGLE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/coverage_toggle.hpp")
set(FSIM_COVERAGE_TOGGLE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/coverage_toggle.cpp")
set(FSIM_COVERAGE_TOGGLE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/coverage_toggle_test.cpp")
set(FSIM_VERILOG_TOGGLE_INVENTORY
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/verilog_toggle_inventory.hpp")
set(FSIM_VERILOG_TOGGLE_INVENTORY_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/verilog_toggle_inventory.cpp")
set(FSIM_VERILOG_TOGGLE_INVENTORY_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/verilog_toggle_inventory_test.cpp")
set(FSIM_VHDL_TOGGLE_INVENTORY
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/vhdl_toggle_inventory.hpp")
set(FSIM_VHDL_TOGGLE_INVENTORY_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/vhdl_toggle_inventory.cpp")
set(FSIM_VHDL_TOGGLE_INVENTORY_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/vhdl_toggle_inventory_test.cpp")
set(FSIM_COVERAGE_TOGGLE_SELECTION
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_toggle_selection.hpp")
set(FSIM_COVERAGE_TOGGLE_SELECTION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_toggle_selection.cpp")
set(FSIM_COVERAGE_TOGGLE_SELECTION_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_toggle_selection_test.cpp")
set(FSIM_COVERAGE_MEMORY_TOGGLE
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_memory_toggle.hpp")
set(FSIM_COVERAGE_MEMORY_TOGGLE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_memory_toggle.cpp")
set(FSIM_COVERAGE_MEMORY_TOGGLE_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_memory_toggle_test.cpp")
set(FSIM_COVERAGE_FSM_INFERENCE
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_fsm_inference.hpp")
set(FSIM_COVERAGE_FSM_INFERENCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_fsm_inference.cpp")
set(FSIM_COVERAGE_FSM_INFERENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_fsm_inference_test.cpp")
set(FSIM_COVERAGE_FSM_HINTS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_fsm_hints.hpp")
set(FSIM_COVERAGE_FSM_HINTS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_fsm_hints.cpp")
set(FSIM_COVERAGE_FSM_HINTS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_fsm_hints_test.cpp")
set(FSIM_COVERAGE_FSM_HINTS_PROJECT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/project/project.cpp")
set(FSIM_COVERAGE_FSM_RUNTIME
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/coverage_fsm.hpp")
set(FSIM_COVERAGE_FSM_RUNTIME_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/coverage_fsm.cpp")
set(FSIM_COVERAGE_FSM_RUNTIME_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/coverage_fsm_test.cpp")
set(FSIM_COVERAGE_FSM_VALIDATION
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_fsm_validation.hpp")
set(FSIM_COVERAGE_FSM_VALIDATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_fsm_validation.cpp")
set(FSIM_COVERAGE_FSM_VALIDATION_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_fsm_validation_test.cpp")
set(FSIM_FRONTEND_DESIGN
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design.hpp")
set(FSIM_VERILOG_PARSER_CORE
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_core.cpp")
set(FSIM_VHDL_COVERAGE_POINTS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/vhdl_coverage_points.hpp")
set(FSIM_VHDL_COVERAGE_POINTS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/vhdl_coverage_points.cpp")
set(FSIM_VHDL_COVERAGE_POINTS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/vhdl_coverage_points_test.cpp")
set(FSIM_COVERAGE_BRANCHES
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_branches.hpp")
set(FSIM_COVERAGE_BRANCHES_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_branches.cpp")
set(FSIM_COVERAGE_BRANCHES_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_branches_test.cpp")
set(FSIM_COVERAGE_LINE_STATE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/coverage_line_state.hpp")
set(FSIM_COVERAGE_LINE_STATE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/coverage_line_state.cpp")
set(FSIM_COVERAGE_LINE_STATE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/coverage_line_state_test.cpp")
set(FSIM_COVERAGE_AGGREGATION
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/coverage_aggregation.hpp")
set(FSIM_COVERAGE_AGGREGATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/coverage_aggregation.cpp")
set(FSIM_COVERAGE_AGGREGATION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/coverage_aggregation_test.cpp")
set(FSIM_COVERAGE_INVENTORY
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_inventory.hpp")
set(FSIM_COVERAGE_INSTANCE_IDENTITY
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_instance_identity.hpp")
set(FSIM_COVERAGE_INSTANCE_IDENTITY_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_instance_identity.cpp")
set(FSIM_COVERAGE_INSTANCE_IDENTITY_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_instance_identity_test.cpp")
set(FSIM_COVERAGE_INVENTORY_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_inventory.cpp")
set(FSIM_COVERAGE_INVENTORY_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_inventory_test.cpp")
set(FSIM_COVERAGE_POINTS
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/coverage_points.hpp")
set(FSIM_COVERAGE_POINTS_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/elaboration/coverage_points.cpp")
set(FSIM_COVERAGE_POINTS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/coverage_points_test.cpp")
set(FSIM_SIMIR_COVERAGE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/simir_coverage.hpp")
set(FSIM_SIMIR_COVERAGE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/simir_coverage.cpp")
set(FSIM_SIMIR_COVERAGE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/simir_coverage_test.cpp")
set(FSIM_LLVM_COVERAGE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_coverage.cpp")
set(FSIM_LLVM_COVERAGE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/code_coverage_application_test.cpp")
set(FSIM_DEBUG_COVERAGE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_debug_coverage.cpp")
set(FSIM_CODE_COVERAGE_CONTROL_PROJECT
  "${FSIM_SOURCE_DIR}/include/fsim/project/project.hpp")
set(FSIM_CODE_COVERAGE_CONTROL_CLI
  "${FSIM_SOURCE_DIR}/include/fsim/cli/driver.hpp")
set(FSIM_CODE_COVERAGE_CONTROL_CLI_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/cli/driver.cpp")
set(FSIM_CODE_COVERAGE_CONTROL_APPLICATION
  "${FSIM_SOURCE_DIR}/include/fsim/app/application.hpp")
set(FSIM_CODE_COVERAGE_CONTROL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_coverage_control.cpp")
set(FSIM_CODE_COVERAGE_CONTROL_TEST
  "${FSIM_SOURCE_DIR}/tests/app/code_coverage_control_test.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PREPROCESSOR
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_preprocessor.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_expressions.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_expression_system.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_OPERATIONS
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/simir_operations_extended.hpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_coverage.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_INTERPRETER
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_interpreter.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_BOUNDARIES
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_boundaries.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LLVM
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_lowering_operations_suffix.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_cache_key_operations_secondary.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_APPLICATION
  "${FSIM_SOURCE_DIR}/src/app/application_simulation_impl_setup.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_APPLICATION
  "${FSIM_SOURCE_DIR}/src/app/application_simulation_impl_coverage.cpp")
set(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_TEST
  "${FSIM_SOURCE_DIR}/tests/compiler/llvm_jit_control_cache_test.cpp")
set(FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vpi_abi.h")
set(FSIM_SYSTEMVERILOG_VPI_COVERAGE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vpi_coverage.hpp")
set(FSIM_SYSTEMVERILOG_VPI_COVERAGE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/vpi_coverage.cpp")
set(FSIM_SYSTEMVERILOG_VPI_COVERAGE_OBJECT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/vpi_object.cpp")
set(FSIM_SYSTEMVERILOG_VPI_COVERAGE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/vpi_coverage_test.cpp")
set(FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vpi_abi_c_test.c")
set(FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_identity.hpp")
set(FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_identity.cpp")
set(FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_identity_test.cpp")
set(FSIM_CODE_COVERAGE_EQUIVALENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/code_coverage_equivalence_test.cpp")
set(FSIM_CODE_COVERAGE_METRICS_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/code_coverage_metrics_application_test.cpp")
set(FSIM_CODE_COVERAGE_METRICS_EQUIVALENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/code_coverage_metrics_equivalence_test.cpp")
set(FSIM_CODE_COVERAGE_METRICS_IDENTITY_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_metrics_identity_test.cpp")
set(FSIM_CODE_COVERAGE_METRICS_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/code_coverage_metrics_inventory.tsv")
set(FSIM_CODE_COVERAGE_METRICS_CHECKER
  "${FSIM_SOURCE_DIR}/cmake/CheckCodeCoverageMetricsInventory.cmake")
set(FSIM_LEGACY_TF_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/legacy_tf_inventory.tsv")
set(FSIM_LEGACY_TF_CHECKER
  "${FSIM_SOURCE_DIR}/cmake/CheckLegacyTfInventory.cmake")
set(FSIM_LEGACY_ACC_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/legacy_acc_inventory.tsv")
set(FSIM_LEGACY_ACC_CHECKER
  "${FSIM_SOURCE_DIR}/cmake/CheckLegacyAccInventory.cmake")
set(FSIM_VHDL_2019_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/vhdl_2019_inventory.tsv")
set(FSIM_VHDL_2019_CHECKER
  "${FSIM_SOURCE_DIR}/cmake/CheckVhdl2019Inventory.cmake")
set(FSIM_SYSTEMVERILOG_2023_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemverilog_2023_inventory.tsv")
set(FSIM_SYSTEMVERILOG_2023_CHECKER
  "${FSIM_SOURCE_DIR}/cmake/CheckSystemVerilog2023Inventory.cmake")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_assertions.cpp")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_assert.cpp")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_TASK_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_tasks.cpp")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_fork.cpp")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_BOUNDARIES
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_boundaries.cpp")
set(FSIM_SYSTEMVERILOG_2023_EXECUTION_SHARED
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_shared.cpp")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_declaration_tests.cpp")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/assertion_application_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_ADVANCED_TEST
  "${FSIM_SOURCE_DIR}/tests/app/assertion_application_advanced_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design.hpp")
set(FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_RESOLUTION
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_assertion_resolution.cpp")
set(FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_SEMANTIC_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/systemverilog_hir.hpp")
set(FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_systemverilog_hir.cpp")
set(FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_interface_tests.cpp")
set(FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR_TEST
  "${FSIM_SOURCE_DIR}/tests/app/systemverilog_hir_application_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design.hpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_assertions.cpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_INSTANCE_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_units.cpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_MODEL
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_internal.hpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_core.cpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_RESOLUTION
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_checker_resolution.cpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_sv_conformance_tests.cpp")
set(FSIM_SYSTEMVERILOG_2023_CHECKER_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/assertion_application_checker.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_classes.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/systemverilog_hir.hpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_systemverilog_hir.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_RESOLUTION
  "${FSIM_SOURCE_DIR}/src/frontend/class_expression_resolution.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_INLINE_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_expression_unary.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_LOWERING
  "${FSIM_SOURCE_DIR}/src/app/application_constraint_lowering.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/class_randomize.hpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/class_randomize.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_TEMPLATE_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/constraint_expression.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION
  "${FSIM_SOURCE_DIR}/src/app/application_class_randomization.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_OBJECTS
  "${FSIM_SOURCE_DIR}/src/app/application_class_execution.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_LLVM_VALIDATION
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_validation_class.hpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_class_tests.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_constraint_solver_tests.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/random_application_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_TEST
  "${FSIM_SOURCE_DIR}/tests/app/systemverilog_hir_application_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_HIERARCHY
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_instantiate_processes_and_children.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_CONTROL
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_control.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_EXPRESSION
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_expression.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_process_part2.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS_CORE
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_process.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_STATE
  "${FSIM_SOURCE_DIR}/src/runtime/simir_state.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_SCHEDULER
  "${FSIM_SOURCE_DIR}/src/runtime/scheduler.cpp")
set(FSIM_SYSTEMVERILOG_2023_PROCESS_STATE
  "${FSIM_SOURCE_DIR}/src/runtime/simir_internal.hpp")
set(FSIM_SYSTEMVERILOG_2023_PROCESS_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_callable_context.cpp")
set(FSIM_SYSTEMVERILOG_2023_PROCESS_FORK_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_fork.cpp")
set(FSIM_SYSTEMVERILOG_2023_PROCESS_TEST
  "${FSIM_SOURCE_DIR}/tests/app/fork_application_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/simir.hpp")
set(FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CONTEXT
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_context.hpp")
set(FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_assignment.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CAST
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_expression.cpp")
set(FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_TEST
  "${FSIM_SOURCE_DIR}/tests/app/sv_conformance_application_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_STREAM_CONTROL
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_control.cpp")
set(FSIM_SYSTEMVERILOG_2023_STREAM_EXPRESSION
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_expression_system.cpp")
set(FSIM_SYSTEMVERILOG_2023_OPERATOR_TOKENS
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/token.hpp")
set(FSIM_SYSTEMVERILOG_2023_OPERATOR_LEXER
  "${FSIM_SOURCE_DIR}/src/frontend/lexer.cpp")
set(FSIM_SYSTEMVERILOG_2023_OPERATOR_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_expressions.cpp")
set(FSIM_SYSTEMVERILOG_2023_OPERATOR_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_expression_membership.cpp")
set(FSIM_SYSTEMVERILOG_2023_CALLABLE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_SYSTEMVERILOG_2023_CALLABLE_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_functions.cpp")
set(FSIM_SYSTEMVERILOG_2023_CALLABLE_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_callables.cpp")
set(FSIM_SYSTEMVERILOG_2023_CALLABLE_PORTABLE
  "${FSIM_SOURCE_DIR}/src/library/portable_unit.cpp")
set(FSIM_SYSTEMVERILOG_2023_CALLABLE_SCHEMA
  "${FSIM_SOURCE_DIR}/include/fsim/library/portable_unit.hpp")
set(FSIM_SYSTEMVERILOG_2023_CALLABLE_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_declaration_tests.cpp")
set(FSIM_SYSTEMVERILOG_2023_CLOCKING_TIME
  "${FSIM_SOURCE_DIR}/src/app/application_time.cpp")
set(FSIM_SYSTEMVERILOG_2023_CLOCKING_CONSTANTS
  "${FSIM_SOURCE_DIR}/src/elaboration/elaboration_sv_constant_services.cpp")
set(FSIM_SYSTEMVERILOG_2023_CLOCKING_ELABORATION
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_instantiate_processes_and_children.cpp")
set(FSIM_SYSTEMVERILOG_2023_CLOCKING_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_process.cpp")
set(FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_synchronization.cpp")
set(FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_synchronization_tests.cpp")
set(FSIM_SYSTEMVERILOG_2023_CLOCKING_TEST
  "${FSIM_SOURCE_DIR}/tests/app/synchronization_application_test.cpp")
set(FSIM_SYSTEMVERILOG_2023_PROFILE_CLASS_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_classes.cpp")
set(FSIM_SYSTEMVERILOG_2023_PROFILE_PROCESS_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/verilog_parser_processes.cpp")
set(FSIM_SYSTEMVERILOG_2023_PROFILE_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_sv_conformance_tests.cpp")
set(FSIM_VHDL_2019_FRONTEND_IDENTITY
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/token.hpp")
set(FSIM_VHDL_2019_PROJECT_IDENTITY
  "${FSIM_SOURCE_DIR}/include/fsim/project/project.hpp")
set(FSIM_VHDL_2019_PROJECT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/project/project.cpp")
set(FSIM_VHDL_2019_ANALYSIS_IDENTITY
  "${FSIM_SOURCE_DIR}/src/app/application_analysis.cpp")
set(FSIM_VHDL_2019_CLI_IDENTITY
  "${FSIM_SOURCE_DIR}/src/cli/driver.cpp")
set(FSIM_VHDL_2019_PORTABLE_IDENTITY
  "${FSIM_SOURCE_DIR}/src/library/portable_unit.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_conditional_analysis.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_INTERNAL
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_conditional_analysis_internal.hpp")
set(FSIM_VHDL_2019_CONDITIONAL_DRIVER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_expressions.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/vhdl_hir.hpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir.cpp")
set(FSIM_VHDL_HIR_BUILDERS
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_builder.cpp")
set(FSIM_VHDL_HIR_CALLABLE_BUILDERS
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_callables.cpp")
set(FSIM_VHDL_HIR_TYPE_BUILDERS
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_types.cpp")
set(FSIM_VHDL_HIR_INTERNAL
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_internal.hpp")
set(FSIM_VHDL_HIR_STATEMENT_BUILDERS
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_statements.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_EXECUTABLE_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_executable_hir.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_condition.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_TYPES
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_overloads.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_type_tests.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST_PART2
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_type_tests_part2.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_ELABORATION_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_expression_test.cpp")
set(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_2019_integer_application_test.cpp")
set(FSIM_VHDL_2019_RESULT_SUBTYPE_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_functions.cpp")
set(FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_functions.cpp")
set(FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING_PART2
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_functions_part2.cpp")
set(FSIM_VHDL_2019_RESULT_SUBTYPE_STORAGE
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_process.cpp")
set(FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_types.cpp")
set(FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES_PART2
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_types_part2.cpp")
set(FSIM_VHDL_2019_RESULT_SUBTYPE_ELABORATION_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_vhdl_function_test.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/vhdl_hir.hpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_statements.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_statements.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_EXECUTABLE_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_executable_hir.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_process.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING_PART2
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_process_part2.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_statement_tests.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_2019_integer_application_test.cpp")
set(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_RUNTIME_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_projected_application_test.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/parser.hpp")
set(FSIM_VHDL_2019_SIMULATOR_API_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_packages.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_REAL_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_expressions.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_analysis.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_executable_hir.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_functions.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_procedures.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/simir_operations_extended.hpp")
set(FSIM_VHDL_2019_SIMULATOR_API_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_boundaries.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_JIT
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_lowering_operations_suffix.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_validation_support.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR
  "${FSIM_SOURCE_DIR}/src/app/application_executors.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_sv_strings.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_TYPE_INFERENCE
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_vhdl_array_selection.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_INTEGER_INFERENCE
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_types.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_TIME_NORMALIZATION
  "${FSIM_SOURCE_DIR}/src/app/application_time.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_APPLICATION
  "${FSIM_SOURCE_DIR}/src/app/application_run_commands.cpp")
set(FSIM_VHDL_2019_SIMULATOR_API_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_ieee_integration_vhdl2019.cpp")
set(FSIM_VHDL_2019_ASSERT_API_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_part2.cpp")
set(FSIM_VHDL_2019_ASSERT_API_TEXTIO
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_vhdl_textio.cpp")
set(FSIM_VHDL_2019_ASSERT_API_EXECUTOR
  "${FSIM_SOURCE_DIR}/src/app/application_executor_services.cpp")
set(FSIM_VHDL_2019_ASSERT_API_VALIDATION
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_validation_operations_prefix.cpp")
set(FSIM_VHDL_2019_REFLECTION_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_core.cpp")
set(FSIM_VHDL_2019_REFLECTION_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_vhdl_protected.cpp")
set(FSIM_VHDL_2019_REFLECTION_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_part2.cpp")
set(FSIM_VHDL_2019_REFLECTION_CACHE_KEY
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_cache_key_operations.cpp")
set(FSIM_VHDL_2019_REFLECTION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_advanced_type_application_test.cpp")
set(FSIM_VHDL_2019_PREDEFINED_PACKAGE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/library/artifact.hpp")
set(FSIM_VHDL_2019_PREDEFINED_PACKAGE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_standard_library.cpp")
set(FSIM_VHDL_2019_VHPI_OBJECT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vhpi_object.hpp")
set(FSIM_VHDL_2019_VHPI_OBJECT_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/vhpi_object.cpp")
set(FSIM_VHDL_2019_VHPI_TYPE_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/vhpi_type.cpp")
set(FSIM_VHDL_2019_VHPI_CHECKPOINT_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/vhpi_checkpoint.cpp")
set(FSIM_VHDL_2019_VHPI_APPLICATION
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_debug.cpp")
set(FSIM_VHDL_2019_VHPI_OBJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_object_tests.cpp")
set(FSIM_VHDL_2019_VHPI_HIERARCHY_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_hierarchy_tests.cpp")
set(FSIM_VHDL_2019_VHPI_CHECKPOINT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_checkpoint_tests.cpp")
set(FSIM_VHDL_2019_VHPI_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_type_generic_application_test.cpp")
set(FSIM_VHDL_2019_VHPI_DOCUMENTATION
  "${FSIM_SOURCE_DIR}/docs/vhdl-vhpi.md")
set(FSIM_VHDL_2019_VHPI_ABI
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vhpi_abi.h")
set(FSIM_VHDL_2019_VHPI_PLUGIN_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vhpi_plugin.hpp")
set(FSIM_VHDL_2019_VHPI_PLUGIN_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/vhpi_plugin.cpp")
set(FSIM_VHDL_2019_VHPI_CALLBACK_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vhpi_callback.hpp")
set(FSIM_VHDL_2019_VHPI_CALLBACK_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/vhpi_callback.cpp")
set(FSIM_VHDL_2019_VHPI_CALLBACK_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_callback_tests.cpp")
set(FSIM_VHDL_2019_VHPI_REFERENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_reference_plugin_tests.cpp")
set(FSIM_VHDL_2019_VHPI_C_ABI_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_abi_c_test.c")
set(FSIM_VHDL_2019_VHPI_C_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/vhpi_reference_plugin_c.c")
set(FSIM_VHDL_2019_VHPI_CPP_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/vhpi_reference_plugin_cpp.cpp")
set(FSIM_VHDL_2019_FOREIGN_ABI_CHECKER
  "${FSIM_SOURCE_DIR}/cmake/CheckForeignAbiFreeze.cmake")
set(FSIM_VHDL_2019_PSL_API_RUNTIME
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_psl.cpp")
set(FSIM_VHDL_2019_PSL_API_INTERPRETER_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/simir.hpp")
set(FSIM_VHDL_2019_PSL_API_INTERPRETER_SETUP
  "${FSIM_SOURCE_DIR}/src/runtime/simir_interpreter.cpp")
set(FSIM_VHDL_2019_PSL_API_INTERPRETER
  "${FSIM_SOURCE_DIR}/src/runtime/simir_execution_interpreter.cpp")
set(FSIM_VHDL_2019_PSL_API_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_psl_application_test.cpp")
set(FSIM_VHDL_2019_COVERAGE_POINTS
  "${FSIM_SOURCE_DIR}/src/elaboration/vhdl_coverage_points.cpp")
set(FSIM_VHDL_2019_COVERAGE_POINTS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/vhdl_coverage_points_test.cpp")
set(FSIM_VHDL_2019_PSL_COVERAGE_DATABASE
  "${FSIM_SOURCE_DIR}/src/app/application_coverage_database_psl.cpp")
set(FSIM_VHDL_2019_PSL_COVERAGE_DATABASE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/coverage_database_psl_test.cpp")
set(FSIM_VHDL_2019_TYPED_BOUNDARY_TEST
  "${FSIM_SOURCE_DIR}/tests/app/typed_boundary_application_test.cpp")
set(FSIM_VHDL_2019_ARTIFACT_PHASE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_ATTRIBUTE_HIR_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/vhdl_hir.hpp")
set(FSIM_VHDL_2019_ATTRIBUTE_TYPE_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_core.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_EXPRESSION_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_expressions.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_ALIAS_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_declarations.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_HIR_TYPES
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_types.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_MODE_VIEW
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_mode_view.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_TYPE_RESOLUTION
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_type_resolution.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_TYPE_INFERENCE
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_types.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_vhdl_attributes.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_CODEC
  "${FSIM_SOURCE_DIR}/src/app/application_design_artifact_codec.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_attribute_tests.cpp")
set(FSIM_VHDL_2019_ATTRIBUTE_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_type_generic_application_test.cpp")
set(FSIM_VHDL_2019_OVERLOAD_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_OVERLOAD_COMMON
  "${FSIM_SOURCE_DIR}/src/frontend/common.cpp")
set(FSIM_VHDL_2019_OVERLOAD_FUNCTIONS
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_functions.cpp")
set(FSIM_VHDL_2019_OVERLOAD_PROCEDURES
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_procedures.cpp")
set(FSIM_VHDL_2019_OVERLOAD_RESOLUTION
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_overloads.cpp")
set(FSIM_VHDL_2019_OVERLOAD_PACKAGES
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_packages.cpp")
set(FSIM_VHDL_2019_OVERLOAD_PROTECTED
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_vhdl_protected.cpp")
set(FSIM_VHDL_2019_OVERLOAD_GENERIC_SUBPROGRAMS
  "${FSIM_SOURCE_DIR}/src/elaboration/elaboration_vhdl_subprograms.cpp")
set(FSIM_VHDL_2019_OVERLOAD_TYPE_SPECIALIZATION
  "${FSIM_SOURCE_DIR}/src/elaboration/elaboration_type_generics.cpp")
set(FSIM_VHDL_2019_OVERLOAD_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_executable_hir.cpp")
set(FSIM_VHDL_2019_OVERLOAD_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_vhdl_overload_test.cpp")
set(FSIM_VHDL_2019_OVERLOAD_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_type_generic_application_test.cpp")
set(FSIM_VHDL_2019_PROFILE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design.hpp")
set(FSIM_VHDL_2019_PROFILE_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_core.cpp")
set(FSIM_VHDL_2019_PROFILE_CONDITIONAL
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser.cpp")
set(FSIM_VHDL_2019_PROFILE_ATTRIBUTES
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_expressions.cpp")
set(FSIM_VHDL_2019_PROFILE_APPLICATION
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_analysis.cpp")
set(FSIM_VHDL_2019_PROFILE_APPLICATION_CHECK
  "${FSIM_SOURCE_DIR}/src/app/application_check.cpp")
set(FSIM_VHDL_2019_PROFILE_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir.cpp")
set(FSIM_VHDL_2019_PROFILE_ELABORATION
  "${FSIM_SOURCE_DIR}/src/elaboration/elaborate.cpp")
set(FSIM_VHDL_2019_PROFILE_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_revision_profile_tests.cpp")
set(FSIM_VHDL_2019_PROFILE_ELABORATION_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_vhdl_attribute_test.cpp")
set(FSIM_VHDL_2019_PROFILE_LEGACY_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_enumeration_application_test.cpp")
set(FSIM_VHDL_2019_ARTIFACT_LIBRARY_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/library/artifact.hpp")
set(FSIM_VHDL_2019_ARTIFACT_PORTABLE_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/library/portable_unit.hpp")
set(FSIM_VHDL_2019_ARTIFACT_DESIGN_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/app/design_artifact.hpp")
set(FSIM_VHDL_2019_ARTIFACT_PORTABLE_CODEC
  "${FSIM_SOURCE_DIR}/src/library/portable_unit.cpp")
set(FSIM_VHDL_2019_ARTIFACT_DESIGN_CODEC
  "${FSIM_SOURCE_DIR}/src/app/application_design_artifact_codec.cpp")
set(FSIM_VHDL_2019_ARTIFACT_PORTABLE_TEST
  "${FSIM_SOURCE_DIR}/tests/library/library_artifact_test.cpp")
set(FSIM_VHDL_2019_ARTIFACT_DESIGN_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_VHDL_2019_ARTIFACT_PORTABLE_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/portable_object_contract.tsv")
set(FSIM_VHDL_2019_ARTIFACT_NESTED_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/nested_portable_contract.tsv")
set(FSIM_VHDL_2019_ARTIFACT_STALE_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/portable_stale_schema_contract.tsv")
set(FSIM_VHDL_2019_ACCESS_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_ACCESS_HIR_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/vhdl_hir.hpp")
set(FSIM_VHDL_2019_ACCESS_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_declarations.cpp")
set(FSIM_VHDL_2019_ACCESS_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir_types.cpp")
set(FSIM_VHDL_2019_ACCESS_SPECIALIZATION
  "${FSIM_SOURCE_DIR}/src/elaboration/elaboration_type_generics.cpp")
set(FSIM_VHDL_2019_ACCESS_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_vhdl_access.cpp")
set(FSIM_VHDL_2019_ACCESS_ASSIGNMENT
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_assignment.cpp")
set(FSIM_VHDL_2019_ACCESS_PROCESS
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_process.cpp")
set(FSIM_VHDL_2019_ACCESS_FUNCTIONS
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_functions.cpp")
set(FSIM_VHDL_2019_ACCESS_PROCEDURES
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_procedures.cpp")
set(FSIM_VHDL_2019_ACCESS_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_advanced_type_application_test.cpp")
set(FSIM_VHDL_LEGACY_ACCESS_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_vhdl_composite_test.cpp")
set(FSIM_VHDL_2019_PROTECTED_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_declarations.cpp")
set(FSIM_VHDL_2019_PROTECTED_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design.hpp")
set(FSIM_VHDL_2019_PROTECTED_CORE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_PROTECTED_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir.cpp")
set(FSIM_VHDL_2019_PROTECTED_MERGE
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_vhdl_protected.cpp")
set(FSIM_VHDL_2019_PROTECTED_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_type_tests.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_core.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_UNSPECIFIED_HIR_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/vhdl_hir.hpp")
set(FSIM_VHDL_2019_UNSPECIFIED_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_EXECUTABLE_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_executable_hir.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_RESOLUTION
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_type_resolution.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_PORT_INFERENCE
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_instantiate_processes_and_children.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_overloads.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_GENERIC
  "${FSIM_SOURCE_DIR}/src/elaboration/elaboration_type_generics.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_type_tests.cpp")
set(FSIM_VHDL_2019_UNSPECIFIED_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_type_generic_application_test.cpp")
set(FSIM_VHDL_2019_INTEGER_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_INTEGER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/frontend/common.cpp")
set(FSIM_VHDL_2019_INTEGER_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_internal.hpp")
set(FSIM_VHDL_2019_INTEGER_ELABORATION
  "${FSIM_SOURCE_DIR}/src/elaboration/elaboration_constants.cpp")
set(FSIM_VHDL_2019_INTEGER_SIMIR
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/simir.hpp")
set(FSIM_VHDL_2019_INTEGER_RUNTIME
  "${FSIM_SOURCE_DIR}/src/runtime/simir_arithmetic.cpp")
set(FSIM_VHDL_2019_INTEGER_LLVM
  "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_operations_value.cpp")
set(FSIM_VHDL_2019_INTEGER_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_revision_profile_tests.cpp")
set(FSIM_VHDL_2019_INTEGER_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_2019_integer_application_test.cpp")
set(FSIM_VHDL_2019_MODE_VIEW_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design_core.hpp")
set(FSIM_VHDL_2019_MODE_VIEW_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_declarations.cpp")
set(FSIM_VHDL_2019_MODE_VIEW_HIR_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/vhdl_hir.hpp")
set(FSIM_VHDL_2019_MODE_VIEW_HIR
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_hir.cpp")
set(FSIM_VHDL_2019_MODE_VIEW_COMPOSITION
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_mode_view.cpp")
set(FSIM_VHDL_2019_MODE_VIEW_RESOLUTION
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_type_resolution.cpp")
set(FSIM_VHDL_2019_MODE_VIEW_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_type_tests.cpp")
set(FSIM_VHDL_2019_MODE_VIEW_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_type_generic_application_test.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_core.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_COMPONENT_PARSER
  "${FSIM_SOURCE_DIR}/src/frontend/vhdl_parser_components.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_SEMANTIC
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_mode_view.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_ANALYSIS
  "${FSIM_SOURCE_DIR}/src/app/application_vhdl_analysis.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_ELABORATION
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_types.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_ELABORATION_FRAGMENT
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_connect_ports.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_COMPONENT_BINDING
  "${FSIM_SOURCE_DIR}/src/elaboration/elaboration_vhdl_components.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/elaborator.hpp")
set(FSIM_VHDL_2019_VIEW_PORT_TEST
  "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_vhdl_component_test.cpp")
set(FSIM_VHDL_2019_VIEW_PORT_TEST_CLOSURE
  "${FSIM_SOURCE_DIR}/tests/elaboration/elaborator_vhdl_component_closure_test.cpp")
set(FSIM_VHDL_2019_VIEW_EXECUTION
  "${FSIM_SOURCE_DIR}/tests/app/vhdl_composite_operation_application_test.cpp")
set(FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_assignment.cpp")
set(FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING_SUPPORT
  "${FSIM_SOURCE_DIR}/src/elaboration/lowerer_assignment_support.cpp")
set(FSIM_VHDL_2019_VIEW_EXECUTION_DRIVERS
  "${FSIM_SOURCE_DIR}/src/elaboration/hierarchy_resolution.cpp")
set(FSIM_VHDL_2019_FRONTEND_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/frontend_vhdl_revision_profile_tests.cpp")
set(FSIM_VHDL_2019_PROJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/project/project_config_test.cpp")
set(FSIM_VHDL_2019_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_VHDL_2019_CLI_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_non_project_cli.cpp")
set(FSIM_ACC_USER_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/acc_user.h")
set(FSIM_ACC_USER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_user_abi_test.cpp")
set(FSIM_ACC_USER_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_user_abi_c_test.c")
set(FSIM_ACC_LIFECYCLE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_lifecycle.cpp")
set(FSIM_ACC_LIFECYCLE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_lifecycle_test.cpp")
set(FSIM_ACC_HANDLE_BRIDGE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/acc_handle_bridge.h")
set(FSIM_ACC_HANDLE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_handle.cpp")
set(FSIM_ACC_HANDLE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_handle_test.cpp")
set(FSIM_ACC_INTERNAL
  "${FSIM_SOURCE_DIR}/src/runtime/acc_internal.hpp")
set(FSIM_ACC_LOOKUP_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_lookup.cpp")
set(FSIM_ACC_LOOKUP_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_lookup_test.cpp")
set(FSIM_ACC_TRAVERSAL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_traversal.cpp")
set(FSIM_ACC_TRAVERSAL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_traversal_test.cpp")
set(FSIM_ACC_OBJECT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_object.cpp")
set(FSIM_ACC_OBJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_object_test.cpp")
set(FSIM_ACC_READ_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_read.cpp")
set(FSIM_ACC_READ_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_read_test.cpp")
set(FSIM_ACC_WRITE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_write.cpp")
set(FSIM_ACC_WRITE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_write_test.cpp")
set(FSIM_ACC_ITERATOR_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_iterator.cpp")
set(FSIM_ACC_ITERATOR_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_iterator_test.cpp")
set(FSIM_ACC_TIMING_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_timing.cpp")
set(FSIM_ACC_TIMING_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_timing_test.cpp")
set(FSIM_ACC_VCL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_vcl.cpp")
set(FSIM_ACC_VCL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_vcl_test.cpp")
set(FSIM_ACC_CALLBACK_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_callback.cpp")
set(FSIM_ACC_CALLBACK_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_callback_test.cpp")
set(FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_handle_lifetime.cpp")
set(FSIM_ACC_HANDLE_LIFETIME_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_handle_lifetime_test.cpp")
set(FSIM_ACC_TF_COHERENCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_tf_coherence.cpp")
set(FSIM_ACC_TF_COHERENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_tf_coherence_test.cpp")
set(FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_vpi_coherence.cpp")
set(FSIM_ACC_VPI_COHERENCE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_vpi_coherence_test.cpp")
set(FSIM_NATIVE_PLUGIN_ABI
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/native_plugin_abi.h")
set(FSIM_NATIVE_PLUGIN_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/native_plugin.hpp")
set(FSIM_NATIVE_PLUGIN_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/native_plugin.cpp")
set(FSIM_NATIVE_PLUGIN_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/native_plugin_abi_test.cpp")
set(FSIM_NATIVE_PLUGIN_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/native_plugin_abi_c_test.c")
set(FSIM_VERIUSER_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/veriuser.h")
set(FSIM_VERIUSER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/veriuser_abi_test.cpp")
set(FSIM_VERIUSER_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/veriuser_abi_c_test.c")
set(FSIM_TF_LINK_CMAKE
  "${FSIM_SOURCE_DIR}/cmake/FsimNativePlugin.cmake")
set(FSIM_TF_LINK_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_link.cpp")
set(FSIM_TF_LINK_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_plugin_link_test.cpp")
set(FSIM_TF_LINK_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_link_probe_plugin.c")
set(FSIM_TF_PLUGIN_ABI
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_plugin_abi.h")
set(FSIM_TF_PLUGIN_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_plugin.hpp")
set(FSIM_TF_PLUGIN_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_plugin.cpp")
set(FSIM_TF_PLUGIN_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_plugin_test.cpp")
set(FSIM_TF_REGISTRATION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_registration.hpp")
set(FSIM_TF_REGISTRATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_registration.cpp")
set(FSIM_TF_REGISTRATION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_registration_test.cpp")
set(FSIM_TF_CALL_BRIDGE
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_call_bridge.h")
set(FSIM_TF_CALL_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_call.hpp")
set(FSIM_TF_CALL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_call.cpp")
set(FSIM_TF_CALL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_call_test.cpp")
set(FSIM_TF_MISC_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_misc.hpp")
set(FSIM_TF_MISC_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_misc.cpp")
set(FSIM_TF_MISC_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_misc_test.cpp")
set(FSIM_TF_ARGUMENT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_argument.hpp")
set(FSIM_TF_ARGUMENT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_argument.cpp")
set(FSIM_TF_ARGUMENT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_argument_test.cpp")
set(FSIM_TF_VALUE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_value.hpp")
set(FSIM_TF_VALUE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_value.cpp")
set(FSIM_TF_VALUE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_value_test.cpp")
set(FSIM_TF_INSTANCE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_instance.hpp")
set(FSIM_TF_INSTANCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_instance.cpp")
set(FSIM_TF_INSTANCE_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_instance_test.cpp")
set(FSIM_TF_TIME_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_time.hpp")
set(FSIM_TF_TIME_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_time.cpp")
set(FSIM_TF_TIME_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_time_test.cpp")
set(FSIM_TF_CONTEXT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_context.hpp")
set(FSIM_TF_CONTEXT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_context.cpp")
set(FSIM_TF_CONTEXT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_context_test.cpp")
set(FSIM_TF_CONTROL_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_control.hpp")
set(FSIM_TF_CONTROL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_control.cpp")
set(FSIM_TF_CONTROL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_control_test.cpp")
set(FSIM_TF_SYNCHRONIZATION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_synchronization.hpp")
set(FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_synchronization.cpp")
set(FSIM_TF_SYNCHRONIZATION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_synchronization_test.cpp")
set(FSIM_TF_APPLICATION_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/app/application_tf.hpp")
set(FSIM_TF_APPLICATION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_tf.cpp")
set(FSIM_TF_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/tf_plugin_application_test.cpp")
set(FSIM_TF_SCHEDULER_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/app/application_tf_scheduler.hpp")
set(FSIM_TF_SCHEDULER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_tf_scheduler.cpp")
set(FSIM_TF_SCHEDULER_TEST
  "${FSIM_SOURCE_DIR}/tests/app/tf_scheduler_application_test.cpp")
set(FSIM_ACC_SCHEDULER_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/app/application_acc_scheduler.hpp")
set(FSIM_ACC_SCHEDULER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_acc_scheduler.cpp")
set(FSIM_ACC_SCHEDULER_TEST
  "${FSIM_SOURCE_DIR}/tests/app/acc_scheduler_application_test.cpp")
set(FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/acc_vendor_rejection.cpp")
set(FSIM_ACC_VENDOR_REJECTION_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_vendor_rejection_test.cpp")
set(FSIM_ACC_C_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_link_probe_plugin.c")
set(FSIM_ACC_CPP_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_cpp_probe_plugin.cpp")
set(FSIM_ACC_CROSS_PLATFORM_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/acc_cross_platform_plugins_test.cpp")
set(FSIM_TF_CONTAINMENT_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/tf_containment.hpp")
set(FSIM_TF_CONTAINMENT_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/tf_containment.cpp")
set(FSIM_TF_CONTAINMENT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_containment_test.cpp")
set(FSIM_TF_CPP_PLUGIN
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_cpp_probe_plugin.cpp")
set(FSIM_TF_CROSS_PLATFORM_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/tf_cross_platform_plugins_test.cpp")
set(FSIM_TF_PACKAGE_CONFIG
  "${FSIM_SOURCE_DIR}/cmake/fsimConfig.cmake.in")
set(FSIM_TF_INSTALLED_CONTRACT
  "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake")
set(FSIM_TF_INSTALL_OWNERSHIP
  "${FSIM_SOURCE_DIR}/cmake/CheckBinaryInstallOwnership.cmake")
set(FSIM_TF_INSTALLED_CONSUMER_CMAKE
  "${FSIM_SOURCE_DIR}/tests/runtime/installed_tf_consumer/CMakeLists.txt")
set(FSIM_TF_INSTALLED_CONSUMER
  "${FSIM_SOURCE_DIR}/tests/runtime/installed_tf_consumer/consumer.c")
set(FSIM_TF_SYSTEMC_BOUNDARY
  "${FSIM_SOURCE_DIR}/cmake/FsimSystemCAccellera.cmake")
set(FSIM_COVERAGE_DATABASE_SCHEMA
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_database.hpp")
set(FSIM_COVERAGE_DATABASE_SCHEMA_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_database.cpp")
set(FSIM_COVERAGE_DATABASE_SCHEMA_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_database_schema_test.cpp")
set(FSIM_COVERAGE_DATABASE_CODEC
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_database_codec.hpp")
set(FSIM_COVERAGE_DATABASE_CODEC_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_database_codec.cpp")
set(FSIM_COVERAGE_DATABASE_CODEC_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_database_codec_test.cpp")
set(FSIM_COVERAGE_DATABASE_MERGE
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_database_merge.hpp")
set(FSIM_COVERAGE_DATABASE_MERGE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_database_merge.cpp")
set(FSIM_COVERAGE_DATABASE_MERGE_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_database_merge_test.cpp")
set(FSIM_COVERAGE_DATABASE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_database_model.hpp")
set(FSIM_COVERAGE_DATABASE_MODEL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_database_model.cpp")
set(FSIM_COVERAGE_DATABASE_MODEL_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_database_model_test.cpp")
set(FSIM_COVERAGE_DATABASE_PARTIAL_MERGE
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/coverage_database_partial_merge.hpp")
set(FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/artifact/coverage_database_partial_merge.cpp")
set(FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/coverage_database_partial_merge_test.cpp")
set(FSIM_COVERAGE_DATABASE_SYSTEMVERILOG
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/coverage_database_systemverilog.hpp")
set(FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/frontend/coverage_database_systemverilog.cpp")
set(FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/coverage_database_systemverilog_test.cpp")
set(FSIM_COVERAGE_DATABASE_PSL
  "${FSIM_SOURCE_DIR}/include/fsim/app/coverage_database_psl.hpp")
set(FSIM_COVERAGE_DATABASE_PSL_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/app/application_coverage_database_psl.cpp")
set(FSIM_COVERAGE_DATABASE_PSL_TEST
  "${FSIM_SOURCE_DIR}/tests/app/coverage_database_psl_test.cpp")
set(FSIM_JIT_RUNTIME
  "${FSIM_SOURCE_DIR}/include/fsim/compiler/jit_runtime.h")
set(FSIM_FST_WRITER "${FSIM_SOURCE_DIR}/include/fsim/runtime/fst_writer.hpp")
set(FSIM_FST_WRITER_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/fst_writer.cpp")
set(FSIM_FST_WRITER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/fst_writer_tests.cpp")
set(FSIM_FST_FORMAT
  "${FSIM_SOURCE_DIR}/src/app/application_trace_format.cpp")
set(FSIM_FST_FORMAT_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_format_application_test.cpp")
set(FSIM_FST_ENCODER "${FSIM_SOURCE_DIR}/src/runtime/fst_value_encoder.cpp")
set(FSIM_FST_ENCODER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/fst_value_encoder_tests.cpp")
set(FSIM_FST_CHANGE_ENCODER
  "${FSIM_SOURCE_DIR}/src/runtime/fst_change_encoder.cpp")
set(FSIM_FST_CHANGE_ENCODER_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/fst_change_encoder_tests.cpp")
set(FSIM_FST_HIERARCHY
  "${FSIM_SOURCE_DIR}/src/app/application_trace_hierarchy.cpp")
set(FSIM_FST_HIERARCHY_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_hierarchy_application_test.cpp")
set(FSIM_FST_OBSERVATION
  "${FSIM_SOURCE_DIR}/src/app/application_trace_observation.cpp")
set(FSIM_FST_OBSERVATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_observation_application_test.cpp")
set(FSIM_FST_CONTROL
  "${FSIM_SOURCE_DIR}/src/app/application_trace_control.cpp")
set(FSIM_FST_CONTROL_TEST
  "${FSIM_SOURCE_DIR}/tests/app/trace_control_application_test.cpp")
set(FSIM_FST_APPLICATION "${FSIM_SOURCE_DIR}/src/app/application_run.cpp")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_ROOT}"
    "${FSIM_FOOTPRINT}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_FUZZ_CMAKE}"
    "${FSIM_WORKFLOW}"
    "${FSIM_SCOPED}"
    "${FSIM_SYSTEMC}"
    "${FSIM_COVERAGE}"
    "${FSIM_CODE_COVERAGE_MODEL}"
    "${FSIM_CODE_COVERAGE_MODEL_TEST}"
    "${FSIM_CODE_COVERAGE_SOURCE}"
    "${FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION}"
    "${FSIM_CODE_COVERAGE_SOURCE_TEST}"
    "${FSIM_COVERAGE_SOURCE_CONTROL}"
    "${FSIM_COVERAGE_SOURCE_CONTROL_IMPLEMENTATION}"
    "${FSIM_COVERAGE_SOURCE_CONTROL_TEST}"
    "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS}"
    "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_IMPLEMENTATION}"
    "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_TEST}"
    "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_APPLICATION}"
    "${FSIM_COVERAGE_REPORT_RENDER}"
    "${FSIM_COVERAGE_REPORT_RENDER_IMPLEMENTATION}"
    "${FSIM_COVERAGE_REPORT_RENDER_INTERNAL}"
    "${FSIM_COVERAGE_REPORT_RENDER_HTML}"
    "${FSIM_COVERAGE_REPORT_RENDER_JSON}"
    "${FSIM_COVERAGE_REPORT_RENDER_TEXT}"
    "${FSIM_COVERAGE_REPORT_RENDER_TEST}"
    "${FSIM_COVERAGE_REPORT_PROJECTION}"
    "${FSIM_COVERAGE_REPORT_PROJECTION_IMPLEMENTATION}"
    "${FSIM_COVERAGE_REPORT_PROJECTION_TEST}"
    "${FSIM_COVERAGE_DATABASE_ROBUSTNESS_TEST}"
    "${FSIM_CODE_COVERAGE_POINT}"
    "${FSIM_CODE_COVERAGE_POINT_IMPLEMENTATION}"
    "${FSIM_CODE_COVERAGE_POINT_TEST}"
    "${FSIM_VERILOG_COVERAGE_POINTS}"
    "${FSIM_VERILOG_COVERAGE_POINTS_IMPLEMENTATION}"
    "${FSIM_VERILOG_COVERAGE_POINTS_TEST}"
    "${FSIM_VERILOG_COVERAGE_CONDITIONS}"
    "${FSIM_VERILOG_COVERAGE_CONDITIONS_IMPLEMENTATION}"
    "${FSIM_VERILOG_COVERAGE_CONDITIONS_TEST}"
    "${FSIM_COVERAGE_CONDITIONS}"
    "${FSIM_COVERAGE_CONDITIONS_IMPLEMENTATION}"
    "${FSIM_VHDL_COVERAGE_CONDITIONS}"
    "${FSIM_VHDL_COVERAGE_CONDITIONS_IMPLEMENTATION}"
    "${FSIM_VHDL_COVERAGE_CONDITIONS_TEST}"
    "${FSIM_COVERAGE_CONDITION_EVALUATION}"
    "${FSIM_COVERAGE_CONDITION_EVALUATION_IMPLEMENTATION}"
    "${FSIM_COVERAGE_CONDITION_EVALUATION_TEST}"
    "${FSIM_COVERAGE_CONDITION_OUTCOMES}"
    "${FSIM_COVERAGE_CONDITION_OUTCOMES_IMPLEMENTATION}"
    "${FSIM_COVERAGE_CONDITION_OUTCOMES_TEST}"
    "${FSIM_COVERAGE_EXPRESSION}"
    "${FSIM_COVERAGE_EXPRESSION_IMPLEMENTATION}"
    "${FSIM_COVERAGE_EXPRESSION_TEST}"
    "${FSIM_COVERAGE_TOGGLE}"
    "${FSIM_COVERAGE_TOGGLE_IMPLEMENTATION}"
    "${FSIM_COVERAGE_TOGGLE_TEST}"
    "${FSIM_VHDL_COVERAGE_POINTS}"
    "${FSIM_VHDL_COVERAGE_POINTS_IMPLEMENTATION}"
    "${FSIM_VHDL_COVERAGE_POINTS_TEST}"
    "${FSIM_COVERAGE_BRANCHES}"
    "${FSIM_COVERAGE_BRANCHES_IMPLEMENTATION}"
    "${FSIM_COVERAGE_BRANCHES_TEST}"
    "${FSIM_COVERAGE_LINE_STATE}"
    "${FSIM_COVERAGE_LINE_STATE_IMPLEMENTATION}"
    "${FSIM_COVERAGE_LINE_STATE_TEST}"
    "${FSIM_COVERAGE_INVENTORY}"
    "${FSIM_COVERAGE_INVENTORY_IMPLEMENTATION}"
    "${FSIM_COVERAGE_INVENTORY_TEST}"
    "${FSIM_COVERAGE_POINTS}"
    "${FSIM_COVERAGE_POINTS_IMPLEMENTATION}"
    "${FSIM_COVERAGE_POINTS_TEST}"
    "${FSIM_SIMIR_COVERAGE}"
    "${FSIM_SIMIR_COVERAGE_IMPLEMENTATION}"
    "${FSIM_SIMIR_COVERAGE_TEST}"
    "${FSIM_LLVM_COVERAGE_IMPLEMENTATION}"
    "${FSIM_LLVM_COVERAGE_TEST}"
    "${FSIM_DEBUG_COVERAGE_IMPLEMENTATION}"
    "${FSIM_CODE_COVERAGE_CONTROL_PROJECT}"
    "${FSIM_CODE_COVERAGE_CONTROL_CLI}"
    "${FSIM_CODE_COVERAGE_CONTROL_CLI_IMPLEMENTATION}"
    "${FSIM_CODE_COVERAGE_CONTROL_APPLICATION}"
    "${FSIM_CODE_COVERAGE_CONTROL_IMPLEMENTATION}"
    "${FSIM_CODE_COVERAGE_CONTROL_TEST}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PREPROCESSOR}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PARSER}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LOWERING}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_OPERATIONS}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_RUNTIME}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_INTERPRETER}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_BOUNDARIES}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LLVM}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_APPLICATION}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_APPLICATION}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_TEST}"
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI}"
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE}"
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_IMPLEMENTATION}"
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_OBJECT_IMPLEMENTATION}"
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_TEST}"
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI_TEST}"
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY}"
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_IMPLEMENTATION}"
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_TEST}"
    "${FSIM_CODE_COVERAGE_EQUIVALENCE_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_APPLICATION_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_EQUIVALENCE_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_IDENTITY_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_INVENTORY}"
    "${FSIM_CODE_COVERAGE_METRICS_CHECKER}"
    "${FSIM_LEGACY_TF_INVENTORY}"
    "${FSIM_LEGACY_TF_CHECKER}"
    "${FSIM_LEGACY_ACC_INVENTORY}"
    "${FSIM_LEGACY_ACC_CHECKER}"
    "${FSIM_VHDL_2019_INVENTORY}"
    "${FSIM_VHDL_2019_CHECKER}"
    "${FSIM_SYSTEMVERILOG_2023_INVENTORY}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_PARSER}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_LOWERING}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_TASK_LOWERING}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_RUNTIME}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_BOUNDARIES}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_FRONTEND_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_APPLICATION_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_ADVANCED_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_RESOLUTION}"
    "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_SEMANTIC_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR}"
    "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_FRONTEND_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_PARSER}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_INSTANCE_PARSER}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_PARSER}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_RESOLUTION}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_FRONTEND_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_CHECKER_APPLICATION_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_PARSER}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_RESOLUTION}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_INLINE_LOWERING}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_LOWERING}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_TEMPLATE_RUNTIME}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_OBJECTS}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_LLVM_VALIDATION}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_FRONTEND_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_HIERARCHY}"
    "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_CONTROL}"
    "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_EXPRESSION}"
    "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS}"
    "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS_CORE}"
    "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_STATE}"
    "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_SCHEDULER}"
    "${FSIM_SYSTEMVERILOG_2023_PROCESS_STATE}"
    "${FSIM_SYSTEMVERILOG_2023_PROCESS_RUNTIME}"
    "${FSIM_SYSTEMVERILOG_2023_PROCESS_FORK_RUNTIME}"
    "${FSIM_SYSTEMVERILOG_2023_PROCESS_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CONTEXT}"
    "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_LOWERING}"
    "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CAST}"
    "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_TEST}"
    "${FSIM_SYSTEMVERILOG_2023_STREAM_CONTROL}"
    "${FSIM_SYSTEMVERILOG_2023_STREAM_EXPRESSION}"
    "${FSIM_SYSTEMVERILOG_2023_OPERATOR_TOKENS}"
    "${FSIM_SYSTEMVERILOG_2023_OPERATOR_LEXER}"
    "${FSIM_SYSTEMVERILOG_2023_OPERATOR_PARSER}"
    "${FSIM_SYSTEMVERILOG_2023_OPERATOR_LOWERING}"
    "${FSIM_SYSTEMVERILOG_2023_CALLABLE_MODEL}"
    "${FSIM_SYSTEMVERILOG_2023_CALLABLE_PARSER}"
    "${FSIM_SYSTEMVERILOG_2023_CALLABLE_LOWERING}"
    "${FSIM_SYSTEMVERILOG_2023_CALLABLE_PORTABLE}"
    "${FSIM_SYSTEMVERILOG_2023_CALLABLE_SCHEMA}"
    "${FSIM_SYSTEMVERILOG_2023_CALLABLE_TEST}"
    "${FSIM_VHDL_2019_FRONTEND_IDENTITY}"
    "${FSIM_VHDL_2019_PROJECT_IDENTITY}"
    "${FSIM_VHDL_2019_PROJECT_IMPLEMENTATION}"
    "${FSIM_VHDL_2019_ANALYSIS_IDENTITY}"
    "${FSIM_VHDL_2019_CLI_IDENTITY}"
    "${FSIM_VHDL_2019_PORTABLE_IDENTITY}"
    "${FSIM_VHDL_2019_CONDITIONAL_IMPLEMENTATION}"
    "${FSIM_VHDL_2019_CONDITIONAL_INTERNAL}"
    "${FSIM_VHDL_2019_CONDITIONAL_DRIVER}"
    "${FSIM_VHDL_2019_UNSPECIFIED_PARSER}"
    "${FSIM_VHDL_2019_UNSPECIFIED_MODEL}"
    "${FSIM_VHDL_2019_UNSPECIFIED_HIR_MODEL}"
    "${FSIM_VHDL_2019_UNSPECIFIED_HIR}"
    "${FSIM_VHDL_2019_UNSPECIFIED_EXECUTABLE_HIR}"
    "${FSIM_VHDL_2019_UNSPECIFIED_RESOLUTION}"
    "${FSIM_VHDL_2019_UNSPECIFIED_PORT_INFERENCE}"
    "${FSIM_VHDL_2019_UNSPECIFIED_LOWERING}"
    "${FSIM_VHDL_2019_UNSPECIFIED_GENERIC}"
    "${FSIM_VHDL_2019_UNSPECIFIED_FRONTEND_TEST}"
    "${FSIM_VHDL_2019_UNSPECIFIED_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_INTEGER_MODEL}"
    "${FSIM_VHDL_2019_INTEGER_IMPLEMENTATION}"
    "${FSIM_VHDL_2019_INTEGER_PARSER}"
    "${FSIM_VHDL_2019_INTEGER_ELABORATION}"
    "${FSIM_VHDL_2019_INTEGER_SIMIR}"
    "${FSIM_VHDL_2019_INTEGER_RUNTIME}"
    "${FSIM_VHDL_2019_INTEGER_LLVM}"
    "${FSIM_VHDL_2019_INTEGER_FRONTEND_TEST}"
    "${FSIM_VHDL_2019_INTEGER_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_MODEL}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR_MODEL}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_PARSER}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_EXECUTABLE_HIR}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_FRONTEND_TEST}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_RUNTIME_TEST}"
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL}"
    "${FSIM_VHDL_2019_SIMULATOR_API_PARSER}"
    "${FSIM_VHDL_2019_SIMULATOR_API_REAL_PARSER}"
    "${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS}"
    "${FSIM_VHDL_2019_SIMULATOR_API_HIR}"
    "${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING}"
    "${FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING}"
    "${FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING}"
    "${FSIM_VHDL_2019_SIMULATOR_API_TYPE_INFERENCE}"
    "${FSIM_VHDL_2019_SIMULATOR_API_INTEGER_INFERENCE}"
    "${FSIM_VHDL_2019_SIMULATOR_API_TIME_NORMALIZATION}"
    "${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL}"
    "${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME}"
    "${FSIM_VHDL_2019_SIMULATOR_API_JIT}"
    "${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION}"
    "${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR}"
    "${FSIM_VHDL_2019_SIMULATOR_API_APPLICATION}"
    "${FSIM_VHDL_2019_SIMULATOR_API_TEST}"
    "${FSIM_VHDL_2019_REFLECTION_PARSER}"
    "${FSIM_VHDL_2019_REFLECTION_LOWERING}"
    "${FSIM_VHDL_2019_REFLECTION_RUNTIME}"
    "${FSIM_VHDL_2019_REFLECTION_CACHE_KEY}"
    "${FSIM_VHDL_2019_REFLECTION_TEST}"
    "${FSIM_VHDL_2019_PREDEFINED_PACKAGE_MODEL}"
    "${FSIM_VHDL_2019_PREDEFINED_PACKAGE_IMPLEMENTATION}"
    "${FSIM_VHDL_2019_VHPI_OBJECT_MODEL}"
    "${FSIM_VHDL_2019_VHPI_OBJECT_RUNTIME}"
    "${FSIM_VHDL_2019_VHPI_TYPE_RUNTIME}"
    "${FSIM_VHDL_2019_VHPI_CHECKPOINT_RUNTIME}"
    "${FSIM_VHDL_2019_VHPI_APPLICATION}"
    "${FSIM_VHDL_2019_VHPI_OBJECT_TEST}"
    "${FSIM_VHDL_2019_VHPI_HIERARCHY_TEST}"
    "${FSIM_VHDL_2019_VHPI_CHECKPOINT_TEST}"
    "${FSIM_VHDL_2019_VHPI_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_VHPI_DOCUMENTATION}"
    "${FSIM_VHDL_2019_VHPI_ABI}"
    "${FSIM_VHDL_2019_VHPI_PLUGIN_MODEL}"
    "${FSIM_VHDL_2019_VHPI_PLUGIN_RUNTIME}"
    "${FSIM_VHDL_2019_VHPI_CALLBACK_MODEL}"
    "${FSIM_VHDL_2019_VHPI_CALLBACK_RUNTIME}"
    "${FSIM_VHDL_2019_VHPI_CALLBACK_TEST}"
    "${FSIM_VHDL_2019_VHPI_REFERENCE_TEST}"
    "${FSIM_VHDL_2019_VHPI_C_ABI_TEST}"
    "${FSIM_VHDL_2019_VHPI_C_PLUGIN}"
    "${FSIM_VHDL_2019_VHPI_CPP_PLUGIN}"
    "${FSIM_VHDL_2019_FOREIGN_ABI_CHECKER}"
    "${FSIM_VHDL_2019_PSL_API_RUNTIME}"
    "${FSIM_VHDL_2019_PSL_API_INTERPRETER_MODEL}"
    "${FSIM_VHDL_2019_PSL_API_INTERPRETER_SETUP}"
    "${FSIM_VHDL_2019_PSL_API_INTERPRETER}"
    "${FSIM_VHDL_2019_PSL_API_TEST}"
    "${FSIM_VHDL_2019_TYPED_BOUNDARY_TEST}"
    "${FSIM_VHDL_2019_ARTIFACT_PHASE_TEST}"
    "${FSIM_VHDL_2019_ATTRIBUTE_MODEL}"
    "${FSIM_VHDL_2019_ATTRIBUTE_HIR_MODEL}"
    "${FSIM_VHDL_2019_ATTRIBUTE_TYPE_PARSER}"
    "${FSIM_VHDL_2019_ATTRIBUTE_EXPRESSION_PARSER}"
    "${FSIM_VHDL_2019_ATTRIBUTE_ALIAS_PARSER}"
    "${FSIM_VHDL_2019_ATTRIBUTE_HIR}"
    "${FSIM_VHDL_2019_ATTRIBUTE_HIR_TYPES}"
    "${FSIM_VHDL_2019_ATTRIBUTE_MODE_VIEW}"
    "${FSIM_VHDL_2019_ATTRIBUTE_TYPE_RESOLUTION}"
    "${FSIM_VHDL_2019_ATTRIBUTE_TYPE_INFERENCE}"
    "${FSIM_VHDL_2019_ATTRIBUTE_LOWERING}"
    "${FSIM_VHDL_2019_ATTRIBUTE_CODEC}"
    "${FSIM_VHDL_2019_ATTRIBUTE_FRONTEND_TEST}"
    "${FSIM_VHDL_2019_ATTRIBUTE_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_OVERLOAD_MODEL}"
    "${FSIM_VHDL_2019_OVERLOAD_COMMON}"
    "${FSIM_VHDL_2019_OVERLOAD_FUNCTIONS}"
    "${FSIM_VHDL_2019_OVERLOAD_PROCEDURES}"
    "${FSIM_VHDL_2019_OVERLOAD_RESOLUTION}"
    "${FSIM_VHDL_2019_OVERLOAD_PACKAGES}"
    "${FSIM_VHDL_2019_OVERLOAD_PROTECTED}"
    "${FSIM_VHDL_2019_OVERLOAD_GENERIC_SUBPROGRAMS}"
    "${FSIM_VHDL_2019_OVERLOAD_TYPE_SPECIALIZATION}"
    "${FSIM_VHDL_2019_OVERLOAD_HIR}"
    "${FSIM_VHDL_2019_OVERLOAD_TEST}"
    "${FSIM_VHDL_2019_OVERLOAD_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_PROFILE_MODEL}"
    "${FSIM_VHDL_2019_PROFILE_PARSER}"
    "${FSIM_VHDL_2019_PROFILE_CONDITIONAL}"
    "${FSIM_VHDL_2019_PROFILE_ATTRIBUTES}"
    "${FSIM_VHDL_2019_PROFILE_APPLICATION}"
    "${FSIM_VHDL_2019_PROFILE_APPLICATION_CHECK}"
    "${FSIM_VHDL_2019_PROFILE_HIR}"
    "${FSIM_VHDL_2019_PROFILE_ELABORATION}"
    "${FSIM_VHDL_2019_PROFILE_FRONTEND_TEST}"
    "${FSIM_VHDL_2019_PROFILE_ELABORATION_TEST}"
    "${FSIM_VHDL_2019_PROFILE_LEGACY_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_ARTIFACT_LIBRARY_HEADER}"
    "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_HEADER}"
    "${FSIM_VHDL_2019_ARTIFACT_DESIGN_HEADER}"
    "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_CODEC}"
    "${FSIM_VHDL_2019_ARTIFACT_DESIGN_CODEC}"
    "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_TEST}"
    "${FSIM_VHDL_2019_ARTIFACT_DESIGN_TEST}"
    "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_CONTRACT}"
    "${FSIM_VHDL_2019_ARTIFACT_NESTED_CONTRACT}"
    "${FSIM_VHDL_2019_ARTIFACT_STALE_CONTRACT}"
    "${FSIM_VHDL_2019_ACCESS_MODEL}"
    "${FSIM_VHDL_2019_ACCESS_HIR_MODEL}"
    "${FSIM_VHDL_2019_ACCESS_PARSER}"
    "${FSIM_VHDL_2019_ACCESS_HIR}"
    "${FSIM_VHDL_2019_ACCESS_SPECIALIZATION}"
    "${FSIM_VHDL_2019_ACCESS_LOWERING}"
    "${FSIM_VHDL_2019_ACCESS_TEST}"
    "${FSIM_VHDL_LEGACY_ACCESS_TEST}"
    "${FSIM_VHDL_2019_FRONTEND_TEST}"
    "${FSIM_VHDL_2019_PROJECT_TEST}"
    "${FSIM_VHDL_2019_APPLICATION_TEST}"
    "${FSIM_VHDL_2019_CLI_TEST}"
    "${FSIM_ACC_USER_HEADER}"
    "${FSIM_ACC_USER_TEST}"
    "${FSIM_ACC_USER_C_TEST}"
    "${FSIM_ACC_LIFECYCLE_IMPLEMENTATION}"
    "${FSIM_ACC_LIFECYCLE_TEST}"
    "${FSIM_ACC_HANDLE_BRIDGE}"
    "${FSIM_ACC_HANDLE_IMPLEMENTATION}"
    "${FSIM_ACC_HANDLE_TEST}"
    "${FSIM_ACC_INTERNAL}"
    "${FSIM_ACC_LOOKUP_IMPLEMENTATION}"
    "${FSIM_ACC_LOOKUP_TEST}"
    "${FSIM_ACC_TRAVERSAL_IMPLEMENTATION}"
    "${FSIM_ACC_TRAVERSAL_TEST}"
    "${FSIM_ACC_OBJECT_IMPLEMENTATION}"
    "${FSIM_ACC_OBJECT_TEST}"
    "${FSIM_ACC_READ_IMPLEMENTATION}"
    "${FSIM_ACC_READ_TEST}"
    "${FSIM_ACC_WRITE_IMPLEMENTATION}"
    "${FSIM_ACC_WRITE_TEST}"
    "${FSIM_ACC_ITERATOR_IMPLEMENTATION}"
    "${FSIM_ACC_ITERATOR_TEST}"
    "${FSIM_ACC_TF_COHERENCE_IMPLEMENTATION}"
    "${FSIM_ACC_TF_COHERENCE_TEST}"
    "${FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION}"
    "${FSIM_ACC_VPI_COHERENCE_TEST}"
    "${FSIM_NATIVE_PLUGIN_ABI}"
    "${FSIM_NATIVE_PLUGIN_MODEL}"
    "${FSIM_NATIVE_PLUGIN_IMPLEMENTATION}"
    "${FSIM_NATIVE_PLUGIN_TEST}"
    "${FSIM_NATIVE_PLUGIN_C_TEST}"
    "${FSIM_VERIUSER_HEADER}"
    "${FSIM_VERIUSER_TEST}"
    "${FSIM_VERIUSER_C_TEST}"
    "${FSIM_TF_LINK_CMAKE}"
    "${FSIM_TF_LINK_IMPLEMENTATION}"
    "${FSIM_TF_LINK_TEST}"
    "${FSIM_TF_LINK_PLUGIN}"
    "${FSIM_TF_PLUGIN_ABI}"
    "${FSIM_TF_PLUGIN_MODEL}"
    "${FSIM_TF_PLUGIN_IMPLEMENTATION}"
    "${FSIM_TF_PLUGIN_TEST}"
    "${FSIM_TF_REGISTRATION_MODEL}"
    "${FSIM_TF_REGISTRATION_IMPLEMENTATION}"
    "${FSIM_TF_REGISTRATION_TEST}"
    "${FSIM_TF_CALL_BRIDGE}"
    "${FSIM_TF_CALL_MODEL}"
    "${FSIM_TF_CALL_IMPLEMENTATION}"
    "${FSIM_TF_CALL_TEST}"
    "${FSIM_TF_MISC_MODEL}"
    "${FSIM_TF_MISC_IMPLEMENTATION}"
    "${FSIM_TF_MISC_TEST}"
    "${FSIM_TF_ARGUMENT_MODEL}"
    "${FSIM_TF_ARGUMENT_IMPLEMENTATION}"
    "${FSIM_TF_ARGUMENT_TEST}"
    "${FSIM_TF_VALUE_MODEL}"
    "${FSIM_TF_VALUE_IMPLEMENTATION}"
    "${FSIM_TF_VALUE_TEST}"
    "${FSIM_TF_INSTANCE_MODEL}"
    "${FSIM_TF_INSTANCE_IMPLEMENTATION}"
    "${FSIM_TF_INSTANCE_TEST}"
    "${FSIM_TF_TIME_MODEL}"
    "${FSIM_TF_TIME_IMPLEMENTATION}"
    "${FSIM_TF_TIME_TEST}"
    "${FSIM_TF_CONTEXT_MODEL}"
    "${FSIM_TF_CONTEXT_IMPLEMENTATION}"
    "${FSIM_TF_CONTEXT_TEST}"
    "${FSIM_TF_CONTROL_MODEL}"
    "${FSIM_TF_CONTROL_IMPLEMENTATION}"
    "${FSIM_TF_CONTROL_TEST}"
    "${FSIM_TF_SYNCHRONIZATION_MODEL}"
    "${FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION}"
    "${FSIM_TF_SYNCHRONIZATION_TEST}"
    "${FSIM_TF_APPLICATION_MODEL}"
    "${FSIM_TF_APPLICATION_IMPLEMENTATION}"
    "${FSIM_TF_APPLICATION_TEST}"
    "${FSIM_TF_SCHEDULER_MODEL}"
    "${FSIM_TF_SCHEDULER_IMPLEMENTATION}"
    "${FSIM_TF_SCHEDULER_TEST}"
    "${FSIM_ACC_SCHEDULER_MODEL}"
    "${FSIM_ACC_SCHEDULER_IMPLEMENTATION}"
    "${FSIM_ACC_SCHEDULER_TEST}"
    "${FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION}"
    "${FSIM_ACC_VENDOR_REJECTION_TEST}"
    "${FSIM_ACC_C_PLUGIN}" "${FSIM_ACC_CPP_PLUGIN}"
    "${FSIM_ACC_CROSS_PLATFORM_TEST}"
    "${FSIM_TF_CONTAINMENT_MODEL}"
    "${FSIM_TF_CONTAINMENT_IMPLEMENTATION}"
    "${FSIM_TF_CONTAINMENT_TEST}"
    "${FSIM_TF_CPP_PLUGIN}"
    "${FSIM_TF_CROSS_PLATFORM_TEST}"
    "${FSIM_TF_PACKAGE_CONFIG}"
    "${FSIM_TF_INSTALLED_CONTRACT}"
    "${FSIM_TF_INSTALL_OWNERSHIP}"
    "${FSIM_TF_INSTALLED_CONSUMER_CMAKE}"
    "${FSIM_TF_INSTALLED_CONSUMER}"
    "${FSIM_TF_SYSTEMC_BOUNDARY}"
    "${FSIM_COVERAGE_DATABASE_SCHEMA}"
    "${FSIM_COVERAGE_DATABASE_SCHEMA_IMPLEMENTATION}"
    "${FSIM_COVERAGE_DATABASE_SCHEMA_TEST}"
    "${FSIM_COVERAGE_DATABASE_CODEC}"
    "${FSIM_COVERAGE_DATABASE_CODEC_IMPLEMENTATION}"
    "${FSIM_COVERAGE_DATABASE_CODEC_TEST}"
    "${FSIM_COVERAGE_DATABASE_MODEL}"
    "${FSIM_COVERAGE_DATABASE_MODEL_IMPLEMENTATION}"
    "${FSIM_COVERAGE_DATABASE_MODEL_TEST}"
    "${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG}"
    "${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_IMPLEMENTATION}"
    "${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_TEST}"
    "${FSIM_COVERAGE_DATABASE_PSL}"
    "${FSIM_COVERAGE_DATABASE_PSL_IMPLEMENTATION}"
    "${FSIM_COVERAGE_DATABASE_PSL_TEST}"
    "${FSIM_JIT_RUNTIME}"
    "${FSIM_FST_WRITER}"
    "${FSIM_FST_WRITER_IMPLEMENTATION}"
    "${FSIM_FST_WRITER_TEST}"
    "${FSIM_FST_FORMAT}"
    "${FSIM_FST_FORMAT_TEST}"
    "${FSIM_FST_ENCODER}"
    "${FSIM_FST_ENCODER_TEST}"
    "${FSIM_FST_CHANGE_ENCODER}"
    "${FSIM_FST_CHANGE_ENCODER_TEST}"
    "${FSIM_FST_HIERARCHY}"
    "${FSIM_FST_HIERARCHY_TEST}"
    "${FSIM_FST_OBSERVATION}"
    "${FSIM_FST_OBSERVATION_TEST}"
    "${FSIM_FST_CONTROL}"
    "${FSIM_FST_CONTROL_TEST}"
    "${FSIM_FST_APPLICATION}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "resource contract input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_ROOT}" FSIM_ROOT_CONTENTS)
file(READ "${FSIM_FOOTPRINT}" FSIM_FOOTPRINT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_RUNTIME_TEST_CMAKE}" FSIM_RUNTIME_TEST_CMAKE_CONTENTS)
file(READ "${FSIM_FUZZ_CMAKE}" FSIM_FUZZ_CMAKE_CONTENTS)
file(READ "${FSIM_WORKFLOW}" FSIM_WORKFLOW_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_CONTROL_PROJECT}"
  FSIM_CODE_COVERAGE_CONTROL_PROJECT_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_CONTROL_CLI}"
  FSIM_CODE_COVERAGE_CONTROL_CLI_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_CONTROL_CLI_IMPLEMENTATION}"
  FSIM_CODE_COVERAGE_CONTROL_CLI_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_CONTROL_APPLICATION}"
  FSIM_CODE_COVERAGE_CONTROL_APPLICATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_CONTROL_IMPLEMENTATION}"
  FSIM_CODE_COVERAGE_CONTROL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_CONTROL_TEST}"
  FSIM_CODE_COVERAGE_CONTROL_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PREPROCESSOR}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PREPROCESSOR_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PARSER}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LOWERING}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_OPERATIONS}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_OPERATIONS_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_RUNTIME}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_RUNTIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_INTERPRETER}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_INTERPRETER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_BOUNDARIES}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_BOUNDARIES_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LLVM}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LLVM_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_APPLICATION}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_APPLICATION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_APPLICATION}"
  FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_APPLICATION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_TEST}"
  FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI}"
  FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_VPI_COVERAGE}"
  FSIM_SYSTEMVERILOG_VPI_COVERAGE_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_IMPLEMENTATION}"
  FSIM_SYSTEMVERILOG_VPI_COVERAGE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_OBJECT_IMPLEMENTATION}"
  FSIM_SYSTEMVERILOG_VPI_COVERAGE_OBJECT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_TEST}"
  FSIM_SYSTEMVERILOG_VPI_COVERAGE_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI_TEST}"
  FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY}"
  FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_IMPLEMENTATION}"
  FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_TEST}"
  FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_EQUIVALENCE_TEST}"
  FSIM_CODE_COVERAGE_EQUIVALENCE_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_METRICS_APPLICATION_TEST}"
  FSIM_CODE_COVERAGE_METRICS_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_METRICS_EQUIVALENCE_TEST}"
  FSIM_CODE_COVERAGE_METRICS_EQUIVALENCE_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_METRICS_IDENTITY_TEST}"
  FSIM_CODE_COVERAGE_METRICS_IDENTITY_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_METRICS_INVENTORY}"
  FSIM_CODE_COVERAGE_METRICS_INVENTORY_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_METRICS_CHECKER}"
  FSIM_CODE_COVERAGE_METRICS_CHECKER_CONTENTS)
file(READ "${FSIM_LEGACY_TF_INVENTORY}"
  FSIM_LEGACY_TF_INVENTORY_CONTENTS)
file(READ "${FSIM_LEGACY_TF_CHECKER}"
  FSIM_LEGACY_TF_CHECKER_CONTENTS)
file(READ "${FSIM_LEGACY_ACC_INVENTORY}"
  FSIM_LEGACY_ACC_INVENTORY_CONTENTS)
file(READ "${FSIM_LEGACY_ACC_CHECKER}"
  FSIM_LEGACY_ACC_CHECKER_CONTENTS)
file(READ "${FSIM_VHDL_2019_INVENTORY}"
  FSIM_VHDL_2019_INVENTORY_CONTENTS)
file(READ "${FSIM_VHDL_2019_CHECKER}"
  FSIM_VHDL_2019_CHECKER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_INVENTORY}"
  FSIM_SYSTEMVERILOG_2023_INVENTORY_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_PARSER}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_TASK_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_TASK_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_RUNTIME}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_RUNTIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_BOUNDARIES}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_BOUNDARIES_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_EXECUTION_SHARED}"
  FSIM_SYSTEMVERILOG_2023_EXECUTION_SHARED_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_FRONTEND_TEST}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_APPLICATION_TEST}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_ADVANCED_TEST}"
  FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_ADVANCED_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_MODEL}"
  FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_RESOLUTION}"
  FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_RESOLUTION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_SEMANTIC_MODEL}"
  FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_SEMANTIC_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR}"
  FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_FRONTEND_TEST}"
  FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR_TEST}"
  FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_MODEL}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_PARSER}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_INSTANCE_PARSER}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_INSTANCE_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_MODEL}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_PARSER}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_RESOLUTION}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_RESOLUTION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_FRONTEND_TEST}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CHECKER_APPLICATION_TEST}"
  FSIM_SYSTEMVERILOG_2023_CHECKER_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_PARSER}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_MODEL}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_RESOLUTION}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_RESOLUTION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_INLINE_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_INLINE_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_MODEL}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_TEMPLATE_RUNTIME}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_TEMPLATE_RUNTIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_OBJECTS}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_OBJECTS_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_LLVM_VALIDATION}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_LLVM_VALIDATION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_FRONTEND_TEST}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME_TEST}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION_TEST}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_TEST}"
  FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_HIERARCHY}"
  FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_HIERARCHY_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_CONTROL}"
  FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_CONTROL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_EXPRESSION}"
  FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_EXPRESSION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS}"
  FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS_CORE}"
  FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS_CORE_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_STATE}"
  FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_STATE_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_SCHEDULER}"
  FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_SCHEDULER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_PROCESS_STATE}"
  FSIM_SYSTEMVERILOG_2023_PROCESS_STATE_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_PROCESS_RUNTIME}"
  FSIM_SYSTEMVERILOG_2023_PROCESS_RUNTIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_PROCESS_FORK_RUNTIME}"
  FSIM_SYSTEMVERILOG_2023_PROCESS_FORK_RUNTIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_PROCESS_TEST}"
  FSIM_SYSTEMVERILOG_2023_PROCESS_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_MODEL}"
  FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CONTEXT}"
  FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CONTEXT_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CAST}"
  FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CAST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_TEST}"
  FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_STREAM_CONTROL}"
  FSIM_SYSTEMVERILOG_2023_STREAM_CONTROL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_STREAM_EXPRESSION}"
  FSIM_SYSTEMVERILOG_2023_STREAM_EXPRESSION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_OPERATOR_TOKENS}"
  FSIM_SYSTEMVERILOG_2023_OPERATOR_TOKENS_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_OPERATOR_LEXER}"
  FSIM_SYSTEMVERILOG_2023_OPERATOR_LEXER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_OPERATOR_PARSER}"
  FSIM_SYSTEMVERILOG_2023_OPERATOR_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_OPERATOR_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_OPERATOR_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CALLABLE_MODEL}"
  FSIM_SYSTEMVERILOG_2023_CALLABLE_MODEL_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CALLABLE_PARSER}"
  FSIM_SYSTEMVERILOG_2023_CALLABLE_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CALLABLE_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_CALLABLE_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CALLABLE_PORTABLE}"
  FSIM_SYSTEMVERILOG_2023_CALLABLE_PORTABLE_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CALLABLE_SCHEMA}"
  FSIM_SYSTEMVERILOG_2023_CALLABLE_SCHEMA_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CALLABLE_TEST}"
  FSIM_SYSTEMVERILOG_2023_CALLABLE_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CLOCKING_TIME}"
  FSIM_SYSTEMVERILOG_2023_CLOCKING_TIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CLOCKING_CONSTANTS}"
  FSIM_SYSTEMVERILOG_2023_CLOCKING_CONSTANTS_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CLOCKING_ELABORATION}"
  FSIM_SYSTEMVERILOG_2023_CLOCKING_ELABORATION_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CLOCKING_LOWERING}"
  FSIM_SYSTEMVERILOG_2023_CLOCKING_LOWERING_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_RUNTIME}"
  FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_RUNTIME_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_TEST}"
  FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_CLOCKING_TEST}"
  FSIM_SYSTEMVERILOG_2023_CLOCKING_TEST_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_PROFILE_CLASS_PARSER}"
  FSIM_SYSTEMVERILOG_2023_PROFILE_CLASS_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_PROFILE_PROCESS_PARSER}"
  FSIM_SYSTEMVERILOG_2023_PROFILE_PROCESS_PARSER_CONTENTS)
file(READ "${FSIM_SYSTEMVERILOG_2023_PROFILE_TEST}"
  FSIM_SYSTEMVERILOG_2023_PROFILE_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_FRONTEND_IDENTITY}"
  FSIM_VHDL_2019_FRONTEND_IDENTITY_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROJECT_IDENTITY}"
  FSIM_VHDL_2019_PROJECT_IDENTITY_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROJECT_IMPLEMENTATION}"
  FSIM_VHDL_2019_PROJECT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_ANALYSIS_IDENTITY}"
  FSIM_VHDL_2019_ANALYSIS_IDENTITY_CONTENTS)
file(READ "${FSIM_VHDL_2019_CLI_IDENTITY}"
  FSIM_VHDL_2019_CLI_IDENTITY_CONTENTS)
file(READ "${FSIM_VHDL_2019_PORTABLE_IDENTITY}"
  FSIM_VHDL_2019_PORTABLE_IDENTITY_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_IMPLEMENTATION}"
  FSIM_VHDL_2019_CONDITIONAL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_INTERNAL}"
  FSIM_VHDL_2019_CONDITIONAL_INTERNAL_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_DRIVER}"
  FSIM_VHDL_2019_CONDITIONAL_DRIVER_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_MODEL}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_PARSER}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_MODEL}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_CONTENTS)
file(READ "${FSIM_VHDL_HIR_BUILDERS}"
  FSIM_VHDL_HIR_BUILDERS_CONTENTS)
file(READ "${FSIM_VHDL_HIR_CALLABLE_BUILDERS}"
  FSIM_VHDL_HIR_CALLABLE_BUILDERS_CONTENTS)
file(READ "${FSIM_VHDL_HIR_TYPE_BUILDERS}"
  FSIM_VHDL_HIR_TYPE_BUILDERS_CONTENTS)
file(READ "${FSIM_VHDL_HIR_INTERNAL}"
  FSIM_VHDL_HIR_INTERNAL_CONTENTS)
file(READ "${FSIM_VHDL_HIR_STATEMENT_BUILDERS}"
  FSIM_VHDL_HIR_STATEMENT_BUILDERS_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_EXECUTABLE_HIR}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_EXECUTABLE_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_LOWERING}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_TYPES}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_TYPES_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST_PART2}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST_PART2_CONTENTS)
string(APPEND FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST_CONTENTS
  "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST_PART2_CONTENTS}")
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_ELABORATION_TEST}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_ELABORATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_APPLICATION_TEST}"
  FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_RESULT_SUBTYPE_PARSER}"
  FSIM_VHDL_2019_RESULT_SUBTYPE_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING}"
  FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING_PART2}"
  FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING_PART2_CONTENTS)
string(APPEND FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING_CONTENTS
  "${FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING_PART2_CONTENTS}")
file(READ "${FSIM_VHDL_2019_RESULT_SUBTYPE_STORAGE}"
  FSIM_VHDL_2019_RESULT_SUBTYPE_STORAGE_CONTENTS)
file(READ "${FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES}"
  FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES_CONTENTS)
file(READ "${FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES_PART2}"
  FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES_PART2_CONTENTS)
string(APPEND FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES_CONTENTS
  "${FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES_PART2_CONTENTS}")
file(READ "${FSIM_VHDL_2019_RESULT_SUBTYPE_ELABORATION_TEST}"
  FSIM_VHDL_2019_RESULT_SUBTYPE_ELABORATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_MODEL}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR_MODEL}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_PARSER}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_EXECUTABLE_HIR}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_EXECUTABLE_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING_PART2}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING_PART2_CONTENTS)
string(APPEND FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING_CONTENTS
  "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING_PART2_CONTENTS}")
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_FRONTEND_TEST}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_APPLICATION_TEST}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_RUNTIME_TEST}"
  FSIM_VHDL_2019_SEQUENTIAL_BLOCK_RUNTIME_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_MODEL}"
  FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_PARSER}"
  FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_REAL_PARSER}"
  FSIM_VHDL_2019_SIMULATOR_API_REAL_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS}"
  FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_HIR}"
  FSIM_VHDL_2019_SIMULATOR_API_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING}"
  FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING}"
  FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL}"
  FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME}"
  FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_JIT}"
  FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION}"
  FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR}"
  FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING}"
  FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_TYPE_INFERENCE}"
  FSIM_VHDL_2019_SIMULATOR_API_TYPE_INFERENCE_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_INTEGER_INFERENCE}"
  FSIM_VHDL_2019_SIMULATOR_API_INTEGER_INFERENCE_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_TIME_NORMALIZATION}"
  FSIM_VHDL_2019_SIMULATOR_API_TIME_NORMALIZATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_APPLICATION}"
  FSIM_VHDL_2019_SIMULATOR_API_APPLICATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_SIMULATOR_API_TEST}"
  FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_ASSERT_API_RUNTIME}"
  FSIM_VHDL_2019_ASSERT_API_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_ASSERT_API_TEXTIO}"
  FSIM_VHDL_2019_ASSERT_API_TEXTIO_CONTENTS)
file(READ "${FSIM_VHDL_2019_ASSERT_API_EXECUTOR}"
  FSIM_VHDL_2019_ASSERT_API_EXECUTOR_CONTENTS)
file(READ "${FSIM_VHDL_2019_ASSERT_API_VALIDATION}"
  FSIM_VHDL_2019_ASSERT_API_VALIDATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_REFLECTION_PARSER}"
  FSIM_VHDL_2019_REFLECTION_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_REFLECTION_LOWERING}"
  FSIM_VHDL_2019_REFLECTION_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_REFLECTION_RUNTIME}"
  FSIM_VHDL_2019_REFLECTION_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_REFLECTION_CACHE_KEY}"
  FSIM_VHDL_2019_REFLECTION_CACHE_KEY_CONTENTS)
file(READ "${FSIM_VHDL_2019_REFLECTION_TEST}"
  FSIM_VHDL_2019_REFLECTION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_PREDEFINED_PACKAGE_MODEL}"
  FSIM_VHDL_2019_PREDEFINED_PACKAGE_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_PREDEFINED_PACKAGE_IMPLEMENTATION}"
  FSIM_VHDL_2019_PREDEFINED_PACKAGE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_OBJECT_MODEL}"
  FSIM_VHDL_2019_VHPI_OBJECT_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_OBJECT_RUNTIME}"
  FSIM_VHDL_2019_VHPI_OBJECT_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_TYPE_RUNTIME}"
  FSIM_VHDL_2019_VHPI_TYPE_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_CHECKPOINT_RUNTIME}"
  FSIM_VHDL_2019_VHPI_CHECKPOINT_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_APPLICATION}"
  FSIM_VHDL_2019_VHPI_APPLICATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_OBJECT_TEST}"
  FSIM_VHDL_2019_VHPI_OBJECT_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_HIERARCHY_TEST}"
  FSIM_VHDL_2019_VHPI_HIERARCHY_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_CHECKPOINT_TEST}"
  FSIM_VHDL_2019_VHPI_CHECKPOINT_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_APPLICATION_TEST}"
  FSIM_VHDL_2019_VHPI_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_DOCUMENTATION}"
  FSIM_VHDL_2019_VHPI_DOCUMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_ABI}"
  FSIM_VHDL_2019_VHPI_ABI_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_PLUGIN_MODEL}"
  FSIM_VHDL_2019_VHPI_PLUGIN_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_PLUGIN_RUNTIME}"
  FSIM_VHDL_2019_VHPI_PLUGIN_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_CALLBACK_MODEL}"
  FSIM_VHDL_2019_VHPI_CALLBACK_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_CALLBACK_RUNTIME}"
  FSIM_VHDL_2019_VHPI_CALLBACK_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_CALLBACK_TEST}"
  FSIM_VHDL_2019_VHPI_CALLBACK_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_REFERENCE_TEST}"
  FSIM_VHDL_2019_VHPI_REFERENCE_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_C_ABI_TEST}"
  FSIM_VHDL_2019_VHPI_C_ABI_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_C_PLUGIN}"
  FSIM_VHDL_2019_VHPI_C_PLUGIN_CONTENTS)
file(READ "${FSIM_VHDL_2019_VHPI_CPP_PLUGIN}"
  FSIM_VHDL_2019_VHPI_CPP_PLUGIN_CONTENTS)
file(READ "${FSIM_VHDL_2019_FOREIGN_ABI_CHECKER}"
  FSIM_VHDL_2019_FOREIGN_ABI_CHECKER_CONTENTS)
file(READ "${FSIM_VHDL_2019_PSL_API_RUNTIME}"
  FSIM_VHDL_2019_PSL_API_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_PSL_API_INTERPRETER_MODEL}"
  FSIM_VHDL_2019_PSL_API_INTERPRETER_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_PSL_API_INTERPRETER_SETUP}"
  FSIM_VHDL_2019_PSL_API_INTERPRETER_SETUP_CONTENTS)
file(READ "${FSIM_VHDL_2019_PSL_API_INTERPRETER}"
  FSIM_VHDL_2019_PSL_API_INTERPRETER_CONTENTS)
file(READ "${FSIM_VHDL_2019_PSL_API_TEST}"
  FSIM_VHDL_2019_PSL_API_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_COVERAGE_POINTS}"
  FSIM_VHDL_2019_COVERAGE_POINTS_CONTENTS)
file(READ "${FSIM_VHDL_2019_COVERAGE_POINTS_TEST}"
  FSIM_VHDL_2019_COVERAGE_POINTS_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_PSL_COVERAGE_DATABASE}"
  FSIM_VHDL_2019_PSL_COVERAGE_DATABASE_CONTENTS)
file(READ "${FSIM_VHDL_2019_PSL_COVERAGE_DATABASE_TEST}"
  FSIM_VHDL_2019_PSL_COVERAGE_DATABASE_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_TYPED_BOUNDARY_TEST}"
  FSIM_VHDL_2019_TYPED_BOUNDARY_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_PHASE_TEST}"
  FSIM_VHDL_2019_ARTIFACT_PHASE_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_MODEL}"
  FSIM_VHDL_2019_ATTRIBUTE_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_HIR_MODEL}"
  FSIM_VHDL_2019_ATTRIBUTE_HIR_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_TYPE_PARSER}"
  FSIM_VHDL_2019_ATTRIBUTE_TYPE_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_EXPRESSION_PARSER}"
  FSIM_VHDL_2019_ATTRIBUTE_EXPRESSION_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_ALIAS_PARSER}"
  FSIM_VHDL_2019_ATTRIBUTE_ALIAS_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_HIR}"
  FSIM_VHDL_2019_ATTRIBUTE_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_HIR_TYPES}"
  FSIM_VHDL_2019_ATTRIBUTE_HIR_TYPES_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_MODE_VIEW}"
  FSIM_VHDL_2019_ATTRIBUTE_MODE_VIEW_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_TYPE_RESOLUTION}"
  FSIM_VHDL_2019_ATTRIBUTE_TYPE_RESOLUTION_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_TYPE_INFERENCE}"
  FSIM_VHDL_2019_ATTRIBUTE_TYPE_INFERENCE_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_LOWERING}"
  FSIM_VHDL_2019_ATTRIBUTE_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_CODEC}"
  FSIM_VHDL_2019_ATTRIBUTE_CODEC_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_FRONTEND_TEST}"
  FSIM_VHDL_2019_ATTRIBUTE_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_ATTRIBUTE_APPLICATION_TEST}"
  FSIM_VHDL_2019_ATTRIBUTE_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_MODEL}"
  FSIM_VHDL_2019_OVERLOAD_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_COMMON}"
  FSIM_VHDL_2019_OVERLOAD_COMMON_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_FUNCTIONS}"
  FSIM_VHDL_2019_OVERLOAD_FUNCTIONS_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_PROCEDURES}"
  FSIM_VHDL_2019_OVERLOAD_PROCEDURES_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_RESOLUTION}"
  FSIM_VHDL_2019_OVERLOAD_RESOLUTION_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_PACKAGES}"
  FSIM_VHDL_2019_OVERLOAD_PACKAGES_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_PROTECTED}"
  FSIM_VHDL_2019_OVERLOAD_PROTECTED_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_GENERIC_SUBPROGRAMS}"
  FSIM_VHDL_2019_OVERLOAD_GENERIC_SUBPROGRAMS_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_TYPE_SPECIALIZATION}"
  FSIM_VHDL_2019_OVERLOAD_TYPE_SPECIALIZATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_HIR}"
  FSIM_VHDL_2019_OVERLOAD_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_TEST}"
  FSIM_VHDL_2019_OVERLOAD_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_OVERLOAD_APPLICATION_TEST}"
  FSIM_VHDL_2019_OVERLOAD_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_MODEL}"
  FSIM_VHDL_2019_PROFILE_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_PARSER}"
  FSIM_VHDL_2019_PROFILE_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_CONDITIONAL}"
  FSIM_VHDL_2019_PROFILE_CONDITIONAL_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_ATTRIBUTES}"
  FSIM_VHDL_2019_PROFILE_ATTRIBUTES_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_APPLICATION}"
  FSIM_VHDL_2019_PROFILE_APPLICATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_APPLICATION_CHECK}"
  FSIM_VHDL_2019_PROFILE_APPLICATION_CHECK_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_HIR}"
  FSIM_VHDL_2019_PROFILE_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_ELABORATION}"
  FSIM_VHDL_2019_PROFILE_ELABORATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_FRONTEND_TEST}"
  FSIM_VHDL_2019_PROFILE_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_ELABORATION_TEST}"
  FSIM_VHDL_2019_PROFILE_ELABORATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROFILE_LEGACY_APPLICATION_TEST}"
  FSIM_VHDL_2019_PROFILE_LEGACY_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_LIBRARY_HEADER}"
  FSIM_VHDL_2019_ARTIFACT_LIBRARY_HEADER_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_HEADER}"
  FSIM_VHDL_2019_ARTIFACT_PORTABLE_HEADER_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_DESIGN_HEADER}"
  FSIM_VHDL_2019_ARTIFACT_DESIGN_HEADER_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_CODEC}"
  FSIM_VHDL_2019_ARTIFACT_PORTABLE_CODEC_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_DESIGN_CODEC}"
  FSIM_VHDL_2019_ARTIFACT_DESIGN_CODEC_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_TEST}"
  FSIM_VHDL_2019_ARTIFACT_PORTABLE_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_DESIGN_TEST}"
  FSIM_VHDL_2019_ARTIFACT_DESIGN_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_PORTABLE_CONTRACT}"
  FSIM_VHDL_2019_ARTIFACT_PORTABLE_CONTRACT_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_NESTED_CONTRACT}"
  FSIM_VHDL_2019_ARTIFACT_NESTED_CONTRACT_CONTENTS)
file(READ "${FSIM_VHDL_2019_ARTIFACT_STALE_CONTRACT}"
  FSIM_VHDL_2019_ARTIFACT_STALE_CONTRACT_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_MODEL}"
  FSIM_VHDL_2019_ACCESS_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_HIR_MODEL}"
  FSIM_VHDL_2019_ACCESS_HIR_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_PARSER}"
  FSIM_VHDL_2019_ACCESS_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_HIR}"
  FSIM_VHDL_2019_ACCESS_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_SPECIALIZATION}"
  FSIM_VHDL_2019_ACCESS_SPECIALIZATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_LOWERING}"
  FSIM_VHDL_2019_ACCESS_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_ASSIGNMENT}"
  FSIM_VHDL_2019_ACCESS_ASSIGNMENT_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_PROCESS}"
  FSIM_VHDL_2019_ACCESS_PROCESS_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_FUNCTIONS}"
  FSIM_VHDL_2019_ACCESS_FUNCTIONS_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_PROCEDURES}"
  FSIM_VHDL_2019_ACCESS_PROCEDURES_CONTENTS)
file(READ "${FSIM_VHDL_2019_ACCESS_TEST}"
  FSIM_VHDL_2019_ACCESS_TEST_CONTENTS)
file(READ "${FSIM_VHDL_LEGACY_ACCESS_TEST}"
  FSIM_VHDL_LEGACY_ACCESS_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROTECTED_PARSER}"
  FSIM_VHDL_2019_PROTECTED_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROTECTED_MODEL}"
  FSIM_VHDL_2019_PROTECTED_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROTECTED_CORE_MODEL}"
  FSIM_VHDL_2019_PROTECTED_CORE_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROTECTED_HIR}"
  FSIM_VHDL_2019_PROTECTED_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROTECTED_MERGE}"
  FSIM_VHDL_2019_PROTECTED_MERGE_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROTECTED_TEST}"
  FSIM_VHDL_2019_PROTECTED_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_PARSER}"
  FSIM_VHDL_2019_UNSPECIFIED_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_MODEL}"
  FSIM_VHDL_2019_UNSPECIFIED_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_HIR_MODEL}"
  FSIM_VHDL_2019_UNSPECIFIED_HIR_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_HIR}"
  FSIM_VHDL_2019_UNSPECIFIED_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_EXECUTABLE_HIR}"
  FSIM_VHDL_2019_UNSPECIFIED_EXECUTABLE_HIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_RESOLUTION}"
  FSIM_VHDL_2019_UNSPECIFIED_RESOLUTION_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_PORT_INFERENCE}"
  FSIM_VHDL_2019_UNSPECIFIED_PORT_INFERENCE_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_LOWERING}"
  FSIM_VHDL_2019_UNSPECIFIED_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_GENERIC}"
  FSIM_VHDL_2019_UNSPECIFIED_GENERIC_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_FRONTEND_TEST}"
  FSIM_VHDL_2019_UNSPECIFIED_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_UNSPECIFIED_APPLICATION_TEST}"
  FSIM_VHDL_2019_UNSPECIFIED_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_MODEL}"
  FSIM_VHDL_2019_INTEGER_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_IMPLEMENTATION}"
  FSIM_VHDL_2019_INTEGER_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_PARSER}"
  FSIM_VHDL_2019_INTEGER_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_ELABORATION}"
  FSIM_VHDL_2019_INTEGER_ELABORATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_SIMIR}"
  FSIM_VHDL_2019_INTEGER_SIMIR_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_RUNTIME}"
  FSIM_VHDL_2019_INTEGER_RUNTIME_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_LLVM}"
  FSIM_VHDL_2019_INTEGER_LLVM_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_FRONTEND_TEST}"
  FSIM_VHDL_2019_INTEGER_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_INTEGER_APPLICATION_TEST}"
  FSIM_VHDL_2019_INTEGER_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_MODE_VIEW_MODEL}"
  FSIM_VHDL_2019_MODE_VIEW_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_MODE_VIEW_PARSER}"
  FSIM_VHDL_2019_MODE_VIEW_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_MODE_VIEW_HIR_MODEL}"
  FSIM_VHDL_2019_MODE_VIEW_HIR_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_MODE_VIEW_HIR}"
  FSIM_VHDL_2019_MODE_VIEW_HIR_CONTENTS)
string(APPEND FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_CONTENTS
  "${FSIM_VHDL_HIR_CALLABLE_BUILDERS_CONTENTS}${FSIM_VHDL_HIR_TYPE_BUILDERS_CONTENTS}")
string(APPEND FSIM_VHDL_2019_PROTECTED_HIR_CONTENTS
  "${FSIM_VHDL_HIR_BUILDERS_CONTENTS}${FSIM_VHDL_HIR_CALLABLE_BUILDERS_CONTENTS}${FSIM_VHDL_HIR_TYPE_BUILDERS_CONTENTS}")
string(APPEND FSIM_VHDL_2019_UNSPECIFIED_HIR_CONTENTS
  "${FSIM_VHDL_HIR_CALLABLE_BUILDERS_CONTENTS}${FSIM_VHDL_HIR_TYPE_BUILDERS_CONTENTS}")
string(APPEND FSIM_VHDL_2019_MODE_VIEW_HIR_CONTENTS
  "${FSIM_VHDL_HIR_CALLABLE_BUILDERS_CONTENTS}${FSIM_VHDL_HIR_TYPE_BUILDERS_CONTENTS}${FSIM_VHDL_HIR_INTERNAL_CONTENTS}")
file(READ "${FSIM_VHDL_2019_MODE_VIEW_COMPOSITION}"
  FSIM_VHDL_2019_MODE_VIEW_COMPOSITION_CONTENTS)
file(READ "${FSIM_VHDL_2019_MODE_VIEW_RESOLUTION}"
  FSIM_VHDL_2019_MODE_VIEW_RESOLUTION_CONTENTS)
file(READ "${FSIM_VHDL_2019_MODE_VIEW_FRONTEND_TEST}"
  FSIM_VHDL_2019_MODE_VIEW_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_MODE_VIEW_APPLICATION_TEST}"
  FSIM_VHDL_2019_MODE_VIEW_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_PARSER}"
  FSIM_VHDL_2019_VIEW_PORT_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_COMPONENT_PARSER}"
  FSIM_VHDL_2019_VIEW_PORT_COMPONENT_PARSER_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_SEMANTIC}"
  FSIM_VHDL_2019_VIEW_PORT_SEMANTIC_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_ANALYSIS}"
  FSIM_VHDL_2019_VIEW_PORT_ANALYSIS_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_ELABORATION}"
  FSIM_VHDL_2019_VIEW_PORT_ELABORATION_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_ELABORATION_FRAGMENT}"
  FSIM_VHDL_2019_VIEW_PORT_ELABORATION_FRAGMENT_CONTENTS)
string(APPEND FSIM_VHDL_2019_VIEW_PORT_ELABORATION_CONTENTS
  "${FSIM_VHDL_2019_VIEW_PORT_ELABORATION_FRAGMENT_CONTENTS}")
file(READ "${FSIM_VHDL_2019_VIEW_PORT_COMPONENT_BINDING}"
  FSIM_VHDL_2019_VIEW_PORT_COMPONENT_BINDING_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_MODEL}"
  FSIM_VHDL_2019_VIEW_PORT_MODEL_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_TEST}"
  FSIM_VHDL_2019_VIEW_PORT_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_PORT_TEST_CLOSURE}"
  FSIM_VHDL_2019_VIEW_PORT_TEST_CLOSURE_CONTENTS)
string(APPEND FSIM_VHDL_2019_VIEW_PORT_TEST_CONTENTS
  "${FSIM_VHDL_2019_VIEW_PORT_TEST_CLOSURE_CONTENTS}")
file(READ "${FSIM_VHDL_2019_VIEW_EXECUTION}"
  FSIM_VHDL_2019_VIEW_EXECUTION_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING}"
  FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING_CONTENTS)
file(READ "${FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING_SUPPORT}"
  FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING_SUPPORT_CONTENTS)
string(APPEND FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING_CONTENTS
  "${FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING_SUPPORT_CONTENTS}")
file(READ "${FSIM_VHDL_2019_VIEW_EXECUTION_DRIVERS}"
  FSIM_VHDL_2019_VIEW_EXECUTION_DRIVERS_CONTENTS)
file(READ "${FSIM_VHDL_2019_FRONTEND_TEST}"
  FSIM_VHDL_2019_FRONTEND_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_PROJECT_TEST}"
  FSIM_VHDL_2019_PROJECT_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_APPLICATION_TEST}"
  FSIM_VHDL_2019_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_VHDL_2019_CLI_TEST}"
  FSIM_VHDL_2019_CLI_TEST_CONTENTS)
file(READ "${FSIM_ACC_USER_HEADER}" FSIM_ACC_USER_HEADER_CONTENTS)
file(READ "${FSIM_ACC_USER_TEST}" FSIM_ACC_USER_TEST_CONTENTS)
file(READ "${FSIM_ACC_USER_C_TEST}" FSIM_ACC_USER_C_TEST_CONTENTS)
file(READ "${FSIM_ACC_LIFECYCLE_IMPLEMENTATION}"
  FSIM_ACC_LIFECYCLE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_LIFECYCLE_TEST}"
  FSIM_ACC_LIFECYCLE_TEST_CONTENTS)
file(READ "${FSIM_ACC_HANDLE_BRIDGE}" FSIM_ACC_HANDLE_BRIDGE_CONTENTS)
file(READ "${FSIM_ACC_HANDLE_IMPLEMENTATION}"
  FSIM_ACC_HANDLE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_HANDLE_TEST}" FSIM_ACC_HANDLE_TEST_CONTENTS)
file(READ "${FSIM_ACC_INTERNAL}" FSIM_ACC_INTERNAL_CONTENTS)
file(READ "${FSIM_ACC_LOOKUP_IMPLEMENTATION}"
  FSIM_ACC_LOOKUP_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_LOOKUP_TEST}" FSIM_ACC_LOOKUP_TEST_CONTENTS)
file(READ "${FSIM_ACC_TRAVERSAL_IMPLEMENTATION}"
  FSIM_ACC_TRAVERSAL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_TRAVERSAL_TEST}" FSIM_ACC_TRAVERSAL_TEST_CONTENTS)
file(READ "${FSIM_ACC_OBJECT_IMPLEMENTATION}"
  FSIM_ACC_OBJECT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_OBJECT_TEST}" FSIM_ACC_OBJECT_TEST_CONTENTS)
file(READ "${FSIM_ACC_READ_IMPLEMENTATION}"
  FSIM_ACC_READ_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_READ_TEST}" FSIM_ACC_READ_TEST_CONTENTS)
file(READ "${FSIM_ACC_WRITE_IMPLEMENTATION}"
  FSIM_ACC_WRITE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_WRITE_TEST}" FSIM_ACC_WRITE_TEST_CONTENTS)
file(READ "${FSIM_ACC_ITERATOR_IMPLEMENTATION}"
  FSIM_ACC_ITERATOR_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_ITERATOR_TEST}" FSIM_ACC_ITERATOR_TEST_CONTENTS)
file(READ "${FSIM_ACC_TIMING_IMPLEMENTATION}"
  FSIM_ACC_TIMING_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_TIMING_TEST}" FSIM_ACC_TIMING_TEST_CONTENTS)
file(READ "${FSIM_ACC_VCL_IMPLEMENTATION}"
  FSIM_ACC_VCL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_VCL_TEST}" FSIM_ACC_VCL_TEST_CONTENTS)
file(READ "${FSIM_ACC_CALLBACK_IMPLEMENTATION}"
  FSIM_ACC_CALLBACK_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_CALLBACK_TEST}" FSIM_ACC_CALLBACK_TEST_CONTENTS)
file(READ "${FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION}"
  FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_HANDLE_LIFETIME_TEST}"
  FSIM_ACC_HANDLE_LIFETIME_TEST_CONTENTS)
file(READ "${FSIM_ACC_TF_COHERENCE_IMPLEMENTATION}"
  FSIM_ACC_TF_COHERENCE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_TF_COHERENCE_TEST}"
  FSIM_ACC_TF_COHERENCE_TEST_CONTENTS)
file(READ "${FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION}"
  FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_VPI_COHERENCE_TEST}"
  FSIM_ACC_VPI_COHERENCE_TEST_CONTENTS)
file(READ "${FSIM_NATIVE_PLUGIN_ABI}" FSIM_NATIVE_PLUGIN_ABI_CONTENTS)
file(READ "${FSIM_NATIVE_PLUGIN_MODEL}" FSIM_NATIVE_PLUGIN_MODEL_CONTENTS)
file(READ "${FSIM_NATIVE_PLUGIN_IMPLEMENTATION}"
  FSIM_NATIVE_PLUGIN_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_NATIVE_PLUGIN_TEST}" FSIM_NATIVE_PLUGIN_TEST_CONTENTS)
file(READ "${FSIM_NATIVE_PLUGIN_C_TEST}" FSIM_NATIVE_PLUGIN_C_TEST_CONTENTS)
file(READ "${FSIM_VERIUSER_HEADER}" FSIM_VERIUSER_HEADER_CONTENTS)
file(READ "${FSIM_VERIUSER_TEST}" FSIM_VERIUSER_TEST_CONTENTS)
file(READ "${FSIM_VERIUSER_C_TEST}" FSIM_VERIUSER_C_TEST_CONTENTS)
file(READ "${FSIM_TF_LINK_CMAKE}" FSIM_TF_LINK_CMAKE_CONTENTS)
file(READ "${FSIM_TF_LINK_IMPLEMENTATION}"
  FSIM_TF_LINK_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_LINK_TEST}" FSIM_TF_LINK_TEST_CONTENTS)
file(READ "${FSIM_TF_LINK_PLUGIN}" FSIM_TF_LINK_PLUGIN_CONTENTS)
file(READ "${FSIM_TF_PLUGIN_ABI}" FSIM_TF_PLUGIN_ABI_CONTENTS)
file(READ "${FSIM_TF_PLUGIN_MODEL}" FSIM_TF_PLUGIN_MODEL_CONTENTS)
file(READ "${FSIM_TF_PLUGIN_IMPLEMENTATION}"
  FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_PLUGIN_TEST}" FSIM_TF_PLUGIN_TEST_CONTENTS)
file(READ "${FSIM_TF_REGISTRATION_MODEL}"
  FSIM_TF_REGISTRATION_MODEL_CONTENTS)
file(READ "${FSIM_TF_REGISTRATION_IMPLEMENTATION}"
  FSIM_TF_REGISTRATION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_REGISTRATION_TEST}"
  FSIM_TF_REGISTRATION_TEST_CONTENTS)
file(READ "${FSIM_TF_CALL_BRIDGE}" FSIM_TF_CALL_BRIDGE_CONTENTS)
file(READ "${FSIM_TF_CALL_MODEL}" FSIM_TF_CALL_MODEL_CONTENTS)
file(READ "${FSIM_TF_CALL_IMPLEMENTATION}"
  FSIM_TF_CALL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_CALL_TEST}" FSIM_TF_CALL_TEST_CONTENTS)
file(READ "${FSIM_TF_MISC_MODEL}" FSIM_TF_MISC_MODEL_CONTENTS)
file(READ "${FSIM_TF_MISC_IMPLEMENTATION}"
  FSIM_TF_MISC_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_MISC_TEST}" FSIM_TF_MISC_TEST_CONTENTS)
file(READ "${FSIM_TF_ARGUMENT_MODEL}" FSIM_TF_ARGUMENT_MODEL_CONTENTS)
file(READ "${FSIM_TF_ARGUMENT_IMPLEMENTATION}"
  FSIM_TF_ARGUMENT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_ARGUMENT_TEST}" FSIM_TF_ARGUMENT_TEST_CONTENTS)
file(READ "${FSIM_TF_VALUE_MODEL}" FSIM_TF_VALUE_MODEL_CONTENTS)
file(READ "${FSIM_TF_VALUE_IMPLEMENTATION}"
  FSIM_TF_VALUE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_VALUE_TEST}" FSIM_TF_VALUE_TEST_CONTENTS)
file(READ "${FSIM_TF_INSTANCE_MODEL}" FSIM_TF_INSTANCE_MODEL_CONTENTS)
file(READ "${FSIM_TF_INSTANCE_IMPLEMENTATION}"
  FSIM_TF_INSTANCE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_INSTANCE_TEST}" FSIM_TF_INSTANCE_TEST_CONTENTS)
file(READ "${FSIM_TF_TIME_MODEL}" FSIM_TF_TIME_MODEL_CONTENTS)
file(READ "${FSIM_TF_TIME_IMPLEMENTATION}"
  FSIM_TF_TIME_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_TIME_TEST}" FSIM_TF_TIME_TEST_CONTENTS)
file(READ "${FSIM_TF_CONTEXT_MODEL}" FSIM_TF_CONTEXT_MODEL_CONTENTS)
file(READ "${FSIM_TF_CONTEXT_IMPLEMENTATION}"
  FSIM_TF_CONTEXT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_CONTEXT_TEST}" FSIM_TF_CONTEXT_TEST_CONTENTS)
file(READ "${FSIM_TF_CONTROL_MODEL}" FSIM_TF_CONTROL_MODEL_CONTENTS)
file(READ "${FSIM_TF_CONTROL_IMPLEMENTATION}"
  FSIM_TF_CONTROL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_CONTROL_TEST}" FSIM_TF_CONTROL_TEST_CONTENTS)
file(READ "${FSIM_TF_SYNCHRONIZATION_MODEL}"
  FSIM_TF_SYNCHRONIZATION_MODEL_CONTENTS)
file(READ "${FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION}"
  FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_SYNCHRONIZATION_TEST}"
  FSIM_TF_SYNCHRONIZATION_TEST_CONTENTS)
file(READ "${FSIM_TF_APPLICATION_MODEL}" FSIM_TF_APPLICATION_MODEL_CONTENTS)
file(READ "${FSIM_TF_APPLICATION_IMPLEMENTATION}"
  FSIM_TF_APPLICATION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_APPLICATION_TEST}" FSIM_TF_APPLICATION_TEST_CONTENTS)
file(READ "${FSIM_TF_SCHEDULER_MODEL}" FSIM_TF_SCHEDULER_MODEL_CONTENTS)
file(READ "${FSIM_TF_SCHEDULER_IMPLEMENTATION}"
  FSIM_TF_SCHEDULER_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_SCHEDULER_TEST}" FSIM_TF_SCHEDULER_TEST_CONTENTS)
file(READ "${FSIM_ACC_SCHEDULER_MODEL}"
  FSIM_ACC_SCHEDULER_MODEL_CONTENTS)
file(READ "${FSIM_ACC_SCHEDULER_IMPLEMENTATION}"
  FSIM_ACC_SCHEDULER_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_SCHEDULER_TEST}"
  FSIM_ACC_SCHEDULER_TEST_CONTENTS)
file(READ "${FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION}"
  FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_ACC_VENDOR_REJECTION_TEST}"
  FSIM_ACC_VENDOR_REJECTION_TEST_CONTENTS)
file(READ "${FSIM_ACC_C_PLUGIN}" FSIM_ACC_C_PLUGIN_CONTENTS)
file(READ "${FSIM_ACC_CPP_PLUGIN}" FSIM_ACC_CPP_PLUGIN_CONTENTS)
file(READ "${FSIM_ACC_CROSS_PLATFORM_TEST}"
  FSIM_ACC_CROSS_PLATFORM_TEST_CONTENTS)
file(READ "${FSIM_TF_CONTAINMENT_MODEL}"
  FSIM_TF_CONTAINMENT_MODEL_CONTENTS)
file(READ "${FSIM_TF_CONTAINMENT_IMPLEMENTATION}"
  FSIM_TF_CONTAINMENT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_TF_CONTAINMENT_TEST}"
  FSIM_TF_CONTAINMENT_TEST_CONTENTS)
file(READ "${FSIM_TF_CPP_PLUGIN}" FSIM_TF_CPP_PLUGIN_CONTENTS)
file(READ "${FSIM_TF_CROSS_PLATFORM_TEST}"
  FSIM_TF_CROSS_PLATFORM_TEST_CONTENTS)
file(READ "${FSIM_TF_PACKAGE_CONFIG}" FSIM_TF_PACKAGE_CONFIG_CONTENTS)
file(READ "${FSIM_TF_INSTALLED_CONTRACT}"
  FSIM_TF_INSTALLED_CONTRACT_CONTENTS)
file(READ "${FSIM_TF_INSTALL_OWNERSHIP}"
  FSIM_TF_INSTALL_OWNERSHIP_CONTENTS)
file(READ "${FSIM_TF_INSTALLED_CONSUMER_CMAKE}"
  FSIM_TF_INSTALLED_CONSUMER_CMAKE_CONTENTS)
file(READ "${FSIM_TF_INSTALLED_CONSUMER}"
  FSIM_TF_INSTALLED_CONSUMER_CONTENTS)
file(READ "${FSIM_TF_SYSTEMC_BOUNDARY}" FSIM_TF_SYSTEMC_BOUNDARY_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_SCHEMA}"
  FSIM_COVERAGE_DATABASE_SCHEMA_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_SCHEMA_IMPLEMENTATION}"
  FSIM_COVERAGE_DATABASE_SCHEMA_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_SCHEMA_TEST}"
  FSIM_COVERAGE_DATABASE_SCHEMA_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_CODEC}"
  FSIM_COVERAGE_DATABASE_CODEC_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_CODEC_IMPLEMENTATION}"
  FSIM_COVERAGE_DATABASE_CODEC_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_CODEC_TEST}"
  FSIM_COVERAGE_DATABASE_CODEC_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_MERGE}"
  FSIM_COVERAGE_DATABASE_MERGE_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_MERGE_IMPLEMENTATION}"
  FSIM_COVERAGE_DATABASE_MERGE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_MERGE_TEST}"
  FSIM_COVERAGE_DATABASE_MERGE_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_MODEL}"
  FSIM_COVERAGE_DATABASE_MODEL_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_MODEL_IMPLEMENTATION}"
  FSIM_COVERAGE_DATABASE_MODEL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_MODEL_TEST}"
  FSIM_COVERAGE_DATABASE_MODEL_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE}"
  FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_IMPLEMENTATION}"
  FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_TEST}"
  FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG}"
  FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_IMPLEMENTATION}"
  FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_TEST}"
  FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_PSL}"
  FSIM_COVERAGE_DATABASE_PSL_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_PSL_IMPLEMENTATION}"
  FSIM_COVERAGE_DATABASE_PSL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_PSL_TEST}"
  FSIM_COVERAGE_DATABASE_PSL_TEST_CONTENTS)

foreach(FSIM_RUNNER_TEMP_POLICY IN ITEMS
    "llvm_installer=\"\${RUNNER_TEMP}/llvm-22-installer.sh\""
    "--output-document=\"\${llvm_installer}\""
    "sudo \"\${llvm_installer}\" 22")
  string(FIND "${FSIM_WORKFLOW_CONTENTS}" "${FSIM_RUNNER_TEMP_POLICY}"
    FSIM_RUNNER_TEMP_INDEX)
  if(FSIM_RUNNER_TEMP_INDEX EQUAL -1)
    message(FATAL_ERROR
      "LLVM provisioning lost runner-temp source-tree isolation: ${FSIM_RUNNER_TEMP_POLICY}")
  endif()
endforeach()
file(READ "${FSIM_SCOPED}" FSIM_SCOPED_CONTENTS)
file(READ "${FSIM_SYSTEMC}" FSIM_SYSTEMC_CONTENTS)
file(READ "${FSIM_COVERAGE}" FSIM_COVERAGE_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_MODEL}"
  FSIM_CODE_COVERAGE_MODEL_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_MODEL_IMPLEMENTATION}"
  FSIM_CODE_COVERAGE_MODEL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_MODEL_TEST}"
  FSIM_CODE_COVERAGE_MODEL_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_SOURCE}"
  FSIM_CODE_COVERAGE_SOURCE_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION}"
  FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_SOURCE_TEST}"
  FSIM_CODE_COVERAGE_SOURCE_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_SOURCE_CONTROL}"
  FSIM_COVERAGE_SOURCE_CONTROL_CONTENTS)
file(READ "${FSIM_COVERAGE_SOURCE_CONTROL_IMPLEMENTATION}"
  FSIM_COVERAGE_SOURCE_CONTROL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_SOURCE_CONTROL_TEST}"
  FSIM_COVERAGE_SOURCE_CONTROL_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS}"
  FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_CONTENTS)
file(READ "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_IMPLEMENTATION}"
  FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_TEST}"
  FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_APPLICATION}"
  FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_APPLICATION_CONTENTS)
file(READ "${FSIM_COVERAGE_EXCLUSION_PERSISTENCE}"
  FSIM_COVERAGE_EXCLUSION_PERSISTENCE_CONTENTS)
file(READ "${FSIM_COVERAGE_EXCLUSION_PERSISTENCE_IMPLEMENTATION}"
  FSIM_COVERAGE_EXCLUSION_PERSISTENCE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_EXCLUSION_PERSISTENCE_TEST}"
  FSIM_COVERAGE_EXCLUSION_PERSISTENCE_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_EXCLUSION_REPORT}"
  FSIM_COVERAGE_EXCLUSION_REPORT_CONTENTS)
file(READ "${FSIM_COVERAGE_EXCLUSION_REPORT_IMPLEMENTATION}"
  FSIM_COVERAGE_EXCLUSION_REPORT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_EXCLUSION_REPORT_TEST}"
  FSIM_COVERAGE_EXCLUSION_REPORT_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_MODEL}"
  FSIM_COVERAGE_REPORT_MODEL_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_MODEL_IMPLEMENTATION}"
  FSIM_COVERAGE_REPORT_MODEL_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_MODEL_TEST}"
  FSIM_COVERAGE_REPORT_MODEL_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_RENDER}"
  FSIM_COVERAGE_REPORT_RENDER_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_RENDER_IMPLEMENTATION}"
  FSIM_COVERAGE_REPORT_RENDER_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_RENDER_INTERNAL}"
  FSIM_COVERAGE_REPORT_RENDER_INTERNAL_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_RENDER_HTML}"
  FSIM_COVERAGE_REPORT_RENDER_HTML_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_RENDER_JSON}"
  FSIM_COVERAGE_REPORT_RENDER_JSON_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_RENDER_TEXT}"
  FSIM_COVERAGE_REPORT_RENDER_TEXT_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_RENDER_TEST}"
  FSIM_COVERAGE_REPORT_RENDER_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_PROJECTION}"
  FSIM_COVERAGE_REPORT_PROJECTION_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_PROJECTION_IMPLEMENTATION}"
  FSIM_COVERAGE_REPORT_PROJECTION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_REPORT_PROJECTION_TEST}"
  FSIM_COVERAGE_REPORT_PROJECTION_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_COMMAND}"
  FSIM_COVERAGE_COMMAND_CONTENTS)
file(READ "${FSIM_COVERAGE_COMMAND_CLI}"
  FSIM_COVERAGE_COMMAND_CLI_CONTENTS)
file(READ "${FSIM_COVERAGE_COMMAND_CLI_IMPLEMENTATION}"
  FSIM_COVERAGE_COMMAND_CLI_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_COMMAND_TEST}"
  FSIM_COVERAGE_COMMAND_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_DATABASE_ROBUSTNESS_TEST}"
  FSIM_COVERAGE_DATABASE_ROBUSTNESS_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_POINT}"
  FSIM_CODE_COVERAGE_POINT_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_POINT_IMPLEMENTATION}"
  FSIM_CODE_COVERAGE_POINT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_POINT_TEST}"
  FSIM_CODE_COVERAGE_POINT_TEST_CONTENTS)
file(READ "${FSIM_VERILOG_COVERAGE_POINTS}"
  FSIM_VERILOG_COVERAGE_POINTS_CONTENTS)
file(READ "${FSIM_VERILOG_COVERAGE_POINTS_IMPLEMENTATION}"
  FSIM_VERILOG_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VERILOG_COVERAGE_POINTS_TEST}"
  FSIM_VERILOG_COVERAGE_POINTS_TEST_CONTENTS)
file(READ "${FSIM_VERILOG_COVERAGE_CONDITIONS}"
  FSIM_VERILOG_COVERAGE_CONDITIONS_CONTENTS)
file(READ "${FSIM_VERILOG_COVERAGE_CONDITIONS_IMPLEMENTATION}"
  FSIM_VERILOG_COVERAGE_CONDITIONS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VERILOG_COVERAGE_CONDITIONS_TEST}"
  FSIM_VERILOG_COVERAGE_CONDITIONS_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITIONS}"
  FSIM_COVERAGE_CONDITIONS_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITIONS_IMPLEMENTATION}"
  FSIM_COVERAGE_CONDITIONS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_COVERAGE_CONDITIONS}"
  FSIM_VHDL_COVERAGE_CONDITIONS_CONTENTS)
file(READ "${FSIM_VHDL_COVERAGE_CONDITIONS_IMPLEMENTATION}"
  FSIM_VHDL_COVERAGE_CONDITIONS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_COVERAGE_CONDITIONS_TEST}"
  FSIM_VHDL_COVERAGE_CONDITIONS_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITION_EVALUATION}"
  FSIM_COVERAGE_CONDITION_EVALUATION_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITION_EVALUATION_IMPLEMENTATION}"
  FSIM_COVERAGE_CONDITION_EVALUATION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITION_EVALUATION_TEST}"
  FSIM_COVERAGE_CONDITION_EVALUATION_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITION_OUTCOMES}"
  FSIM_COVERAGE_CONDITION_OUTCOMES_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITION_OUTCOMES_IMPLEMENTATION}"
  FSIM_COVERAGE_CONDITION_OUTCOMES_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_CONDITION_OUTCOMES_TEST}"
  FSIM_COVERAGE_CONDITION_OUTCOMES_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_EXPRESSION}"
  FSIM_COVERAGE_EXPRESSION_CONTENTS)
file(READ "${FSIM_COVERAGE_EXPRESSION_IMPLEMENTATION}"
  FSIM_COVERAGE_EXPRESSION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_EXPRESSION_TEST}"
  FSIM_COVERAGE_EXPRESSION_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_TOGGLE}"
  FSIM_COVERAGE_TOGGLE_CONTENTS)
file(READ "${FSIM_COVERAGE_TOGGLE_IMPLEMENTATION}"
  FSIM_COVERAGE_TOGGLE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_TOGGLE_TEST}"
  FSIM_COVERAGE_TOGGLE_TEST_CONTENTS)
file(READ "${FSIM_VERILOG_TOGGLE_INVENTORY}"
  FSIM_VERILOG_TOGGLE_INVENTORY_CONTENTS)
file(READ "${FSIM_VERILOG_TOGGLE_INVENTORY_IMPLEMENTATION}"
  FSIM_VERILOG_TOGGLE_INVENTORY_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VERILOG_TOGGLE_INVENTORY_TEST}"
  FSIM_VERILOG_TOGGLE_INVENTORY_TEST_CONTENTS)
file(READ "${FSIM_VHDL_TOGGLE_INVENTORY}"
  FSIM_VHDL_TOGGLE_INVENTORY_CONTENTS)
file(READ "${FSIM_VHDL_TOGGLE_INVENTORY_IMPLEMENTATION}"
  FSIM_VHDL_TOGGLE_INVENTORY_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_TOGGLE_INVENTORY_TEST}"
  FSIM_VHDL_TOGGLE_INVENTORY_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_TOGGLE_SELECTION}"
  FSIM_COVERAGE_TOGGLE_SELECTION_CONTENTS)
file(READ "${FSIM_COVERAGE_TOGGLE_SELECTION_IMPLEMENTATION}"
  FSIM_COVERAGE_TOGGLE_SELECTION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_TOGGLE_SELECTION_TEST}"
  FSIM_COVERAGE_TOGGLE_SELECTION_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_MEMORY_TOGGLE}"
  FSIM_COVERAGE_MEMORY_TOGGLE_CONTENTS)
file(READ "${FSIM_COVERAGE_MEMORY_TOGGLE_IMPLEMENTATION}"
  FSIM_COVERAGE_MEMORY_TOGGLE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_MEMORY_TOGGLE_TEST}"
  FSIM_COVERAGE_MEMORY_TOGGLE_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_INFERENCE}"
  FSIM_COVERAGE_FSM_INFERENCE_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_INFERENCE_IMPLEMENTATION}"
  FSIM_COVERAGE_FSM_INFERENCE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_INFERENCE_TEST}"
  FSIM_COVERAGE_FSM_INFERENCE_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_HINTS}"
  FSIM_COVERAGE_FSM_HINTS_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_HINTS_IMPLEMENTATION}"
  FSIM_COVERAGE_FSM_HINTS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_HINTS_TEST}"
  FSIM_COVERAGE_FSM_HINTS_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_HINTS_PROJECT_IMPLEMENTATION}"
  FSIM_COVERAGE_FSM_HINTS_PROJECT_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_RUNTIME}"
  FSIM_COVERAGE_FSM_RUNTIME_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_RUNTIME_IMPLEMENTATION}"
  FSIM_COVERAGE_FSM_RUNTIME_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_RUNTIME_TEST}"
  FSIM_COVERAGE_FSM_RUNTIME_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_VALIDATION}"
  FSIM_COVERAGE_FSM_VALIDATION_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_VALIDATION_IMPLEMENTATION}"
  FSIM_COVERAGE_FSM_VALIDATION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_FSM_VALIDATION_TEST}"
  FSIM_COVERAGE_FSM_VALIDATION_TEST_CONTENTS)
file(READ "${FSIM_FRONTEND_DESIGN}"
  FSIM_FRONTEND_DESIGN_CONTENTS)
file(READ "${FSIM_VERILOG_PARSER_CORE}"
  FSIM_VERILOG_PARSER_CORE_CONTENTS)
file(READ "${FSIM_VHDL_COVERAGE_POINTS}"
  FSIM_VHDL_COVERAGE_POINTS_CONTENTS)
file(READ "${FSIM_VHDL_COVERAGE_POINTS_IMPLEMENTATION}"
  FSIM_VHDL_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_VHDL_COVERAGE_POINTS_TEST}"
  FSIM_VHDL_COVERAGE_POINTS_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_BRANCHES}"
  FSIM_COVERAGE_BRANCHES_CONTENTS)
file(READ "${FSIM_COVERAGE_BRANCHES_IMPLEMENTATION}"
  FSIM_COVERAGE_BRANCHES_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_BRANCHES_TEST}"
  FSIM_COVERAGE_BRANCHES_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_LINE_STATE}"
  FSIM_COVERAGE_LINE_STATE_CONTENTS)
file(READ "${FSIM_COVERAGE_LINE_STATE_IMPLEMENTATION}"
  FSIM_COVERAGE_LINE_STATE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_LINE_STATE_TEST}"
  FSIM_COVERAGE_LINE_STATE_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_AGGREGATION}"
  FSIM_COVERAGE_AGGREGATION_CONTENTS)
file(READ "${FSIM_COVERAGE_AGGREGATION_IMPLEMENTATION}"
  FSIM_COVERAGE_AGGREGATION_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_AGGREGATION_TEST}"
  FSIM_COVERAGE_AGGREGATION_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_INVENTORY}"
  FSIM_COVERAGE_INVENTORY_CONTENTS)
file(READ "${FSIM_COVERAGE_INVENTORY_IMPLEMENTATION}"
  FSIM_COVERAGE_INVENTORY_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_INVENTORY_TEST}"
  FSIM_COVERAGE_INVENTORY_TEST_CONTENTS)
file(READ "${FSIM_COVERAGE_POINTS}"
  FSIM_COVERAGE_POINTS_CONTENTS)
file(READ "${FSIM_COVERAGE_POINTS_IMPLEMENTATION}"
  FSIM_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_POINTS_TEST}"
  FSIM_COVERAGE_POINTS_TEST_CONTENTS)
file(READ "${FSIM_SIMIR_COVERAGE}"
  FSIM_SIMIR_COVERAGE_CONTENTS)
file(READ "${FSIM_SIMIR_COVERAGE_IMPLEMENTATION}"
  FSIM_SIMIR_COVERAGE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_SIMIR_COVERAGE_TEST}"
  FSIM_SIMIR_COVERAGE_TEST_CONTENTS)
file(READ "${FSIM_LLVM_COVERAGE_IMPLEMENTATION}"
  FSIM_LLVM_COVERAGE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_LLVM_COVERAGE_TEST}"
  FSIM_LLVM_COVERAGE_TEST_CONTENTS)
file(READ "${FSIM_DEBUG_COVERAGE_IMPLEMENTATION}"
  FSIM_DEBUG_COVERAGE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_JIT_RUNTIME}" FSIM_JIT_RUNTIME_CONTENTS)
file(READ "${FSIM_FST_WRITER}" FSIM_FST_WRITER_CONTENTS)
file(READ "${FSIM_FST_WRITER_IMPLEMENTATION}"
  FSIM_FST_WRITER_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_FST_WRITER_TEST}" FSIM_FST_WRITER_TEST_CONTENTS)
file(READ "${FSIM_FST_FORMAT}" FSIM_FST_FORMAT_CONTENTS)
file(READ "${FSIM_FST_FORMAT_TEST}" FSIM_FST_FORMAT_TEST_CONTENTS)
file(READ "${FSIM_FST_ENCODER}" FSIM_FST_ENCODER_CONTENTS)
file(READ "${FSIM_FST_ENCODER_TEST}" FSIM_FST_ENCODER_TEST_CONTENTS)
file(READ "${FSIM_FST_CHANGE_ENCODER}" FSIM_FST_CHANGE_ENCODER_CONTENTS)
file(READ "${FSIM_FST_CHANGE_ENCODER_TEST}"
  FSIM_FST_CHANGE_ENCODER_TEST_CONTENTS)
file(READ "${FSIM_FST_HIERARCHY}" FSIM_FST_HIERARCHY_CONTENTS)
file(READ "${FSIM_FST_HIERARCHY_TEST}" FSIM_FST_HIERARCHY_TEST_CONTENTS)
file(READ "${FSIM_FST_OBSERVATION}" FSIM_FST_OBSERVATION_CONTENTS)
file(READ "${FSIM_FST_OBSERVATION_TEST}"
  FSIM_FST_OBSERVATION_TEST_CONTENTS)
file(READ "${FSIM_FST_CONTROL}" FSIM_FST_CONTROL_CONTENTS)
file(READ "${FSIM_FST_CONTROL_TEST}" FSIM_FST_CONTROL_TEST_CONTENTS)
file(READ "${FSIM_FST_APPLICATION}" FSIM_FST_APPLICATION_CONTENTS)

string(REGEX MATCHALL "--parallel 4" FSIM_PARALLEL_STEPS "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_PARALLEL_STEPS FSIM_PARALLEL_COUNT)
if(NOT FSIM_PARALLEL_COUNT EQUAL 5)
  message(FATAL_ERROR
    "expected five four-worker hosted build/test steps, found ${FSIM_PARALLEL_COUNT}")
endif()
string(REGEX MATCH "--parallel ([^4]|4[^[:space:]\r\n])" FSIM_OTHER_PARALLEL
  "${FSIM_WORKFLOW_CONTENTS}")
if(FSIM_OTHER_PARALLEL)
  message(FATAL_ERROR "workflow contains a non-four-worker build/test step")
endif()

string(REGEX MATCHALL
  "timeout-minutes:[ ]*120([ \t\r\n]|$)"
  FSIM_120_MINUTE_TIMEOUTS
  "${FSIM_WORKFLOW_CONTENTS}")
string(REGEX MATCHALL
  "timeout-minutes:"
  FSIM_HOSTED_TIMEOUTS
  "${FSIM_WORKFLOW_CONTENTS}")
list(LENGTH FSIM_120_MINUTE_TIMEOUTS FSIM_120_MINUTE_TIMEOUT_COUNT)
list(LENGTH FSIM_HOSTED_TIMEOUTS FSIM_HOSTED_TIMEOUT_COUNT)
if(NOT FSIM_HOSTED_TIMEOUT_COUNT EQUAL 4
    OR NOT FSIM_120_MINUTE_TIMEOUT_COUNT EQUAL FSIM_HOSTED_TIMEOUT_COUNT)
  message(FATAL_ERROR
    "expected all four hosted job timeouts to be 120 minutes")
endif()

foreach(FSIM_FOOTPRINT_POLICY IN ITEMS
    "FSIM_LINK_POOL_SIZE"
    "\"8\""
    "fsim_link_pool=\${FSIM_LINK_POOL_SIZE}"
    "set(CMAKE_JOB_POOL_LINK fsim_link_pool)"
    "PROPERTY JOB_POOL_LINK fsim_link_pool"
    "FSIM_COMPACT_DEBUG_BUILD"
    "<CONFIG:Debug>:-Og"
    "<CONFIG:Debug>:-gz=zstd")
  string(FIND "${FSIM_FOOTPRINT_CONTENTS}" "${FSIM_FOOTPRINT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "debug/link footprint lost policy: ${FSIM_FOOTPRINT_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_FSM_VALIDATION_POLICY IN ITEMS
    "fsim-coverage-fsm-validation-v3"
    "FSIM-COV-028"
    "FSIM-COV-029"
    "FSIM-COV-030"
    "CoverageFsmDescriptionIssueKind"
    "Ambiguous"
    "Incomplete"
    "Conflicting"
    "CoverageFsmDescriptionSubject"
    "CurrentState"
    "NextState"
    "LegalStates"
    "SystemVerilogPragma"
    "VhdlSource"
    "Manifest"
    "maximum_candidates { 1U << 20U }"
    "maximum_diagnostics { 1U << 20U }"
    "maximum_origins { 6U }"
    "maximum_instance_bytes { 1U << 20U }"
    "maximum_object_bytes { 1U << 16U }"
    "make_coverage_fsm_description_diagnostics"
    "Equivalent candidates are coalesced"
    "diagnostic input order must not change stable coalesced results"
    "each description issue kind must own one stable diagnostic code"
    "complete input is validated before any")
  string(FIND
    "${FSIM_COVERAGE_FSM_VALIDATION_CONTENTS}${FSIM_COVERAGE_FSM_VALIDATION_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_FSM_VALIDATION_TEST_CONTENTS}${FSIM_COVERAGE_FSM_INFERENCE_CONTENTS}${FSIM_COVERAGE_FSM_INFERENCE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_FSM_INFERENCE_TEST_CONTENTS}${FSIM_COVERAGE_FSM_HINTS_TEST_CONTENTS}"
    "${FSIM_COVERAGE_FSM_VALIDATION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage FSM validation lost ambiguity, incompleteness, conflict, identity, origin, transaction, or resource policy: ${FSIM_COVERAGE_FSM_VALIDATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_FSM_RUNTIME_POLICY IN ITEMS
    "fsim-coverage-fsm-runtime-v3"
    "FSIM-COV-027"
    "struct CoverageFsmVisitBin"
    "struct CoverageFsmTransitionBin"
    "struct CoverageFsmMachineRuntimeState"
    "Intentionally has no combined or synthetic FSM score"
    "maximum_machines { 1U << 20U }"
    "maximum_states { 1U << 22U }"
    "maximum_legal_transitions { 1U << 24U }"
    "maximum_observations { 1U << 24U }"
    "maximum_instance_bytes { 1U << 20U }"
    "state-visit"
    "legal-transition"
    "record_coverage_fsm_observations"
    "undeclared pair updates only the separate diagnostic count"
    "interleaved machines must retain independent previous-state history"
    "visit, transition, and diagnostic counters must saturate explicitly"
    "the complete observation batch must validate before mutation")
  string(FIND
    "${FSIM_COVERAGE_FSM_RUNTIME_CONTENTS}${FSIM_COVERAGE_FSM_RUNTIME_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_FSM_RUNTIME_TEST_CONTENTS}"
    "${FSIM_COVERAGE_FSM_RUNTIME_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage FSM runtime lost visit, transition, identity, saturation, transaction, or resource policy: ${FSIM_COVERAGE_FSM_RUNTIME_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_FSM_INFERENCE_POLICY IN ITEMS
    "fsim-coverage-fsm-inference-v3"
    "maximum_sources { 1U << 16U }"
    "maximum_objects { 1U << 20U }"
    "maximum_cases { 1U << 20U }"
    "maximum_case_choices { 1U << 22U }"
    "maximum_assignments { 1U << 22U }"
    "maximum_pragmas { 1U << 16U }"
    "maximum_pragma_specifications { 1U << 18U }"
    "maximum_pragma_value_bytes { 1U << 20U }"
    "maximum_states { 1U << 22U }"
    "maximum_next_state_objects { 1U << 20U }"
    "maximum_legal_state_sets { 1U << 20U }"
    "maximum_state_name_bytes { 1U << 16U }"
    "maximum_hierarchy_bytes { 1U << 20U }"
    "maximum_statement_depth { 1U << 12U }"
    "maximum_line_number { 1ULL << 31U }"
    "CodeCoverageConstructKind::FsmCurrentStateObject"
    "CodeCoverageConstructKind::FsmNextStateObject"
    "statement.case_match_kind"
    "unit.type_aliases"
    "compatible_state_types"
    "SystemVerilogFsmPragma"
    "fsm_current_state"
    "fsm_next_state"
    "fsm_legal_states"
    "valid_systemverilog_pragma_standard"
    "vendor_fsm_encoding"
    "only exact vendor-neutral FSM pragma keys must be retained by group"
    "every retained SystemVerilog profile must honor exact FSM pragmas"
    "SystemVerilog FSM pragma semantics must not leak into Verilog profiles"
    "FSM pragma-group ceiling must be enforced"
    "FSM pragma-specification ceiling must be enforced"
    "FSM pragma-value byte ceiling must be enforced"
    "enum- and exact-case-driven retained objects must be inferred"
    "every retained VHDL profile must infer enum/case current state equivalently"
    "conflicting case-only descriptions must not infer an ambiguous object"
    "a process-local shadow must prevent unit-object case inference"
    "compatible scalar assignment must infer an optional next-state object"
    "legal-state sets must reference existing stable state identities"
    "multiple compatible next-state candidates must suppress the optional relation only"
    "a process-local shadow must prevent retained next-state inference"
    "candidate-object ceiling must be enforced"
    "case-choice ceiling must be enforced"
    "aggregate inferred-state ceiling must be enforced"
    "statement-depth ceiling must be enforced"
    "assignment traversal ceiling must be enforced"
    "next-state object ceiling must be enforced"
    "legal-state-set ceiling must be enforced")
  string(FIND
    "${FSIM_COVERAGE_FSM_INFERENCE_CONTENTS}${FSIM_COVERAGE_FSM_INFERENCE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_FSM_INFERENCE_TEST_CONTENTS}${FSIM_FRONTEND_DESIGN_CONTENTS}${FSIM_VERILOG_PARSER_CORE_CONTENTS}"
    "${FSIM_COVERAGE_FSM_INFERENCE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage FSM inference lost enum/case, identity, ambiguity, profile, or resource policy: ${FSIM_COVERAGE_FSM_INFERENCE_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_FSM_HINT_POLICY IN ITEMS
    "fsim-coverage-fsm-hints-v3"
    "FSIM-COV-026"
    "CoverageFsmHintOrigin"
    "VhdlSource"
    "Manifest"
    "maximum_attributes { 1U << 18U }"
    "maximum_attribute_entity_names { 1U << 18U }"
    "maximum_manifest_hints { 1U << 16U }"
    "maximum_hints { 1U << 18U }"
    "maximum_legal_states { 1U << 20U }"
    "maximum_name_bytes { 1U << 16U }"
    "maximum_value_bytes { 1U << 20U }"
    "maximum_instance_bytes { 1U << 20U }"
    "fsm_current_state"
    "fsm_next_state"
    "fsm_legal_states"
    "attribute.entity_class != \"signal\""
    "marker != \"true\" && marker != \"false\""
    "struct CoverageFsmHintEntry"
    "normalized == \"coverage.fsm\""
    "assign_coverage_fsm"
    "current_state = \"state\""
    "next_state = \"next_state\""
    "legal_states = [\"idle\", \"run\"]"
    "all retained VHDL profiles must construct identical FSM hints"
    "the same manifest model must describe a SystemVerilog FSM without source semantics"
    "matching SystemVerilog pragma and manifest descriptions must retain both provenance sources"
    "conflicting source and manifest descriptions must suppress explicit evidence without losing enum inference"
    "unknown vendor keys must not become manifest FSM aliases"
    "inference must revalidate its untrusted hint-count ceiling"
    "inference must revalidate its aggregate hint-state ceiling")
  string(FIND
    "${FSIM_COVERAGE_FSM_HINTS_CONTENTS}${FSIM_COVERAGE_FSM_HINTS_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_FSM_HINTS_TEST_CONTENTS}${FSIM_COVERAGE_FSM_HINTS_PROJECT_IMPLEMENTATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_PROJECT_CONTENTS}${FSIM_COVERAGE_FSM_INFERENCE_CONTENTS}${FSIM_COVERAGE_FSM_INFERENCE_IMPLEMENTATION_CONTENTS}"
    "${FSIM_COVERAGE_FSM_HINT_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage FSM hints lost VHDL, manifest, composition, or resource policy: ${FSIM_COVERAGE_FSM_HINT_POLICY}")
  endif()
endforeach()

foreach(FSIM_VERILOG_TOGGLE_INVENTORY_POLICY IN ITEMS
    "maximum_sources { 1U << 16U }"
    "maximum_objects { 1U << 20U }"
    "maximum_bits { 1U << 22U }"
    "maximum_object_width { 1U << 20U }"
    "maximum_hierarchy_bytes { 1U << 20U }"
    "CoverageInstanceIdentity instance_identity"
    "CodeCoverageConstructKind::ToggleObject"
    "instance_point_identity"
    "VerilogToggleObjectKind::RetainedVariable"
    "width_value > limits.maximum_bits - total_bits"
    "real, string, and whole-container objects must not manufacture binary bins"
    "the real parser semantic surface must expose every packed port, net, signal, and retained variable bit"
    "empty semantic libraries must normalize to the elaborated work owner"
    "aggregate bit ceiling must be enforced"
    "per-object width ceiling must be enforced")
  string(FIND
    "${FSIM_VERILOG_TOGGLE_INVENTORY_CONTENTS}${FSIM_VERILOG_TOGGLE_INVENTORY_IMPLEMENTATION_CONTENTS}${FSIM_VERILOG_TOGGLE_INVENTORY_TEST_CONTENTS}"
    "${FSIM_VERILOG_TOGGLE_INVENTORY_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog toggle inventory lost hierarchy, identity, or resource policy: ${FSIM_VERILOG_TOGGLE_INVENTORY_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_TOGGLE_INVENTORY_POLICY IN ITEMS
    "maximum_sources { 1U << 16U }"
    "maximum_objects { 1U << 20U }"
    "maximum_bits { 1U << 22U }"
    "maximum_object_width { 1U << 20U }"
    "maximum_hierarchy_bytes { 1U << 20U }"
    "CoverageInstanceIdentity instance_identity"
    "CodeCoverageConstructKind::ToggleObject"
    "instance_point_identity"
    "VhdlToggleObjectKind::RetainedVariable"
    "valid_standard"
    "directly_packed_vhdl_vector"
    "owner.source_dependencies"
    "every retained VHDL profile must inventory ports, signals, and shared retained variables equivalently"
    "entity ports must be authenticated through the architecture owner's exact source dependency"
    "ordinary locals, files, protected/physical objects, and process locals must remain outside default VHDL toggle selection"
    "aggregate-bit, and per-object width ceilings must be enforced")
  string(FIND
    "${FSIM_VHDL_TOGGLE_INVENTORY_CONTENTS}${FSIM_VHDL_TOGGLE_INVENTORY_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_TOGGLE_INVENTORY_TEST_CONTENTS}"
    "${FSIM_VHDL_TOGGLE_INVENTORY_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL toggle inventory lost profile, hierarchy, identity, or resource policy: ${FSIM_VHDL_TOGGLE_INVENTORY_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_TOGGLE_SELECTION_POLICY IN ITEMS
    "fsim-coverage-toggle-selection-v3"
    "maximum_sources { 1U << 16U }"
    "maximum_declarations { 1U << 20U }"
    "maximum_exclusions { 1U << 20U }"
    "maximum_reasons { 1U << 21U }"
    "maximum_scope_depth { 64U }"
    "CoverageToggleExclusionReason"
    "AutomaticLocal"
    "ProceduralLocal"
    "Memory"
    "Array"
    "CodeCoverageConstructKind::ToggleObject"
    "owner.source_dependencies"
    "every default-excluded local, memory, and array must remain explicit"
    "default exclusions must be checkout-location independent"
    "non-vector VHDL arrays and retained array memories must be explicit default exclusions"
    "invalid callable or procedural scope text must be rejected"
    "declaration, exclusion, reason, and lexical-depth ceilings must be enforced")
  string(FIND
    "${FSIM_COVERAGE_TOGGLE_SELECTION_CONTENTS}${FSIM_COVERAGE_TOGGLE_SELECTION_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_TOGGLE_SELECTION_TEST_CONTENTS}"
    "${FSIM_COVERAGE_TOGGLE_SELECTION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage toggle default selection lost exclusion, identity, or resource policy: ${FSIM_COVERAGE_TOGGLE_SELECTION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_MEMORY_TOGGLE_POLICY IN ITEMS
    "fsim-coverage-memory-toggle-v3"
    "maximum_exclusions { 1U << 20U }"
    "maximum_rules { 1U << 16U }"
    "maximum_selectors { 1U << 20U }"
    "maximum_dimensions { 64U }"
    "maximum_string_keys { 1U << 20U }"
    "maximum_range_span { 1U << 20U }"
    "maximum_elements { 1U << 20U }"
    "maximum_bits { 1U << 22U }"
    "No wildcard or"
    "element_identity"
    "DuplicateBitSelection"
    "post-parse static memory shape must retain exact dimensions and packed element width"
    "no empty selector may silently enable a whole container"
    "static selection must reject unresolved elaboration-time bounds"
    "rule and key declaration order must not affect the canonical inventory"
    "exclusion, range, element, bit, and selector ceilings must reject before publication")
  string(FIND
    "${FSIM_COVERAGE_MEMORY_TOGGLE_CONTENTS}${FSIM_COVERAGE_MEMORY_TOGGLE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_MEMORY_TOGGLE_TEST_CONTENTS}"
    "${FSIM_COVERAGE_MEMORY_TOGGLE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage memory/array toggle selection lost explicit range, identity, or resource policy: ${FSIM_COVERAGE_MEMORY_TOGGLE_POLICY}")
  endif()
endforeach()

file(READ "${FSIM_COVERAGE_INSTANCE_IDENTITY}"
  FSIM_COVERAGE_INSTANCE_IDENTITY_CONTENTS)
file(READ "${FSIM_COVERAGE_INSTANCE_IDENTITY_IMPLEMENTATION}"
  FSIM_COVERAGE_INSTANCE_IDENTITY_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_COVERAGE_INSTANCE_IDENTITY_TEST}"
  FSIM_COVERAGE_INSTANCE_IDENTITY_TEST_CONTENTS)
foreach(FSIM_COVERAGE_INSTANCE_IDENTITY_POLICY IN ITEMS
    "fsim-code-coverage-instance-v3"
    "maximum_hierarchy_bytes { 1U << 20U }"
    "maximum_parameter_count { 1U << 16U }"
    "maximum_parameter_bytes { 1U << 20U }"
    "DuplicateParameterName"
    "std::ranges::sort(parameters"
    "top.lanes[3].decoder"
    "top.lanes[4].decoder")
  string(FIND
    "${FSIM_COVERAGE_INSTANCE_IDENTITY_CONTENTS}${FSIM_COVERAGE_INSTANCE_IDENTITY_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_INSTANCE_IDENTITY_TEST_CONTENTS}"
    "${FSIM_COVERAGE_INSTANCE_IDENTITY_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage instance identity lost policy: ${FSIM_COVERAGE_INSTANCE_IDENTITY_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_AGGREGATION_POLICY IN ITEMS
    "maximum_instances { 1U << 20U }"
    "maximum_sources { 1U << 16U }"
    "maximum_points { 1U << 20U }"
    "OwnershipCountMismatch"
    "DuplicateInstancePoint"
    "PointSourceConflict"
    "hits_saturated = true"
    "covered_occurrences"
    "result.instances[0].points[1].status"
    "result.sources[0].points.size() == 2U")
  string(FIND
    "${FSIM_COVERAGE_AGGREGATION_CONTENTS}${FSIM_COVERAGE_AGGREGATION_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_AGGREGATION_TEST_CONTENTS}"
    "${FSIM_COVERAGE_AGGREGATION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage source aggregation lost policy: ${FSIM_COVERAGE_AGGREGATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_CONTROL_POLICY IN ITEMS
    "struct CoverageSection"
    "bool enabled{false}"
    "std::optional<bool> code_coverage"
    "--code-coverage"
    "bool code_coverage_enabled { }"
    "return config.coverage.enabled"
    "!built->design.code_coverage_inventory()"
    "assert(enabled->code_coverage_enabled)")
  string(FIND
    "${FSIM_CODE_COVERAGE_CONTROL_PROJECT_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_CLI_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_CLI_IMPLEMENTATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_APPLICATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_IMPLEMENTATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_TEST_CONTENTS}"
    "${FSIM_CODE_COVERAGE_CONTROL_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage opt-in control lost fixed disabled-default policy: ${FSIM_CODE_COVERAGE_CONTROL_POLICY}")
  endif()
endforeach()

foreach(FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_POLICY IN ITEMS
    "SV_COV_START"
    "SV_COV_OVERFLOW"
    "FSIM-COV-038"
    "enum class SystemVerilogCoverageScope"
    "struct CoverageControl"
    "SystemVerilogCoverageStatus"
    "control_coverage"
    "CodeCoverageCounterUpdate::Ignored"
    "direct_values"
    "CoverageControl>"
    "code_coverage_counter("
    "sources.standard = \"2005\""
    "test_counter_control_semantics"
    "test_coverage_control_cache_identity"
    "fsim.application.code-coverage-control")
  string(FIND
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PREPROCESSOR_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_OPERATIONS_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_INTERPRETER_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_BOUNDARIES_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LLVM_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_APPLICATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog coverage control lost standard, engine, or cache policy: ${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_POLICY}")
  endif()
endforeach()

foreach(FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_POLICY IN ITEMS
    "FSIM-COV-039"
    "enum class SystemVerilogCoverageAccessKind"
    "struct CoverageAccess"
    "access_coverage"
    "$coverage_get"
    "$coverage_get_max"
    "$coverage_merge"
    "$coverage_save"
    "write_coverage_database_atomically"
    "merge_coverage_databases"
    "coverage_database_file(event.filename)"
    "test_coverage_access_semantics"
    "test_coverage_access_cache_identity")
  string(FIND
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_OPERATIONS_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_INTERPRETER_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_BOUNDARIES_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LLVM_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_APPLICATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_TEST_CONTENTS}${FSIM_DIAGNOSTICS_CONTENTS}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog coverage access lost query, persistence, engine, or cache policy: ${FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_POLICY}")
  endif()
endforeach()

foreach(FSIM_SYSTEMVERILOG_COVERAGE_SELECTION_POLICY IN ITEMS
    "selector_is_instance"
    "instance_context"
    "builder.add(\"instance-context\""
    "select_standard_coverage("
    "standard_module_name_matches("
    "selector == \"$root\""
    "scope == Scope::hierarchy"
    "set_code_coverage_collection_enabled("
    "test_standard_coverage_selection"
    "leaf_maximum == 2U"
    "hierarchy_maximum == 3U")
  string(FIND
    "${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_OPERATIONS_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_INTERPRETER_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_BOUNDARIES_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_LLVM_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_APPLICATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_CONTROL_CACHE_TEST_CONTENTS}"
    "${FSIM_SYSTEMVERILOG_COVERAGE_SELECTION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog coverage selection lost scope, instance, engine, or cache policy: ${FSIM_SYSTEMVERILOG_COVERAGE_SELECTION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_INVENTORY_POLICY IN ITEMS
    "maximum_sources { 1U << 16U }"
    "maximum_instances { 1U << 20U }"
    "maximum_points { 1U << 20U }"
    "maximum_line_number { 1ULL << 31U }"
    "instances.size() != owners.size()"
    "instance.points.size() > limits.maximum_points - total_points"
    "point.span.end_offset"
    "PointSourceOwnershipMismatch"
    "CounterOwnershipMismatch"
    "DuplicateInstanceIdentity"
    "instance.identity != *identity.identity"
    "code_coverage_inventory_ = std::move(*built.inventory)"
    "complete only when every elaborated owner occurs exactly once"
    "Only the final retained point vectors are materialized"
    "limits.maximum_points = 3U"
    "invalid_state.code_coverage_inventory")
  string(FIND
    "${FSIM_COVERAGE_INVENTORY_CONTENTS}${FSIM_COVERAGE_INVENTORY_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_INVENTORY_TEST_CONTENTS}"
    "${FSIM_COVERAGE_INVENTORY_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage instance inventory lost resource policy: ${FSIM_COVERAGE_INVENTORY_POLICY}")
  endif()
endforeach()

foreach(FSIM_SIMIR_COVERAGE_POLICY IN ITEMS
    "maximum_points { 1U << 20U }"
    "maximum_operations { 1U << 24U }"
    "process.operations.size() > limits.maximum_operations"
    "process.operations.size()"
    "std::numeric_limits<InstructionIndex>::max()"
    "CodeCoverageHitError::ResourceLimit"
    "CounterOwnershipMismatch"
    "DuplicatePointOwnership"
    "DuplicateHit"
    "validation must not mutate unrelated process state"
    "counter differences must not copy the immutable operation body")
  string(FIND
    "${FSIM_SIMIR_COVERAGE_CONTENTS}${FSIM_SIMIR_COVERAGE_IMPLEMENTATION_CONTENTS}${FSIM_SIMIR_COVERAGE_TEST_CONTENTS}"
    "${FSIM_SIMIR_COVERAGE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SimIR code coverage hit lost bounded ownership policy: ${FSIM_SIMIR_COVERAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_EXCLUSION_POLICY IN ITEMS
    "CoveragePointExclusionKind::Declaration"
    "CoveragePointExclusionKind::StaticallyRemoved"
    "maximum_points { 1U << 20U }"
    "maximum_exclusions { 1U << 20U }"
    "point.span.begin_offset"
    "point.span.end_offset"
    "DuplicateExclusion"
    "A procedural declaration must not manufacture an executable point"
    "Every retained VHDL profile must exclude declarations"
    "Enclosing and neighboring executable identities must not change")
  string(FIND
    "${FSIM_COVERAGE_POINTS_CONTENTS}${FSIM_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_POINTS_TEST_CONTENTS}${FSIM_VERILOG_COVERAGE_POINTS_TEST_CONTENTS}${FSIM_VHDL_COVERAGE_POINTS_TEST_CONTENTS}"
    "${FSIM_COVERAGE_EXCLUSION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage exclusion lost bounded ownership policy: ${FSIM_COVERAGE_EXCLUSION_POLICY}")
  endif()
endforeach()

foreach(FSIM_INTERPRETER_COVERAGE_POLICY IN ITEMS
    "std::vector<std::uint64_t> values_"
    "std::vector<std::uint8_t> overflowed_"
    "values.size() > maximum_points"
    "std::numeric_limits<std::uint64_t>::max()"
    "CodeCoverageCounterUpdate::FirstOverflow"
    "CodeCoverageCounterUpdate::Saturated"
    "overflow must be sticky and must never wrap the counter"
    "each executed hit must increment its exact counter once"
    "interpreter overflow must saturate and report the counter once"
    "a rejected counter-table replacement must preserve prior state")
  string(FIND
    "${FSIM_SIMIR_COVERAGE_CONTENTS}${FSIM_SIMIR_COVERAGE_IMPLEMENTATION_CONTENTS}${FSIM_SIMIR_COVERAGE_TEST_CONTENTS}"
    "${FSIM_INTERPRETER_COVERAGE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "interpreter code coverage counter lost saturation policy: ${FSIM_INTERPRETER_COVERAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_LLVM_COVERAGE_POLICY IN ITEMS
    "code_coverage_hit_counters"
    "code_coverage_counter_values"
    "code_coverage_hit_count"
    "code_coverage_counter_count"
    "record_code_coverage_counter"
    "std::numeric_limits<std::uint64_t>::max()"
    "compiled coverage must update the effective instance counter"
    "compiled coverage overflow must be reported exactly once"
    "application O3 must retain the qualified optimized native profile")
  string(FIND
    "${FSIM_JIT_RUNTIME_CONTENTS}${FSIM_LLVM_COVERAGE_IMPLEMENTATION_CONTENTS}${FSIM_LLVM_COVERAGE_TEST_CONTENTS}"
    "${FSIM_LLVM_COVERAGE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "LLVM code coverage counter lost bounded direct-path policy: ${FSIM_LLVM_COVERAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_DEBUG_COVERAGE_POLICY IN ITEMS
    "debug_code_coverage_snapshot"
    "maximum_points"
    "code_coverage_counter"
    "counter_out_of_range"
    "duplicate_point"
    "Debug pause before the statement must not manufacture a hit"
    "Debug execution must retain point identity and record one hit")
  string(FIND
    "${FSIM_DEBUG_COVERAGE_IMPLEMENTATION_CONTENTS}${FSIM_LLVM_COVERAGE_TEST_CONTENTS}"
    "${FSIM_DEBUG_COVERAGE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Debug code coverage lost identity/hit observation policy: ${FSIM_DEBUG_COVERAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_LINE_STATE_POLICY IN ITEMS
    "maximum_sources { 1U << 16U }"
    "maximum_statement_points { 1U << 20U }"
    "maximum_lines { 1U << 20U }"
    "maximum_line_number { 1ULL << 31U }"
    "statement_sites.size() > limits.maximum_statement_points"
    "site.source_index >= limits.maximum_sources"
    "site.line > limits.maximum_line_number"
    "result.lines.size() >= limits.maximum_lines"
    "derived lines never acquire counters of their own"
    "statement ownership must be bounded before allocation"
    "unique derived lines must obey the configured ceiling")
  string(FIND
    "${FSIM_COVERAGE_LINE_STATE_CONTENTS}${FSIM_COVERAGE_LINE_STATE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_LINE_STATE_TEST_CONTENTS}"
    "${FSIM_COVERAGE_LINE_STATE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage line-state derivation lost resource policy: ${FSIM_COVERAGE_LINE_STATE_POLICY}")
  endif()
endforeach()

foreach(FSIM_FST_VALUE_POLICY IN ITEMS
    "maximum_container_bytes"
    "maximum_hierarchy_bytes"
    "maximum_buffer_bytes"
    "maximum_name_bytes"
    "maximum_events"
    "maximum_timestamps"
    "maximum_type_metadata_bytes"
    "maximum_value_bytes"
    "fst_maximum_type_metadata_bytes"
    "fst_maximum_string_bytes"
    "fst_maximum_type_members"
    "fst_maximum_shape_dimensions"
    "FST change count exceeds its limit"
    "FST timestamp count exceeds its limit"
    "FST change payload exceeds its byte limit"
    "FST initial-value buffer exceeds its limit"
    "FST change buffer exceeds its limit"
    "FST writer is terminal after failure"
    "failed to publish FST output"
    "trace lifecycle ended without a clean close"
    "terminal_count == 1"
    "FST trace instance ancestry is invalid"
    "trace hierarchy maps distinct signals"
    "fsim-trace-provenance-v1"
    "trace observation fanout cannot be reentrant"
    "trace observation callback failed after accepting a record"
    "late trace snapshot would reorder an accepted record"
    "trace selection declaration owner is invalid"
    "FST values must have a nonzero width"
    "4'097U"
    "4'103U")
  string(FIND
    "${FSIM_FST_WRITER_CONTENTS}${FSIM_FST_WRITER_IMPLEMENTATION_CONTENTS}${FSIM_FST_WRITER_TEST_CONTENTS}${FSIM_FST_FORMAT_CONTENTS}${FSIM_FST_FORMAT_TEST_CONTENTS}${FSIM_FST_ENCODER_CONTENTS}${FSIM_FST_ENCODER_TEST_CONTENTS}${FSIM_FST_CHANGE_ENCODER_CONTENTS}${FSIM_FST_CHANGE_ENCODER_TEST_CONTENTS}${FSIM_FST_HIERARCHY_CONTENTS}${FSIM_FST_HIERARCHY_TEST_CONTENTS}${FSIM_FST_OBSERVATION_CONTENTS}${FSIM_FST_OBSERVATION_TEST_CONTENTS}${FSIM_FST_CONTROL_CONTENTS}${FSIM_FST_CONTROL_TEST_CONTENTS}${FSIM_FST_APPLICATION_CONTENTS}"
    "${FSIM_FST_VALUE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "FST arbitrary-width value resource policy lost token: ${FSIM_FST_VALUE_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_POLICY IN ITEMS
    "maximum_points { 1U << 20U }"
    "maximum_metric_results { 3U }"
    "run.points.size() > limits.maximum_points"
    "result.points.size() > limits.maximum_points"
    "result.metrics.size() > limits.maximum_metric_results"
    "CodeCoverageModelError::ResourceLimit"
    "the point budget must bound run ownership"
    "the metric-result budget must be bounded")
  string(FIND
    "${FSIM_CODE_COVERAGE_MODEL_CONTENTS}${FSIM_CODE_COVERAGE_MODEL_IMPLEMENTATION_CONTENTS}${FSIM_CODE_COVERAGE_MODEL_TEST_CONTENTS}"
    "${FSIM_CODE_COVERAGE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage model lost bounded ownership policy: ${FSIM_CODE_COVERAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_POLICY IN ITEMS
    "kCodeCoverageArtifactSchema = 3U"
    "kCodeCoverageDisabledModel = \"none\""
    "fsim-code-coverage-foundation-v3"
    "fsim-code-coverage-broad-metrics-v3"
    "fsim-code-coverage-artifact-identity-v3"
    "identity.digest.size() == 64U"
    "invalid.schema = 2U")
  string(FIND
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_CONTENTS}${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_IMPLEMENTATION_CONTENTS}${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_TEST_CONTENTS}"
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage artifact identity lost fixed v3 policy: ${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_METRICS_INTEGRATION_POLICY IN ITEMS
    "kSystemVerilogSource"
    "kVhdlSource"
    "top.sv_gen[0]"
    "top.vhdl_gen[1]"
    "coverage_condition_outcome_status"
    "coverage_expression_combination_truth"
    "coverage_toggle_status"
    "summarize_coverage_fsm"
    "kInstanceCount = 6U"
    "project::Optimization::o0"
    "project::Optimization::o1"
    "project::Optimization::o2"
    "project::Optimization::o3"
    "debug_engine"
    "kCodeCoverageBroadMetricsModel"
    "kCodeCoverageFoundationModel"
    "fsim.application.code-coverage-metrics"
    "fsim.application.code-coverage-metrics-equivalence"
    "fsim.artifact.code-coverage-metrics-identity")
  string(FIND
    "${FSIM_CODE_COVERAGE_METRICS_APPLICATION_TEST_CONTENTS}${FSIM_CODE_COVERAGE_METRICS_EQUIVALENCE_TEST_CONTENTS}${FSIM_CODE_COVERAGE_METRICS_IDENTITY_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_CODE_COVERAGE_METRICS_INTEGRATION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage broad-metric integration lost policy: ${FSIM_CODE_COVERAGE_METRICS_INTEGRATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_EQUIVALENCE_POLICY IN ITEMS
    "kVerilogSource"
    "kSystemVerilogSource"
    "kVhdlSource"
    "project::Optimization::o0"
    "project::Optimization::o1"
    "project::Optimization::o2"
    "project::Optimization::o3"
    "debug_engine"
    "point.covered_occurrences == 1U"
    "point.uncovered_occurrences == 1U"
    "make_code_coverage_aggregation")
  string(FIND
    "${FSIM_CODE_COVERAGE_EQUIVALENCE_TEST_CONTENTS}"
    "${FSIM_CODE_COVERAGE_EQUIVALENCE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage engine/aggregation equivalence lost policy: ${FSIM_CODE_COVERAGE_EQUIVALENCE_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_METRICS_POLICY IN ITEMS
    "COVMET-C02"
    "COVMET-C18"
    "verilog-condition-decomposition"
    "vhdl-condition-decomposition"
    "short-circuit-recording"
    "four-state-outcomes"
    "expression-combinations"
    "binary-toggle-bins"
    "verilog-toggle-objects"
    "vhdl-toggle-objects"
    "toggle-default-exclusions"
    "memory-array-selection"
    "unknown-toggle-diagnostics"
    "current-state-inference"
    "next-state-legal-sets"
    "systemverilog-fsm-pragmas"
    "vhdl-manifest-fsm-hints"
    "fsm-visits-transitions"
    "fsm-description-validation"
    "set(FSIM_COMPLETED_CHANGE 18)"
    "set(FSIM_COMPLETED_INTEGRATION_CHANGE 19)"
    "mc/dc"
    "fsim.code-coverage-metrics-inventory")
  string(TOLOWER
    "${FSIM_CODE_COVERAGE_METRICS_INVENTORY_CONTENTS}${FSIM_CODE_COVERAGE_METRICS_CHECKER_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    FSIM_CODE_COVERAGE_METRICS_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_CODE_COVERAGE_METRICS_POLICY}"
    FSIM_CODE_COVERAGE_METRICS_POLICY_LOWER)
  string(FIND "${FSIM_CODE_COVERAGE_METRICS_CONTENTS_LOWER}"
    "${FSIM_CODE_COVERAGE_METRICS_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage metrics inventory lost resource policy: ${FSIM_CODE_COVERAGE_METRICS_POLICY}")
  endif()
endforeach()

foreach(FSIM_LEGACY_ACC_POLICY IN ITEMS
    "LEGACC-C02"
    "LEGACC-C19"
    "public-acc-header"
    "lifecycle-configuration-errors"
    "generation-qualified-handles"
    "name-lookup"
    "hierarchy-traversal"
    "complete-object-model"
    "value-and-property-reads"
    "value-updates"
    "indexed-iterators"
    "path-delay-timing-checks"
    "value-change-links"
    "callback-cancellation-order"
    "safe-point-validity"
    "tf-acc-coherence"
    "acc-vpi-object-equivalence"
    "parallel-coordination"
    "vendor-name-rejection"
    "legacy-pli-closure-corpus"
    "ieee-only-no-vendor-extensions"
    "set(FSIM_COMPLETED_CHANGE 19)"
    "FSIM_ROUTINE_COUNT EQUAL 102"
    "FSIM_OBJECT_COUNT EQUAL 115"
    "typedef struct t_acc_time"
    "typedef struct t_setval_value"
    "typedef struct t_vc_record"
    "acc_handle_calling_mod_m"
    "fsim.runtime.acc_user_abi"
    "kMaximumConfigurationValueSize = 4096"
    "bounded_configuration_value"
    "std::scoped_lock"
    "acc_error_flag = 0"
    "FSIM_PROJECT_VERSION"
    "fsim.runtime.acc_lifecycle"
    "FSIM_ACC_HANDLE_MAX_OBJECTS"
    "simulation_identity"
    "hierarchy_generation"
    "validate_tf_callback_pointer"
    "fsim_acc_handle_from_vpi_v3"
    "acc_release_object"
    "mapping rejects a second valid VPI object at the context limit"
    "fsim.runtime.acc_handle"
    "FSIM_ACC_LOOKUP_ABSOLUTE"
    "FSIM_ACC_LOOKUP_RELATIVE"
    "FSIM_ACC_LOOKUP_PLI_SCOPE"
    "FSIM_ACC_RELATION_SIMULATED_NET"
    "FSIM_ACC_NAME_MAXIMUM_BYTES 4096u"
    "configuration_enabled"
    "enabled optional set-scope names select an absolute module"
    "unreadable lookup names fail before callback dispatch"
    "fsim.runtime.acc_lookup"
    "FSIM_ACC_TRAVERSE_BEGIN"
    "FSIM_ACC_TRAVERSE_NEXT"
    "FSIM_ACC_TRAVERSE_END"
    "FSIM_ACC_COLLECTION_MAXIMUM_OBJECTS"
    "filtered next traversal preserves canonical child creation order"
    "next traversal can recover position from a valid prior object"
    "completed and failed traversals release every native cursor"
    "fsim.runtime.acc_traversal"
    "FSIM_ACC_OBJECT_QUERY_ABI_VERSION 3u"
    "fsim_acc_object_query_v3"
    "type_matches"
    "full types preserve standardized generic object membership"
    "enabled module-path arguments select validated connection handles"
    "enabled timing-check arguments select a validated connection handle"
    "invalid owners indices names and edges fail before callback dispatch"
    "fsim.runtime.acc_object"
    "FSIM_ACC_READ_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_READ_MAXIMUM_BITS"
    "fsim_acc_vpi_read_v3_fn"
    "thread_local std::string string_buffer"
    "wide four-state values preserve every aval and bval word"
    "structured string-copy failures retain the ACC error state"
    "resolver exceptions are contained at the ACC boundary"
    "fsim.runtime.acc_read"
    "FSIM_ACC_WRITE_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_WRITE_CAP_DEPOSIT"
    "fsim_acc_vpi_write_v3_fn"
    "no-delay vector deposit copies every four-state word atomically"
    "every standardized string input format is copied exactly"
    "release returns the post-release vector in the same transaction"
    "write callback exceptions are contained at the ACC boundary"
    "fsim.runtime.acc_write"
    "FSIM_ACC_ITERATOR_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_ITERATOR_MAXIMUM_TYPES 256u"
    "std::map<SessionKey, IteratorSession>"
    "same result identity can own independent relation-family cursors"
    "iterator recovers position from a valid prior relation object"
    "completed and failed indexed iterators release every native cursor"
    "fsim.runtime.acc_iterator"
    "FSIM_ACC_TIMING_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_TIMING_MAXIMUM_DELAYS 12u"
    "fsim_acc_vpi_timing_v3_fn"
    "configuration_value"
    "single delay values publish after validation"
    "minimum typical maximum delays use one bounded array"
    "pulse reject and error pairs publish atomically"
    "malformed results cannot partially publish"
    "callback exceptions are contained"
    "fsim.runtime.acc_timing"
    "FSIM_ACC_VCL_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_VCL_MAXIMUM_LINKS"
    "fsim_acc_vcl_dispatch_v3"
    "logic callback retains time value and user data"
    "strength callback retains logic and both strengths"
    "vector callback retains the exact generation-qualified handle"
    "consumer exceptions are contained at the ACC boundary"
    "fsim.runtime.acc_vcl"
    "FSIM_ACC_VCL_UNREGISTER"
    "sequence_identity"
    "std::condition_variable link_condition"
    "cancel_vcl_link"
    "out-of-order callback sequence is rejected"
    "cancellation blocks simulator re-entry before unregister returns"
    "removed callbacks cannot publish late observations"
    "self-cancellation returns without deadlock"
    "fsim.runtime.acc_callback"
    "FSIM_ACC_SAFE_POINT_ABI_VERSION 3u"
    "fsim_acc_safe_point_advance_v3"
    "invalidate_iterator_safe_point"
    "reset_read_borrowed_storage"
    "reset_write_borrowed_storage"
    "safe-point advance closes retained iterator cursors"
    "generation-qualified handles survive safe-point advance"
    "callback links survive safe-point advance"
    "an iterator cannot resume across its safe-point boundary"
    "old handles reject a new hierarchy generation"
    "fsim.runtime.acc_handle_lifetime"
    "FSIM_ACC_TF_CONTEXT_ABI_VERSION 3u"
    "fsim_acc_tf_context_v3"
    "fsim_tf_current_call_context_v3"
    "valid_tf_context_binding"
    "ACC and TF share argument values, instance scope, and work area"
    "shared call state cannot escape the callback lifetime"
    "fsim.runtime.acc_tf_coherence"
    "fsim_acc_vpi_same_object_v3"
    "SystemVerilogVpiObjectKind::Constant"
    "SystemVerilogVpiObjectKind::Concatenation"
    "SystemVerilogVpiObjectKind::Operation"
    "SystemVerilogVpiObjectKind::MinTypMax"
    "ACC value reads the same storage as the VPI registry"
    "ACC hierarchy resolves the same VPI parent and full name"
    "ACC connectivity returns the exact VPI expression identity"
    "ACC timing reads the same VPI-keyed path record"
    "ACC and VPI reject the same released generation"
    "fsim.runtime.acc_vpi_coherence"
    "kMaximumAccSchedulerRequests"
    "AccSchedulerOperationKind"
    "std::recursive_mutex"
    "std::map<AccSchedulerSequence"
    "AccSchedulerError::OutOfOrderEpoch"
    "impl_->scheduler->running()"
    "impl_->scheduler->current_phase()"
    "parallel workers stage every ACC operation family"
    "publication retains the exact scheduler boundary"
    "worker completion order cannot change ACC execution order"
    "a later epoch cannot strand earlier staged ACC operations"
    "foreign exceptions and callback re-entry are contained deterministically"
    "fsim.application.acc-scheduler"
    "FSIM_ACC_STANDARD_QUERY_ABI_VERSION 3u"
    "FSIM_ACC_STANDARD_NAME_MAXIMUM_BYTES 128u"
    "kStandardRoutines.size() == 102U"
    "std::ranges::is_sorted(kStandardRoutines)"
    "FSIM-ACC-NAME-001"
    "FSIM-ACC-NAME-002"
    "FSIM-ACC-NAME-003"
    "FSIM-ACC-NAME-004"
    "FSIM-ACC-NAME-005"
    "FSIM-ACC-NAME-006"
    "validate_tf_callback_pointer(dispatch)"
    "an unsupported vendor routine cannot enter fallback dispatch"
    "every canonical ACC object constant is accepted"
    "an unsupported behavior selector cannot enter fallback dispatch"
    "a v2 query is rejected before name inspection or dispatch"
    "fsim.runtime.acc_vendor_rejection"
    "standard_acc_symbols.size() == 102U"
    "fsim_acc_link_probe"
    "fsim_acc_cpp_probe"
    "cached ACC plug-in artifacts did not load"
    "fsim.runtime.acc_cross_platform_plugins"
    "linux;windows;engine;artifact;cache"
    "fsim.legacy-acc-inventory")
  string(TOLOWER
    "${FSIM_LEGACY_ACC_INVENTORY_CONTENTS}${FSIM_LEGACY_ACC_CHECKER_CONTENTS}${FSIM_ACC_USER_HEADER_CONTENTS}${FSIM_ACC_USER_TEST_CONTENTS}${FSIM_ACC_USER_C_TEST_CONTENTS}${FSIM_ACC_LIFECYCLE_IMPLEMENTATION_CONTENTS}${FSIM_ACC_LIFECYCLE_TEST_CONTENTS}${FSIM_ACC_HANDLE_BRIDGE_CONTENTS}${FSIM_ACC_HANDLE_IMPLEMENTATION_CONTENTS}${FSIM_ACC_HANDLE_TEST_CONTENTS}${FSIM_ACC_INTERNAL_CONTENTS}${FSIM_ACC_LOOKUP_IMPLEMENTATION_CONTENTS}${FSIM_ACC_LOOKUP_TEST_CONTENTS}${FSIM_ACC_TRAVERSAL_IMPLEMENTATION_CONTENTS}${FSIM_ACC_TRAVERSAL_TEST_CONTENTS}${FSIM_ACC_OBJECT_IMPLEMENTATION_CONTENTS}${FSIM_ACC_OBJECT_TEST_CONTENTS}${FSIM_ACC_READ_IMPLEMENTATION_CONTENTS}${FSIM_ACC_READ_TEST_CONTENTS}${FSIM_ACC_WRITE_IMPLEMENTATION_CONTENTS}${FSIM_ACC_WRITE_TEST_CONTENTS}${FSIM_ACC_ITERATOR_IMPLEMENTATION_CONTENTS}${FSIM_ACC_ITERATOR_TEST_CONTENTS}${FSIM_ACC_TIMING_IMPLEMENTATION_CONTENTS}${FSIM_ACC_TIMING_TEST_CONTENTS}${FSIM_ACC_VCL_IMPLEMENTATION_CONTENTS}${FSIM_ACC_VCL_TEST_CONTENTS}${FSIM_ACC_CALLBACK_IMPLEMENTATION_CONTENTS}${FSIM_ACC_CALLBACK_TEST_CONTENTS}${FSIM_ACC_HANDLE_LIFETIME_IMPLEMENTATION_CONTENTS}${FSIM_ACC_HANDLE_LIFETIME_TEST_CONTENTS}${FSIM_ACC_TF_COHERENCE_IMPLEMENTATION_CONTENTS}${FSIM_ACC_TF_COHERENCE_TEST_CONTENTS}${FSIM_ACC_VPI_COHERENCE_IMPLEMENTATION_CONTENTS}${FSIM_ACC_VPI_COHERENCE_TEST_CONTENTS}${FSIM_ACC_SCHEDULER_MODEL_CONTENTS}${FSIM_ACC_SCHEDULER_IMPLEMENTATION_CONTENTS}${FSIM_ACC_SCHEDULER_TEST_CONTENTS}${FSIM_ACC_VENDOR_REJECTION_IMPLEMENTATION_CONTENTS}${FSIM_ACC_VENDOR_REJECTION_TEST_CONTENTS}${FSIM_ACC_C_PLUGIN_CONTENTS}${FSIM_ACC_CPP_PLUGIN_CONTENTS}${FSIM_ACC_CROSS_PLATFORM_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}${FSIM_ROOT_CONTENTS}"
    FSIM_LEGACY_ACC_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_LEGACY_ACC_POLICY}"
    FSIM_LEGACY_ACC_POLICY_LOWER)
  string(FIND "${FSIM_LEGACY_ACC_CONTENTS_LOWER}"
    "${FSIM_LEGACY_ACC_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "legacy ACC inventory lost resource policy: ${FSIM_LEGACY_ACC_POLICY}")
  endif()
endforeach()

foreach(FSIM_SYSTEMVERILOG_2023_POLICY IN ITEMS
    "S23-B185-C02"
    "S23-B185-C19"
    "S23-B186-C01"
    "S23-B186-C02"
    "S23-B186-C03"
    "S23-B186-C04"
    "S23-B186-C05"
    "S23-B186-C19"
    "S23-B187-C01"
    "S23-B187-C19"
    "IEEE1800-2023"
    "profile-identity"
    "concurrent-assertions"
    "functional-coverage"
    "modules-hierarchy"
    "dpi-declarations-runtime"
    "vpi-object-model"
    "clause-closure"
    "rows=56"
    "FSIM_ACTIVE_COUNT"
    "S23-B185-C11"
    "S23-B185-C12"
    "S23-B185-C13"
    "S23-B185-C14"
    "S23-B185-C15"
    "S23-B185-C16"
    "S23-B185-C17"
    "S23-B185-C18"
    "escaping_callable_contexts"
    "maximum_container_storage_bytes"
    "automatic_capture"
    "nonblocking { }"
    "SchedulerPhase::update"
    "FSIM-ELAB-SVASSIGN-001"
    "FSIM-ELAB-SVCAST-005"
    "pair_t'('{8'h12, 8'h34})"
    "values[index] <= source"
    "@stream-target-left"
    "FSIM-ELAB-SVSTREAM-002"
    "FSIM-ELAB-SVASSIGN-002"
    "{<<8{left, right}} = 16'h1234"
    "'{upper, lower} = 16'habcd"
    "AbsoluteTolerance"
    "RelativeTolerance"
    "@inside-absolute-tolerance"
    "@inside-relative-tolerance"
    "FSIM-ELAB-SVTOLERANCE-001"
    "107 inside {[100 +/- 7]}"
    "75 inside {[100 +%- 25]}"
    "@sv-foreach"
    "FSIM-ELAB-SVFOREACH-002"
    "FSIM-ELAB-SVFOREACH-004"
    "foreach (text[index])"
    "ref static"
    "const_reference"
    "static_reference"
    "FSIM-ELAB-SVFUNC-013"
    "FSIM-ELAB-SVTASK-015"
    "kOwningUnitSchemaVersion = 31"
    "visit_clocking_delays"
    "FSIM-ELAB-CLOCK-008"
    "FSIM-ELAB-CLOCK-009"
    "WaitRegion { runtime::SchedulerPhase::reactive }"
    "an observed deferred immediate assertion"
    "an observed deferred assertion action must be null or a single subroutine call"
    "WaitRegion{*deferred_assertion_action_phase_}"
    "DisableFork{*pass_fork}"
    "disable_fork && disable_fork->site"
    "targeted_cancellation_suspends == 0"
    "final failure sampled"
    "compiled.native_cache.misses == 1"
    "SystemVerilogConcurrentAssertionForm"
    "only a cover directive accepts the sequence form"
    "directive.inline_sequence"
    "test_cover_sequence_revisions"
    "cover sequence remains available in its retained and exact 2023 profiles"
    "cover sequence remains unavailable in the Verilog profile"
    "ConcurrentAssertionForm::sequence"
    "compiled.native_cache.misses != 0"
    "systemverilog_concurrent_assertion"
    "sample_concurrent_assertion_reads_"
    "SignalReadKind::sampled"
    "fsim.concurrent-assertion-action-region|"
    "test_concurrent_assertion_execution_regions"
    "sampled_before_update"
    "event:pass"
    "SchedulerPhase::observed"
    "SchedulerPhase::reactive"
    "SystemVerilogCheckerInstance"
    "checker_declarations"
    "checker_assertions"
    "parse_checker_instances"
    "compilation_unit_checkers_"
    "instantiate_checkers"
    "checker connections cannot mix ordered and named forms"
    "test_systemverilog_checker_revisions"
    "test_checker_instances"
    "input logic expected = observed"
    "named_instance.okay"
    "ordered_instance.okay"
    "wildcard_instance.okay"
    "value_checker wildcard_instance(.*, .expected())"
    "@constraint-unique"
    "ConstraintExpressionKind::unique_constraint"
    "TemplateKind::Unique"
    "SystemVerilogClassRandomizeSelection::NoProperties"
    "@randomize-null"
    "FSIM-SV-CLASS-022"
    "randomize(null)"
    "unique {left, right, third}"
    "a checker-only call must not perturb the stable stream used by a later randomize assignment"
    "semaphore get count cannot be negative"
    "semaphore put count cannot be negative"
    "put(0)"
    "clocking skew mismatch"
    "FSIM-SV-PARSE-369"
    "FSIM-SV-PARSE-370"
    "must remain isolated to the exact 2023 profile"
    "2023 profile gates must not narrow established 2017 forms"
    "test_2023_artifact_and_cache_round_trip"
    "stale-systemverilog-2023-object"
    "stale-systemverilog-2023-design"
    "systemverilog-2023-artifact-elaborate"
    "fsim.systemverilog-2023-inventory")
  string(TOLOWER
    "${FSIM_SYSTEMVERILOG_2023_INVENTORY_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_TASK_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_BOUNDARIES_CONTENTS}${FSIM_SYSTEMVERILOG_2023_EXECUTION_SHARED_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_FRONTEND_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_APPLICATION_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_IMMEDIATE_ASSERTION_ADVANCED_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_RESOLUTION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_SEMANTIC_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_FRONTEND_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CONCURRENT_ASSERTION_HIR_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_INSTANCE_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_SCOPE_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_RESOLUTION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_FRONTEND_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CHECKER_APPLICATION_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_HIERARCHY_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_CONTROL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_EXPRESSION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_PROCESS_CORE_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_STATE_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSERTION_EXECUTION_SCHEDULER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_PROCESS_STATE_CONTENTS}${FSIM_SYSTEMVERILOG_2023_PROCESS_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_2023_PROCESS_FORK_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_2023_PROCESS_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CONTEXT_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_CAST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_ASSIGNMENT_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_STREAM_CONTROL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_STREAM_EXPRESSION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_OPERATOR_TOKENS_CONTENTS}${FSIM_SYSTEMVERILOG_2023_OPERATOR_LEXER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_OPERATOR_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_OPERATOR_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CALLABLE_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CALLABLE_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CALLABLE_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CALLABLE_PORTABLE_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CALLABLE_SCHEMA_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CALLABLE_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CLOCKING_TIME_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CLOCKING_CONSTANTS_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CLOCKING_ELABORATION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CLOCKING_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_2023_SYNCHRONIZATION_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_CLOCKING_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_PROFILE_CLASS_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_PROFILE_PROCESS_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_PROFILE_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    FSIM_SYSTEMVERILOG_2023_CONTENTS_LOWER)
  string(TOLOWER
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_PARSER_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_RESOLUTION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_INLINE_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_LOWERING_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_MODEL_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_TEMPLATE_RUNTIME_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_CLASS_OBJECTS_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_LLVM_VALIDATION_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_FRONTEND_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_RUNTIME_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_APPLICATION_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_2023_RANDOM_HIR_TEST_CONTENTS}"
    FSIM_SYSTEMVERILOG_2023_RANDOM_CONTENTS_LOWER)
  string(APPEND FSIM_SYSTEMVERILOG_2023_CONTENTS_LOWER
    "${FSIM_SYSTEMVERILOG_2023_RANDOM_CONTENTS_LOWER}")
  string(TOLOWER "${FSIM_SYSTEMVERILOG_2023_POLICY}"
    FSIM_SYSTEMVERILOG_2023_POLICY_LOWER)
  string(FIND "${FSIM_SYSTEMVERILOG_2023_CONTENTS_LOWER}"
    "${FSIM_SYSTEMVERILOG_2023_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog-2023 inventory lost resource policy: ${FSIM_SYSTEMVERILOG_2023_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_POLICY IN ITEMS
    "V19-B183-C02"
    "V19-B183-C03"
    "V19-B183-C04"
    "V19-B183-C19"
    "V19-B184-C01"
    "V19-B184-C02"
    "V19-B184-C03"
    "V19-B184-C04"
    "V19-B184-C05"
    "V19-B184-C06"
    "V19-B184-C07"
    "V19-B184-C08"
    "V19-B184-C09"
    "V19-B184-C10"
    "V19-B184-C11"
    "V19-B184-C12"
    "V19-B184-C13"
    "V19-B184-C14"
    "V19-B184-C19"
    "IEEE1076-2019"
    "profile-identity"
    "view-declarations"
    "conditional-expressions"
    "sequential-blocks"
    "simulator-api"
    "psl-api"
    "report-assert-api"
    "reflection-api"
    "tool-directives"
    "vhpi-information-model"
    "engine-artifact-mixed"
    "rows=37"
    "FSIM_ACTIVE_COUNT"
    "fsim.vhdl-2019-inventory")
  string(TOLOWER
    "${FSIM_VHDL_2019_INVENTORY_CONTENTS}${FSIM_VHDL_2019_CHECKER_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    FSIM_VHDL_2019_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_VHDL_2019_POLICY}"
    FSIM_VHDL_2019_POLICY_LOWER)
  string(FIND "${FSIM_VHDL_2019_CONTENTS_LOWER}"
    "${FSIM_VHDL_2019_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 inventory lost resource policy: ${FSIM_VHDL_2019_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_PREDEFINED_PACKAGE_POLICY IN ITEMS
    "VhdlPackageDependency"
    "kStdPackages"
    "numeric_bit_unsigned"
    "numeric_std_unsigned"
    "math_complex"
    "ieee-1076-standard:"
    ":fsim-v3"
    "governed_package_digest"
    "governed_package_identity"
    "vhdl_package_dependencies"
    "validate_vhdl_package_dependencies"
    "verify_vhdl2019_governed_packages"
    "standard_sources.size() == 18U")
  string(FIND
    "${FSIM_VHDL_2019_PREDEFINED_PACKAGE_MODEL_CONTENTS}${FSIM_VHDL_2019_PREDEFINED_PACKAGE_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_PREDEFINED_PACKAGE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 predefined package governance lost policy: ${FSIM_VHDL_2019_PREDEFINED_PACKAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_VHPI_POLICY IN ITEMS
    "VhdlVhpiCapabilities"
    "VhdlVhpiPropertyKind"
    "VhdlVhpiPropertyResult"
    "VhdlVhpiObjectKind::InterfaceView"
    "VhdlVhpiObjectKind::ViewElement"
    "VhdlVhpiRelationshipKind::Parent"
    "VhdlVhpiObjectError::InvalidProperty"
    "maximum_index_dimensions"
    "maximum_package_dependencies"
    "declaration->interface_view"
    "UnitKind::architecture"
    "property(channel.value.handle"
    "captured.artifact.objects.size() == 5"
    "immutable capability record"
    "zero-or-one snapshot iterator")
  string(FIND
    "${FSIM_VHDL_2019_VHPI_OBJECT_MODEL_CONTENTS}${FSIM_VHDL_2019_VHPI_OBJECT_RUNTIME_CONTENTS}${FSIM_VHDL_2019_VHPI_TYPE_RUNTIME_CONTENTS}${FSIM_VHDL_2019_VHPI_CHECKPOINT_RUNTIME_CONTENTS}${FSIM_VHDL_2019_VHPI_APPLICATION_CONTENTS}${FSIM_VHDL_2019_VHPI_OBJECT_TEST_CONTENTS}${FSIM_VHDL_2019_VHPI_HIERARCHY_TEST_CONTENTS}${FSIM_VHDL_2019_VHPI_CHECKPOINT_TEST_CONTENTS}${FSIM_VHDL_2019_VHPI_APPLICATION_TEST_CONTENTS}${FSIM_VHDL_2019_VHPI_DOCUMENTATION_CONTENTS}"
    "${FSIM_VHDL_2019_VHPI_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 VHPI information model lost policy: ${FSIM_VHDL_2019_VHPI_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_VHPI_RUNTIME_POLICY IN ITEMS
    "FSIM_VHPI_HOST_ABI_VERSION_V3 3u"
    "FSIM_VHPI_SERVICE_PROPERTY = 14"
    "FSIM_VHPI_SERVICE_TOOL = 15"
    "FSIM_VHPI_SERVICE_CAPABILITY = 16"
    "typedef struct fsim_vhpi_capabilities_v3"
    "typedef struct fsim_vhpi_value_v3"
    "typedef struct fsim_vhpi_tool_request_v3"
    "typedef struct fsim_vhpi_host_v3"
    "make_vhdl_vhpi_host_v3"
    "required_host_v3_size"
    "VhdlVhpiCallbackKind::ToolExecution"
    "reference_capabilities"
    "reference_value_access"
    "reference_tool_execution"
    "FSIM_VHPI_LAYOUT(fsim_vhpi_host_v3, 80u, 8u)"
    "all sixteen service")
  string(FIND
    "${FSIM_VHDL_2019_VHPI_ABI_CONTENTS}${FSIM_VHDL_2019_VHPI_PLUGIN_MODEL_CONTENTS}${FSIM_VHDL_2019_VHPI_PLUGIN_RUNTIME_CONTENTS}${FSIM_VHDL_2019_VHPI_CALLBACK_MODEL_CONTENTS}${FSIM_VHDL_2019_VHPI_CALLBACK_RUNTIME_CONTENTS}${FSIM_VHDL_2019_VHPI_CALLBACK_TEST_CONTENTS}${FSIM_VHDL_2019_VHPI_REFERENCE_TEST_CONTENTS}${FSIM_VHDL_2019_VHPI_C_ABI_TEST_CONTENTS}${FSIM_VHDL_2019_VHPI_C_PLUGIN_CONTENTS}${FSIM_VHDL_2019_VHPI_CPP_PLUGIN_CONTENTS}${FSIM_VHDL_2019_FOREIGN_ABI_CHECKER_CONTENTS}${FSIM_VHDL_2019_VHPI_DOCUMENTATION_CONTENTS}"
    "${FSIM_VHDL_2019_VHPI_RUNTIME_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 VHPI runtime ABI lost policy: ${FSIM_VHDL_2019_VHPI_RUNTIME_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_REFLECTION_POLICY IN ITEMS
    "vhdl_reflection_type"
    "VhdlReflectionClass"
    "VhdlReflectionApiKind"
    "convert_generic"
    "access_heap"
    "designated_value"
    "execute_vhdl_reflection_api"
    "lower_vhdl_reflection_expression"
    "enumeration_literal"
    "get_file_logical_name"
    "verify_vhdl_2019_reflection"
    "VhdlReflectionApi>(operation)")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_ASSERT_API_VALIDATION_CONTENTS}${FSIM_VHDL_2019_REFLECTION_PARSER_CONTENTS}${FSIM_VHDL_2019_REFLECTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_REFLECTION_RUNTIME_CONTENTS}${FSIM_VHDL_2019_REFLECTION_CACHE_KEY_CONTENTS}${FSIM_VHDL_2019_REFLECTION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_REFLECTION_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 reflection API lost policy: ${FSIM_VHDL_2019_REFLECTION_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_ASSERT_API_POLICY IN ITEMS
    "VhdlAssertApiKind"
    "std.env.isvhdlassertfailed"
    "std.env.getvhdlassertcount"
    "std.env.clearvhdlassert"
    "std.env.setvhdlassertenable"
    "std.env.getvhdlassertenable"
    "std.env.setvhdlassertformat"
    "std.env.getvhdlassertformat"
    "std.env.setvhdlreadseverity"
    "std.env.getvhdlreadseverity"
    "vhdl_assert_counts"
    "vhdl_assert_enabled"
    "vhdl_assert_formats"
    "vhdl_read_severity"
    "record_read_failure"
    "verify_vhdl2019_assert_api"
    "VhdlAssertApi>(operation)")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TYPE_INFERENCE_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS}${FSIM_VHDL_2019_ASSERT_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_ASSERT_API_TEXTIO_CONTENTS}${FSIM_VHDL_2019_ASSERT_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_ASSERT_API_VALIDATION_CONTENTS}"
    "${FSIM_VHDL_2019_ASSERT_API_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 report/assert API lost policy: ${FSIM_VHDL_2019_ASSERT_API_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_SOURCE_LOCATION_API_POLICY IN ITEMS
    "VhdlEnvironmentGetCallPath"
    "VhdlEnvironmentCallPath"
    "std.env.call_path_element"
    "std.env.call_path_vector"
    "std.env.call_path_vector_ptr"
    "std.env.get_call_path"
    "std.env.file_name"
    "std.env.file_path"
    "std.env.file_line"
    "source_value"
    "source_index"
    "collect_vhdl_call_path"
    "dynamic_call_stack"
    "STD.ENV TO_STRING call-path index is out of range"
    "saved_path.all(0).file_name.all"
    "saved_path.all(0).file_line > 0"
    "VhdlEnvironmentGetCallPath>(operation)"
    "VhdlEnvironmentCallPath>(operation)")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TYPE_INFERENCE_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_SOURCE_LOCATION_API_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 source-location API lost policy: ${FSIM_VHDL_2019_SOURCE_LOCATION_API_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_PSL_API_POLICY IN ITEMS
    "VhdlPslApiKind"
    "std.env.pslassertfailed"
    "std.env.psliscovered"
    "std.env.getpslcoverassert"
    "std.env.pslisassertcovered"
    "std.env.setpslcoverassert"
    "std.env.clearpslstate"
    "set_vhdl_psl_api_hook"
    "cover_assert_ever_enabled"
    "engine.reset()"
    "verify_vhdl2019_psl_api"
    "first edge coverage"
    "second edge assertion failure"
    "cleared assert-covered state"
    "VhdlPslApi>(operation)")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_PSL_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_PSL_API_INTERPRETER_MODEL_CONTENTS}${FSIM_VHDL_2019_PSL_API_INTERPRETER_SETUP_CONTENTS}${FSIM_VHDL_2019_PSL_API_INTERPRETER_CONTENTS}${FSIM_VHDL_2019_PSL_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_PSL_API_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 PSL API lost policy: ${FSIM_VHDL_2019_PSL_API_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_COVERAGE_POLICY IN ITEMS
    "enqueue_block_callables"
    "VhdlStandard::Vhdl2019"
    "block-local callable and block-body points"
    "source_identity.high"
    "source_identity.low"
    "remapped_bindings"
    "changed_attempt_ten->bin_identity"
    "psl_api_interpreter == psl_api_debug"
    "psl_api_interpreter == psl_api_o2_warm")
  string(FIND
    "${FSIM_VHDL_2019_COVERAGE_POINTS_CONTENTS}${FSIM_VHDL_2019_COVERAGE_POINTS_TEST_CONTENTS}${FSIM_VHDL_2019_PSL_COVERAGE_DATABASE_CONTENTS}${FSIM_VHDL_2019_PSL_COVERAGE_DATABASE_TEST_CONTENTS}${FSIM_VHDL_2019_PSL_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_COVERAGE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 code/PSL coverage integration lost policy: ${FSIM_VHDL_2019_COVERAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_INTEGRATION_POLICY IN ITEMS
    "vhdl_sources.standard = \"2019\""
    "FSIM-VHDL-2019-MIXED-PASS"
    "fsim::app::SimulationEngine::debug"
    "compare_capture(reference, debug)"
    "vhdl_2019_object_text.c_str()"
    "vhdl_design_input->standard == \"2019\""
    "const auto debug = run_engine(app::SimulationEngine::debug)"
    "compiled_warm == debug"
    "active_design = relocated_design"
    "active_design_text.c_str(), \"--engine\","
    "\"compiled\", \"--trace\", trace_text.c_str()")
  string(FIND
    "${FSIM_VHDL_2019_TYPED_BOUNDARY_TEST_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_PHASE_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_INTEGRATION_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 engine/artifact/mixed integration lost policy: ${FSIM_VHDL_2019_INTEGRATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_SIMULATOR_API_POLICY IN ITEMS
    "VhdlSimulatorApi"
    "std.env.stop"
    "std.env.finish"
    "std.env.resolution_limit"
    "validate_vhdl_simulator_api"
    "FSIM-FE-VHENV-002"
    "output.procedure = name"
    "lower_vhdl_simulator_procedure_call"
    "lower_vhdl_simulator_function_expression"
    "std::optional<RegisterId> status"
    "capture_simulator_status"
    "synchronize_uses_to_frame"
    "FSIM-RUN-VHENV-001"
    "verify_vhdl2019_simulator_api"
    "capture->paused.simulator_status == 3"
    "capture->finished.simulator_status == 7")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_HIR_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_APPLICATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_SIMULATOR_API_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 simulator API lost policy: ${FSIM_VHDL_2019_SIMULATOR_API_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_DIRECTORY_API_POLICY IN ITEMS
    "VhdlEnvironmentDirectoryKind"
    "VhdlEnvironmentDirectory"
    "std.env.directory_items"
    "std.env.directory"
    "std.env.dir_open_status"
    "std.env.dir_create_status"
    "std.env.dir_delete_status"
    "std.env.file_delete_status"
    "std.env.dir_open"
    "std.env.dir_close"
    "std.env.dir_itemexists"
    "std.env.dir_itemisdir"
    "std.env.dir_itemisfile"
    "std.env.dir_workingdir"
    "std.env.dir_createdir"
    "std.env.dir_deletedir"
    "std.env.dir_deletefile"
    "std.env.dir_separator"
    "vhdl_directory_item_limit = 4096U"
    "vhdl_directory_text_limit = 1024U * 1024U"
    "vhdl_path_below_root"
    "std::sort(items.begin(), items.end())"
    "FSIM-ELAB-VHENV-006"
    "VhdlEnvironmentDirectory>(operation)"
    "serialize_runtime_state"
    "status_access_denied"
    "status_not_empty"
    "status_no_file"
    "FSIM-FE-VHSTD-003")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PROCEDURE_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_DIRECTORY_API_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 directory API lost policy: ${FSIM_VHDL_2019_DIRECTORY_API_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_ENVIRONMENT_API_POLICY IN ITEMS
    "VhdlEnvironmentGetenv"
    "std.env.getenv"
    "std.env.vhdl_version"
    "std.env.tool_type"
    "std.env.tool_vendor"
    "std.env.tool_name"
    "std.env.tool_edition"
    "std.env.tool_version"
    "vhdl_environment_getenv_call"
    "fsim::support::environment_variable(name)"
    "STD.ENV GETENV result exceeds the bounded string limit"
    "VhdlEnvironmentGetenv>(operation)"
    "verify_vhdl2019_environment_api"
    "FSIM_VHDL2019_ENVIRONMENT_API_MISSING"
    "std::string(4097U, 'x')")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_ENVIRONMENT_API_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 environment API lost policy: ${FSIM_VHDL_2019_ENVIRONMENT_API_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_DATA_TIME_POLICY IN ITEMS
    "VhdlEnvironmentTimeKind"
    "vhdl_environment_time_record_type"
    "microsecond"
    "dayofyear"
    "vhdl_real_payload"
    "std.env.localtime"
    "std.env.gmtime"
    "std.env.epoch"
    "std.env.time_to_seconds"
    "std.env.seconds_to_time"
    "std.env.to_string"
    "@builtin:time"
    "localtime_s"
    "localtime_r"
    "utc_epoch_from_calendar"
    "TIME_RECORD is not a representable local calendar time"
    "supports_wide_register_operation"
    "VhdlEnvironmentTimeToString>(operation)"
    "serialize_runtime_state"
    "serialize_vhdl_hir_state"
    "cold.native_cache.misses == 1"
    "warm.native_cache.hits == 1")
  string(FIND
    "${FSIM_VHDL_2019_SIMULATOR_API_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_REAL_PARSER_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_HIR_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_FUNCTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_STRING_LOWERING_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TYPE_INFERENCE_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_INTEGER_INFERENCE_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TIME_NORMALIZATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_MODEL_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_RUNTIME_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_JIT_VALIDATION_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_EXECUTOR_CONTENTS}${FSIM_VHDL_2019_SIMULATOR_API_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_DATA_TIME_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 data/time API lost policy: ${FSIM_VHDL_2019_DATA_TIME_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_UNSPECIFIED_POLICY IN ITEMS
    "VhdlUnspecifiedTypeClass"
    "parse_vhdl_unspecified_type"
    "vhdl_unspecified_type_accepts"
    "adapt_vhdl_unspecified_port_types"
    "unspecified_type_inference_unique"
    "FSIM-VHDL-PARSE-286"
    "FSIM-ELAB-VHUNSPEC-001"
    "FSIM-ELAB-VHUNSPEC-002"
    "VHDL-2019 unspecified type categories must parse"
    "invalid_unspecified_type_generic")
  string(FIND
    "${FSIM_VHDL_2019_UNSPECIFIED_PARSER_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_MODEL_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_HIR_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_EXECUTABLE_HIR_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_RESOLUTION_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_PORT_INFERENCE_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_LOWERING_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_GENERIC_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_UNSPECIFIED_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_UNSPECIFIED_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 unspecified types lost policy: ${FSIM_VHDL_2019_UNSPECIFIED_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_INTEGER_POLICY IN ITEMS
    "vhdl_predefined_integer_storage_width"
    "vhdl_predefined_integer_range"
    "vhdl_integer_storage_width"
    "std::numeric_limits<std::int64_t>::min()"
    "width != 32 && width != 64"
    "sadd_with_overflow"
    "smul_with_overflow"
    "VHDL-2019 accepts the complete required signed 64-bit INTEGER range"
    "every older VHDL profile retains its portable 32-bit INTEGER range"
    "VHDL-2019 integer application test passed"
    "6000000001")
  string(FIND
    "${FSIM_VHDL_2019_INTEGER_MODEL_CONTENTS}${FSIM_VHDL_2019_INTEGER_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_2019_INTEGER_PARSER_CONTENTS}${FSIM_VHDL_2019_INTEGER_ELABORATION_CONTENTS}${FSIM_VHDL_2019_INTEGER_SIMIR_CONTENTS}${FSIM_VHDL_2019_INTEGER_RUNTIME_CONTENTS}${FSIM_VHDL_2019_INTEGER_LLVM_CONTENTS}${FSIM_VHDL_2019_INTEGER_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_INTEGER_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_INTEGER_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 predefined INTEGER lost policy: ${FSIM_VHDL_2019_INTEGER_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_MODE_VIEW_POLICY IN ITEMS
    "VhdlModeViewElementKind"
    "TypeDeclarationKind::VhdlModeView"
    "parse_vhdl_mode_view_declaration"
    "DeclarationForm::mode_view"
    "ModeViewProfile"
    "FSIM-VHDL-PARSE-287"
    "FSIM-VHDL-SEM-107"
    "FSIM-VHDL-SEM-108"
    "Mode view declarations remain declarations rather than types"
    "test_vhdl_2019_mode_view_declarations")
  string(FIND
    "${FSIM_VHDL_2019_MODE_VIEW_MODEL_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_PARSER_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_HIR_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_RESOLUTION_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_MODE_VIEW_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 mode view declarations lost policy: ${FSIM_VHDL_2019_MODE_VIEW_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_MODE_VIEW_COMPOSITION_POLICY IN ITEMS
    "ModeViewCompositionState"
    "compose_vhdl_mode_views"
    "terminal_type"
    "element.subtype = member->subtype"
    "element.elements ="
    "ModeViewElementForm::record_view"
    "ModeViewElementForm::array_view"
    "ModeViewCompositionState::recursive"
    "declared_subtype.vhdl_type_declaration.clear()"
    "pair_left.subtype->type_mark.spelling == \"lane_subtype_t\""
    "nested_pair.elements.size() == 2"
    "nested_array.elements.size() == 3")
  string(FIND
    "${FSIM_VHDL_2019_MODE_VIEW_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_HIR_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_COMPOSITION_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_MODE_VIEW_COMPOSITION_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 nested mode view composition lost policy: ${FSIM_VHDL_2019_MODE_VIEW_COMPOSITION_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_VIEW_PORT_POLICY IN ITEMS
    "VhdlModeViewIndication"
    "parse_vhdl_mode_view_indication"
    "attach_interface_view"
    "validate_vhdl_mode_view_interfaces"
    "FSIM-VHDL-PARSE-288"
    "FSIM-VHDL-SEM-109"
    "FSIM-ELAB-VHVIEW-001"
    "mode_view_profile_matches"
    "VhdlModeViewBinding"
    "vhdl_mode_view_bindings"
    "view_port_top.child.channel.request"
    "materialize_vhdl_mode_view_endpoints"
    "maximum_vhdl_mode_view_endpoints"
    "FSIM-ELAB-VHVIEW-006"
    "nested_view_top.child.channel"
    "endpoint.lsb_offset == expected_offsets[index]")
  string(FIND
    "${FSIM_VHDL_2019_MODE_VIEW_MODEL_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_HIR_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_RESOLUTION_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_PARSER_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_COMPONENT_PARSER_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_SEMANTIC_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_ELABORATION_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_COMPONENT_BINDING_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_MODEL_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_VIEW_PORT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 view-based interfaces lost policy: ${FSIM_VHDL_2019_VIEW_PORT_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_VIEW_EXECUTION_POLICY IN ITEMS
    "validate_vhdl_mode_view_write"
    "FSIM-ELAB-VHVIEW-007"
    "endpoint.direction == frontend::PortDirection::Input"
    "!info.vhdl_mode_view_bindings.empty()"
    "staged.request := staged.response"
    "view_reference.value == \"11\""
    "view_cold.compiled_processes == 2")
  string(FIND
    "${FSIM_VHDL_2019_VIEW_PORT_ELABORATION_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_TEST_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_APPLICATION_TEST_CONTENTS}${FSIM_VHDL_2019_VIEW_EXECUTION_CONTENTS}${FSIM_VHDL_2019_VIEW_EXECUTION_LOWERING_CONTENTS}${FSIM_VHDL_2019_VIEW_EXECUTION_DRIVERS_CONTENTS}"
    "${FSIM_VHDL_2019_VIEW_EXECUTION_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 mode-view execution lost policy: ${FSIM_VHDL_2019_VIEW_EXECUTION_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_COMPLEX_VIEW_POLICY IN ITEMS
    "compatible("
    "resolved_subtype"
    "record->record_elements.size() != profile.elements.size()"
    "validate_vhdl_mode_view_hir"
    "FSIM-VHDL-SEM-110"
    "FSIM-VHDL-SEM-111"
    "compatible_vhdl_types"
    "ModeViewMapState"
    "FSIM-ELAB-VHVIEW-003"
    "FSIM-ELAB-VHVIEW-004"
    "wrong_record_view"
    "wrong_array_view"
    "semantic_declaration_errors == 3")
  string(FIND
    "${FSIM_VHDL_2019_MODE_VIEW_COMPOSITION_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_ANALYSIS_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_RESOLUTION_CONTENTS}${FSIM_VHDL_2019_MODE_VIEW_APPLICATION_TEST_CONTENTS}${FSIM_VHDL_2019_VIEW_PORT_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_COMPLEX_VIEW_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 complex view legality lost policy: ${FSIM_VHDL_2019_COMPLEX_VIEW_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_PROTECTED_POLICY IN ITEMS
    "a generic interface on a protected type"
    "an access, file, or protected parameter on a protected method"
    "a protected method alias"
    "an explicitly private protected member"
    "generic_parameters"
    "method_aliases"
    "vhdl_private"
    "output.alias_target"
    "public_info.variables.insert"
    "private becomes reserved only in the VHDL-2019 lexical profile")
  string(FIND
    "${FSIM_VHDL_2019_PROTECTED_PARSER_CONTENTS}${FSIM_VHDL_2019_PROTECTED_MODEL_CONTENTS}${FSIM_VHDL_2019_PROTECTED_CORE_MODEL_CONTENTS}${FSIM_VHDL_2019_PROTECTED_HIR_CONTENTS}${FSIM_VHDL_2019_PROTECTED_MERGE_CONTENTS}${FSIM_VHDL_2019_PROTECTED_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_PROTECTED_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 protected type updates lost policy: ${FSIM_VHDL_2019_PROTECTED_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_CONDITIONAL_POLICY IN ITEMS
    "analyze_vhdl_conditionals"
    "max_conditional_depth = 128U"
    "FSIM-VHDL-CA-001"
    "FSIM-VHDL-CA-002"
    "FSIM-VHDL-CA-003"
    "FSIM-VHDL-CA-004"
    "FSIM-VHDL-CA-005"
    "FSIM-VHDL-CA-006"
    "FSIM-VHDL-PROTECT-001"
    "FSIM-VHDL-PROTECT-002"
    "FSIM-VHDL-PROTECT-003"
    "FSIM-VHDL-PROTECT-004"
    "directive_string"
    "split_protection_word"
    "a conditional analysis closing directive must be `end or `end if"
    "conditional warning/error directives honor branch selection"
    "VHDL-2008 plaintext protect envelopes retain source"
    "encrypted protect envelopes are contained and rejected once"
    "protect tool directives remain isolated from pre-2008 profiles"
    "nested, unmatched, and unterminated protection controls retain"
    "diagnostic.code == \"FSIM-VHDL-PROTECT-001\""
    "tool_type\", \"SIMULATION"
    "conditional identifier names are case-insensitive while their string "
    "values remain case-sensitive"
    "directive-like text inside block comments is ignored and ordinary "
    "conditional analysis rejects excess nesting without losing group "
    "synchronization or growing its frame stack past the bound")
  string(FIND
    "${FSIM_VHDL_2019_CONDITIONAL_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_INTERNAL_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_DRIVER_CONTENTS}${FSIM_VHDL_2019_FRONTEND_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_CONDITIONAL_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 conditional analysis lost policy: ${FSIM_VHDL_2019_CONDITIONAL_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_POLICY IN ITEMS
    "ExpressionKind::Conditional"
    "vh::ExpressionKind::conditional"
    "lower_vhdl_conditional_expression"
    "a first-class conditional expression requires VHDL-2019"
    "FSIM-ELAB-VHCOND-001"
    "FSIM-ELAB-VHCOND-002"
    "FSIM-ELAB-VHCOND-003"
    "vhdl_expression_matches_type(expression.operands[1], formal)"
    "first-class conditional expressions stay isolated from VHDL-2008"
    "selected(7) when choose else selected(9)"
    "1 / divisor"
    "compiled_process_count() > 0U")
  string(FIND
    "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_MODEL_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_PARSER_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_EXECUTABLE_HIR_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_LOWERING_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_TYPES_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_ELABORATION_TEST_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 conditional expressions lost policy: ${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_RESULT_SUBTYPE_POLICY IN ITEMS
    "vhdl_return_identifier"
    "FSIM-VHDL-SEM-112"
    "FSIM-ELAB-VHRESULT-001"
    "FSIM-ELAB-VHRESULT-002"
    ":function-specialization:"
    "@callable-"
    "result_t'length"
    "narrow <= fill('1')"
    "wide <= fill('0')")
  string(FIND
    "${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_MODEL_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_HIR_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_EXECUTABLE_HIR_CONTENTS}${FSIM_VHDL_2019_RESULT_SUBTYPE_PARSER_CONTENTS}${FSIM_VHDL_2019_RESULT_SUBTYPE_LOWERING_CONTENTS}${FSIM_VHDL_2019_RESULT_SUBTYPE_STORAGE_CONTENTS}${FSIM_VHDL_2019_RESULT_SUBTYPE_VARIABLES_CONTENTS}${FSIM_VHDL_2019_RESULT_SUBTYPE_ELABORATION_TEST_CONTENTS}${FSIM_VHDL_2019_CONDITIONAL_EXPRESSION_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_RESULT_SUBTYPE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 result-subtype specialization lost policy: ${FSIM_VHDL_2019_RESULT_SUBTYPE_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_SEQUENTIAL_BLOCK_POLICY IN ITEMS
    "parse_sequential_block_declarations"
    "sequential block statements"
    "FSIM-VHDL-PARSE-289"
    "FSIM-VHDL-PARSE-293"
    "FSIM-VHDL-UNSUPPORTED-056"
    "support::RareVector<FunctionDeclaration> functions"
    "input.kind == frontend::StatementKind::Block"
    "add_statement_regions"
    "missing VHDL sequential block scope"
    "void Lowerer::lower_block"
    "route_returns_through_cleanup"
    "function_returns_begin"
    "procedure_returns_begin"
    "outer.constants.size() == 1U"
    "outer->declarations.size() == 3U"
    "verify_sequential_block_runtime"
    "reference.values == expected"
    "sequential block exception propagated"
    "point->scope.find(\".failing\")"
    "error.source().path.ends_with"
    "simulation.compiled_process_count() > 0"
    "vhdl2008_sequential_block")
  string(FIND
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_MODEL_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_PARSER_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_HIR_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_EXECUTABLE_HIR_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_LOWERING_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_APPLICATION_TEST_CONTENTS}${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_RUNTIME_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 sequential blocks lost policy: ${FSIM_VHDL_2019_SEQUENTIAL_BLOCK_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_ATTRIBUTE_POLICY IN ITEMS
    "VhdlPredefinedSubtypeAttribute"
    "PredefinedAttribute::index"
    "PredefinedAttribute::designated_subtype"
    "PredefinedAttribute::reflect"
    "vhdl_mode_view_converse_of"
    "converse_direction"
    "visible_type"
    "FSIM-VHDL-SEM-113"
    "FSIM-ELAB-VHATTR-009"
    "FSIM-ELAB-VHATTR-010"
    "FSIM-ELAB-VHATTR-011"
    "object_shorthand"
    "invalid_predefined_attribute_top"
    "consumer->mode_view->converse_of")
  string(FIND
    "${FSIM_VHDL_2019_ATTRIBUTE_MODEL_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_TYPE_PARSER_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_EXPRESSION_PARSER_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_ALIAS_PARSER_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_HIR_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_HIR_TYPES_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_MODE_VIEW_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_TYPE_RESOLUTION_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_TYPE_INFERENCE_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_LOWERING_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_CODEC_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_ATTRIBUTE_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_ATTRIBUTE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 predefined attributes lost policy: ${FSIM_VHDL_2019_ATTRIBUTE_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_OVERLOAD_POLICY IN ITEMS
    "vhdl_base_type_profiles_match"
    "vhdl_parameter_type_profiles_match"
    "vhdl_subtype_indications_conform"
    "std::ranges::stable_sort"
    "retain_pre_mapping_homograph_classes"
    "vhdl-mapped-callable-v1"
    "FSIM-ELAB-VHOVER-003"
    "FSIM-ELAB-VHOVER-006"
    "vhdl-2019-unspecified-homographs.vhd"
    "vhdl-2019-unspecified-body-conformance.vhd"
    "vhdl-2019-mapped-homographs.vhd"
    "first.declarations == second.declarations")
  string(FIND
    "${FSIM_VHDL_2019_OVERLOAD_MODEL_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_COMMON_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_FUNCTIONS_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_PROCEDURES_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_RESOLUTION_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_PACKAGES_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_PROTECTED_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_GENERIC_SUBPROGRAMS_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_TYPE_SPECIALIZATION_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_HIR_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_TEST_CONTENTS}${FSIM_VHDL_2019_OVERLOAD_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_OVERLOAD_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 overload and conformance rules lost policy: ${FSIM_VHDL_2019_OVERLOAD_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_PROFILE_POLICY IN ITEMS
    "vhdl_profile_compatible"
    "conditional_profile_compatible"
    "possible_object_shorthand"
    "validate_vhdl_profile_compatibility"
    "FSIM-ELAB-VHPROFILE-001"
    "object-attribute-shorthand"
    "scalar_length_source"
    "State_T'pos(State_T'high)")
  string(FIND
    "${FSIM_VHDL_2019_PROFILE_MODEL_CONTENTS}${FSIM_VHDL_2019_PROFILE_PARSER_CONTENTS}${FSIM_VHDL_2019_PROFILE_CONDITIONAL_CONTENTS}${FSIM_VHDL_2019_PROFILE_ATTRIBUTES_CONTENTS}${FSIM_VHDL_2019_PROFILE_APPLICATION_CONTENTS}${FSIM_VHDL_2019_PROFILE_APPLICATION_CHECK_CONTENTS}${FSIM_VHDL_2019_PROFILE_HIR_CONTENTS}${FSIM_VHDL_2019_PROFILE_ELABORATION_CONTENTS}${FSIM_VHDL_2019_PROFILE_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_PROFILE_ELABORATION_TEST_CONTENTS}${FSIM_VHDL_2019_PROFILE_LEGACY_APPLICATION_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_PROFILE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 older-profile isolation lost policy: ${FSIM_VHDL_2019_PROFILE_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_FRONTEND_CORPUS_POLICY IN ITEMS
    "test_vhdl_2019_frontend_recovery_corpus"
    "protected-members"
    "unspecified-type"
    "mode-view"
    "view-interface"
    "conditional-expression"
    "result-subtype-identifier"
    "sequential-block"
    "subtype-attribute"
    "converse-view"
    "reflection"
    "conditional-analysis"
    "parsed.design.vhdl_profile_compatible"
    "resume at the following independent design unit")
  string(FIND
    "${FSIM_VHDL_2019_PROFILE_FRONTEND_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_FRONTEND_CORPUS_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 frontend corpus lost a recovery or profile owner: ${FSIM_VHDL_2019_FRONTEND_CORPUS_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_ARTIFACT_POLICY IN ITEMS
    "kPortableSchemaVersion = 14"
    "kOwningUnitSchemaVersion = 31"
    "kRuntimeStateSchema = 61"
    "kVhdlHirStateSchema = 4"
    "value.vhdl_predefined_subtype_attribute"
    "value.default_value, value.vhdl_mode_view"
    "DeclarationForm::mode_view"
    "ModeViewCompositionState::recursive"
    "vhdl-2019-round-trip"
    "vhdl-2019-hir.bin"
    "vhdl_mode_view_bindings"
    "portable-unit schema 14")
  string(FIND
    "${FSIM_VHDL_2019_ARTIFACT_LIBRARY_HEADER_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_PORTABLE_HEADER_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_DESIGN_HEADER_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_PORTABLE_CODEC_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_DESIGN_CODEC_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_PORTABLE_TEST_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_DESIGN_TEST_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_PORTABLE_CONTRACT_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_NESTED_CONTRACT_CONTENTS}${FSIM_VHDL_2019_ARTIFACT_STALE_CONTRACT_CONTENTS}"
    "${FSIM_VHDL_2019_ARTIFACT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 object/design round trip lost policy: ${FSIM_VHDL_2019_ARTIFACT_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_ACCESS_POLICY IN ITEMS
    "deallocate_releases_storage"
    "reclaim_when_unreachable"
    "vhdl_standard_ >= VhdlStandard::Vhdl2019"
    "access.simulation_lifetime = false"
    "if (type->vhdl_access->deallocate_releases_storage)"
    "emit_vhdl_access_reclamation"
    "emit_vhdl_access_scope_cleanup"
    "std::ranges::sort(roots)"
    "DeleteContainer"
    "Alias_Value <= Saved.all"
    "Saved := null"
    "maximum_objects = 1"
    "!type.deallocate_releases_storage"
    "info.vhdl_access->deallocate_releases_storage"
    "!info.vhdl_access->reclaim_when_unreachable")
  string(FIND
    "${FSIM_VHDL_2019_ACCESS_MODEL_CONTENTS}${FSIM_VHDL_2019_ACCESS_HIR_MODEL_CONTENTS}${FSIM_VHDL_2019_ACCESS_PARSER_CONTENTS}${FSIM_VHDL_2019_ACCESS_HIR_CONTENTS}${FSIM_VHDL_2019_ACCESS_SPECIALIZATION_CONTENTS}${FSIM_VHDL_2019_ACCESS_LOWERING_CONTENTS}${FSIM_VHDL_2019_ACCESS_ASSIGNMENT_CONTENTS}${FSIM_VHDL_2019_ACCESS_PROCESS_CONTENTS}${FSIM_VHDL_2019_ACCESS_FUNCTIONS_CONTENTS}${FSIM_VHDL_2019_ACCESS_PROCEDURES_CONTENTS}${FSIM_VHDL_2019_ACCESS_TEST_CONTENTS}${FSIM_VHDL_LEGACY_ACCESS_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_ACCESS_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 access lifetime lost policy: ${FSIM_VHDL_2019_ACCESS_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_2019_IDENTITY_POLICY IN ITEMS
    "Vhdl2019"
    "vhdl_2019"
    "vhdl-2019"
    "19/2019"
    "value == frontend::StandardRevision::Vhdl2019"
    "vhdl_2019_metadata->standard == \"2019\""
    "vhdl_2008_identity->cache_key != vhdl_2019_identity->cache_key"
    "vhdl_2008_identity->specialization_cache_keys"
    "VHDL-2019 inherits the complete retained VHDL-2008 lexical surface"
    "config.source_sets.front().standard == \"2019\"")
  string(FIND
    "${FSIM_VHDL_2019_FRONTEND_IDENTITY_CONTENTS}${FSIM_VHDL_2019_PROJECT_IDENTITY_CONTENTS}${FSIM_VHDL_2019_PROJECT_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_2019_ANALYSIS_IDENTITY_CONTENTS}${FSIM_VHDL_2019_CLI_IDENTITY_CONTENTS}${FSIM_VHDL_2019_PORTABLE_IDENTITY_CONTENTS}${FSIM_VHDL_2019_FRONTEND_TEST_CONTENTS}${FSIM_VHDL_2019_PROJECT_TEST_CONTENTS}${FSIM_VHDL_2019_APPLICATION_TEST_CONTENTS}${FSIM_VHDL_2019_CLI_TEST_CONTENTS}"
    "${FSIM_VHDL_2019_IDENTITY_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL-2019 profile identity lost policy: ${FSIM_VHDL_2019_IDENTITY_POLICY}")
  endif()
endforeach()

foreach(FSIM_LEGACY_TF_POLICY IN ITEMS
    "LEGTF-C02"
    "LEGTF-C19"
    "native-plugin-abi"
    "veriuser-header"
    "platform-link-surfaces"
    "registration-table-discovery"
    "descriptor-validation"
    "task-function-callbacks"
    "misctf-lifecycle"
    "argument-inspection"
    "value-access"
    "parameter-instance-access"
    "time-delay-timescale"
    "scope-workarea-userdata"
    "output-control"
    "synchronization-callbacks"
    "hdl-system-registration"
    "scheduler-coordination"
    "failure-containment"
    "cross-platform-plugins"
    "ieee-only-no-vendor-extensions"
    "set(FSIM_COMPLETED_CHANGE 19)"
    "fsim.legacy-tf-inventory")
  string(TOLOWER
    "${FSIM_LEGACY_TF_INVENTORY_CONTENTS}${FSIM_LEGACY_TF_CHECKER_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    FSIM_LEGACY_TF_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_LEGACY_TF_POLICY}" FSIM_LEGACY_TF_POLICY_LOWER)
  string(FIND "${FSIM_LEGACY_TF_CONTENTS_LOWER}"
    "${FSIM_LEGACY_TF_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "legacy TF inventory lost resource policy: ${FSIM_LEGACY_TF_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_SCHEDULER_POLICY IN ITEMS
    "kMaxTfSchedulerCalls"
    "kMaxTfSchedulerCallbacks"
    "TfSchedulerCoordinator"
    "TfSchedulerPublication"
    "TfSchedulerCallbackKind::Reactivate"
    "TfSchedulerCallbackKind::ReadWriteSynchronize"
    "TfSchedulerCallbackKind::ReadOnlySynchronize"
    "SchedulerPhase::reactive"
    "SchedulerPhase::postponed"
    "schedule_after_cancelable"
    "stable_order_base"
    "TfSchedulerError::InactiveScheduler"
    "TfSchedulerError::Publication"
    "request_stop"
    "reason_reactivate"
    "plugin_coordinator.bind"
    "FSIM_TF_LINK_PROBE_PLUGIN_PATH"
    "fsim.application.tf-scheduler")
  string(TOLOWER
    "${FSIM_TF_SCHEDULER_MODEL_CONTENTS}${FSIM_TF_SCHEDULER_IMPLEMENTATION_CONTENTS}${FSIM_TF_SCHEDULER_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    FSIM_TF_SCHEDULER_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_SCHEDULER_POLICY}"
    FSIM_TF_SCHEDULER_POLICY_LOWER)
  string(FIND "${FSIM_TF_SCHEDULER_CONTENTS_LOWER}"
    "${FSIM_TF_SCHEDULER_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF scheduler coordinator lost resource policy: ${FSIM_TF_SCHEDULER_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_CONTAINMENT_POLICY IN ITEMS
    "TfNativePointerAccess"
    "TfContainmentError"
    "validate_tf_native_pointer"
    "validate_tf_callback_pointer"
    "VirtualQuery"
    "/proc/self/maps"
    "TfPluginError::InvalidPointer"
    "TfRegistrationError::Pointer"
    "TfCallError::InvalidPointer"
    "TfCallError::CallbackException"
    "TfCallError::ContextBusy"
    "fsim_tf_call_context_enter_v3"
    "FSIM_TF_LINK_PROBE_PLUGIN_PATH"
    "loaded.value.reset()"
    "fsim.runtime.tf_containment")
  string(TOLOWER
    "${FSIM_TF_CONTAINMENT_MODEL_CONTENTS}${FSIM_TF_CONTAINMENT_IMPLEMENTATION_CONTENTS}${FSIM_TF_CONTAINMENT_TEST_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_TF_REGISTRATION_IMPLEMENTATION_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_CONTAINMENT_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_CONTAINMENT_POLICY}"
    FSIM_TF_CONTAINMENT_POLICY_LOWER)
  string(FIND "${FSIM_TF_CONTAINMENT_CONTENTS_LOWER}"
    "${FSIM_TF_CONTAINMENT_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF failure containment lost resource policy: ${FSIM_TF_CONTAINMENT_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_CROSS_PLATFORM_POLICY IN ITEMS
    "LINKER_LANGUAGE CXX"
    "c_std_11"
    "cxx_std_20"
    "fsim_tf_link_probe_plugin"
    "fsim_tf_cpp_probe_plugin"
    "fsim_native_plugin_descriptor_v3_get"
    "FSIM_TF_C_PLUGIN_PATH"
    "FSIM_TF_CPP_PLUGIN_PATH"
    "tf_plugin_artifact_loaded"
    "bound calls retain both native images"
    "both native images unload"
    "windows-llvm-mingw"
    "fsim.runtime.tf_cross_platform_plugins")
  string(TOLOWER
    "${FSIM_TF_LINK_CMAKE_CONTENTS}${FSIM_TF_LINK_PLUGIN_CONTENTS}${FSIM_TF_CPP_PLUGIN_CONTENTS}${FSIM_TF_CROSS_PLATFORM_TEST_CONTENTS}${FSIM_TF_PLUGIN_MODEL_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}${FSIM_WORKFLOW_CONTENTS}"
    FSIM_TF_CROSS_PLATFORM_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_CROSS_PLATFORM_POLICY}"
    FSIM_TF_CROSS_PLATFORM_POLICY_LOWER)
  string(FIND "${FSIM_TF_CROSS_PLATFORM_CONTENTS_LOWER}"
    "${FSIM_TF_CROSS_PLATFORM_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF cross-platform plug-in proof lost resource policy: ${FSIM_TF_CROSS_PLATFORM_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_TIME_POLICY IN ITEMS
    "kMaxTfDelayRequests"
    "TfTimeProfile"
    "TfTimeState"
    "TfTimeError"
    "validate_tf_time_profile"
    "tf_time_to_local_integer"
    "tf_local_integer_to_ticks"
    "tf_local_real_to_ticks"
    "fsim_tf_time_bridge_v3"
    "TfCallError::InvalidTime"
    "time_profile"
    "delay_requests"
    "tf_getlongtime"
    "tf_getnextlongtime"
    "tf_getrealtime"
    "tf_gettimeprecision"
    "tf_gettimeunit"
    "tf_setdelay"
    "tf_setlongdelay"
    "tf_setrealdelay"
    "tf_scale_longdelay"
    "tf_unscale_longdelay"
    "thrown.delay_requests.empty()"
    "fsim.runtime.tf_time")
  string(TOLOWER
    "${FSIM_VERIUSER_HEADER_CONTENTS}${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_TIME_MODEL_CONTENTS}${FSIM_TF_TIME_IMPLEMENTATION_CONTENTS}${FSIM_TF_TIME_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_TIME_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_TIME_POLICY}" FSIM_TF_TIME_POLICY_LOWER)
  string(FIND "${FSIM_TF_TIME_CONTENTS_LOWER}"
    "${FSIM_TF_TIME_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF time, delay, or timescale access lost resource policy: ${FSIM_TF_TIME_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_CONTEXT_POLICY IN ITEMS
    "kMaxTfContextNameSize"
    "TfContextProfile"
    "TfContextError"
    "validate_and_copy_tf_context"
    "scope_belongs_to_module"
    "TfCallError::InvalidContext"
    "context_profile"
    "std::recursive_mutex"
    "module_instance_name"
    "scope_name"
    "routine_name"
    "work_area"
    "registration.user_data"
    "tf_mipname"
    "tf_spname"
    "tf_getroutine"
    "tf_getworkarea"
    "tf_setworkarea"
    "tf_imipname"
    "tf_ispname"
    "tf_igetroutine"
    "tf_igetworkarea"
    "tf_isetworkarea"
    "instance_calls[0] == 2"
    "TfCallError::CallbackException"
    "fsim.runtime.tf_context")
  string(TOLOWER
    "${FSIM_VERIUSER_HEADER_CONTENTS}${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_PLUGIN_MODEL_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_TF_CONTEXT_MODEL_CONTENTS}${FSIM_TF_CONTEXT_IMPLEMENTATION_CONTENTS}${FSIM_TF_CONTEXT_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_CONTEXT_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_CONTEXT_POLICY}" FSIM_TF_CONTEXT_POLICY_LOWER)
  string(FIND "${FSIM_TF_CONTEXT_CONTENTS_LOWER}"
    "${FSIM_TF_CONTEXT_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF context or lifetime access lost resource policy: ${FSIM_TF_CONTEXT_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_CONTROL_POLICY IN ITEMS
    "kMaxTfControlEffects"
    "kMaxTfControlTextSize"
    "kMaxTfControlBytes"
    "TfControlEffectKind"
    "TfControlEffect"
    "TfControlCapture"
    "capture_tf_control_effect_v3"
    "FSIM_TF_CONTROL_OUTPUT"
    "TfCallError::ControlLimit"
    "control_effects"
    "control_emit"
    "control_failed"
    "format_control"
    "io_printf"
    "io_mcdprintf"
    "tf_text"
    "tf_warning"
    "tf_error"
    "tf_message"
    "tf_dofinish"
    "tf_dostop"
    "missing_result.control_effects.empty()"
    "fsim.runtime.tf_control")
  string(TOLOWER
    "${FSIM_VERIUSER_HEADER_CONTENTS}${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_CONTROL_MODEL_CONTENTS}${FSIM_TF_CONTROL_IMPLEMENTATION_CONTENTS}${FSIM_TF_CONTROL_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_CONTROL_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_CONTROL_POLICY}" FSIM_TF_CONTROL_POLICY_LOWER)
  string(FIND "${FSIM_TF_CONTROL_CONTENTS_LOWER}"
    "${FSIM_TF_CONTROL_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF output or control access lost resource policy: ${FSIM_TF_CONTROL_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_SYNCHRONIZATION_POLICY IN ITEMS
    "kMaxTfSynchronizationRequests"
    "TfSynchronizationKind"
    "TfSynchronizationRequest"
    "valid_tf_synchronization_kind"
    "tf_synchronization_reason"
    "FSIM_TF_CALL_PHASE_SYNCHRONIZE"
    "FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE"
    "FSIM_TF_SYNCHRONIZATION_READ_WRITE"
    "FSIM_TF_SYNCHRONIZATION_READ_ONLY"
    "synchronization_requests"
    "TfCallError::InvalidSynchronization"
    "TfCallError::MissingMiscCallback"
    "synchronization_count"
    "append_synchronization"
    "tf_synchronize"
    "tf_rosynchronize"
    "tf_isynchronize"
    "tf_irosynchronize"
    "reason_synch"
    "reason_rosynch"
    "read_write.argument_updates.size() == 1"
    "read_only.argument_updates.empty()"
    "TfCallError::ContextBusy"
    "fsim.runtime.tf_synchronization")
  string(TOLOWER
    "${FSIM_VERIUSER_HEADER_CONTENTS}${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_SYNCHRONIZATION_MODEL_CONTENTS}${FSIM_TF_SYNCHRONIZATION_IMPLEMENTATION_CONTENTS}${FSIM_TF_SYNCHRONIZATION_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_SYNCHRONIZATION_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_SYNCHRONIZATION_POLICY}"
    FSIM_TF_SYNCHRONIZATION_POLICY_LOWER)
  string(FIND "${FSIM_TF_SYNCHRONIZATION_CONTENTS_LOWER}"
    "${FSIM_TF_SYNCHRONIZATION_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF synchronization access lost resource policy: ${FSIM_TF_SYNCHRONIZATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_APPLICATION_POLICY IN ITEMS
    "kMaxTfApplicationPlugins"
    "kMaxTfApplicationRegistrations"
    "TfApplicationRegistry"
    "TfApplicationRegistration"
    "tf_application_profile_supported"
    "StandardRevision::Verilog1995"
    "StandardRevision::Verilog2001"
    "StandardRevision::Verilog2001NoConfig"
    "StandardRevision::Verilog2005"
    "StandardRevision::SystemVerilog2005"
    "StandardRevision::SystemVerilog2009"
    "StandardRevision::SystemVerilog2012"
    "StandardRevision::SystemVerilog2017"
    "StandardRevision::Vhdl2008"
    "TfApplicationError::DuplicateRegistration"
    "TfApplicationError::UnsupportedProfile"
    "TfApplicationError::MissingRegistration"
    "TfApplicationError::KindMismatch"
    "runtime::load_tf_plugin"
    "registry.resolve"
    "registry.bind"
    "result_width() == 17"
    "registry.plugin_count() == 1"
    "fsim.application.tf-plugin")
  string(TOLOWER
    "${FSIM_TF_APPLICATION_MODEL_CONTENTS}${FSIM_TF_APPLICATION_IMPLEMENTATION_CONTENTS}${FSIM_TF_APPLICATION_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    FSIM_TF_APPLICATION_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_APPLICATION_POLICY}"
    FSIM_TF_APPLICATION_POLICY_LOWER)
  string(FIND "${FSIM_TF_APPLICATION_CONTENTS_LOWER}"
    "${FSIM_TF_APPLICATION_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF HDL system registration lost resource policy: ${FSIM_TF_APPLICATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_DISCOVERY_POLICY IN ITEMS
    "FSIM_NATIVE_PLUGIN_DESCRIPTOR_SYMBOL"
    "FSIM_NATIVE_PLUGIN_MAX_INTERFACES 8u"
    "fsim_native_plugin_interface_v3"
    "FSIM_TF_INTERFACE_ABI_VERSION 3u"
    "FSIM_TF_REGISTRATION_TABLE_ABI_VERSION 3u"
    "FSIM_TF_MAX_REGISTRATIONS 4096u"
    "fsim_tf_registration_v3"
    "fsim_tf_registration_table_v3"
    "validate_tf_registration_table"
    "validate_tf_plugin_descriptor"
    "InterfaceDuplicate"
    "RegistrationTable"
    "fsim.runtime.tf_plugin")
  string(TOLOWER
    "${FSIM_NATIVE_PLUGIN_ABI_CONTENTS}${FSIM_TF_PLUGIN_ABI_CONTENTS}${FSIM_TF_PLUGIN_MODEL_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_TF_PLUGIN_TEST_CONTENTS}${FSIM_TF_LINK_PLUGIN_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_DISCOVERY_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_DISCOVERY_POLICY}"
    FSIM_TF_DISCOVERY_POLICY_LOWER)
  string(FIND "${FSIM_TF_DISCOVERY_CONTENTS_LOWER}"
    "${FSIM_TF_DISCOVERY_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF registration discovery lost resource policy: ${FSIM_TF_DISCOVERY_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_REGISTRATION_POLICY IN ITEMS
    "FSIM_TF_MAX_REGISTRATION_NAME_SIZE 255u"
    "TfRegistrationKind"
    "TfRegistrationError"
    "DuplicateName"
    "validate_and_copy_tf_registrations"
    "registration.struct_size > table.entry_stride"
    "registration.calltf == nullptr"
    "registration.sizetf == nullptr"
    "registration.sizetf != nullptr"
    "valid_registration_name"
    "result.value.empty()"
    "callback_calls == 0"
    "fsim.runtime.tf_registration")
  string(TOLOWER
    "${FSIM_TF_PLUGIN_ABI_CONTENTS}${FSIM_TF_PLUGIN_MODEL_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_TF_REGISTRATION_MODEL_CONTENTS}${FSIM_TF_REGISTRATION_IMPLEMENTATION_CONTENTS}${FSIM_TF_REGISTRATION_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_REGISTRATION_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_REGISTRATION_POLICY}"
    FSIM_TF_REGISTRATION_POLICY_LOWER)
  string(FIND "${FSIM_TF_REGISTRATION_CONTENTS_LOWER}"
    "${FSIM_TF_REGISTRATION_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF descriptor validation lost resource policy: ${FSIM_TF_REGISTRATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_CALL_POLICY IN ITEMS
    "fsim_tf_routine_v3"
    "fsim_tf_misc_routine_v3"
    "kMaxTfFunctionWidth"
    "TfFunctionResult"
    "TfBoundCall"
    "bind_tf_call_with_owner"
    "reason_checktf"
    "reason_sizetf"
    "reason_calltf"
    "TfCallError::CallbackException"
    "TfCallError::ContextBusy"
    "TfCallError::UnassignedResult"
    "fsim_tf_call_context_enter_v3"
    "fsim_tf_call_context_leave_v3"
    "assign_integral_result"
    "tf_putlongp"
    "tf_putrealp"
    "loaded.value.reset()"
    "fsim.runtime.tf_call")
  string(TOLOWER
    "${FSIM_TF_PLUGIN_ABI_CONTENTS}${FSIM_TF_PLUGIN_MODEL_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_CALL_TEST_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_PLUGIN_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}${FSIM_ROOT_CONTENTS}"
    FSIM_TF_CALL_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_CALL_POLICY}" FSIM_TF_CALL_POLICY_LOWER)
  string(FIND "${FSIM_TF_CALL_CONTENTS_LOWER}"
    "${FSIM_TF_CALL_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF task/function callback lost resource policy: ${FSIM_TF_CALL_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_MISC_POLICY IN ITEMS
    "TfMiscReason"
    "ParameterValueChange = reason_paramvc"
    "Synchronize = reason_synch"
    "ReadOnlySynchronize = reason_rosynch"
    "StartOfSave = reason_startofsave"
    "StartOfRestart = reason_startofrestart"
    "TfMiscDispatcher"
    "valid_parameter"
    "parameter > 0"
    "parameter == 0"
    "TfMiscError::CallbackException"
    "TfMiscError::Reentrant"
    "last_callback_value == 31"
    "loaded.value.reset()"
    "fsim.runtime.tf_misc")
  string(TOLOWER
    "${FSIM_TF_PLUGIN_MODEL_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_TF_MISC_MODEL_CONTENTS}${FSIM_TF_MISC_IMPLEMENTATION_CONTENTS}${FSIM_TF_MISC_TEST_CONTENTS}${FSIM_TF_LINK_PLUGIN_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_MISC_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_MISC_POLICY}" FSIM_TF_MISC_POLICY_LOWER)
  string(FIND "${FSIM_TF_MISC_CONTENTS_LOWER}"
    "${FSIM_TF_MISC_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF misctf lifecycle lost resource policy: ${FSIM_TF_MISC_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_ARGUMENT_POLICY IN ITEMS
    "kMaxTfArguments"
    "kMaxTfArgumentWidth"
    "kMaxTfExpressionTextSize"
    "TfArgumentKind"
    "TfArgumentDirection"
    "validate_and_copy_tf_arguments"
    "FSIM_TF_CALL_PHASE_CHECK"
    "FSIM_TF_CALL_PHASE_SIZE"
    "FSIM_TF_CALL_PHASE_CALL"
    "fsim_tf_argument_bridge_v3"
    "tf_nump(void)"
    "tf_typep"
    "tf_sizep"
    "tf_exprinfo"
    "TfCallError::InvalidArguments"
    "phase_calls[reason_checktf] == 1"
    "phase_calls[reason_sizetf] == 1"
    "phase_calls[reason_calltf] == 1"
    "fsim.runtime.tf_argument")
  string(TOLOWER
    "${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_ARGUMENT_MODEL_CONTENTS}${FSIM_TF_ARGUMENT_IMPLEMENTATION_CONTENTS}${FSIM_TF_ARGUMENT_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_ARGUMENT_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_ARGUMENT_POLICY}" FSIM_TF_ARGUMENT_POLICY_LOWER)
  string(FIND "${FSIM_TF_ARGUMENT_CONTENTS_LOWER}"
    "${FSIM_TF_ARGUMENT_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF argument inspection lost resource policy: ${FSIM_TF_ARGUMENT_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_VALUE_POLICY IN ITEMS
    "kMaxTfCallValueBytes"
    "TfValueKind"
    "TfValueError"
    "validate_and_copy_tf_values"
    "make_default_tf_values"
    "FSIM_TF_VALUE_INTEGRAL"
    "TfArgumentUpdate"
    "TfCallError::InvalidValues"
    "tf_getcstringp"
    "tf_getlongp"
    "tf_getrealp"
    "tf_strgetp"
    "tf_evaluatep"
    "tf_propagatep"
    "value.vector_words.back().bvalbits"
    "invoked.argument_updates.size() == 4"
    "fsim.runtime.tf_value")
  string(TOLOWER
    "${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_VALUE_MODEL_CONTENTS}${FSIM_TF_VALUE_IMPLEMENTATION_CONTENTS}${FSIM_TF_VALUE_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_VALUE_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_VALUE_POLICY}" FSIM_TF_VALUE_POLICY_LOWER)
  string(FIND "${FSIM_TF_VALUE_CONTENTS_LOWER}"
    "${FSIM_TF_VALUE_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF value access lost resource policy: ${FSIM_TF_VALUE_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_INSTANCE_POLICY IN ITEMS
    "TfInstanceIdentity"
    "TfInstanceError"
    "validate_tf_instance_identity"
    "FSIM_TF_INSTANCE_ABI_VERSION 3u"
    "fsim_tf_instance_bridge_v3"
    "TfCallError::InvalidInstance"
    "instance_identity"
    "tf_getinstance(void)"
    "current_instance_matches"
    "tf_iexprinfo"
    "tf_inodeinfo"
    "tf_igetp"
    "tf_iputp"
    "tf_istrgetp"
    "instance_tokens[1] != instance_tokens[2]"
    "fsim.runtime.tf_instance")
  string(TOLOWER
    "${FSIM_TF_CALL_BRIDGE_CONTENTS}${FSIM_TF_CALL_MODEL_CONTENTS}${FSIM_TF_CALL_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_PLUGIN_MODEL_CONTENTS}${FSIM_TF_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_TF_INSTANCE_MODEL_CONTENTS}${FSIM_TF_INSTANCE_IMPLEMENTATION_CONTENTS}${FSIM_TF_INSTANCE_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_TF_INSTANCE_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_INSTANCE_POLICY}" FSIM_TF_INSTANCE_POLICY_LOWER)
  string(FIND "${FSIM_TF_INSTANCE_CONTENTS_LOWER}"
    "${FSIM_TF_INSTANCE_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF parameter/instance access lost resource policy: ${FSIM_TF_INSTANCE_POLICY}")
  endif()
endforeach()

foreach(FSIM_VERIUSER_POLICY IN ITEMS
    "typedef int PLI_INT32"
    "reason_checktf 1"
    "reason_rosynch 11"
    "reason_startofrestart 28"
    "tf_readwritereal 16"
    "tf_real_node 107"
    "typedef struct t_tfexprinfo"
    "typedef struct t_tfnodeinfo"
    "tf_getinstance"
    "tf_igetp"
    "tf_rosynchronize"
    "tf_synchronize"
    "veriuser_version_str"
    "endofcompile_routines"
    "must not hide C++ keywords"
    "fsim.runtime.veriuser_abi")
  string(TOLOWER
    "${FSIM_VERIUSER_HEADER_CONTENTS}${FSIM_VERIUSER_TEST_CONTENTS}${FSIM_VERIUSER_C_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_VERIUSER_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_VERIUSER_POLICY}" FSIM_VERIUSER_POLICY_LOWER)
  string(FIND "${FSIM_VERIUSER_CONTENTS_LOWER}"
    "${FSIM_VERIUSER_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "standard veriuser header lost resource policy: ${FSIM_VERIUSER_POLICY}")
  endif()
endforeach()

foreach(FSIM_TF_LINK_POLICY IN ITEMS
    "fsim_add_tf_link_surface"
    "add_library(fsim::tf ALIAS"
    "FSIM_TF_LINK_SURFACE_BUILD=1"
    "OUTPUT_NAME fsim_tf"
    "SOVERSION 3"
    "FSIM_NATIVE_PLUGIN_CAPABILITY_TF"
    "standard_tf_symbols"
    "standard_tf_symbols[0])"
    "add_library(fsim::tf SHARED IMPORTED)"
    "target_link_libraries(fsim_installed_tf_consumer PRIVATE fsim::tf)"
    "installed TF consumer"
    "fsim/runtime/veriuser.h"
    "target STREQUAL \"fsim_tf\""
    "err_intercept"
    "veriuser_version_str"
    "vpi_printf"
    "fsim.runtime.tf_plugin_link")
  string(TOLOWER
    "${FSIM_TF_LINK_CMAKE_CONTENTS}${FSIM_TF_LINK_IMPLEMENTATION_CONTENTS}${FSIM_TF_LINK_TEST_CONTENTS}${FSIM_TF_LINK_PLUGIN_CONTENTS}${FSIM_TF_PACKAGE_CONFIG_CONTENTS}${FSIM_TF_INSTALLED_CONTRACT_CONTENTS}${FSIM_TF_INSTALL_OWNERSHIP_CONTENTS}${FSIM_TF_INSTALLED_CONSUMER_CMAKE_CONTENTS}${FSIM_TF_INSTALLED_CONSUMER_CONTENTS}${FSIM_TF_SYSTEMC_BOUNDARY_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}${FSIM_ROOT_CONTENTS}"
    FSIM_TF_LINK_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_TF_LINK_POLICY}" FSIM_TF_LINK_POLICY_LOWER)
  string(FIND "${FSIM_TF_LINK_CONTENTS_LOWER}"
    "${FSIM_TF_LINK_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "TF platform link surface lost resource policy: ${FSIM_TF_LINK_POLICY}")
  endif()
endforeach()

foreach(FSIM_NATIVE_PLUGIN_POLICY IN ITEMS
    "FSIM_NATIVE_PLUGIN_ABI_VERSION 3u"
    "fsim_native_plugin_descriptor_v3_get"
    "FSIM_NATIVE_PLUGIN_CAPABILITY_TF"
    "FSIM_NATIVE_PLUGIN_CAPABILITY_ACC"
    "FSIM_NATIVE_PLUGIN_KNOWN_CAPABILITIES"
    "validate_native_plugin_descriptor"
    "copy_native_plugin_metadata"
    "value.abi_version = 2"
    "fsim.runtime.native_plugin_abi")
  string(TOLOWER
    "${FSIM_NATIVE_PLUGIN_ABI_CONTENTS}${FSIM_NATIVE_PLUGIN_MODEL_CONTENTS}${FSIM_NATIVE_PLUGIN_IMPLEMENTATION_CONTENTS}${FSIM_NATIVE_PLUGIN_TEST_CONTENTS}${FSIM_NATIVE_PLUGIN_C_TEST_CONTENTS}${FSIM_RUNTIME_TEST_CMAKE_CONTENTS}"
    FSIM_NATIVE_PLUGIN_CONTENTS_LOWER)
  string(TOLOWER "${FSIM_NATIVE_PLUGIN_POLICY}"
    FSIM_NATIVE_PLUGIN_POLICY_LOWER)
  string(FIND "${FSIM_NATIVE_PLUGIN_CONTENTS_LOWER}"
    "${FSIM_NATIVE_PLUGIN_POLICY_LOWER}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "v3 native plug-in ABI lost resource policy: ${FSIM_NATIVE_PLUGIN_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_SCHEMA_POLICY IN ITEMS
    "kCoverageDatabaseMagic"
    "kCoverageDatabaseSchema = 3U"
    "kCoverageDatabaseNamespaceSchema = 3U"
    "kCoverageDatabaseByteOrderMarker = 0x01020304U"
    "kCoverageDatabaseHeaderBytes = 64U"
    "kCoverageDatabaseNamespaceDescriptorBytes = 64U"
    "kCoverageDatabaseNamespaceCount = 3U"
    "CoverageDatabaseNamespace::Code"
    "CoverageDatabaseNamespace::SystemVerilogFunctional"
    "CoverageDatabaseNamespace::Psl"
    "maximum_container_bytes { 1ULL << 30U }"
    "maximum_namespace_bytes { 512ULL << 20U }"
    "CoverageDatabaseSchemaError::ArithmeticOverflow"
    "invalid.header.schema = 2U"
    "fsim.artifact.coverage-database-schema")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_SCHEMA_CONTENTS}${FSIM_COVERAGE_DATABASE_SCHEMA_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_DATABASE_SCHEMA_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_SCHEMA_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      ".fsimcov v3 container schema lost bounded policy: ${FSIM_COVERAGE_DATABASE_SCHEMA_POLICY}")
  endif()
endforeach()

foreach(FSIM_SYSTEMVERILOG_VPI_COVERAGE_POLICY IN ITEMS
    "FSIM-COV-040"
    "fsim-systemverilog-vpi-coverage-v3"
    "vpiCoverageStart 750"
    "vpiFsm 758"
    "vpiCovered 765"
    "vpiFsmStateExpression 776"
    "SystemVerilogVpiCoverageService"
    "maximum_states { 1U << 22U }"
    "publish_target"
    "publish_fsm"
    "DuplicateState"
    "CrossSimulation"
    "release_iterator"
    "Bit 62 is reserved for v3 coverage objects"
    "configure_standard_vpi_coverage"
    "test_fsm_relations_values_and_iterators")
  string(FIND
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI_CONTENTS}${FSIM_SYSTEMVERILOG_VPI_COVERAGE_CONTENTS}${FSIM_SYSTEMVERILOG_VPI_COVERAGE_IMPLEMENTATION_CONTENTS}${FSIM_SYSTEMVERILOG_VPI_COVERAGE_OBJECT_IMPLEMENTATION_CONTENTS}${FSIM_SYSTEMVERILOG_VPI_COVERAGE_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_VPI_COVERAGE_ABI_TEST_CONTENTS}${FSIM_SYSTEMVERILOG_COVERAGE_ACCESS_APPLICATION_CONTENTS}${FSIM_DIAGNOSTICS_CONTENTS}"
    "${FSIM_SYSTEMVERILOG_VPI_COVERAGE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "SystemVerilog VPI coverage lost ABI, ownership, traversal, or resource policy: ${FSIM_SYSTEMVERILOG_VPI_COVERAGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_MODEL_POLICY IN ITEMS
    "fsim-unified-coverage-database-v3"
    "CoverageDatabaseModelFingerprint"
    "std::vector<CoverageDatabaseSourceRecord> sources"
    "std::vector<CoverageDatabaseRunRecord> runs"
    "std::vector<CoverageDatabaseMetricRecord> metrics"
    "std::vector<CoverageDatabaseExclusionRecord> exclusions"
    "maximum_sources { 1U << 20U }"
    "maximum_runs { 1U << 16U }"
    "maximum_metrics { 1U << 24U }"
    "maximum_exclusions { 1U << 24U }"
    "maximum_text_bytes { 1U << 30U }"
    "CoverageDatabaseNamespace::SystemVerilogFunctional"
    "CoverageDatabaseNamespace::Psl"
    "metric.overflow"
    "std::ranges::sort(contents.metrics"
    "invalid.fingerprint.schema = 2U"
    "fsim.artifact.coverage-database-model")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_MODEL_CONTENTS}${FSIM_COVERAGE_DATABASE_MODEL_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_DATABASE_MODEL_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_MODEL_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      ".fsimcov v3 content model lost bounded policy: ${FSIM_COVERAGE_DATABASE_MODEL_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_POLICY IN ITEMS
    "FSIM-COV-033"
    "CoverageDatabaseNamespace::SystemVerilogFunctional"
    "CoverageDatabaseMetricFamily::SystemVerilogCoverpoint"
    "CoverageDatabaseMetricFamily::SystemVerilogCross"
    "maximum_source_bindings { 1U << 20U }"
    "maximum_declarations { 1U << 20U }"
    "maximum_instances { 1U << 20U }"
    "maximum_bins { 1U << 24U }"
    "fsim-systemverilog-coverage-instance-v3"
    "fsim-systemverilog-coverpoint-bin-v3"
    "fsim-systemverilog-cross-bin-v3"
    "SystemVerilog functional coverage bin excluded"
    "cross.exclusion_count"
    "SystemVerilogCoverageDatabaseError::NamespaceNotEmpty"
    "SystemVerilogCoverageDatabaseError::InvalidDatabaseModel"
    "fsim.frontend.coverage-database-systemverilog")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_MODEL_CONTENTS}${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_CONTENTS}${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      ".fsimcov SystemVerilog namespace lost bounded policy: ${FSIM_COVERAGE_DATABASE_SYSTEMVERILOG_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_PSL_POLICY IN ITEMS
    "FSIM-COV-034"
    "CoverageDatabaseNamespace::Psl"
    "CoverageDatabaseMetricFamily::PslDirective"
    "CoverageDatabaseMetricFamily::PslProperty"
    "maximum_source_bindings { 1U << 20U }"
    "maximum_directives { 1U << 20U }"
    "maximum_identity_bytes { 1U << 20U }"
    "fsim-psl-coverage-instance-v3"
    "fsim-psl-coverage-bin-v3"
    "std::string_view { \"pass\" }"
    "std::string_view { \"failure\" }"
    "std::string_view { \"vacuous\" }"
    "std::string_view { \"aborted\" }"
    "completed != item.attempts"
    "PslCoverageDatabaseError::NamespaceNotEmpty"
    "PslCoverageDatabaseError::InvalidDatabaseModel"
    "fsim.application.coverage-database-psl")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_MODEL_CONTENTS}${FSIM_COVERAGE_DATABASE_PSL_CONTENTS}${FSIM_COVERAGE_DATABASE_PSL_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_DATABASE_PSL_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_PSL_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      ".fsimcov PSL namespace lost bounded policy: ${FSIM_COVERAGE_DATABASE_PSL_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_CODEC_POLICY IN ITEMS
    "FSIM-COV-035"
    "serialize_coverage_database"
    "deserialize_coverage_database"
    "write_coverage_database_atomically"
    "read_coverage_database"
    "kNamespaceMagic"
    "writer.patch_u64(16U"
    "digest_bytes(payload) != descriptor.payload_digest"
    "add_product(minimum_bytes"
    "CoverageDatabaseCodecError::ResourceLimit"
    "CoverageDatabaseSchemaError::SchemaMismatch"
    "CoverageDatabaseSchemaError::NamespaceSchemaMismatch"
    ".fsim-tmp"
    ".fsim-old"
    "!destination_exists && backup_exists"
    "fsim.artifact.coverage-database-codec")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_CODEC_CONTENTS}${FSIM_COVERAGE_DATABASE_CODEC_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_DATABASE_CODEC_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_CODEC_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      ".fsimcov codec lost deterministic, bounded, or atomic policy: ${FSIM_COVERAGE_DATABASE_CODEC_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_MERGE_POLICY IN ITEMS
    "FSIM-COV-036"
    "merge_coverage_databases"
    "CoverageDatabaseMergeError::FingerprintMismatch"
    "CoverageDatabaseMergeError::SourceInventoryMismatch"
    "CoverageDatabaseMergeError::ExclusionInventoryMismatch"
    "CoverageDatabaseMergeError::DuplicateRun"
    "checked_accumulate"
    "std::set<CoverageDatabaseIdentity> run_identities"
    "maximum_inputs { 1U << 16U }"
    "make_coverage_database_contents(inputs[input_index], limits.model)"
    "limits.model.maximum_runs"
    "limits.model.maximum_metrics"
    "fingerprint.schema = 2U"
    "fsim.artifact.coverage-database-merge")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_MERGE_CONTENTS}${FSIM_COVERAGE_DATABASE_MERGE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_DATABASE_MERGE_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_MERGE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      ".fsimcov strict merge lost identity, transaction, or resource policy: ${FSIM_COVERAGE_DATABASE_MERGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_POLICY IN ITEMS
    "FSIM-COV-037"
    "merge_coverage_databases_partially"
    "CoverageDatabasePartialMergeStatistics"
    "maximum_historical_sources { 1U << 22U }"
    "maximum_historical_runs { 1U << 20U }"
    "maximum_historical_metrics { 1U << 26U }"
    "maximum_historical_exclusions { 1U << 24U }"
    "history.size() > limits.merge.maximum_inputs - 1U"
    "target_metric_count"
    "std::ranges::binary_search(target_metrics"
    "unchanged_sources"
    "statistics.omitted_metrics"
    "statistics.omitted_exclusions"
    "fingerprint.schema = 2U"
    "fsim.artifact.coverage-database-partial-merge")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_CONTENTS}${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      ".fsimcov partial merge lost target, point-identity, omission, or resource policy: ${FSIM_COVERAGE_DATABASE_PARTIAL_MERGE_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_SOURCE_POLICY IN ITEMS
    "maximum_logical_path_bytes { 1U << 20U }"
    "maximum_content_bytes { 1U << 30U }"
    "contents.size() > limits.maximum_content_bytes"
    "logical_path.size() > limits.maximum_logical_path_bytes"
    "contains_parent_component(relative)"
    "valid_utf8(logical_path)"
    "logical path bytes must be bounded"
    "source content bytes must be bounded")
  string(FIND
    "${FSIM_CODE_COVERAGE_SOURCE_CONTENTS}${FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION_CONTENTS}${FSIM_CODE_COVERAGE_SOURCE_TEST_CONTENTS}"
    "${FSIM_CODE_COVERAGE_SOURCE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage source identity lost resource policy: ${FSIM_CODE_COVERAGE_SOURCE_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_SOURCE_CONTROL_POLICY IN ITEMS
    "FSIM-COV-041"
    "maximum_source_bytes { 1U << 30U }"
    "maximum_line_bytes { 1U << 20U }"
    "maximum_directives { 1U << 16U }"
    "maximum_reason_bytes { 1U << 12U }"
    "maximum_total_reason_bytes { 1U << 20U }"
    "// fsim coverage off metric=statement reason=\"generated glue\""
    "corresponding VHDL `--` spelling"
    "CoverageSourceControlError::ConflictingAllMetric"
    "source.source_text.size()"
    "support::Sha256::digest(source.source_text)"
    "coverage_source_exclusion_at("
    "CoverageSourceMetric::Statement"
    "VerilogCoveragePointError::InvalidSourceControl"
    "VhdlCoveragePointError::InvalidSourceControl"
    "fsim.elaboration.coverage_source_control")
  string(FIND
    "${FSIM_COVERAGE_SOURCE_CONTROL_CONTENTS}${FSIM_COVERAGE_SOURCE_CONTROL_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_SOURCE_CONTROL_TEST_CONTENTS}${FSIM_VERILOG_COVERAGE_POINTS_CONTENTS}${FSIM_VERILOG_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_COVERAGE_POINTS_CONTENTS}${FSIM_VHDL_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_SOURCE_CONTROL_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage source control lost syntax, reason, metric, language, or resource policy: ${FSIM_COVERAGE_SOURCE_CONTROL_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_EXTERNAL_EXCLUSION_POLICY IN ITEMS
    "FSIM-COV-042"
    "fsim-coverage-external-exclusion-v1"
    "CoverageExclusionEntry"
    "coverage.exclude"
    "maximum_rules { 1U << 16U }"
    "maximum_pattern_bytes { 1U << 12U }"
    "maximum_reason_bytes { 1U << 12U }"
    "maximum_total_bytes { 1U << 24U }"
    "maximum_targets { 1U << 20U }"
    "maximum_match_operations { 1U << 28U }"
    "CoverageExternalExclusionError::InvalidPlan"
    "config.coverage.exclusions"
    "must fail before elaboration"
    "match_coverage_external_exclusions("
    "fsim.elaboration.coverage_external_exclusions")
  string(FIND
    "${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_CONTENTS}${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_TEST_CONTENTS}${FSIM_COVERAGE_EXTERNAL_EXCLUSIONS_APPLICATION_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_PROJECT_CONTENTS}${FSIM_CODE_COVERAGE_CONTROL_TEST_CONTENTS}${FSIM_COVERAGE_FSM_HINTS_PROJECT_IMPLEMENTATION_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_EXTERNAL_EXCLUSION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "external coverage exclusions lost manifest, selector, identity, or resource policy: ${FSIM_COVERAGE_EXTERNAL_EXCLUSION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_EXCLUSION_PERSISTENCE_POLICY IN ITEMS
    "FSIM-COV-043"
    "CoverageExclusionCandidate"
    "maximum_candidates { 1U << 20U }"
    "maximum_source_reasons { 1U << 24U }"
    "maximum_records { 1U << 24U }"
    "maximum_total_reason_bytes { 1U << 30U }"
    "CoverageDatabaseMetricScope::Source"
    "CoverageDatabaseMetricScope::Instance"
    "match_coverage_external_exclusions("
    "record.point_identity, record.reason"
    "VerilogStatementCoverageExclusion"
    "VhdlStatementCoverageExclusion"
    "CoverageExclusionReportPoint"
    "validate_coverage_database_contents("
    "total_reasons"
    "fsim.elaboration.coverage_exclusion_persistence"
    "fsim.artifact.coverage-exclusion-report")
  string(FIND
    "${FSIM_COVERAGE_EXCLUSION_PERSISTENCE_CONTENTS}${FSIM_COVERAGE_EXCLUSION_PERSISTENCE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_EXCLUSION_PERSISTENCE_TEST_CONTENTS}${FSIM_COVERAGE_EXCLUSION_REPORT_CONTENTS}${FSIM_COVERAGE_EXCLUSION_REPORT_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_EXCLUSION_REPORT_TEST_CONTENTS}${FSIM_COVERAGE_DATABASE_MODEL_IMPLEMENTATION_CONTENTS}${FSIM_VERILOG_COVERAGE_POINTS_CONTENTS}${FSIM_VHDL_COVERAGE_POINTS_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_EXCLUSION_PERSISTENCE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage exclusion persistence lost point, reason, database, report, or resource policy: ${FSIM_COVERAGE_EXCLUSION_PERSISTENCE_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_REPORT_MODEL_POLICY IN ITEMS
    "FSIM-COV-044"
    "CoverageSourceReport"
    "CoverageInstanceReport"
    "CoverageCombinedReport"
    "Deliberately no overall"
    "maximum_exact_points { 1U << 24U }"
    "maximum_source_points { 1U << 24U }"
    "maximum_instance_points { 1U << 24U }"
    "maximum_instances { 1U << 20U }"
    "make_coverage_exclusion_report("
    "source_occurrence"
    "combined.metrics"
    "hits_saturated"
    "fsim.artifact.coverage-report-model")
  string(FIND
    "${FSIM_COVERAGE_REPORT_MODEL_CONTENTS}${FSIM_COVERAGE_REPORT_MODEL_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_REPORT_MODEL_TEST_CONTENTS}${FSIM_COVERAGE_EXCLUSION_REPORT_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_REPORT_MODEL_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage report model lost source, instance, combined, exclusion, saturation, or resource policy: ${FSIM_COVERAGE_REPORT_MODEL_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_REPORT_RENDER_POLICY IN ITEMS
    "FSIM-COV-045"
    "fsim-coverage-report-v3"
    "CoverageReportFormat::Text"
    "CoverageReportFormat::Html"
    "CoverageReportFormat::Json"
    "maximum_output_bytes { 1U << 30U }"
    "escaped_text"
    "escaped_html"
    "escaped_json"
    "hits_saturated"
    "total_reasons"
    "grand_score"
    "fsim.artifact.coverage-report-render")
  string(FIND
    "${FSIM_COVERAGE_REPORT_RENDER_CONTENTS}${FSIM_COVERAGE_REPORT_RENDER_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_REPORT_RENDER_INTERNAL_CONTENTS}${FSIM_COVERAGE_REPORT_RENDER_HTML_CONTENTS}${FSIM_COVERAGE_REPORT_RENDER_JSON_CONTENTS}${FSIM_COVERAGE_REPORT_RENDER_TEXT_CONTENTS}${FSIM_COVERAGE_REPORT_RENDER_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_REPORT_RENDER_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage report rendering lost deterministic text, HTML, JSON, fidelity, escaping, or resource policy: ${FSIM_COVERAGE_REPORT_RENDER_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_REPORT_PROJECTION_POLICY IN ITEMS
    "FSIM-COV-046"
    "CoverageReportProjectionFormat::Lcov"
    "CoverageReportProjectionFormat::Cobertura"
    "maximum_lines { 1U << 24U }"
    "maximum_branches { 1U << 24U }"
    "MissingSourceLine"
    "BRDA:"
    "condition-coverage"
    "point.status == CoverageReportPointStatus::Excluded"
    "fsim.artifact.coverage-report-projection")
  string(FIND
    "${FSIM_COVERAGE_REPORT_PROJECTION_CONTENTS}${FSIM_COVERAGE_REPORT_PROJECTION_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_REPORT_PROJECTION_TEST_CONTENTS}${FSIM_COVERAGE_REPORT_MODEL_CONTENTS}${FSIM_COVERAGE_DATABASE_MODEL_CONTENTS}${FSIM_COVERAGE_DATABASE_CODEC_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_REPORT_PROJECTION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage projection lost LCOV, Cobertura, source-line, exclusion, or resource policy: ${FSIM_COVERAGE_REPORT_PROJECTION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_COMMAND_POLICY IN ITEMS
    "FSIM-COV-047"
    "coverage merge"
    "coverage report"
    "--partial"
    "--threshold METRIC=PERCENT"
    "kThresholdFailure = 4"
    "merge_coverage_databases("
    "merge_coverage_databases_partially("
    "write_coverage_database_atomically("
    "write_text_atomically("
    "project_coverage_report("
    "minimum_covered("
    "statement=100"
    "fsim.application.coverage-command")
  string(FIND
    "${FSIM_COVERAGE_COMMAND_CONTENTS}${FSIM_COVERAGE_COMMAND_CLI_CONTENTS}${FSIM_COVERAGE_COMMAND_CLI_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_COMMAND_TEST_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}${FSIM_ROOT_CONTENTS}"
    "${FSIM_COVERAGE_COMMAND_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage command lost merge, report, threshold, CI-exit, atomic-output, or test policy: ${FSIM_COVERAGE_COMMAND_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_DATABASE_ROBUSTNESS_POLICY IN ITEMS
    "C:/checkout/rtl/top.sv"
    "rtl/top.sv"
    "rtl/leaf.vhd"
    "rtl/properties.psl"
    "CoverageDatabaseNamespace::SystemVerilogFunctional"
    "CoverageDatabaseNamespace::Psl"
    "CoverageDatabaseSchemaError::ResourceLimit"
    "CoverageDatabaseMergeError::FingerprintMismatch"
    "CoverageDatabaseMergeError::SourceInventoryMismatch"
    "CoverageDatabaseMergeError::ExclusionInventoryMismatch"
    "CoverageDatabaseMergeError::DuplicateRun"
    "unchanged_source_matches == 2U"
    "CoverageReportFormat::Json"
    "CoverageReportProjectionFormat::Lcov"
    "CoverageReportProjectionFormat::Cobertura"
    "fsim.artifact.coverage-database-robustness")
  string(FIND
    "${FSIM_COVERAGE_DATABASE_ROBUSTNESS_TEST_CONTENTS}${FSIM_COVERAGE_DATABASE_MODEL_IMPLEMENTATION_CONTENTS}${FSIM_TEST_CMAKE_CONTENTS}"
    "${FSIM_COVERAGE_DATABASE_ROBUSTNESS_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage robustness lost corruption, ceiling, path, conflict, or mixed-language policy: ${FSIM_COVERAGE_DATABASE_ROBUSTNESS_POLICY}")
  endif()
endforeach()

foreach(FSIM_CODE_COVERAGE_POINT_POLICY IN ITEMS
    "std::uint64_t begin_offset"
    "std::uint64_t end_offset"
    "span.end_offset > source.content_bytes"
    "update_digest(hash, source.digest)"
    "digest_word(digest, 0U)"
    "digest_word(digest, 8U)"
    "d8adc683731fb1114e3957ecec2b508c"
    "point identity must not depend on request or allocation order")
  string(FIND
    "${FSIM_CODE_COVERAGE_POINT_CONTENTS}${FSIM_CODE_COVERAGE_POINT_IMPLEMENTATION_CONTENTS}${FSIM_CODE_COVERAGE_POINT_TEST_CONTENTS}"
    "${FSIM_CODE_COVERAGE_POINT_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "code coverage point identity lost fixed resource policy: ${FSIM_CODE_COVERAGE_POINT_POLICY}")
  endif()
endforeach()

foreach(FSIM_VERILOG_COVERAGE_CONDITION_POLICY IN ITEMS
    "AtomicCondition = 7U"
    "maximum_sources { 1U << 16U }"
    "maximum_statements { 1U << 20U }"
    "maximum_conditions { 1U << 20U }"
    "maximum_expression_nodes { 1U << 22U }"
    "maximum_path_steps { 1U << 22U }"
    "maximum_statement_nesting { 1U << 12U }"
    "maximum_expression_nesting { 1U << 12U }"
    "CoverageConditionLogicalOperator::Not"
    "CoverageConditionOperand::Only"
    "expression.expression->operands.size() != 2U"
    "decomposition must not rewrite or flatten the retained expression tree"
    "every retained Verilog/SystemVerilog profile must parse the condition corpus"
    "duplicate atomic identities must not publish partial output")
  string(FIND
    "${FSIM_CODE_COVERAGE_POINT_CONTENTS}${FSIM_COVERAGE_CONDITIONS_CONTENTS}${FSIM_COVERAGE_CONDITIONS_IMPLEMENTATION_CONTENTS}${FSIM_VERILOG_COVERAGE_CONDITIONS_CONTENTS}${FSIM_VERILOG_COVERAGE_CONDITIONS_IMPLEMENTATION_CONTENTS}${FSIM_VERILOG_COVERAGE_CONDITIONS_TEST_CONTENTS}"
    "${FSIM_VERILOG_COVERAGE_CONDITION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog atomic-condition decomposition lost policy: ${FSIM_VERILOG_COVERAGE_CONDITION_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_COVERAGE_CONDITION_POLICY IN ITEMS
    "CoverageConditionLogicalOperator::Nand"
    "CoverageConditionLogicalOperator::Nor"
    "CoverageConditionLogicalOperator::Xor"
    "CoverageConditionLogicalOperator::Xnor"
    "CoverageConditionDecisionKind::WaitUntil"
    "frontend::VhdlStandard::Vhdl1987"
    "frontend::VhdlStandard::Vhdl2008"
    "every retained VHDL profile must parse the Boolean corpus"
    "VHDL decomposition must not mutate the retained expression tree"
    "duplicate VHDL condition identities must not publish partial output"
    "synthetic unconditional loop and bare-wait conditions must not manufacture atoms")
  string(FIND
    "${FSIM_COVERAGE_CONDITIONS_CONTENTS}${FSIM_COVERAGE_CONDITIONS_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_COVERAGE_CONDITIONS_CONTENTS}${FSIM_VHDL_COVERAGE_CONDITIONS_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_COVERAGE_CONDITIONS_TEST_CONTENTS}"
    "${FSIM_VHDL_COVERAGE_CONDITION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL atomic-condition decomposition lost policy: ${FSIM_VHDL_COVERAGE_CONDITION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_CONDITION_EVALUATION_POLICY IN ITEMS
    "maximum_atoms { 1U << 20U }"
    "maximum_nodes { 1U << 22U }"
    "maximum_path_steps { 1U << 22U }"
    "maximum_nesting { 1U << 12U }"
    "CoverageConditionTruth::Unknown"
    "short_circuit_result(operation, frame.left)"
    "result.observations.clear()"
    "result.skipped.clear()"
    "four-state logical evaluation must skip only determined results"
    "VHDL Boolean operators must match the governed lowering contract"
    "callback failure must discard every partial observation"
    "tree node ceilings must fail before evaluation")
  string(FIND
    "${FSIM_COVERAGE_CONDITION_EVALUATION_CONTENTS}${FSIM_COVERAGE_CONDITION_EVALUATION_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_CONDITION_EVALUATION_TEST_CONTENTS}"
    "${FSIM_COVERAGE_CONDITION_EVALUATION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "condition evaluation lost short-circuit or resource policy: ${FSIM_COVERAGE_CONDITION_EVALUATION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_CONDITION_OUTCOME_POLICY IN ITEMS
    "maximum_outcomes { 1U << 20U }"
    "maximum_observations { 1U << 20U }"
    "unknown_observations"
    "coverage_condition_outcome_status"
    "observed[observation.condition_index]"
    "std::numeric_limits<std::uint64_t>::max()"
    "unknown observations must not satisfy either binary score bin"
    "a short-circuited atom with no observation must remain unchanged"
    "every saturated update must be reported explicitly"
    "overflow flags without saturated counters must be rejected"
    "outcome-table ceilings must apply before mutation")
  string(FIND
    "${FSIM_COVERAGE_CONDITION_OUTCOMES_CONTENTS}${FSIM_COVERAGE_CONDITION_OUTCOMES_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_CONDITION_OUTCOMES_TEST_CONTENTS}"
    "${FSIM_COVERAGE_CONDITION_OUTCOME_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "condition outcomes lost binary, unknown, saturation, or resource policy: ${FSIM_COVERAGE_CONDITION_OUTCOME_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_EXPRESSION_POLICY IN ITEMS
    "maximum_atoms { 1U << 20U }"
    "maximum_combinations { 1U << 20U }"
    "maximum_combination_words { 1U << 22U }"
    "omitted_count_exact"
    "limits.maximum_combination_words / word_count"
    "2^"
    "combination ceiling must retain a deterministic canonical prefix"
    "every bounded-away binary combination must be reported exactly"
    "wide omissions must publish the exact symbolic space expression"
    "whole-combination word storage must be bounded without partial bins"
    "invalid identities must not publish a partial inventory")
  string(FIND
    "${FSIM_COVERAGE_EXPRESSION_CONTENTS}${FSIM_COVERAGE_EXPRESSION_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_EXPRESSION_TEST_CONTENTS}"
    "${FSIM_COVERAGE_EXPRESSION_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage expression lost bounded expansion or omission policy: ${FSIM_COVERAGE_EXPRESSION_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_TOGGLE_POLICY IN ITEMS
    "CoverageToggleDirection::ZeroToOne"
    "CoverageToggleDirection::OneToZero"
    "maximum_outcomes { 1U << 22U }"
    "maximum_transitions { 1U << 22U }"
    "transition.previous == transition.current"
    "outcome.zero_to_one_hits"
    "outcome.one_to_zero_hits"
    "CoverageToggleLogicValue::Unknown"
    "CoverageToggleLogicValue::HighImpedance"
    "outcome.unknown_transition_observations"
    "outcome.high_impedance_transition_observations"
    "both endpoints are"
    "one selected bit must own distinct direction-qualified bin identities"
    "only actual binary transitions may increment their exact direction"
    "observing the reverse direction must complete rather than alias a bin"
    "both direction counters must saturate explicitly without wrapping"
    "X/Z activity must remain diagnostic and never satisfy a binary toggle bin"
    "X and Z diagnostic counters must saturate independently without wrapping"
    "invalid four-state encodings must fail before observation mutation"
    "transition ceiling must apply before mutation")
  string(FIND
    "${FSIM_COVERAGE_TOGGLE_CONTENTS}${FSIM_COVERAGE_TOGGLE_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_TOGGLE_TEST_CONTENTS}"
    "${FSIM_COVERAGE_TOGGLE_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage toggle lost direction, saturation, or resource policy: ${FSIM_COVERAGE_TOGGLE_POLICY}")
  endif()
endforeach()

foreach(FSIM_VERILOG_COVERAGE_POINT_POLICY IN ITEMS
    "maximum_sources { 1U << 16U }"
    "maximum_statements { 1U << 20U }"
    "maximum_nesting { 1U << 12U }"
    "sources.size() > limits.maximum_sources"
    "statements.size() > limits.maximum_statements"
    "current.depth > limits.maximum_nesting"
    "blocks, declarations, and null statements must not become points"
    "the statement-count ceiling must be enforced before allocation"
    "the nesting ceiling must bound adversarial statement trees")
  string(FIND
    "${FSIM_VERILOG_COVERAGE_POINTS_CONTENTS}${FSIM_VERILOG_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS}${FSIM_VERILOG_COVERAGE_POINTS_TEST_CONTENTS}"
    "${FSIM_VERILOG_COVERAGE_POINT_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Verilog coverage statement discovery lost resource policy: ${FSIM_VERILOG_COVERAGE_POINT_POLICY}")
  endif()
endforeach()

foreach(FSIM_VHDL_COVERAGE_POINT_POLICY IN ITEMS
    "maximum_sources { 1U << 16U }"
    "maximum_statements { 1U << 20U }"
    "maximum_nesting { 1U << 12U }"
    "sources.size() > limits.maximum_sources"
    "statements.size() > limits.maximum_statements"
    "current.depth > limits.maximum_nesting"
    "a source point identity must be stable across VHDL revisions"
    "VHDL source and statement ceilings must apply before allocation"
    "VHDL nesting must be bounded for adversarial statement trees")
  string(FIND
    "${FSIM_VHDL_COVERAGE_POINTS_CONTENTS}${FSIM_VHDL_COVERAGE_POINTS_IMPLEMENTATION_CONTENTS}${FSIM_VHDL_COVERAGE_POINTS_TEST_CONTENTS}"
    "${FSIM_VHDL_COVERAGE_POINT_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "VHDL coverage statement discovery lost resource policy: ${FSIM_VHDL_COVERAGE_POINT_POLICY}")
  endif()
endforeach()

foreach(FSIM_COVERAGE_BRANCH_POLICY IN ITEMS
    "maximum_sources { 1U << 16U }"
    "maximum_statements { 1U << 20U }"
    "maximum_arms { 1U << 20U }"
    "maximum_nesting { 1U << 12U }"
    "result.points.size() >= limits.maximum_arms"
    "current.depth > limits.maximum_nesting"
    "if, case, and conditional loop arms must be individually discovered"
    "duplicate arm identities must not publish partial output"
    "branch discovery nesting must be bounded")
  string(FIND
    "${FSIM_COVERAGE_BRANCHES_CONTENTS}${FSIM_COVERAGE_BRANCHES_IMPLEMENTATION_CONTENTS}${FSIM_COVERAGE_BRANCHES_TEST_CONTENTS}"
    "${FSIM_COVERAGE_BRANCH_POLICY}"
    FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "coverage branch discovery lost resource policy: ${FSIM_COVERAGE_BRANCH_POLICY}")
  endif()
endforeach()

string(FIND
  "${FSIM_COVERAGE_CONTENTS}"
  "boost/multiprecision/cpp_int.hpp"
  FSIM_BOOST_CONSUMER_INDEX)
string(REGEX MATCHALL
  "PRIVATE[ \t\r\n]+fsim_boost_pfr_headers"
  FSIM_BOOST_INCLUDE_BINDINGS
  "${FSIM_ROOT_CONTENTS}")
string(REGEX MATCHALL
  "PRIVATE[ \t\r\n]+fsim_boost_pfr_headers"
  FSIM_FUZZ_BOOST_INCLUDE_BINDINGS
  "${FSIM_FUZZ_CMAKE_CONTENTS}")
list(LENGTH FSIM_BOOST_INCLUDE_BINDINGS FSIM_BOOST_INCLUDE_COUNT)
list(LENGTH FSIM_FUZZ_BOOST_INCLUDE_BINDINGS FSIM_FUZZ_BOOST_INCLUDE_COUNT)
if(FSIM_BOOST_CONSUMER_INDEX EQUAL -1
    OR NOT FSIM_BOOST_INCLUDE_COUNT EQUAL 5
    OR NOT FSIM_FUZZ_BOOST_INCLUDE_COUNT EQUAL 1)
  message(FATAL_ERROR
    "Boost consumers lost a pinned header target binding")
endif()
foreach(FSIM_STACK_POLICY IN ITEMS
    "function(fsim_configure_test_platform target)"
    "CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\""
    "target_link_options(\${target} PRIVATE /STACK:134217728)")
  string(FIND "${FSIM_ROOT_CONTENTS}" "${FSIM_STACK_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "Windows test stack lost policy: ${FSIM_STACK_POLICY}")
  endif()
endforeach()

foreach(FSIM_TIMEOUT_POLICY IN ITEMS
    "fsim.application.scoped_locals"
    "PROPERTIES TIMEOUT 60"
    "fsim.application.systemc_matrix"
    "PROPERTIES TIMEOUT 1200"
    "fsim.application.sv_containers"
    "PROPERTIES TIMEOUT 1200"
    "set_tests_properties(fsim.api PROPERTIES TIMEOUT 120)")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_TIMEOUT_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "test inventory lost timeout policy: ${FSIM_TIMEOUT_POLICY}")
  endif()
endforeach()

foreach(FSIM_PHASE IN ITEMS
    "building reference project"
    "building compiled project"
    "running interpreter"
    "running compiled engine"
    "building warm project"
    "running warm compiled engine"
    "complete")
  string(FIND "${FSIM_SCOPED_CONTENTS}" "${FSIM_PHASE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "scoped-local trace lost phase: ${FSIM_PHASE}")
  endif()
endforeach()
foreach(FSIM_PHASE IN ITEMS
    "systemc matrix: integration"
    "systemc matrix: scheduling"
    "systemc matrix: complete")
  string(FIND "${FSIM_SYSTEMC_CONTENTS}" "${FSIM_PHASE}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemC matrix trace lost phase: ${FSIM_PHASE}")
  endif()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "fsim.resource-portability-contract"
  FSIM_REGISTRATION_INDEX)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "resource portability contract CTest is not registered")
endif()

message(STATUS
  "resource portability contract: five four-worker build/test steps, "
  "120-minute hosted jobs, "
  "eight-link pool, compact Debug objects, 128 MiB Windows stacks, bounded "
  "large-test, code-coverage model/source/point/statement/branch discovery, opt-in and standard SystemVerilog control/query/merge/save, v3 artifact identity, bounded .fsimcov schema, and mixed-language engine/aggregation equivalence "
  "line-state derivation and instance inventory attachment, FST value/change/hierarchy storage, pinned "
  "Boost headers, "
  "broad coverage-metric generate/mixed-engine equivalence, complete legacy ACC routine/object inventory, public C/C++ header ABI, bounded transactional ACC lifecycle, generation-qualified ACC/VPI handles, bounded hierarchy lookup, independently worded VHDL-2019 clause ownership and profile/artifact/cache identity, and scoped/SystemC phase traces are present")

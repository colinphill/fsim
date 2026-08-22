# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_ROOT "${FSIM_SOURCE_DIR}/CMakeLists.txt")
set(FSIM_FOOTPRINT "${FSIM_SOURCE_DIR}/cmake/FsimDebugFootprint.cmake")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_FUZZ_CMAKE "${FSIM_SOURCE_DIR}/tests/fuzz/CMakeLists.txt")
set(FSIM_WORKFLOW "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml")
set(FSIM_SCOPED "${FSIM_SOURCE_DIR}/tests/app/scoped_local_application_test.cpp")
set(FSIM_SYSTEMC "${FSIM_SOURCE_DIR}/tests/app/application_systemc_matrix_test.cpp")
set(FSIM_COVERAGE "${FSIM_SOURCE_DIR}/src/frontend/coverage_sampling.cpp")
set(FSIM_CODE_COVERAGE_MODEL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/code_coverage.hpp")
set(FSIM_CODE_COVERAGE_MODEL_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/code_coverage_model_test.cpp")
set(FSIM_CODE_COVERAGE_SOURCE
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/coverage_source_identity.hpp")
set(FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/frontend/coverage_source_identity.cpp")
set(FSIM_CODE_COVERAGE_SOURCE_TEST
  "${FSIM_SOURCE_DIR}/tests/frontend/coverage_source_identity_test.cpp")
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
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY}"
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_IMPLEMENTATION}"
    "${FSIM_CODE_COVERAGE_ARTIFACT_IDENTITY_TEST}"
    "${FSIM_CODE_COVERAGE_EQUIVALENCE_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_APPLICATION_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_EQUIVALENCE_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_IDENTITY_TEST}"
    "${FSIM_CODE_COVERAGE_METRICS_INVENTORY}"
    "${FSIM_CODE_COVERAGE_METRICS_CHECKER}"
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
file(READ "${FSIM_CODE_COVERAGE_MODEL_TEST}"
  FSIM_CODE_COVERAGE_MODEL_TEST_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_SOURCE}"
  FSIM_CODE_COVERAGE_SOURCE_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION}"
  FSIM_CODE_COVERAGE_SOURCE_IMPLEMENTATION_CONTENTS)
file(READ "${FSIM_CODE_COVERAGE_SOURCE_TEST}"
  FSIM_CODE_COVERAGE_SOURCE_TEST_CONTENTS)
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
    "${FSIM_CODE_COVERAGE_MODEL_CONTENTS}${FSIM_CODE_COVERAGE_MODEL_TEST_CONTENTS}"
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
  "large-test, code-coverage model/source/point/statement/branch discovery, opt-in control, v3 artifact identity, and mixed-language engine/aggregation equivalence "
  "line-state derivation and instance inventory attachment, FST value/change/hierarchy storage, pinned "
  "Boost headers, "
  "broad coverage-metric generate/mixed-engine equivalence, and scoped/SystemC phase traces are present")

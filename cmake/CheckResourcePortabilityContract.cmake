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
  "and scoped/SystemC phase traces are present")

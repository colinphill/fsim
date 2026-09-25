# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_CTEST_COMMAND FSIM_TEST_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()
file(MAKE_DIRECTORY "${FSIM_TEST_WORK_DIR}")

function(fsim_expect_rejection FSIM_CASE FSIM_MARKER FSIM_CHECKER)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      "-DFSIM_BINARY_DIR=${FSIM_BINARY_DIR}"
      "-DFSIM_CTEST_COMMAND=${FSIM_CTEST_COMMAND}"
      ${ARGN}
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_CHECKER}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  string(FIND "${FSIM_OUTPUT}${FSIM_ERROR}" "${FSIM_MARKER}"
    FSIM_MARKER_OFFSET)
  if(FSIM_RESULT EQUAL 0 OR FSIM_MARKER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "${FSIM_CASE} did not reject the intended mutation: "
      "${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
endfunction()

file(READ
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/abi_schema_evidence_matrix.tsv"
  FSIM_ABI_MATRIX)
string(FIND "${FSIM_ABI_MATRIX}" "\tcovered\t" FSIM_COVERED_OFFSET)
if(FSIM_COVERED_OFFSET EQUAL -1)
  message(FATAL_ERROR "ABI/schema fixture has no covered cell to mutate")
endif()
string(REPLACE "\tcovered\t" "\tmissing\t"
  FSIM_BAD_ABI_MATRIX "${FSIM_ABI_MATRIX}")
set(FSIM_BAD_ABI_PATH "${FSIM_TEST_WORK_DIR}/bad-abi-matrix.tsv")
file(WRITE "${FSIM_BAD_ABI_PATH}" "${FSIM_BAD_ABI_MATRIX}")
fsim_expect_rejection(
  "ABI/schema uncovered cell" "ABI/schema evidence cell is not covered"
  CheckAbiSchemaEvidenceMatrix.cmake
  "-DFSIM_ABI_SCHEMA_TEST_MATRIX=${FSIM_BAD_ABI_PATH}")

file(READ "${FSIM_SOURCE_DIR}/include/fsim/app/application.hpp"
  FSIM_APPLICATION_HEADER)
set(FSIM_BAD_HEADER_PATH "${FSIM_TEST_WORK_DIR}/bad-application.hpp")
file(WRITE "${FSIM_BAD_HEADER_PATH}"
  "${FSIM_APPLICATION_HEADER}\n// CompilationWorkspace is forbidden here.\n")
fsim_expect_rejection(
  "AST-lifetime forbidden owner" "exposes forbidden token: CompilationWorkspace"
  CheckAstLifetimeGovernance.cmake
  "-DFSIM_AST_LIFETIME_TEST_APPLICATION_HEADER=${FSIM_BAD_HEADER_PATH}")

file(READ "${FSIM_SOURCE_DIR}/src/app/application_compiled_environment_sv.cpp"
  FSIM_COMPILED_SV_SOURCE)
set(FSIM_BAD_COMPILED_SV_PATH
  "${FSIM_TEST_WORK_DIR}/bad-compiled-environment-sv.cpp")
string(REPLACE "class LinkedEnvironment final {"
  "class LinkedEnvironment final {\n    std::unique_ptr<frontend::Expression> retained_syntax;"
  FSIM_BAD_COMPILED_SV_SOURCE "${FSIM_COMPILED_SV_SOURCE}")
file(WRITE "${FSIM_BAD_COMPILED_SV_PATH}" "${FSIM_BAD_COMPILED_SV_SOURCE}")
fsim_expect_rejection(
  "compiled HIR resolver retained AST ownership"
  "frontend::Expression"
  CheckAstLifetimeGovernance.cmake
  "-DFSIM_AST_LIFETIME_TEST_COMPILED_SV_SOURCE=${FSIM_BAD_COMPILED_SV_PATH}")

string(REPLACE "class LinkedEnvironment final {"
  "class LinkedEnvironment final {\n    frontend::Expression retained_syntax;"
  FSIM_BAD_COMPILED_SV_SOURCE "${FSIM_COMPILED_SV_SOURCE}")
file(WRITE "${FSIM_BAD_COMPILED_SV_PATH}" "${FSIM_BAD_COMPILED_SV_SOURCE}")
fsim_expect_rejection(
  "compiled HIR resolver retained AST value"
  "frontend::Expression"
  CheckAstLifetimeGovernance.cmake
  "-DFSIM_AST_LIFETIME_TEST_COMPILED_SV_SOURCE=${FSIM_BAD_COMPILED_SV_PATH}")

string(REPLACE "class SourceEnvironment final {"
  "class SourceEnvironment final {\n    frontend::DesignUnit reconstructed_import;"
  FSIM_BAD_COMPILED_SV_SOURCE "${FSIM_COMPILED_SV_SOURCE}")
file(WRITE "${FSIM_BAD_COMPILED_SV_PATH}" "${FSIM_BAD_COMPILED_SV_SOURCE}")
fsim_expect_rejection(
  "compiled source adapter reconstructed imported AST"
  "compiled source environment must not reconstruct frontend::DesignUnit"
  CheckAstLifetimeGovernance.cmake
  "-DFSIM_AST_LIFETIME_TEST_COMPILED_SV_SOURCE=${FSIM_BAD_COMPILED_SV_PATH}")

file(READ "${FSIM_SOURCE_DIR}/src/app/application_compiled_environment_sv.hpp"
  FSIM_COMPILED_SV_HEADER)
set(FSIM_BAD_COMPILED_SV_HEADER_PATH
  "${FSIM_TEST_WORK_DIR}/bad-compiled-environment-sv.hpp")
file(WRITE "${FSIM_BAD_COMPILED_SV_HEADER_PATH}"
  "${FSIM_COMPILED_SV_HEADER}\nstruct RetainedSyntax { frontend::ParsedDesign* parsed; };\n")
fsim_expect_rejection(
  "compiled environment interface retained AST"
  "frontend::ParsedDesign"
  CheckAstLifetimeGovernance.cmake
  "-DFSIM_AST_LIFETIME_TEST_COMPILED_SV_HEADER=${FSIM_BAD_COMPILED_SV_HEADER_PATH}")

file(WRITE "${FSIM_BAD_COMPILED_SV_HEADER_PATH}"
  "${FSIM_COMPILED_SV_HEADER}\nstruct RetainedSyntax { frontend::ParsedDesign& parsed; };\n")
fsim_expect_rejection(
  "compiled environment interface retained AST reference"
  "frontend::ParsedDesign"
  CheckAstLifetimeGovernance.cmake
  "-DFSIM_AST_LIFETIME_TEST_COMPILED_SV_HEADER=${FSIM_BAD_COMPILED_SV_HEADER_PATH}")

execute_process(
  COMMAND "${FSIM_CTEST_COMMAND}" --test-dir "${FSIM_BINARY_DIR}"
    --show-only=json-v1
  RESULT_VARIABLE FSIM_CTEST_RESULT
  OUTPUT_VARIABLE FSIM_CTEST_JSON
  ERROR_VARIABLE FSIM_CTEST_ERROR)
if(NOT FSIM_CTEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "cannot inspect CTest fixture metadata: ${FSIM_CTEST_ERROR}")
endif()
string(FIND "${FSIM_CTEST_JSON}"
  "fsim_compatibility_smoke_closure_witnesses" FSIM_FIXTURE_OFFSET)
if(FSIM_FIXTURE_OFFSET EQUAL -1)
  message(FATAL_ERROR "fixture mutation target is missing")
endif()
string(REPLACE "fsim_compatibility_smoke_closure_witnesses"
  "fsim_missing_fixture" FSIM_BAD_CTEST_JSON "${FSIM_CTEST_JSON}")
set(FSIM_BAD_CTEST_PATH "${FSIM_TEST_WORK_DIR}/bad-ctest.json")
file(WRITE "${FSIM_BAD_CTEST_PATH}" "${FSIM_BAD_CTEST_JSON}")
fsim_expect_rejection(
  "child-directory fixture ownership"
  "child-directory witness fsim.runtime"
  CheckCTestCommandUniqueness.cmake
  "-DFSIM_CTEST_TEST_JSON=${FSIM_BAD_CTEST_PATH}")

message(STATUS
  "ABI/schema, AST-lifetime, compiled-environment, and fixture rejection probes passed")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR OR
   NOT DEFINED FSIM_BINARY_DIR OR
   NOT DEFINED FSIM_CTEST_COMMAND OR
   NOT DEFINED FSIM_OUTPUT_DIR)
  message(FATAL_ERROR "SDF closure requires source, binary, ctest and output directories")
endif()

file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}")
set(FSIM_TRANSCRIPT "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.txt")
file(WRITE "${FSIM_TRANSCRIPT}"
  "FSIM-SDF-CLOSURE-START revisions=2.1,3.0,4.0\n")

set(FSIM_TEST_REGEX
  "^fsim\\.(frontend\\.sdf.*|application\\.sdf_.*|application$|project$|library\\.artifact$|artifact\\.design$|diagnostics-catalog$|source-line-budget$|sdf-inventory$|resource-portability-contract$)")
execute_process(
  COMMAND "${FSIM_CTEST_COMMAND}"
    --test-dir "${FSIM_BINARY_DIR}"
    --output-on-failure
    --verbose
    --timeout 1200
    -R "${FSIM_TEST_REGEX}"
  RESULT_VARIABLE FSIM_STATUS
  OUTPUT_VARIABLE FSIM_STDOUT
  ERROR_VARIABLE FSIM_STDERR
  TIMEOUT 1200
)
file(APPEND "${FSIM_TRANSCRIPT}" "${FSIM_STDOUT}${FSIM_STDERR}")

set(FSIM_EXPECTED_OUTPUT
  "FSIM-SDF-CORPUS-PASS revisions=3 files=6"
  "application tests passed"
  "FSIM-OLDER-STANDARD-ARTIFACT-MATRIX-PASS"
  "100% tests passed")
foreach(FSIM_MARKER IN LISTS FSIM_EXPECTED_OUTPUT)
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "${FSIM_MARKER}"
    FSIM_MARKER_OFFSET)
  if(FSIM_MARKER_OFFSET EQUAL -1)
    file(WRITE "${FSIM_RESULT}"
      "FAIL missing-progress-marker=${FSIM_MARKER}\n")
    message(FATAL_ERROR
      "SDF closure did not observe progress marker: ${FSIM_MARKER}; retained log: ${FSIM_TRANSCRIPT}")
  endif()
endforeach()

set(FSIM_NEGATIVE_SOURCES
  "${FSIM_SOURCE_DIR}/tests/frontend/sdf_lexer_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/frontend/sdf_parser_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/frontend/sdf21_adapter_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/frontend/sdf30_adapter_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/app/sdf_cell_resolution_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/app/sdf_endpoint_resolution_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/app/sdf_mapping_validation_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/app/sdf_schema_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/app/sdf_artifact_identity_test.cpp"
  "${FSIM_SOURCE_DIR}/tests/app/application_test_non_project_cli.cpp")
set(FSIM_NEGATIVE_TEXT "")
foreach(FSIM_INPUT IN LISTS FSIM_NEGATIVE_SOURCES)
  file(READ "${FSIM_INPUT}" FSIM_INPUT_TEXT)
  string(APPEND FSIM_NEGATIVE_TEXT "${FSIM_INPUT_TEXT}")
endforeach()
foreach(FSIM_DIAGNOSTIC_FAMILY IN ITEMS
    "FSIM-SDF-LEX-"
    "FSIM-SDF-PARSE-"
    "FSIM-SDF-21-"
    "FSIM-SDF-30-"
    "FSIM-SDF-RESOLVE-"
    "FSIM-SDF-ENDPOINT-"
    "FSIM-SDF-MAP-"
    "FSIM-SDF-SCHEMA-"
    "FSIM-SDF-ARTIFACT-"
    "FSIM-SDF-PORTABLE-")
  string(FIND "${FSIM_NEGATIVE_TEXT}" "${FSIM_DIAGNOSTIC_FAMILY}"
    FSIM_DIAGNOSTIC_OFFSET)
  if(FSIM_DIAGNOSTIC_OFFSET EQUAL -1)
    file(WRITE "${FSIM_RESULT}"
      "FAIL missing-negative-family=${FSIM_DIAGNOSTIC_FAMILY}\n")
    message(FATAL_ERROR
      "SDF closure lost asserted negative family ${FSIM_DIAGNOSTIC_FAMILY}")
  endif()
endforeach()

if(NOT FSIM_STATUS EQUAL 0)
  file(WRITE "${FSIM_RESULT}" "FAIL ctest-status=${FSIM_STATUS}\n")
  message(FATAL_ERROR
    "SDF closure failed with status ${FSIM_STATUS}; retained log: ${FSIM_TRANSCRIPT}")
endif()

file(APPEND "${FSIM_TRANSCRIPT}"
  "FSIM-SDF-CLOSURE-PASS engines=interpreter-llvm cache=cold-warm-relocated negatives=10 resources=governed\n")
file(WRITE "${FSIM_RESULT}"
  "PASS revisions=3 engines=2 negative-families=10 swaps-required=0\n")
message(STATUS
  "FSIM-SDF-CLOSURE-PASS retained-log=${FSIM_TRANSCRIPT}")

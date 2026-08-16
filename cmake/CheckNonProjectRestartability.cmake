# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_normalized_text_sha256 path output_variable)
  file(READ "${path}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(REPLACE "\r" "\n" contents "${contents}")
  string(SHA256 digest "${contents}")
  set("${output_variable}" "${digest}" PARENT_SCOPE)
endfunction()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/non_project_restartability_contract.tsv")
set(FSIM_EXPECTED_DIGEST
  "db5a4cb6ad75e7049891ca87596adc9c3c6415214ab4f1c01a512c63bb81ba73")
fsim_normalized_text_sha256("${FSIM_CONTRACT}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "non-project restartability digest changed: expected ${FSIM_EXPECTED_DIGEST}, "
    "got ${FSIM_ACTUAL_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 38)
  message(FATAL_ERROR
    "non-project restartability contract requires SPDX, header and 36 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tphase\tentrypoint\tboundary\tsource_state\tlanguages\troots\tinvariant\tevidence\tctest")
  message(FATAL_ERROR "non-project restartability header or SPDX drifted")
endif()

set(FSIM_IDS)
foreach(FSIM_INDEX RANGE 2 37)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 10)
    message(FATAL_ERROR "restartability row ${FSIM_INDEX} is malformed")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 4 FSIM_SOURCE_STATE)
  list(GET FSIM_FIELDS 8 FSIM_EVIDENCE)
  list(GET FSIM_FIELDS 9 FSIM_CTEST)
  math(EXPR FSIM_NUMBER "${FSIM_INDEX} - 1")
  if(FSIM_NUMBER LESS 10)
    set(FSIM_EXPECTED_ID "RESTART174-0${FSIM_NUMBER}")
  else()
    set(FSIM_EXPECTED_ID "RESTART174-${FSIM_NUMBER}")
  endif()
  if(NOT FSIM_ID STREQUAL FSIM_EXPECTED_ID OR
     NOT FSIM_SOURCE_STATE MATCHES "^(present|hidden-after|hidden|edited)$" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}" OR FSIM_CTEST STREQUAL "")
    message(FATAL_ERROR "restartability row ${FSIM_ID} drifted")
  endif()
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate restartability id ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()

function(fsim_require_restart_tokens relative_path)
  file(READ "${FSIM_SOURCE_DIR}/${relative_path}" FSIM_TEXT)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_TEXT}" "${FSIM_TOKEN}" FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "restartability owner ${relative_path} lost ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_restart_tokens(tests/app/application_test_non_project_cli.cpp
  "\"compile\"" "\"elaborate\"" "\"simulate\""
  "serialize_runtime_state" "producer-hidden" "relocated_read_only_design")
fsim_require_restart_tokens(tests/app/application_test_cli.cpp
  "\"fsim\", \"run\"" "\"fsim\", \"debug\"" "debug_trace")
fsim_require_restart_tokens(tests/app/application_test_artifact_phases.cpp
  "checkpoint" "relocated_interpreted" "relocated_compiled"
  "provenance" "cache.hits")
fsim_require_restart_tokens(tests/app/application_test_multiple_roots.cpp
  "\"source\"" "\"sink\"" "design.roots()")
fsim_require_restart_tokens(tests/app/application_test_source_updates.cpp
  "write_provenance_source" "write_unused_source" "write_parameter_top")
fsim_require_restart_tokens(tests/api/api_mapped_library_test.cpp
  "producer-hidden" "fsim_session_build" "native_accepted")
fsim_require_restart_tokens(tests/runtime/runtime_vpi_checkpoint_tests.cpp
  "checkpoint" "restore")
fsim_require_restart_tokens(tests/runtime/runtime_vhpi_checkpoint_tests.cpp
  "checkpoint" "restore")

message(STATUS
  "non-project restartability contract passed: rows=36 digest=${FSIM_ACTUAL_DIGEST}")

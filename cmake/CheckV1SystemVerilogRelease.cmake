# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_AUDIT "${FSIM_SOURCE_DIR}/docs/v1-systemverilog-release-audit.md")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_MATRIX}" "${FSIM_AUDIT}" "${FSIM_TEST_CMAKE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SystemVerilog release-audit input not found: ${FSIM_INPUT}")
  endif()
endforeach()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
    -P "${FSIM_SOURCE_DIR}/cmake/CheckV1ReleaseAudit.cmake"
  RESULT_VARIABLE FSIM_RELEASE_RESULT
  OUTPUT_VARIABLE FSIM_RELEASE_OUTPUT
  ERROR_VARIABLE FSIM_RELEASE_ERROR
)
if(NOT FSIM_RELEASE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "composed final release audit failed\n"
    "${FSIM_RELEASE_OUTPUT}${FSIM_RELEASE_ERROR}")
endif()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
file(READ "${FSIM_AUDIT}" FSIM_AUDIT_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
string(REPLACE ";" "<SEMICOLON>" FSIM_MATRIX_LINES "${FSIM_MATRIX_CONTENTS}")
string(REPLACE "\n" ";" FSIM_MATRIX_LINES "${FSIM_MATRIX_LINES}")

set(FSIM_ROW_COUNT 0)
set(FSIM_SV_COUNT 0)
set(FSIM_V1_SV_COUNT 0)
set(FSIM_POSITIVE_OWNER_GAPS)
set(FSIM_NEGATIVE_OWNER_GAPS)
set(FSIM_ELABORATION_OWNER_GAPS)
set(FSIM_RUNTIME_OWNER_GAPS)
set(FSIM_STALE_CLAIM_GAPS)
set(FSIM_RUNTIME_OWNERS)

foreach(FSIM_LINE IN LISTS FSIM_MATRIX_LINES)
  if(NOT FSIM_LINE MATCHES "^\\| ((SV|V1-SV)-[0-9]+) \\|")
    continue()
  endif()
  set(FSIM_ID "${CMAKE_MATCH_1}")
  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
  if(FSIM_ID MATCHES "^V1-SV-")
    math(EXPR FSIM_V1_SV_COUNT "${FSIM_V1_SV_COUNT} + 1")
  else()
    math(EXPR FSIM_SV_COUNT "${FSIM_SV_COUNT} + 1")
  endif()

  set(FSIM_FIELDS_TEXT "${FSIM_LINE}")
  string(REPLACE "\\|" "<PIPE>" FSIM_FIELDS_TEXT "${FSIM_FIELDS_TEXT}")
  string(REPLACE "|" ";" FSIM_FIELDS "${FSIM_FIELDS_TEXT}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(FSIM_FIELD_COUNT LESS 9)
    message(FATAL_ERROR "malformed SystemVerilog release row: ${FSIM_ID}")
  endif()
  math(EXPR FSIM_STATUS_INDEX "${FSIM_FIELD_COUNT} - 6")
  math(EXPR FSIM_POSITIVE_INDEX "${FSIM_FIELD_COUNT} - 5")
  math(EXPR FSIM_NEGATIVE_INDEX "${FSIM_FIELD_COUNT} - 4")
  math(EXPR FSIM_ELABORATION_INDEX "${FSIM_FIELD_COUNT} - 3")
  math(EXPR FSIM_RUNTIME_INDEX "${FSIM_FIELD_COUNT} - 2")
  list(GET FSIM_FIELDS ${FSIM_STATUS_INDEX} FSIM_STATUS)
  list(GET FSIM_FIELDS ${FSIM_POSITIVE_INDEX} FSIM_POSITIVE)
  list(GET FSIM_FIELDS ${FSIM_NEGATIVE_INDEX} FSIM_NEGATIVE)
  list(GET FSIM_FIELDS ${FSIM_ELABORATION_INDEX} FSIM_ELABORATION)
  list(GET FSIM_FIELDS ${FSIM_RUNTIME_INDEX} FSIM_RUNTIME)
  foreach(FSIM_FIELD IN ITEMS
      FSIM_STATUS FSIM_POSITIVE FSIM_NEGATIVE FSIM_ELABORATION FSIM_RUNTIME)
    string(STRIP "${${FSIM_FIELD}}" ${FSIM_FIELD})
    if(${FSIM_FIELD} STREQUAL "" OR ${FSIM_FIELD} STREQUAL "—")
      message(FATAL_ERROR "${FSIM_ID} has an empty ${FSIM_FIELD} release owner")
    endif()
  endforeach()
  if(NOT FSIM_STATUS STREQUAL "execute")
    message(FATAL_ERROR "${FSIM_ID} is not executable in the final matrix")
  endif()

  if(NOT FSIM_POSITIVE MATCHES
      "\\]\\(\\.\\./(tests|src|include)/")
    list(APPEND FSIM_POSITIVE_OWNER_GAPS "${FSIM_ID}")
  endif()
  if(NOT FSIM_NEGATIVE MATCHES
      "\\]\\((\\.\\./(tests|src|include)/|diagnostics\\.md)")
    list(APPEND FSIM_NEGATIVE_OWNER_GAPS "${FSIM_ID}")
  endif()
  if(NOT FSIM_ELABORATION MATCHES
      "\\]\\(\\.\\./(tests|src|include)/")
    list(APPEND FSIM_ELABORATION_OWNER_GAPS "${FSIM_ID}")
  endif()
  if(NOT FSIM_RUNTIME MATCHES "\\]\\(\\.\\./tests/")
    list(APPEND FSIM_RUNTIME_OWNER_GAPS "${FSIM_ID}")
  endif()
  if(FSIM_POSITIVE MATCHES "tests missing|positive tests missing"
      OR FSIM_RUNTIME MATCHES "kernels only|implementation only")
    list(APPEND FSIM_STALE_CLAIM_GAPS "${FSIM_ID}")
  endif()

  set(FSIM_RUNTIME_LINKS "${FSIM_RUNTIME}")
  while(FSIM_RUNTIME_LINKS MATCHES "\\]\\((\\.\\./tests/[^)#]+)")
    list(APPEND FSIM_RUNTIME_OWNERS "${CMAKE_MATCH_1}")
    string(REPLACE "${CMAKE_MATCH_0}" "" FSIM_RUNTIME_LINKS "${FSIM_RUNTIME_LINKS}")
  endwhile()
endforeach()

foreach(FSIM_GAP_SET IN ITEMS
    FSIM_POSITIVE_OWNER_GAPS
    FSIM_NEGATIVE_OWNER_GAPS
    FSIM_ELABORATION_OWNER_GAPS
    FSIM_RUNTIME_OWNER_GAPS
    FSIM_STALE_CLAIM_GAPS)
  if(${FSIM_GAP_SET})
    list(JOIN ${FSIM_GAP_SET} ", " FSIM_GAP_TEXT)
    message(FATAL_ERROR "${FSIM_GAP_SET}: ${FSIM_GAP_TEXT}")
  endif()
endforeach()

if(NOT FSIM_ROW_COUNT EQUAL 721
    OR NOT FSIM_SV_COUNT EQUAL 712
    OR NOT FSIM_V1_SV_COUNT EQUAL 9)
  message(FATAL_ERROR
    "expected 721 SystemVerilog rows split 712/9, found "
    "${FSIM_ROW_COUNT} split ${FSIM_SV_COUNT}/${FSIM_V1_SV_COUNT}")
endif()

list(REMOVE_DUPLICATES FSIM_RUNTIME_OWNERS)
list(SORT FSIM_RUNTIME_OWNERS)
list(LENGTH FSIM_RUNTIME_OWNERS FSIM_RUNTIME_OWNER_COUNT)
if(FSIM_RUNTIME_OWNER_COUNT LESS 20)
  message(FATAL_ERROR
    "expected at least 20 distinct SystemVerilog runtime owners, found "
    "${FSIM_RUNTIME_OWNER_COUNT}")
endif()

set(FSIM_REVIEW_IDS
  B130-T2-SV-SYNTAX
  B130-T2-SV-SEMANTICS
  B130-T2-SV-EXECUTION
  B130-T2-SV-NATIVE
  B130-T2-SV-RELEASE
)
foreach(FSIM_REVIEW_ID IN LISTS FSIM_REVIEW_IDS)
  string(FIND "${FSIM_AUDIT_CONTENTS}" "`${FSIM_REVIEW_ID}`" FSIM_REVIEW_INDEX)
  if(FSIM_REVIEW_INDEX EQUAL -1)
    message(FATAL_ERROR "SystemVerilog audit omits review ${FSIM_REVIEW_ID}")
  endif()
endforeach()

string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "NAME fsim.v1-systemverilog-release"
  FSIM_REGISTRATION_INDEX
)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "fsim.v1-systemverilog-release is not registered")
endif()

message(STATUS
  "final SystemVerilog audit covers ${FSIM_ROW_COUNT} rows with "
  "${FSIM_RUNTIME_OWNER_COUNT} distinct executable runtime owners")

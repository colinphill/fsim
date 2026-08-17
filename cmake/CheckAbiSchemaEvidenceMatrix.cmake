# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_MATRIX
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/abi_schema_evidence_matrix.tsv")
set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/abi_schema_inventory.tsv")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_DIAGNOSTICS "${FSIM_SOURCE_DIR}/docs/diagnostics.md")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MATRIX}"
    "${FSIM_INVENTORY}"
    "${FSIM_TEST_CMAKE}"
    "${FSIM_DIAGNOSTICS}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "ABI/schema evidence input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
string(REPLACE "\r\n" "\n" FSIM_MATRIX_CONTENTS
  "${FSIM_MATRIX_CONTENTS}")
string(REPLACE "\r" "\n" FSIM_MATRIX_CONTENTS
  "${FSIM_MATRIX_CONTENTS}")
string(SHA256 FSIM_MATRIX_SHA256 "${FSIM_MATRIX_CONTENTS}")
set(FSIM_EXPECTED_SHA256
  "b9bcf54400eedd53f66e01bfea3b0d54448a27a8321161a24196c591611b8d07")
if(NOT FSIM_MATRIX_SHA256 STREQUAL FSIM_EXPECTED_SHA256)
  message(FATAL_ERROR
    "ABI/schema evidence matrix digest changed: ${FSIM_MATRIX_SHA256}")
endif()

file(STRINGS "${FSIM_INVENTORY}" FSIM_INVENTORY_LINES)
list(REMOVE_AT FSIM_INVENTORY_LINES 0 1)
set(FSIM_LEDGER_IDS "")
foreach(FSIM_LINE IN LISTS FSIM_INVENTORY_LINES)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(GET FSIM_FIELDS 0 FSIM_LEDGER_ID)
  list(APPEND FSIM_LEDGER_IDS "${FSIM_LEDGER_ID}")
endforeach()
list(LENGTH FSIM_LEDGER_IDS FSIM_LEDGER_COUNT)
if(NOT FSIM_LEDGER_COUNT EQUAL 18)
  message(FATAL_ERROR
    "ABI/schema evidence expected 18 ledger rows, found ${FSIM_LEDGER_COUNT}")
endif()

set(FSIM_BOUNDARIES
  positive
  single-field-mutation
  stale-future-version
  corruption
  truncation
  resource
  relocation
  read-only
  source-hidden
  toolchain
  platform)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
file(READ "${FSIM_DIAGNOSTICS}" FSIM_DIAGNOSTIC_TEXT)
file(STRINGS "${FSIM_MATRIX}" FSIM_MATRIX_LINES)
list(GET FSIM_MATRIX_LINES 1 FSIM_HEADER)
set(FSIM_EXPECTED_HEADER
  "ledger_id\tboundary\toutcome\tctest\tdiagnostic\timplementation\tevidence")
if(NOT FSIM_HEADER STREQUAL FSIM_EXPECTED_HEADER)
  message(FATAL_ERROR "ABI/schema evidence matrix header changed")
endif()
list(REMOVE_AT FSIM_MATRIX_LINES 0 1)

foreach(FSIM_LINE IN LISTS FSIM_MATRIX_LINES)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 7)
    message(FATAL_ERROR
      "ABI/schema evidence row has ${FSIM_FIELD_COUNT} fields: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_LEDGER_ID)
  list(GET FSIM_FIELDS 1 FSIM_BOUNDARY)
  list(GET FSIM_FIELDS 2 FSIM_OUTCOME)
  list(GET FSIM_FIELDS 3 FSIM_CTEST)
  list(GET FSIM_FIELDS 4 FSIM_DIAGNOSTIC)
  list(GET FSIM_FIELDS 5 FSIM_IMPLEMENTATION)
  list(GET FSIM_FIELDS 6 FSIM_EVIDENCE)

  list(FIND FSIM_LEDGER_IDS "${FSIM_LEDGER_ID}" FSIM_LEDGER_INDEX)
  list(FIND FSIM_BOUNDARIES "${FSIM_BOUNDARY}" FSIM_BOUNDARY_INDEX)
  if(FSIM_LEDGER_INDEX EQUAL -1 OR FSIM_BOUNDARY_INDEX EQUAL -1)
    message(FATAL_ERROR
      "ABI/schema evidence has unknown cell: ${FSIM_LEDGER_ID}/${FSIM_BOUNDARY}")
  endif()
  if(NOT FSIM_OUTCOME STREQUAL "covered")
    message(FATAL_ERROR
      "ABI/schema evidence cell is not covered: ${FSIM_LEDGER_ID}/${FSIM_BOUNDARY}")
  endif()

  string(MAKE_C_IDENTIFIER
    "${FSIM_LEDGER_ID}_${FSIM_BOUNDARY}" FSIM_CELL_ID)
  if(DEFINED FSIM_SEEN_${FSIM_CELL_ID})
    message(FATAL_ERROR
      "duplicate ABI/schema evidence cell: ${FSIM_LEDGER_ID}/${FSIM_BOUNDARY}")
  endif()
  set(FSIM_SEEN_${FSIM_CELL_ID} TRUE)

  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_CTEST}" FSIM_CTEST_OFFSET)
  if(FSIM_CTEST_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ABI/schema evidence CTest is not registered: ${FSIM_CTEST}")
  endif()
  string(FIND "${FSIM_DIAGNOSTIC_TEXT}"
    "`${FSIM_DIAGNOSTIC}`" FSIM_DIAGNOSTIC_OFFSET)
  if(FSIM_DIAGNOSTIC_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "ABI/schema evidence diagnostic is not cataloged: ${FSIM_DIAGNOSTIC}")
  endif()
  foreach(FSIM_PATH IN ITEMS "${FSIM_IMPLEMENTATION}" "${FSIM_EVIDENCE}")
    if(IS_ABSOLUTE "${FSIM_PATH}" OR FSIM_PATH MATCHES "(^|/)\\.\\.(/|$)" OR
       NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
      message(FATAL_ERROR
        "ABI/schema evidence path is not checked in: ${FSIM_PATH}")
    endif()
  endforeach()
endforeach()

foreach(FSIM_LEDGER_ID IN LISTS FSIM_LEDGER_IDS)
  foreach(FSIM_BOUNDARY IN LISTS FSIM_BOUNDARIES)
    string(MAKE_C_IDENTIFIER
      "${FSIM_LEDGER_ID}_${FSIM_BOUNDARY}" FSIM_CELL_ID)
    if(NOT DEFINED FSIM_SEEN_${FSIM_CELL_ID})
      message(FATAL_ERROR
        "missing ABI/schema evidence cell: ${FSIM_LEDGER_ID}/${FSIM_BOUNDARY}")
    endif()
  endforeach()
endforeach()

list(LENGTH FSIM_MATRIX_LINES FSIM_CELL_COUNT)
if(NOT FSIM_CELL_COUNT EQUAL 198)
  message(FATAL_ERROR
    "ABI/schema evidence expected 198 cells, found ${FSIM_CELL_COUNT}")
endif()

message(STATUS
  "ABI/schema evidence matrix: 18 ledger rows, 11 boundaries and 198 owned cells")

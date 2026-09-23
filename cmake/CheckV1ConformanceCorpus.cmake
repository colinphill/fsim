# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/CurrentEvidenceOwners.cmake")
fsim_current_registered_ctests(FSIM_REGISTERED_CTESTS)

set(FSIM_MANIFEST
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v1_conformance_corpus.txt")
if(NOT EXISTS "${FSIM_MANIFEST}")
  message(FATAL_ERROR "v1 conformance corpus manifest is missing")
endif()

set(FSIM_REQUIRED_MARKERS_FILE
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v1_conformance_required.tsv")
if(NOT EXISTS "${FSIM_REQUIRED_MARKERS_FILE}")
  message(FATAL_ERROR "required conformance marker ledger is missing")
endif()
file(STRINGS "${FSIM_REQUIRED_MARKERS_FILE}" FSIM_REQUIRED_MARKER_ROWS)
list(FILTER FSIM_REQUIRED_MARKER_ROWS EXCLUDE REGEX
  "^# SPDX-License-Identifier: Apache-2.0$")
list(POP_FRONT FSIM_REQUIRED_MARKER_ROWS FSIM_REQUIRED_MARKER_HEADER)
if(NOT FSIM_REQUIRED_MARKER_HEADER STREQUAL
    "id\tpath\towner\tsource\texpectation")
  message(FATAL_ERROR "required conformance marker ledger has an invalid header")
endif()
set(FSIM_REQUIRED_MARKER_IDS)
foreach(FSIM_REQUIRED_ROW IN LISTS FSIM_REQUIRED_MARKER_ROWS)
  string(REPLACE "\t" ";" FSIM_REQUIRED_FIELDS "${FSIM_REQUIRED_ROW}")
  list(LENGTH FSIM_REQUIRED_FIELDS FSIM_REQUIRED_FIELD_COUNT)
  if(NOT FSIM_REQUIRED_FIELD_COUNT EQUAL 5)
    message(FATAL_ERROR "malformed required conformance marker row")
  endif()
  list(GET FSIM_REQUIRED_FIELDS 0 FSIM_REQUIRED_ID)
  list(FIND FSIM_REQUIRED_MARKER_IDS "${FSIM_REQUIRED_ID}" FSIM_DUPLICATE_ID)
  if(NOT FSIM_DUPLICATE_ID EQUAL -1)
    message(FATAL_ERROR "duplicate required conformance marker: ${FSIM_REQUIRED_ID}")
  endif()
  if(NOT FSIM_REQUIRED_ID MATCHES "^[A-Z0-9-]+$")
    message(FATAL_ERROR "invalid required conformance marker: ${FSIM_REQUIRED_ID}")
  endif()
  list(APPEND FSIM_REQUIRED_MARKER_IDS "${FSIM_REQUIRED_ID}")
  string(SHA256 FSIM_REQUIRED_ID_HASH "${FSIM_REQUIRED_ID}")
  set(FSIM_REQUIRED_MARKER_${FSIM_REQUIRED_ID_HASH}
    "${FSIM_REQUIRED_FIELDS}")
endforeach()
if(NOT FSIM_REQUIRED_MARKER_IDS)
  message(FATAL_ERROR "required conformance marker ledger is empty")
endif()

set(FSIM_ALLOWED_MODES
  frontend
  elaboration
  interpreter
  llvm-o0
  llvm-o2
  cache-cold
  cache-warm
  cache-edit
  debugger
  callbacks
  vcd-normalized
  source-map
  portable-path
  diagnostic
  c-abi
  c-api
  plugin
  compiler
  tcl
  cli
  lifecycle
  project
  compile
)
set(FSIM_ALLOWED_SOURCES
  SRC-FSIM
  SRC-IEEE-P1076
  SRC-SV-TESTS
  SRC-SURELOG
  SRC-SLANG
  SRC-UVVM
  SRC-SYSTEMC
  SRC-COCOTB
  SRC-LLVM
  SRC-TCL
)

file(STRINGS "${FSIM_MANIFEST}" FSIM_MANIFEST_LINES ENCODING UTF-8)
set(FSIM_MAPPED_FILES)
set(FSIM_MAPPED_TESTS)
set(FSIM_MAPPED_MODES)
set(FSIM_ALL_MODES)
foreach(FSIM_LINE IN LISTS FSIM_MANIFEST_LINES)
  string(STRIP "${FSIM_LINE}" FSIM_LINE)
  if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#")
    continue()
  endif()
  string(REPLACE "|" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR
      "invalid conformance manifest row, expected three fields: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_FIXTURE)
  list(GET FSIM_FIELDS 1 FSIM_CTEST)
  list(GET FSIM_FIELDS 2 FSIM_MODE_TEXT)
  fsim_current_evidence_file("${FSIM_FIXTURE}")
  list(FIND FSIM_MAPPED_FILES "${FSIM_FIXTURE}" FSIM_DUPLICATE_FILE)
  if(NOT FSIM_DUPLICATE_FILE EQUAL -1)
    message(FATAL_ERROR
      "duplicate conformance fixture mapping: ${FSIM_FIXTURE}")
  endif()
  string(REPLACE "," ";" FSIM_MODES "${FSIM_MODE_TEXT}")
  if(NOT FSIM_MODES)
    message(FATAL_ERROR
      "conformance fixture has no evidence modes: ${FSIM_FIXTURE}")
  endif()
  foreach(FSIM_MODE IN LISTS FSIM_MODES)
    list(FIND FSIM_ALLOWED_MODES "${FSIM_MODE}" FSIM_MODE_INDEX)
    if(FSIM_MODE_INDEX EQUAL -1)
      message(FATAL_ERROR
        "unknown conformance evidence mode ${FSIM_MODE}: ${FSIM_FIXTURE}")
    endif()
    list(APPEND FSIM_ALL_MODES "${FSIM_MODE}")
  endforeach()
  foreach(FSIM_REQUIRED_FIXTURE_MODE IN ITEMS source-map portable-path)
    list(FIND
      FSIM_MODES "${FSIM_REQUIRED_FIXTURE_MODE}" FSIM_REQUIRED_MODE_INDEX)
    if(FSIM_REQUIRED_MODE_INDEX EQUAL -1)
      message(FATAL_ERROR
        "conformance fixture lacks ${FSIM_REQUIRED_FIXTURE_MODE}: "
        "${FSIM_FIXTURE}")
    endif()
  endforeach()
  list(APPEND FSIM_MAPPED_FILES "${FSIM_FIXTURE}")
  list(APPEND FSIM_MAPPED_TESTS "${FSIM_CTEST}")
  list(APPEND FSIM_MAPPED_MODES "${FSIM_MODE_TEXT}")
endforeach()

foreach(FSIM_CTEST IN LISTS FSIM_MAPPED_TESTS)
  list(FIND FSIM_REGISTERED_CTESTS "${FSIM_CTEST}" FSIM_CTEST_INDEX)
  if(FSIM_CTEST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "conformance mapping names an unregistered CTest: ${FSIM_CTEST}")
  endif()
endforeach()

file(GLOB_RECURSE FSIM_TEST_SOURCES LIST_DIRECTORIES FALSE
  "${FSIM_SOURCE_DIR}/tests/*.c"
  "${FSIM_SOURCE_DIR}/tests/*.cpp"
  "${FSIM_SOURCE_DIR}/tests/*.hpp")
set(FSIM_MARKER_IDS)
set(FSIM_MARKER_FILES)
foreach(FSIM_TEST_SOURCE IN LISTS FSIM_TEST_SOURCES)
  file(STRINGS
    "${FSIM_TEST_SOURCE}" FSIM_MARKER_LINES REGEX "FSIM-CONFORMANCE")
  if(NOT FSIM_MARKER_LINES)
    continue()
  endif()
  file(RELATIVE_PATH
    FSIM_RELATIVE_SOURCE "${FSIM_SOURCE_DIR}" "${FSIM_TEST_SOURCE}")
  list(FIND FSIM_MAPPED_FILES "${FSIM_RELATIVE_SOURCE}" FSIM_MAPPING_INDEX)
  if(FSIM_MAPPING_INDEX EQUAL -1)
    message(FATAL_ERROR
      "conformance markers have no corpus mapping: ${FSIM_RELATIVE_SOURCE}")
  endif()
  list(GET FSIM_MAPPED_MODES ${FSIM_MAPPING_INDEX} FSIM_MODE_TEXT)
  string(REPLACE "," ";" FSIM_MODES "${FSIM_MODE_TEXT}")
  list(APPEND FSIM_MARKER_FILES "${FSIM_RELATIVE_SOURCE}")
  foreach(FSIM_MARKER_LINE IN LISTS FSIM_MARKER_LINES)
    string(REGEX MATCH
      "FSIM-CONFORMANCE[ \t]+([A-Z0-9-]+)[ \t]+source=([A-Z0-9-]+)[ \t]+expectation=([^ \t\r\n]+)"
      FSIM_MARKER_MATCH "${FSIM_MARKER_LINE}")
    if(NOT FSIM_MARKER_MATCH)
      message(FATAL_ERROR
        "malformed conformance marker in ${FSIM_RELATIVE_SOURCE}: "
        "${FSIM_MARKER_LINE}")
    endif()
    set(FSIM_MARKER_ID "${CMAKE_MATCH_1}")
    set(FSIM_MARKER_SOURCE "${CMAKE_MATCH_2}")
    set(FSIM_MARKER_EXPECTATION "${CMAKE_MATCH_3}")
    list(FIND FSIM_MARKER_IDS "${FSIM_MARKER_ID}" FSIM_DUPLICATE_ID)
    if(NOT FSIM_DUPLICATE_ID EQUAL -1)
      message(FATAL_ERROR
        "duplicate conformance expectation ID: ${FSIM_MARKER_ID}")
    endif()
    string(SHA256 FSIM_MARKER_ID_HASH "${FSIM_MARKER_ID}")
    if(DEFINED FSIM_REQUIRED_MARKER_${FSIM_MARKER_ID_HASH})
      list(GET FSIM_MAPPED_TESTS ${FSIM_MAPPING_INDEX} FSIM_MARKER_CTEST)
      set(FSIM_ACTUAL_REQUIRED_MARKER
        "${FSIM_MARKER_ID};${FSIM_RELATIVE_SOURCE};${FSIM_MARKER_CTEST};${FSIM_MARKER_SOURCE};${FSIM_MARKER_EXPECTATION}")
      if(NOT FSIM_ACTUAL_REQUIRED_MARKER STREQUAL
          FSIM_REQUIRED_MARKER_${FSIM_MARKER_ID_HASH})
        message(FATAL_ERROR
          "required conformance marker mapping changed: ${FSIM_MARKER_ID}")
      endif()
    endif()
    list(FIND
      FSIM_ALLOWED_SOURCES "${FSIM_MARKER_SOURCE}" FSIM_SOURCE_INDEX)
    if(FSIM_SOURCE_INDEX EQUAL -1)
      message(FATAL_ERROR
        "unsupported conformance source ${FSIM_MARKER_SOURCE}: "
        "${FSIM_MARKER_ID}")
    endif()

    set(FSIM_HAS_EXECUTION_MODE FALSE)
    foreach(FSIM_EXECUTION_MODE IN ITEMS
        interpreter llvm-o0 llvm-o2 c-api plugin compiler tcl compile)
      list(FIND FSIM_MODES "${FSIM_EXECUTION_MODE}" FSIM_MODE_INDEX)
      if(NOT FSIM_MODE_INDEX EQUAL -1)
        set(FSIM_HAS_EXECUTION_MODE TRUE)
      endif()
    endforeach()
    set(FSIM_HAS_ACCEPTANCE_MODE FALSE)
    foreach(FSIM_ACCEPTANCE_MODE IN ITEMS
        frontend elaboration compile c-abi project)
      list(FIND FSIM_MODES "${FSIM_ACCEPTANCE_MODE}" FSIM_MODE_INDEX)
      if(NOT FSIM_MODE_INDEX EQUAL -1)
        set(FSIM_HAS_ACCEPTANCE_MODE TRUE)
      endif()
    endforeach()
    list(FIND FSIM_MODES diagnostic FSIM_DIAGNOSTIC_INDEX)
    list(FIND FSIM_MODES interpreter FSIM_INTERPRETER_INDEX)
    if(FSIM_MARKER_EXPECTATION STREQUAL "execute"
        AND NOT FSIM_HAS_EXECUTION_MODE)
      message(FATAL_ERROR
        "execute expectation lacks an execution mode: ${FSIM_MARKER_ID}")
    elseif(FSIM_MARKER_EXPECTATION STREQUAL "accept"
        AND NOT FSIM_HAS_ACCEPTANCE_MODE)
      message(FATAL_ERROR
        "accept expectation lacks an acceptance mode: ${FSIM_MARKER_ID}")
    endif()
    if(FSIM_MARKER_EXPECTATION MATCHES "^FSIM-"
        OR FSIM_MARKER_EXPECTATION STREQUAL "reject"
        OR FSIM_MARKER_EXPECTATION STREQUAL "runtime-failure"
        OR FSIM_MARKER_EXPECTATION STREQUAL "contain")
      if(FSIM_DIAGNOSTIC_INDEX EQUAL -1)
        message(FATAL_ERROR
          "failure expectation lacks diagnostic evidence: ${FSIM_MARKER_ID}")
      endif()
    endif()
    if(FSIM_MARKER_EXPECTATION STREQUAL "runtime-failure"
        AND FSIM_INTERPRETER_INDEX EQUAL -1)
      message(FATAL_ERROR
        "runtime failure lacks interpreter evidence: ${FSIM_MARKER_ID}")
    endif()
    if(FSIM_MARKER_ID MATCHES "^CF-(SV|VHDL|MIX)-"
        AND FSIM_MARKER_EXPECTATION STREQUAL "execute"
        AND FSIM_INTERPRETER_INDEX EQUAL -1)
      message(FATAL_ERROR
        "language runtime expectation lacks interpreter evidence: "
        "${FSIM_MARKER_ID}")
    endif()

    list(APPEND FSIM_MARKER_IDS "${FSIM_MARKER_ID}")
  endforeach()
endforeach()

foreach(FSIM_MAPPED_FILE IN LISTS FSIM_MAPPED_FILES)
  list(FIND FSIM_MARKER_FILES "${FSIM_MAPPED_FILE}" FSIM_MARKER_FILE_INDEX)
  if(FSIM_MARKER_FILE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "conformance corpus mapping has no markers: ${FSIM_MAPPED_FILE}")
  endif()
endforeach()

foreach(FSIM_REQUIRED_ID IN LISTS FSIM_REQUIRED_MARKER_IDS)
  list(FIND FSIM_MARKER_IDS "${FSIM_REQUIRED_ID}" FSIM_REQUIRED_INDEX)
  if(FSIM_REQUIRED_INDEX EQUAL -1)
    message(FATAL_ERROR
      "required conformance marker is missing: ${FSIM_REQUIRED_ID}")
  endif()
endforeach()
list(LENGTH FSIM_MARKER_IDS FSIM_MARKER_COUNT)

list(REMOVE_DUPLICATES FSIM_ALL_MODES)
foreach(FSIM_REQUIRED_MODE IN LISTS FSIM_ALLOWED_MODES)
  list(FIND FSIM_ALL_MODES "${FSIM_REQUIRED_MODE}" FSIM_REQUIRED_MODE_INDEX)
  if(FSIM_REQUIRED_MODE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "complete conformance corpus lacks ${FSIM_REQUIRED_MODE} evidence")
  endif()
endforeach()

list(LENGTH FSIM_MAPPED_FILES FSIM_FIXTURE_COUNT)
list(REMOVE_DUPLICATES FSIM_MAPPED_TESTS)
list(LENGTH FSIM_MAPPED_TESTS FSIM_CTEST_COUNT)
message(STATUS
  "v1 conformance corpus: ${FSIM_MARKER_COUNT} expectations, "
  "${FSIM_FIXTURE_COUNT} fixtures, ${FSIM_CTEST_COUNT} CTests, "
  "required identities preserved; additions allowed")

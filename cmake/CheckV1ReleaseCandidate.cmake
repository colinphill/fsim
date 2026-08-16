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

set(FSIM_MATRIX "${FSIM_SOURCE_DIR}/docs/feature-matrix.md")
set(FSIM_CORPUS "${FSIM_SOURCE_DIR}/docs/v1-release-candidate-corpus.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_VERILOG_GAP
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_gap_inventory.tsv")
set(FSIM_VERILOG_WIDTH
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_literal_width_inventory.tsv")
set(FSIM_VERILOG_CLOSURE
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/verilog_release_closure.tsv")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_MATRIX}" "${FSIM_CORPUS}" "${FSIM_TEST_CMAKE}"
    "${FSIM_VERILOG_GAP}" "${FSIM_VERILOG_WIDTH}"
    "${FSIM_VERILOG_CLOSURE}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "release-candidate input not found: ${FSIM_INPUT}")
  endif()
endforeach()

foreach(FSIM_GATE IN ITEMS
    CheckV1SystemVerilogRelease.cmake
    CheckV1VhdlRelease.cmake
    CheckV1MixedSystemCRelease.cmake
    CheckV1DifferentialRelease.cmake
    CheckV1PublicRelease.cmake
    CheckV1InventoryRelease.cmake
    CheckV1ResourceRelease.cmake)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_GATE}"
    RESULT_VARIABLE FSIM_GATE_RESULT
    OUTPUT_VARIABLE FSIM_GATE_OUTPUT
    ERROR_VARIABLE FSIM_GATE_ERROR)
  if(NOT FSIM_GATE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "composed release-candidate gate failed: ${FSIM_GATE}\n"
      "${FSIM_GATE_OUTPUT}${FSIM_GATE_ERROR}")
  endif()
endforeach()

file(READ "${FSIM_MATRIX}" FSIM_MATRIX_CONTENTS)
file(READ "${FSIM_CORPUS}" FSIM_CORPUS_CONTENTS)
file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_CONTENTS)
fsim_normalized_text_sha256("${FSIM_VERILOG_GAP}" FSIM_VERILOG_GAP_DIGEST)
fsim_normalized_text_sha256("${FSIM_VERILOG_WIDTH}" FSIM_VERILOG_WIDTH_DIGEST)
fsim_normalized_text_sha256(
  "${FSIM_VERILOG_CLOSURE}" FSIM_VERILOG_CLOSURE_DIGEST)
string(REPLACE "\r\n" "\n" FSIM_MATRIX_CONTENTS "${FSIM_MATRIX_CONTENTS}")
string(SHA256 FSIM_MATRIX_DIGEST "${FSIM_MATRIX_CONTENTS}")

string(REPLACE ";" "<SEMICOLON>" FSIM_MATRIX_LINES "${FSIM_MATRIX_CONTENTS}")
string(REPLACE "\n" ";" FSIM_MATRIX_LINES "${FSIM_MATRIX_LINES}")
set(FSIM_IDS)
set(FSIM_EVIDENCE_PATHS)
set(FSIM_ROW_COUNT 0)
set(FSIM_EVIDENCE_SLOT_COUNT 0)
foreach(FSIM_LINE IN LISTS FSIM_MATRIX_LINES)
  if(NOT FSIM_LINE MATCHES
      "^\\| ((VH|SV|CM|SC|ML|V1-CM|V1-SV|V1-VH)-[0-9]+) \\|")
    continue()
  endif()
  set(FSIM_ID "${CMAKE_MATCH_1}")
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE_INDEX)
  if(NOT FSIM_DUPLICATE_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate release-candidate row: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")

  set(FSIM_FIELDS_TEXT "${FSIM_LINE}")
  string(REPLACE "\\|" "<PIPE>" FSIM_FIELDS_TEXT "${FSIM_FIELDS_TEXT}")
  string(REPLACE "|" ";" FSIM_FIELDS "${FSIM_FIELDS_TEXT}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  math(EXPR FSIM_STATUS_INDEX "${FSIM_FIELD_COUNT} - 6")
  list(GET FSIM_FIELDS ${FSIM_STATUS_INDEX} FSIM_STATUS)
  string(STRIP "${FSIM_STATUS}" FSIM_STATUS)
  if(NOT FSIM_STATUS STREQUAL "execute")
    message(FATAL_ERROR
      "required row ${FSIM_ID} is not release-classified execute: ${FSIM_STATUS}")
  endif()

  foreach(FSIM_OFFSET IN ITEMS 5 4 3 2)
    math(EXPR FSIM_EVIDENCE_INDEX "${FSIM_FIELD_COUNT} - ${FSIM_OFFSET}")
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_EVIDENCE)
    string(STRIP "${FSIM_EVIDENCE}" FSIM_EVIDENCE)
    if(FSIM_EVIDENCE STREQUAL "" OR FSIM_EVIDENCE STREQUAL "—")
      message(FATAL_ERROR "required row ${FSIM_ID} retains an evidence gap")
    endif()
    set(FSIM_CELL_LINK_COUNT 0)
    set(FSIM_LINK_TEXT "${FSIM_EVIDENCE}")
    while(FSIM_LINK_TEXT MATCHES
        "\\]\\((\\.\\./[^)#]+|[A-Za-z0-9_.-]+\\.(md|txt))")
      set(FSIM_TARGET "${CMAKE_MATCH_1}")
      list(APPEND FSIM_EVIDENCE_PATHS "${FSIM_TARGET}")
      set(FSIM_MATCHED_LINK "${CMAKE_MATCH_0}")
      string(REPLACE "${FSIM_MATCHED_LINK}" "" FSIM_LINK_TEXT "${FSIM_LINK_TEXT}")
      math(EXPR FSIM_CELL_LINK_COUNT "${FSIM_CELL_LINK_COUNT} + 1")
      set(FSIM_TARGET_PATH "${FSIM_SOURCE_DIR}/docs/${FSIM_TARGET}")
      if(NOT EXISTS "${FSIM_TARGET_PATH}")
        message(FATAL_ERROR
          "required row ${FSIM_ID} names missing evidence ${FSIM_TARGET}")
      endif()
    endwhile()
    if(FSIM_CELL_LINK_COUNT EQUAL 0)
      message(FATAL_ERROR
        "required row ${FSIM_ID} has an unlinked evidence cell: ${FSIM_EVIDENCE}")
    endif()
    math(EXPR FSIM_EVIDENCE_SLOT_COUNT "${FSIM_EVIDENCE_SLOT_COUNT} + 1")
  endforeach()
  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
endforeach()

list(REMOVE_DUPLICATES FSIM_EVIDENCE_PATHS)
list(SORT FSIM_EVIDENCE_PATHS)
list(LENGTH FSIM_EVIDENCE_PATHS FSIM_EVIDENCE_PATH_COUNT)
set(FSIM_TEST_EVIDENCE_COUNT 0)
set(FSIM_PRODUCTION_EVIDENCE_COUNT 0)
set(FSIM_RELEASE_EVIDENCE_COUNT 0)
foreach(FSIM_TARGET IN LISTS FSIM_EVIDENCE_PATHS)
  if(FSIM_TARGET MATCHES "^\\.\\./tests/")
    math(EXPR FSIM_TEST_EVIDENCE_COUNT "${FSIM_TEST_EVIDENCE_COUNT} + 1")
  elseif(FSIM_TARGET MATCHES "^\\.\\./(src|include)/")
    math(EXPR FSIM_PRODUCTION_EVIDENCE_COUNT
      "${FSIM_PRODUCTION_EVIDENCE_COUNT} + 1")
  else()
    math(EXPR FSIM_RELEASE_EVIDENCE_COUNT "${FSIM_RELEASE_EVIDENCE_COUNT} + 1")
  endif()
endforeach()
list(JOIN FSIM_EVIDENCE_PATHS "\n" FSIM_EVIDENCE_CANONICAL)
string(SHA256 FSIM_EVIDENCE_DIGEST "${FSIM_EVIDENCE_CANONICAL}\n")

set(FSIM_EXPECTED_VALUES
  FSIM_ROW_COUNT 1294
  FSIM_EVIDENCE_SLOT_COUNT 5176
  FSIM_EVIDENCE_PATH_COUNT 622
  FSIM_TEST_EVIDENCE_COUNT 275
  FSIM_PRODUCTION_EVIDENCE_COUNT 321
  FSIM_RELEASE_EVIDENCE_COUNT 26)
while(FSIM_EXPECTED_VALUES)
  list(POP_FRONT FSIM_EXPECTED_VALUES FSIM_VALUE_NAME FSIM_VALUE_EXPECTED)
  if(NOT ${FSIM_VALUE_NAME} EQUAL FSIM_VALUE_EXPECTED)
    message(FATAL_ERROR
      "release-candidate ${FSIM_VALUE_NAME} changed: expected "
      "${FSIM_VALUE_EXPECTED}, found ${${FSIM_VALUE_NAME}}")
  endif()
endwhile()

set(FSIM_EXPECTED_MATRIX_DIGEST
  "e4860fbf68b82f6b7fcd4714f3af93e67cf425682a5be258680eec6734cca311")
set(FSIM_EXPECTED_EVIDENCE_DIGEST
  "4f3f7cfb428f6bac7bb6acd5f81b8176240ed7a0f19940da9880e2c3e41e8fb6")
if(NOT FSIM_MATRIX_DIGEST STREQUAL FSIM_EXPECTED_MATRIX_DIGEST
    OR NOT FSIM_EVIDENCE_DIGEST STREQUAL FSIM_EXPECTED_EVIDENCE_DIGEST)
  message(FATAL_ERROR
    "release-candidate digest changed: matrix=${FSIM_MATRIX_DIGEST}, "
    "evidence=${FSIM_EVIDENCE_DIGEST}")
endif()

foreach(FSIM_TOKEN IN ITEMS
    "matrix-rows: 1294"
    "matrix-sha256: ${FSIM_MATRIX_DIGEST}"
    "evidence-slots: 5176"
    "evidence-paths: 622"
    "evidence-sha256: ${FSIM_EVIDENCE_DIGEST}"
    "verilog-gap-sha256: ${FSIM_VERILOG_GAP_DIGEST}"
    "verilog-width-sha256: ${FSIM_VERILOG_WIDTH_DIGEST}"
    "verilog-closure-sha256: ${FSIM_VERILOG_CLOSURE_DIGEST}"
    "test-evidence-paths: 275"
    "production-evidence-paths: 321"
    "release-evidence-paths: 26"
    "runtime-evidence-paths: 145"
    "corpus-ctests: 36"
    "required-status: execute"
    "B130-T9-ROWS"
    "B130-T9-CLASSIFICATION"
    "B130-T9-EVIDENCE"
    "B130-T9-CORPUS"
    "B130-T9-DIGEST")
  string(FIND "${FSIM_CORPUS_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "release-candidate corpus omits ${FSIM_TOKEN}")
  endif()
endforeach()
string(FIND
  "${FSIM_TEST_CMAKE_CONTENTS}"
  "NAME fsim.v1-release-candidate"
  FSIM_REGISTRATION_INDEX)
if(FSIM_REGISTRATION_INDEX EQUAL -1)
  message(FATAL_ERROR "fsim.v1-release-candidate is not registered")
endif()

message(STATUS
  "final release candidate: 1294 execute rows, 5176 linked evidence cells, "
  "622 exact paths (275 test, 321 production, 26 release), 145 runtime files, "
  "and 36 corpus CTests")

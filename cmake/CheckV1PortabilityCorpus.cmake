# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CORPUS "${FSIM_SOURCE_DIR}/docs/v1-portability-corpus.txt")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
if(NOT EXISTS "${FSIM_CORPUS}" OR NOT EXISTS "${FSIM_TEST_CMAKE}")
  message(FATAL_ERROR "v1 portability corpus inputs are missing")
endif()
file(GLOB_RECURSE
  FSIM_TEST_CMAKE_FILES
  "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt"
  "${FSIM_SOURCE_DIR}/tests/*/CMakeLists.txt")
set(FSIM_TEST_CMAKE_CONTENTS "")
foreach(FSIM_CMAKE_FILE IN LISTS FSIM_TEST_CMAKE_FILES)
  file(READ "${FSIM_CMAKE_FILE}" FSIM_CMAKE_CONTENT)
  string(APPEND FSIM_TEST_CMAKE_CONTENTS "\n${FSIM_CMAKE_CONTENT}")
endforeach()
file(STRINGS "${FSIM_CORPUS}" FSIM_LINES)

set(FSIM_IDS "")
set(FSIM_ALL_MODES "")
set(FSIM_ROW_COUNT 0)
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE MATCHES "^#" OR FSIM_LINE STREQUAL "")
    continue()
  endif()
  string(REPLACE "|" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 6)
    message(FATAL_ERROR "malformed portability row: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_SURFACE)
  list(GET FSIM_FIELDS 2 FSIM_MODES)
  list(GET FSIM_FIELDS 3 FSIM_CTEST)
  list(GET FSIM_FIELDS 4 FSIM_EVIDENCE)
  list(GET FSIM_FIELDS 5 FSIM_MARKER)
  if(NOT FSIM_ID MATCHES "^PORT-[0-9][0-9][0-9]$")
    message(FATAL_ERROR "invalid portability ID: ${FSIM_ID}")
  endif()
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE_INDEX)
  if(NOT FSIM_DUPLICATE_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate portability ID: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  string(APPEND FSIM_ALL_MODES "+${FSIM_MODES}")
  string(FIND "${FSIM_TEST_CMAKE_CONTENTS}" "${FSIM_CTEST}" FSIM_CTEST_INDEX)
  if(FSIM_CTEST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "${FSIM_ID} names an unregistered CTest: ${FSIM_CTEST}")
  endif()
  set(FSIM_EVIDENCE_PATH "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
  if(NOT EXISTS "${FSIM_EVIDENCE_PATH}")
    message(FATAL_ERROR
      "${FSIM_ID} evidence file is missing: ${FSIM_EVIDENCE}")
  endif()
  file(READ "${FSIM_EVIDENCE_PATH}" FSIM_EVIDENCE_CONTENTS)
  string(FIND "${FSIM_EVIDENCE_CONTENTS}" "${FSIM_MARKER}" FSIM_MARKER_INDEX)
  if(FSIM_MARKER_INDEX EQUAL -1)
    message(FATAL_ERROR
      "${FSIM_ID} lost marker '${FSIM_MARKER}' in ${FSIM_EVIDENCE}")
  endif()
  math(EXPR FSIM_ROW_COUNT "${FSIM_ROW_COUNT} + 1")
endforeach()

if(NOT FSIM_ROW_COUNT EQUAL 20)
  message(FATAL_ERROR
    "expected 20 exact portability rows, found ${FSIM_ROW_COUNT}")
endif()
foreach(FSIM_REQUIRED_MODE IN ITEMS
    Debug Release interpreter LLVM-O0 LLVM-O2 cold warm edit api abi plugin
    callback debugger vcd files path newline resources)
  string(FIND "${FSIM_ALL_MODES}" "${FSIM_REQUIRED_MODE}" FSIM_MODE_INDEX)
  if(FSIM_MODE_INDEX EQUAL -1)
    message(FATAL_ERROR
      "portability corpus lacks required mode: ${FSIM_REQUIRED_MODE}")
  endif()
endforeach()
set(FSIM_REQUIRED_IDS
  PORT-001 PORT-002 PORT-003 PORT-004 PORT-005
  PORT-006 PORT-007 PORT-008 PORT-009 PORT-010
  PORT-011 PORT-012 PORT-013 PORT-014 PORT-015
  PORT-016 PORT-017 PORT-018 PORT-019 PORT-020)
foreach(FSIM_REQUIRED_ID IN LISTS FSIM_REQUIRED_IDS)
  list(FIND FSIM_IDS "${FSIM_REQUIRED_ID}" FSIM_REQUIRED_INDEX)
  if(FSIM_REQUIRED_INDEX EQUAL -1)
    message(FATAL_ERROR "portability corpus lacks ${FSIM_REQUIRED_ID}")
  endif()
endforeach()

message(STATUS
  "v1 portability corpus: 20 exact rows cover Debug/Release, interpreter, "
  "LLVM O0/O2, cold/warm/edit, API/ABI, plug-in, debugger/VCD, files, "
  "path/newline, callbacks, and bounded resources")

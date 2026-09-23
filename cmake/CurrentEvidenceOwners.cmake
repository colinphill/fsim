# SPDX-License-Identifier: Apache-2.0

function(fsim_current_registered_ctests FSIM_NAMES_OUT)
  foreach(FSIM_VARIABLE IN ITEMS FSIM_BINARY_DIR FSIM_CTEST_COMMAND)
    if(NOT DEFINED ${FSIM_VARIABLE}
       OR "${${FSIM_VARIABLE}}" STREQUAL "")
      message(FATAL_ERROR "${FSIM_VARIABLE} is required")
    endif()
  endforeach()
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}" --test-dir "${FSIM_BINARY_DIR}"
            --show-only=json-v1
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_JSON
    ERROR_VARIABLE FSIM_ERROR)
  if(NOT FSIM_STATUS EQUAL 0)
    message(FATAL_ERROR "cannot enumerate registered CTests: ${FSIM_ERROR}")
  endif()
  string(JSON FSIM_COUNT ERROR_VARIABLE FSIM_JSON_ERROR
    LENGTH "${FSIM_JSON}" tests)
  if(FSIM_JSON_ERROR OR FSIM_COUNT LESS 1)
    message(FATAL_ERROR "CTest returned no usable inventory: ${FSIM_JSON_ERROR}")
  endif()
  math(EXPR FSIM_LAST "${FSIM_COUNT} - 1")
  set(FSIM_NAMES)
  foreach(FSIM_INDEX RANGE 0 ${FSIM_LAST})
    string(JSON FSIM_NAME ERROR_VARIABLE FSIM_JSON_ERROR
      GET "${FSIM_JSON}" tests ${FSIM_INDEX} name)
    if(FSIM_JSON_ERROR)
      message(FATAL_ERROR "invalid CTest name at index ${FSIM_INDEX}")
    endif()
    list(FIND FSIM_NAMES "${FSIM_NAME}" FSIM_DUPLICATE)
    if(NOT FSIM_DUPLICATE EQUAL -1)
      message(FATAL_ERROR "duplicate registered CTest name: ${FSIM_NAME}")
    endif()
    list(APPEND FSIM_NAMES "${FSIM_NAME}")
  endforeach()
  set(${FSIM_NAMES_OUT} "${FSIM_NAMES}" PARENT_SCOPE)
endfunction()

function(fsim_current_evidence_file FSIM_RELATIVE_PATH)
  if(FSIM_RELATIVE_PATH STREQUAL ""
     OR IS_ABSOLUTE "${FSIM_RELATIVE_PATH}"
     OR FSIM_RELATIVE_PATH MATCHES "^[A-Za-z]:"
     OR FSIM_RELATIVE_PATH MATCHES "\\\\"
     OR FSIM_RELATIVE_PATH MATCHES "(^|/)\\.\\.?(/|$)"
     OR FSIM_RELATIVE_PATH MATCHES "//"
     OR FSIM_RELATIVE_PATH MATCHES "/$")
    message(FATAL_ERROR "unsafe evidence path: ${FSIM_RELATIVE_PATH}")
  endif()
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE_PATH}")
  if(NOT EXISTS "${FSIM_PATH}" OR IS_DIRECTORY "${FSIM_PATH}")
    message(FATAL_ERROR "evidence file is missing: ${FSIM_RELATIVE_PATH}")
  endif()
  file(REAL_PATH "${FSIM_SOURCE_DIR}" FSIM_ROOT)
  file(REAL_PATH "${FSIM_PATH}" FSIM_CANONICAL)
  cmake_path(IS_PREFIX FSIM_ROOT "${FSIM_CANONICAL}"
    NORMALIZE FSIM_WITHIN_ROOT)
  if(NOT FSIM_WITHIN_ROOT)
    message(FATAL_ERROR "evidence path escapes source tree: ${FSIM_RELATIVE_PATH}")
  endif()
endfunction()

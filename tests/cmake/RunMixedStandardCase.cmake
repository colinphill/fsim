# SPDX-License-Identifier: Apache-2.0
#
# Compiles Verilog/SystemVerilog files under different standards into one
# workspace, elaborates a top and simulates it on one engine, then compares
# the lines that start with "MIX " with the expected text.
#
# FSIM_STEPS lists the compile steps separated by "|"; each step is
# "file@standard" or "file@standard@compatibility-switch". FSIM_SOURCE_DIR
# holds the files. FSIM_FAIL_MESSAGE, when set, is text the last compile step
# must report while it fails; the case ends there.

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_STEPS FSIM_SOURCE_DIR
    FSIM_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

# The workspace is a fixed directory of the build tree; recompiling replaces
# its managed objects.
file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")

string(REPLACE "|" ";" FSIM_STEP_LIST "${FSIM_STEPS}")
list(LENGTH FSIM_STEP_LIST FSIM_STEP_COUNT)
set(FSIM_STEP_INDEX 0)
foreach(FSIM_STEP IN LISTS FSIM_STEP_LIST)
  math(EXPR FSIM_STEP_INDEX "${FSIM_STEP_INDEX} + 1")
  string(REPLACE "@" ";" FSIM_PARTS "${FSIM_STEP}")
  list(GET FSIM_PARTS 0 FSIM_FILE)
  list(GET FSIM_PARTS 1 FSIM_STANDARD)
  configure_file("${FSIM_SOURCE_DIR}/${FSIM_FILE}"
    "${FSIM_WORK_DIR}/${FSIM_FILE}" COPYONLY)
  set(FSIM_COMPAT "")
  list(LENGTH FSIM_PARTS FSIM_PART_COUNT)
  if(FSIM_PART_COUNT GREATER 2)
    list(GET FSIM_PARTS 2 FSIM_SWITCH)
    set(FSIM_COMPAT --compatibility "${FSIM_SWITCH}")
  endif()
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" compile --standard "${FSIM_STANDARD}"
      ${FSIM_COMPAT} "${FSIM_FILE}"
    WORKING_DIRECTORY "${FSIM_WORK_DIR}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  if(DEFINED FSIM_FAIL_MESSAGE AND NOT "${FSIM_FAIL_MESSAGE}" STREQUAL ""
      AND FSIM_STEP_INDEX EQUAL FSIM_STEP_COUNT)
    if(FSIM_RESULT EQUAL 0)
      message(FATAL_ERROR
        "compile of ${FSIM_FILE} succeeded, wanted a failure:\n${FSIM_OUTPUT}${FSIM_ERROR}")
    endif()
    string(FIND "${FSIM_OUTPUT}${FSIM_ERROR}" "${FSIM_FAIL_MESSAGE}"
      FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "compile of ${FSIM_FILE} did not report \"${FSIM_FAIL_MESSAGE}\":\n${FSIM_OUTPUT}${FSIM_ERROR}")
    endif()
    return()
  endif()
  if(NOT FSIM_RESULT EQUAL 0)
    message(FATAL_ERROR
      "compile of ${FSIM_FILE} (${FSIM_STANDARD}) returned ${FSIM_RESULT}:\n${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
endforeach()

foreach(FSIM_REQUIRED IN ITEMS FSIM_TOP FSIM_ENGINE FSIM_EXPECTED)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${FSIM_EXECUTABLE}" elaborate "${FSIM_TOP}"
  WORKING_DIRECTORY "${FSIM_WORK_DIR}"
  RESULT_VARIABLE FSIM_RESULT
  OUTPUT_VARIABLE FSIM_OUTPUT
  ERROR_VARIABLE FSIM_ERROR)
if(NOT FSIM_RESULT EQUAL 0)
  message(FATAL_ERROR
    "elaborate ${FSIM_TOP} returned ${FSIM_RESULT}:\n${FSIM_OUTPUT}${FSIM_ERROR}")
endif()
execute_process(
  COMMAND "${FSIM_EXECUTABLE}" simulate --engine "${FSIM_ENGINE}"
  WORKING_DIRECTORY "${FSIM_WORK_DIR}"
  RESULT_VARIABLE FSIM_RESULT
  OUTPUT_VARIABLE FSIM_OUTPUT
  ERROR_VARIABLE FSIM_ERROR)
if(NOT FSIM_RESULT EQUAL 0)
  message(FATAL_ERROR
    "simulate returned ${FSIM_RESULT}:\n${FSIM_OUTPUT}${FSIM_ERROR}")
endif()

string(REPLACE "\r\n" "\n" FSIM_OUTPUT "${FSIM_OUTPUT}")
string(REPLACE "\n" ";" FSIM_LINES "${FSIM_OUTPUT}")
set(FSIM_TRACE "")
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE MATCHES "^MIX ")
    string(APPEND FSIM_TRACE "${FSIM_LINE}\n")
  endif()
endforeach()
if(NOT FSIM_TRACE STREQUAL "${FSIM_EXPECTED}\n")
  message(FATAL_ERROR
    "output differs\n--- actual\n${FSIM_TRACE}--- expected\n${FSIM_EXPECTED}\n")
endif()

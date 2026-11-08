# SPDX-License-Identifier: Apache-2.0
#
# Runs one SystemVerilog source through `fsim compile`, `fsim elaborate` and
# `fsim simulate --engine ENGINE`, and compares the whole standard output,
# less the final "simulation completed" line, with an expected file.
# FSIM_ELABORATE_MESSAGE, when set, is text a failing `fsim elaborate` must
# report (the case ends there); FSIM_SIMULATE_MESSAGE, when set, is text a
# failing `fsim simulate` (exit status 1) must report.

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_SOURCE FSIM_TOP
    FSIM_ENGINE FSIM_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
get_filename_component(FSIM_SOURCE_NAME "${FSIM_SOURCE}" NAME)
configure_file("${FSIM_SOURCE}" "${FSIM_WORK_DIR}/${FSIM_SOURCE_NAME}" COPYONLY)

function(fsim_step FSIM_STEP_NAME FSIM_WANTED)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${FSIM_WORK_DIR}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  if(NOT FSIM_RESULT EQUAL FSIM_WANTED)
    message(FATAL_ERROR
      "fsim ${FSIM_STEP_NAME} returned ${FSIM_RESULT}, wanted ${FSIM_WANTED}:\n${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
  set(FSIM_STEP_OUTPUT "${FSIM_OUTPUT}" PARENT_SCOPE)
  set(FSIM_STEP_ERROR "${FSIM_ERROR}" PARENT_SCOPE)
endfunction()

function(fsim_require_message FSIM_STEP_NAME FSIM_MESSAGE)
  string(FIND "${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}" "${FSIM_MESSAGE}"
    FSIM_OFFSET)
  if(FSIM_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "fsim ${FSIM_STEP_NAME} did not report \"${FSIM_MESSAGE}\":\n${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}")
  endif()
endfunction()

fsim_step(compile 0 compile "${FSIM_SOURCE_NAME}")
if(DEFINED FSIM_ELABORATE_MESSAGE AND NOT FSIM_ELABORATE_MESSAGE STREQUAL "")
  fsim_step(elaborate 1 elaborate "${FSIM_TOP}")
  fsim_require_message(elaborate "${FSIM_ELABORATE_MESSAGE}")
  return()
endif()
fsim_step(elaborate 0 elaborate "${FSIM_TOP}")
if(DEFINED FSIM_SIMULATE_MESSAGE AND NOT FSIM_SIMULATE_MESSAGE STREQUAL "")
  fsim_step(simulate 1 simulate --engine "${FSIM_ENGINE}")
  fsim_require_message(simulate "${FSIM_SIMULATE_MESSAGE}")
  return()
endif()
fsim_step(simulate 0 simulate --engine "${FSIM_ENGINE}")

if(NOT DEFINED FSIM_EXPECTED OR FSIM_EXPECTED STREQUAL "")
  message(FATAL_ERROR "FSIM_EXPECTED is required")
endif()
string(REPLACE "\r\n" "\n" FSIM_TEXT "${FSIM_STEP_OUTPUT}")
# The final status line carries the stop time, not the checked output.
string(REGEX REPLACE "(^|\n)simulation completed[^\n]*\n?" "\\1"
  FSIM_ACTUAL "${FSIM_TEXT}")
file(WRITE "${FSIM_WORK_DIR}/actual.out" "${FSIM_ACTUAL}")
file(READ "${FSIM_EXPECTED}" FSIM_EXPECTED_TEXT)
if(NOT FSIM_ACTUAL STREQUAL FSIM_EXPECTED_TEXT)
  message(FATAL_ERROR
    "output differs from ${FSIM_EXPECTED}\n--- actual\n${FSIM_ACTUAL}--- expected\n${FSIM_EXPECTED_TEXT}")
endif()

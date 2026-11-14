# SPDX-License-Identifier: Apache-2.0
#
# Runs one SDF backannotation case of a VITAL model through `fsim compile`,
# `fsim elaborate --sdf` and `fsim simulate`, and compares the messages the
# design reports with an expected trace.
#
# The trace holds the text of every VHDL report (the part after
# "[FSIM-HDL-REPORT]: ") and every output line that starts with "TRACE ",
# in the order the run prints them.
#
# FSIM_SOURCES lists the sources separated by "|", compiled one per step in
# order; a VHDL source is compiled as VHDL-2008. FSIM_SDF lists the SDF files
# separated by "|" that `fsim elaborate` annotates, FSIM_SDF_ROOT is the
# `--sdf-root` and FSIM_DELAY_MODE the `--delay-mode` (typ when unset).
# FSIM_ELABORATE_MESSAGE, when set, is text a failing `fsim elaborate` must
# report; the case ends there. FSIM_ELABORATE_WARNING, when set, lists texts
# ("|"-separated) that a succeeding `fsim elaborate` must report. FSIM_SIMULATE_SDF lists SDF files given to
# `fsim simulate` instead, with FSIM_SIMULATE_MESSAGE the text that failing
# run must report. FSIM_DEBUG_COMMAND, when set, is Tcl text that `fsim debug`
# evaluates on the elaborated snapshot instead of a simulation; its output
# must contain FSIM_SIMULATE_MESSAGE and the case ends there.

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_SOURCES FSIM_TOP
    FSIM_ENGINE FSIM_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/FsimTestWorkspace.cmake")
# FSIM_WORK_DIR names the case; each run works in a new folder of its own,
# removed when the case passes.
fsim_test_case_workspace(FSIM_WORK_DIR "${FSIM_WORK_DIR}")
if(NOT DEFINED FSIM_DELAY_MODE OR "${FSIM_DELAY_MODE}" STREQUAL "")
  set(FSIM_DELAY_MODE typ)
endif()

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

function(fsim_copy_inputs FSIM_LIST_VARIABLE FSIM_NAMES_VARIABLE)
  string(REPLACE "|" ";" FSIM_FILES "${${FSIM_LIST_VARIABLE}}")
  set(FSIM_NAMES "")
  foreach(FSIM_FILE IN LISTS FSIM_FILES)
    get_filename_component(FSIM_NAME "${FSIM_FILE}" NAME)
    configure_file("${FSIM_FILE}" "${FSIM_WORK_DIR}/${FSIM_NAME}" COPYONLY)
    list(APPEND FSIM_NAMES "${FSIM_NAME}")
  endforeach()
  set(${FSIM_NAMES_VARIABLE} "${FSIM_NAMES}" PARENT_SCOPE)
endfunction()

fsim_copy_inputs(FSIM_SOURCES FSIM_SOURCE_NAMES)
foreach(FSIM_SOURCE_NAME IN LISTS FSIM_SOURCE_NAMES)
  if(FSIM_SOURCE_NAME MATCHES "\\.vhd$")
    fsim_step(compile 0 compile --standard 2008 "${FSIM_SOURCE_NAME}")
  else()
    fsim_step(compile 0 compile "${FSIM_SOURCE_NAME}")
  endif()
endforeach()

set(FSIM_SDF_ARGUMENTS "")
if(DEFINED FSIM_SDF AND NOT "${FSIM_SDF}" STREQUAL "")
  fsim_copy_inputs(FSIM_SDF FSIM_SDF_NAMES)
  foreach(FSIM_SDF_NAME IN LISTS FSIM_SDF_NAMES)
    list(APPEND FSIM_SDF_ARGUMENTS --sdf "${FSIM_SDF_NAME}")
  endforeach()
  list(APPEND FSIM_SDF_ARGUMENTS --sdf-root "${FSIM_SDF_ROOT}"
    --delay-mode "${FSIM_DELAY_MODE}")
endif()

set(FSIM_ELABORATE_STATUS 0)
if(DEFINED FSIM_ELABORATE_MESSAGE AND NOT FSIM_ELABORATE_MESSAGE STREQUAL "")
  set(FSIM_ELABORATE_STATUS 1)
endif()
fsim_step(elaborate ${FSIM_ELABORATE_STATUS} elaborate
  ${FSIM_SDF_ARGUMENTS} "${FSIM_TOP}")
if(FSIM_ELABORATE_STATUS EQUAL 1)
  string(FIND "${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}"
    "${FSIM_ELABORATE_MESSAGE}" FSIM_MESSAGE_OFFSET)
  if(FSIM_MESSAGE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "fsim elaborate did not report \"${FSIM_ELABORATE_MESSAGE}\":\n${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}")
  endif()
  fsim_test_workspace_cleanup()
  return()
endif()
if(DEFINED FSIM_ELABORATE_WARNING AND NOT "${FSIM_ELABORATE_WARNING}" STREQUAL "")
  string(REPLACE "|" ";" FSIM_WARNINGS "${FSIM_ELABORATE_WARNING}")
  foreach(FSIM_WARNING IN LISTS FSIM_WARNINGS)
    string(FIND "${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}"
      "${FSIM_WARNING}" FSIM_MESSAGE_OFFSET)
    if(FSIM_MESSAGE_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "fsim elaborate did not report \"${FSIM_WARNING}\":\n${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}")
    endif()
  endforeach()
endif()

if(DEFINED FSIM_DEBUG_COMMAND AND NOT "${FSIM_DEBUG_COMMAND}" STREQUAL "")
  fsim_copy_inputs(FSIM_SIMULATE_SDF FSIM_SIMULATE_SDF_NAMES)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" debug --engine "${FSIM_ENGINE}"
      --command "${FSIM_DEBUG_COMMAND}"
    WORKING_DIRECTORY "${FSIM_WORK_DIR}"
    OUTPUT_VARIABLE FSIM_DEBUG_OUTPUT
    ERROR_VARIABLE FSIM_DEBUG_ERROR)
  string(FIND "${FSIM_DEBUG_OUTPUT}${FSIM_DEBUG_ERROR}"
    "${FSIM_SIMULATE_MESSAGE}" FSIM_MESSAGE_OFFSET)
  if(FSIM_MESSAGE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "fsim debug did not report \"${FSIM_SIMULATE_MESSAGE}\":\n${FSIM_DEBUG_OUTPUT}${FSIM_DEBUG_ERROR}")
  endif()
  fsim_test_workspace_cleanup()
  return()
endif()
set(FSIM_SIMULATE_ARGUMENTS --engine "${FSIM_ENGINE}")
set(FSIM_SIMULATE_STATUS 0)
if(DEFINED FSIM_SIMULATE_SDF AND NOT "${FSIM_SIMULATE_SDF}" STREQUAL "")
  fsim_copy_inputs(FSIM_SIMULATE_SDF FSIM_SIMULATE_SDF_NAMES)
  foreach(FSIM_SDF_NAME IN LISTS FSIM_SIMULATE_SDF_NAMES)
    list(APPEND FSIM_SIMULATE_ARGUMENTS --sdf "${FSIM_SDF_NAME}")
  endforeach()
  list(APPEND FSIM_SIMULATE_ARGUMENTS --sdf-root "${FSIM_SDF_ROOT}"
    --delay-mode "${FSIM_DELAY_MODE}")
  set(FSIM_SIMULATE_STATUS 1)
endif()
fsim_step(simulate ${FSIM_SIMULATE_STATUS} simulate ${FSIM_SIMULATE_ARGUMENTS})
if(FSIM_SIMULATE_STATUS EQUAL 1)
  string(FIND "${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}"
    "${FSIM_SIMULATE_MESSAGE}" FSIM_MESSAGE_OFFSET)
  if(FSIM_MESSAGE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "fsim simulate did not report \"${FSIM_SIMULATE_MESSAGE}\":\n${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}")
  endif()
  fsim_test_workspace_cleanup()
  return()
endif()

string(REPLACE "\r\n" "\n" FSIM_OUTPUT "${FSIM_STEP_OUTPUT}\n${FSIM_STEP_ERROR}")
string(REPLACE "\n" ";" FSIM_LINES "${FSIM_OUTPUT}")
set(FSIM_TRACE "")
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE MATCHES "\\[FSIM-HDL-REPORT\\]: (.*)$")
    string(APPEND FSIM_TRACE "${CMAKE_MATCH_1}\n")
  elseif(FSIM_LINE MATCHES "^TRACE ")
    string(APPEND FSIM_TRACE "${FSIM_LINE}\n")
  endif()
endforeach()
# The trace as run, for inspecting a difference without line wrapping.
file(WRITE "${FSIM_WORK_DIR}/actual.trace" "${FSIM_TRACE}")
file(READ "${FSIM_EXPECTED}" FSIM_EXPECTED_TRACE)
if(NOT FSIM_TRACE STREQUAL FSIM_EXPECTED_TRACE)
  message(FATAL_ERROR
    "trace differs from ${FSIM_EXPECTED}\n--- actual\n${FSIM_TRACE}--- expected\n${FSIM_EXPECTED_TRACE}")
endif()
fsim_test_workspace_cleanup()

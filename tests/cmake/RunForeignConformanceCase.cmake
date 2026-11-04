# SPDX-License-Identifier: Apache-2.0
#
# Runs one VPI, VHPI or DPI-C conformance plug-in on a small design through
# `fsim compile`, `fsim elaborate` and `fsim simulate`, and compares the
# lines the plug-in marks with "#c " against an expected trace.
# FSIM_SOURCE may list several files separated by "|", compiled one per
# step in order (one language per compile); FSIM_INTERFACE "mixed" loads
# the plug-in as both a VPI and a VHPI library. FSIM_SIMULATE_STATUS is the
# exit status `fsim simulate` must return (0 when unset); a true
# FSIM_ELABORATE_PLUGIN also loads the VPI plug-in at `fsim elaborate`; a
# non-empty FSIM_SDF names an SDF file that elaboration annotates.
# FSIM_SIMULATE_MESSAGE, when set, is text `fsim simulate` must report.

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_PLUGIN FSIM_INTERFACE
    FSIM_SOURCE FSIM_TOP FSIM_ENGINE FSIM_EXPECTED FSIM_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
string(REPLACE "|" ";" FSIM_SOURCES "${FSIM_SOURCE}")
set(FSIM_SOURCE_NAMES "")
foreach(FSIM_SOURCE_FILE IN LISTS FSIM_SOURCES)
  get_filename_component(FSIM_SOURCE_NAME "${FSIM_SOURCE_FILE}" NAME)
  configure_file("${FSIM_SOURCE_FILE}" "${FSIM_WORK_DIR}/${FSIM_SOURCE_NAME}"
    COPYONLY)
  list(APPEND FSIM_SOURCE_NAMES "${FSIM_SOURCE_NAME}")
endforeach()

function(fsim_step FSIM_STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${FSIM_WORK_DIR}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  set(FSIM_WANTED 0)
  if(FSIM_STEP_NAME STREQUAL "simulate" AND DEFINED FSIM_SIMULATE_STATUS
      AND NOT FSIM_SIMULATE_STATUS STREQUAL "")
    set(FSIM_WANTED "${FSIM_SIMULATE_STATUS}")
  endif()
  if(NOT FSIM_RESULT EQUAL FSIM_WANTED)
    message(FATAL_ERROR
      "fsim ${FSIM_STEP_NAME} failed (${FSIM_RESULT}):\n${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
  set(FSIM_STEP_OUTPUT "${FSIM_OUTPUT}" PARENT_SCOPE)
  set(FSIM_STEP_ERROR "${FSIM_ERROR}" PARENT_SCOPE)
endfunction()

foreach(FSIM_SOURCE_NAME IN LISTS FSIM_SOURCE_NAMES)
  fsim_step(compile compile "${FSIM_SOURCE_NAME}")
endforeach()
set(FSIM_ELABORATE_ARGUMENTS "")
if(DEFINED FSIM_SDF AND NOT "${FSIM_SDF}" STREQUAL "")
  get_filename_component(FSIM_SDF_NAME "${FSIM_SDF}" NAME)
  configure_file("${FSIM_SDF}" "${FSIM_WORK_DIR}/${FSIM_SDF_NAME}" COPYONLY)
  list(APPEND FSIM_ELABORATE_ARGUMENTS --sdf "${FSIM_SDF_NAME}")
endif()
if(FSIM_ELABORATE_PLUGIN)
  fsim_step(elaborate elaborate --vpi "${FSIM_PLUGIN}"
    ${FSIM_ELABORATE_ARGUMENTS} "${FSIM_TOP}")
else()
  fsim_step(elaborate elaborate ${FSIM_ELABORATE_ARGUMENTS} "${FSIM_TOP}")
endif()
separate_arguments(FSIM_SIMULATE_ARGUMENTS UNIX_COMMAND "${FSIM_SIMULATE_ARGS}")
if(FSIM_INTERFACE STREQUAL "mixed")
  set(FSIM_LIBRARY_ARGUMENTS --vpi "${FSIM_PLUGIN}" --vhpi "${FSIM_PLUGIN}")
elseif(FSIM_INTERFACE STREQUAL "dpi")
  set(FSIM_LIBRARY_ARGUMENTS --sv-lib "${FSIM_PLUGIN}")
else()
  set(FSIM_LIBRARY_ARGUMENTS "--${FSIM_INTERFACE}" "${FSIM_PLUGIN}")
endif()
fsim_step(simulate simulate --engine "${FSIM_ENGINE}"
  ${FSIM_LIBRARY_ARGUMENTS} ${FSIM_SIMULATE_ARGUMENTS})

# FSIM_SIMULATE_MESSAGE is text the run must report, in either stream.
if(DEFINED FSIM_SIMULATE_MESSAGE AND NOT FSIM_SIMULATE_MESSAGE STREQUAL "")
  string(FIND "${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}"
    "${FSIM_SIMULATE_MESSAGE}" FSIM_MESSAGE_OFFSET)
  if(FSIM_MESSAGE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "fsim simulate did not report \"${FSIM_SIMULATE_MESSAGE}\":\n${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}")
  endif()
endif()

string(REPLACE "\r\n" "\n" FSIM_OUTPUT "${FSIM_STEP_OUTPUT}")
string(REPLACE "\n" ";" FSIM_LINES "${FSIM_OUTPUT}")
set(FSIM_TRACE "")
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE MATCHES "^#c ")
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

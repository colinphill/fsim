# SPDX-License-Identifier: Apache-2.0
#
# Runs one VPI or VHPI conformance plug-in on a small design through
# `fsim compile`, `fsim elaborate` and `fsim simulate`, and compares the
# lines the plug-in marks with "#c " against an expected trace.

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_PLUGIN FSIM_INTERFACE
    FSIM_SOURCE FSIM_TOP FSIM_ENGINE FSIM_EXPECTED FSIM_WORK_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
get_filename_component(FSIM_SOURCE_NAME "${FSIM_SOURCE}" NAME)
configure_file("${FSIM_SOURCE}" "${FSIM_WORK_DIR}/${FSIM_SOURCE_NAME}" COPYONLY)

function(fsim_step FSIM_STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${FSIM_WORK_DIR}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  if(NOT FSIM_RESULT EQUAL 0)
    message(FATAL_ERROR
      "fsim ${FSIM_STEP_NAME} failed (${FSIM_RESULT}):\n${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
  set(FSIM_STEP_OUTPUT "${FSIM_OUTPUT}" PARENT_SCOPE)
endfunction()

fsim_step(compile compile "${FSIM_SOURCE_NAME}")
fsim_step(elaborate elaborate "${FSIM_TOP}")
separate_arguments(FSIM_SIMULATE_ARGUMENTS UNIX_COMMAND "${FSIM_SIMULATE_ARGS}")
fsim_step(simulate simulate --engine "${FSIM_ENGINE}"
  "--${FSIM_INTERFACE}" "${FSIM_PLUGIN}" ${FSIM_SIMULATE_ARGUMENTS})

string(REPLACE "\r\n" "\n" FSIM_OUTPUT "${FSIM_STEP_OUTPUT}")
string(REPLACE "\n" ";" FSIM_LINES "${FSIM_OUTPUT}")
set(FSIM_TRACE "")
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE MATCHES "^#c ")
    string(APPEND FSIM_TRACE "${FSIM_LINE}\n")
  endif()
endforeach()
file(READ "${FSIM_EXPECTED}" FSIM_EXPECTED_TRACE)
if(NOT FSIM_TRACE STREQUAL FSIM_EXPECTED_TRACE)
  message(FATAL_ERROR
    "trace differs from ${FSIM_EXPECTED}\n--- actual\n${FSIM_TRACE}--- expected\n${FSIM_EXPECTED_TRACE}")
endif()

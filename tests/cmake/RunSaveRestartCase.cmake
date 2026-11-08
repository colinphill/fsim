# SPDX-License-Identifier: Apache-2.0
#
# Save and restart of a run (IEEE 1800-2023 D.9). Runs one SystemVerilog
# source through `fsim compile` and `fsim elaborate`, then
#
#   1. `fsim simulate --engine ENGINE ${FSIM_SAVE_ARGS}` (the run that writes
#      the state files, by --save-at or by $save in the source); its whole
#      output, less the final "simulation completed" line, must equal
#      FSIM_EXPECTED: saving must not change what the run prints;
#   2. for each entry "FILE=EXPECTED" of FSIM_RESTARTS, in order,
#      `fsim simulate --engine ENGINE --restart FILE`, whose output must equal
#      the expected file, the part of the uninterrupted output after the save.
#
# FSIM_COMMON_ARGS, when set, are further `simulate` arguments of every run (a
# restart repeats the command line of the save, so a foreign library appears in
# both). FSIM_TRACE_ONLY compares only the lines that start with "#c " (the
# trace a foreign library prints), whatever else the runs print.
# FSIM_ELABORATE_PLUGIN, when true, also passes FSIM_COMMON_ARGS to `fsim
# elaborate` (a library that defines system tasks the source calls).
#
# FSIM_REJECT, when set, is a list of "ARGS...=MESSAGE" entries (arguments
# separated by '|') that must fail with that message, after the saves.

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_SOURCE FSIM_TOP
    FSIM_ENGINE FSIM_WORK_DIR FSIM_EXPECTED)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

# The lists arrive comma-separated.
foreach(FSIM_LIST IN ITEMS FSIM_SAVE_ARGS FSIM_RESTARTS FSIM_REJECT
    FSIM_COMMON_ARGS)
  if(DEFINED ${FSIM_LIST})
    string(REPLACE "," ";" ${FSIM_LIST} "${${FSIM_LIST}}")
  endif()
endforeach()

file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
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

function(fsim_compare FSIM_STEP_NAME FSIM_EXPECTED_FILE)
  string(REPLACE "\r\n" "\n" FSIM_TEXT "${FSIM_STEP_OUTPUT}")
  # The final status line carries the stop time, not the checked output.
  string(REGEX REPLACE "(^|\n)simulation completed[^\n]*\n?" "\\1"
    FSIM_ACTUAL "${FSIM_TEXT}")
  if(FSIM_TRACE_ONLY)
    string(REPLACE "\n" ";" FSIM_LINES "${FSIM_ACTUAL}")
    set(FSIM_ACTUAL "")
    foreach(FSIM_LINE IN LISTS FSIM_LINES)
      if(FSIM_LINE MATCHES "^#c ")
        string(APPEND FSIM_ACTUAL "${FSIM_LINE}\n")
      endif()
    endforeach()
  endif()
  file(READ "${FSIM_EXPECTED_FILE}" FSIM_EXPECTED_TEXT)
  if(NOT FSIM_ACTUAL STREQUAL FSIM_EXPECTED_TEXT)
    message(FATAL_ERROR
      "${FSIM_STEP_NAME}: output differs from ${FSIM_EXPECTED_FILE}\n--- actual\n${FSIM_ACTUAL}--- expected\n${FSIM_EXPECTED_TEXT}")
  endif()
endfunction()

fsim_step(compile 0 compile "${FSIM_SOURCE_NAME}")
# FSIM_ELABORATE_PLUGIN: a PLI 1.0 task ($tf_probe) is defined by the library,
# so `fsim elaborate` loads it too (as RunForeignConformanceCase.cmake does).
if(FSIM_ELABORATE_PLUGIN)
  fsim_step(elaborate 0 elaborate ${FSIM_COMMON_ARGS} "${FSIM_TOP}")
else()
  fsim_step(elaborate 0 elaborate "${FSIM_TOP}")
endif()
fsim_step(save 0 simulate --engine "${FSIM_ENGINE}" ${FSIM_COMMON_ARGS}
  ${FSIM_SAVE_ARGS})
fsim_compare("the saving run" "${FSIM_EXPECTED}")

foreach(FSIM_RESTART IN LISTS FSIM_RESTARTS)
  string(REPLACE "=" ";" FSIM_PARTS "${FSIM_RESTART}")
  list(GET FSIM_PARTS 0 FSIM_STATE_FILE)
  list(GET FSIM_PARTS 1 FSIM_RESTART_EXPECTED)
  if(NOT EXISTS "${FSIM_WORK_DIR}/${FSIM_STATE_FILE}")
    message(FATAL_ERROR "the saving run did not write ${FSIM_STATE_FILE}")
  endif()
  fsim_step(restart 0 simulate --engine "${FSIM_ENGINE}" ${FSIM_COMMON_ARGS}
    --restart "${FSIM_STATE_FILE}")
  fsim_compare("the restart from ${FSIM_STATE_FILE}" "${FSIM_RESTART_EXPECTED}")
endforeach()

foreach(FSIM_CASE IN LISTS FSIM_REJECT)
  string(REPLACE "=" ";" FSIM_PARTS "${FSIM_CASE}")
  list(GET FSIM_PARTS 0 FSIM_ARGS)
  list(GET FSIM_PARTS 1 FSIM_MESSAGE)
  string(REPLACE "|" ";" FSIM_ARGS "${FSIM_ARGS}")
  fsim_step(reject 1 simulate --engine "${FSIM_ENGINE}" ${FSIM_COMMON_ARGS}
    ${FSIM_ARGS})
  string(FIND "${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}" "${FSIM_MESSAGE}"
    FSIM_OFFSET)
  if(FSIM_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "fsim simulate ${FSIM_ARGS} did not report \"${FSIM_MESSAGE}\":\n${FSIM_STEP_OUTPUT}${FSIM_STEP_ERROR}")
  endif()
endforeach()

# SPDX-License-Identifier: Apache-2.0
#
# Save, restart and reset of a UVM testbench (IEEE 1800-2023 D.8, D.9; IEEE
# 1800.2-2020). The governed UVM library (FSIM_UVM_ROOT, release FSIM_UVM_RELEASE, default 2020.3.1) is
# compiled once into a library of its own, which the test workspace maps and
# searches; then one SystemVerilog source is compiled and elaborated, and
#
#   1. `fsim simulate --engine ENGINE` runs it; the whole output, less the
#      final "simulation stopped" or "simulation completed" line, must equal
#      FSIM_EXPECTED. The source saves itself with $save (or FSIM_SAVE_ARGS
#      asks for --save-at), so a run that saves must print what a run that
#      does not save prints;
#   2. for each entry "FILE=EXPECTED" of FSIM_RESTARTS, in order,
#      `fsim simulate --engine ENGINE --restart FILE` must print the expected
#      file, the part of the uninterrupted output after the save.
#
# FSIM_NO_EXECUTOR, when true, runs the design once more with the run profile
# (FSIM_PROFILE_PHASES) and requires that the host class executor ran no class
# code: the functions of the design's classes run as compiled code.
#
# FSIM_NORMALIZE_IDS, when true, replaces each instance id ("@12", and the
# padding after one that ends a table row) with "@N" in every output.
#
# A source that calls $reset needs no restarts: its expected output holds both
# lives of the run.
#
# FSIM_UVM_LIBRARY_ONLY, when true, only compiles the UVM library into
# FSIM_WORK_DIR: the fixture setup test that every case of one release shares,
# as the corpus runner compiles the library once per run. A case given
# FSIM_UVM_LIBRARY_DIR maps that compiled library instead of compiling its own.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_UVM_RELEASE OR "${FSIM_UVM_RELEASE}" STREQUAL "")
  set(FSIM_UVM_RELEASE 2020.3.1)
endif()

if(FSIM_UVM_LIBRARY_ONLY)
  foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_WORK_DIR FSIM_UVM_ROOT)
    if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
      message(FATAL_ERROR "${FSIM_REQUIRED} is required")
    endif()
  endforeach()
  file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
  file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" compile -q --standard 2017 --library uvm
      --uvm-release "${FSIM_UVM_RELEASE}" -I "${FSIM_UVM_ROOT}/src"
      "${FSIM_UVM_ROOT}/src/uvm_pkg.sv"
    WORKING_DIRECTORY "${FSIM_WORK_DIR}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR
    TIMEOUT 1200)
  if(NOT FSIM_RESULT EQUAL 0)
    message(FATAL_ERROR
      "fsim compile of the UVM library returned ${FSIM_RESULT}:\n${FSIM_OUTPUT}${FSIM_ERROR}")
  endif()
  return()
endif()

foreach(FSIM_REQUIRED IN ITEMS FSIM_EXECUTABLE FSIM_SOURCE FSIM_TOP
    FSIM_ENGINE FSIM_WORK_DIR FSIM_EXPECTED FSIM_UVM_ROOT)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

# The lists arrive comma-separated.
foreach(FSIM_LIST IN ITEMS FSIM_SAVE_ARGS FSIM_RESTARTS)
  if(DEFINED ${FSIM_LIST})
    string(REPLACE "," ";" ${FSIM_LIST} "${${FSIM_LIST}}")
  endif()
endforeach()

set(FSIM_LIBRARY_DIR "${FSIM_WORK_DIR}/uvm-library")
set(FSIM_RUN_DIR "${FSIM_WORK_DIR}/run")
file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
file(MAKE_DIRECTORY "${FSIM_LIBRARY_DIR}" "${FSIM_RUN_DIR}")
get_filename_component(FSIM_SOURCE_NAME "${FSIM_SOURCE}" NAME)
configure_file("${FSIM_SOURCE}" "${FSIM_RUN_DIR}/${FSIM_SOURCE_NAME}" COPYONLY)

function(fsim_step FSIM_STEP_DIR FSIM_STEP_NAME FSIM_WANTED)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${FSIM_STEP_DIR}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR
    TIMEOUT 1200)
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
  string(REGEX REPLACE "(^|\n)simulation (completed|stopped)[^\n]*\n?" "\\1"
    FSIM_ACTUAL "${FSIM_TEXT}")
  # The instance id of a topology row is run state; FSIM_NORMALIZE_IDS compares
  # each "@<digits>" as "@N": at the end of a table row and inside a tree or
  # line printer's rows ("name: (type) @12 {"). A report's "@ <time>" has a
  # space and is kept.
  if(FSIM_NORMALIZE_IDS)
    string(REGEX REPLACE "@[0-9]+ *\n" "@N\n" FSIM_ACTUAL "${FSIM_ACTUAL}")
    string(REGEX REPLACE "@[0-9]+" "@N" FSIM_ACTUAL "${FSIM_ACTUAL}")
  endif()
  file(READ "${FSIM_EXPECTED_FILE}" FSIM_EXPECTED_TEXT)
  if(NOT FSIM_ACTUAL STREQUAL FSIM_EXPECTED_TEXT)
    message(FATAL_ERROR
      "${FSIM_STEP_NAME}: output differs from ${FSIM_EXPECTED_FILE}\n--- actual\n${FSIM_ACTUAL}--- expected\n${FSIM_EXPECTED_TEXT}")
  endif()
endfunction()

# The library of the governed UVM release, as the corpus runner builds it:
# the shared one of the release's fixture, or one of the case's own.
if(DEFINED FSIM_UVM_LIBRARY_DIR AND NOT "${FSIM_UVM_LIBRARY_DIR}" STREQUAL "")
  set(FSIM_LIBRARY_DIR "${FSIM_UVM_LIBRARY_DIR}")
else()
  fsim_step("${FSIM_LIBRARY_DIR}" "compile of the UVM library" 0
    compile -q --standard 2017 --library uvm --uvm-release "${FSIM_UVM_RELEASE}"
    -I "${FSIM_UVM_ROOT}/src" "${FSIM_UVM_ROOT}/src/uvm_pkg.sv")
endif()
fsim_step("${FSIM_RUN_DIR}" "library map" 0
  library map uvm "${FSIM_LIBRARY_DIR}/.fsim/libraries/uvm")
fsim_step("${FSIM_RUN_DIR}" "compile" 0
  compile -q --lang systemverilog --standard 2017 --search-library uvm
  -I "${FSIM_UVM_ROOT}/src" "${FSIM_SOURCE_NAME}")
fsim_step("${FSIM_RUN_DIR}" "elaborate" 0
  elaborate -q --no-aot --search-library uvm --top "${FSIM_TOP}")
fsim_step("${FSIM_RUN_DIR}" "simulate" 0
  simulate --engine "${FSIM_ENGINE}" ${FSIM_SAVE_ARGS})
fsim_compare("the saving run" "${FSIM_EXPECTED}")

if(FSIM_NO_EXECUTOR)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env FSIM_PROFILE_PHASES=1
      "${FSIM_EXECUTABLE}" simulate --engine "${FSIM_ENGINE}"
    WORKING_DIRECTORY "${FSIM_RUN_DIR}"
    RESULT_VARIABLE FSIM_PROFILE_RESULT
    ERROR_VARIABLE FSIM_PROFILE_ERROR
    OUTPUT_QUIET
    TIMEOUT 1200)
  if(NOT FSIM_PROFILE_RESULT EQUAL 0)
    message(FATAL_ERROR "profiled simulate returned ${FSIM_PROFILE_RESULT}")
  endif()
  if(NOT FSIM_PROFILE_ERROR MATCHES "fsim-profile: class-executor invocations=0\n")
    message(FATAL_ERROR
      "the host class executor ran class code of the design:\n${FSIM_PROFILE_ERROR}")
  endif()
endif()

foreach(FSIM_RESTART IN LISTS FSIM_RESTARTS)
  string(REPLACE "=" ";" FSIM_PARTS "${FSIM_RESTART}")
  list(GET FSIM_PARTS 0 FSIM_STATE_FILE)
  list(GET FSIM_PARTS 1 FSIM_RESTART_EXPECTED)
  if(NOT EXISTS "${FSIM_RUN_DIR}/${FSIM_STATE_FILE}")
    message(FATAL_ERROR "the saving run did not write ${FSIM_STATE_FILE}")
  endif()
  fsim_step("${FSIM_RUN_DIR}" "restart" 0
    simulate --engine "${FSIM_ENGINE}" --restart "${FSIM_STATE_FILE}")
  fsim_compare("the restart from ${FSIM_STATE_FILE}" "${FSIM_RESTART_EXPECTED}")
endforeach()

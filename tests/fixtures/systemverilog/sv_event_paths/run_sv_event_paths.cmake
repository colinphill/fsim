# SPDX-License-Identifier: Apache-2.0
#
# Named-event arrays and hierarchical paths (IEEE 1800-2023 5.6.1, 7.4, 15.5,
# 23.6, 23.8):
#  - event_arrays.sv: arrays whose element type, or whole type, is a typedef
#    of event, an array of more than 65,536 events, and event arrays of a
#    loop generate block selected hierarchically with constant and variable
#    indices;
#  - event_array_index.sv: waits, blocking and nonblocking triggers and the
#    triggered state of event-array elements selected by a run-time index,
#    and indices that select no element (7.4.6);
#  - escaped_paths.sv: hierarchical names through escaped identifiers that
#    contain '.' and '[', and an upward name through an escaped instance.
# The interpreter, the compiled kernel and the compiled kernel with
# FSIM_KERNEL_VERIFY must each print the case's .expected lines.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the event paths fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

function(run_step CASE STEP)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${CASE} ${STEP} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
  endif()
  string(REGEX MATCHALL "sep [^\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP}_reports "${reports}" PARENT_SCOPE)
endfunction()

function(run_case CASE)
  file(READ "${FSIM_FIXTURE_DIR}/${CASE}.expected" expected)
  string(STRIP "${expected}" expected)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-sv-event-paths-${CASE}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/${CASE}.sv" "${case_dir}/${CASE}.sv" COPYONLY)
  run_step(${CASE} compile compile -q --standard 2023 ${CASE}.sv)
  run_step(${CASE} elaborate elaborate -q work.top --no-aot)
  run_step(${CASE} interpreter simulate --engine interpreter)
  run_step(${CASE} kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_step(${CASE} verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "${CASE} (${run}): the reports differ from ${CASE}.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endfunction()

run_case(event_arrays)
run_case(event_array_index)
run_case(escaped_paths)

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

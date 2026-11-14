# SPDX-License-Identifier: Apache-2.0
#
# fork_tasks.sv (a `fork ... join_none` of a virtual task call that waits a
# delay and an event, IEEE 1800-2023 9.3.2, 9.4, 15.5.1) prints
# fork_tasks.expected, derived by hand: the requests come at 1 and 2, the run
# task writes waited=1 at 6 and waited=2 once the pre task triggers the event
# at 9, so the reports at 3, 8, 13 and 18 are 0, 1, 2, 2. The interpreter,
# the compiled kernel, the kernel under FSIM_KERNEL_VERIFY and the kernel with
# every procedure hosted (FSIM_STATIC_KERNEL_BEHAVIORAL=0: the forked thread
# is a hosted child) print the same.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the fork task fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_STATIC_KERNEL_BEHAVIORAL FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL
    FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

file(READ "${FSIM_FIXTURE_DIR}/fork_tasks.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-sv-fork-task-calls-${workspace_id}")
fsim_test_workspace("${case_dir}")
configure_file("${FSIM_FIXTURE_DIR}/fork_tasks.sv" "${case_dir}/fork_tasks.sv" COPYONLY)

function(run_fsim_step STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
  endif()
  string(REGEX MATCHALL "fk [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile compile -q --standard 2023 fork_tasks.sv)
run_fsim_step(elaborate elaborate -q work.top --no-aot)
run_fsim_step(interpreter simulate --engine interpreter)
run_fsim_step(kernel simulate --engine v4)
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})
set(ENV{FSIM_STATIC_KERNEL_BEHAVIORAL} 0)
run_fsim_step(hosted simulate --engine v4)
unset(ENV{FSIM_STATIC_KERNEL_BEHAVIORAL})
foreach(run IN ITEMS interpreter kernel verified hosted)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "fork_tasks (${run}): the reports differ from fork_tasks.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

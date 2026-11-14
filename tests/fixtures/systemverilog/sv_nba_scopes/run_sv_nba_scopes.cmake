# SPDX-License-Identifier: Apache-2.0
#
# Scope resolution of the nonblocking-assignment target rule (IEEE 1800-2023
# 6.21, 10.4.2, 23.9):
#  - the target's root identifier names the innermost declaration visible at
#    the statement; an automatic local is rejected (FSIM-SV-SEM-397) in a
#    void function, beside a static local of the same name in a sibling
#    block, in the block that hides a module variable, and in a static
#    function with an `automatic` local, whether or not the function is called;
#  - scopes.sv writes a static block local and a module variable of the same
#    names as automatic locals of sibling blocks; the interpreter, the
#    compiled kernel and the compiled kernel with FSIM_KERNEL_VERIFY must
#    print scopes.expected.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the nonblocking scopes fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

function(expect_rejection SOURCE CODE)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-nba-scopes-${SOURCE}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/${SOURCE}.sv" "${case_dir}/${SOURCE}.sv" COPYONLY)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" compile -q --standard 2023 ${SOURCE}.sv
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE compile_result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if("${compile_result}" STREQUAL "0")
    message(FATAL_ERROR "${SOURCE} must be rejected with ${CODE} when compiled")
  endif()
  if(NOT "${out}${err}" MATCHES "${CODE}")
    message(FATAL_ERROR "${SOURCE} must report ${CODE}:\n${out}${err}")
  endif()
endfunction()

expect_rejection(reject_sibling "FSIM-SV-SEM-397")
expect_rejection(reject_void "FSIM-SV-SEM-397")
expect_rejection(reject_shadow "FSIM-SV-SEM-397")
expect_rejection(reject_static_function "FSIM-SV-SEM-397")
expect_rejection(reject_container "FSIM-SV-SEM-397")

# An automatic container of a procedural block is found when the design is
# elaborated.
function(expect_elaboration_rejection SOURCE CODE)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-nba-scopes-${SOURCE}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/${SOURCE}.sv" "${case_dir}/${SOURCE}.sv" COPYONLY)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" compile -q --standard 2023 ${SOURCE}.sv
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE compile_result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${compile_result}" STREQUAL "0")
    message(FATAL_ERROR "${SOURCE} must compile:\n${out}${err}")
  endif()
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" elaborate -q work.top --no-aot
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE elaborate_result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if("${elaborate_result}" STREQUAL "0")
    message(FATAL_ERROR "${SOURCE} must be rejected with ${CODE} when elaborated")
  endif()
  if(NOT "${out}${err}" MATCHES "${CODE}")
    message(FATAL_ERROR "${SOURCE} must report ${CODE}:\n${out}${err}")
  endif()
  if("${out}${err}" MATCHES "FSIM-ELAB-HIR-001")
    message(FATAL_ERROR "${SOURCE} must not report a lowering failure:\n${out}${err}")
  endif()
endfunction()

expect_elaboration_rejection(reject_block_container "FSIM-SV-SEM-397")

file(READ "${FSIM_FIXTURE_DIR}/scopes.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-sv-nba-scopes-${workspace_id}")
fsim_test_workspace("${case_dir}")
configure_file("${FSIM_FIXTURE_DIR}/scopes.sv" "${case_dir}/scopes.sv" COPYONLY)

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
  string(REGEX MATCHALL "sns [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
endfunction()

run_fsim_step(compile compile -q --standard 2023 scopes.sv)
run_fsim_step(elaborate elaborate -q work.top --no-aot)
run_fsim_step(interpreter simulate --engine interpreter)
run_fsim_step(kernel simulate --engine v4)
set(ENV{FSIM_KERNEL_VERIFY} 1)
run_fsim_step(verified simulate --engine v4)
unset(ENV{FSIM_KERNEL_VERIFY})
foreach(run IN ITEMS interpreter kernel verified)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "scopes (${run}): the reports differ from scopes.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
      "workspace: ${case_dir}")
  endif()
endforeach()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

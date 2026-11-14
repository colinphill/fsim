# SPDX-License-Identifier: Apache-2.0
#
# The simulate.engine configuration key picks the engine of `fsim simulate`,
# and --engine on the command line wins over it. FSIM_PROFILE_KERNEL=1 makes
# a run on the compiled kernel print "fsim-kernel:" lines; the reference
# interpreter prints none, so the lines show which engine ran.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the engine config fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-engine-config-${workspace_id}")
fsim_test_workspace("${case_dir}")
file(COPY_FILE "${FSIM_FIXTURE_DIR}/top.sv" "${case_dir}/top.sv")

function(run_fsim STEP_NAME)
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
  set(${STEP_NAME}_output "${stdout}\n${stderr}" PARENT_SCOPE)
endfunction()

function(expect_engine STEP_NAME EXPECT_KERNEL)
  string(FIND "${${STEP_NAME}_output}" "fsim-kernel:" found)
  if(EXPECT_KERNEL AND found EQUAL -1)
    message(FATAL_ERROR
      "${STEP_NAME}: the compiled kernel did not run\n"
      "${${STEP_NAME}_output}\nworkspace: ${case_dir}")
  endif()
  if(NOT EXPECT_KERNEL AND NOT found EQUAL -1)
    message(FATAL_ERROR
      "${STEP_NAME}: the reference interpreter was expected\n"
      "${${STEP_NAME}_output}\nworkspace: ${case_dir}")
  endif()
endfunction()

run_fsim(compile compile -q top.sv)
run_fsim(elaborate elaborate -q work.top --no-aot)
set(ENV{FSIM_PROFILE_KERNEL} 1)

# No key, no option: the default engine is the kernel.
run_fsim(default simulate)
expect_engine(default TRUE)

file(WRITE "${case_dir}/fsim.tcl" "fsim::config set simulate.engine interpreter\n")
run_fsim(keyed simulate)
expect_engine(keyed FALSE)

# The command line wins over the key.
run_fsim(overridden simulate --engine v4)
expect_engine(overridden TRUE)

# A value that is not an engine is a configuration error.
file(WRITE "${case_dir}/fsim.tcl" "fsim::config set simulate.engine warp\n")
execute_process(
  COMMAND "${FSIM_EXECUTABLE}" simulate
  WORKING_DIRECTORY "${case_dir}"
  RESULT_VARIABLE bad_result
  OUTPUT_VARIABLE bad_stdout
  ERROR_VARIABLE bad_stderr)
if("${bad_result}" STREQUAL "0")
  message(FATAL_ERROR "simulate.engine warp was accepted\n${bad_stdout}${bad_stderr}")
endif()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

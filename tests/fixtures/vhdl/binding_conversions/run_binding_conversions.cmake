# SPDX-License-Identifier: Apache-2.0
#
# Port map conversions in a binding indication (binding_conversions: bc1
# actual part, bc2 formal part, bc3 both, bc4 both in a configuration
# declaration, bc5 both with local ports whose actuals are an expression
# and a part of a signal): the reports of the interpreter and of the
# compiled kernel must equal binding_conversions.expected, derived from IEEE
# 1076-2019 7.3.2.2, 6.5.7.1, 6.5.7.3 and 14.5.4. bc6 leaves a local port
# out of the binding map: elaboration fails with FSIM-ELAB-VHCOMP-017
# (6.5.7.3).

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the binding conversions fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

function(run_fsim_step CASE_DIR STEP_NAME)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${CASE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${STEP_NAME} failed (${result})\n"
      "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${CASE_DIR}")
  endif()
  # The report texts, without source locations (the ';' after each text is
  # a list separator here, so it is not part of the match).
  string(REGEX MATCHALL "bc[0-9] [^;\n]*" reports "${stdout}\n${stderr}")
  list(JOIN reports "\n" reports)
  set(last_reports "${reports}" PARENT_SCOPE)
endfunction()

set(interpreter_reports "")
set(kernel_reports "")
foreach(case IN ITEMS bc1 bc2 bc3 bc4 bc5 bc6)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-binding-conversions-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  file(COPY_FILE "${FSIM_FIXTURE_DIR}/${case}.vhd" "${case_dir}/${case}.vhd")
  set(top work.top)
  if(case STREQUAL "bc4")
    set(top work.cfg)
  endif()
  run_fsim_step("${case_dir}" "${case} compile" compile -q --standard 2008 ${case}.vhd)
  if(case STREQUAL "bc6")
    execute_process(
      COMMAND "${FSIM_EXECUTABLE}" elaborate -q ${top} --no-aot
      WORKING_DIRECTORY "${case_dir}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE stdout
      ERROR_VARIABLE stderr)
    if("${result}" STREQUAL "0"
        OR NOT "${stdout}${stderr}" MATCHES "FSIM-ELAB-VHCOMP-017"
        OR NOT "${stdout}${stderr}" MATCHES "local port 'd' of component 'cmp'")
      message(FATAL_ERROR
        "bc6 elaborate must fail with FSIM-ELAB-VHCOMP-017 naming local "
        "port 'd' of component 'cmp' (${result})\n"
        "stdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    continue()
  endif()
  run_fsim_step("${case_dir}" "${case} elaborate" elaborate -q ${top} --no-aot)
  run_fsim_step("${case_dir}" "${case} interpreter" simulate --engine interpreter)
  list(APPEND interpreter_reports "${last_reports}")
  run_fsim_step("${case_dir}" "${case} kernel" simulate --engine v4)
  list(APPEND kernel_reports "${last_reports}")
endforeach()
list(JOIN interpreter_reports "\n" interpreter_reports)
list(JOIN kernel_reports "\n" kernel_reports)

file(READ "${FSIM_FIXTURE_DIR}/binding_conversions.expected" expected)
string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
string(STRIP "${expected}" expected)
foreach(run IN ITEMS interpreter kernel)
  if(NOT "${${run}_reports}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "binding_conversions (${run}): the reports differ from "
      "binding_conversions.expected\n"
      "Expected:\n${expected}\n${run}:\n${${run}_reports}")
  endif()
endforeach()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

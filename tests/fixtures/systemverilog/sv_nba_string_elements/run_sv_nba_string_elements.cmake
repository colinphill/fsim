# SPDX-License-Identifier: Apache-2.0
#
# Nonblocking assignments to elements of string arrays (IEEE 1800-2023 7.4, 7.5,
# 7.8, 7.10, 10.4.2): string_elements.sv covers fixed, multidimensional,
# dynamic, queue and associative arrays, nested_forms.sv nested containers,
# structure members, a block variable and a delayed string-key write,
# assoc_string_keys.sv numeric elements of a string-keyed associative array and
# aggregate_elements.sv whole structure and container elements,
# nested_elements.sv whole elements of nested containers and a structure's
# container member, procedural_containers.sv containers of a procedural block,
# blocking_nested.sv blocking whole-element writes into nested containers,
# procedural_nested.sv nested elements of static procedural containers, in
# SystemVerilog-2017 (which allows the
# dynamically sized ones) and fixed_2023.sv a fixed array in SystemVerilog-2023.
# The interpreter, the compiled kernel and the compiled kernel with
# FSIM_KERNEL_VERIFY must print the .expected files.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the string element fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

function(run_case NAME STANDARD)
  file(READ "${FSIM_FIXTURE_DIR}/${NAME}.expected" expected)
  string(REGEX REPLACE "^# SPDX[^\n]*\n" "" expected "${expected}")
  string(STRIP "${expected}" expected)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-sv-nba-${NAME}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/${NAME}.sv" "${case_dir}/${NAME}.sv" COPYONLY)

  function(run_step STEP_NAME)
    execute_process(
      COMMAND "${FSIM_EXECUTABLE}" ${ARGN}
      WORKING_DIRECTORY "${case_dir}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE stdout
      ERROR_VARIABLE stderr)
    if(NOT "${result}" STREQUAL "0")
      message(FATAL_ERROR
        "${NAME} ${STEP_NAME} failed (${result})\n"
        "stdout:\n${stdout}\nstderr:\n${stderr}\nworkspace: ${case_dir}")
    endif()
    string(REGEX MATCHALL "sns [^;\n]*" reports "${stdout}\n${stderr}")
    list(JOIN reports "\n" reports)
    set(${STEP_NAME}_reports "${reports}" PARENT_SCOPE)
  endfunction()

  run_step(compile compile -q --standard ${STANDARD} ${NAME}.sv)
  run_step(elaborate elaborate -q work.top --no-aot)
  run_step(interpreter simulate --engine interpreter)
  run_step(kernel simulate --engine v4)
  set(ENV{FSIM_KERNEL_VERIFY} 1)
  run_step(verified simulate --engine v4)
  unset(ENV{FSIM_KERNEL_VERIFY})
  foreach(run IN ITEMS interpreter kernel verified)
    if(NOT "${${run}_reports}" STREQUAL "${expected}")
      message(FATAL_ERROR
        "${NAME} (${run}): the reports differ from ${NAME}.expected\n"
        "Expected:\n${expected}\n${run}:\n${${run}_reports}\n"
        "workspace: ${case_dir}")
    endif()
  endforeach()
endfunction()

run_case(string_elements 2017)
run_case(nested_forms 2017)
run_case(assoc_elements 2017)
run_case(assoc_string_keys 2017)
run_case(aggregate_elements 2017)
run_case(nested_elements 2017)
run_case(procedural_containers 2017)
run_case(blocking_nested 2017)
run_case(procedural_nested 2017)
run_case(fixed_2023 2023)

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

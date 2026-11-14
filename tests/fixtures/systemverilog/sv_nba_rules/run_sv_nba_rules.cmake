# SPDX-License-Identifier: Apache-2.0
#
# Legality of nonblocking assignments (IEEE 1800-2023 6.21, 10.4.2, 13.4.4):
# an automatic variable (a local or a formal of an automatic function) is not
# a target (FSIM-SV-SEM-397); a function has no intra-assignment delay
# (FSIM-SV-SEM-063); an element of a dynamically sized array is not a target in
# SystemVerilog-2023 (FSIM-ELAB-SVASSIGN-001) but is in SystemVerilog-2017,
# where it runs on the interpreter and the compiled kernel.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the nonblocking rules fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

# A rejected case: the diagnostic code appears when the design is compiled or
# elaborated.
function(expect_rejection SOURCE STANDARD CODE)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-nba-rules-${SOURCE}-${STANDARD}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/${SOURCE}.sv" "${case_dir}/${SOURCE}.sv" COPYONLY)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" compile -q --standard ${STANDARD} ${SOURCE}.sv
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE compile_result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(text "${out}${err}")
  if("${compile_result}" STREQUAL "0")
    execute_process(
      COMMAND "${FSIM_EXECUTABLE}" elaborate -q work.top --no-aot
      WORKING_DIRECTORY "${case_dir}"
      RESULT_VARIABLE elaborate_result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(text "${text}${out}${err}")
    if("${elaborate_result}" STREQUAL "0")
      message(FATAL_ERROR "${SOURCE} (${STANDARD}) must be rejected with ${CODE}")
    endif()
  endif()
  if(NOT text MATCHES "${CODE}")
    message(FATAL_ERROR "${SOURCE} (${STANDARD}) must report ${CODE}:\n${text}")
  endif()
endfunction()

expect_rejection(automatic_local 2023 "FSIM-SV-SEM-397")
expect_rejection(automatic_formal 2023 "FSIM-SV-SEM-397")
expect_rejection(function_delay 2023 "FSIM-SV-SEM-063")
expect_rejection(dynamic_element 2023 "FSIM-ELAB-SVASSIGN-001")

# SystemVerilog-2017 allows the element write.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/fsim-nba-rules-2017-${workspace_id}")
fsim_test_workspace("${case_dir}")
configure_file("${FSIM_FIXTURE_DIR}/dynamic_element.sv" "${case_dir}/dynamic_element.sv" COPYONLY)
foreach(step IN ITEMS "compile;-q;--standard;2017;dynamic_element.sv"
    "elaborate;-q;work.top;--no-aot")
  execute_process(COMMAND "${FSIM_EXECUTABLE}" ${step}
    WORKING_DIRECTORY "${case_dir}" RESULT_VARIABLE result
    OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "SystemVerilog-2017 dynamic_element: ${step} failed\n${out}${err}")
  endif()
endforeach()
foreach(engine IN ITEMS interpreter v4)
  execute_process(COMMAND "${FSIM_EXECUTABLE}" simulate --engine ${engine}
    WORKING_DIRECTORY "${case_dir}" RESULT_VARIABLE result
    OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${result}" STREQUAL "0" OR NOT "${out}${err}" MATCHES "nbarule element 1")
    message(FATAL_ERROR "SystemVerilog-2017 dynamic_element (${engine}) must report 'nbarule element 1':\n${out}${err}")
  endif()
endforeach()

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

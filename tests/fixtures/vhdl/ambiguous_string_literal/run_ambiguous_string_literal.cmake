# SPDX-License-Identifier: Apache-2.0
#
# A string literal that fits the vector formals of more than one overload is
# ambiguous (ambiguous_generate.vhd, ambiguous_process.vhd; IEEE 1076-2019
# 9.3.2, 12.5): compile or elaboration fails with FSIM-ELAB-VHENUM-004. The same call
# with a qualified expression (qualified_process.vhd, 9.3.5) elaborates and
# runs.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FsimTestWorkspace.cmake")

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the ambiguous string literal fixture directory")
endif()
foreach(variable IN ITEMS FSIM_STATIC_KERNEL FSIM_KERNEL_VERIFY
    FSIM_PROFILE_PHASES FSIM_PROFILE_KERNEL FSIM_VERIFY_ARTIFACT)
  unset(ENV{${variable}})
endforeach()

function(run_case SOURCE EXPECT_FAILURE)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef workspace_id)
  set(case_dir
    "${CMAKE_CURRENT_BINARY_DIR}/fsim-ambiguous-string-${SOURCE}-${workspace_id}")
  fsim_test_workspace("${case_dir}")
  configure_file("${FSIM_FIXTURE_DIR}/${SOURCE}.vhd" "${case_dir}/${SOURCE}.vhd"
    COPYONLY)
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" compile -q --standard 2008 ${SOURCE}.vhd
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_out ERROR_VARIABLE compile_err)
  # Compile already elaborates the design for its analysis checks, so the
  # ambiguity may be reported there or by the elaborate step.
  if(NOT "${compile_result}" STREQUAL "0")
    if(EXPECT_FAILURE
        AND "${compile_out}${compile_err}" MATCHES "FSIM-ELAB-VHENUM-004")
      return()
    endif()
    message(FATAL_ERROR "${SOURCE}: compile failed\n${compile_out}${compile_err}")
  endif()
  execute_process(
    COMMAND "${FSIM_EXECUTABLE}" elaborate -q work.top --no-aot
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(EXPECT_FAILURE)
    if("${result}" STREQUAL "0" OR NOT "${out}${err}" MATCHES "FSIM-ELAB-VHENUM-004")
      message(FATAL_ERROR
        "${SOURCE}: elaboration must fail with FSIM-ELAB-VHENUM-004 "
        "(${result})\nstdout:\n${out}\nstderr:\n${err}")
    endif()
    return()
  endif()
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "${SOURCE}: elaboration failed\n${out}${err}")
  endif()
  foreach(engine IN ITEMS interpreter v4)
    execute_process(
      COMMAND "${FSIM_EXECUTABLE}" simulate --engine ${engine}
      WORKING_DIRECTORY "${case_dir}"
      RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT "${result}" STREQUAL "0" OR NOT "${out}${err}" MATCHES "qualified 01XX")
      message(FATAL_ERROR
        "${SOURCE} (${engine}): expected the report 'qualified 01XX'\n"
        "stdout:\n${out}\nstderr:\n${err}")
    endif()
  endforeach()
endfunction()

run_case(ambiguous_generate TRUE)
run_case(ambiguous_process TRUE)
run_case(qualified_process FALSE)

# Passed: remove this run's workspaces (a failure above keeps them).
fsim_test_workspace_cleanup()

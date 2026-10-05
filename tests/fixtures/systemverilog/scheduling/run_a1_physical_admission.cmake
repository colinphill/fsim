# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_EXECUTABLE OR NOT EXISTS "${FSIM_EXECUTABLE}")
  message(FATAL_ERROR "FSIM_EXECUTABLE must name the built fsim command")
endif()
if(NOT DEFINED FSIM_VPI_TEST_EXECUTABLE
    OR NOT EXISTS "${FSIM_VPI_TEST_EXECUTABLE}")
  message(FATAL_ERROR
    "FSIM_VPI_TEST_EXECUTABLE must name fsim_vpi_application_tests")
endif()
if(NOT DEFINED FSIM_FIXTURE_DIR OR NOT IS_DIRECTORY "${FSIM_FIXTURE_DIR}")
  message(FATAL_ERROR "FSIM_FIXTURE_DIR must name the scheduling fixture directory")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    FSIM_REQUIRE_A1_PHYSICAL_ALIASES=1
    "${FSIM_VPI_TEST_EXECUTABLE}"
  RESULT_VARIABLE alias_result
  OUTPUT_VARIABLE alias_stdout
  ERROR_VARIABLE alias_stderr)
if(NOT "${alias_result}" STREQUAL "0")
  message(FATAL_ERROR
    "The application test did not prove two physical leaf aliases and one "
    "writable aggregate proxy (${alias_result})\n"
    "stdout:\n${alias_stdout}\nstderr:\n${alias_stderr}")
endif()

foreach(optimization IN ITEMS O0 O2)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_EXECUTABLE=${FSIM_EXECUTABLE}"
      "-DFSIM_FIXTURE_DIR=${FSIM_FIXTURE_DIR}"
      "-DFSIM_SCHEDULING_ENGINE=llvm"
      "-DFSIM_SCHEDULING_OPTIMIZATION=${optimization}"
      "-DFSIM_SCHEDULING_CASES=a1_alias_leaf_native_read"
      "-DFSIM_REQUIRE_A1_NATIVE_DIRECT_READ_SLOTS=ON"
      -P "${CMAKE_CURRENT_LIST_DIR}/run_fsim.cmake"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_stdout
    ERROR_VARIABLE native_stderr)
  if(NOT "${native_result}" STREQUAL "0")
    message(FATAL_ERROR
      "A1 alias fixture failed its ${optimization} native/direct-slot gate "
      "(${native_result})\nstdout:\n${native_stdout}\n"
      "stderr:\n${native_stderr}")
  endif()
endforeach()

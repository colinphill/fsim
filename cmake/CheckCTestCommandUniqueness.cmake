# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS FSIM_BINARY_DIR FSIM_CTEST_COMMAND)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${FSIM_CTEST_COMMAND}"
    --test-dir "${FSIM_BINARY_DIR}"
    --show-only=json-v1
  RESULT_VARIABLE FSIM_CTEST_STATUS
  OUTPUT_VARIABLE FSIM_CTEST_JSON
  ERROR_VARIABLE FSIM_CTEST_ERROR
  TIMEOUT 120)
if(NOT FSIM_CTEST_STATUS EQUAL 0)
  message(FATAL_ERROR
    "could not inspect generated CTest commands: ${FSIM_CTEST_ERROR}")
endif()

string(JSON FSIM_TEST_COUNT LENGTH "${FSIM_CTEST_JSON}" tests)
if(FSIM_TEST_COUNT LESS 1)
  message(FATAL_ERROR "generated CTest metadata contains no tests")
endif()
math(EXPR FSIM_LAST_TEST "${FSIM_TEST_COUNT} - 1")
foreach(FSIM_TEST_INDEX RANGE 0 ${FSIM_LAST_TEST})
  string(JSON FSIM_TEST_NAME
    GET "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX} name)
  string(JSON FSIM_COMMAND_COUNT
    LENGTH "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX} command)
  set(FSIM_CANONICAL_COMMAND "")
  if(FSIM_COMMAND_COUNT GREATER 0)
    math(EXPR FSIM_LAST_ARGUMENT "${FSIM_COMMAND_COUNT} - 1")
    foreach(FSIM_ARGUMENT_INDEX RANGE 0 ${FSIM_LAST_ARGUMENT})
      string(JSON FSIM_ARGUMENT
        GET "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX}
        command ${FSIM_ARGUMENT_INDEX})
      string(LENGTH "${FSIM_ARGUMENT}" FSIM_ARGUMENT_LENGTH)
      string(APPEND FSIM_CANONICAL_COMMAND
        "${FSIM_ARGUMENT_LENGTH}:${FSIM_ARGUMENT}")
    endforeach()
  endif()
  string(SHA256 FSIM_COMMAND_SHA256 "${FSIM_CANONICAL_COMMAND}")
  set(FSIM_COMMAND_OWNER "FSIM_CTEST_OWNER_${FSIM_COMMAND_SHA256}")
  if(DEFINED ${FSIM_COMMAND_OWNER})
    message(FATAL_ERROR
      "duplicate CTest command owners: ${${FSIM_COMMAND_OWNER}} and ${FSIM_TEST_NAME}")
  endif()
  set(${FSIM_COMMAND_OWNER} "${FSIM_TEST_NAME}")
endforeach()

message(STATUS
  "CTest command uniqueness: ${FSIM_TEST_COUNT} generated commands have one owner each")

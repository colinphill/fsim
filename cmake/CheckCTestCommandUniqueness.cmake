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

# Tests created by a child CMake directory cannot acquire fixture properties
# from the parent directory's registration pass. Verify their generated CTest
# metadata directly so a de-duplicated closure cannot suppress its nested run
# without first executing every promised child-directory witness.
function(fsim_require_fixture_setup FSIM_REQUIRED_TEST)
  set(FSIM_EXPECTED_FIXTURES ${ARGN})
  set(FSIM_TEST_FOUND FALSE)
  foreach(FSIM_TEST_INDEX RANGE 0 ${FSIM_LAST_TEST})
    string(JSON FSIM_TEST_NAME
      GET "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX} name)
    if(NOT "${FSIM_TEST_NAME}" STREQUAL "${FSIM_REQUIRED_TEST}")
      continue()
    endif()
    set(FSIM_TEST_FOUND TRUE)
    string(JSON FSIM_PROPERTY_COUNT
      LENGTH "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX} properties)
    set(FSIM_FIXTURE_PROPERTY_FOUND FALSE)
    if(FSIM_PROPERTY_COUNT GREATER 0)
      math(EXPR FSIM_LAST_PROPERTY "${FSIM_PROPERTY_COUNT} - 1")
      foreach(FSIM_PROPERTY_INDEX RANGE 0 ${FSIM_LAST_PROPERTY})
        string(JSON FSIM_PROPERTY_NAME
          GET "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX}
          properties ${FSIM_PROPERTY_INDEX} name)
        if(NOT FSIM_PROPERTY_NAME STREQUAL "FIXTURES_SETUP")
          continue()
        endif()
        set(FSIM_FIXTURE_PROPERTY_FOUND TRUE)
        string(JSON FSIM_FIXTURE_COUNT
          LENGTH "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX}
          properties ${FSIM_PROPERTY_INDEX} value)
        foreach(FSIM_EXPECTED_FIXTURE IN LISTS FSIM_EXPECTED_FIXTURES)
          set(FSIM_EXPECTED_FIXTURE_FOUND FALSE)
          if(FSIM_FIXTURE_COUNT GREATER 0)
            math(EXPR FSIM_LAST_FIXTURE "${FSIM_FIXTURE_COUNT} - 1")
            foreach(FSIM_FIXTURE_INDEX RANGE 0 ${FSIM_LAST_FIXTURE})
              string(JSON FSIM_ACTUAL_FIXTURE
                GET "${FSIM_CTEST_JSON}" tests ${FSIM_TEST_INDEX}
                properties ${FSIM_PROPERTY_INDEX} value
                ${FSIM_FIXTURE_INDEX})
              if("${FSIM_ACTUAL_FIXTURE}" STREQUAL
                 "${FSIM_EXPECTED_FIXTURE}")
                set(FSIM_EXPECTED_FIXTURE_FOUND TRUE)
                break()
              endif()
            endforeach()
          endif()
          if(NOT FSIM_EXPECTED_FIXTURE_FOUND)
            message(FATAL_ERROR
              "CTest fixture ${FSIM_EXPECTED_FIXTURE} does not own child-directory witness ${FSIM_REQUIRED_TEST}")
          endif()
        endforeach()
      endforeach()
    endif()
    if(NOT FSIM_FIXTURE_PROPERTY_FOUND)
      message(FATAL_ERROR
        "child-directory witness ${FSIM_REQUIRED_TEST} has no CTest fixture ownership")
    endif()
  endforeach()
  if(NOT FSIM_TEST_FOUND)
    message(FATAL_ERROR
      "required child-directory CTest witness is missing: ${FSIM_REQUIRED_TEST}")
  endif()
endfunction()

fsim_require_fixture_setup(
  fsim.runtime
  fsim_compatibility_smoke_closure_witnesses
  fsim_verilog_systemverilog_standard_mode_closure_witnesses
  fsim_systemverilog_closure_witnesses
  fsim_verilog_closure_witnesses)
fsim_require_fixture_setup(
  fsim.runtime.fst_reader
  fsim_fst_closure_witnesses)

message(STATUS
  "CTest command uniqueness: ${FSIM_TEST_COUNT} generated commands have one owner each")

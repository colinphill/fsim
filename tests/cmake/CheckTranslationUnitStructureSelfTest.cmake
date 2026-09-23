# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_REQUIRED IN ITEMS FSIM_SOURCE_DIR FSIM_BINARY_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

set(FSIM_FIXTURE_ROOT
  "${FSIM_BINARY_DIR}/translation-unit-structure-fixture")
set(FSIM_CHECKER
  "${FSIM_SOURCE_DIR}/cmake/CheckTranslationUnitStructure.cmake")

function(fsim_check_structure FSIM_EXPECT_PASS FSIM_EXPECT_PATH)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_FIXTURE_ROOT}" -P "${FSIM_CHECKER}"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  if(FSIM_EXPECT_PASS)
    if(NOT FSIM_STATUS EQUAL 0)
      message(FATAL_ERROR
        "clean translation-unit fixture failed: ${FSIM_ERROR}")
    endif()
    return()
  endif()
  string(FIND "${FSIM_OUTPUT}${FSIM_ERROR}"
    ".tpp implementation files are forbidden: ${FSIM_EXPECT_PATH}"
    FSIM_DIAGNOSTIC_OFFSET)
  if(FSIM_STATUS EQUAL 0 OR FSIM_DIAGNOSTIC_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "forbidden ${FSIM_EXPECT_PATH} fixture was not rejected")
  endif()
endfunction()

foreach(FSIM_SUBDIR IN ITEMS include src tests)
  file(MAKE_DIRECTORY "${FSIM_FIXTURE_ROOT}/${FSIM_SUBDIR}")
  file(REMOVE "${FSIM_FIXTURE_ROOT}/${FSIM_SUBDIR}/forbidden.tpp")
endforeach()
fsim_check_structure(TRUE "")
foreach(FSIM_SUBDIR IN ITEMS include src tests)
  set(FSIM_FORBIDDEN
    "${FSIM_FIXTURE_ROOT}/${FSIM_SUBDIR}/forbidden.tpp")
  file(WRITE "${FSIM_FORBIDDEN}" "// fixture\n")
  fsim_check_structure(FALSE "${FSIM_SUBDIR}/forbidden.tpp")
  file(REMOVE "${FSIM_FORBIDDEN}")
endforeach()
fsim_check_structure(TRUE "")

message(STATUS "translation-unit structure fixtures passed")

# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test.cpp")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
set(FSIM_RECURSIVE_DRIVERS
  RunAccelleraSystemCClosure.cmake
  RunSdfClosure.cmake
  RunSdfApplicationClosure.cmake
  RunSdfVitalClosure.cmake
  RunFstClosure.cmake
  RunScvClosure.cmake
  RunVhdlStandardModeClosureMatrix.cmake
  RunVerilogSystemVerilogStandardModeClosureMatrix.cmake
  RunSystemVerilogClosureMatrix.cmake
  RunVerilogClosureMatrix.cmake)
set(FSIM_INPUTS "${FSIM_APPLICATION_TEST}" "${FSIM_TEST_CMAKE}")
foreach(FSIM_DRIVER IN LISTS FSIM_RECURSIVE_DRIVERS)
  list(APPEND FSIM_INPUTS "${FSIM_SOURCE_DIR}/cmake/${FSIM_DRIVER}")
endforeach()
foreach(FSIM_INPUT IN LISTS FSIM_INPUTS)
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "application de-duplication input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_APPLICATION_TEST}" FSIM_APPLICATION_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "#if defined(FSIM_MERGED_APPLICATION_TESTS)"
    "application: partitioned cases registered separately"
    "#else"
    "fsim_application_case_core_simulation()"
    "test_class_simulation_integration"
    "test_specialization_and_packages"
    "test_artifact_phase_semantics")
  string(FIND "${FSIM_APPLICATION_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "application de-duplication contract lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(READ "${FSIM_TEST_CMAKE}" FSIM_TEST_CMAKE_TEXT)
foreach(FSIM_TEST IN ITEMS
    fsim.application
    fsim.application.core_simulation
    fsim.application.core_mixed
    fsim.application.core_multiple_roots
    fsim.application.core_preprocessing_cli
    fsim.application.core_non_project_cli
    fsim.application.specialization
    fsim.application.artifact_phases
    fsim.application.classes)
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TEST}" FSIM_TEST_INDEX)
  if(FSIM_TEST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "application de-duplication lost registered test: ${FSIM_TEST}")
  endif()
endforeach()

string(REGEX MATCHALL "-DFSIM_DEDUPLICATED_CTEST=ON"
  FSIM_DEDUPLICATED_DRIVER_FLAGS "${FSIM_TEST_CMAKE_TEXT}")
list(LENGTH FSIM_DEDUPLICATED_DRIVER_FLAGS FSIM_DRIVER_FLAG_COUNT)
list(LENGTH FSIM_RECURSIVE_DRIVERS FSIM_RECURSIVE_DRIVER_COUNT)
if(NOT FSIM_DRIVER_FLAG_COUNT EQUAL FSIM_RECURSIVE_DRIVER_COUNT)
  message(FATAL_ERROR
    "regression de-duplication expected ${FSIM_RECURSIVE_DRIVER_COUNT} closure-driver flags, found ${FSIM_DRIVER_FLAG_COUNT}")
endif()

foreach(FSIM_TOKEN IN ITEMS
    "FIXTURES_REQUIRED"
    "FIXTURES_SETUP"
    "fsim_require_fixture_witnesses"
    "fsim_require_fixture_regex")
  string(FIND "${FSIM_TEST_CMAKE_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR
      "regression de-duplication lost fixture token: ${FSIM_TOKEN}")
  endif()
endforeach()

foreach(FSIM_DRIVER IN LISTS FSIM_RECURSIVE_DRIVERS)
  file(READ "${FSIM_SOURCE_DIR}/cmake/${FSIM_DRIVER}" FSIM_DRIVER_TEXT)
  foreach(FSIM_TOKEN IN ITEMS
      "if(FSIM_DEDUPLICATED_CTEST)"
      "nested CTest execution suppressed"
      "return()")
    string(FIND "${FSIM_DRIVER_TEXT}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
    if(FSIM_TOKEN_INDEX EQUAL -1)
      message(FATAL_ERROR
        "regression de-duplication driver ${FSIM_DRIVER} lost token: ${FSIM_TOKEN}")
    endif()
  endforeach()
endforeach()

message(STATUS
  "regression de-duplication: application partitions and ten fixture-backed closure drivers are present")

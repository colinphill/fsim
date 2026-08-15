# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test.cpp")
set(FSIM_TEST_CMAKE "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_APPLICATION_TEST}" "${FSIM_TEST_CMAKE}")
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

message(STATUS
  "application regression de-duplication: umbrella sentinel plus eight dedicated application phases are present")

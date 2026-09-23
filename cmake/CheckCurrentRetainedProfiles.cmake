# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_VARIABLE IN ITEMS FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_CTEST_COMMAND)
  if(NOT DEFINED ${FSIM_VARIABLE} OR "${${FSIM_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_VARIABLE} is required")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CurrentEvidenceOwners.cmake")
fsim_current_registered_ctests(FSIM_REGISTERED_CTESTS)

function(fsim_profile_test_owner FSIM_PATH FSIM_OWNER_OUT)
  if(FSIM_PATH STREQUAL "tests/project/project_config_test.cpp")
    set(FSIM_OWNER fsim.project)
  elseif(FSIM_PATH STREQUAL "tests/app/vhdl_logic9_application_test.cpp")
    set(FSIM_OWNER fsim.application.vhdl_logic9)
  elseif(FSIM_PATH STREQUAL
      "tests/app/vhdl_ieee_integration_application_test.cpp")
    set(FSIM_OWNER fsim.application.vhdl_ieee_integration)
  elseif(FSIM_PATH STREQUAL "tests/app/typed_boundary_application_test.cpp")
    set(FSIM_OWNER fsim.application.typed_boundaries)
  elseif(FSIM_PATH STREQUAL "tests/app/application_test_artifact_verilog.cpp"
      OR FSIM_PATH STREQUAL "tests/app/application_test_artifact_phases.cpp")
    set(FSIM_OWNER fsim.application.artifact_phases)
  else()
    message(FATAL_ERROR "retained profile evidence has no named CTest: ${FSIM_PATH}")
  endif()
  set(${FSIM_OWNER_OUT} "${FSIM_OWNER}" PARENT_SCOPE)
endfunction()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_retained_profile_qualification.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_ROWS)
list(POP_FRONT FSIM_ROWS FSIM_LICENSE)
list(POP_FRONT FSIM_ROWS FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0"
   OR NOT FSIM_HEADER STREQUAL
      "id\tlanguage\tcanonical\taliases\tselection_evidence\tbehavioral_evidence\tartifact_evidence\towner")
  message(FATAL_ERROR "retained-profile ledger header changed")
endif()

set(FSIM_SEEN_IDS)
foreach(FSIM_ROW IN LISTS FSIM_ROWS)
  if(FSIM_ROW MATCHES ";")
    message(FATAL_ERROR "retained-profile row has an invalid list separator")
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 8)
    message(FATAL_ERROR "retained-profile row has ${FSIM_FIELD_COUNT} fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_LANGUAGE)
  list(GET FSIM_FIELDS 2 FSIM_CANONICAL)
  list(GET FSIM_FIELDS 3 FSIM_ALIASES)
  if(NOT FSIM_ID MATCHES "^V3PROFILE-[A-Z0-9-]+$"
     OR NOT FSIM_LANGUAGE MATCHES "^(vhdl|verilog|systemverilog)$"
     OR FSIM_CANONICAL STREQUAL "" OR FSIM_ALIASES STREQUAL "")
    message(FATAL_ERROR "retained-profile row is malformed: ${FSIM_ID}")
  endif()
  if(FSIM_ID IN_LIST FSIM_SEEN_IDS)
    message(FATAL_ERROR "duplicate retained-profile ID: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_SEEN_IDS "${FSIM_ID}")
  foreach(FSIM_EVIDENCE_INDEX RANGE 4 6)
    list(GET FSIM_FIELDS ${FSIM_EVIDENCE_INDEX} FSIM_PATH)
    fsim_current_evidence_file("${FSIM_PATH}")
    fsim_profile_test_owner("${FSIM_PATH}" FSIM_OWNER)
    list(FIND FSIM_REGISTERED_CTESTS "${FSIM_OWNER}" FSIM_OWNER_INDEX)
    if(FSIM_OWNER_INDEX EQUAL -1)
      message(FATAL_ERROR "retained-profile owner CTest is missing: ${FSIM_ID}")
    endif()
  endforeach()
endforeach()

set(FSIM_REQUIRED_FILE
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt")
file(STRINGS "${FSIM_REQUIRED_FILE}" FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(NOT FSIM_ID MATCHES "^V3PROFILE-[A-Z0-9-]+$")
    continue()
  endif()
  list(FIND FSIM_SEEN_IDS "${FSIM_ID}" FSIM_FOUND)
  if(FSIM_FOUND EQUAL -1)
    message(FATAL_ERROR "required retained profile is missing: ${FSIM_ID}")
  endif()
endforeach()

list(LENGTH FSIM_SEEN_IDS FSIM_PROFILE_COUNT)
message(STATUS
  "retained profiles: ${FSIM_PROFILE_COUNT} mapped identities have selection, behavior, and artifact owners; additions allow")

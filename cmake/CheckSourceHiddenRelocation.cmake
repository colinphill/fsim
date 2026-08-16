# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_normalized_text_sha256 path output_variable)
  file(READ "${path}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(REPLACE "\r" "\n" contents "${contents}")
  string(SHA256 digest "${contents}")
  set("${output_variable}" "${digest}" PARENT_SCOPE)
endfunction()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/source_hidden_relocation_contract.tsv")
if(NOT EXISTS "${FSIM_CONTRACT}")
  message(FATAL_ERROR "source-hidden relocation contract is missing")
endif()

set(FSIM_EXPECTED_DIGEST
  "7f6c40bb6f814bab9bb056e74c5f70e33a7f95850222270bd862f98ca3ea266a")
fsim_normalized_text_sha256("${FSIM_CONTRACT}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "source-hidden relocation contract digest changed: expected "
    "${FSIM_EXPECTED_DIGEST}, got ${FSIM_ACTUAL_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 32)
  message(FATAL_ERROR
    "source-hidden relocation contract requires SPDX, header and 30 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tartifact_family\tproducer_hidden\tpath_profile\tread_only\tdiscovery\tconsumer_output\tevidence")
  message(FATAL_ERROR "source-hidden relocation header or SPDX drifted")
endif()

set(FSIM_FAMILIES)
foreach(FSIM_INDEX RANGE 2 31)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 8)
    message(FATAL_ERROR "source-hidden relocation row ${FSIM_INDEX} is malformed")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_FAMILY)
  list(GET FSIM_FIELDS 2 FSIM_HIDDEN)
  list(GET FSIM_FIELDS 3 FSIM_PATH_PROFILE)
  list(GET FSIM_FIELDS 4 FSIM_READ_ONLY)
  list(GET FSIM_FIELDS 5 FSIM_DISCOVERY)
  list(GET FSIM_FIELDS 6 FSIM_OUTPUT)
  list(GET FSIM_FIELDS 7 FSIM_EVIDENCE)
  math(EXPR FSIM_NUMBER "${FSIM_INDEX} - 1")
  if(FSIM_NUMBER LESS 10)
    set(FSIM_EXPECTED_ID "RELOC174-0${FSIM_NUMBER}")
  else()
    set(FSIM_EXPECTED_ID "RELOC174-${FSIM_NUMBER}")
  endif()
  if(NOT FSIM_ID STREQUAL FSIM_EXPECTED_ID OR FSIM_FAMILY STREQUAL "" OR
     NOT FSIM_HIDDEN STREQUAL "yes" OR
     NOT FSIM_PATH_PROFILE STREQUAL "space-unicode" OR
     NOT FSIM_READ_ONLY STREQUAL "yes" OR FSIM_DISCOVERY STREQUAL "" OR
     FSIM_OUTPUT STREQUAL "" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
    message(FATAL_ERROR "source-hidden relocation row ${FSIM_ID} drifted")
  endif()
  list(FIND FSIM_FAMILIES "${FSIM_FAMILY}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate relocation family ${FSIM_FAMILY}")
  endif()
  list(APPEND FSIM_FAMILIES "${FSIM_FAMILY}")
endforeach()

function(fsim_require_relocation_tokens relative_path)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${relative_path}")
  file(READ "${FSIM_PATH}" FSIM_TEXT)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_TEXT}" "${FSIM_TOKEN}" FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "source-hidden relocation owner ${relative_path} lost ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_relocation_tokens(cmake/CheckInstalledPublicContract.cmake
  "staged-origin" "relocated install é" "file(RENAME"
  "file(CHMOD_RECURSE" "read-only relocation")
fsim_require_relocation_tokens(tests/api/api_mapped_library_test.cpp
  "relocated mapped packages" "vendor library.fsimlib" "producer-hidden"
  "perm_options::remove" "fsim_session_build")
fsim_require_relocation_tokens(tests/app/application_test_non_project_cli.cpp
  "relocated artifacts" "read only design.fsimdesign"
  "make_tree_read_only" "producer_prefix" "relocated outputs"
  "relocated-consumer-cache" "relocated-consumer-files")
fsim_require_relocation_tokens(tests/app/application_test_artifact_phases.cpp
  "relocated" "checkpoint" "provenance" "cache.hits")
fsim_require_relocation_tokens(tests/scv/scv_installed_consumer_test.cmake
  "relocated-install-é" "installed consumer resolved a non-relocated")

message(STATUS
  "source-hidden relocation contract passed: rows=30 digest=${FSIM_ACTUAL_DIGEST}")

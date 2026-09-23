# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_VARIABLE IN ITEMS FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_EXECUTABLE)
  if(NOT DEFINED ${FSIM_VARIABLE} OR "${${FSIM_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_VARIABLE} is required")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CurrentEvidenceOwners.cmake")

file(READ "${FSIM_SOURCE_DIR}/CMakeLists.txt" FSIM_PROJECT_CMAKE)
if(NOT FSIM_PROJECT_CMAKE MATCHES
    "project\\([ \n\r\t]*fsim[ \n\r\t]+VERSION[ \n\r\t]+([0-9]+\\.[0-9]+\\.[0-9]+)")
  message(FATAL_ERROR "cannot read fsim project version")
endif()
set(FSIM_VERSION "${CMAKE_MATCH_1}")
file(READ "${FSIM_SOURCE_DIR}/include/fsim/version.hpp" FSIM_HEADER)
if(NOT FSIM_HEADER MATCHES "version = \"${FSIM_VERSION}\"")
  message(FATAL_ERROR "compiled version header differs from project version")
endif()
if(NOT FSIM_HEADER MATCHES
    "native_abi_version = ([0-9]+)")
  message(FATAL_ERROR "cannot read native ABI version")
endif()
set(FSIM_ABI_VERSION "${CMAKE_MATCH_1}")

set(FSIM_INVENTORY
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_version_identity.tsv")
file(STRINGS "${FSIM_INVENTORY}" FSIM_ROWS)
set(FSIM_IDS)
set(FSIM_PATHS)
foreach(FSIM_ROW IN LISTS FSIM_ROWS)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  if(FSIM_ROW MATCHES ";")
    message(FATAL_ERROR "invalid v3 version-identity row")
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 5)
    message(FATAL_ERROR "invalid v3 version-identity row: ${FSIM_ROW}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_SURFACE)
  list(GET FSIM_FIELDS 2 FSIM_PATH)
  list(GET FSIM_FIELDS 3 FSIM_IDENTITY)
  list(GET FSIM_FIELDS 4 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^V3VER-[A-Z0-9-]+$"
     OR FSIM_SURFACE STREQUAL "" OR FSIM_IDENTITY STREQUAL ""
     OR FSIM_OWNER STREQUAL "" OR FSIM_ID IN_LIST FSIM_IDS
     OR FSIM_PATH IN_LIST FSIM_PATHS)
    message(FATAL_ERROR "invalid v3 version identity/owner: ${FSIM_ROW}")
  endif()
  fsim_current_evidence_file("${FSIM_PATH}")
  if(FSIM_ID MATCHES "^V3VER-(CMAKE|HEADER|PKGCONFIG)$")
    set(FSIM_EXPECTED "${FSIM_VERSION}")
  elseif(FSIM_ID STREQUAL "V3VER-RECORD")
    set(FSIM_EXPECTED "v${FSIM_VERSION}")
  elseif(FSIM_ID MATCHES "^V3VER-(POLICY|ARCHIVES|CI)$")
    set(FSIM_EXPECTED "fsim-v${FSIM_VERSION}")
  else()
    set(FSIM_EXPECTED "${FSIM_IDENTITY}")
  endif()
  if(NOT FSIM_IDENTITY STREQUAL FSIM_EXPECTED)
    message(FATAL_ERROR "v3 version identity differs: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_PATHS "${FSIM_PATH}")
endforeach()

file(STRINGS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt"
  FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(FSIM_ID MATCHES "^V3VER-[A-Z0-9-]+$"
     AND NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "required v3 version identity is missing: ${FSIM_ID}")
  endif()
endforeach()

file(READ "${FSIM_BINARY_DIR}/fsim.pc" FSIM_PC)
string(FIND "\n${FSIM_PC}\n" "\nVersion: ${FSIM_VERSION}\n"
  FSIM_PC_VERSION)
if(FSIM_PC_VERSION EQUAL -1)
  message(FATAL_ERROR "generated fsim.pc differs from project version")
endif()
execute_process(COMMAND "${FSIM_EXECUTABLE}" --version
  RESULT_VARIABLE FSIM_VERSION_STATUS OUTPUT_VARIABLE FSIM_COMPILED_VERSION
  ERROR_VARIABLE FSIM_VERSION_ERROR)
string(STRIP "${FSIM_COMPILED_VERSION}" FSIM_COMPILED_VERSION)
if(NOT FSIM_VERSION_STATUS EQUAL 0
   OR NOT FSIM_COMPILED_VERSION STREQUAL
     "fsim ${FSIM_VERSION} (C API ${FSIM_ABI_VERSION})")
  message(FATAL_ERROR "compiled v3 version identity failed: ${FSIM_COMPILED_VERSION}${FSIM_VERSION_ERROR}")
endif()

file(READ "${FSIM_SOURCE_DIR}/packaging/package-policy.txt" FSIM_POLICY)
string(FIND "\n${FSIM_POLICY}\n"
  "\nrelease_identity=fsim-v${FSIM_VERSION}\n" FSIM_POLICY_VERSION)
if(FSIM_POLICY_VERSION EQUAL -1)
  message(FATAL_ERROR "package policy differs from project version")
endif()
file(READ "${FSIM_SOURCE_DIR}/packaging/v3-release-record.txt" FSIM_RECORD)
string(FIND "\n${FSIM_RECORD}\n"
  "\ncompiled_version_required=${FSIM_VERSION}\n" FSIM_RECORD_VERSION)
if(FSIM_RECORD_VERSION EQUAL -1)
  message(FATAL_ERROR "release record differs from project version")
endif()

file(READ "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml" FSIM_CI)
string(FIND "${FSIM_CI}"
  "binary_archive: fsim-v${FSIM_VERSION}-windows-x86_64-llvm-mingw-llvm22.zip"
  FSIM_CI_ARCHIVE)
if(FSIM_CI_ARCHIVE EQUAL -1)
  message(FATAL_ERROR "hosted archive differs from project version")
endif()

foreach(FSIM_PATH IN ITEMS packaging/package-policy.txt .github/workflows/ci.yml)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_PATH}" FSIM_CONTENTS)
  if(FSIM_CONTENTS MATCHES "fsim-v2[.]0[.]0"
     OR FSIM_CONTENTS MATCHES "linux-x86_64-gcc13")
    message(FATAL_ERROR "active v3 product surface retains v2/GCC identity: ${FSIM_PATH}")
  endif()
endforeach()

list(LENGTH FSIM_IDS FSIM_ID_COUNT)
message(STATUS
  "v3 version identity: ${FSIM_ID_COUNT} required/additive surfaces agree with ${FSIM_VERSION}")

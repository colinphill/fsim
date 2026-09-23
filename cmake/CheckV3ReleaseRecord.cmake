# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CurrentEvidenceOwners.cmake")

set(record "${FSIM_SOURCE_DIR}/packaging/v3-release-record.txt")
file(STRINGS "${record}" rows)
set(keys)
foreach(row IN LISTS rows)
  if(row MATCHES "^#" OR row STREQUAL "")
    continue()
  endif()
  if(NOT row MATCHES "^([a-z0-9_]+)=(.+)$")
    message(FATAL_ERROR "malformed v3 release-record row: ${row}")
  endif()
  set(key "${CMAKE_MATCH_1}")
  set(value "${CMAKE_MATCH_2}")
  if(key IN_LIST keys OR value STREQUAL "PENDING" OR value STREQUAL "TO_FILL")
    message(FATAL_ERROR "duplicate or unfrozen v3 release key: ${key}")
  endif()
  list(APPEND keys "${key}")
  set("record_${key}" "${value}")
endforeach()

foreach(required IN ITEMS
    schema candidate compiled_version_required tag_name tag_kind tag_message
    release_notes known_issues api_reference source_artifact
    linux_clang22_no_llvm_artifact linux_clang22_llvm22_artifact
    windows_llvm_mingw_no_llvm_artifact windows_llvm_mingw_llvm22_artifact
    signature release_state batch188_required)
  if(NOT required IN_LIST keys)
    message(FATAL_ERROR "v3 release record omits ${required}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/CMakeLists.txt" FSIM_PROJECT_CMAKE)
if(NOT FSIM_PROJECT_CMAKE MATCHES
    "project\\([ \n\r\t]*fsim[ \n\r\t]+VERSION[ \n\r\t]+([0-9]+\\.[0-9]+\\.[0-9]+)")
  message(FATAL_ERROR "cannot read fsim project version")
endif()
set(FSIM_PROJECT_VERSION "${CMAKE_MATCH_1}")
file(READ "${FSIM_SOURCE_DIR}/include/fsim/version.hpp" FSIM_VERSION_HEADER)
if(NOT FSIM_VERSION_HEADER MATCHES
    "version = \"${FSIM_PROJECT_VERSION}\"")
  message(FATAL_ERROR "compiled version header differs from project version")
endif()

if(NOT record_schema STREQUAL "fsim-v3-release-record-v1"
    OR NOT record_candidate STREQUAL "v${FSIM_PROJECT_VERSION}"
    OR NOT record_compiled_version_required STREQUAL "${FSIM_PROJECT_VERSION}"
    OR NOT record_tag_name STREQUAL "v${FSIM_PROJECT_VERSION}"
    OR NOT record_tag_kind STREQUAL "annotated"
    OR NOT record_tag_message STREQUAL "fsim v${FSIM_PROJECT_VERSION}"
    OR NOT record_signature STREQUAL "unsigned-release"
    OR NOT record_release_state STREQUAL "prepared"
    OR record_batch188_required STREQUAL "")
  message(FATAL_ERROR "v3 release scalar contract drifted")
endif()

file(STRINGS "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt"
  FSIM_PACKAGE_PATHS)
foreach(FSIM_KEY IN ITEMS release_notes known_issues api_reference)
  set(FSIM_PATH "${record_${FSIM_KEY}}")
  fsim_current_evidence_file("${FSIM_PATH}")
  if(NOT FSIM_PATH IN_LIST FSIM_PACKAGE_PATHS)
    message(FATAL_ERROR "v3 release document is not packaged: ${FSIM_PATH}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/packaging/package-policy.txt" FSIM_POLICY)
string(FIND "\n${FSIM_POLICY}\n"
  "\nrelease_identity=fsim-v${FSIM_PROJECT_VERSION}\n"
  FSIM_POLICY_IDENTITY)
if(FSIM_POLICY_IDENTITY EQUAL -1)
  message(FATAL_ERROR "package policy differs from release version")
endif()

file(STRINGS "${FSIM_SOURCE_DIR}/packaging/v3-archive-layout.tsv" archives)
set(FSIM_ARCHIVE_NAMES)
foreach(FSIM_ROW IN LISTS archives)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(GET FSIM_FIELDS 0 FSIM_NAME)
  if(FSIM_NAME IN_LIST FSIM_ARCHIVE_NAMES)
    message(FATAL_ERROR "duplicate v3 archive identity: ${FSIM_NAME}")
  endif()
  list(APPEND FSIM_ARCHIVE_NAMES "${FSIM_NAME}")
endforeach()
foreach(key IN ITEMS source_artifact linux_clang22_no_llvm_artifact
    linux_clang22_llvm22_artifact windows_llvm_mingw_no_llvm_artifact
    windows_llvm_mingw_llvm22_artifact)
  set(FSIM_NAME "${record_${key}}")
  if(NOT FSIM_NAME MATCHES "^fsim-v${FSIM_PROJECT_VERSION}-.+[.]zip$"
     OR NOT FSIM_NAME IN_LIST FSIM_ARCHIVE_NAMES)
    message(FATAL_ERROR "v3 release artifact is not frozen in archive layout: ${record_${key}}")
  endif()
endforeach()

# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

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
    release_notes release_notes_sha256 known_issues known_issues_sha256
    api_reference api_reference_sha256 source_artifact
    linux_clang22_no_llvm_artifact linux_clang22_llvm22_artifact
    windows_llvm_mingw_no_llvm_artifact windows_llvm_mingw_llvm22_artifact
    archive_layout_sha256 supply_chain_inventory_sha256 signature
    release_state batch188_required)
  if(NOT required IN_LIST keys)
    message(FATAL_ERROR "v3 release record omits ${required}")
  endif()
endforeach()

if(NOT record_schema STREQUAL "fsim-v3-release-record-v1"
    OR NOT record_candidate STREQUAL "v3.0.0"
    OR NOT record_compiled_version_required STREQUAL "3.0.0"
    OR NOT record_tag_name STREQUAL "v3.0.0"
    OR NOT record_tag_kind STREQUAL "annotated"
    OR NOT record_tag_message STREQUAL "fsim v3.0.0"
    OR NOT record_signature STREQUAL "unsigned-release"
    OR NOT record_release_state STREQUAL "prepared")
  message(FATAL_ERROR "v3 release scalar contract drifted")
endif()

foreach(pair IN ITEMS
    "release_notes;release_notes_sha256"
    "known_issues;known_issues_sha256"
    "api_reference;api_reference_sha256")
  list(GET pair 0 path_key)
  list(GET pair 1 digest_key)
  set(path "${record_${path_key}}")
  if(NOT EXISTS "${FSIM_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "v3 release document is missing: ${path}")
  endif()
  file(SHA256 "${FSIM_SOURCE_DIR}/${path}" digest)
  if(NOT digest STREQUAL "${record_${digest_key}}")
    message(FATAL_ERROR "v3 release document digest drifted: ${path}")
  endif()
endforeach()

file(SHA256 "${FSIM_SOURCE_DIR}/packaging/v3-archive-layout.tsv" layout_digest)
file(SHA256 "${FSIM_SOURCE_DIR}/packaging/v3-supply-chain-inventory.tsv" supply_digest)
if(NOT layout_digest STREQUAL record_archive_layout_sha256
    OR NOT supply_digest STREQUAL record_supply_chain_inventory_sha256)
  message(FATAL_ERROR "v3 release governance digest drifted")
endif()

file(STRINGS "${FSIM_SOURCE_DIR}/packaging/v3-archive-layout.tsv" archives)
foreach(key IN ITEMS source_artifact linux_clang22_no_llvm_artifact
    linux_clang22_llvm22_artifact windows_llvm_mingw_no_llvm_artifact
    windows_llvm_mingw_llvm22_artifact)
  set(found FALSE)
  foreach(row IN LISTS archives)
    if(row MATCHES "^${record_${key}}\t")
      set(found TRUE)
      break()
    endif()
  endforeach()
  if(NOT found)
    message(FATAL_ERROR "v3 release artifact is not frozen in archive layout: ${record_${key}}")
  endif()
endforeach()

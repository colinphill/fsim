# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_TARGET_DIR "${FSIM_SOURCE_DIR}/packaging/targets")
set(FSIM_TARGET_FILES
    linux-clang22-llvm22.txt
    linux-gcc13-llvm22.txt
    linux-gcc13-no-llvm.txt)
set(FSIM_TARGET_IDS)
foreach(FSIM_TARGET_FILE IN LISTS FSIM_TARGET_FILES)
  set(FSIM_PATH "${FSIM_TARGET_DIR}/${FSIM_TARGET_FILE}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "Linux package target is missing: ${FSIM_TARGET_FILE}")
  endif()
  file(STRINGS "${FSIM_PATH}" FSIM_LINES ENCODING UTF-8)
  set(FSIM_KEYS)
  foreach(FSIM_LINE IN LISTS FSIM_LINES)
    if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#")
      continue()
    endif()
    if(NOT FSIM_LINE MATCHES "^([a-z0-9_]+)=(.*)$")
      message(FATAL_ERROR
        "malformed Linux package target row in ${FSIM_TARGET_FILE}: ${FSIM_LINE}")
    endif()
    set(FSIM_KEY "${CMAKE_MATCH_1}")
    set(FSIM_VALUE "${CMAKE_MATCH_2}")
    list(FIND FSIM_KEYS "${FSIM_KEY}" FSIM_DUPLICATE_INDEX)
    if(NOT FSIM_DUPLICATE_INDEX EQUAL -1 OR FSIM_VALUE STREQUAL "")
      message(FATAL_ERROR
        "duplicate/empty Linux package key ${FSIM_KEY}: ${FSIM_TARGET_FILE}")
    endif()
    list(APPEND FSIM_KEYS "${FSIM_KEY}")
    set("FSIM_VALUE_${FSIM_KEY}" "${FSIM_VALUE}")
  endforeach()
  foreach(FSIM_REQUIRED_KEY IN ITEMS
      schema target_id host compiler_family compiler_version llvm_mode
      llvm_version configurations archive_format job_timeout_minutes
      local_workers_minimum hosted_workers batch176_evidence batch177_required
      signature)
    list(FIND FSIM_KEYS "${FSIM_REQUIRED_KEY}" FSIM_KEY_INDEX)
    if(FSIM_KEY_INDEX EQUAL -1)
      message(FATAL_ERROR
        "Linux package target ${FSIM_TARGET_FILE} omits ${FSIM_REQUIRED_KEY}")
    endif()
  endforeach()
  if(NOT FSIM_VALUE_schema STREQUAL "fsim-package-target-v1"
     OR NOT FSIM_VALUE_host STREQUAL "linux-x86_64"
     OR NOT FSIM_VALUE_configurations STREQUAL "Debug,Release"
     OR NOT FSIM_VALUE_archive_format STREQUAL "zip"
     OR NOT FSIM_VALUE_job_timeout_minutes STREQUAL "120"
     OR NOT FSIM_VALUE_local_workers_minimum STREQUAL "8"
     OR NOT FSIM_VALUE_hosted_workers STREQUAL "4"
     OR NOT FSIM_VALUE_signature STREQUAL "unsigned-release"
     OR NOT FSIM_VALUE_batch177_required MATCHES "release-build"
     OR NOT FSIM_VALUE_batch177_required MATCHES "release-archive"
     OR NOT FSIM_VALUE_batch177_required MATCHES "install-smoke")
    message(FATAL_ERROR
      "Linux package target policy drifted: ${FSIM_TARGET_FILE}")
  endif()
  if(FSIM_VALUE_batch176_evidence MATCHES "release"
     OR FSIM_VALUE_batch176_evidence MATCHES "hosted")
    message(FATAL_ERROR
      "Batch 176 Linux package evidence overclaims Release/hosted work: ${FSIM_TARGET_FILE}")
  endif()
  if(FSIM_VALUE_llvm_mode STREQUAL "ON")
    if(NOT FSIM_VALUE_llvm_version STREQUAL "22.1.8")
      message(FATAL_ERROR "LLVM-enabled package target is not exact 22.1.8")
    endif()
  elseif(NOT FSIM_VALUE_llvm_mode STREQUAL "OFF"
         OR NOT FSIM_VALUE_llvm_version STREQUAL "none")
    message(FATAL_ERROR "invalid Linux LLVM package profile: ${FSIM_TARGET_FILE}")
  endif()
  list(FIND FSIM_TARGET_IDS "${FSIM_VALUE_target_id}" FSIM_ID_INDEX)
  if(NOT FSIM_ID_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate Linux package target: ${FSIM_VALUE_target_id}")
  endif()
  list(APPEND FSIM_TARGET_IDS "${FSIM_VALUE_target_id}")
endforeach()

list(SORT FSIM_TARGET_IDS)
if(NOT FSIM_TARGET_IDS STREQUAL
   "linux-x86_64-clang22-llvm22;linux-x86_64-gcc13-llvm22;linux-x86_64-gcc13-no-llvm")
  message(FATAL_ERROR "Linux package target set drifted: ${FSIM_TARGET_IDS}")
endif()
message(STATUS
  "Linux package definitions: 3 Debug-owned targets, every Release/archive/install/hosted result deferred to Batch 177")

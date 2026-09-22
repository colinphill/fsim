# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_warning_audit_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 6)
  message(FATAL_ERROR "v3 warning-audit ledger must contain four rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tplatform\tcompiler\tconfiguration\tllvm_mode\tconfigure_owner\tworkflow_owner\twarning_policy\towner")
  message(FATAL_ERROR "v3 warning-audit ledger header changed")
endif()

set(FSIM_EXPECTED_IDS
  V3WARN-LINUX-LLVM-DEBUG V3WARN-LINUX-LLVM-RELEASE
  V3WARN-WINDOWS-DEBUG-ON V3WARN-WINDOWS-RELEASE-ON)
set(FSIM_IDS)
set(FSIM_LINUX_COUNT 0)
set(FSIM_WINDOWS_COUNT 0)
set(FSIM_LEDGER_LANE_SIGNATURES)
foreach(FSIM_INDEX RANGE 2 5)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 9)
    message(FATAL_ERROR "v3 warning-audit row must contain nine fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_PLATFORM)
  list(GET FSIM_FIELDS 2 FSIM_COMPILER)
  list(GET FSIM_FIELDS 3 FSIM_CONFIGURATION)
  list(GET FSIM_FIELDS 4 FSIM_LLVM_MODE)
  list(GET FSIM_FIELDS 8 FSIM_OWNER)
  if(FSIM_ID IN_LIST FSIM_IDS OR NOT FSIM_ID IN_LIST FSIM_EXPECTED_IDS OR
     NOT FSIM_COMPILER MATCHES "^(clang(-22)?|llvm-mingw-clang)$" OR
     NOT FSIM_CONFIGURATION MATCHES "^(Debug|Release|RelWithDebInfo)$" OR
     NOT FSIM_LLVM_MODE STREQUAL "ON" OR
     NOT FSIM_OWNER STREQUAL "B188A-C20")
    message(FATAL_ERROR "v3 warning-audit row is malformed: ${FSIM_ID}")
  endif()
  if(FSIM_PLATFORM MATCHES "^ubuntu")
    math(EXPR FSIM_LINUX_COUNT "${FSIM_LINUX_COUNT} + 1")
    set(FSIM_LANE_JOB "linux-llvm22")
  elseif(FSIM_PLATFORM STREQUAL "windows-2022")
    math(EXPR FSIM_WINDOWS_COUNT "${FSIM_WINDOWS_COUNT} + 1")
    set(FSIM_LANE_JOB "windows-llvm-mingw")
  else()
    message(FATAL_ERROR "v3 warning-audit platform is unsupported: ${FSIM_ID}")
  endif()
  foreach(FSIM_FIELD_INDEX RANGE 5 7)
    list(GET FSIM_FIELDS ${FSIM_FIELD_INDEX} FSIM_REFERENCE)
    string(REGEX REPLACE ":[^:]+$" "" FSIM_RELATIVE "${FSIM_REFERENCE}")
    if(IS_ABSOLUTE "${FSIM_RELATIVE}" OR
       FSIM_RELATIVE MATCHES "(^|/)\.\.(/|$)" OR
       FSIM_RELATIVE MATCHES "[\\]" OR
       NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
      message(FATAL_ERROR
        "v3 warning-audit owner is unsafe or missing: ${FSIM_ID}")
    endif()
  endforeach()
  list(APPEND FSIM_LEDGER_LANE_SIGNATURES
    "${FSIM_LANE_JOB}|${FSIM_CONFIGURATION}|ON")
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()
foreach(FSIM_ID IN LISTS FSIM_EXPECTED_IDS)
  if(NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "v3 warning-audit lane is missing: ${FSIM_ID}")
  endif()
endforeach()
if(NOT FSIM_LINUX_COUNT EQUAL 2 OR NOT FSIM_WINDOWS_COUNT EQUAL 2)
  message(FATAL_ERROR
    "v3 warning-audit platform counts changed: ${FSIM_LINUX_COUNT}/${FSIM_WINDOWS_COUNT}")
endif()

function(fsim_require_warning_tokens FSIM_RELATIVE)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 warning-audit evidence changed in ${FSIM_RELATIVE}: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

function(fsim_require_warning_occurrences FSIM_TOKEN FSIM_EXPECTED_COUNT)
  string(LENGTH "${FSIM_TOKEN}" FSIM_TOKEN_LENGTH)
  string(LENGTH "${FSIM_WARNING_WORKFLOW}" FSIM_BEFORE_LENGTH)
  string(REPLACE "${FSIM_TOKEN}" "" FSIM_WITHOUT_TOKEN
    "${FSIM_WARNING_WORKFLOW}")
  string(LENGTH "${FSIM_WITHOUT_TOKEN}" FSIM_AFTER_LENGTH)
  math(EXPR FSIM_REMOVED_LENGTH
    "${FSIM_BEFORE_LENGTH} - ${FSIM_AFTER_LENGTH}")
  math(EXPR FSIM_OCCURRENCE_COUNT
    "${FSIM_REMOVED_LENGTH} / ${FSIM_TOKEN_LENGTH}")
  if(NOT FSIM_OCCURRENCE_COUNT EQUAL FSIM_EXPECTED_COUNT)
    message(FATAL_ERROR
      "v3 warning workflow token count changed: expected=${FSIM_EXPECTED_COUNT} actual=${FSIM_OCCURRENCE_COUNT}: ${FSIM_TOKEN}")
  endif()
endfunction()

fsim_require_warning_tokens(cmake/FsimWarnings.cmake
  "function(fsim_enable_warnings target)"
  "if(FSIM_WARNINGS_AS_ERRORS)"
  "target_compile_options(\${target} PRIVATE -Werror)")
fsim_require_warning_tokens(.github/workflows/ci.yml
  "linux-llvm22:"
  "windows-llvm-mingw:"
  "-DCMAKE_C_COMPILER=clang-22"
  "-DCMAKE_CXX_COMPILER=clang++-22"
  "-DCMAKE_C_COMPILER=$env:LLVM_MINGW_ROOT/bin/clang.exe"
  "-DCMAKE_CXX_COMPILER=$env:LLVM_MINGW_ROOT/bin/clang++.exe"
  "-DFSIM_WARNINGS_AS_ERRORS=ON"
  "Final closure checks"
  "if: matrix.configuration == 'Debug'"
  "-L '^recursive-closure$'"
  "--parallel 4")

file(READ "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml"
  FSIM_WARNING_WORKFLOW)
file(STRINGS "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml"
  FSIM_WARNING_WORKFLOW_LINES ENCODING UTF-8)
set(FSIM_IN_JOBS OFF)
set(FSIM_WARNING_JOB_KEYS)
set(FSIM_MATRIX_ROW_COUNT 0)
set(FSIM_DEBUG_ROW_COUNT 0)
set(FSIM_RELEASE_ROW_COUNT 0)
set(FSIM_MATRIX_LLVM_MODE_COUNT 0)
set(FSIM_WORKFLOW_LANE_SIGNATURES)
set(FSIM_CURRENT_JOB)
set(FSIM_CURRENT_CONFIGURATION)
set(FSIM_CURRENT_LLVM_MODE)
foreach(FSIM_WORKFLOW_LINE IN LISTS FSIM_WARNING_WORKFLOW_LINES)
  if(FSIM_WORKFLOW_LINE STREQUAL "jobs:")
    set(FSIM_IN_JOBS ON)
  elseif(FSIM_IN_JOBS AND
         FSIM_WORKFLOW_LINE MATCHES "^  ([A-Za-z0-9_-]+):[ ]*$")
    list(APPEND FSIM_WARNING_JOB_KEYS "${CMAKE_MATCH_1}")
    set(FSIM_CURRENT_JOB "${CMAKE_MATCH_1}")
  endif()

  string(TOUPPER "${FSIM_WORKFLOW_LINE}" FSIM_NORMALIZED_LINE)
  string(REPLACE " " "" FSIM_NORMALIZED_LINE "${FSIM_NORMALIZED_LINE}")
  string(REPLACE "\t" "" FSIM_NORMALIZED_LINE "${FSIM_NORMALIZED_LINE}")
  string(REPLACE "'" "" FSIM_NORMALIZED_LINE "${FSIM_NORMALIZED_LINE}")
  string(REPLACE "\"" "" FSIM_NORMALIZED_LINE "${FSIM_NORMALIZED_LINE}")
  if(FSIM_NORMALIZED_LINE MATCHES "LLVM_MODE(:|=)OFF" OR
     FSIM_NORMALIZED_LINE MATCHES "(NO[-_]LLVM|LLVM[-_]OFF)")
    message(FATAL_ERROR
      "non-LLVM hosted warning lane returned: ${FSIM_WORKFLOW_LINE}")
  endif()

  if(FSIM_WORKFLOW_LINE MATCHES
      "^[ ]+- configuration:[ ]*(Debug|Release)[ ]*$")
    set(FSIM_CURRENT_CONFIGURATION "${CMAKE_MATCH_1}")
    if(FSIM_CURRENT_JOB STREQUAL "linux-llvm22")
      set(FSIM_CURRENT_LLVM_MODE "ON")
    else()
      set(FSIM_CURRENT_LLVM_MODE)
    endif()
    math(EXPR FSIM_MATRIX_ROW_COUNT "${FSIM_MATRIX_ROW_COUNT} + 1")
    if(CMAKE_MATCH_1 STREQUAL "Debug")
      math(EXPR FSIM_DEBUG_ROW_COUNT "${FSIM_DEBUG_ROW_COUNT} + 1")
    else()
      math(EXPR FSIM_RELEASE_ROW_COUNT "${FSIM_RELEASE_ROW_COUNT} + 1")
    endif()
  endif()
  if(FSIM_WORKFLOW_LINE MATCHES "^[ ]+llvm_mode:")
    math(EXPR FSIM_MATRIX_LLVM_MODE_COUNT
      "${FSIM_MATRIX_LLVM_MODE_COUNT} + 1")
    if(NOT FSIM_NORMALIZED_LINE STREQUAL "LLVM_MODE:ON")
      message(FATAL_ERROR
        "hosted warning matrix LLVM mode is not ON: ${FSIM_WORKFLOW_LINE}")
    endif()
    set(FSIM_CURRENT_LLVM_MODE "ON")
  endif()
  if(FSIM_WORKFLOW_LINE MATCHES "^[ ]+retained_log:[ ]*(.+)[ ]*$")
    if(FSIM_CURRENT_JOB STREQUAL "" OR
       FSIM_CURRENT_CONFIGURATION STREQUAL "" OR
       FSIM_CURRENT_LLVM_MODE STREQUAL "")
      message(FATAL_ERROR
        "incomplete v3 warning workflow lane: ${FSIM_WORKFLOW_LINE}")
    endif()
    list(APPEND FSIM_WORKFLOW_LANE_SIGNATURES
      "${FSIM_CURRENT_JOB}|${FSIM_CURRENT_CONFIGURATION}|${FSIM_CURRENT_LLVM_MODE}")
  endif()
endforeach()
list(SORT FSIM_WARNING_JOB_KEYS)
if(NOT FSIM_WARNING_JOB_KEYS STREQUAL
   "linux-llvm22;windows-llvm-mingw")
  message(FATAL_ERROR
    "v3 warning workflow job set changed: ${FSIM_WARNING_JOB_KEYS}")
endif()
if(NOT FSIM_MATRIX_ROW_COUNT EQUAL 4 OR
   NOT FSIM_DEBUG_ROW_COUNT EQUAL 2 OR
   NOT FSIM_RELEASE_ROW_COUNT EQUAL 2 OR
   NOT FSIM_MATRIX_LLVM_MODE_COUNT EQUAL 2)
  message(FATAL_ERROR
    "v3 warning workflow matrix changed: rows=${FSIM_MATRIX_ROW_COUNT} debug=${FSIM_DEBUG_ROW_COUNT} release=${FSIM_RELEASE_ROW_COUNT} llvm-mode=${FSIM_MATRIX_LLVM_MODE_COUNT}")
endif()
list(SORT FSIM_LEDGER_LANE_SIGNATURES)
list(SORT FSIM_WORKFLOW_LANE_SIGNATURES)
if(NOT FSIM_WORKFLOW_LANE_SIGNATURES STREQUAL
   FSIM_LEDGER_LANE_SIGNATURES)
  message(FATAL_ERROR
    "v3 warning workflow lanes do not match the ledger: workflow=${FSIM_WORKFLOW_LANE_SIGNATURES} ledger=${FSIM_LEDGER_LANE_SIGNATURES}")
endif()
fsim_require_warning_occurrences("-DFSIM_LLVM_MODE=ON" 1)
fsim_require_warning_occurrences(
  "-DFSIM_LLVM_MODE=\${{ matrix.llvm_mode }}" 1)
fsim_require_warning_occurrences("-DFSIM_WARNINGS_AS_ERRORS=ON" 2)
fsim_require_warning_occurrences("--parallel 4" 5)
fsim_require_warning_occurrences("timeout-minutes: 120" 2)
fsim_require_warning_tokens(tests/CMakeLists.txt
  "NAME fsim.v3-warning-audit"
  "CheckV3WarningAudit.cmake")

file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
set(FSIM_EXPECTED_DIGEST
  "35b4ab3a0bd75d8372742aa46d023b91bf13db8b6238d1db9ac2590400aa646e")
if(NOT FSIM_LEDGER_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "v3 warning-audit digest changed: expected=${FSIM_EXPECTED_DIGEST} actual=${FSIM_LEDGER_DIGEST}")
endif()
message(STATUS
  "v3 warning audit passed: lanes=4 linux=2 windows=2 digest=${FSIM_LEDGER_DIGEST}")

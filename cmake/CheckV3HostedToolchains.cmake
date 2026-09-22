# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_hosted_toolchain_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 6)
  message(FATAL_ERROR "v3 hosted-toolchain ledger must contain four rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\trunner\tcompiler\tcompiler_version\tllvm_mode\tllvm_version\tconfiguration\tartifact\tretained_log\ttimeout_minutes\tworkers\towner")
  message(FATAL_ERROR "v3 hosted-toolchain ledger header changed")
endif()

set(FSIM_EXPECTED_IDS
  V3HOST-LINUX-LLVM-DEBUG V3HOST-LINUX-LLVM-RELEASE
  V3HOST-WINDOWS-DEBUG-ON V3HOST-WINDOWS-RELEASE-ON)
set(FSIM_IDS)
set(FSIM_LINUX_COUNT 0)
set(FSIM_WINDOWS_COUNT 0)
set(FSIM_LLVM_ON_COUNT 0)
set(FSIM_ARTIFACTS)
set(FSIM_RETAINED_LOGS)
set(FSIM_LEDGER_LANE_SIGNATURES)
foreach(FSIM_INDEX RANGE 2 5)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR "v3 hosted-toolchain row must contain twelve fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_RUNNER)
  list(GET FSIM_FIELDS 2 FSIM_COMPILER)
  list(GET FSIM_FIELDS 4 FSIM_LLVM_MODE)
  list(GET FSIM_FIELDS 5 FSIM_LLVM_VERSION)
  list(GET FSIM_FIELDS 6 FSIM_CONFIGURATION)
  list(GET FSIM_FIELDS 7 FSIM_ARTIFACT)
  list(GET FSIM_FIELDS 8 FSIM_RETAINED_LOG)
  list(GET FSIM_FIELDS 9 FSIM_TIMEOUT)
  list(GET FSIM_FIELDS 10 FSIM_WORKERS)
  list(GET FSIM_FIELDS 11 FSIM_OWNER)
  if(FSIM_ID IN_LIST FSIM_IDS OR NOT FSIM_ID IN_LIST FSIM_EXPECTED_IDS OR
     NOT FSIM_RUNNER MATCHES "^(ubuntu-latest|ubuntu-24.04|windows-2022)$" OR
     NOT FSIM_COMPILER MATCHES "^(clang|llvm-mingw)$" OR
     NOT FSIM_LLVM_MODE STREQUAL "ON" OR
     NOT FSIM_CONFIGURATION MATCHES "^(Debug|Release|RelWithDebInfo)$" OR
     NOT FSIM_TIMEOUT STREQUAL "120" OR NOT FSIM_WORKERS STREQUAL "2" OR
     NOT FSIM_OWNER STREQUAL "B188A-C20" OR FSIM_ARTIFACT STREQUAL "" OR
     IS_ABSOLUTE "${FSIM_RETAINED_LOG}" OR
     FSIM_RETAINED_LOG MATCHES "(^|/)\.\.(/|$)" OR
     NOT FSIM_RETAINED_LOG MATCHES "^build/qualification/.*\.log$")
    message(FATAL_ERROR "v3 hosted-toolchain row is malformed: ${FSIM_ID}")
  endif()
  if(NOT FSIM_LLVM_VERSION STREQUAL "22.1.8")
    message(FATAL_ERROR "v3 hosted LLVM version changed: ${FSIM_ID}")
  endif()
  math(EXPR FSIM_LLVM_ON_COUNT "${FSIM_LLVM_ON_COUNT} + 1")
  if(FSIM_RUNNER MATCHES "^ubuntu")
    math(EXPR FSIM_LINUX_COUNT "${FSIM_LINUX_COUNT} + 1")
    set(FSIM_LANE_JOB "linux-llvm22")
  else()
    math(EXPR FSIM_WINDOWS_COUNT "${FSIM_WINDOWS_COUNT} + 1")
    set(FSIM_LANE_JOB "windows-llvm-mingw")
  endif()
  list(APPEND FSIM_ARTIFACTS "${FSIM_ARTIFACT}")
  list(APPEND FSIM_RETAINED_LOGS "${FSIM_RETAINED_LOG}")
  list(APPEND FSIM_LEDGER_LANE_SIGNATURES
    "${FSIM_LANE_JOB}|${FSIM_CONFIGURATION}|ON|${FSIM_ARTIFACT}|${FSIM_RETAINED_LOG}")
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()
foreach(FSIM_ID IN LISTS FSIM_EXPECTED_IDS)
  if(NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "v3 hosted-toolchain lane is missing: ${FSIM_ID}")
  endif()
endforeach()
if(NOT FSIM_LINUX_COUNT EQUAL 2 OR NOT FSIM_WINDOWS_COUNT EQUAL 2 OR
   NOT FSIM_LLVM_ON_COUNT EQUAL 4)
  message(FATAL_ERROR
    "v3 hosted-toolchain counts changed: linux=${FSIM_LINUX_COUNT} windows=${FSIM_WINDOWS_COUNT} llvm=${FSIM_LLVM_ON_COUNT}")
endif()

function(fsim_require_hosted_tokens FSIM_RELATIVE)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 hosted-toolchain evidence changed in ${FSIM_RELATIVE}: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

function(fsim_require_hosted_occurrences FSIM_TOKEN FSIM_EXPECTED_COUNT)
  string(LENGTH "${FSIM_TOKEN}" FSIM_TOKEN_LENGTH)
  string(LENGTH "${FSIM_HOSTED_WORKFLOW}" FSIM_BEFORE_LENGTH)
  string(REPLACE "${FSIM_TOKEN}" "" FSIM_WITHOUT_TOKEN
    "${FSIM_HOSTED_WORKFLOW}")
  string(LENGTH "${FSIM_WITHOUT_TOKEN}" FSIM_AFTER_LENGTH)
  math(EXPR FSIM_REMOVED_LENGTH
    "${FSIM_BEFORE_LENGTH} - ${FSIM_AFTER_LENGTH}")
  math(EXPR FSIM_OCCURRENCE_COUNT
    "${FSIM_REMOVED_LENGTH} / ${FSIM_TOKEN_LENGTH}")
  if(NOT FSIM_OCCURRENCE_COUNT EQUAL FSIM_EXPECTED_COUNT)
    message(FATAL_ERROR
      "v3 hosted workflow token count changed: expected=${FSIM_EXPECTED_COUNT} actual=${FSIM_OCCURRENCE_COUNT}: ${FSIM_TOKEN}")
  endif()
endfunction()

fsim_require_hosted_tokens(.github/workflows/ci.yml
  "runs-on: ubuntu-24.04"
  "runs-on: windows-2022"
  "timeout-minutes: 120"
  "clang-22 --version"
  "clang++-22 --version"
  "installed_version=$(/usr/lib/llvm-22/bin/llvm-config --version)"
  "if [[ \"\${installed_version}\" != \"22.1.8\" ]]"
  "-Release \"20260616\""
  "-Version \"22.1.8\""
  "-Sha256 \"b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35\""
  "test \"$(/clang64/bin/llvm-config.exe --version)\" = \"22.1.8\""
  "Final closure checks"
  "if: matrix.configuration == 'Debug'"
  "-L '^recursive-closure$'"
  "--parallel 4"
  "actions/upload-artifact@v7"
  "if-no-files-found: error")

file(READ "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml"
  FSIM_HOSTED_WORKFLOW)
file(STRINGS "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml"
  FSIM_HOSTED_WORKFLOW_LINES ENCODING UTF-8)
set(FSIM_IN_JOBS OFF)
set(FSIM_HOSTED_JOB_KEYS)
set(FSIM_MATRIX_ROW_COUNT 0)
set(FSIM_DEBUG_ROW_COUNT 0)
set(FSIM_RELEASE_ROW_COUNT 0)
set(FSIM_MATRIX_LLVM_MODE_COUNT 0)
set(FSIM_WORKFLOW_LANE_SIGNATURES)
set(FSIM_CURRENT_JOB)
set(FSIM_CURRENT_CONFIGURATION)
set(FSIM_CURRENT_LLVM_MODE)
set(FSIM_CURRENT_ARTIFACT)
foreach(FSIM_WORKFLOW_LINE IN LISTS FSIM_HOSTED_WORKFLOW_LINES)
  if(FSIM_WORKFLOW_LINE STREQUAL "jobs:")
    set(FSIM_IN_JOBS ON)
  elseif(FSIM_IN_JOBS AND
         FSIM_WORKFLOW_LINE MATCHES "^  ([A-Za-z0-9_-]+):[ ]*$")
    list(APPEND FSIM_HOSTED_JOB_KEYS "${CMAKE_MATCH_1}")
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
      "non-LLVM hosted product lane returned: ${FSIM_WORKFLOW_LINE}")
  endif()

  if(FSIM_WORKFLOW_LINE MATCHES
      "^[ ]+- configuration:[ ]*(Debug|Release)[ ]*$")
    set(FSIM_CURRENT_CONFIGURATION "${CMAKE_MATCH_1}")
    set(FSIM_CURRENT_ARTIFACT)
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
        "hosted matrix LLVM mode is not ON: ${FSIM_WORKFLOW_LINE}")
    endif()
    set(FSIM_CURRENT_LLVM_MODE "ON")
  endif()
  if(FSIM_WORKFLOW_LINE MATCHES "^[ ]+artifact:[ ]*(.+)[ ]*$")
    set(FSIM_CURRENT_ARTIFACT "${CMAKE_MATCH_1}")
  endif()
  if(FSIM_WORKFLOW_LINE MATCHES "^[ ]+retained_log:[ ]*(.+)[ ]*$")
    set(FSIM_CURRENT_RETAINED_LOG "${CMAKE_MATCH_1}")
    if(FSIM_CURRENT_JOB STREQUAL "" OR
       FSIM_CURRENT_CONFIGURATION STREQUAL "" OR
       FSIM_CURRENT_LLVM_MODE STREQUAL "" OR
       FSIM_CURRENT_ARTIFACT STREQUAL "")
      message(FATAL_ERROR
        "incomplete v3 hosted workflow lane: ${FSIM_WORKFLOW_LINE}")
    endif()
    list(APPEND FSIM_WORKFLOW_LANE_SIGNATURES
      "${FSIM_CURRENT_JOB}|${FSIM_CURRENT_CONFIGURATION}|${FSIM_CURRENT_LLVM_MODE}|${FSIM_CURRENT_ARTIFACT}|${FSIM_CURRENT_RETAINED_LOG}")
  endif()
endforeach()
list(SORT FSIM_HOSTED_JOB_KEYS)
if(NOT FSIM_HOSTED_JOB_KEYS STREQUAL
   "linux-llvm22;windows-llvm-mingw")
  message(FATAL_ERROR
    "v3 hosted workflow job set changed: ${FSIM_HOSTED_JOB_KEYS}")
endif()
if(NOT FSIM_MATRIX_ROW_COUNT EQUAL 4 OR
   NOT FSIM_DEBUG_ROW_COUNT EQUAL 2 OR
   NOT FSIM_RELEASE_ROW_COUNT EQUAL 2 OR
   NOT FSIM_MATRIX_LLVM_MODE_COUNT EQUAL 2)
  message(FATAL_ERROR
    "v3 hosted workflow matrix changed: rows=${FSIM_MATRIX_ROW_COUNT} debug=${FSIM_DEBUG_ROW_COUNT} release=${FSIM_RELEASE_ROW_COUNT} llvm-mode=${FSIM_MATRIX_LLVM_MODE_COUNT}")
endif()
list(SORT FSIM_LEDGER_LANE_SIGNATURES)
list(SORT FSIM_WORKFLOW_LANE_SIGNATURES)
if(NOT FSIM_WORKFLOW_LANE_SIGNATURES STREQUAL
   FSIM_LEDGER_LANE_SIGNATURES)
  message(FATAL_ERROR
    "v3 hosted workflow lanes do not match the ledger: workflow=${FSIM_WORKFLOW_LANE_SIGNATURES} ledger=${FSIM_LEDGER_LANE_SIGNATURES}")
endif()
fsim_require_hosted_occurrences("-DFSIM_LLVM_MODE=ON" 1)
fsim_require_hosted_occurrences(
  "-DFSIM_LLVM_MODE=\${{ matrix.llvm_mode }}" 1)
fsim_require_hosted_occurrences("--parallel 4" 5)
fsim_require_hosted_occurrences("timeout-minutes: 120" 2)
foreach(FSIM_INDEX RANGE 0 3)
  list(GET FSIM_ARTIFACTS ${FSIM_INDEX} FSIM_ARTIFACT)
  list(GET FSIM_RETAINED_LOGS ${FSIM_INDEX} FSIM_RETAINED_LOG)
  fsim_require_hosted_occurrences("${FSIM_ARTIFACT}" 1)
  fsim_require_hosted_occurrences("${FSIM_RETAINED_LOG}" 1)
  set(FSIM_LANE_MAPPING
    "artifact: ${FSIM_ARTIFACT}\n            retained_log: ${FSIM_RETAINED_LOG}")
  fsim_require_hosted_occurrences("${FSIM_LANE_MAPPING}" 1)
endforeach()
fsim_require_hosted_tokens(tests/CMakeLists.txt
  "NAME fsim.v3-hosted-toolchains"
  "CheckV3HostedToolchains.cmake")

file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
set(FSIM_EXPECTED_DIGEST "658d96161909dd0a982399068ab1b49b316c333f543423922b262e013416b8d5")
if(NOT FSIM_LEDGER_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "v3 hosted-toolchain digest changed: expected=${FSIM_EXPECTED_DIGEST} actual=${FSIM_LEDGER_DIGEST}")
endif()
message(STATUS
  "v3 hosted toolchains passed: lanes=4 linux=2 windows=2 llvm22=4 digest=${FSIM_LEDGER_DIGEST}")

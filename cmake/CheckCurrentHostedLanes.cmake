# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED FSIM_SOURCE_DIR OR FSIM_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CurrentEvidenceOwners.cmake")

file(READ "${FSIM_SOURCE_DIR}/include/fsim/version.hpp" FSIM_VERSION_HEADER)
if(NOT FSIM_VERSION_HEADER MATCHES
    "production_llvm_version = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
  message(FATAL_ERROR "production LLVM version is unavailable")
endif()
set(FSIM_LLVM_VERSION "${CMAKE_MATCH_1}")

set(FSIM_HOSTED_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_hosted_toolchain_inventory.tsv")
file(STRINGS "${FSIM_HOSTED_LEDGER}" FSIM_HOSTED_ROWS)
set(FSIM_HOSTED_IDS)
set(FSIM_HOSTED_LANES)
foreach(FSIM_ROW IN LISTS FSIM_HOSTED_ROWS)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW MATCHES "^id\t"
     OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 12)
    message(FATAL_ERROR "invalid hosted lane row: ${FSIM_ROW}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_RUNNER)
  list(GET FSIM_FIELDS 2 FSIM_COMPILER)
  list(GET FSIM_FIELDS 3 FSIM_COMPILER_VERSION)
  list(GET FSIM_FIELDS 4 FSIM_MODE)
  list(GET FSIM_FIELDS 5 FSIM_LEDGER_LLVM_VERSION)
  list(GET FSIM_FIELDS 6 FSIM_CONFIG)
  list(GET FSIM_FIELDS 7 FSIM_ARTIFACT)
  list(GET FSIM_FIELDS 8 FSIM_LOG)
  list(GET FSIM_FIELDS 9 FSIM_TIMEOUT)
  list(GET FSIM_FIELDS 11 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^V3HOST-[A-Z0-9-]+$"
     OR FSIM_ID IN_LIST FSIM_HOSTED_IDS
     OR NOT FSIM_CONFIG MATCHES "^(Debug|Release)$"
     OR NOT FSIM_MODE STREQUAL "ON"
     OR NOT FSIM_LEDGER_LLVM_VERSION STREQUAL FSIM_LLVM_VERSION
     OR FSIM_TIMEOUT LESS 1 OR FSIM_TIMEOUT GREATER 120
     OR FSIM_ARTIFACT STREQUAL "" OR NOT FSIM_OWNER STREQUAL "V3-HOSTED-CI")
    message(FATAL_ERROR "invalid hosted lane identity: ${FSIM_ID}")
  endif()
  if(FSIM_RUNNER STREQUAL "ubuntu-24.04"
     AND FSIM_COMPILER STREQUAL "clang"
     AND FSIM_COMPILER_VERSION STREQUAL "22")
    set(FSIM_JOB linux-llvm22)
  elseif(FSIM_RUNNER STREQUAL "windows-2022"
      AND FSIM_COMPILER STREQUAL "llvm-mingw"
      AND FSIM_COMPILER_VERSION STREQUAL "20260616")
    set(FSIM_JOB windows-llvm-mingw)
  else()
    message(FATAL_ERROR "unsupported hosted compiler/runner: ${FSIM_ID}")
  endif()
  if(IS_ABSOLUTE "${FSIM_LOG}" OR FSIM_LOG MATCHES "\\\\"
     OR FSIM_LOG MATCHES "(^|/)\\.\\.?(/|$)"
     OR NOT FSIM_LOG MATCHES
       "^build/qualification/ci-[A-Za-z0-9._-]+[.]log$")
    message(FATAL_ERROR "unsafe hosted retained log: ${FSIM_ID}")
  endif()
  set(FSIM_LANE "${FSIM_JOB}|${FSIM_CONFIG}")
  if(FSIM_LANE IN_LIST FSIM_HOSTED_LANES)
    message(FATAL_ERROR "duplicate hosted lane: ${FSIM_LANE}")
  endif()
  list(APPEND FSIM_HOSTED_IDS "${FSIM_ID}")
  list(APPEND FSIM_HOSTED_LANES "${FSIM_LANE}")
endforeach()

set(FSIM_WARNING_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_warning_audit_inventory.tsv")
file(STRINGS "${FSIM_WARNING_LEDGER}" FSIM_WARNING_ROWS)
set(FSIM_WARNING_IDS)
set(FSIM_WARNING_LANES)
foreach(FSIM_ROW IN LISTS FSIM_WARNING_ROWS)
  if(FSIM_ROW MATCHES "^#" OR FSIM_ROW MATCHES "^id\t"
     OR FSIM_ROW STREQUAL "")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 9)
    message(FATAL_ERROR "invalid hosted warning row: ${FSIM_ROW}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_RUNNER)
  list(GET FSIM_FIELDS 2 FSIM_COMPILER)
  list(GET FSIM_FIELDS 3 FSIM_CONFIG)
  list(GET FSIM_FIELDS 4 FSIM_MODE)
  list(GET FSIM_FIELDS 5 FSIM_CONFIG_PATH)
  list(GET FSIM_FIELDS 6 FSIM_WORKFLOW_PATH)
  list(GET FSIM_FIELDS 7 FSIM_POLICY)
  list(GET FSIM_FIELDS 8 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^V3WARN-[A-Z0-9-]+$"
     OR FSIM_ID IN_LIST FSIM_WARNING_IDS
     OR NOT FSIM_CONFIG MATCHES "^(Debug|Release)$"
     OR NOT FSIM_MODE STREQUAL "ON"
     OR NOT FSIM_OWNER STREQUAL "V3-HOSTED-CI")
    message(FATAL_ERROR "invalid hosted warning identity: ${FSIM_ID}")
  endif()
  if(FSIM_RUNNER STREQUAL "ubuntu-24.04"
     AND FSIM_COMPILER STREQUAL "clang-22")
    set(FSIM_JOB linux-llvm22)
  elseif(FSIM_RUNNER STREQUAL "windows-2022"
      AND FSIM_COMPILER STREQUAL "llvm-mingw-clang")
    set(FSIM_JOB windows-llvm-mingw)
  else()
    message(FATAL_ERROR "unsupported warning compiler/runner: ${FSIM_ID}")
  endif()
  foreach(FSIM_PATH IN ITEMS "${FSIM_CONFIG_PATH}" "${FSIM_WORKFLOW_PATH}")
    fsim_current_evidence_file("${FSIM_PATH}")
  endforeach()
  if(NOT FSIM_POLICY STREQUAL "cmake/FsimWarnings.cmake:-Werror")
    message(FATAL_ERROR "hosted warning policy differs: ${FSIM_ID}")
  endif()
  set(FSIM_LANE "${FSIM_JOB}|${FSIM_CONFIG}")
  if(FSIM_LANE IN_LIST FSIM_WARNING_LANES)
    message(FATAL_ERROR "duplicate warning lane: ${FSIM_LANE}")
  endif()
  list(APPEND FSIM_WARNING_IDS "${FSIM_ID}")
  list(APPEND FSIM_WARNING_LANES "${FSIM_LANE}")
endforeach()

file(STRINGS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt"
  FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(FSIM_ID MATCHES "^V3HOST-" AND NOT FSIM_ID IN_LIST FSIM_HOSTED_IDS)
    message(FATAL_ERROR "required hosted lane is missing: ${FSIM_ID}")
  elseif(FSIM_ID MATCHES "^V3WARN-"
      AND NOT FSIM_ID IN_LIST FSIM_WARNING_IDS)
    message(FATAL_ERROR "required warning lane is missing: ${FSIM_ID}")
  endif()
endforeach()
foreach(FSIM_LANE IN LISTS FSIM_HOSTED_LANES)
  if(NOT FSIM_LANE IN_LIST FSIM_WARNING_LANES)
    message(FATAL_ERROR "hosted lane has no warning owner: ${FSIM_LANE}")
  endif()
endforeach()
foreach(FSIM_LANE IN LISTS FSIM_WARNING_LANES)
  if(NOT FSIM_LANE IN_LIST FSIM_HOSTED_LANES)
    message(FATAL_ERROR "warning lane has no hosted owner: ${FSIM_LANE}")
  endif()
endforeach()

file(STRINGS "${FSIM_SOURCE_DIR}/.github/workflows/ci.yml"
  FSIM_WORKFLOW_LINES)
set(FSIM_IN_JOBS OFF)
set(FSIM_CURRENT_JOB "")
foreach(FSIM_LINE IN LISTS FSIM_WORKFLOW_LINES)
  if(FSIM_LINE STREQUAL "jobs:")
    set(FSIM_IN_JOBS ON)
  elseif(FSIM_IN_JOBS AND FSIM_LINE MATCHES "^  ([A-Za-z0-9_-]+):[ ]*$")
    set(FSIM_CURRENT_JOB "${CMAKE_MATCH_1}")
  endif()
  if(FSIM_CURRENT_JOB STREQUAL "linux-llvm22")
    string(APPEND FSIM_LINUX_JOB "${FSIM_LINE}\n")
  elseif(FSIM_CURRENT_JOB STREQUAL "windows-llvm-mingw")
    string(APPEND FSIM_WINDOWS_JOB "${FSIM_LINE}\n")
  endif()
endforeach()

function(fsim_require_job_token FSIM_JOB_TEXT FSIM_TOKEN)
  string(FIND "${FSIM_JOB_TEXT}" "${FSIM_TOKEN}" FSIM_FOUND)
  if(FSIM_FOUND EQUAL -1)
    message(FATAL_ERROR "hosted job omits ${FSIM_TOKEN}")
  endif()
endfunction()

foreach(FSIM_JOB IN ITEMS LINUX WINDOWS)
  set(FSIM_TEXT "${FSIM_${FSIM_JOB}_JOB}")
  if(FSIM_TEXT STREQUAL "")
    message(FATAL_ERROR "required hosted job is missing: ${FSIM_JOB}")
  endif()
  string(TOLOWER "${FSIM_TEXT}" FSIM_LOWER_JOB)
  if(FSIM_LOWER_JOB MATCHES "batch[0-9]")
    message(FATAL_ERROR "hosted job retains batch-specific naming: ${FSIM_JOB}")
  endif()
  string(TOUPPER "${FSIM_TEXT}" FSIM_NORMALIZED_JOB)
  if(FSIM_NORMALIZED_JOB MATCHES
      "LLVM_MODE:[ ]*['\"]?OFF|FSIM_LLVM_MODE=OFF|NO[-_]LLVM")
    message(FATAL_ERROR "non-LLVM hosted lane is present: ${FSIM_JOB}")
  endif()
  if(NOT FSIM_TEXT MATCHES "timeout-minutes: ([0-9]+)")
    message(FATAL_ERROR "hosted job has no timeout: ${FSIM_JOB}")
  endif()
  if(CMAKE_MATCH_1 LESS 1 OR CMAKE_MATCH_1 GREATER 120)
    message(FATAL_ERROR "hosted job timeout is unbounded: ${FSIM_JOB}")
  endif()
  fsim_require_job_token("${FSIM_TEXT}" "-DFSIM_WARNINGS_AS_ERRORS=ON")
  string(REGEX MATCHALL "ctest[ ]+--test-dir" FSIM_CTEST_RUNS
    "${FSIM_TEXT}")
  list(LENGTH FSIM_CTEST_RUNS FSIM_CTEST_RUN_COUNT)
  if(NOT FSIM_CTEST_RUN_COUNT EQUAL 1
     OR FSIM_TEXT MATCHES "(^|[ \t])-L(E)?[ \t]")
    message(FATAL_ERROR
      "hosted job must run one unfiltered CTest selection: ${FSIM_JOB}")
  endif()
  string(REGEX MATCHALL "--parallel[ ]+[0-9]+" FSIM_PARALLEL
    "${FSIM_TEXT}")
  list(LENGTH FSIM_PARALLEL FSIM_PARALLEL_COUNT)
  if(FSIM_PARALLEL_COUNT LESS 2)
    message(FATAL_ERROR "hosted build/test parallelism is missing: ${FSIM_JOB}")
  endif()
  foreach(FSIM_SETTING IN LISTS FSIM_PARALLEL)
    string(REGEX REPLACE ".*--parallel[ ]+" "" FSIM_WORKERS
      "${FSIM_SETTING}")
    if(FSIM_WORKERS LESS 1 OR FSIM_WORKERS GREATER 4)
      message(FATAL_ERROR "hosted parallelism exceeds bound: ${FSIM_JOB}")
    endif()
  endforeach()
endforeach()

foreach(FSIM_LOG_FIELD IN ITEMS binary_log install_log)
  string(REGEX MATCHALL
    "${FSIM_LOG_FIELD}:[ ]*build/qualification/ci-[A-Za-z0-9._-]+[.]log"
    FSIM_WINDOWS_PACKAGE_LOGS "${FSIM_WINDOWS_JOB}")
  list(LENGTH FSIM_WINDOWS_PACKAGE_LOGS FSIM_PACKAGE_LOG_COUNT)
  if(NOT FSIM_PACKAGE_LOG_COUNT EQUAL 1)
    message(FATAL_ERROR
      "Windows hosted job lacks stable ${FSIM_LOG_FIELD} naming")
  endif()
endforeach()

if(DEFINED FSIM_BINARY_DIR AND NOT FSIM_BINARY_DIR STREQUAL "")
  execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${FSIM_BINARY_DIR}"
      --show-only=json-v1
    RESULT_VARIABLE FSIM_CTEST_STATUS
    OUTPUT_VARIABLE FSIM_CTEST_METADATA
    ERROR_VARIABLE FSIM_CTEST_ERROR)
  if(NOT FSIM_CTEST_STATUS EQUAL 0)
    message(FATAL_ERROR
      "unable to enumerate hosted CTest selection: ${FSIM_CTEST_ERROR}")
  endif()
  string(JSON FSIM_TEST_COUNT LENGTH "${FSIM_CTEST_METADATA}" tests)
  if(FSIM_TEST_COUNT LESS 1)
    message(FATAL_ERROR "hosted CTest selection is empty")
  endif()
  math(EXPR FSIM_LAST_TEST "${FSIM_TEST_COUNT} - 1")
  set(FSIM_TEST_NAMES)
  foreach(FSIM_INDEX RANGE 0 ${FSIM_LAST_TEST})
    string(JSON FSIM_TEST_NAME GET "${FSIM_CTEST_METADATA}"
      tests ${FSIM_INDEX} name)
    if(FSIM_TEST_NAME IN_LIST FSIM_TEST_NAMES)
      message(FATAL_ERROR "duplicate hosted CTest name: ${FSIM_TEST_NAME}")
    endif()
    list(APPEND FSIM_TEST_NAMES "${FSIM_TEST_NAME}")
  endforeach()
  message(STATUS
    "hosted lanes select all ${FSIM_TEST_COUNT} registered CTests once")
endif()

fsim_require_job_token("${FSIM_LINUX_JOB}" "runs-on: ubuntu-24.04")
fsim_require_job_token("${FSIM_LINUX_JOB}" "-DCMAKE_C_COMPILER=clang-22")
fsim_require_job_token("${FSIM_LINUX_JOB}" "-DCMAKE_CXX_COMPILER=clang++-22")
fsim_require_job_token("${FSIM_LINUX_JOB}" "-DFSIM_LLVM_MODE=ON")
fsim_require_job_token("${FSIM_LINUX_JOB}"
  "installed_version=\$(/usr/lib/llvm-22/bin/llvm-config --version)")
fsim_require_job_token("${FSIM_LINUX_JOB}" "!= \"${FSIM_LLVM_VERSION}\"")
fsim_require_job_token("${FSIM_WINDOWS_JOB}" "runs-on: windows-2022")
fsim_require_job_token("${FSIM_WINDOWS_JOB}" "-Release \"20260616\"")
fsim_require_job_token("${FSIM_WINDOWS_JOB}"
  "-Version \"${FSIM_LLVM_VERSION}\"")
fsim_require_job_token("${FSIM_WINDOWS_JOB}"
  "-DCMAKE_C_COMPILER=\$env:LLVM_MINGW_ROOT/bin/clang.exe")
fsim_require_job_token("${FSIM_WINDOWS_JOB}"
  "-DCMAKE_CXX_COMPILER=\$env:LLVM_MINGW_ROOT/bin/clang++.exe")
fsim_require_job_token("${FSIM_WINDOWS_JOB}"
  "-DFSIM_LLVM_MODE=\${{ matrix.llvm_mode }}")
fsim_require_job_token("${FSIM_WINDOWS_JOB}"
  "llvm-config.exe --version)\" = \"${FSIM_LLVM_VERSION}\"")

set(FSIM_WORKFLOW_LANES)
foreach(FSIM_JOB IN ITEMS linux-llvm22 windows-llvm-mingw)
  if(FSIM_JOB STREQUAL "linux-llvm22")
    set(FSIM_TEXT "${FSIM_LINUX_JOB}")
  else()
    set(FSIM_TEXT "${FSIM_WINDOWS_JOB}")
  endif()
  string(REPLACE "\n" ";" FSIM_LINES "${FSIM_TEXT}")
  set(FSIM_CONFIG "")
  set(FSIM_MODE "")
  set(FSIM_ARTIFACT "")
  foreach(FSIM_LINE IN LISTS FSIM_LINES)
    if(FSIM_LINE MATCHES "^[ ]+- configuration:[ ]*(Debug|Release)[ ]*$")
      set(FSIM_CONFIG "${CMAKE_MATCH_1}")
      set(FSIM_MODE "")
      set(FSIM_ARTIFACT "")
    elseif(FSIM_LINE MATCHES "^[ ]+llvm_mode:[ ]*['\"]?([A-Za-z]+)")
      set(FSIM_MODE "${CMAKE_MATCH_1}")
    elseif(FSIM_LINE MATCHES "^[ ]+artifact:[ ]*([^ ]+)")
      set(FSIM_ARTIFACT "${CMAKE_MATCH_1}")
    elseif(FSIM_LINE MATCHES "^[ ]+retained_log:[ ]*([^ ]+)")
      set(FSIM_LOG "${CMAKE_MATCH_1}")
      if(FSIM_JOB STREQUAL "linux-llvm22")
        set(FSIM_MODE "ON")
      endif()
      if(FSIM_CONFIG STREQUAL "" OR NOT FSIM_MODE STREQUAL "ON"
         OR FSIM_ARTIFACT STREQUAL "" OR IS_ABSOLUTE "${FSIM_LOG}"
         OR FSIM_LOG MATCHES "(^|/)\\.\\.?(/|$)"
         OR NOT FSIM_LOG MATCHES "^build/qualification/.+[.]log$")
        message(FATAL_ERROR "hosted workflow lane is invalid: ${FSIM_JOB}")
      endif()
      set(FSIM_LANE "${FSIM_JOB}|${FSIM_CONFIG}")
      if(FSIM_LANE IN_LIST FSIM_WORKFLOW_LANES)
        message(FATAL_ERROR "duplicate hosted workflow lane: ${FSIM_LANE}")
      endif()
      list(APPEND FSIM_WORKFLOW_LANES "${FSIM_LANE}")
    endif()
  endforeach()
endforeach()
foreach(FSIM_LANE IN LISTS FSIM_HOSTED_LANES)
  if(NOT FSIM_LANE IN_LIST FSIM_WORKFLOW_LANES)
    message(FATAL_ERROR "hosted workflow lane is missing: ${FSIM_LANE}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/cmake/FsimWarnings.cmake" FSIM_WARNINGS)
foreach(FSIM_TOKEN IN ITEMS "if(FSIM_WARNINGS_AS_ERRORS)"
    "target_compile_options(\${target} PRIVATE -Werror)"
    "target_compile_options(\${target} PRIVATE /WX)")
  fsim_require_job_token("${FSIM_WARNINGS}" "${FSIM_TOKEN}")
endforeach()

list(LENGTH FSIM_HOSTED_LANES FSIM_LANE_COUNT)
message(STATUS
  "hosted lanes: ${FSIM_LANE_COUNT} required/additive LLVM and warning lanes with bounded resources")

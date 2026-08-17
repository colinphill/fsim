# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0009 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()
if(NOT DEFINED FSIM_EVIDENCE_DIR OR FSIM_EVIDENCE_DIR STREQUAL "")
  set(FSIM_EVIDENCE_DIR "${FSIM_SOURCE_DIR}/build/qualification")
endif()
if(NOT DEFINED FSIM_OUTPUT_DIR OR FSIM_OUTPUT_DIR STREQUAL "")
  set(FSIM_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/release-candidate-log-audit")
endif()

set(FSIM_EXPECTED_ISSUE_LOGS
  batch176-change02-precompiled-library-initial.log
  batch176-change02-vertical-slice-initial.log
  batch176-change04-focused-initial.log
  batch176-change15-clang22-debug-clean-machine-initial-failure/console.log
  batch176-change15-clang22-debug-clean-machine-initial-failure/logs/examples.log
  batch176-change15-gcc13-no-llvm-debug-clean-machine-initial-failure/console.log
  batch176-change15-gcc13-no-llvm-debug-clean-machine-initial-failure/logs/install.log)
set(FSIM_REPAIRED_LOGS
  batch176-change02-precompiled-library.log
  batch176-change02-vertical-slice.log
  batch176-change04-focused.log
  batch176-change15-clang22-debug-clean-machine/console.log
  batch176-change15-gcc13-no-llvm-debug-clean-machine/console.log
  batch176-change15-clang22-debug-install-lock-repair.log
  batch176-change15-gcc13-llvm22-debug-install-lock-repair.log)

file(GLOB_RECURSE FSIM_CANDIDATE_LOGS RELATIVE "${FSIM_EVIDENCE_DIR}"
  "${FSIM_EVIDENCE_DIR}/*.log")
set(FSIM_AUDITED_LOGS)
foreach(FSIM_LOG IN LISTS FSIM_CANDIDATE_LOGS)
  if(FSIM_LOG MATCHES "^batch176-change(0[2-9]|1[0-6])")
    list(APPEND FSIM_AUDITED_LOGS "${FSIM_LOG}")
  endif()
endforeach()
list(SORT FSIM_AUDITED_LOGS)
list(LENGTH FSIM_AUDITED_LOGS FSIM_LOG_COUNT)
if(FSIM_LOG_COUNT EQUAL 0)
  if(FSIM_REQUIRE_EVIDENCE)
    message(FATAL_ERROR
      "Batch 176 retained evidence is required but absent: ${FSIM_EVIDENCE_DIR}")
  endif()
  message(STATUS
    "release-candidate log audit policy: 69 Debug/fixture logs, 7 expected issue records, 5 repaired issue classes; local retained evidence unavailable")
  return()
endif()
if(NOT FSIM_LOG_COUNT EQUAL 69)
  message(FATAL_ERROR
    "Batch 176 Change 2-16 log inventory drifted: expected 69, found ${FSIM_LOG_COUNT}")
endif()

foreach(FSIM_REPAIRED_LOG IN LISTS FSIM_REPAIRED_LOGS)
  list(FIND FSIM_AUDITED_LOGS "${FSIM_REPAIRED_LOG}" FSIM_REPAIRED_INDEX)
  if(FSIM_REPAIRED_INDEX EQUAL -1)
    message(FATAL_ERROR "repaired evidence is missing: ${FSIM_REPAIRED_LOG}")
  endif()
endforeach()

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
file(WRITE "${FSIM_RESULT}" "log\tsha256\tdisposition\n")
set(FSIM_CLEAN_COUNT 0)
set(FSIM_ISSUE_COUNT 0)
set(FSIM_MAX_CTEST_SECONDS 0)
foreach(FSIM_LOG IN LISTS FSIM_AUDITED_LOGS)
  set(FSIM_PATH "${FSIM_EVIDENCE_DIR}/${FSIM_LOG}")
  file(SIZE "${FSIM_PATH}" FSIM_SIZE)
  if(FSIM_SIZE EQUAL 0)
    message(FATAL_ERROR "retained candidate log is empty: ${FSIM_LOG}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  file(SHA256 "${FSIM_PATH}" FSIM_SHA256)
  list(FIND FSIM_EXPECTED_ISSUE_LOGS "${FSIM_LOG}" FSIM_ISSUE_INDEX)
  if(NOT FSIM_ISSUE_INDEX EQUAL -1)
    math(EXPR FSIM_ISSUE_COUNT "${FSIM_ISSUE_COUNT} + 1")
    if(FSIM_LOG STREQUAL "batch176-change02-vertical-slice-initial.log")
      set(FSIM_EXPECTED_TOKEN "warning[FSIM-FE-CU-0001]")
      set(FSIM_DISPOSITION "configuration-warning-repaired")
    elseif(FSIM_LOG STREQUAL
           "batch176-change02-precompiled-library-initial.log")
      set(FSIM_EXPECTED_TOKEN "Permission denied")
      set(FSIM_DISPOSITION "tutorial-procedure-repaired")
    elseif(FSIM_LOG STREQUAL "batch176-change04-focused-initial.log")
      set(FSIM_EXPECTED_TOKEN "expected 1407 files, found 1414")
      set(FSIM_DISPOSITION "premature-inventory-selection-repaired")
    elseif(FSIM_LOG MATCHES
           "batch176-change15-clang22-debug-clean-machine-initial-failure")
      set(FSIM_EXPECTED_TOKEN "expected 1407 files, found 1428")
      set(FSIM_DISPOSITION "premature-release-audit-selection-repaired")
    else()
      set(FSIM_EXPECTED_TOKEN "install manifest path escapes")
      set(FSIM_DISPOSITION "parallel-install-manifest-race-repaired")
    endif()
    string(FIND "${FSIM_CONTENTS}" "${FSIM_EXPECTED_TOKEN}" FSIM_TOKEN_INDEX)
    if(FSIM_TOKEN_INDEX EQUAL -1)
      message(FATAL_ERROR
        "expected issue signature is absent from ${FSIM_LOG}: ${FSIM_EXPECTED_TOKEN}")
    endif()
  else()
    foreach(FSIM_BAD_TOKEN IN ITEMS
        "warning["
        "warning:"
        "error["
        "error:"
        "CMake Error"
        "***Failed"
        "Permission denied"
        "path escapes"
        "Assertion failed"
        "***Exception"
        "Segmentation fault"
        "FAILED:"
        "timed out"
        "Killed")
      string(FIND "${FSIM_CONTENTS}" "${FSIM_BAD_TOKEN}" FSIM_BAD_INDEX)
      if(NOT FSIM_BAD_INDEX EQUAL -1)
        message(FATAL_ERROR
          "unclassified candidate-log marker ${FSIM_BAD_TOKEN}: ${FSIM_LOG}")
      endif()
    endforeach()
    math(EXPR FSIM_CLEAN_COUNT "${FSIM_CLEAN_COUNT} + 1")
    set(FSIM_DISPOSITION "clean")
  endif()
  string(REGEX MATCHALL
    "Total Test time \\(real\\) = +[0-9.]+ sec" FSIM_TIME_ROWS
    "${FSIM_CONTENTS}")
  foreach(FSIM_TIME_ROW IN LISTS FSIM_TIME_ROWS)
    string(REGEX REPLACE ".*= +([0-9.]+) sec" "\\1" FSIM_SECONDS
      "${FSIM_TIME_ROW}")
    if(FSIM_SECONDS GREATER FSIM_MAX_CTEST_SECONDS)
      set(FSIM_MAX_CTEST_SECONDS "${FSIM_SECONDS}")
    endif()
  endforeach()
  file(APPEND "${FSIM_RESULT}"
    "${FSIM_LOG}\t${FSIM_SHA256}\t${FSIM_DISPOSITION}\n")
endforeach()

if(NOT FSIM_CLEAN_COUNT EQUAL 62 OR NOT FSIM_ISSUE_COUNT EQUAL 7)
  message(FATAL_ERROR
    "candidate-log disposition drifted: clean=${FSIM_CLEAN_COUNT}, issue=${FSIM_ISSUE_COUNT}")
endif()
if(FSIM_MAX_CTEST_SECONDS GREATER 7200)
  message(FATAL_ERROR
    "retained CTest time exceeded 120 minutes: ${FSIM_MAX_CTEST_SECONDS}")
endif()
file(SHA256 "${FSIM_RESULT}" FSIM_RESULT_SHA256)
message(STATUS
  "release-candidate log audit: 69 logs, 62 clean, 7 expected records across 5 repaired issue classes, max CTest ${FSIM_MAX_CTEST_SECONDS}s, result SHA-256 ${FSIM_RESULT_SHA256}")

# Change 20 closes the local Debug candidate after the original Change 17
# inventory was frozen. Keep that historical 69-log identity stable, but also
# require the final records whenever the local closeout evidence is present.
set(FSIM_FINAL_CLEAN_LOGS
  batch176-change17-log-audit-focused.log
  batch176-change18-release-records.log
  batch176-change19-docs-packaging.log
  batch176-change20-clang22-llvm22-debug-build.log
  batch176-change20-clang22-llvm22-debug-regression-final.log
  batch176-change20-gcc13-llvm22-debug-build-final.log
  batch176-change20-gcc13-llvm22-debug-regression.log
  batch176-change20-gcc13-no-llvm-debug-build.log
  batch176-change20-gcc13-no-llvm-debug-regression.log
  batch176-change20-final-non-release-gates.log)
set(FSIM_FINAL_ISSUE_LOGS
  batch176-change20-clang22-llvm22-debug-regression.log
  batch176-change20-gcc13-llvm22-debug-build.log)
set(FSIM_FINAL_LOGS ${FSIM_FINAL_CLEAN_LOGS} ${FSIM_FINAL_ISSUE_LOGS})
set(FSIM_FINAL_PRESENT_COUNT 0)
foreach(FSIM_LOG IN LISTS FSIM_FINAL_LOGS)
  if(EXISTS "${FSIM_EVIDENCE_DIR}/${FSIM_LOG}")
    math(EXPR FSIM_FINAL_PRESENT_COUNT "${FSIM_FINAL_PRESENT_COUNT} + 1")
  endif()
endforeach()

if(FSIM_FINAL_PRESENT_COUNT GREATER 0)
  list(LENGTH FSIM_FINAL_LOGS FSIM_FINAL_LOG_COUNT)
  if(NOT FSIM_FINAL_PRESENT_COUNT EQUAL FSIM_FINAL_LOG_COUNT)
    message(FATAL_ERROR
      "Batch 176 final log inventory is incomplete: expected "
      "${FSIM_FINAL_LOG_COUNT}, found ${FSIM_FINAL_PRESENT_COUNT}")
  endif()

  set(FSIM_FINAL_RESULT "${FSIM_OUTPUT_DIR}/final-result.tsv")
  file(WRITE "${FSIM_FINAL_RESULT}" "log\tsha256\tdisposition\n")
  foreach(FSIM_LOG IN LISTS FSIM_FINAL_LOGS)
    set(FSIM_PATH "${FSIM_EVIDENCE_DIR}/${FSIM_LOG}")
    file(SIZE "${FSIM_PATH}" FSIM_SIZE)
    if(FSIM_SIZE EQUAL 0)
      message(FATAL_ERROR "retained final candidate log is empty: ${FSIM_LOG}")
    endif()
    file(READ "${FSIM_PATH}" FSIM_CONTENTS)
    file(SHA256 "${FSIM_PATH}" FSIM_SHA256)
    list(FIND FSIM_FINAL_ISSUE_LOGS "${FSIM_LOG}" FSIM_ISSUE_INDEX)
    if(FSIM_ISSUE_INDEX EQUAL -1)
      foreach(FSIM_BAD_TOKEN IN ITEMS
          "warning["
          "warning:"
          "error["
          "error:"
          "CMake Error"
          "***Failed"
          "Assertion failed"
          "***Exception"
          "Segmentation fault"
          "FAILED:"
          "timed out"
          "Killed"
          "jobserver unavailable"
          "forced in submake")
        string(FIND "${FSIM_CONTENTS}" "${FSIM_BAD_TOKEN}" FSIM_BAD_INDEX)
        if(NOT FSIM_BAD_INDEX EQUAL -1)
          message(FATAL_ERROR
            "unclassified final candidate-log marker ${FSIM_BAD_TOKEN}: ${FSIM_LOG}")
        endif()
      endforeach()
      set(FSIM_DISPOSITION "clean")
    elseif(FSIM_LOG MATCHES "clang22-llvm22-debug-regression\\.log$")
      set(FSIM_EXPECTED_TOKEN "lost token: transaction")
      set(FSIM_DISPOSITION "scv-documentation-closure-repaired")
    else()
      set(FSIM_EXPECTED_TOKEN "warning: -j8 forced in submake")
      set(FSIM_DISPOSITION "tcl-jobserver-policy-repaired")
    endif()
    if(NOT FSIM_ISSUE_INDEX EQUAL -1)
      string(FIND "${FSIM_CONTENTS}" "${FSIM_EXPECTED_TOKEN}" FSIM_TOKEN_INDEX)
      if(FSIM_TOKEN_INDEX EQUAL -1)
        message(FATAL_ERROR
          "expected final issue signature is absent from ${FSIM_LOG}: "
          "${FSIM_EXPECTED_TOKEN}")
      endif()
    endif()
    file(APPEND "${FSIM_FINAL_RESULT}"
      "${FSIM_LOG}\t${FSIM_SHA256}\t${FSIM_DISPOSITION}\n")
  endforeach()
  file(SHA256 "${FSIM_FINAL_RESULT}" FSIM_FINAL_RESULT_SHA256)
  message(STATUS
    "release-candidate final log audit: 12 logs, 10 clean, 2 repaired issue records; result SHA-256 ${FSIM_FINAL_RESULT_SHA256}")
endif()

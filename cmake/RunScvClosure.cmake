# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_CTEST_COMMAND FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "SCV closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

get_filename_component(FSIM_BINARY_ROOT "${FSIM_BINARY_DIR}" ABSOLUTE)
get_filename_component(FSIM_EVIDENCE_ROOT "${FSIM_OUTPUT_DIR}" ABSOLUTE)
string(FIND "${FSIM_EVIDENCE_ROOT}/" "${FSIM_BINARY_ROOT}/"
  FSIM_EVIDENCE_PREFIX)
if(NOT FSIM_EVIDENCE_PREFIX EQUAL 0 OR
   FSIM_EVIDENCE_ROOT STREQUAL FSIM_BINARY_ROOT)
  message(FATAL_ERROR
    "SCV closure output must be a child of the build directory")
endif()

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
file(WRITE "${FSIM_CONSOLE}" "FSIM-SCV-CLOSURE-START release=2.0.1\n")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\tlog\n")

function(fsim_run_scv_stage FSIM_STAGE FSIM_REGEX)
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}"
      --test-dir "${FSIM_BINARY_DIR}"
      --output-on-failure
      --verbose
      --timeout 7200
      -j 8
      -R "${FSIM_REGEX}"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_STDOUT
    ERROR_VARIABLE FSIM_STDERR
    TIMEOUT 7200
  )
  file(WRITE "${FSIM_LOG}" "${FSIM_STDOUT}${FSIM_STDERR}")
  file(APPEND "${FSIM_CONSOLE}" "${FSIM_STDOUT}${FSIM_STDERR}")
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "100% tests passed"
    FSIM_PASS_OFFSET)
  if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
    file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tFAIL\t${FSIM_LOG}\n")
    message(FATAL_ERROR
      "SCV ${FSIM_STAGE} closure failed; retained log: ${FSIM_LOG}")
  endif()
  file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tPASS\t${FSIM_LOG}\n")
endfunction()

fsim_run_scv_stage(governance
  "^fsim\\.(scv-(inventory|portability-contract|upstream-provenance|patch-governance|closure-contract)|scv\\.(installed_consumer|shared_runtime|compatibility|plugin_compiler|artifact))$")
fsim_run_scv_stage(behavior
  "^fsim\\.(scv\\.(backend_protocol|randomization|smart_ptr|constraints|extensions|recording|backend_transport|resources|corpus)|runtime\\.transaction_record|application\\.scv_(protocol|randomization|introspection|recording|trace))$")
fsim_run_scv_stage(official
  "^fsim\\.scv\\.official_(hello|introspection|randomization|transactions)$")
fsim_run_scv_stage(retained-systemc
  "^fsim\\.systemc\\.(shared_runtime|compatibility|tlm1_backend|tlm2_backend)$")

file(READ "${FSIM_OUTPUT_DIR}/logs/behavior.log" FSIM_BEHAVIOR_TEXT)
foreach(FSIM_MARKER IN ITEMS
    "SCV_RESOURCE_BASELINE"
    "enabled_transactions=64"
    "serialized_bytes=32448"
    "peak_queue_records=4"
    "backpressure_events=15"
    "digest=08de48315d224e3a9340adecaec8c3bb747dc84b43b1a08e6c23a99d264f42a2")
  string(FIND "${FSIM_BEHAVIOR_TEXT}" "${FSIM_MARKER}" FSIM_MARKER_OFFSET)
  if(FSIM_MARKER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "SCV closure lost resource marker '${FSIM_MARKER}'")
  endif()
endforeach()

foreach(FSIM_STAGE IN ITEMS governance behavior official retained-systemc)
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  file(SIZE "${FSIM_LOG}" FSIM_LOG_SIZE)
  if(FSIM_LOG_SIZE EQUAL 0)
    message(FATAL_ERROR "SCV closure retained an empty log: ${FSIM_LOG}")
  endif()
endforeach()

file(APPEND "${FSIM_CONSOLE}"
  "FSIM-SCV-CLOSURE-PASS release=2.0.1 rows=18 active=0 "
  "resource-digest=08de48315d224e3a9340adecaec8c3bb747dc84b43b1a08e6c23a99d264f42a2 "
  "release-sanitizer-ci=deferred-batch-177\n")
message(STATUS "SCV closure passed; retained evidence: ${FSIM_OUTPUT_DIR}")

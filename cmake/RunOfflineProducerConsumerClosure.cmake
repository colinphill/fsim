# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_BINARY_DIR FSIM_CTEST_COMMAND FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "offline producer/consumer closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
file(WRITE "${FSIM_CONSOLE}" "FSIM-OFFLINE-PRODUCER-CONSUMER-START\n")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\tlog\n")

set(FSIM_STAGES
  "discovery@@^fsim\\.(source-package-manifest|binary-install-ownership|installed-public-contract|installed-pkg-config-consumer)$"
  "artifacts@@^fsim\\.(artifact\\.(object|design)|library\\.artifact|application\\.artifact_phases|source-hidden-relocation-contract)$"
  "plugins@@^fsim\\.(systemc\\.(compiler|incremental)|scv\\.(plugin_compiler|artifact|installed_consumer))$"
  "upstream@@^fsim\\.(systemc-upstream-provenance|scv-upstream-provenance|scv-patch-governance)$")

foreach(FSIM_STAGE_SPEC IN LISTS FSIM_STAGES)
  string(REPLACE "@@" ";" FSIM_STAGE_FIELDS "${FSIM_STAGE_SPEC}")
  list(GET FSIM_STAGE_FIELDS 0 FSIM_STAGE)
  list(GET FSIM_STAGE_FIELDS 1 FSIM_REGEX)
  set(FSIM_LOG "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log")
  execute_process(
    COMMAND "${FSIM_CTEST_COMMAND}"
      --test-dir "${FSIM_BINARY_DIR}"
      --output-on-failure
      --timeout 7200
      -j 8
      -R "${FSIM_REGEX}"
    RESULT_VARIABLE FSIM_STATUS
    OUTPUT_VARIABLE FSIM_STDOUT
    ERROR_VARIABLE FSIM_STDERR
    TIMEOUT 7200)
  file(WRITE "${FSIM_LOG}" "${FSIM_STDOUT}${FSIM_STDERR}")
  file(APPEND "${FSIM_CONSOLE}" "${FSIM_STDOUT}${FSIM_STDERR}")
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}" "100% tests passed"
    FSIM_PASS_OFFSET)
  if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
    file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tFAIL\t${FSIM_LOG}\n")
    message(FATAL_ERROR
      "offline producer/consumer ${FSIM_STAGE} closure failed; retained log: ${FSIM_LOG}")
  endif()
  file(APPEND "${FSIM_RESULT}" "${FSIM_STAGE}\tPASS\t${FSIM_LOG}\n")
endforeach()

foreach(FSIM_STAGE IN ITEMS discovery artifacts plugins upstream)
  file(SIZE "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log" FSIM_LOG_SIZE)
  if(FSIM_LOG_SIZE EQUAL 0)
    message(FATAL_ERROR
      "offline producer/consumer closure retained an empty ${FSIM_STAGE} log")
  endif()
endforeach()
file(APPEND "${FSIM_CONSOLE}" "FSIM-OFFLINE-PRODUCER-CONSUMER-PASS\n")
message(STATUS
  "offline producer/consumer closure passed; retained evidence: ${FSIM_OUTPUT_DIR}")

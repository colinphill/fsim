# SPDX-License-Identifier: Apache-2.0

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_SOURCE_DIR FSIM_BINARY_DIR FSIM_CTEST_COMMAND FSIM_OUTPUT_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

set(FSIM_PORTABILITY_CONTRACT
    "${FSIM_SOURCE_DIR}/cmake/CheckSystemCPortabilityContract.cmake")
if(NOT EXISTS "${FSIM_PORTABILITY_CONTRACT}")
  message(FATAL_ERROR "SystemC portability contract is missing")
endif()
file(READ "${FSIM_PORTABILITY_CONTRACT}" FSIM_PORTABILITY_CONTENTS)
foreach(FSIM_REMOVAL_POLICY IN ITEMS
    "foreach(FSIM_REMOVED_LEGACY_PATH IN ITEMS"
    "foreach(FSIM_REMOVED_LEGACY_TOKEN IN ITEMS"
    "legacy SystemC facade token remains")
  string(FIND "${FSIM_PORTABILITY_CONTENTS}" "${FSIM_REMOVAL_POLICY}" FSIM_INDEX)
  if(FSIM_INDEX EQUAL -1)
    message(FATAL_ERROR
      "removed SystemC facade rejection drifted: ${FSIM_REMOVAL_POLICY}")
  endif()
endforeach()

if(FSIM_DEDUPLICATED_CTEST)
  message(STATUS
    "compatibility smoke closure fixture witnesses passed; nested CTest execution suppressed")
  return()
endif()

file(REMOVE_RECURSE "${FSIM_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${FSIM_OUTPUT_DIR}/logs")
set(FSIM_CONSOLE "${FSIM_OUTPUT_DIR}/console.log")
set(FSIM_RESULT "${FSIM_OUTPUT_DIR}/result.tsv")
file(WRITE "${FSIM_CONSOLE}" "FSIM-COMPATIBILITY-SMOKE-START\n")
file(WRITE "${FSIM_RESULT}" "stage\tstatus\twitnesses\tlog\n")

set(FSIM_STAGES
  "systemc@@9@@^fsim\\.(systemc\\.(compatibility|compiler|incremental|kernel_backend_(execution|session)|tlm[12]_backend|shared-runtime-contract)|systemc-portability-contract)$"
  "scv@@8@@^fsim\\.(scv\\.(compatibility|plugin_compiler|artifact|backend_protocol|backend_transport|recording|installed_consumer)|runtime\\.transaction_record)$"
  "foreign@@5@@^fsim\\.(runtime|application\\.vpi|foreign-abi-freeze|native-producer-policy|schema-producer-diagnostics)$"
  "containment@@3@@^fsim\\.(systemc\\.(plugin|abi-c)|cache-corruption-isolation-contract)$")

foreach(FSIM_STAGE_SPEC IN LISTS FSIM_STAGES)
  string(REPLACE "@@" ";" FSIM_STAGE_FIELDS "${FSIM_STAGE_SPEC}")
  list(GET FSIM_STAGE_FIELDS 0 FSIM_STAGE)
  list(GET FSIM_STAGE_FIELDS 1 FSIM_EXPECTED_COUNT)
  list(GET FSIM_STAGE_FIELDS 2 FSIM_REGEX)
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
  string(FIND "${FSIM_STDOUT}${FSIM_STDERR}"
    "100% tests passed, 0 tests failed out of ${FSIM_EXPECTED_COUNT}"
    FSIM_PASS_OFFSET)
  if(NOT FSIM_STATUS EQUAL 0 OR FSIM_PASS_OFFSET EQUAL -1)
    file(APPEND "${FSIM_RESULT}"
      "${FSIM_STAGE}\tFAIL\t${FSIM_EXPECTED_COUNT}\t${FSIM_LOG}\n")
    message(FATAL_ERROR
      "compatibility ${FSIM_STAGE} smoke failed; retained log: ${FSIM_LOG}")
  endif()
  file(APPEND "${FSIM_RESULT}"
    "${FSIM_STAGE}\tPASS\t${FSIM_EXPECTED_COUNT}\t${FSIM_LOG}\n")
endforeach()

foreach(FSIM_STAGE IN ITEMS systemc scv foreign containment)
  file(SIZE "${FSIM_OUTPUT_DIR}/logs/${FSIM_STAGE}.log" FSIM_LOG_SIZE)
  if(FSIM_LOG_SIZE EQUAL 0)
    message(FATAL_ERROR "compatibility closure retained an empty ${FSIM_STAGE} log")
  endif()
endforeach()
file(APPEND "${FSIM_CONSOLE}" "FSIM-COMPATIBILITY-SMOKE-PASS\n")
message(STATUS
  "compatibility smoke closure passed 25 SystemC/TLM/SCV/DPI/VPI/VHPI witnesses; retained evidence: ${FSIM_OUTPUT_DIR}")

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_release_integration_inventory.tsv")
if(NOT EXISTS "${FSIM_LEDGER}")
  message(FATAL_ERROR "v3 release integration inventory is missing")
endif()

file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 8)
  message(FATAL_ERROR
    "v3 release integration inventory must contain six rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0")
  message(FATAL_ERROR "v3 release integration inventory lost its license")
endif()
if(NOT FSIM_HEADER STREQUAL
    "id\tdomain\tinventory\tchecker\texpected_rows\texpected_digest\tstate\towner")
  message(FATAL_ERROR "v3 release integration inventory header changed")
endif()

set(FSIM_EXPECTED_DOMAINS
  coverage-foundation coverage-metrics pli-tf pli-acc vhdl-2019
  systemverilog-2023)
set(FSIM_IDS)
set(FSIM_DOMAINS)
set(FSIM_TOTAL_ROWS 0)

foreach(FSIM_INDEX RANGE 2 7)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 8)
    message(FATAL_ERROR
      "v3 release integration row ${FSIM_INDEX} must contain eight fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 2 FSIM_INVENTORY_RELATIVE)
  list(GET FSIM_FIELDS 3 FSIM_CHECKER_RELATIVE)
  list(GET FSIM_FIELDS 4 FSIM_EXPECTED_ROWS)
  list(GET FSIM_FIELDS 5 FSIM_EXPECTED_DIGEST)
  list(GET FSIM_FIELDS 6 FSIM_STATE)
  list(GET FSIM_FIELDS 7 FSIM_OWNER)

  if(FSIM_ID IN_LIST FSIM_IDS OR FSIM_DOMAIN IN_LIST FSIM_DOMAINS)
    message(FATAL_ERROR
      "v3 release integration row duplicates an identity or domain: ${FSIM_ID}")
  endif()
  if(NOT FSIM_DOMAIN IN_LIST FSIM_EXPECTED_DOMAINS)
    message(FATAL_ERROR "v3 release integration domain is unknown: ${FSIM_DOMAIN}")
  endif()
  string(LENGTH "${FSIM_EXPECTED_DIGEST}" FSIM_DIGEST_LENGTH)
  if(NOT FSIM_EXPECTED_ROWS MATCHES "^[1-9][0-9]*$" OR
     NOT FSIM_EXPECTED_DIGEST MATCHES "^[0-9a-f]+$" OR
     NOT FSIM_DIGEST_LENGTH EQUAL 64)
    message(FATAL_ERROR
      "v3 release integration count or digest is malformed: ${FSIM_ID}")
  endif()
  if(NOT FSIM_STATE STREQUAL "preserved" OR
     NOT FSIM_OWNER STREQUAL "B188-C01")
    message(FATAL_ERROR
      "v3 release integration row is unresolved: ${FSIM_ID}")
  endif()
  foreach(FSIM_RELATIVE IN ITEMS
      "${FSIM_INVENTORY_RELATIVE}" "${FSIM_CHECKER_RELATIVE}")
    if(IS_ABSOLUTE "${FSIM_RELATIVE}" OR
       FSIM_RELATIVE MATCHES "(^|/)\.\.(/|$)" OR
       FSIM_RELATIVE MATCHES "[\\:]")
      message(FATAL_ERROR
        "v3 release integration path is unsafe: ${FSIM_RELATIVE}")
    endif()
  endforeach()

  set(FSIM_INVENTORY "${FSIM_SOURCE_DIR}/${FSIM_INVENTORY_RELATIVE}")
  set(FSIM_CHECKER "${FSIM_SOURCE_DIR}/${FSIM_CHECKER_RELATIVE}")
  if(NOT EXISTS "${FSIM_INVENTORY}" OR NOT EXISTS "${FSIM_CHECKER}")
    message(FATAL_ERROR
      "v3 release integration owner is missing: ${FSIM_ID}")
  endif()

  file(READ "${FSIM_INVENTORY}" FSIM_INVENTORY_CONTENTS)
  if(FSIM_INVENTORY_CONTENTS MATCHES
      "\t(active|planned|unresolved|deferred|partial)\t")
    message(FATAL_ERROR
      "v3 release integration inventory contains unresolved rows: ${FSIM_ID}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_CHECKER}"
    RESULT_VARIABLE FSIM_CHECK_RESULT
    OUTPUT_VARIABLE FSIM_CHECK_OUTPUT
    ERROR_VARIABLE FSIM_CHECK_ERROR
    TIMEOUT 120)
  if(NOT FSIM_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR
      "v3 release integration checker failed for ${FSIM_ID}: ${FSIM_CHECK_ERROR}")
  endif()
  foreach(FSIM_REQUIRED_OUTPUT IN ITEMS
      "active=0"
      "preserved=${FSIM_EXPECTED_ROWS}"
      "digest=${FSIM_EXPECTED_DIGEST}")
    string(FIND "${FSIM_CHECK_OUTPUT}" "${FSIM_REQUIRED_OUTPUT}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 release integration checker evidence changed for ${FSIM_ID}: ${FSIM_REQUIRED_OUTPUT}")
    endif()
  endforeach()

  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")
  math(EXPR FSIM_TOTAL_ROWS "${FSIM_TOTAL_ROWS} + ${FSIM_EXPECTED_ROWS}")
endforeach()

foreach(FSIM_DOMAIN IN LISTS FSIM_EXPECTED_DOMAINS)
  if(NOT FSIM_DOMAIN IN_LIST FSIM_DOMAINS)
    message(FATAL_ERROR
      "v3 release integration domain is missing: ${FSIM_DOMAIN}")
  endif()
endforeach()
if(NOT FSIM_TOTAL_ROWS EQUAL 163)
  message(FATAL_ERROR
    "v3 release integration row total changed: ${FSIM_TOTAL_ROWS}")
endif()

file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
message(STATUS
  "v3 release integration inventory passed: domains=6 rows=163 active=0 preserved=163 digest=${FSIM_LEDGER_DIGEST}")

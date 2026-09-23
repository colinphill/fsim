# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(FSIM_REQUIRED_VARIABLE IN ITEMS
    FSIM_ID_LEDGER_FILE FSIM_REQUIRED_IDS_FILE FSIM_REGISTERED_CTESTS_FILE
    FSIM_SOURCE_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED_VARIABLE}
     OR "${${FSIM_REQUIRED_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED_VARIABLE} is required")
  endif()
endforeach()

if(NOT IS_DIRECTORY "${FSIM_SOURCE_DIR}")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is not a directory: ${FSIM_SOURCE_DIR}")
endif()
file(REAL_PATH "${FSIM_SOURCE_DIR}" FSIM_SOURCE_ROOT)

function(fsim_read_idset_lines FSIM_LINES_FILE FSIM_LINES_KIND FSIM_LINES_OUT)
  if(NOT EXISTS "${FSIM_LINES_FILE}" OR IS_DIRECTORY "${FSIM_LINES_FILE}")
    message(FATAL_ERROR
      "${FSIM_LINES_KIND} file does not exist: ${FSIM_LINES_FILE}")
  endif()

  file(READ "${FSIM_LINES_FILE}" FSIM_LINES_CONTENT)
  string(REPLACE "\r\n" "\n" FSIM_LINES_CONTENT "${FSIM_LINES_CONTENT}")
  if(FSIM_LINES_CONTENT MATCHES "\r")
    message(FATAL_ERROR
      "${FSIM_LINES_KIND} file contains a bare carriage return")
  endif()
  if(FSIM_LINES_CONTENT MATCHES ";" OR FSIM_LINES_CONTENT MATCHES "\\\\")
    message(FATAL_ERROR
      "${FSIM_LINES_KIND} file contains an unsupported semicolon or backslash")
  endif()

  string(REPLACE "\n" ";" FSIM_LINE_ITEMS "${FSIM_LINES_CONTENT}")
  set(FSIM_PARSED_LINES "")
  foreach(FSIM_LINE IN LISTS FSIM_LINE_ITEMS)
    if(FSIM_LINE STREQUAL "" OR FSIM_LINE STREQUAL
        "# SPDX-License-Identifier: Apache-2.0")
      continue()
    endif()
    if(FSIM_LINE MATCHES "\t")
      message(FATAL_ERROR
        "${FSIM_LINES_KIND} line contains a tab outside the TSV ledger")
    endif()
    string(STRIP "${FSIM_LINE}" FSIM_TRIMMED_LINE)
    if(FSIM_TRIMMED_LINE STREQUAL "")
      message(FATAL_ERROR
        "${FSIM_LINES_KIND} file contains a whitespace-only line")
    endif()
    if(NOT "${FSIM_TRIMMED_LINE}" STREQUAL "${FSIM_LINE}")
      message(FATAL_ERROR
        "${FSIM_LINES_KIND} line has leading or trailing whitespace: '${FSIM_LINE}'")
    endif()
    list(APPEND FSIM_PARSED_LINES "${FSIM_LINE}")
  endforeach()

  set(${FSIM_LINES_OUT} "${FSIM_PARSED_LINES}" PARENT_SCOPE)
endfunction()

function(fsim_parse_ledger_tsv_row FSIM_ROW FSIM_ROW_KIND FSIM_FIELDS_OUT)
  if(FSIM_ROW MATCHES ";")
    message(FATAL_ERROR
      "${FSIM_ROW_KIND} row contains an unsupported semicolon")
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  set(${FSIM_FIELDS_OUT} "${FSIM_FIELDS}" PARENT_SCOPE)
endfunction()

function(fsim_check_relative_ledger_path FSIM_RELATIVE_PATH FSIM_ROW_ID)
  if(FSIM_RELATIVE_PATH STREQUAL ""
     OR IS_ABSOLUTE "${FSIM_RELATIVE_PATH}"
     OR FSIM_RELATIVE_PATH MATCHES "^[A-Za-z]:"
     OR FSIM_RELATIVE_PATH MATCHES "\\\\"
     OR FSIM_RELATIVE_PATH MATCHES "//"
     OR FSIM_RELATIVE_PATH MATCHES "(^|/)\\.(/|$)"
     OR FSIM_RELATIVE_PATH MATCHES "(^|/)\\.\\.(/|$)"
     OR FSIM_RELATIVE_PATH MATCHES "/$")
    message(FATAL_ERROR
      "unsafe path for ledger ID ${FSIM_ROW_ID}: '${FSIM_RELATIVE_PATH}'")
  endif()

  set(FSIM_CANDIDATE_PATH "${FSIM_SOURCE_ROOT}/${FSIM_RELATIVE_PATH}")
  if(NOT EXISTS "${FSIM_CANDIDATE_PATH}" OR IS_DIRECTORY "${FSIM_CANDIDATE_PATH}")
    message(
      FATAL_ERROR
      "ledger path for ID ${FSIM_ROW_ID} does not name an existing regular file: "
      "'${FSIM_RELATIVE_PATH}'")
  endif()

  file(REAL_PATH "${FSIM_CANDIDATE_PATH}" FSIM_CANONICAL_PATH)
  if(NOT EXISTS "${FSIM_CANONICAL_PATH}"
     OR IS_DIRECTORY "${FSIM_CANONICAL_PATH}")
    message(
      FATAL_ERROR
      "ledger path for ID ${FSIM_ROW_ID} does not resolve to an existing regular file: "
      "'${FSIM_RELATIVE_PATH}'")
  endif()
  cmake_path(IS_PREFIX FSIM_SOURCE_ROOT "${FSIM_CANONICAL_PATH}"
    NORMALIZE FSIM_PATH_IS_WITHIN_SOURCE)
  if(NOT FSIM_PATH_IS_WITHIN_SOURCE)
    message(
      FATAL_ERROR
      "ledger path for ID ${FSIM_ROW_ID} escapes FSIM_SOURCE_DIR after symlink resolution: "
      "'${FSIM_RELATIVE_PATH}'")
  endif()
endfunction()

fsim_read_idset_lines(
  "${FSIM_REQUIRED_IDS_FILE}" required-ID FSIM_REQUIRED_ID_LINES)
fsim_read_idset_lines(
  "${FSIM_REGISTERED_CTESTS_FILE}" registered-CTest FSIM_REGISTERED_CTEST_LINES)

if(NOT FSIM_REQUIRED_ID_LINES)
  message(FATAL_ERROR "required-ID file contains no IDs")
endif()
if(NOT FSIM_REGISTERED_CTEST_LINES)
  message(FATAL_ERROR "registered-CTest file contains no names")
endif()

set(FSIM_REGISTERED_CTEST_COUNT 0)
foreach(FSIM_CTEST_NAME IN LISTS FSIM_REGISTERED_CTEST_LINES)
  string(SHA256 FSIM_CTEST_NAME_HASH "${FSIM_CTEST_NAME}")
  set(FSIM_CTEST_NAME_KEY "FSIM_IDSET_REGISTERED_CTEST_${FSIM_CTEST_NAME_HASH}")
  if(DEFINED ${FSIM_CTEST_NAME_KEY})
    message(FATAL_ERROR
      "duplicate registered CTest name while validating ID owners: ${FSIM_CTEST_NAME}")
  endif()
  set(${FSIM_CTEST_NAME_KEY} "${FSIM_CTEST_NAME}")
  math(EXPR FSIM_REGISTERED_CTEST_COUNT
    "${FSIM_REGISTERED_CTEST_COUNT} + 1")
endforeach()

if(NOT EXISTS "${FSIM_ID_LEDGER_FILE}"
   OR IS_DIRECTORY "${FSIM_ID_LEDGER_FILE}")
  message(FATAL_ERROR "ID ledger file does not exist: ${FSIM_ID_LEDGER_FILE}")
endif()
file(READ "${FSIM_ID_LEDGER_FILE}" FSIM_ID_LEDGER_CONTENT)
string(REPLACE "\r\n" "\n" FSIM_ID_LEDGER_CONTENT
  "${FSIM_ID_LEDGER_CONTENT}")
if(FSIM_ID_LEDGER_CONTENT MATCHES "\r")
  message(FATAL_ERROR "ID ledger contains a bare carriage return")
endif()
if(FSIM_ID_LEDGER_CONTENT MATCHES ";")
  message(FATAL_ERROR "ID ledger contains an unsupported semicolon")
endif()

string(REPLACE "\n" ";" FSIM_ID_LEDGER_ROWS "${FSIM_ID_LEDGER_CONTENT}")
set(FSIM_ID_LEDGER_HEADER_SEEN FALSE)
set(FSIM_LEDGER_ID_COUNT 0)
foreach(FSIM_LEDGER_ROW IN LISTS FSIM_ID_LEDGER_ROWS)
  if(FSIM_LEDGER_ROW STREQUAL "" OR FSIM_LEDGER_ROW STREQUAL
      "# SPDX-License-Identifier: Apache-2.0")
    continue()
  endif()

  if(NOT FSIM_ID_LEDGER_HEADER_SEEN)
    fsim_parse_ledger_tsv_row(
      "${FSIM_LEDGER_ROW}" header FSIM_ID_LEDGER_COLUMNS)
    list(LENGTH FSIM_ID_LEDGER_COLUMNS FSIM_ID_LEDGER_COLUMN_COUNT)
    set(FSIM_ID_COLUMN_INDEX -1)
    set(FSIM_PATH_COLUMN_INDEX -1)
    set(FSIM_OWNER_COLUMN_INDEX -1)
    set(FSIM_COLUMN_INDEX 0)
    foreach(FSIM_COLUMN_NAME IN LISTS FSIM_ID_LEDGER_COLUMNS)
      if(FSIM_COLUMN_NAME STREQUAL "")
        message(FATAL_ERROR "ID ledger header contains an empty column name")
      endif()
      if(FSIM_COLUMN_NAME STREQUAL "id")
        set(FSIM_ID_COLUMN_INDEX ${FSIM_COLUMN_INDEX})
      elseif(FSIM_COLUMN_NAME STREQUAL "path")
        set(FSIM_PATH_COLUMN_INDEX ${FSIM_COLUMN_INDEX})
      elseif(FSIM_COLUMN_NAME STREQUAL "owner")
        set(FSIM_OWNER_COLUMN_INDEX ${FSIM_COLUMN_INDEX})
      endif()
      math(EXPR FSIM_COLUMN_INDEX "${FSIM_COLUMN_INDEX} + 1")
    endforeach()
    if(FSIM_ID_COLUMN_INDEX LESS 0
       OR FSIM_PATH_COLUMN_INDEX LESS 0
       OR FSIM_OWNER_COLUMN_INDEX LESS 0)
      message(FATAL_ERROR
        "ID ledger header must contain exactly named id, path, and owner columns")
    endif()
    set(FSIM_ID_LEDGER_HEADER_SEEN TRUE)
    continue()
  endif()

  fsim_parse_ledger_tsv_row(
    "${FSIM_LEDGER_ROW}" data FSIM_LEDGER_FIELDS)
  list(LENGTH FSIM_LEDGER_FIELDS FSIM_LEDGER_FIELD_COUNT)
  if(NOT FSIM_LEDGER_FIELD_COUNT EQUAL FSIM_ID_LEDGER_COLUMN_COUNT)
    message(
      FATAL_ERROR
      "ID ledger row has ${FSIM_LEDGER_FIELD_COUNT} fields; header has "
      "${FSIM_ID_LEDGER_COLUMN_COUNT}")
  endif()
  list(GET FSIM_LEDGER_FIELDS ${FSIM_ID_COLUMN_INDEX} FSIM_LEDGER_ID)
  list(GET FSIM_LEDGER_FIELDS ${FSIM_PATH_COLUMN_INDEX} FSIM_LEDGER_PATH)
  list(GET FSIM_LEDGER_FIELDS ${FSIM_OWNER_COLUMN_INDEX} FSIM_LEDGER_OWNER)

  if(NOT FSIM_LEDGER_ID MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$")
    message(FATAL_ERROR
      "malformed ledger ID: '${FSIM_LEDGER_ID}'")
  endif()
  string(SHA256 FSIM_LEDGER_ID_HASH "${FSIM_LEDGER_ID}")
  set(FSIM_LEDGER_ID_KEY "FSIM_LEDGER_ID_${FSIM_LEDGER_ID_HASH}")
  if(DEFINED ${FSIM_LEDGER_ID_KEY})
    message(FATAL_ERROR "duplicate ledger ID: ${FSIM_LEDGER_ID}")
  endif()
  set(${FSIM_LEDGER_ID_KEY} "${FSIM_LEDGER_ID}")

  fsim_check_relative_ledger_path("${FSIM_LEDGER_PATH}" "${FSIM_LEDGER_ID}")

  if(FSIM_LEDGER_OWNER STREQUAL "")
    message(FATAL_ERROR "missing named owner for ledger ID ${FSIM_LEDGER_ID}")
  endif()
  string(STRIP "${FSIM_LEDGER_OWNER}" FSIM_TRIMMED_OWNER)
  if(NOT "${FSIM_TRIMMED_OWNER}" STREQUAL "${FSIM_LEDGER_OWNER}")
    message(FATAL_ERROR
      "owner for ledger ID ${FSIM_LEDGER_ID} has leading or trailing whitespace")
  endif()
  string(SHA256 FSIM_OWNER_HASH "${FSIM_LEDGER_OWNER}")
  set(FSIM_OWNER_KEY "FSIM_IDSET_REGISTERED_CTEST_${FSIM_OWNER_HASH}")
  if(NOT DEFINED ${FSIM_OWNER_KEY})
    message(FATAL_ERROR
      "missing named registered owner '${FSIM_LEDGER_OWNER}' for ledger ID ${FSIM_LEDGER_ID}")
  endif()
  math(EXPR FSIM_LEDGER_ID_COUNT "${FSIM_LEDGER_ID_COUNT} + 1")
endforeach()

if(NOT FSIM_ID_LEDGER_HEADER_SEEN)
  message(FATAL_ERROR "ID ledger is empty or has no TSV header")
endif()
if(FSIM_LEDGER_ID_COUNT LESS 1)
  message(FATAL_ERROR "ID ledger contains no data rows")
endif()

set(FSIM_REQUIRED_ID_COUNT 0)
foreach(FSIM_REQUIRED_ID IN LISTS FSIM_REQUIRED_ID_LINES)
  if(NOT FSIM_REQUIRED_ID MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$")
    message(FATAL_ERROR
      "malformed required ID: '${FSIM_REQUIRED_ID}'")
  endif()
  string(SHA256 FSIM_REQUIRED_ID_HASH "${FSIM_REQUIRED_ID}")
  set(FSIM_REQUIRED_ID_KEY "FSIM_REQUIRED_ID_${FSIM_REQUIRED_ID_HASH}")
  if(DEFINED ${FSIM_REQUIRED_ID_KEY})
    message(FATAL_ERROR "duplicate required ID: ${FSIM_REQUIRED_ID}")
  endif()
  set(${FSIM_REQUIRED_ID_KEY} "${FSIM_REQUIRED_ID}")

  set(FSIM_LEDGER_REQUIRED_ID_KEY "FSIM_LEDGER_ID_${FSIM_REQUIRED_ID_HASH}")
  if(NOT DEFINED ${FSIM_LEDGER_REQUIRED_ID_KEY})
    message(FATAL_ERROR "required ID is missing from ledger: ${FSIM_REQUIRED_ID}")
  endif()
  math(EXPR FSIM_REQUIRED_ID_COUNT "${FSIM_REQUIRED_ID_COUNT} + 1")
endforeach()

message(
  STATUS
  "required ID set: ${FSIM_REQUIRED_ID_COUNT} required IDs found exactly once "
  "across ${FSIM_LEDGER_ID_COUNT} valid ledger rows; "
  "${FSIM_REGISTERED_CTEST_COUNT} registered owner names checked; additive rows allow")

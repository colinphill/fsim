# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

function(fsim_normalized_text_sha256 path output_variable)
  file(READ "${path}" contents)
  string(REPLACE "\r\n" "\n" contents "${contents}")
  string(REPLACE "\r" "\n" contents "${contents}")
  string(SHA256 digest "${contents}")
  set("${output_variable}" "${digest}" PARENT_SCOPE)
endfunction()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/abi_schema_reference_contract.tsv")
set(FSIM_REFERENCE "${FSIM_SOURCE_DIR}/docs/abi-schema-reference.md")
set(FSIM_EXPECTED_DIGEST
  "0153c03838b7ba675797b9fa36a6873cede0b0683f0633969cdabfb862a1d9dc")
fsim_normalized_text_sha256("${FSIM_CONTRACT}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "ABI/schema reference digest changed: expected ${FSIM_EXPECTED_DIGEST}, "
    "got ${FSIM_ACTUAL_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 34)
  message(FATAL_ERROR "ABI/schema reference requires SPDX, header and 32 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tboundary\tcurrent_identity\tpolicy\treference_anchor\tevidence")
  message(FATAL_ERROR "ABI/schema reference header or SPDX drifted")
endif()

set(FSIM_IDS)
foreach(FSIM_INDEX RANGE 2 33)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 6)
    message(FATAL_ERROR "ABI/schema reference row ${FSIM_INDEX} is malformed")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 2 FSIM_IDENTITY)
  list(GET FSIM_FIELDS 3 FSIM_POLICY)
  list(GET FSIM_FIELDS 4 FSIM_ANCHOR)
  list(GET FSIM_FIELDS 5 FSIM_EVIDENCE)
  math(EXPR FSIM_NUMBER "${FSIM_INDEX} - 1")
  if(FSIM_NUMBER LESS 10)
    set(FSIM_EXPECTED_ID "REF174-0${FSIM_NUMBER}")
  else()
    set(FSIM_EXPECTED_ID "REF174-${FSIM_NUMBER}")
  endif()
  if(NOT FSIM_ID STREQUAL FSIM_EXPECTED_ID OR FSIM_IDENTITY STREQUAL "" OR
     FSIM_POLICY STREQUAL "" OR FSIM_ANCHOR STREQUAL "" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
    message(FATAL_ERROR "ABI/schema reference row ${FSIM_ID} drifted")
  endif()
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate ABI/schema reference id ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()

file(READ "${FSIM_REFERENCE}" FSIM_REFERENCE_TEXT)
foreach(FSIM_TOKEN IN ITEMS
    "# fsim v3 ABI and schema reference"
    "## Compatibility policy"
    "## Installed packages and targets"
    "## Core C API"
    "## Native plug-in and foreign ABIs"
    "## Current persisted identities"
    "## Composition and provenance"
    "## Rejection and transactionality"
    "## Regeneration and rebuild workflows"
    "## Audit anchors"
    "FSIM_API_VERSION` is 1"
    "`fsim::api`"
    "`SystemC::systemc`"
    "`SCV::scv`"
    "| SystemC | ABI 4 |"
    "project schema 3"
    "format 7, portable schema 14"
    "format 12, runtime ABI 1"
    "fsim-code-coverage-foundation-v3"
    "format 5, portable schema 14"
    "format 2, runtime ABI 1, SystemC ABI 4"
    "runtime 61"
    "There is no supported in-place migration command"
    "intentionally provides no fallback reader")
  string(FIND "${FSIM_REFERENCE_TEXT}" "${FSIM_TOKEN}" FSIM_OFFSET)
  if(FSIM_OFFSET EQUAL -1)
    message(FATAL_ERROR "ABI/schema reference lost token: ${FSIM_TOKEN}")
  endif()
endforeach()

file(STRINGS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/core_api_contract.tsv"
  FSIM_API_SYMBOL_ROWS REGEX "^symbol\t")
list(LENGTH FSIM_API_SYMBOL_ROWS FSIM_API_SYMBOL_COUNT)
if(NOT FSIM_API_SYMBOL_COUNT EQUAL 31)
  message(FATAL_ERROR "core API contract no longer has exactly 31 symbols")
endif()
foreach(FSIM_SYMBOL_ROW IN LISTS FSIM_API_SYMBOL_ROWS)
  string(REPLACE "\t" ";" FSIM_SYMBOL_FIELDS "${FSIM_SYMBOL_ROW}")
  list(GET FSIM_SYMBOL_FIELDS 1 FSIM_SYMBOL)
  string(FIND "${FSIM_REFERENCE_TEXT}" "${FSIM_SYMBOL}" FSIM_SYMBOL_OFFSET)
  if(FSIM_SYMBOL_OFFSET EQUAL -1)
    message(FATAL_ERROR "ABI/schema reference omits ${FSIM_SYMBOL}")
  endif()
endforeach()

foreach(FSIM_OWNER IN ITEMS README.md docs/architecture.md docs/diagnostics.md)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_OWNER}" FSIM_OWNER_TEXT)
  string(FIND "${FSIM_OWNER_TEXT}" "abi-schema-reference.md" FSIM_LINK_OFFSET)
  if(FSIM_LINK_OFFSET EQUAL -1)
    message(FATAL_ERROR "${FSIM_OWNER} lost the ABI/schema reference link")
  endif()
endforeach()

message(STATUS
  "ABI/schema reference passed: rows=32 symbols=31 digest=${FSIM_ACTUAL_DIGEST}")

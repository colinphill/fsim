# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

# Composes the per-domain schema-freeze checkers named by the schema-freeze
# ledger and verifies that every exported schema/ABI constant still exists as a
# positive integer in its authoritative header. The gate pins identities (ledger
# IDs, checker paths, constant names), never copied schema numbers, row counts
# or digests, so a legitimate schema bump needs no edit here. The compiled
# identity witness (fsim.schema-identity) owns the constant values.

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_schema_freeze_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tdomain\tchecker\texpected_rows\texpected_digest\tstate\towner")
  message(FATAL_ERROR "schema freeze inventory header changed")
endif()
list(REMOVE_AT FSIM_LINES 0 1)

set(FSIM_EXPECTED_DOMAINS abi object design checkpoint cache)
set(FSIM_IDS)
set(FSIM_DOMAINS)
foreach(FSIM_LINE IN LISTS FSIM_LINES)
  if(FSIM_LINE STREQUAL "")
    continue()
  endif()
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 7)
    message(FATAL_ERROR
      "schema freeze row has ${FSIM_FIELD_COUNT} fields: ${FSIM_LINE}")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 2 FSIM_CHECKER_RELATIVE)
  list(GET FSIM_FIELDS 5 FSIM_STATE)
  list(GET FSIM_FIELDS 6 FSIM_OWNER)
  if(NOT FSIM_ID MATCHES "^V3SCHEMA-[A-Z0-9-]+$" OR
     FSIM_ID IN_LIST FSIM_IDS OR FSIM_DOMAIN IN_LIST FSIM_DOMAINS OR
     NOT FSIM_DOMAIN IN_LIST FSIM_EXPECTED_DOMAINS OR
     NOT FSIM_STATE MATCHES "^(active|preserved)$" OR
     FSIM_OWNER STREQUAL "")
    message(FATAL_ERROR "schema freeze row is malformed: ${FSIM_ID}")
  endif()
  if(IS_ABSOLUTE "${FSIM_CHECKER_RELATIVE}" OR
     FSIM_CHECKER_RELATIVE MATCHES "(^|/)\\.\\.(/|$)" OR
     FSIM_CHECKER_RELATIVE MATCHES "[\\\\:]")
    message(FATAL_ERROR "schema freeze checker path is unsafe: ${FSIM_ID}")
  endif()
  set(FSIM_CHECKER "${FSIM_SOURCE_DIR}/${FSIM_CHECKER_RELATIVE}")
  if(NOT EXISTS "${FSIM_CHECKER}")
    message(FATAL_ERROR "schema freeze checker is missing: ${FSIM_ID}")
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
      "schema freeze owner failed for ${FSIM_ID}: ${FSIM_CHECK_ERROR}")
  endif()
  if(NOT FSIM_CHECK_OUTPUT MATCHES "rows=[1-9][0-9]*" OR
     NOT FSIM_CHECK_OUTPUT MATCHES "digest=[0-9a-f]+")
    message(FATAL_ERROR
      "schema freeze owner reported no rows/digest evidence: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")
endforeach()

foreach(FSIM_DOMAIN IN LISTS FSIM_EXPECTED_DOMAINS)
  if(NOT FSIM_DOMAIN IN_LIST FSIM_DOMAINS)
    message(FATAL_ERROR "schema freeze domain is missing: ${FSIM_DOMAIN}")
  endif()
endforeach()

file(STRINGS
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_current_required_ids.txt"
  FSIM_REQUIRED_IDS)
foreach(FSIM_ID IN LISTS FSIM_REQUIRED_IDS)
  if(FSIM_ID MATCHES "^V3SCHEMA-[A-Z0-9-]+$" AND
     NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "required schema-freeze identity is missing: ${FSIM_ID}")
  endif()
endforeach()

# Every exported constant must stay defined as a positive integer. Only the
# names are pinned; the values are whatever the headers export now.
function(fsim_require_schema_constants FSIM_RELATIVE)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "schema identity owner is missing: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  foreach(FSIM_NAME IN LISTS ARGN)
    if(NOT FSIM_CONTENTS MATCHES
        "[^A-Za-z0-9_]${FSIM_NAME}[ \t]*=?[ \t]*([0-9]+)")
      message(FATAL_ERROR
        "schema constant is not an integer in ${FSIM_RELATIVE}: ${FSIM_NAME}")
    endif()
    if(CMAKE_MATCH_1 EQUAL 0)
      message(FATAL_ERROR
        "schema constant is zero in ${FSIM_RELATIVE}: ${FSIM_NAME}")
    endif()
  endforeach()
endfunction()

fsim_require_schema_constants(include/fsim/runtime/native_plugin_abi.h
  FSIM_NATIVE_PLUGIN_ABI_VERSION)
fsim_require_schema_constants(include/fsim/runtime/tf_plugin_abi.h
  FSIM_TF_INTERFACE_ABI_VERSION FSIM_TF_REGISTRATION_TABLE_ABI_VERSION)
fsim_require_schema_constants(include/fsim/runtime/svdpi_bridge.h
  FSIM_SVDPI_CONTEXT_ABI_VERSION)
fsim_require_schema_constants(include/fsim/runtime/acc_handle_bridge.h
  FSIM_ACC_STANDARD_QUERY_ABI_VERSION)
fsim_require_schema_constants(include/fsim/artifact/object.hpp
  kObjectFormatVersion)
fsim_require_schema_constants(include/fsim/library/artifact.hpp
  kFormatVersion kPortableSchemaVersion kCompiledHirSchemaVersion)
fsim_require_schema_constants(include/fsim/artifact/design.hpp
  kDesignFormatVersion)
fsim_require_schema_constants(include/fsim/app/artifact_schema.hpp
  kRuntimeStateSchema
  kSemanticStateSchema
  kDesignIrStateSchema
  kCompiledHirBundleSchema
  kSystemVerilogConstraintHirStateSchema
  kSystemVerilogCoverageStateSchema
  kSystemVerilogUvmStateSchema
  kSystemVerilogClassSpecializationStateSchema
  kVhdlHirStateSchema)

list(LENGTH FSIM_IDS FSIM_DOMAIN_COUNT)
message(STATUS
  "schema freeze composition: ${FSIM_DOMAIN_COUNT} domain owners passed and every exported schema constant is defined")

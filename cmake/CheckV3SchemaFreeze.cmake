# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_schema_freeze_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 8)
  message(FATAL_ERROR "v3 schema freeze inventory must contain six rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tdomain\tchecker\texpected_rows\texpected_digest\tstate\towner")
  message(FATAL_ERROR "v3 schema freeze inventory header changed")
endif()

set(FSIM_EXPECTED_DOMAINS manifest abi object design checkpoint cache)
set(FSIM_IDS)
set(FSIM_DOMAINS)
set(FSIM_TOTAL_ROWS 0)
foreach(FSIM_INDEX RANGE 2 7)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 7)
    message(FATAL_ERROR "v3 schema freeze row must contain seven fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 2 FSIM_CHECKER_RELATIVE)
  list(GET FSIM_FIELDS 3 FSIM_EXPECTED_ROWS)
  list(GET FSIM_FIELDS 4 FSIM_EXPECTED_DIGEST)
  list(GET FSIM_FIELDS 5 FSIM_STATE)
  list(GET FSIM_FIELDS 6 FSIM_OWNER)
  string(LENGTH "${FSIM_EXPECTED_DIGEST}" FSIM_DIGEST_LENGTH)

  if(FSIM_ID IN_LIST FSIM_IDS OR FSIM_DOMAIN IN_LIST FSIM_DOMAINS OR
     NOT FSIM_DOMAIN IN_LIST FSIM_EXPECTED_DOMAINS)
    message(FATAL_ERROR
      "v3 schema freeze row has duplicate or unknown identity: ${FSIM_ID}")
  endif()
  if(NOT FSIM_EXPECTED_ROWS MATCHES "^[1-9][0-9]*$" OR
     NOT FSIM_EXPECTED_DIGEST MATCHES "^[0-9a-f]+$" OR
     NOT FSIM_DIGEST_LENGTH EQUAL 64 OR
     NOT FSIM_STATE STREQUAL "preserved" OR
     NOT FSIM_OWNER STREQUAL "B188-C02")
    message(FATAL_ERROR "v3 schema freeze row is malformed: ${FSIM_ID}")
  endif()
  if(IS_ABSOLUTE "${FSIM_CHECKER_RELATIVE}" OR
     FSIM_CHECKER_RELATIVE MATCHES "(^|/)\.\.(/|$)" OR
     FSIM_CHECKER_RELATIVE MATCHES "[\\:]")
    message(FATAL_ERROR "v3 schema freeze checker path is unsafe: ${FSIM_ID}")
  endif()

  set(FSIM_CHECKER "${FSIM_SOURCE_DIR}/${FSIM_CHECKER_RELATIVE}")
  if(NOT EXISTS "${FSIM_CHECKER}")
    message(FATAL_ERROR "v3 schema freeze checker is missing: ${FSIM_ID}")
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
      "v3 schema freeze owner failed for ${FSIM_ID}: ${FSIM_CHECK_ERROR}")
  endif()
  foreach(FSIM_REQUIRED_OUTPUT IN ITEMS
      "rows=${FSIM_EXPECTED_ROWS}" "digest=${FSIM_EXPECTED_DIGEST}")
    string(FIND "${FSIM_CHECK_OUTPUT}" "${FSIM_REQUIRED_OUTPUT}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 schema freeze evidence changed for ${FSIM_ID}: ${FSIM_REQUIRED_OUTPUT}")
    endif()
  endforeach()
  if(FSIM_DOMAIN STREQUAL "manifest")
    string(FIND "${FSIM_CHECK_OUTPUT}" "schema=3" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR "v3 manifest schema identity changed")
    endif()
  endif()

  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_DOMAINS "${FSIM_DOMAIN}")
  math(EXPR FSIM_TOTAL_ROWS "${FSIM_TOTAL_ROWS} + ${FSIM_EXPECTED_ROWS}")
endforeach()

foreach(FSIM_DOMAIN IN LISTS FSIM_EXPECTED_DOMAINS)
  if(NOT FSIM_DOMAIN IN_LIST FSIM_DOMAINS)
    message(FATAL_ERROR "v3 schema freeze domain is missing: ${FSIM_DOMAIN}")
  endif()
endforeach()
if(NOT FSIM_TOTAL_ROWS EQUAL 267)
  message(FATAL_ERROR "v3 schema freeze row total changed: ${FSIM_TOTAL_ROWS}")
endif()

function(fsim_require_schema_tokens FSIM_RELATIVE)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_PATH}")
    message(FATAL_ERROR "v3 schema identity owner is missing: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 schema identity changed in ${FSIM_RELATIVE}: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_schema_tokens(include/fsim/project/project.hpp
  "kSchemaVersion = 3")
fsim_require_schema_tokens(include/fsim/runtime/native_plugin_abi.h
  "FSIM_NATIVE_PLUGIN_ABI_VERSION 3u")
fsim_require_schema_tokens(include/fsim/runtime/tf_plugin_abi.h
  "FSIM_TF_INTERFACE_ABI_VERSION 3u"
  "FSIM_TF_REGISTRATION_TABLE_ABI_VERSION 3u")
fsim_require_schema_tokens(include/fsim/runtime/svdpi_bridge.h
  "FSIM_SVDPI_CONTEXT_ABI_VERSION 3u")
fsim_require_schema_tokens(include/fsim/runtime/acc_handle_bridge.h
  "FSIM_ACC_STANDARD_QUERY_ABI_VERSION 3u")
fsim_require_schema_tokens(include/fsim/artifact/object.hpp
  "kObjectFormatVersion = 7")
fsim_require_schema_tokens(include/fsim/library/artifact.hpp
  "kFormatVersion = 5"
  "kPortableSchemaVersion = 14")
fsim_require_schema_tokens(include/fsim/library/portable_unit.hpp
  "kOwningUnitSchemaVersion = 32")
fsim_require_schema_tokens(include/fsim/artifact/design.hpp
  "kDesignFormatVersion = 12")
fsim_require_schema_tokens(include/fsim/app/design_artifact.hpp
  "kRuntimeStateSchema = 62"
  "kSemanticStateSchema = 4"
  "kDesignIrStateSchema = 4"
  "kClassStateSchema = 12"
  "kSystemVerilogConstraintHirStateSchema = 7"
  "kSystemVerilogCoverageStateSchema = 7"
  "kSystemVerilogUvmStateSchema = 3"
  "kVhdlHirStateSchema = 4")
fsim_require_schema_tokens(src/compiler/llvm_jit_cache_key.cpp
  "fsim-llvm-native-object-v168")

file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
message(STATUS
  "v3 schema freeze passed: domains=6 owner-rows=267 manifest=3 object=7/14/32 design=12/5 checkpoint=62 cache=v168 digest=${FSIM_LEDGER_DIGEST}")

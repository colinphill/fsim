# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/nested_portable_contract.tsv")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS "${FSIM_CONTRACT}" "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "nested portable freeze input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "7e947fdb501fa6b1348216a5c6e20b4bfebaaf6bcc022441a36ad9a6ebfba7be")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "nested portable contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 67)
  message(FATAL_ERROR "nested portable contract requires SPDX, header and 65 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL "domain\towner\tschema\tbounded_metadata")
  message(FATAL_ERROR "nested portable contract header or SPDX policy changed")
endif()

set(FSIM_SDF_CONTRACT_OWNERS)
foreach(FSIM_INDEX RANGE 2 66)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 4)
    message(FATAL_ERROR "nested portable row ${FSIM_INDEX} requires four fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_DOMAIN)
  list(GET FSIM_FIELDS 1 FSIM_OWNER)
  list(GET FSIM_FIELDS 2 FSIM_SCHEMA)
  list(GET FSIM_FIELDS 3 FSIM_BOUNDS)
  if(FSIM_DOMAIN STREQUAL "" OR FSIM_SCHEMA STREQUAL "" OR
     FSIM_BOUNDS STREQUAL "" OR
     NOT FSIM_OWNER MATCHES "^include/fsim/")
    message(FATAL_ERROR "nested portable row ${FSIM_INDEX} has an empty or misplaced field")
  endif()
  set(FSIM_OWNER_PATH "${FSIM_SOURCE_DIR}/${FSIM_OWNER}")
  if(NOT EXISTS "${FSIM_OWNER_PATH}")
    message(FATAL_ERROR "nested portable owner is missing: ${FSIM_OWNER}")
  endif()
  if(FSIM_DOMAIN STREQUAL "sdf")
    list(APPEND FSIM_SDF_CONTRACT_OWNERS "${FSIM_OWNER}")
    file(READ "${FSIM_OWNER_PATH}" FSIM_OWNER_TEXT)
    string(FIND "${FSIM_OWNER_TEXT}"
      "schema_version = ${FSIM_SCHEMA}U" FSIM_SCHEMA_OFFSET)
    if(FSIM_SCHEMA_OFFSET EQUAL -1)
      message(FATAL_ERROR "${FSIM_OWNER} lost SDF schema ${FSIM_SCHEMA}")
    endif()
    if(FSIM_OWNER MATCHES "sdf_vital_archive.hpp$")
      string(FIND "${FSIM_OWNER_TEXT}" "SdfEffectiveArchiveLimits" FSIM_LIMIT_OFFSET)
    else()
      string(FIND "${FSIM_OWNER_TEXT}" "max_" FSIM_LIMIT_OFFSET)
    endif()
    if(FSIM_LIMIT_OFFSET EQUAL -1)
      message(FATAL_ERROR "${FSIM_OWNER} lost explicit or delegated resource limits")
    endif()
  endif()
endforeach()

file(GLOB FSIM_SDF_HEADERS
  RELATIVE "${FSIM_SOURCE_DIR}"
  "${FSIM_SOURCE_DIR}/include/fsim/app/sdf_*.hpp")
set(FSIM_SDF_SCHEMA_HEADERS)
foreach(FSIM_SDF_HEADER IN LISTS FSIM_SDF_HEADERS)
  file(READ "${FSIM_SOURCE_DIR}/${FSIM_SDF_HEADER}" FSIM_SDF_TEXT)
  string(FIND "${FSIM_SDF_TEXT}" "schema_version" FSIM_SCHEMA_OFFSET)
  if(NOT FSIM_SCHEMA_OFFSET EQUAL -1)
    list(APPEND FSIM_SDF_SCHEMA_HEADERS "${FSIM_SDF_HEADER}")
  endif()
endforeach()
list(SORT FSIM_SDF_SCHEMA_HEADERS)
list(SORT FSIM_SDF_CONTRACT_OWNERS)
if(NOT FSIM_SDF_SCHEMA_HEADERS STREQUAL FSIM_SDF_CONTRACT_OWNERS)
  message(FATAL_ERROR
    "SDF schema owners and nested portable contract are not one-to-one")
endif()
list(LENGTH FSIM_SDF_SCHEMA_HEADERS FSIM_SDF_COUNT)
if(NOT FSIM_SDF_COUNT EQUAL 36)
  message(FATAL_ERROR "nested portable freeze requires exactly 36 SDF schema owners")
endif()

function(fsim_require_nested_portable_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "nested portable owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

set(FSIM_DESIGN_HEADER "${FSIM_SOURCE_DIR}/include/fsim/app/design_artifact.hpp")
set(FSIM_DESIGN_CODEC
  "${FSIM_SOURCE_DIR}/src/app/application_design_artifact_codec.tpp")
set(FSIM_DESIGN_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
fsim_require_nested_portable_tokens("${FSIM_DESIGN_HEADER}"
  "kRuntimeStateSchema = 48"
  "kSemanticStateSchema = 3"
  "kDesignIrStateSchema = 3"
  "kClassStateSchema = 10"
  "kSystemVerilogConstraintHirStateSchema = 6"
  "kSystemVerilogCoverageStateSchema = 4"
  "kSystemVerilogUvmStateSchema = 2"
  "kVhdlHirStateSchema = 1")
fsim_require_nested_portable_tokens("${FSIM_DESIGN_CODEC}"
  "kMaximumNesting = 1024"
  "design state string exceeds the payload"
  "design state vector exceeds the payload"
  "design state contains an invalid variant alternative"
  "design state exceeds the safe structural nesting depth"
  "design state contains trailing bytes")
fsim_require_nested_portable_tokens("${FSIM_DESIGN_TEST}"
  "kRuntimeStateSchema == 48"
  "kSystemVerilogCoverageStateSchema == 4"
  "kSystemVerilogUvmStateSchema == 2"
  "future-coverage-state.bin"
  "truncated-coverage-state.bin"
  "future_uvm")

fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/app/trace_api.hpp"
  "schema_version = 1U"
  "max_selection_count"
  "max_output_bytes")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/app/trace_archive.hpp"
  "schema_version = 1U"
  "profile_version = 1U"
  "max_archive_bytes")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/fst_compression.hpp"
  "schema_version = 1U"
  "algorithm_version{1U}"
  "maximum_input_bytes"
  "maximum_output_bytes")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/app/trace_archive_application_test.cpp"
  "future envelope schema must reject"
  "future profile schema must reject"
  "trailing payload must reject"
  "max_selection_count = 1U")

fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/transaction_record.hpp"
  "transaction_record_schema_version = 1U"
  "max_message_bytes"
  "max_total_string_bytes"
  "max_total_words"
  "max_attributes"
  "max_correlated_objects")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/runtime/transaction_record_test.cpp"
  "invalid.schema = 2U"
  "small_limits.max_attributes = 2U"
  "corrupt.pop_back()"
  "corrupt.push_back(std::byte { 0U })")

fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/uvm_checkpoint.hpp"
  "systemverilog_uvm_checkpoint_schema = 2"
  "maximum_records"
  "maximum_text_bytes"
  "maximum_payload_bytes"
  "maximum_identity_bytes"
  "maximum_external_callbacks")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vpi_checkpoint.hpp"
  "systemverilog_vpi_checkpoint_schema = 2"
  "SystemVerilogVpiObjectStateSnapshot objects"
  "SystemVerilogVpiCheckpointError::None")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vhpi_checkpoint.hpp"
  "vhdl_vhpi_checkpoint_schema = 1"
  "std::vector<VhdlVhpiCheckpointObject> objects"
  "VhdlVhpiCheckpointError::None")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/uvm_register_model.hpp"
  "maximum_register_coverage_models"
  "maximum_register_coverage_samples"
  "maximum_register_coverage_bins"
  "maximum_register_coverage_bin_bytes")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_uvm_checkpoint_tests.cpp"
  "require_resource_limit"
  "mismatch.schema += 1")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vpi_checkpoint_tests.cpp"
  "mismatch.schema += 1"
  "rejected artifact changed target state")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_checkpoint_tests.cpp"
  "mismatch.schema += 1"
  "missing.handles.empty()"
  "missing.invalidations.empty()")

fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/scv.hpp"
  "FSIM_SCV_ARTIFACT_SCHEMA_VERSION 1u"
  "FSIM_SCV_CACHE_SCHEMA_VERSION 1u")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/scv_backend_protocol.hpp"
  "scv_backend_protocol_version = 1U"
  "scv_backend_message_header_bytes = 160U"
  "max_identity_bytes"
  "max_payload_bytes"
  "max_message_bytes")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/scv_backend_transport.hpp"
  "scv_transport_schema_version = 1U"
  "scv_transport_header_bytes = 64U"
  "max_queued_bytes"
  "runtime::TransactionRecordLimits record_limits")
foreach(FSIM_SCV_HEADER IN ITEMS
    scv_recording.hpp
    scv_resources.hpp
    scv_extensions.hpp
    scv_random.hpp
    scv_constraints.hpp
    scv_smart_ptr.hpp)
  fsim_require_nested_portable_tokens(
    "${FSIM_SOURCE_DIR}/include/fsim/systemc/${FSIM_SCV_HEADER}"
    "Limits"
    "max_")
endforeach()
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/scv/scv_backend_protocol_test.cpp"
  "invalid.header.schema = 2U"
  "oversized.payload.resize"
  "corrupt.pop_back()"
  "corrupt.push_back(std::byte { 0U })")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/scv/scv_backend_transport_test.cpp"
  "narrow.max_queued_records = 1U"
  "corrupt[8] = std::byte { 2U }")
fsim_require_nested_portable_tokens(
  "${FSIM_SOURCE_DIR}/tests/scv/scv_recording_test.cpp"
  "narrow.max_streams = 1U"
  "narrow.max_completed_records = 1U")

fsim_require_nested_portable_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.nested-portable-schema-freeze"
  "CheckNestedPortableFreeze.cmake")

message(STATUS
  "nested portable schema freeze passed: rows=65 sdf=36 digest=${FSIM_CONTRACT_DIGEST}")

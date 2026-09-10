# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/schema_producer_diagnostic_contract.tsv")
set(FSIM_BUILDER
  "${FSIM_SOURCE_DIR}/src/diagnostic/artifact_identity.cpp")
set(FSIM_PROJECT "${FSIM_SOURCE_DIR}/src/project/project.cpp")
set(FSIM_OBJECT "${FSIM_SOURCE_DIR}/src/artifact/object.cpp")
set(FSIM_DESIGN "${FSIM_SOURCE_DIR}/src/artifact/design.cpp")
set(FSIM_LIBRARY "${FSIM_SOURCE_DIR}/src/library/artifact.cpp")
set(FSIM_PORTABLE "${FSIM_SOURCE_DIR}/src/library/portable_unit.cpp")
set(FSIM_DESIGN_STATE
  "${FSIM_SOURCE_DIR}/src/app/application_design_artifact_codec.cpp")
set(FSIM_LIBRARY_IMPORT
  "${FSIM_SOURCE_DIR}/src/app/application_library_import.cpp")
set(FSIM_INCREMENTAL_CODEC
  "${FSIM_SOURCE_DIR}/src/systemc/incremental_artifact.cpp")
set(FSIM_INCREMENTAL_COMPILER
  "${FSIM_SOURCE_DIR}/src/systemc/incremental_compiler.cpp")
set(FSIM_VPI_CHECKPOINT
  "${FSIM_SOURCE_DIR}/src/runtime/vpi_checkpoint.cpp")
set(FSIM_VHPI_CHECKPOINT
  "${FSIM_SOURCE_DIR}/src/runtime/vhpi_checkpoint.cpp")
set(FSIM_UVM_CHECKPOINT
  "${FSIM_SOURCE_DIR}/src/runtime/uvm_checkpoint.cpp")
set(FSIM_PROJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/project/project_config_test.cpp")
set(FSIM_OBJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/object_artifact_test.cpp")
set(FSIM_DESIGN_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/design_artifact_test.cpp")
set(FSIM_LIBRARY_TEST
  "${FSIM_SOURCE_DIR}/tests/library/library_artifact_test.cpp")
set(FSIM_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_LIBRARY_IMPORT_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_cli.cpp")
set(FSIM_INCREMENTAL_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/incremental_compiler_test.cpp")
set(FSIM_VPI_CHECKPOINT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vpi_checkpoint_tests.cpp")
set(FSIM_VHPI_CHECKPOINT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_checkpoint_tests.cpp")
set(FSIM_UVM_CHECKPOINT_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_uvm_checkpoint_tests.cpp")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_BUILDER}"
    "${FSIM_PROJECT}"
    "${FSIM_OBJECT}"
    "${FSIM_DESIGN}"
    "${FSIM_LIBRARY}"
    "${FSIM_PORTABLE}"
    "${FSIM_DESIGN_STATE}"
    "${FSIM_LIBRARY_IMPORT}"
    "${FSIM_INCREMENTAL_CODEC}"
    "${FSIM_INCREMENTAL_COMPILER}"
    "${FSIM_VPI_CHECKPOINT}"
    "${FSIM_VHPI_CHECKPOINT}"
    "${FSIM_UVM_CHECKPOINT}"
    "${FSIM_PROJECT_TEST}"
    "${FSIM_OBJECT_TEST}"
    "${FSIM_DESIGN_TEST}"
    "${FSIM_LIBRARY_TEST}"
    "${FSIM_APPLICATION_TEST}"
    "${FSIM_LIBRARY_IMPORT_TEST}"
    "${FSIM_INCREMENTAL_TEST}"
    "${FSIM_VPI_CHECKPOINT_TEST}"
    "${FSIM_VHPI_CHECKPOINT_TEST}"
    "${FSIM_UVM_CHECKPOINT_TEST}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "schema/producer diagnostic input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "48dda18806aeba6b108e84770686057e8000f9deb1084f7d52662c8972a1928e")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "schema/producer diagnostic contract changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 39)
  message(FATAL_ERROR
    "schema/producer diagnostic contract requires SPDX, header and 37 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "family\tboundary\tfound_identity\trequired_identity\tsafe_action\tcontainment")
  message(FATAL_ERROR
    "schema/producer diagnostic header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 38)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 6)
    message(FATAL_ERROR
      "schema/producer diagnostic row ${FSIM_INDEX} requires six fields")
  endif()
endforeach()

function(fsim_require_schema_diagnostic_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "schema/producer diagnostic owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

function(fsim_require_schema_diagnostic_order path anchor first second)
  file(READ "${path}" FSIM_CONTENTS)
  string(FIND "${FSIM_CONTENTS}" "${anchor}" FSIM_ANCHOR_OFFSET)
  if(FSIM_ANCHOR_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "schema/producer diagnostic order anchor is missing in ${path}: ${anchor}")
  endif()
  string(SUBSTRING "${FSIM_CONTENTS}" ${FSIM_ANCHOR_OFFSET} -1 FSIM_TAIL)
  string(FIND "${FSIM_TAIL}" "${first}" FSIM_FIRST_OFFSET)
  string(FIND "${FSIM_TAIL}" "${second}" FSIM_SECOND_OFFSET)
  if(FSIM_FIRST_OFFSET EQUAL -1 OR FSIM_SECOND_OFFSET EQUAL -1 OR
     NOT FSIM_FIRST_OFFSET LESS FSIM_SECOND_OFFSET)
    message(FATAL_ERROR
      "schema/producer diagnostic order changed in ${path}: ${first} must precede ${second}")
  endif()
endfunction()

fsim_require_schema_diagnostic_tokens("${FSIM_BUILDER}"
  "unsupported_artifact_identity("
  "unsupported "
  " identity: found "
  "; required "
  "; regenerate "
  " with this fsim build")
fsim_require_schema_diagnostic_tokens("${FSIM_PROJECT}"
  "\"project manifest\", \"no schema\""
  "\"schema \" + std::to_string(kSchemaVersion), \"fsim.toml\""
  "\"schema outside the uint32 range\""
  "config_.schema != kSchemaVersion")
fsim_require_schema_diagnostic_tokens("${FSIM_OBJECT}"
  "\".fsimobj\""
  "\".fsimobj publication\""
  "portable-unit schema"
  "unsupported_artifact_identity(")
fsim_require_schema_diagnostic_tokens("${FSIM_DESIGN}"
  "\".fsimdesign\""
  "runtime ABI"
  "unsupported_artifact_identity(")
fsim_require_schema_diagnostic_tokens("${FSIM_LIBRARY}"
  "\".fsimlib\""
  "\".fsimlib publication\""
  "portable-unit schema"
  "unsupported_artifact_identity(")
fsim_require_schema_diagnostic_tokens("${FSIM_PORTABLE}"
  "\"portable owning unit\""
  "\"portable UDP declaration\""
  "\"portable class unit\""
  "unsupported_artifact_identity(")
fsim_require_schema_diagnostic_tokens("${FSIM_DESIGN_STATE}"
  "\"design state \" + std::string { magic }"
  "unsupported_artifact_identity("
  "schema != expected_schema")
fsim_require_schema_diagnostic_tokens("${FSIM_INCREMENTAL_CODEC}"
  "incremental_identity_diagnostic("
  "\".fsimscobj producer\""
  "\".fsimscplugin producer\""
  "\".fsimscobj SCV producer\""
  "\".fsimscplugin SCV producer\"")
fsim_require_schema_diagnostic_tokens("${FSIM_INCREMENTAL_COMPILER}"
  "\"cached SystemC link producer\""
  "\"SystemC object producer\""
  "\"SystemC plug-in compiler producer\""
  "\"SystemC plug-in producer\"")
fsim_require_schema_diagnostic_tokens("${FSIM_LIBRARY_IMPORT}"
  "\"mapped LLVM native object producer\""
  "features SHA-256"
  "\"mapped SystemC native plug-in SCV producer\""
  "\"mapped SystemC native plug-in producer\""
  "\".fsimlib native payload\"")
fsim_require_schema_diagnostic_order("${FSIM_LIBRARY_IMPORT}"
  "bool admit_llvm_artifact(" "unsupported_artifact_identity(" "read_payload(")
fsim_require_schema_diagnostic_order("${FSIM_LIBRARY_IMPORT}"
  "bool admit_systemc_artifact(" "unsupported_artifact_identity(" "read_payload(")
fsim_require_schema_diagnostic_order("${FSIM_INCREMENTAL_COMPILER}"
  "std::shared_ptr<HierarchyRegistry> load_incremental_plugin("
  "unsupported_artifact_identity(" "HierarchyRegistry::load(")

fsim_require_schema_diagnostic_order("${FSIM_VPI_CHECKPOINT}"
  "restore_systemverilog_vpi_checkpoint(" "artifact.schema !=" "external_state(")
fsim_require_schema_diagnostic_order("${FSIM_VHPI_CHECKPOINT}"
  "restore_vhdl_vhpi_checkpoint(" "artifact.schema !=" "result.handles.reserve(")
fsim_require_schema_diagnostic_tokens("${FSIM_UVM_CHECKPOINT}"
  "artifact.schema != systemverilog_uvm_checkpoint_schema"
  "return SystemVerilogUvmCheckpointError::SchemaMismatch")

fsim_require_schema_diagnostic_tokens("${FSIM_PROJECT_TEST}"
  "unsupported project manifest identity: found schema 1"
  "found no schema"
  "found schema outside the uint32 range")
fsim_require_schema_diagnostic_tokens("${FSIM_OBJECT_TEST}"
  "expect_identity_rejection"
  "regenerate "
  ".fsimobj with this fsim build"
  "stale-publication")
fsim_require_schema_diagnostic_tokens("${FSIM_DESIGN_TEST}"
  "has_design_identity_diagnostic"
  "regenerate .fsimdesign"
  "stale-publication")
fsim_require_schema_diagnostic_tokens("${FSIM_LIBRARY_TEST}"
  "has_identity_diagnostic"
  "portable-unit schema 14"
  "stale-publication.fsimlib")
fsim_require_schema_diagnostic_tokens("${FSIM_APPLICATION_TEST}"
  "unsupported design state FSIMUVM1 identity: found"
  "future-vhdl-object"
  "scalar-artifact-o0.fsimlib")
fsim_require_schema_diagnostic_tokens("${FSIM_LIBRARY_IMPORT_TEST}"
  "identity: found"
  "regenerate .fsimlib native payload with this fsim build"
  "incompatible_cache_has_file"
  "stale_cache_has_file")
fsim_require_schema_diagnostic_tokens("${FSIM_INCREMENTAL_TEST}"
  "has_identity_diagnostic"
  "stale-object-producer"
  "stale-plugin-producer"
  "not-a-native-shared-library")
fsim_require_schema_diagnostic_tokens("${FSIM_VPI_CHECKPOINT_TEST}"
  "VPI artifact schema mismatch was accepted"
  "VPI rejected artifact changed target state")
fsim_require_schema_diagnostic_tokens("${FSIM_VHPI_CHECKPOINT_TEST}"
  "VHPI checkpoint schema mismatch was accepted"
  "VHPI same-process restart crossed simulation ownership")
fsim_require_schema_diagnostic_tokens("${FSIM_UVM_CHECKPOINT_TEST}"
  "UVM checkpoint schema mismatch was accepted")
fsim_require_schema_diagnostic_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.schema-producer-diagnostics"
  "CheckSchemaProducerDiagnostics.cmake")

message(STATUS
  "schema/producer diagnostic policy passed: rows=37 digest=${FSIM_CONTRACT_DIGEST}")

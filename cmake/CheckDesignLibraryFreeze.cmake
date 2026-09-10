# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/design_library_contract.tsv")
set(FSIM_DESIGN_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/design.hpp")
set(FSIM_LIBRARY_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/library/artifact.hpp")
set(FSIM_DESIGN_CODEC "${FSIM_SOURCE_DIR}/src/artifact/design.cpp")
set(FSIM_LIBRARY_CODEC "${FSIM_SOURCE_DIR}/src/library/artifact.cpp")
set(FSIM_DESIGN_ADMISSION
  "${FSIM_SOURCE_DIR}/src/app/application_phase_design.cpp")
set(FSIM_LIBRARY_ADMISSION
  "${FSIM_SOURCE_DIR}/src/app/application_library_import.cpp")
set(FSIM_DESIGN_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/design_artifact_test.cpp")
set(FSIM_LIBRARY_TEST
  "${FSIM_SOURCE_DIR}/tests/library/library_artifact_test.cpp")
set(FSIM_PHASE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_DESIGN_HEADER}"
    "${FSIM_LIBRARY_HEADER}"
    "${FSIM_DESIGN_CODEC}"
    "${FSIM_LIBRARY_CODEC}"
    "${FSIM_DESIGN_ADMISSION}"
    "${FSIM_LIBRARY_ADMISSION}"
    "${FSIM_DESIGN_TEST}"
    "${FSIM_LIBRARY_TEST}"
    "${FSIM_PHASE_TEST}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "design/library freeze input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "238084297c9ba6cdc04487c9ef07e86d98d183960495cd9dd2e03a8d0c8a3737")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "design/library contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 51)
  message(FATAL_ERROR "design/library contract requires SPDX, header and 49 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL "kind\tname\tcontract")
  message(FATAL_ERROR "design/library contract header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 50)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "design/library row ${FSIM_INDEX} requires three fields")
  endif()
endforeach()

function(fsim_require_design_library_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "design/library owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_design_library_tokens("${FSIM_DESIGN_HEADER}"
  "kDesignFormatVersion = 12"
  "kDesignMetadataFilename = \"fsim-design.bin\""
  "std::string scv_compatibility"
  "Producer paths are deliberately")
fsim_require_design_library_tokens("${FSIM_LIBRARY_HEADER}"
  "kFormatVersion = 5"
  "kPortableSchemaVersion = 14"
  "Empty when an exporter intentionally omits source text"
  "enough producer identity to make admission an exact"
  "Opens only fsim-library.toml")

fsim_require_design_library_tokens("${FSIM_DESIGN_CODEC}"
  "'F', 'S', 'I', 'M', 'D', 'E', 'S', '\\0'"
  "writer.u32(kDesignFormatVersion)"
  "writer.u32(metadata.runtime_abi)"
  "unsupported_artifact_identity("
  "\".fsimdesign\""
  "design SystemC plug-in records must match their metadata"
  "design digest does not match its provenance and payload indexes"
  "invalid design metadata supplied for publication"
  "design payload set is incomplete")
fsim_require_design_library_tokens("${FSIM_LIBRARY_CODEC}"
  "unsupported_artifact_identity("
  "\".fsimlib\""
  "portable-unit schema"
  "either no payload or a contained"
  "kind-specific compatibility fields"
  "load_metadata("
  "portable payload set does not cover every indexed unit")

fsim_require_design_library_tokens("${FSIM_DESIGN_ADMISSION}"
  "validate_scv_artifact_compatibility("
  "load_incremental_plugin_metadata("
  "embedded SystemC plug-in metadata disagrees with design provenance"
  "record.compiler_fingerprint != *current_fingerprint"
  "load_incremental_plugin(")
fsim_require_design_library_tokens("${FSIM_LIBRARY_ADMISSION}"
  "native.runtime_abi != runtime_abi_version"
  "native.compiler_fingerprint != *fingerprint"
  "read_payload("
  "library::load_metadata(")

fsim_require_design_library_tokens("${FSIM_DESIGN_TEST}"
  "kDesignFormatVersion == 12U"
  "kCodeCoverageArtifactDiagnostic"
  "stale-coverage"
  "runtime_abi_version == 1U"
  "embedded-plugin"
  "missing-scv-identity"
  "mismatched-plugin-payload"
  "corrupt-magic"
  "truncated-header"
  "unsupported-format-3"
  "incompatible-runtime-abi"
  "stale-publication"
  "oversized-root"
  "rejected-plugin")
fsim_require_design_library_tokens("${FSIM_LIBRARY_TEST}"
  "kFormatVersion == 5"
  "source-hidden.fsimlib"
  "stale.toml"
  "stale-portable.toml"
  "future-portable.toml"
  "stale-publication.fsimlib"
  "systemc-native.toml"
  "missing-scv-native.toml"
  "rejected-native.fsimlib")
fsim_require_design_library_tokens("${FSIM_PHASE_TEST}"
  ".fsimdesign"
  ".fsimlib"
  "producer-hidden")
fsim_require_design_library_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.design-library-schema-freeze"
  "CheckDesignLibraryFreeze.cmake")

message(STATUS
  "design/library schema freeze passed: rows=49 digest=${FSIM_CONTRACT_DIGEST}")

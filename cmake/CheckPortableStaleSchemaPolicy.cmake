# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/portable_stale_schema_contract.tsv")
set(FSIM_OBJECT_CODEC "${FSIM_SOURCE_DIR}/src/artifact/object.cpp")
set(FSIM_DESIGN_CODEC "${FSIM_SOURCE_DIR}/src/artifact/design.cpp")
set(FSIM_LIBRARY_CODEC "${FSIM_SOURCE_DIR}/src/library/artifact.cpp")
set(FSIM_PORTABLE_CODEC "${FSIM_SOURCE_DIR}/src/library/portable_unit.cpp")
set(FSIM_OBJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/object_artifact_test.cpp")
set(FSIM_DESIGN_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/design_artifact_test.cpp")
set(FSIM_LIBRARY_TEST
  "${FSIM_SOURCE_DIR}/tests/library/library_artifact_test.cpp")
set(FSIM_PHASE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_NESTED_FREEZE
  "${FSIM_SOURCE_DIR}/cmake/CheckNestedPortableFreeze.cmake")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_OBJECT_CODEC}"
    "${FSIM_DESIGN_CODEC}"
    "${FSIM_LIBRARY_CODEC}"
    "${FSIM_PORTABLE_CODEC}"
    "${FSIM_OBJECT_TEST}"
    "${FSIM_DESIGN_TEST}"
    "${FSIM_LIBRARY_TEST}"
    "${FSIM_PHASE_TEST}"
    "${FSIM_NESTED_FREEZE}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "portable stale-schema input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "9b560a521088d0dbf7856ab1a8549d96264e412096a3b230798843ff2a65ebac")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "portable stale-schema contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 33)
  message(FATAL_ERROR
    "portable stale-schema contract requires SPDX, header and 31 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "family\tboundary\tcurrent_identity\trejection_and_containment")
  message(FATAL_ERROR "portable stale-schema header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 32)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 4)
    message(FATAL_ERROR
      "portable stale-schema row ${FSIM_INDEX} requires four fields")
  endif()
endforeach()

function(fsim_require_portable_policy_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "portable stale-schema owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

function(fsim_forbid_portable_policy_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(NOT FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "portable stale-schema owner ${path} regained forbidden compatibility: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_portable_policy_tokens("${FSIM_OBJECT_CODEC}"
  "writer.u32(kObjectFormatVersion)"
  "writer.u32(library::kPortableSchemaVersion)"
  "metadata.format != kObjectFormatVersion"
  "metadata.portable_schema != library::kPortableSchemaVersion"
  "validate_metadata("
  "invalid object metadata supplied for publication")
fsim_forbid_portable_policy_tokens("${FSIM_OBJECT_CODEC}"
  "writer.u32(metadata.format)"
  "writer.u32(metadata.portable_schema)")

file(READ "${FSIM_OBJECT_CODEC}" FSIM_OBJECT_TEXT)
string(FIND "${FSIM_OBJECT_TEXT}"
  "metadata.format = *read_format" FSIM_OBJECT_HEADER_OFFSET)
if(FSIM_OBJECT_HEADER_OFFSET EQUAL -1)
  message(FATAL_ERROR "object decoder lost its explicit header identities")
endif()
string(SUBSTRING "${FSIM_OBJECT_TEXT}" ${FSIM_OBJECT_HEADER_OFFSET} -1
  FSIM_OBJECT_DECODER_TEXT)
string(FIND "${FSIM_OBJECT_DECODER_TEXT}"
  "metadata.format != kObjectFormatVersion" FSIM_OBJECT_REJECT_OFFSET)
string(FIND "${FSIM_OBJECT_DECODER_TEXT}"
  "const auto read_string" FSIM_OBJECT_ROOT_OFFSET)
if(FSIM_OBJECT_REJECT_OFFSET EQUAL -1 OR FSIM_OBJECT_ROOT_OFFSET EQUAL -1 OR
   FSIM_OBJECT_REJECT_OFFSET GREATER_EQUAL FSIM_OBJECT_ROOT_OFFSET)
  message(FATAL_ERROR "object schema rejection must precede root decoding")
endif()

fsim_require_portable_policy_tokens("${FSIM_DESIGN_CODEC}"
  "writer.u32(kDesignFormatVersion)"
  "metadata.format != kDesignFormatVersion"
  "metadata.runtime_abi != runtime_abi_version"
  "fsim-design-provenance-v10-code-coverage"
  "validate("
  "invalid design metadata supplied for publication")
fsim_forbid_portable_policy_tokens("${FSIM_DESIGN_CODEC}"
  "writer.u32(metadata.format)"
  "metadata.format >="
  "metadata.format <"
  "metadata.format == 1"
  "fsim-design-provenance-v1\""
  "fsim-design-provenance-v2\""
  "fsim-design-provenance-v3\""
  "fsim-design-provenance-v4\""
  "fsim-design-provenance-v5\""
  "fsim-design-provenance-v6\""
  "fsim-design-provenance-v7\""
  "fsim-design-provenance-v8\""
  "fsim-design-provenance-v9\"")

file(READ "${FSIM_DESIGN_CODEC}" FSIM_DESIGN_TEXT)
string(FIND "${FSIM_DESIGN_TEXT}"
  "metadata.format = *format" FSIM_DESIGN_HEADER_OFFSET)
if(FSIM_DESIGN_HEADER_OFFSET EQUAL -1)
  message(FATAL_ERROR "design decoder lost its explicit header identities")
endif()
string(SUBSTRING "${FSIM_DESIGN_TEXT}" ${FSIM_DESIGN_HEADER_OFFSET} -1
  FSIM_DESIGN_DECODER_TEXT)
string(FIND "${FSIM_DESIGN_DECODER_TEXT}"
  "metadata.format != kDesignFormatVersion" FSIM_DESIGN_REJECT_OFFSET)
string(FIND "${FSIM_DESIGN_DECODER_TEXT}"
  "const auto read_string" FSIM_DESIGN_ROOT_OFFSET)
if(FSIM_DESIGN_REJECT_OFFSET EQUAL -1 OR FSIM_DESIGN_ROOT_OFFSET EQUAL -1 OR
   FSIM_DESIGN_REJECT_OFFSET GREATER_EQUAL FSIM_DESIGN_ROOT_OFFSET)
  message(FATAL_ERROR "design schema rejection must precede root decoding")
endif()

fsim_require_portable_policy_tokens("${FSIM_LIBRARY_CODEC}"
  "output << \"format = \" << kFormatVersion"
  "<< \"portable_schema = \" << kPortableSchemaVersion"
  "metadata.format != kFormatVersion"
  "metadata.portable_schema != kPortableSchemaVersion"
  "unsupported_artifact_identity("
  "\".fsimlib publication\""
  "and portable-unit schema")
fsim_forbid_portable_policy_tokens("${FSIM_LIBRARY_CODEC}"
  "output << \"format = \" << metadata.format"
  "<< \"portable_schema = \" << metadata.portable_schema")

fsim_require_portable_policy_tokens("${FSIM_PORTABLE_CODEC}"
  "schema != kOwningUnitSchemaVersion || !reader.read(unit)"
  "schema != kUdpDeclarationSchemaVersion"
  "unsupported_artifact_identity("
  "\"portable owning unit\""
  "\"portable class unit\""
  "\"portable UDP declaration\"")
fsim_require_portable_policy_tokens("${FSIM_OBJECT_TEST}"
  "format < fsim::artifact::kObjectFormatVersion"
  "schema < fsim::library::kPortableSchemaVersion"
  "stale-publication")
fsim_require_portable_policy_tokens("${FSIM_DESIGN_TEST}"
  "{ 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U }"
  "future_header"
  "stale-publication")
fsim_require_portable_policy_tokens("${FSIM_LIBRARY_TEST}"
  "format < fsim::library::kFormatVersion"
  "schema < fsim::library::kPortableSchemaVersion"
  "stale-publication.fsimlib")
fsim_require_portable_policy_tokens("${FSIM_PHASE_TEST}"
  "future_design_metadata[8]"
  "future-vhdl-design")
fsim_require_portable_policy_tokens("${FSIM_NESTED_FREEZE}"
  "SDF schema owners and nested portable contract are not one-to-one"
  "future envelope schema must reject"
  "rejected artifact changed target state")
fsim_require_portable_policy_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.portable-stale-schema-policy"
  "CheckPortableStaleSchemaPolicy.cmake")

message(STATUS
  "portable stale-schema policy passed: rows=31 digest=${FSIM_CONTRACT_DIGEST}")

# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/project_manifest_contract.tsv")
set(FSIM_HEADER "${FSIM_SOURCE_DIR}/include/fsim/project/project.hpp")
set(FSIM_READER "${FSIM_SOURCE_DIR}/src/project/project.cpp")
set(FSIM_TEST "${FSIM_SOURCE_DIR}/tests/project/project_config_test.cpp")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_HEADER}"
    "${FSIM_READER}"
    "${FSIM_TEST}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "project manifest freeze input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "4d858da38980443e2af0c7d8f25a2ff45b299fc155e1908193c8c95beeed064f")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "project manifest contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 18)
  message(FATAL_ERROR "project manifest contract requires SPDX, header and 16 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER_ROW STREQUAL "kind\tname\tcontract")
  message(FATAL_ERROR "project manifest contract header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 17)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "project manifest row ${FSIM_INDEX} requires three fields")
  endif()
endforeach()

function(fsim_require_project_manifest_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "project manifest owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_project_manifest_tokens("${FSIM_HEADER}"
  "kSchemaVersion = 3"
  "struct CoverageSection"
  "bool enabled{false}"
  "Parses, validates, and resolves a schema-3 fsim.toml"
  "std::optional<Config> parse("
  "std::optional<Config> load(")
fsim_require_project_manifest_tokens("${FSIM_READER}"
  "kSchemaCode = \"FSIM-PROJ-0005\""
  "unsupported_artifact_identity("
  "\"project manifest\", \"no schema\""
  "\"schema \" + std::to_string(kSchemaVersion)"
  "\"schema outside the uint32 range\""
  "config_.schema != kSchemaVersion"
  "if (diagnostics.has_error())"
  "return std::nullopt;")
file(READ "${FSIM_READER}" FSIM_READER_TEXT)
foreach(FSIM_FORBIDDEN IN ITEMS
    "schema == 1"
    "schema <= 1"
    "schema == 2"
    "schema <= 2"
    "legacy_schema"
    "migrate_project"
    "upgrade_project"
    "write_legacy_manifest")
  string(FIND "${FSIM_READER_TEXT}" "${FSIM_FORBIDDEN}" FSIM_FORBIDDEN_OFFSET)
  if(NOT FSIM_FORBIDDEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "project manifest reader regained a compatibility path: ${FSIM_FORBIDDEN}")
  endif()
endforeach()

fsim_require_project_manifest_tokens("${FSIM_TEST}"
  "unsupported project manifest identity: found schema 1; required schema 3; regenerate fsim.toml with this fsim build"
  "unsupported project manifest identity: found no schema; required schema 3"
  "unsupported project manifest identity: found schema 0; required schema 3"
  "unsupported project manifest identity: found schema 2; required schema 3"
  "unsupported project manifest identity: found schema 4; required schema 3"
  "schema = 18446744073709551615"
  "found schema outside the uint32 range"
  "non-current project schema is rejected")
fsim_require_project_manifest_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.project-manifest-schema-freeze"
  "CheckProjectManifestFreeze.cmake")

message(STATUS
  "project manifest schema freeze passed: rows=16 schema=3 digest=${FSIM_CONTRACT_DIGEST}")

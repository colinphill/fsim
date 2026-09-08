# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/portable_object_contract.tsv")
set(FSIM_OBJECT_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/artifact/object.hpp")
set(FSIM_LIBRARY_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/library/artifact.hpp")
set(FSIM_PORTABLE_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/library/portable_unit.hpp")
set(FSIM_OBJECT_CODEC "${FSIM_SOURCE_DIR}/src/artifact/object.cpp")
set(FSIM_PORTABLE_CODEC "${FSIM_SOURCE_DIR}/src/library/portable_unit.cpp")
set(FSIM_OBJECT_TEST
  "${FSIM_SOURCE_DIR}/tests/artifact/object_artifact_test.cpp")
set(FSIM_PORTABLE_TEST
  "${FSIM_SOURCE_DIR}/tests/library/library_artifact_test.cpp")
set(FSIM_PHASE_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_artifact_phases.cpp")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_OBJECT_HEADER}"
    "${FSIM_LIBRARY_HEADER}"
    "${FSIM_PORTABLE_HEADER}"
    "${FSIM_OBJECT_CODEC}"
    "${FSIM_PORTABLE_CODEC}"
    "${FSIM_OBJECT_TEST}"
    "${FSIM_PORTABLE_TEST}"
    "${FSIM_PHASE_TEST}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "portable object freeze input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "059915915e53748387bb12c071996b4bd01f76e49846fb7f733c5cecc289980c")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "portable object contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 48)
  message(FATAL_ERROR "portable object contract requires 48 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL "kind\tname\tcontract")
  message(FATAL_ERROR "portable object contract header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 47)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "portable object row ${FSIM_INDEX} requires three fields")
  endif()
endforeach()

function(fsim_require_portable_object_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "portable object owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_portable_object_tokens("${FSIM_OBJECT_HEADER}"
  "kObjectFormatVersion = 7"
  "kObjectMetadataFilename = \"fsim-object.bin\""
  "portable_schema{library::kPortableSchemaVersion}"
  "Canonical little-endian metadata codec"
  "installed .fsimobj tree is read-only")
fsim_require_portable_object_tokens("${FSIM_LIBRARY_HEADER}"
  "kPortableSchemaVersion = 11")
fsim_require_portable_object_tokens("${FSIM_PORTABLE_HEADER}"
  "kOwningUnitSchemaVersion = 27"
  "kUdpDeclarationSchemaVersion = 1"
  "Unknown schemas, truncation, trailing bytes, and out-of-range values reject")

fsim_require_portable_object_tokens("${FSIM_OBJECT_CODEC}"
  "'F', 'S', 'I', 'M', 'O', 'B', 'J', '\\0'"
  "writer.u32(kObjectFormatVersion)"
  "writer.u32(library::kPortableSchemaVersion)"
  "fsim-object-compilation-v7-code-coverage"
  "unsupported_artifact_identity("
  "\".fsimobj\""
  "portable-unit schema"
  "trailing object metadata bytes"
  "object compilation digest does not match"
  "object output already exists or cannot be inspected")
fsim_require_portable_object_tokens("${FSIM_PORTABLE_CODEC}"
  "kMagic = \"FSIMUNIT\""
  "kUdpMagic = \"FSIMUDPD\""
  "kClassMagic = \"FSIMCLSU\""
  "kMaximumArchiveNesting = 512"
  "portable unit contains a cyclic owning type graph"
  "portable string length exceeds the artifact"
  "portable vector length exceeds the artifact"
  "archive_fields(T& value)"
  "frontend::Expression"
  "frontend::Type"
  "valid_unit_strengths"
  "valid_unit_specify"
  "valid_unit_hierarchy")

fsim_require_portable_object_tokens("${FSIM_OBJECT_TEST}"
  "kObjectFormatVersion == 7U"
  "kCodeCoverageArtifactDiagnostic"
  "stale-coverage"
  "kPortableSchemaVersion == 11U"
  "kOwningUnitSchemaVersion == 27U"
  "corrupt-magic"
  "stale-format"
  "future-format"
  "stale-portable-schema"
  "future-portable-schema"
  "stale-publication"
  "truncated-header"
  "oversized-root"
  "inconsistent-digest")
fsim_require_portable_object_tokens("${FSIM_PORTABLE_TEST}"
  "stale.fsimir"
  "future.fsimir"
  "truncated.fsimir"
  "oversized.fsimir"
  "corrupt.fsimir"
  "stale.fsimudp"
  "future.fsimudp"
  "stale.fsimclass"
  "future.fsimclass"
  "producer-absolute")
fsim_require_portable_object_tokens("${FSIM_PHASE_TEST}"
  ".fsimobj"
  "artifact-vhdl-stale.fsimobj"
  "artifact-vhdl-2019.fsimobj.producer-hidden"
  "stages=compile/object/elaborate/design/simulate")
fsim_require_portable_object_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.portable-object-schema-freeze"
  "CheckPortableObjectFreeze.cmake")

message(STATUS
  "portable .fsimobj schema freeze passed: rows=46 digest=${FSIM_CONTRACT_DIGEST}")

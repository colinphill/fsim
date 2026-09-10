# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR OR "${FSIM_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_LEDGER
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/v3_v2_input_rejection_inventory.tsv")
file(STRINGS "${FSIM_LEDGER}" FSIM_LINES ENCODING UTF-8)
list(LENGTH FSIM_LINES FSIM_LINE_COUNT)
if(NOT FSIM_LINE_COUNT EQUAL 17)
  message(FATAL_ERROR "v3 v2-input rejection inventory must contain 15 rows")
endif()
list(GET FSIM_LINES 0 FSIM_LICENSE)
list(GET FSIM_LINES 1 FSIM_HEADER)
if(NOT FSIM_LICENSE STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tfamily\tv2_identity\tv3_identity\tevidence\tcontainment\towner")
  message(FATAL_ERROR "v3 v2-input rejection inventory header changed")
endif()

set(FSIM_EXPECTED_IDS
  V3REJECT-MANIFEST
  V3REJECT-OBJECT
  V3REJECT-PORTABLE
  V3REJECT-DESIGN
  V3REJECT-LIBRARY
  V3REJECT-RUNTIME
  V3REJECT-SEMANTIC
  V3REJECT-DESIGN-IR
  V3REJECT-CLASS
  V3REJECT-CONSTRAINT
  V3REJECT-COVERAGE
  V3REJECT-UVM
  V3REJECT-VHDL-HIR
  V3REJECT-CACHE
  V3REJECT-PLUGIN)
set(FSIM_EXPECTED_FAMILIES manifest object design checkpoint cache plugin)
set(FSIM_IDS)
set(FSIM_FAMILIES)
foreach(FSIM_INDEX RANGE 2 16)
  list(GET FSIM_LINES ${FSIM_INDEX} FSIM_LINE)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 7)
    message(FATAL_ERROR "v3 v2-input rejection row must contain seven fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 1 FSIM_FAMILY)
  list(GET FSIM_FIELDS 2 FSIM_V2_IDENTITY)
  list(GET FSIM_FIELDS 3 FSIM_V3_IDENTITY)
  list(GET FSIM_FIELDS 4 FSIM_EVIDENCE_RELATIVE)
  list(GET FSIM_FIELDS 5 FSIM_CONTAINMENT)
  list(GET FSIM_FIELDS 6 FSIM_OWNER)
  if(FSIM_ID IN_LIST FSIM_IDS OR NOT FSIM_ID IN_LIST FSIM_EXPECTED_IDS OR
     NOT FSIM_FAMILY IN_LIST FSIM_EXPECTED_FAMILIES OR
     FSIM_V2_IDENTITY STREQUAL "" OR FSIM_V3_IDENTITY STREQUAL "" OR
     NOT FSIM_OWNER STREQUAL "B188-C03")
    message(FATAL_ERROR "v3 v2-input rejection row is malformed: ${FSIM_ID}")
  endif()
  if(IS_ABSOLUTE "${FSIM_EVIDENCE_RELATIVE}" OR
     FSIM_EVIDENCE_RELATIVE MATCHES "(^|/)\.\.(/|$)" OR
     FSIM_EVIDENCE_RELATIVE MATCHES "[\\:]" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE_RELATIVE}")
    message(FATAL_ERROR "v3 v2-input evidence path is unsafe or missing: ${FSIM_ID}")
  endif()
  if(NOT FSIM_CONTAINMENT MATCHES
       "^(exact-diagnostic-no-config|header-first-exact-diagnostic|header-first-no-unit|metadata-first-exact-diagnostic|header-first-no-state|namespace-miss-no-fallback|abi-error-no-metadata)$")
    message(FATAL_ERROR "v3 v2-input containment changed: ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
  list(APPEND FSIM_FAMILIES "${FSIM_FAMILY}")
endforeach()
foreach(FSIM_ID IN LISTS FSIM_EXPECTED_IDS)
  if(NOT FSIM_ID IN_LIST FSIM_IDS)
    message(FATAL_ERROR "v3 v2-input rejection identity is missing: ${FSIM_ID}")
  endif()
endforeach()
foreach(FSIM_FAMILY IN LISTS FSIM_EXPECTED_FAMILIES)
  if(NOT FSIM_FAMILY IN_LIST FSIM_FAMILIES)
    message(FATAL_ERROR "v3 v2-input rejection family is missing: ${FSIM_FAMILY}")
  endif()
endforeach()

function(fsim_require_v2_rejection_tokens FSIM_RELATIVE)
  set(FSIM_PATH "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  file(READ "${FSIM_PATH}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_FOUND)
    if(FSIM_FOUND EQUAL -1)
      message(FATAL_ERROR
        "v3 v2-input rejection evidence changed in ${FSIM_RELATIVE}: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_v2_rejection_tokens(tests/project/project_config_test.cpp
  "found schema 2; required schema 3"
  "non-current project schema is rejected")
fsim_require_v2_rejection_tokens(tests/artifact/object_artifact_test.cpp
  "store_u32(v2_header, 8U, 6U)"
  "store_u32(v2_header, 12U, 10U)"
  "v2-object-repeat"
  "format 6 and portable-unit schema 10")
fsim_require_v2_rejection_tokens(tests/library/library_artifact_test.cpp
  "v2_unit[8] = static_cast<char>(26U)"
  "portable owning unit\", \"schema 26\""
  "v2-library-repeat.toml"
  "portable-unit schema 10")
fsim_require_v2_rejection_tokens(tests/artifact/design_artifact_test.cpp
  "store_u32(v2_header, 8U, 11U)"
  "v2-design-repeat"
  "format 11 and runtime ABI 1")
fsim_require_v2_rejection_tokens(
  tests/app/application_test_artifact_phases.cpp
  "48U"
  "v2-vhdl-hir.bin"
  "v2-sv-uvm.bin"
  "v2-coverage-state.bin")
fsim_require_v2_rejection_tokens(tests/app/application_test_non_project_cli.cpp
  "v2-semantics"
  "v2-design-ir")
fsim_require_v2_rejection_tokens(tests/app/application_test_classes.cpp
  "{ 10U, fsim::app::kClassStateSchema + 1U }"
  "{ 6U, fsim::app::kSystemVerilogConstraintHirStateSchema + 1U }"
  "incompatible-sv-constraint-hir.bin")
fsim_require_v2_rejection_tokens(src/compiler/llvm_jit_cache_key.cpp
  "fsim-llvm-native-object-v168")
file(READ "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_cache_key.cpp"
  FSIM_CACHE_KEY_TEXT)
string(FIND "${FSIM_CACHE_KEY_TEXT}" "fsim-llvm-native-object-v116"
  FSIM_V2_CACHE_FOUND)
if(NOT FSIM_V2_CACHE_FOUND EQUAL -1)
  message(FATAL_ERROR "v3 native cache regained the v2 namespace")
endif()
fsim_require_v2_rejection_tokens(tests/runtime/native_plugin_abi_test.cpp
  "value.abi_version = 2"
  "native plug-in ABI rejects v2 directly"
  "invalid metadata is never partially published")

foreach(FSIM_CHECKER IN ITEMS
    CheckProjectManifestFreeze.cmake
    CheckPortableStaleSchemaPolicy.cmake
    CheckNestedPortableFreeze.cmake
    CheckIncrementalNativeFreeze.cmake
    CheckForeignAbiFreeze.cmake
    CheckSchemaProducerDiagnostics.cmake)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DFSIM_SOURCE_DIR=${FSIM_SOURCE_DIR}"
      -P "${FSIM_SOURCE_DIR}/cmake/${FSIM_CHECKER}"
    RESULT_VARIABLE FSIM_CHECK_RESULT
    OUTPUT_VARIABLE FSIM_CHECK_OUTPUT
    ERROR_VARIABLE FSIM_CHECK_ERROR
    TIMEOUT 120)
  if(NOT FSIM_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR
      "v3 v2-input owner failed in ${FSIM_CHECKER}: ${FSIM_CHECK_ERROR}")
  endif()
endforeach()

fsim_require_v2_rejection_tokens(tests/CMakeLists.txt
  "NAME fsim.v3-v2-input-rejection"
  "CheckV3V2InputRejection.cmake")
file(SHA256 "${FSIM_LEDGER}" FSIM_LEDGER_DIGEST)
set(FSIM_EXPECTED_DIGEST
  "59e448c6f0c448e329a59810eb4c4b0c4b621056b147dcb1408f6b04f83b0a0e")
if(NOT FSIM_LEDGER_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "v3 v2-input rejection digest changed: expected=${FSIM_EXPECTED_DIGEST} actual=${FSIM_LEDGER_DIGEST}")
endif()
message(STATUS
  "v3 v2-input rejection passed: rows=15 families=6 direct-readers=14 cache=v116-to-v168 digest=${FSIM_LEDGER_DIGEST}")

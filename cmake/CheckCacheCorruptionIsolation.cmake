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
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/cache_corruption_isolation_contract.tsv")
set(FSIM_EXPECTED_DIGEST
  "1f422f08663aa0fdebd96611853ef60eebecaed158ece973a58ff91ac1fe5a00")
fsim_normalized_text_sha256("${FSIM_CONTRACT}" FSIM_ACTUAL_DIGEST)
if(NOT FSIM_ACTUAL_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "cache corruption/isolation digest changed: expected ${FSIM_EXPECTED_DIGEST}, "
    "got ${FSIM_ACTUAL_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 38)
  message(FATAL_ERROR
    "cache corruption/isolation contract requires SPDX, header and 36 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "id\tfailure\tplatform\tsurface\texpected\tidentity_partition\tevidence")
  message(FATAL_ERROR "cache corruption/isolation header or SPDX drifted")
endif()

set(FSIM_IDS)
foreach(FSIM_INDEX RANGE 2 37)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 7)
    message(FATAL_ERROR "cache corruption/isolation row ${FSIM_INDEX} is malformed")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_ID)
  list(GET FSIM_FIELDS 2 FSIM_PLATFORM)
  list(GET FSIM_FIELDS 4 FSIM_EXPECTED)
  list(GET FSIM_FIELDS 5 FSIM_PARTITION)
  list(GET FSIM_FIELDS 6 FSIM_EVIDENCE)
  math(EXPR FSIM_NUMBER "${FSIM_INDEX} - 1")
  if(FSIM_NUMBER LESS 10)
    set(FSIM_EXPECTED_ID "CACHE174-0${FSIM_NUMBER}")
  else()
    set(FSIM_EXPECTED_ID "CACHE174-${FSIM_NUMBER}")
  endif()
  if(NOT FSIM_ID STREQUAL FSIM_EXPECTED_ID OR
     FSIM_PLATFORM STREQUAL "" OR FSIM_EXPECTED STREQUAL "" OR
     FSIM_PARTITION STREQUAL "" OR
     NOT EXISTS "${FSIM_SOURCE_DIR}/${FSIM_EVIDENCE}")
    message(FATAL_ERROR "cache corruption/isolation row ${FSIM_ID} drifted")
  endif()
  list(FIND FSIM_IDS "${FSIM_ID}" FSIM_DUPLICATE)
  if(NOT FSIM_DUPLICATE EQUAL -1)
    message(FATAL_ERROR "duplicate cache corruption/isolation id ${FSIM_ID}")
  endif()
  list(APPEND FSIM_IDS "${FSIM_ID}")
endforeach()

function(fsim_require_cache_tokens relative_path)
  file(READ "${FSIM_SOURCE_DIR}/${relative_path}" FSIM_TEXT)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_TEXT}" "${FSIM_TOKEN}" FSIM_OFFSET)
    if(FSIM_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "cache corruption/isolation owner ${relative_path} lost ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_cache_tokens(src/compiler/object_cache.cpp
  "kMaximumCacheEntryBytes" "std::errc::file_too_large"
  "MoveFileExW" "std::filesystem::rename" ".lock" ".tmp.")
fsim_require_cache_tokens(tests/compiler/cache_test.cpp
  "illegal_byte_sequence" "maximum_cache_entry_bytes"
  "std::launch::async" "read_only" "uppercase_key")
fsim_require_cache_tokens(CMakeLists.txt
  "FSIM_BUILD_CONFIGURATION=\"$<CONFIG>\"")
fsim_require_cache_tokens(src/compiler/llvm_jit.cpp
  "build-configuration" "optimization" "runtime-abi-version")
fsim_require_cache_tokens(src/systemc/plugin_compiler.cpp
  "build-configuration" "toolchain" "systemc-abi" "scv-compatibility")
fsim_require_cache_tokens(src/library/artifact.cpp
  "checksum_spelling(native.compiler_fingerprint)")
fsim_require_cache_tokens(src/app/application_library_export.cpp
  "host.fingerprint")
fsim_require_cache_tokens(src/app/application_library_import.cpp
  "native.compiler_fingerprint != host.fingerprint")
fsim_require_cache_tokens(tests/compiler/llvm_jit_cache_test.cpp
  "JitOptimizationLevel::o0" "JitOptimizationLevel::o2"
  "test_optimization_cache_invalidation")
fsim_require_cache_tokens(tests/compiler/llvm_jit_vhdl_profile_cache_test.cpp
  "test_vhdl_language_profile_cache_identity")
fsim_require_cache_tokens(tests/systemc/incremental_compiler_test.cpp
  "compiler_fingerprint" "stale_target_plugin")

message(STATUS
  "cache corruption/isolation contract passed: rows=36 digest=${FSIM_ACTUAL_DIGEST}")

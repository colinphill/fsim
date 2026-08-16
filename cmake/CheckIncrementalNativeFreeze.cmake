# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/incremental_native_contract.tsv")
set(FSIM_HEADER "${FSIM_SOURCE_DIR}/include/fsim/systemc/incremental.hpp")
set(FSIM_CODEC "${FSIM_SOURCE_DIR}/src/systemc/incremental_artifact.cpp")
set(FSIM_COMPILER "${FSIM_SOURCE_DIR}/src/systemc/incremental_compiler.cpp")
set(FSIM_PLUGIN_COMPILER "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler.cpp")
set(FSIM_COMPILER_COMMON
  "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler_common.cpp")
set(FSIM_ACCEL_COMPAT
  "${FSIM_SOURCE_DIR}/src/systemc/accellera_compatibility.cpp")
set(FSIM_SCV_COMPAT "${FSIM_SOURCE_DIR}/src/systemc/scv_compatibility.cpp")
set(FSIM_INCREMENTAL_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/incremental_compiler_test.cpp")
set(FSIM_PLUGIN_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/plugin_compiler_test.cpp")
set(FSIM_LOADER_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/plugin_loader_test.cpp")
set(FSIM_SCV_TEST
  "${FSIM_SOURCE_DIR}/tests/scv/scv_plugin_compiler_test.cpp")
set(FSIM_CACHE_TEST "${FSIM_SOURCE_DIR}/tests/compiler/cache_test.cpp")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_HEADER}"
    "${FSIM_CODEC}"
    "${FSIM_COMPILER}"
    "${FSIM_PLUGIN_COMPILER}"
    "${FSIM_COMPILER_COMMON}"
    "${FSIM_ACCEL_COMPAT}"
    "${FSIM_SCV_COMPAT}"
    "${FSIM_INCREMENTAL_TEST}"
    "${FSIM_PLUGIN_TEST}"
    "${FSIM_LOADER_TEST}"
    "${FSIM_SCV_TEST}"
    "${FSIM_CACHE_TEST}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "incremental native freeze input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "f2d1b2316d4bf168ccf9f6f259ee8477a6d9a1d06548e3a99624bdaccdb51e7d")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "incremental native contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 58)
  message(FATAL_ERROR "incremental native contract requires SPDX, header and 56 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER_ROW STREQUAL "kind\tname\tcontract")
  message(FATAL_ERROR "incremental native contract header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 57)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "incremental native row ${FSIM_INDEX} requires three fields")
  endif()
endforeach()

function(fsim_require_incremental_native_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "incremental native owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_incremental_native_tokens("${FSIM_HEADER}"
  "kIncrementalObjectFormatVersion = 2"
  "kIncrementalPluginFormatVersion = 2"
  "fsim-systemc-object.bin"
  "fsim-systemc-plugin.bin"
  "std::string scv_compatibility"
  "std::string compiler_fingerprint")
fsim_require_incremental_native_tokens("${FSIM_CODEC}"
  "'F', 'S', 'I', 'M', 'S', 'C', 'O', '\\0'"
  "'F', 'S', 'I', 'M', 'S', 'C', 'P', '\\0'"
  "fsim-systemc-object-input-v1"
  "fsim-systemc-plugin-input-v1"
  "writer.u32(kIncrementalObjectFormatVersion)"
  "writer.u32(kIncrementalPluginFormatVersion)"
  "std::string { \"fsim \" } + std::string { version }"
  "incremental_identity_diagnostic("
  "\".fsimscobj\""
  "\".fsimscplugin\""
  "native payload checksum mismatch"
  "invalid SystemC object supplied for publication"
  "invalid SystemC plug-in supplied for publication")
fsim_require_incremental_native_tokens("${FSIM_COMPILER}"
  "plugin_producer_fingerprint(settings, base, diagnostics)"
  "SystemC source or dependency changed during compilation"
  "systemc\" / \"incremental\" / kind"
  "SystemC link inputs have incompatible toolchain identities"
  "\"SystemC plug-in compiler producer\""
  "unsupported_artifact_identity("
  "HierarchyRegistry::load("
  "result.cache_hit = true")
fsim_require_incremental_native_tokens("${FSIM_PLUGIN_COMPILER}"
  "fsim-systemc-host-v1"
  "source-standard\", \"c++20"
  "runtime-abi"
  "systemc-abi"
  "accellera-compatibility"
  "scv-compatibility"
  "host-format"
  "msvc-runtime"
  "msvc-member-pointer-model\", \"/vmg"
  "add_compiler_environment_to_key")
fsim_require_incremental_native_tokens("${FSIM_COMPILER_COMMON}"
  "compiler.binary"
  "CPLUS_INCLUDE_PATH"
  "WindowsSdkDir"
  "SOURCE_DATE_EPOCH")
fsim_require_incremental_native_tokens("${FSIM_ACCEL_COMPAT}"
  "libc++-"
  "msvc-stl-"
  "libstdc++-"
  "|stdlib=")
fsim_require_incremental_native_tokens("${FSIM_SCV_COMPAT}"
  "|scv="
  "|scv-patch="
  "|stdlib=")

fsim_require_incremental_native_tokens("${FSIM_INCREMENTAL_TEST}"
  "kIncrementalObjectFormatVersion == 2U"
  "kIncrementalPluginFormatVersion == 2U"
  "stale-object-format"
  "future-object-format"
  "incompatible-object-runtime"
  "incompatible-object-systemc"
  "oversized-object-identity"
  "stale-object-producer"
  "stale-plugin-format"
  "future-plugin-format"
  "incompatible-plugin-runtime"
  "incompatible-plugin-systemc"
  "oversized-plugin-identity"
  "stale-plugin-producer"
  "stale-target.fsimscplugin"
  "not-a-native-shared-library"
  "cached_first_warm.success && cached_first_warm.cache_hit"
  "cached_first_changed.success && !cached_first_changed.cache_hit"
  "cached_second_unchanged.success && cached_second_unchanged.cache_hit")
fsim_require_incremental_native_tokens("${FSIM_PLUGIN_TEST}"
  "environment_plan->cache_key != first_plan->cache_key"
  "restored_environment_plan->cache_key == first_plan->cache_key")
fsim_require_incremental_native_tokens("${FSIM_LOADER_TEST}"
  "incompatible_scv_host"
  "unopened-truncated-host")
fsim_require_incremental_native_tokens("${FSIM_SCV_TEST}"
  "source_warm.success && source_warm.cache_hit"
  "source_edited.success && !source_edited.cache_hit"
  "source_edited.cache_key != source_cold.cache_key"
  "link_warm.success && link_warm.cache_hit")
fsim_require_incremental_native_tokens("${FSIM_CACHE_TEST}"
  "cache.store(key, replacement, error)"
  "skipped_locked_entries == 1"
  "removed_temporary_files == 1")
fsim_require_incremental_native_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.incremental-native-cache-freeze"
  "CheckIncrementalNativeFreeze.cmake")

message(STATUS
  "incremental native cache freeze passed: rows=56 digest=${FSIM_CONTRACT_DIGEST}")

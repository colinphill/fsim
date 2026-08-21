# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/native_producer_policy_contract.tsv")
set(FSIM_LIBRARY_IMPORT
  "${FSIM_SOURCE_DIR}/src/app/application_library_import.cpp")
set(FSIM_INCREMENTAL_CODEC
  "${FSIM_SOURCE_DIR}/src/systemc/incremental_artifact.cpp")
set(FSIM_INCREMENTAL_COMPILER
  "${FSIM_SOURCE_DIR}/src/systemc/incremental_compiler.cpp")
set(FSIM_PLUGIN_COMPILER
  "${FSIM_SOURCE_DIR}/src/systemc/plugin_compiler.cpp")
set(FSIM_SYSTEMC_LOADER
  "${FSIM_SOURCE_DIR}/src/systemc/plugin_loader.cpp")
set(FSIM_DPI_LOADER "${FSIM_SOURCE_DIR}/src/runtime/dpi_plugin.cpp")
set(FSIM_VPI_LOADER "${FSIM_SOURCE_DIR}/src/runtime/vpi_plugin.cpp")
set(FSIM_VHPI_LOADER "${FSIM_SOURCE_DIR}/src/runtime/vhpi_plugin.cpp")
set(FSIM_LLVM_KEY "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit_cache_key.cpp")
set(FSIM_LLVM_JIT "${FSIM_SOURCE_DIR}/src/compiler/llvm_jit.cpp")
set(FSIM_APPLICATION_TEST
  "${FSIM_SOURCE_DIR}/tests/app/application_test_cli.cpp")
set(FSIM_INCREMENTAL_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/incremental_compiler_test.cpp")
set(FSIM_SYSTEMC_LOADER_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/plugin_loader_test.cpp")
set(FSIM_DPI_TEST "${FSIM_SOURCE_DIR}/tests/runtime/runtime_dpi_tests.cpp")
set(FSIM_VPI_TEST "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vpi_tests.cpp")
set(FSIM_VHPI_TEST "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_tests.cpp")
set(FSIM_UVM_C_TEST
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_uvm_foreign_abi_c_test.c")
set(FSIM_SCV_TEST
  "${FSIM_SOURCE_DIR}/tests/scv/scv_plugin_compiler_test.cpp")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_LIBRARY_IMPORT}"
    "${FSIM_INCREMENTAL_CODEC}"
    "${FSIM_INCREMENTAL_COMPILER}"
    "${FSIM_PLUGIN_COMPILER}"
    "${FSIM_SYSTEMC_LOADER}"
    "${FSIM_DPI_LOADER}"
    "${FSIM_VPI_LOADER}"
    "${FSIM_VHPI_LOADER}"
    "${FSIM_LLVM_KEY}"
    "${FSIM_LLVM_JIT}"
    "${FSIM_APPLICATION_TEST}"
    "${FSIM_INCREMENTAL_TEST}"
    "${FSIM_SYSTEMC_LOADER_TEST}"
    "${FSIM_DPI_TEST}"
    "${FSIM_VPI_TEST}"
    "${FSIM_VHPI_TEST}"
    "${FSIM_UVM_C_TEST}"
    "${FSIM_SCV_TEST}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "native producer policy input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "bfaec1c5a63e7b59239096a507ef2e8e6e050de4e10d73b8c394d157f2f1229c")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "native producer policy digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 39)
  message(FATAL_ERROR "native producer policy requires SPDX, header and 37 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL
     "family\tboundary\texact_identity\tpreload_and_containment")
  message(FATAL_ERROR "native producer policy header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 38)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 4)
    message(FATAL_ERROR "native producer row ${FSIM_INDEX} requires four fields")
  endif()
endforeach()

function(fsim_require_native_producer_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "native producer owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

function(fsim_require_native_producer_order path anchor first second)
  file(READ "${path}" FSIM_CONTENTS)
  string(FIND "${FSIM_CONTENTS}" "${anchor}" FSIM_ANCHOR_OFFSET)
  if(FSIM_ANCHOR_OFFSET EQUAL -1)
    message(FATAL_ERROR "native producer order anchor is missing in ${path}: ${anchor}")
  endif()
  string(SUBSTRING "${FSIM_CONTENTS}" ${FSIM_ANCHOR_OFFSET} -1 FSIM_TAIL)
  string(FIND "${FSIM_TAIL}" "${first}" FSIM_FIRST_OFFSET)
  string(FIND "${FSIM_TAIL}" "${second}" FSIM_SECOND_OFFSET)
  if(FSIM_FIRST_OFFSET EQUAL -1 OR FSIM_SECOND_OFFSET EQUAL -1 OR
     NOT FSIM_FIRST_OFFSET LESS FSIM_SECOND_OFFSET)
    message(FATAL_ERROR
      "native producer order changed in ${path}: ${first} must precede ${second}")
  endif()
endfunction()

fsim_require_native_producer_tokens("${FSIM_LIBRARY_IMPORT}"
  "mapped LLVM native object producer"
  "mapped SystemC native plug-in producer"
  "native.runtime_abi != runtime_abi_version"
  "native.compiler_fingerprint != *fingerprint")
fsim_require_native_producer_order("${FSIM_LIBRARY_IMPORT}"
  "bool admit_llvm_artifact(" "mapped LLVM native object producer" "read_payload(")
fsim_require_native_producer_order("${FSIM_LIBRARY_IMPORT}"
  "bool admit_systemc_artifact(" "mapped SystemC native plug-in producer" "read_payload(")

fsim_require_native_producer_tokens("${FSIM_INCREMENTAL_CODEC}"
  "writer.u32(kIncrementalObjectFormatVersion)"
  "writer.u32(kIncrementalPluginFormatVersion)"
  "std::string { \"fsim \" } + std::string { version }"
  "writer.string(metadata.producer)"
  "incremental_identity_diagnostic("
  "\".fsimscobj\""
  "\".fsimscplugin\"")
file(READ "${FSIM_INCREMENTAL_CODEC}" FSIM_INCREMENTAL_CODEC_TEXT)
if(FSIM_INCREMENTAL_CODEC_TEXT MATCHES "writer\\.u32\\(metadata\\.format\\)")
  message(FATAL_ERROR "incremental native writers must not emit caller-supplied formats")
endif()

fsim_require_native_producer_tokens("${FSIM_INCREMENTAL_COMPILER}"
  "\"cached SystemC link producer\""
  "\"SystemC object producer\""
  "\"SystemC plug-in producer\""
  "unsupported_artifact_identity("
  "target_identity()"
  "HierarchyRegistry::load(")
fsim_require_native_producer_order("${FSIM_INCREMENTAL_COMPILER}"
  "std::shared_ptr<HierarchyRegistry> load_incremental_plugin("
  "metadata->producer" "HierarchyRegistry::load(")

fsim_require_native_producer_tokens("${FSIM_PLUGIN_COMPILER}"
  "fsim-systemc-host-v1"
  "runtime-abi"
  "systemc-abi"
  "accellera-compatibility"
  "scv-compatibility"
  "host-format"
  "msvc-runtime"
  "msvc-member-pointer-model\", \"/vmg"
  "add_compiler_environment_to_key")

fsim_require_native_producer_tokens("${FSIM_SYSTEMC_LOADER}"
  "host.abi_version != FSIM_SYSTEMC_ABI_VERSION"
  "fsim_scv_accepts_compatibility_identity("
  "platform::DynamicLibrary::open(")
fsim_require_native_producer_order("${FSIM_SYSTEMC_LOADER}"
  "std::unique_ptr<Plugin> Plugin::load(" "host.abi_version" "platform::DynamicLibrary::open(")
fsim_require_native_producer_order("${FSIM_SYSTEMC_LOADER}"
  "std::unique_ptr<Plugin> Plugin::load(" "fsim_scv_accepts_compatibility_identity(" "platform::DynamicLibrary::open(")

fsim_require_native_producer_order("${FSIM_VPI_LOADER}"
  "load_systemverilog_vpi_plugin(" "validate_systemverilog_vpi_host(" "platform::DynamicLibrary::open(")
fsim_require_native_producer_order("${FSIM_VHPI_LOADER}"
  "load_vhdl_vhpi_plugin(" "validate_vhdl_vhpi_host(" "platform::DynamicLibrary::open(")
fsim_require_native_producer_order("${FSIM_DPI_LOADER}"
  "load_systemverilog_dpi_plugin(" "validate_systemverilog_dpi_plugin_descriptor(" "resolve(manifest.imported_symbols")

fsim_require_native_producer_tokens("${FSIM_LLVM_KEY}"
  "kNativeObjectCacheSchema"
  "runtime-abi-version"
  "llvm-version"
  "data-layout"
  "resume-result-abi-version"
  "container-semantics")
fsim_require_native_producer_tokens("${FSIM_LLVM_JIT}"
  "isRelocatableObject()"
  "getBytesInAddress() == sizeof(void*)"
  "getArch() == target_triple_.getArch()"
  "getTripleObjectFormat()"
  "target_triple_.getObjectFormat()")

fsim_require_native_producer_tokens("${FSIM_APPLICATION_TEST}"
  "mapped-native-incompatible"
  "mapped-systemc-incompatible"
  "incompatible_cache_has_file")
fsim_require_native_producer_tokens("${FSIM_INCREMENTAL_TEST}"
  "stale-object-producer"
  "stale-plugin-producer"
  "stale-target.fsimscplugin"
  "not-a-native-shared-library"
  "cached_first_warm.success && cached_first_warm.cache_hit"
  "cached_first_changed.success && !cached_first_changed.cache_hit"
  "cached_second_unchanged.success && cached_second_unchanged.cache_hit")
fsim_require_native_producer_tokens("${FSIM_SYSTEMC_LOADER_TEST}"
  "incompatible_scv_host"
  "unopened-truncated-host")
fsim_require_native_producer_tokens("${FSIM_DPI_TEST}"
  "validate_systemverilog_dpi_plugin_descriptor"
  "DPI plug-in ABI descriptors reject reserved flags")
fsim_require_native_producer_tokens("${FSIM_VPI_TEST}"
  "validate_systemverilog_vpi_host"
  "VPI loader rejects a truncated plug-in descriptor")
fsim_require_native_producer_tokens("${FSIM_VHPI_TEST}"
  "validate_vhdl_vhpi_host"
  "VHPI loader rejects a truncated plug-in descriptor")
fsim_require_native_producer_tokens("${FSIM_UVM_C_TEST}"
  "FSIM_UVM_FOREIGN_ABI_VERSION"
  "FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, release, 40u)")
fsim_require_native_producer_tokens("${FSIM_SCV_TEST}"
  "source_warm.success && source_warm.cache_hit"
  "source_edited.success && !source_edited.cache_hit"
  "link_warm.success && link_warm.cache_hit")
fsim_require_native_producer_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.native-producer-policy"
  "CheckNativeProducerPolicy.cmake")

message(STATUS
  "native producer policy passed: rows=37 digest=${FSIM_CONTRACT_DIGEST}")

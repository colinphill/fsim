# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/systemc_abi_contract.tsv")
set(FSIM_SYSTEMC_METADATA
  "${FSIM_SOURCE_DIR}/cmake/FsimSystemCAccellera.cmake")
set(FSIM_SCV_METADATA "${FSIM_SOURCE_DIR}/cmake/FsimScv.cmake")
set(FSIM_ABI_HEADER "${FSIM_SOURCE_DIR}/include/fsim/systemc_abi.h")
set(FSIM_ACCELERA_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/accellera.hpp")
set(FSIM_SCV_HEADER "${FSIM_SOURCE_DIR}/include/fsim/systemc/scv.hpp")
set(FSIM_KERNEL_PROTOCOL
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/kernel_backend_protocol.hpp")
set(FSIM_SCV_PROTOCOL
  "${FSIM_SOURCE_DIR}/include/fsim/systemc/scv_backend_protocol.hpp")
set(FSIM_TRANSACTION_PROTOCOL
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/transaction_record.hpp")
set(FSIM_SCV_COMPATIBILITY
  "${FSIM_SOURCE_DIR}/src/systemc/scv_compatibility.cpp")
set(FSIM_C_PROBE "${FSIM_SOURCE_DIR}/tests/systemc/systemc_abi_c_test.c")
set(FSIM_CPP_PROBE
  "${FSIM_SOURCE_DIR}/tests/systemc/systemc_compatibility_test.cpp")
set(FSIM_LOADER_TEST
  "${FSIM_SOURCE_DIR}/tests/systemc/plugin_loader_test.cpp")
set(FSIM_INSTALLED_CONSUMER
  "${FSIM_SOURCE_DIR}/tests/systemc/installed_consumer/CMakeLists.txt")
set(FSIM_INSTALLED_CONTRACT
  "${FSIM_SOURCE_DIR}/cmake/CheckInstalledPublicContract.cmake")
set(FSIM_SHARED_CONTRACT
  "${FSIM_SOURCE_DIR}/cmake/CheckSystemCSharedRuntime.cmake")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_SYSTEMC_METADATA}"
    "${FSIM_SCV_METADATA}"
    "${FSIM_ABI_HEADER}"
    "${FSIM_ACCELERA_HEADER}"
    "${FSIM_SCV_HEADER}"
    "${FSIM_KERNEL_PROTOCOL}"
    "${FSIM_SCV_PROTOCOL}"
    "${FSIM_TRANSACTION_PROTOCOL}"
    "${FSIM_SCV_COMPATIBILITY}"
    "${FSIM_C_PROBE}"
    "${FSIM_CPP_PROBE}"
    "${FSIM_LOADER_TEST}"
    "${FSIM_INSTALLED_CONSUMER}"
    "${FSIM_INSTALLED_CONTRACT}"
    "${FSIM_SHARED_CONTRACT}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "SystemC ABI freeze input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "b148b76bc867a2778e7fb27fdd2ad4e8bba900f62aa51d51af8add9cfcb531f5")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "SystemC ABI contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 28)
  message(FATAL_ERROR "SystemC ABI contract requires 28 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER_ROW)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER_ROW STREQUAL "kind\tname\tcontract")
  message(FATAL_ERROR "SystemC ABI contract header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 27)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "SystemC ABI row ${FSIM_INDEX} requires three fields")
  endif()
endforeach()

function(fsim_require_systemc_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "SystemC ABI owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_systemc_tokens("${FSIM_SYSTEMC_METADATA}"
  "set(FSIM_SYSTEMC_VERSION \"3.0.2\")"
  "set(FSIM_SYSTEMC_TLM_VERSION \"2.0.6\")"
  "70b0fc8e4a74acc677b0fc73cea08f940c2115d5"
  "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4"
  "b1fbb8b7bcb76e3f803155585f2c3e1073e4c990de72302040cfab283c9f9fd0")
fsim_require_systemc_tokens("${FSIM_SCV_METADATA}"
  "set(FSIM_SCV_VERSION \"2.0.1\")"
  "7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881"
  "85cc2e4e3ee1893878de2567f253003859c847a151f080e48f185825c34f933c"
  "61f2a7a414b317bba1f566bfd855c0ae1329c6f848d8bf5fa22b584c2f71969a"
  "e7590f83e157e7df3362c9987ccc55b6b50328d51ec809c489e9ab1b2f80c598")
fsim_require_systemc_tokens("${FSIM_ABI_HEADER}"
  "FSIM_SYSTEMC_ABI_VERSION 4u"
  "typedef struct fsim_sc_value_view_v1"
  "typedef struct fsim_sc_host_v1"
  "const char* scv_compatibility_identity"
  "typedef struct fsim_sc_registrar_v1"
  "typedef fsim_sc_status_v1 (*fsim_plugin_init_v1_fn)"
  "FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1")
fsim_require_systemc_tokens("${FSIM_C_PROBE}"
  "FSIM_SC_LAYOUT(fsim_sc_value_view_v1, 32u, 8u)"
  "FSIM_SC_LAYOUT(fsim_sc_host_v1, 152u, 8u)"
  "FSIM_SC_OFFSET(fsim_sc_host_v1, scv_compatibility_identity, 144u)"
  "FSIM_SC_LAYOUT(fsim_sc_registrar_v1, 32u, 8u)"
  "fsim_plugin_init_v1_fn")
fsim_require_systemc_tokens("${FSIM_CPP_PROBE}"
  "TLM_VERSION_MAJOR == 2"
  "TLM_VERSION_PATCH == 6"
  "sizeof(fsim_sc_host_v1) == 152U"
  "kSystemCKernelMessageHeaderBytes == 128U"
  "scv_backend_message_header_bytes == 160U"
  "transaction_record_schema_version == 1U"
  "!std::is_pointer_v<fsim::systemc::SystemCIslandId>"
  "!std::is_pointer_v<fsim::systemc::ScvIslandId>")
fsim_require_systemc_tokens("${FSIM_LOADER_TEST}"
  "sizeof(host) + 64"
  "sizeof(registrar) + 64"
  "unopened-truncated-host"
  "unopened-truncated-registrar"
  "SystemC host/registrar ABI mismatch")
fsim_require_systemc_tokens("${FSIM_ACCELERA_HEADER}"
  "FSIM_SYSTEMC_ACCELERA_VERSION \"3.0.2\""
  "FSIM_SYSTEMC_BRIDGE_REVISION 2u"
  "fsim_systemc_accellera_runtime_identity() noexcept"
  "fsim_systemc_accellera_compatibility_identity() noexcept")
fsim_require_systemc_tokens("${FSIM_SCV_HEADER}"
  "FSIM_SCV_VERSION \"2.0.1\""
  "FSIM_SCV_ADAPTER_ABI_VERSION 1u"
  "FSIM_SCV_PLUGIN_ABI_VERSION 1u"
  "FSIM_SCV_ARTIFACT_SCHEMA_VERSION 1u"
  "FSIM_SCV_CACHE_SCHEMA_VERSION 1u")
fsim_require_systemc_tokens("${FSIM_SCV_COMPATIBILITY}"
  "std::array<std::string_view, 16>"
  "|tlm=2.0.6.20191203"
  "|compiler="
  "|stdlib=")
fsim_require_systemc_tokens("${FSIM_KERNEL_PROTOCOL}"
  "kSystemCKernelProtocolVersion = 1U"
  "kSystemCKernelMessageHeaderBytes = 128U"
  "std::is_trivially_copyable_v<SystemCTransactionId>")
fsim_require_systemc_tokens("${FSIM_SCV_PROTOCOL}"
  "scv_backend_protocol_version = 1U"
  "scv_backend_message_header_bytes = 160U"
  "std::is_trivially_copyable_v<ScvTransactionId>")
fsim_require_systemc_tokens("${FSIM_TRANSACTION_PROTOCOL}"
  "transaction_record_schema_version = 1U"
  "struct TransactionStableId"
  "serialize_transaction_record("
  "deserialize_transaction_record(")
fsim_require_systemc_tokens("${FSIM_INSTALLED_CONSUMER}"
  "find_package(SystemCLanguage 3.0.2.20251031 EXACT CONFIG REQUIRED)"
  "find_package(SystemCTLM 2.0.6.20191203 EXACT CONFIG REQUIRED)"
  "SystemC::systemc")
fsim_require_systemc_tokens("${FSIM_INSTALLED_CONTRACT}"
  "SystemCLanguageConfig.cmake"
  "SystemCTLMConfig.cmake"
  "pkgconfig/systemc.pc"
  "pkgconfig/tlm.pc")
fsim_require_systemc_tokens("${FSIM_SHARED_CONTRACT}"
  "runtime_count EQUAL 1"
  "bridge runtime differs from governed target")
fsim_require_systemc_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.systemc.abi-freeze"
  "CheckSystemCAbiFreeze.cmake")

message(STATUS
  "SystemC/TLM/SCV ABI freeze passed: rows=26 digest=${FSIM_CONTRACT_DIGEST}")

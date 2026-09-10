# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/foreign_abi_contract.tsv")
set(FSIM_DPI_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/dpi_plugin_abi.h")
set(FSIM_SVDPI_HEADER "${FSIM_SOURCE_DIR}/include/svdpi.h")
set(FSIM_SVDPI_IMPLEMENTATION "${FSIM_SOURCE_DIR}/src/runtime/svdpi.cpp")
set(FSIM_SVDPI_BRIDGE_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/svdpi_bridge.h")
set(FSIM_VPI_HEADER "${FSIM_SOURCE_DIR}/include/fsim/runtime/vpi_abi.h")
set(FSIM_VPI_STANDARD_HEADER "${FSIM_SOURCE_DIR}/include/vpi_user.h")
set(FSIM_SV_VPI_STANDARD_HEADER
  "${FSIM_SOURCE_DIR}/include/sv_vpi_user.h")
set(FSIM_VPI_BRIDGE_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/vpi_bridge.h")
set(FSIM_VPI_STANDARD_IMPLEMENTATION
  "${FSIM_SOURCE_DIR}/src/runtime/vpi_standard.cpp")
set(FSIM_VHPI_HEADER "${FSIM_SOURCE_DIR}/include/fsim/runtime/vhpi_abi.h")
set(FSIM_UVM_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/runtime/uvm_foreign_abi.h")
set(FSIM_DPI_C_PROBE
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_dpi_abi_c_test.c")
set(FSIM_VPI_C_PROBE
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vpi_abi_c_test.c")
set(FSIM_VHPI_C_PROBE
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_abi_c_test.c")
set(FSIM_UVM_C_PROBE
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_uvm_foreign_abi_c_test.c")
set(FSIM_CPP_PROBE
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_foreign_abi_cpp_test.cpp")
set(FSIM_DPI_TESTS
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_dpi_tests.cpp")
set(FSIM_VPI_TESTS
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vpi_tests.cpp")
set(FSIM_VPI_STARTUP_FIXTURE
  "${FSIM_SOURCE_DIR}/tests/runtime/vpi_standard_startup_plugin.c")
set(FSIM_VHPI_TESTS
  "${FSIM_SOURCE_DIR}/tests/runtime/runtime_vhpi_tests.cpp")
set(FSIM_UVM_TESTS
  "${FSIM_SOURCE_DIR}/tests/app/application_test_classes.cpp")
set(FSIM_DPI_LOADER "${FSIM_SOURCE_DIR}/src/runtime/dpi_plugin.cpp")
set(FSIM_VPI_LOADER "${FSIM_SOURCE_DIR}/src/runtime/vpi_plugin.cpp")
set(FSIM_VHPI_LOADER "${FSIM_SOURCE_DIR}/src/runtime/vhpi_plugin.cpp")
set(FSIM_UVM_HOST "${FSIM_SOURCE_DIR}/src/runtime/uvm_foreign.cpp")
set(FSIM_RUNTIME_BUILD
  "${FSIM_SOURCE_DIR}/tests/runtime/CMakeLists.txt")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")
foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_DPI_HEADER}"
    "${FSIM_SVDPI_HEADER}"
    "${FSIM_SVDPI_IMPLEMENTATION}"
    "${FSIM_SVDPI_BRIDGE_HEADER}"
    "${FSIM_VPI_HEADER}"
    "${FSIM_VPI_STANDARD_HEADER}"
    "${FSIM_SV_VPI_STANDARD_HEADER}"
    "${FSIM_VPI_BRIDGE_HEADER}"
    "${FSIM_VPI_STANDARD_IMPLEMENTATION}"
    "${FSIM_VHPI_HEADER}"
    "${FSIM_UVM_HEADER}"
    "${FSIM_DPI_C_PROBE}"
    "${FSIM_VPI_C_PROBE}"
    "${FSIM_VHPI_C_PROBE}"
    "${FSIM_UVM_C_PROBE}"
    "${FSIM_CPP_PROBE}"
    "${FSIM_DPI_TESTS}"
    "${FSIM_VPI_TESTS}"
    "${FSIM_VPI_STARTUP_FIXTURE}"
    "${FSIM_VHPI_TESTS}"
    "${FSIM_UVM_TESTS}"
    "${FSIM_DPI_LOADER}"
    "${FSIM_VPI_LOADER}"
    "${FSIM_VHPI_LOADER}"
    "${FSIM_UVM_HOST}"
    "${FSIM_RUNTIME_BUILD}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR "foreign ABI freeze input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_DIGEST
  "121bff0ce429135089c6e68212398ee54a273e5cb509edbff4b289729ad67b60")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_DIGEST)
  message(FATAL_ERROR
    "foreign ABI contract digest changed: expected ${FSIM_EXPECTED_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()
file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 37)
  message(FATAL_ERROR "foreign ABI contract requires 37 rows")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0" OR
   NOT FSIM_HEADER STREQUAL "kind\tname\tcontract")
  message(FATAL_ERROR "foreign ABI contract header or SPDX policy changed")
endif()
foreach(FSIM_INDEX RANGE 2 36)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR "foreign ABI row ${FSIM_INDEX} requires three fields")
  endif()
endforeach()

function(fsim_require_foreign_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR "foreign ABI owner ${path} lost: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_foreign_tokens("${FSIM_DPI_HEADER}"
  "FSIM_DPI_PLUGIN_ABI_VERSION 1u"
  "fsim_dpi_plugin_descriptor_v1_get"
  "fsim_dpi_plugin_descriptor_v1_get_fn")
fsim_require_foreign_tokens("${FSIM_SVDPI_HEADER}"
  "typedef s_vpi_vecval svLogicVecVal"
  "typedef s_vpi_time svTimeVal"
  "SV_PACKED_DATA_NELEMS"
  "svGetArrElemPtr3"
  "svGetScopeFromName"
  "svGetTimePrecision")
fsim_require_foreign_tokens("${FSIM_SVDPI_IMPLEMENTATION}"
  "return \"1800-2023\""
  "svGetBitselLogic"
  "svPutBitselLogic"
  "svGetPartselLogic"
  "svPutPartselLogic"
  "fsim_svdpi_call_context_enter_v3"
  "fsim_svdpi_call_context_leave_v3")
fsim_require_foreign_tokens("${FSIM_SVDPI_BRIDGE_HEADER}"
  "FSIM_SVDPI_CONTEXT_ABI_VERSION 3u"
  "typedef struct fsim_svdpi_call_context_v3"
  "fsim_svdpi_current_call_context_v3")
fsim_require_foreign_tokens("${FSIM_VPI_HEADER}"
  "FSIM_VPI_HOST_ABI_VERSION_V2 2u"
  "FSIM_VPI_STARTUP_ROUTINES_SYMBOL \"vlog_startup_routines\""
  "fsim_vpi_startup_routine_v1"
  "fsim_vpi_plugin_bind_v1"
  "typedef struct fsim_vpi_host_v2"
  "typedef struct fsim_vpi_plugin_v1")
fsim_require_foreign_tokens("${FSIM_VPI_STANDARD_HEADER}"
  "typedef PLI_UINT32 *vpiHandle"
  "typedef struct t_vpi_time"
  "typedef struct t_vpi_value"
  "vpi_register_cb"
  "vpi_handle_by_multi_index"
  "vpi_get_value_array")
fsim_require_foreign_tokens("${FSIM_SV_VPI_STANDARD_HEADER}"
  "vpiPackage"
  "vpi_register_assertion_cb"
  "vpiCoverageStop"
  "vpiTrvsObj 800"
  "vpi_load_extension"
  "vpi_filter")
fsim_require_foreign_tokens("${FSIM_VPI_BRIDGE_HEADER}"
  "FSIM_VPI_CONTEXT_ABI_VERSION 1u"
  "typedef struct fsim_vpi_call_context_v1"
  "fsim_vpi_current_call_context_v1")
fsim_require_foreign_tokens("${FSIM_VPI_STANDARD_IMPLEMENTATION}"
  "maximum_context_depth = 64U"
  "maximum_text_size = 1U << 20U"
  "invoke_extension"
  "fsim_vpi_call_context_enter_v1"
  "vpi_register_assertion_cb"
  "vpi_load_extension")
fsim_require_foreign_tokens("${FSIM_VHPI_HEADER}"
  "FSIM_VHPI_HOST_ABI_VERSION_V2 2u"
  "FSIM_VHPI_HOST_ABI_VERSION_V3 3u"
  "fsim_vhpi_plugin_bind_v1"
  "typedef struct fsim_vhpi_host_v2"
  "typedef struct fsim_vhpi_host_v3"
  "fsim_vhpi_query_capabilities_v3"
  "fsim_vhpi_access_value_v3"
  "fsim_vhpi_execute_tool_v3"
  "typedef struct fsim_vhpi_plugin_v1")
fsim_require_foreign_tokens("${FSIM_UVM_HEADER}"
  "FSIM_UVM_FOREIGN_ABI_VERSION 1u"
  "typedef struct fsim_uvm_foreign_snapshot_v1"
  "typedef struct fsim_uvm_foreign_record_v1"
  "typedef struct fsim_uvm_foreign_activity_v1"
  "typedef struct fsim_uvm_foreign_host_v1")

fsim_require_foreign_tokens("${FSIM_DPI_C_PROBE}"
  "sizeof(fsim_dpi_plugin_descriptor_v1) == 32u"
  "sizeof(svLogicVecVal) == 8u"
  "sizeof(svTimeVal) == 24u"
  "sizeof(fsim_svdpi_call_context_v3) == 112u"
  "fsim_svdpi_time_scale_fn"
  "FSIM_DPI_OFFSET(name, 24u)"
  "fsim_dpi_plugin_descriptor_v1_get_fn")
fsim_require_foreign_tokens("${FSIM_VPI_C_PROBE}"
  "FSIM_VPI_LAYOUT(fsim_vpi_host_v1, 40u, 8u)"
  "FSIM_VPI_LAYOUT(fsim_vpi_service_request_v1, 48u, 8u)"
  "FSIM_VPI_LAYOUT(fsim_vpi_host_v2, 56u, 8u)"
  "FSIM_VPI_LAYOUT(fsim_vpi_plugin_v1, 48u, 8u)"
  "FSIM_VPI_LAYOUT(s_vpi_time, 24u, 8u)"
  "FSIM_VPI_LAYOUT(s_cb_data, 56u, 8u)"
  "FSIM_VPI_LAYOUT(fsim_vpi_call_context_v1, 24u, 8u)")
fsim_require_foreign_tokens("${FSIM_VHPI_C_PROBE}"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_host_v1, 40u, 8u)"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_service_request_v1, 48u, 8u)"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_host_v2, 56u, 8u)"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_capabilities_v3, 32u, 4u)"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_value_v3, 48u, 8u)"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_tool_request_v3, 32u, 8u)"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_host_v3, 80u, 8u)"
  "FSIM_VHPI_LAYOUT(fsim_vhpi_plugin_v1, 48u, 8u)")
fsim_require_foreign_tokens("${FSIM_UVM_C_PROBE}"
  "FSIM_UVM_LAYOUT(fsim_uvm_foreign_snapshot_v1, 48u, 8u)"
  "FSIM_UVM_LAYOUT(fsim_uvm_foreign_record_v1, 72u, 8u)"
  "FSIM_UVM_LAYOUT(fsim_uvm_foreign_activity_v1, 88u, 8u)"
  "FSIM_UVM_LAYOUT(fsim_uvm_foreign_host_v1, 64u, 8u)")
fsim_require_foreign_tokens("${FSIM_CPP_PROBE}"
  "std::is_same_v<fsim_dpi_plugin_descriptor_v1_get_fn"
  "std::is_same_v<fsim_vpi_plugin_bind_v1_fn"
  "std::is_same_v<decltype(&vpi_register_assertion_cb)"
  "std::is_same_v<fsim_vhpi_plugin_bind_v1_fn"
  "std::is_same_v<fsim_vhpi_query_capabilities_v3"
  "std::is_same_v<fsim_vhpi_access_value_v3"
  "std::is_same_v<fsim_vhpi_execute_tool_v3"
  "std::is_same_v<fsim_uvm_foreign_activity_callback_v1")

fsim_require_foreign_tokens("${FSIM_DPI_TESTS}"
  "test_systemverilog_dpi_scalar_marshalling"
  "test_systemverilog_dpi_composite_marshalling"
  "test_systemverilog_dpi_open_arrays"
  "loaded foreign C code uses the standard DPI context surface"
  "one-byte truncated prefix"
  "future append-only extents")
fsim_require_foreign_tokens("${FSIM_VPI_TESTS}"
  ".unopened-truncated-host"
  "before opening an image"
  "SystemVerilogVpiPluginEntryKind::StandardStartupTable"
  "startup_routine_count() == 2U")
fsim_require_foreign_tokens("${FSIM_VPI_STARTUP_FIXTURE}"
  "vlog_startup_routines[]"
  "fsim_vpi_standard_startup_count")
fsim_require_foreign_tokens("${FSIM_VHPI_TESTS}"
  ".unopened-truncated-host"
  "before opening an image")
fsim_require_foreign_tokens("${FSIM_UVM_TESTS}"
  "make_systemverilog_uvm_foreign_host"
  "FSIM_UVM_FOREIGN_BUFFER_TOO_SMALL"
  "FSIM_UVM_FOREIGN_STALE_HANDLE")

fsim_require_foreign_tokens("${FSIM_DPI_LOADER}"
  "validate_systemverilog_dpi_plugin_descriptor"
  "FSIM_DPI_PLUGIN_DESCRIPTOR_SYMBOL")
fsim_require_foreign_tokens("${FSIM_VPI_LOADER}"
  "validate_systemverilog_vpi_host(host)"
  "FSIM_VPI_PLUGIN_BIND_SYMBOL")
fsim_require_foreign_tokens("${FSIM_VHPI_LOADER}"
  "validate_vhdl_vhpi_host(host)"
  "FSIM_VHPI_PLUGIN_BIND_SYMBOL")
fsim_require_foreign_tokens("${FSIM_UVM_HOST}"
  "make_systemverilog_uvm_foreign_host"
  "sizeof(fsim_uvm_foreign_host_v1)")
fsim_require_foreign_tokens("${FSIM_RUNTIME_BUILD}"
  "runtime_dpi_abi_c_test.c"
  "runtime_foreign_abi_cpp_test.cpp"
  "target_link_libraries(fsim_dpi_test_plugin PRIVATE fsim_tf)"
  "fsim_vpi_standard_startup_plugin")
fsim_require_foreign_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.foreign-abi-freeze"
  "CheckForeignAbiFreeze.cmake")

message(STATUS
  "DPI/VPI/VHPI/UVM foreign ABI freeze passed: rows=35 digest=${FSIM_CONTRACT_DIGEST}")

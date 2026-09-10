// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "fsim/runtime/uvm_foreign_abi.h"
#include "fsim/runtime/vhpi_abi.h"
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/vpi_bridge.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace {

static_assert(sizeof(void*) == 8U);
static_assert(sizeof(svScalar) == 1U);
static_assert(sizeof(svBitVecVal) == 4U);
static_assert(sizeof(svLogicVecVal) == 8U);
static_assert(alignof(svLogicVecVal) == 4U);
static_assert(offsetof(svLogicVecVal, aval) == 0U);
static_assert(offsetof(svLogicVecVal, bval) == 4U);
static_assert(sizeof(svTimeVal) == 24U);
static_assert(alignof(svTimeVal) == 8U);
static_assert(offsetof(svTimeVal, real) == 16U);
static_assert(FSIM_SVDPI_CONTEXT_ABI_VERSION == 3U);
static_assert(sizeof(fsim_svdpi_call_context_v3) == 112U);
static_assert(
    offsetof(fsim_svdpi_call_context_v3, get_time_precision) == 104U);
static_assert(SV_PACKED_DATA_NELEMS(65) == 3);
using DpiVersion = const char* (*)();
using DpiGetLogic = svLogic (*)(const svLogicVecVal*, int);
using DpiOpenArrayElement = void* (*)(
    const svOpenArrayHandle, int, int, int);
using DpiPutUserData = int (*)(const svScope, void*, void*);
using DpiGetTime = int (*)(const svScope, svTimeVal*);
using DpiGetTimeScale = int (*)(const svScope, std::int32_t*);
static_assert(std::is_same_v<decltype(&svDpiVersion), DpiVersion>);
static_assert(std::is_same_v<decltype(&svGetBitselLogic), DpiGetLogic>);
static_assert(
    std::is_same_v<decltype(&svGetArrElemPtr3), DpiOpenArrayElement>);
static_assert(std::is_same_v<decltype(&svPutUserData), DpiPutUserData>);
static_assert(std::is_same_v<decltype(&svGetTime), DpiGetTime>);
static_assert(std::is_same_v<decltype(&svGetTimeUnit), DpiGetTimeScale>);
static_assert(
    std::is_same_v<decltype(&svGetTimePrecision), DpiGetTimeScale>);
static_assert(
    std::string_view { FSIM_DPI_PLUGIN_DESCRIPTOR_SYMBOL }
    == "fsim_dpi_plugin_descriptor_v1_get");
static_assert(
    std::string_view { FSIM_VPI_PLUGIN_BIND_SYMBOL }
    == "fsim_vpi_plugin_bind_v1");
static_assert(
    std::string_view { FSIM_VHPI_PLUGIN_BIND_SYMBOL }
    == "fsim_vhpi_plugin_bind_v1");

static_assert(sizeof(fsim_dpi_plugin_descriptor_v1) == 32U);
static_assert(alignof(fsim_dpi_plugin_descriptor_v1) == 8U);
static_assert(offsetof(fsim_dpi_plugin_descriptor_v1, name) == 24U);
using DpiDescriptorGet = const fsim_dpi_plugin_descriptor_v1*(FSIM_DPI_PLUGIN_CALL*)();
static_assert(
    std::is_same_v<fsim_dpi_plugin_descriptor_v1_get_fn, DpiDescriptorGet>);

static_assert(sizeof(fsim_vpi_handle_v1) == 8U);
static_assert(sizeof(fsim_vpi_startup_routine_v1) == sizeof(void*));
static_assert(sizeof(fsim_vpi_error_view_v1) == 32U);
static_assert(sizeof(fsim_vpi_host_v1) == 40U);
static_assert(sizeof(fsim_vpi_service_request_v1) == 48U);
static_assert(sizeof(fsim_vpi_service_result_v1) == 40U);
static_assert(sizeof(fsim_vpi_host_v2) == 56U);
static_assert(sizeof(fsim_vpi_plugin_v1) == 48U);
static_assert(sizeof(vpiHandle) == sizeof(void*));
static_assert(sizeof(s_vpi_time) == 24U);
static_assert(sizeof(s_vpi_vecval) == 8U);
static_assert(sizeof(s_vpi_value) == 16U);
static_assert(sizeof(s_cb_data) == 56U);
static_assert(sizeof(s_vpi_attempt_info) == 32U);
static_assert(sizeof(fsim_vpi_call_context_v1) == 24U);
using VpiGet = PLI_INT32 (*)(PLI_INT32, vpiHandle);
using VpiGet64 = PLI_INT64 (*)(PLI_INT32, vpiHandle);
using VpiGetValue = void (*)(vpiHandle, p_vpi_value);
using VpiPutValue = vpiHandle (*)(
    vpiHandle, p_vpi_value, p_vpi_time, PLI_INT32);
using VpiLoadInit = PLI_INT32 (*)(vpiHandle, vpiHandle, PLI_INT32);
using VpiAssertionRegister = vpiHandle (*)(
    vpiHandle, PLI_INT32, vpi_assertion_callback_func*, PLI_BYTE8*);
static_assert(std::is_same_v<decltype(&vpi_get), VpiGet>);
static_assert(std::is_same_v<decltype(&vpi_get64), VpiGet64>);
static_assert(std::is_same_v<decltype(&vpi_get_value), VpiGetValue>);
static_assert(std::is_same_v<decltype(&vpi_put_value), VpiPutValue>);
static_assert(std::is_same_v<decltype(&vpi_load_init), VpiLoadInit>);
static_assert(
    std::is_same_v<decltype(&vpi_register_assertion_cb),
                   VpiAssertionRegister>);
using VpiReport = void(FSIM_VPI_CALL*)(
    void*, const fsim_vpi_error_view_v1*);
using VpiInvoke = fsim_vpi_status_v1(FSIM_VPI_CALL*)(
    void*, const fsim_vpi_service_request_v1*, fsim_vpi_service_result_v1*);
using VpiLifecycle = fsim_vpi_status_v1(FSIM_VPI_CALL*)(void*);
using VpiBind = fsim_vpi_status_v1(FSIM_VPI_CALL*)(
    const fsim_vpi_host_v1*, fsim_vpi_plugin_v1*);
static_assert(std::is_same_v<fsim_vpi_report_v1, VpiReport>);
static_assert(std::is_same_v<fsim_vpi_invoke_service_v1, VpiInvoke>);
static_assert(std::is_same_v<fsim_vpi_plugin_lifecycle_v1, VpiLifecycle>);
static_assert(std::is_same_v<fsim_vpi_plugin_bind_v1_fn, VpiBind>);

static_assert(sizeof(fsim_vhpi_handle_v1) == 8U);
static_assert(sizeof(fsim_vhpi_error_view_v1) == 32U);
static_assert(sizeof(fsim_vhpi_host_v1) == 40U);
static_assert(sizeof(fsim_vhpi_service_request_v1) == 48U);
static_assert(sizeof(fsim_vhpi_service_result_v1) == 40U);
static_assert(sizeof(fsim_vhpi_host_v2) == 56U);
static_assert(sizeof(fsim_vhpi_capabilities_v3) == 32U);
static_assert(sizeof(fsim_vhpi_value_v3) == 48U);
static_assert(sizeof(fsim_vhpi_tool_request_v3) == 32U);
static_assert(sizeof(fsim_vhpi_host_v3) == 80U);
static_assert(sizeof(fsim_vhpi_plugin_v1) == 48U);
using VhpiReport = void(FSIM_VHPI_CALL*)(
    void*, const fsim_vhpi_error_view_v1*);
using VhpiInvoke = fsim_vhpi_status_v1(FSIM_VHPI_CALL*)(
    void*, const fsim_vhpi_service_request_v1*, fsim_vhpi_service_result_v1*);
using VhpiLifecycle = fsim_vhpi_status_v1(FSIM_VHPI_CALL*)(void*);
using VhpiCapability = fsim_vhpi_status_v1(FSIM_VHPI_CALL*)(
    void*, fsim_vhpi_capabilities_v3*);
using VhpiValueAccess = fsim_vhpi_status_v1(FSIM_VHPI_CALL*)(
    void*, fsim_vhpi_value_access_v3, fsim_vhpi_value_v3*);
using VhpiToolExecution = fsim_vhpi_status_v1(FSIM_VHPI_CALL*)(
    void*, const fsim_vhpi_tool_request_v3*);
using VhpiBind = fsim_vhpi_status_v1(FSIM_VHPI_CALL*)(
    const fsim_vhpi_host_v1*, fsim_vhpi_plugin_v1*);
static_assert(std::is_same_v<fsim_vhpi_report_v1, VhpiReport>);
static_assert(std::is_same_v<fsim_vhpi_invoke_service_v1, VhpiInvoke>);
static_assert(std::is_same_v<fsim_vhpi_plugin_lifecycle_v1, VhpiLifecycle>);
static_assert(std::is_same_v<fsim_vhpi_query_capabilities_v3, VhpiCapability>);
static_assert(std::is_same_v<fsim_vhpi_access_value_v3, VhpiValueAccess>);
static_assert(std::is_same_v<fsim_vhpi_execute_tool_v3, VhpiToolExecution>);
static_assert(std::is_same_v<fsim_vhpi_plugin_bind_v1_fn, VhpiBind>);

static_assert(sizeof(fsim_uvm_foreign_snapshot_v1) == 48U);
static_assert(sizeof(fsim_uvm_foreign_record_v1) == 72U);
static_assert(sizeof(fsim_uvm_foreign_activity_v1) == 88U);
static_assert(sizeof(fsim_uvm_foreign_host_v1) == 64U);
using UvmActivity = fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL*)(
    void*, const fsim_uvm_foreign_activity_v1*);
using UvmCapture = fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL*)(
    void*, fsim_uvm_foreign_snapshot_v1*);
using UvmRelease = fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL*)(
    void*, const fsim_uvm_foreign_snapshot_v1*);
static_assert(
    std::is_same_v<fsim_uvm_foreign_activity_callback_v1, UvmActivity>);
static_assert(std::is_same_v<fsim_uvm_foreign_capture_v1, UvmCapture>);
static_assert(std::is_same_v<fsim_uvm_foreign_release_v1, UvmRelease>);

} // namespace

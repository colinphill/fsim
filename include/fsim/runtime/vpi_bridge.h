// SPDX-License-Identifier: Apache-2.0
#ifndef FSIM_RUNTIME_VPI_BRIDGE_H
#define FSIM_RUNTIME_VPI_BRIDGE_H

#include "fsim/runtime/vpi_abi.h"

#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#define FSIM_VPI_BRIDGE_CALL __cdecl
#if defined(FSIM_VPI_LINK_SURFACE_BUILD)
#define FSIM_VPI_BRIDGE_API __declspec(dllexport)
#else
#define FSIM_VPI_BRIDGE_API __declspec(dllimport)
#endif
#else
#define FSIM_VPI_BRIDGE_CALL
#define FSIM_VPI_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FSIM_VPI_CONTEXT_ABI_VERSION 1u

typedef fsim_vpi_status_v1(FSIM_VPI_BRIDGE_CALL *fsim_vpi_standard_invoke_v1)(
    void *user_data,
    uint32_t routine,
    const fsim_vpi_service_request_v1 *request,
    fsim_vpi_service_result_v1 *result);

typedef struct fsim_vpi_call_context_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  void *user_data;
  fsim_vpi_standard_invoke_v1 invoke;
} fsim_vpi_call_context_v1;

FSIM_VPI_BRIDGE_API int FSIM_VPI_BRIDGE_CALL
fsim_vpi_call_context_enter_v1(fsim_vpi_call_context_v1 *context);
FSIM_VPI_BRIDGE_API int FSIM_VPI_BRIDGE_CALL
fsim_vpi_call_context_leave_v1(fsim_vpi_call_context_v1 *context);
FSIM_VPI_BRIDGE_API const fsim_vpi_call_context_v1 *FSIM_VPI_BRIDGE_CALL
fsim_vpi_current_call_context_v1(void);

#ifdef __cplusplus
}
#endif

#endif

// SPDX-License-Identifier: Apache-2.0
#ifndef FSIM_RUNTIME_SVDPI_BRIDGE_H
#define FSIM_RUNTIME_SVDPI_BRIDGE_H

#include "svdpi.h"

#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#define FSIM_SVDPI_CALL __cdecl
#if defined(FSIM_SVDPI_LINK_SURFACE_BUILD)
#define FSIM_SVDPI_BRIDGE_API __declspec(dllexport)
#else
#define FSIM_SVDPI_BRIDGE_API __declspec(dllimport)
#endif
#else
#define FSIM_SVDPI_CALL
#define FSIM_SVDPI_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FSIM_SVDPI_CONTEXT_ABI_VERSION 3u

typedef svScope(FSIM_SVDPI_CALL* fsim_svdpi_get_scope_v3)(void* user_data);
typedef svScope(FSIM_SVDPI_CALL* fsim_svdpi_set_scope_v3)(
    void* user_data, svScope scope);
typedef const char*(FSIM_SVDPI_CALL* fsim_svdpi_scope_name_v3)(
    void* user_data, svScope scope);
typedef svScope(FSIM_SVDPI_CALL* fsim_svdpi_find_scope_v3)(
    void* user_data, const char* name);
typedef int(FSIM_SVDPI_CALL* fsim_svdpi_put_user_data_v3)(
    void* user_data, svScope scope, void* key, void* value);
typedef void*(FSIM_SVDPI_CALL* fsim_svdpi_get_user_data_v3)(
    void* user_data, svScope scope, void* key);
typedef int(FSIM_SVDPI_CALL* fsim_svdpi_caller_info_v3)(
    void* user_data, const char** file_name, int* line_number);
typedef int(FSIM_SVDPI_CALL* fsim_svdpi_disabled_v3)(void* user_data);
typedef void(FSIM_SVDPI_CALL* fsim_svdpi_ack_disabled_v3)(void* user_data);
typedef int(FSIM_SVDPI_CALL* fsim_svdpi_time_v3)(
    void* user_data, svScope scope, svTimeVal* time_value);
typedef int(FSIM_SVDPI_CALL* fsim_svdpi_time_scale_v3)(
    void* user_data, svScope scope, int32_t* exponent);

typedef struct fsim_svdpi_call_context_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  void* user_data;
  fsim_svdpi_get_scope_v3 get_scope;
  fsim_svdpi_set_scope_v3 set_scope;
  fsim_svdpi_scope_name_v3 get_name_from_scope;
  fsim_svdpi_find_scope_v3 get_scope_from_name;
  fsim_svdpi_put_user_data_v3 put_user_data;
  fsim_svdpi_get_user_data_v3 get_user_data;
  fsim_svdpi_caller_info_v3 get_caller_info;
  fsim_svdpi_disabled_v3 is_disabled_state;
  fsim_svdpi_ack_disabled_v3 acknowledge_disabled_state;
  fsim_svdpi_time_v3 get_time;
  fsim_svdpi_time_scale_v3 get_time_unit;
  fsim_svdpi_time_scale_v3 get_time_precision;
} fsim_svdpi_call_context_v3;

FSIM_SVDPI_BRIDGE_API int FSIM_SVDPI_CALL
fsim_svdpi_call_context_enter_v3(fsim_svdpi_call_context_v3* context);
FSIM_SVDPI_BRIDGE_API int FSIM_SVDPI_CALL
fsim_svdpi_call_context_leave_v3(fsim_svdpi_call_context_v3* context);
FSIM_SVDPI_BRIDGE_API const fsim_svdpi_call_context_v3* FSIM_SVDPI_CALL
fsim_svdpi_current_call_context_v3(void);

#ifdef __cplusplus
}
#endif

#endif

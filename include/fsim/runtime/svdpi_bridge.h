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

/* The svOpenArrayHandle the simulator passes for an open-array formal
 * (IEEE 1800-2023 35.5.6.1, H.12): `data` holds every element in the
 * canonical C representation of the element type, leftmost element first
 * (row-major over the unpacked dimensions), `element_bytes` apart.
 * `bounds` holds each unpacked dimension's left and right index, dimension 1
 * first; dimension 0 is the packed part. `element_code` is the element's DPI
 * type code: b svBit, g svLogic (one byte each), c s i l and C S I L the C
 * integer types, B an svBitVecVal and V an svLogicVecVal array. */
#define FSIM_SVDPI_OPEN_ARRAY_MAGIC 0x6f70656eu

typedef struct fsim_svdpi_open_array_v3 {
  uint32_t magic;
  uint32_t struct_size;
  int32_t dimensions;
  int32_t packed_left;
  int32_t packed_right;
  uint32_t element_code;
  uint32_t element_width;
  uint32_t element_bytes;
  void* data;
  const int32_t* bounds;
} fsim_svdpi_open_array_v3;

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

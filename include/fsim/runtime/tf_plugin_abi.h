// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/veriuser.h"

#define FSIM_TF_INTERFACE_ABI_VERSION 3u
#define FSIM_TF_REGISTRATION_TABLE_ABI_VERSION 3u
#define FSIM_TF_MAX_REGISTRATIONS 4096u
#define FSIM_TF_MAX_REGISTRATION_STRIDE 4096u
#define FSIM_TF_MAX_REGISTRATION_NAME_SIZE 255u

#define FSIM_TF_REGISTRATION_TASK 1u
#define FSIM_TF_REGISTRATION_FUNCTION 2u
#define FSIM_TF_REGISTRATION_REAL_FUNCTION 3u

#ifdef __cplusplus
extern "C" {
#endif

typedef PLI_INT32(FSIM_NATIVE_PLUGIN_CALL *fsim_tf_routine_v3)(
    PLI_INT32 user_data, PLI_INT32 reason);
typedef PLI_INT32(FSIM_NATIVE_PLUGIN_CALL *fsim_tf_misc_routine_v3)(
    PLI_INT32 user_data, PLI_INT32 reason, PLI_INT32 parameter);

typedef struct fsim_tf_registration_v3 {
  uint32_t struct_size;
  uint32_t kind;
  PLI_INT32 user_data;
  uint32_t flags;
  fsim_tf_routine_v3 checktf;
  fsim_tf_routine_v3 sizetf;
  fsim_tf_routine_v3 calltf;
  fsim_tf_misc_routine_v3 misctf;
  uint32_t name_size;
  uint32_t reserved;
  const PLI_BYTE8* name;
} fsim_tf_registration_v3;

typedef struct fsim_tf_registration_table_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t flags;
  uint32_t entry_count;
  uint32_t entry_stride;
  uint32_t reserved;
  const void* entries;
} fsim_tf_registration_table_v3;

#ifdef __cplusplus
}
#endif

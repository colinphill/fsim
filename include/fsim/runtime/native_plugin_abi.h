// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FSIM_NATIVE_PLUGIN_ABI_VERSION 3u
#define FSIM_NATIVE_PLUGIN_DESCRIPTOR_SYMBOL \
  "fsim_native_plugin_descriptor_v3_get"

#define FSIM_NATIVE_PLUGIN_MAX_NAME_SIZE 255u
#define FSIM_NATIVE_PLUGIN_MAX_VERSION_SIZE 63u
#define FSIM_NATIVE_PLUGIN_MAX_PRODUCER_SIZE 255u
#define FSIM_NATIVE_PLUGIN_MAX_BUILD_ID_SIZE 128u
#define FSIM_NATIVE_PLUGIN_MAX_INTERFACES 8u

#define FSIM_NATIVE_PLUGIN_CAPABILITY_TF UINT64_C(0x0000000000000001)
#define FSIM_NATIVE_PLUGIN_CAPABILITY_ACC UINT64_C(0x0000000000000002)
#define FSIM_NATIVE_PLUGIN_KNOWN_CAPABILITIES \
  (FSIM_NATIVE_PLUGIN_CAPABILITY_TF | FSIM_NATIVE_PLUGIN_CAPABILITY_ACC)

#if defined(_WIN32)
#define FSIM_NATIVE_PLUGIN_EXPORT __declspec(dllexport)
#define FSIM_NATIVE_PLUGIN_CALL __cdecl
#else
#define FSIM_NATIVE_PLUGIN_EXPORT __attribute__((visibility("default")))
#define FSIM_NATIVE_PLUGIN_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The v3 descriptor is an append-only loader contract. A consumer validates
 * abi_version and struct_size before reading any field. flags is reserved and
 * must be zero. Text fields are byte-counted, are never interpreted as paths,
 * and remain owned by the loaded image; the host copies them before it can
 * publish or unload the plug-in.
 */
typedef struct fsim_native_plugin_descriptor_v3 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t pointer_bits;
  uint32_t flags;
  uint64_t capabilities;
  uint32_t name_size;
  uint32_t version_size;
  uint32_t producer_size;
  uint32_t build_id_size;
  const char* name;
  const char* version;
  const char* producer;
  const char* build_id;
  uint32_t interface_count;
  uint32_t interface_stride;
  const struct fsim_native_plugin_interface_v3* interfaces;
} fsim_native_plugin_descriptor_v3;

typedef struct fsim_native_plugin_interface_v3 {
  uint64_t capability;
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t flags;
  uint32_t descriptor_size;
  const void* descriptor;
} fsim_native_plugin_interface_v3;

#define FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_BASE_SIZE \
  offsetof(fsim_native_plugin_descriptor_v3, interface_count)
#define FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_INTERFACES_SIZE \
  sizeof(fsim_native_plugin_descriptor_v3)

typedef const fsim_native_plugin_descriptor_v3*
    (FSIM_NATIVE_PLUGIN_CALL *fsim_native_plugin_descriptor_v3_get_fn)(void);

#ifdef __cplusplus
}
#endif

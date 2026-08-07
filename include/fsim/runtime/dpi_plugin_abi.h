// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#define FSIM_DPI_PLUGIN_ABI_VERSION 1u
#define FSIM_DPI_PLUGIN_DESCRIPTOR_SYMBOL \
  "fsim_dpi_plugin_descriptor_v1_get"

#if defined(_WIN32)
#define FSIM_DPI_PLUGIN_EXPORT __declspec(dllexport)
#define FSIM_DPI_PLUGIN_CALL __cdecl
#else
#define FSIM_DPI_PLUGIN_EXPORT __attribute__((visibility("default")))
#define FSIM_DPI_PLUGIN_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fsim_dpi_plugin_descriptor_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t pointer_bits;
  uint32_t flags;
  uint32_t name_size;
  const char* name;
} fsim_dpi_plugin_descriptor_v1;

typedef const fsim_dpi_plugin_descriptor_v1*
    (FSIM_DPI_PLUGIN_CALL *fsim_dpi_plugin_descriptor_v1_get_fn)(void);

#ifdef __cplusplus
}
#endif

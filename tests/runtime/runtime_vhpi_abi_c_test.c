// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_abi.h"

#include <stddef.h>

_Static_assert(sizeof(fsim_vhpi_handle_v1) == 8, "VHPI handles are 64-bit");
_Static_assert(
    offsetof(fsim_vhpi_host_v1, simulation_identity) == 16,
    "VHPI host simulation identity offset");
_Static_assert(
    offsetof(fsim_vhpi_host_v1, context) == 24,
    "VHPI host context offset");
_Static_assert(
    offsetof(fsim_vhpi_host_v1, report) == 32,
    "VHPI host report offset");
_Static_assert(sizeof(fsim_vhpi_host_v1) == 40, "VHPI host v1 size");
_Static_assert(sizeof(fsim_vhpi_plugin_v1) == 48, "VHPI plug-in v1 size");

size_t fsim_vhpi_abi_c_host_size(void) { return sizeof(fsim_vhpi_host_v1); }

const char* fsim_vhpi_abi_c_bind_symbol(void) {
  return FSIM_VHPI_PLUGIN_BIND_SYMBOL;
}

void fsim_vhpi_abi_c_report(const fsim_vhpi_host_v1* host) {
  static const char code[] = "FSIM-VHPI-ABI-TEST";
  static const char message[] = "C translation unit callback";
  const fsim_vhpi_error_view_v1 error = {
      FSIM_VHPI_ERROR_NOTE,
      (uint32_t)(sizeof(code) - 1),
      code,
      (uint32_t)(sizeof(message) - 1),
      message,
  };
  host->report(host->context, &error);
}

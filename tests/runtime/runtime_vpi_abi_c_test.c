// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_abi.h"

#include <stddef.h>

_Static_assert(sizeof(fsim_vpi_handle_v1) == 8, "VPI handles are 64-bit");
_Static_assert(
    offsetof(fsim_vpi_host_v1, simulation_identity) == 16,
    "VPI host simulation identity offset");
_Static_assert(
    offsetof(fsim_vpi_host_v1, context) == 24,
    "VPI host context offset");
_Static_assert(
    offsetof(fsim_vpi_host_v1, report) == 32,
    "VPI host report offset");
_Static_assert(sizeof(fsim_vpi_host_v1) == 40, "VPI host v1 size");
_Static_assert(sizeof(fsim_vpi_plugin_v1) == 48, "VPI plug-in v1 size");

size_t fsim_vpi_abi_c_host_size(void) { return sizeof(fsim_vpi_host_v1); }

void fsim_vpi_abi_c_report(const fsim_vpi_host_v1* host) {
  static const char code[] = "FSIM-VPI-ABI-TEST";
  static const char message[] = "C translation unit callback";
  const fsim_vpi_error_view_v1 error = {
      FSIM_VPI_ERROR_NOTICE,
      (uint32_t)(sizeof(code) - 1),
      code,
      (uint32_t)(sizeof(message) - 1),
      message,
  };
  host->report(host->context, &error);
}

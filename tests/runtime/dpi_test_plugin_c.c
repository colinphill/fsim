// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_plugin_abi.h"

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_c_mix(
    const int left, const int right) {
  return left * 3 + right;
}

// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_plugin_abi.h"

#define FSIM_DPI_TEST_EXPORT extern "C" FSIM_DPI_PLUGIN_EXPORT

namespace {

constexpr char plugin_name[] = "arith_dpi";
constexpr fsim_dpi_plugin_descriptor_v1 descriptor{
    FSIM_DPI_PLUGIN_ABI_VERSION,
    sizeof(fsim_dpi_plugin_descriptor_v1),
    sizeof(void*) * 8U,
    0,
    sizeof(plugin_name) - 1U,
    plugin_name};

}  // namespace

FSIM_DPI_TEST_EXPORT const fsim_dpi_plugin_descriptor_v1*
FSIM_DPI_PLUGIN_CALL fsim_dpi_plugin_descriptor_v1_get() {
  return &descriptor;
}

FSIM_DPI_TEST_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_add(
    const int left, const int right) {
  return left + right;
}

FSIM_DPI_TEST_EXPORT void FSIM_DPI_PLUGIN_CALL sv_report(const int) {}

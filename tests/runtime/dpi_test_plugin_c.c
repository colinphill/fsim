// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_plugin_abi.h"
#include "svdpi.h"

#include <string.h>

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_c_mix(
    const int left, const int right) {
  return left * 3 + right;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_c_context_probe(void) {
  static int user_key;
  static int user_value = 42;
  svScope active = svGetScope();
  svScope root = svGetScopeFromName("top");
  const char* caller_file = 0;
  int caller_line = 0;
  int32_t unit = 0;
  int32_t precision = 0;
  svTimeVal time = {sv_sim_time, 0u, 0u, 0.0};
  return active != 0 && root != 0
      && strcmp(svGetNameFromScope(active), "top.u_dpi") == 0
      && svPutUserData(active, &user_key, &user_value) == 0
      && svGetUserData(active, &user_key) == &user_value
      && svGetCallerInfo(&caller_file, &caller_line) == 1
      && strcmp(caller_file, "dpi-plugin-context.sv") == 0
      && caller_line == 81
      && svGetTime(active, &time) == 0
      && time.high == 1u && time.low == 2u
      && svGetTimeUnit(active, &unit) == 0 && unit == -9
      && svGetTimePrecision(active, &precision) == 0 && precision == -12
      && svIsDisabledState() == 0;
}

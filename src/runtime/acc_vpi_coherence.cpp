// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"

#include <cstdint>

extern "C" {

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL fsim_acc_vpi_same_object_v3(
    const handle object, const std::uint64_t vpi_handle) {
  const auto mapped = fsim_acc_handle_to_vpi_v3(object);
  if (mapped == 0 || vpi_handle == 0 || mapped != vpi_handle) {
    acc_error_flag = 1;
    return 0;
  }
  acc_error_flag = 0;
  return 1;
}

}  // extern "C"

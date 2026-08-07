// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_abi.h"

#include <stdexcept>

namespace {

int mode;
int startup_count;
int shutdown_count;

fsim_vpi_status_v1 FSIM_VPI_CALL startup(void*) {
  ++startup_count;
  if (mode == 8) {
    throw std::runtime_error("intentional VPI startup exception");
  }
  return mode == 7 ? FSIM_VPI_STATUS_INTERNAL_ERROR : FSIM_VPI_STATUS_OK;
}

fsim_vpi_status_v1 FSIM_VPI_CALL shutdown(void*) {
  ++shutdown_count;
  if (mode == 10) {
    throw std::runtime_error("intentional VPI shutdown exception");
  }
  return mode == 9 ? FSIM_VPI_STATUS_INTERNAL_ERROR : FSIM_VPI_STATUS_OK;
}

}  // namespace

extern "C" FSIM_VPI_EXPORT void FSIM_VPI_CALL
fsim_vpi_test_reset(const int selected_mode) {
  mode = selected_mode;
  startup_count = 0;
  shutdown_count = 0;
}

extern "C" FSIM_VPI_EXPORT int FSIM_VPI_CALL
fsim_vpi_test_startup_count() {
  return startup_count;
}

extern "C" FSIM_VPI_EXPORT int FSIM_VPI_CALL
fsim_vpi_test_shutdown_count() {
  return shutdown_count;
}

extern "C" FSIM_VPI_EXPORT fsim_vpi_status_v1 FSIM_VPI_CALL
fsim_vpi_plugin_bind_v1(
    const fsim_vpi_host_v1* const host,
    fsim_vpi_plugin_v1* const plugin) {
  if (mode == 1) {
    return FSIM_VPI_STATUS_INVALID_ARGUMENT;
  }
  if (mode == 2) {
    throw std::runtime_error("intentional VPI bind exception");
  }
  static const char name[] = "fsim-vpi-test";
  *plugin = {
      FSIM_VPI_PLUGIN_ABI_VERSION,
      sizeof(fsim_vpi_plugin_v1),
      0,
      static_cast<uint32_t>(sizeof(name) - 1),
      name,
      const_cast<fsim_vpi_host_v1*>(host),
      startup,
      shutdown,
  };
  if (mode == 3) {
    ++plugin->abi_version;
  } else if (mode == 4) {
    --plugin->struct_size;
  } else if (mode == 5) {
    plugin->flags = 1;
  } else if (mode == 6) {
    plugin->name = nullptr;
  } else if (mode == 11) {
    plugin->startup = nullptr;
  }
  return FSIM_VPI_STATUS_OK;
}

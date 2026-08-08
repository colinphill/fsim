// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_abi.h"

#include <cstdint>
#include <stdexcept>

namespace {

int mode;
int startup_count;
int shutdown_count;

fsim_vhpi_status_v1 FSIM_VHPI_CALL startup(void*) {
  ++startup_count;
  if (mode == 8) {
    throw std::runtime_error("intentional VHPI startup exception");
  }
  return mode == 7 ? FSIM_VHPI_STATUS_INTERNAL_ERROR
                   : FSIM_VHPI_STATUS_OK;
}

fsim_vhpi_status_v1 FSIM_VHPI_CALL shutdown(void*) {
  ++shutdown_count;
  if (mode == 10) {
    throw std::runtime_error("intentional VHPI shutdown exception");
  }
  return mode == 9 ? FSIM_VHPI_STATUS_INTERNAL_ERROR
                   : FSIM_VHPI_STATUS_OK;
}

}  // namespace

extern "C" FSIM_VHPI_EXPORT void FSIM_VHPI_CALL fsim_vhpi_test_reset(
    const int selected_mode) {
  mode = selected_mode;
  startup_count = 0;
  shutdown_count = 0;
}

extern "C" FSIM_VHPI_EXPORT int FSIM_VHPI_CALL
fsim_vhpi_test_startup_count() {
  return startup_count;
}

extern "C" FSIM_VHPI_EXPORT int FSIM_VHPI_CALL
fsim_vhpi_test_shutdown_count() {
  return shutdown_count;
}

extern "C" FSIM_VHPI_EXPORT fsim_vhpi_status_v1 FSIM_VHPI_CALL
fsim_vhpi_plugin_bind_v1(
    const fsim_vhpi_host_v1* const host,
    fsim_vhpi_plugin_v1* const plugin) noexcept(false) {
  if (mode == 1) {
    return FSIM_VHPI_STATUS_INVALID_ARGUMENT;
  }
  if (mode == 2) {
    throw std::runtime_error("intentional VHPI bind exception");
  }
  static const char name[] = "fsim-vhpi-test";
  *plugin = {
      FSIM_VHPI_PLUGIN_ABI_VERSION,
      sizeof(fsim_vhpi_plugin_v1),
      0,
      static_cast<std::uint32_t>(sizeof(name) - 1),
      name,
      const_cast<fsim_vhpi_host_v1*>(host),
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
  return FSIM_VHPI_STATUS_OK;
}

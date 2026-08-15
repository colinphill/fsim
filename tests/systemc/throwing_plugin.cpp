// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

#include <stdexcept>

namespace {

fsim_sc_status_v1 elaborate_module(
    void*, const char*, fsim_sc_handle_v1, fsim_sc_handle_v1, void** result) {
    *result = reinterpret_cast<void*>(0x2);
    return FSIM_SC_OK;
}

void destroy_module(void*, void*) {}

[[noreturn]] void fail_initialization() {
    throw std::runtime_error{"intentional plug-in failure"};
}

} // namespace

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1*, fsim_sc_registrar_v1* registrar) {
    if (registrar == nullptr
        || registrar->register_elaboration_factory == nullptr
        || registrar->register_elaboration_factory(
               registrar->context,
               "must-not-escape",
               elaborate_module,
               destroy_module,
               nullptr)
            != FSIM_SC_OK) {
        return FSIM_SC_RUNTIME_ERROR;
    }
    fail_initialization();
}

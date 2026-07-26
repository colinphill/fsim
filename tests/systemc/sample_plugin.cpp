// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

namespace {

void* create_module(void*, const char*, fsim_sc_handle_v1) {
    return reinterpret_cast<void*>(0x1);
}

void destroy_module(void*, void*) {}

} // namespace

extern "C" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host, fsim_sc_registrar_v1* registrar) {
    if (host == nullptr || registrar == nullptr
        || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host->struct_size != sizeof(fsim_sc_host_v1)
        || registrar->struct_size != sizeof(fsim_sc_registrar_v1)
        || registrar->register_factory == nullptr) {
        return FSIM_SC_ABI_MISMATCH;
    }
    return registrar->register_factory(
        registrar->context, "sample", create_module, destroy_module, nullptr);
}

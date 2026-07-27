// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

namespace {

const fsim_sc_host_v1* active_host = nullptr;

void* create_module(void*, const char*, fsim_sc_handle_v1) {
    return reinterpret_cast<void*>(0x1);
}

void destroy_module(void*, void*) {}

fsim_sc_status_v1 elaborate_bridge(
    void*,
    const char* instance_name,
    const fsim_sc_handle_v1 module,
    fsim_sc_handle_v1,
    void** result) {
    if (active_host == nullptr || instance_name == nullptr
        || *instance_name == '\0' || result == nullptr
        || active_host->register_port == nullptr
        || active_host->register_foreign_child == nullptr
        || active_host->connect_foreign_port == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    fsim_sc_handle_v1 clock = 0;
    fsim_sc_handle_v1 value = 0;
    fsim_sc_handle_v1 child = 0;
    auto status = active_host->register_port(
        active_host->context,
        module,
        "clock",
        FSIM_SC_INPUT,
        FSIM_SC_BIT2,
        1,
        &clock);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = active_host->register_port(
        active_host->context,
        module,
        "value",
        FSIM_SC_OUTPUT,
        FSIM_SC_UNSIGNED,
        8,
        &value);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = active_host->register_foreign_child(
        active_host->context, module, "u_hdl", &child);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = active_host->connect_foreign_port(
        active_host->context,
        child,
        "clock",
        FSIM_SC_INPUT,
        FSIM_SC_BIT2,
        1,
        clock);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = active_host->connect_foreign_port(
        active_host->context,
        child,
        "value",
        FSIM_SC_OUTPUT,
        FSIM_SC_UNSIGNED,
        8,
        value);
    if (status != FSIM_SC_OK) {
        return status;
    }
    *result = new int{42};
    return FSIM_SC_OK;
}

void destroy_bridge(void*, void* module) {
    delete static_cast<int*>(module);
}

} // namespace

extern "C" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host, fsim_sc_registrar_v1* registrar) {
    if (host == nullptr || registrar == nullptr
        || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host->struct_size < sizeof(fsim_sc_host_v1)
        || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
        || registrar->register_factory == nullptr
        || registrar->register_elaboration_factory == nullptr) {
        return FSIM_SC_ABI_MISMATCH;
    }
    active_host = host;
    auto status = registrar->register_factory(
        registrar->context, "sample", create_module, destroy_module, nullptr);
    if (status != FSIM_SC_OK) {
        return status;
    }
    return registrar->register_elaboration_factory(
        registrar->context,
        "bridge",
        elaborate_bridge,
        destroy_bridge,
        nullptr);
}

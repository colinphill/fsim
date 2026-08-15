// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

#include <new>
#include <stdexcept>

namespace {

const fsim_sc_host_v1* retained_host{};

void destroy_nothing(void*, void*) {}

fsim_sc_status_v1 elaborate_status_failure(
    void*,
    const char*,
    const fsim_sc_handle_v1 module,
    fsim_sc_handle_v1,
    void**) {
    fsim_sc_handle_v1 port = 0;
    if (retained_host != nullptr && retained_host->register_port != nullptr) {
        (void)retained_host->register_port(
            retained_host->context,
            module,
            "rolled_back",
            FSIM_SC_INPUT,
            FSIM_SC_BIT2,
            1,
            &port);
    }
    return FSIM_SC_RUNTIME_ERROR;
}

fsim_sc_status_v1 elaborate_null(
    void*,
    const char*,
    fsim_sc_handle_v1,
    fsim_sc_handle_v1,
    void** result) {
    if (result != nullptr) {
        *result = nullptr;
    }
    return FSIM_SC_OK;
}

fsim_sc_status_v1 elaborate_throw(
    void*,
    const char*,
    fsim_sc_handle_v1,
    fsim_sc_handle_v1,
    void**) {
    throw std::runtime_error{"intentional construction exception"};
}

fsim_sc_status_v1 elaborate_destroy_throw(
    void*,
    const char*,
    fsim_sc_handle_v1,
    fsim_sc_handle_v1,
    void** result) {
    if (result == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    *result = new (std::nothrow) int{1};
    return *result == nullptr ? FSIM_SC_RUNTIME_ERROR : FSIM_SC_OK;
}

void destroy_throw(void*, void* object) {
    delete static_cast<int*>(object);
    throw std::runtime_error{"intentional destruction exception"};
}

struct ProcessState {};

void process_throw(void*) {
    throw std::runtime_error{"intentional process exception"};
}

fsim_sc_status_v1 elaborate_process_throw(
    void*,
    const char*,
    const fsim_sc_handle_v1 module,
    fsim_sc_handle_v1,
    void** result) {
    if (result == nullptr || retained_host == nullptr
        || retained_host->register_process == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    auto* state = new (std::nothrow) ProcessState;
    if (state == nullptr) {
        return FSIM_SC_RUNTIME_ERROR;
    }
    fsim_sc_handle_v1 process = 0;
    const auto status = retained_host->register_process(
        retained_host->context,
        module,
        "throwing_method",
        process_throw,
        state,
        &process);
    if (status != FSIM_SC_OK) {
        delete state;
        return status;
    }
    *result = state;
    return FSIM_SC_OK;
}

void destroy_process(void*, void* object) {
    delete static_cast<ProcessState*>(object);
}

} // namespace

extern "C" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
    if (host == nullptr || registrar == nullptr
        || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->register_elaboration_factory == nullptr) {
        return FSIM_SC_ABI_MISMATCH;
    }
    retained_host = host;
    struct Factory {
        const char* name;
        fsim_sc_module_elaborate_v1 elaborate;
        fsim_sc_module_destroy_v1 destroy;
    };
    const Factory factories[]{
        {"status_failure", elaborate_status_failure, destroy_nothing},
        {"null_failure", elaborate_null, destroy_nothing},
        {"throw_failure", elaborate_throw, destroy_nothing},
        {"destroy_throw", elaborate_destroy_throw, destroy_throw},
        {"process_throw", elaborate_process_throw, destroy_process},
    };
    for (const auto& factory : factories) {
        const auto status = registrar->register_elaboration_factory(
            registrar->context,
            factory.name,
            factory.elaborate,
            factory.destroy,
            nullptr);
        if (status != FSIM_SC_OK) {
            return status;
        }
    }
    return FSIM_SC_OK;
}

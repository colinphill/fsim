// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

#include <array>
#include <cstdint>
#include <limits>
#include <new>

namespace {

enum LifecycleEvent : std::uint32_t {
    constructed = 1,
    before_elaboration = 2,
    elaborated = 3,
    started = 4,
    fiber_entered = 5,
    fiber_stopped = 6,
    ended = 7,
    destroyed = 8,
};

std::array<std::uint32_t, 128> lifecycle_events{};
std::size_t lifecycle_event_count{};
bool ignore_duplicate_registration{};

void record(const LifecycleEvent event) noexcept {
    if (lifecycle_event_count < lifecycle_events.size()) {
        lifecycle_events[lifecycle_event_count++] = event;
    }
}

void before_end_of_elaboration(void*) {
    record(before_elaboration);
}

void end_of_elaboration(void*) {
    record(elaborated);
}

void start_of_simulation(void*) {
    record(started);
}

void end_of_simulation(void*) {
    record(ended);
}

struct BridgeState {
    const fsim_sc_host_v1* host{};
};

struct FiberState {
    const fsim_sc_host_v1* host{};
};

void* create_module(void*, const char*, fsim_sc_handle_v1) {
    return reinterpret_cast<void*>(0x1);
}

void destroy_module(void*, void*) {}

void sample_method(void*) {}

fsim_sc_status_v1 elaborate_bridge(
    void* user,
    const char* instance_name,
    const fsim_sc_handle_v1 module,
    fsim_sc_handle_v1,
    void** result) {
    const auto* host =
        static_cast<const fsim_sc_host_v1*>(user);
    if (host == nullptr || instance_name == nullptr
        || *instance_name == '\0' || result == nullptr
        || host->register_port == nullptr
        || host->register_process == nullptr
        || host->add_sensitivity == nullptr
        || host->register_foreign_child == nullptr
        || host->connect_foreign_port == nullptr
        || host->set_foreign_child_actual == nullptr
        || host->get_construction_value == nullptr
        || host->set_process_initialize == nullptr
        || host->register_lifecycle == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    fsim_sc_handle_v1 clock = 0;
    fsim_sc_handle_v1 value = 0;
    fsim_sc_handle_v1 child = 0;
    fsim_sc_handle_v1 process = 0;
    std::int64_t width = 0;
    auto status = host->get_construction_value(
        host->context, module, "WIDTH", &width);
    if (status != FSIM_SC_OK || width <= 0
        || width
            > std::numeric_limits<std::uint32_t>::max()) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    status = host->register_port(
        host->context,
        module,
        "clock",
        FSIM_SC_INPUT,
        FSIM_SC_BIT2,
        1,
        &clock);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->register_port(
        host->context,
        module,
        "value",
        FSIM_SC_OUTPUT,
        FSIM_SC_UNSIGNED,
        static_cast<std::uint32_t>(width),
        &value);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->register_process(
        host->context,
        module,
        "evaluate",
        FSIM_SC_METHOD,
        sample_method,
        nullptr,
        &process);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->add_sensitivity(
        host->context,
        process,
        clock,
        FSIM_SC_POSEDGE);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->set_process_initialize(
        host->context, process, 0);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->register_foreign_child(
        host->context, module, "u_hdl", &child);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->set_foreign_child_actual(
        host->context, child, "WIDTH", 8);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->connect_foreign_port(
        host->context,
        child,
        "clock",
        FSIM_SC_INPUT,
        FSIM_SC_BIT2,
        1,
        clock);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = host->connect_foreign_port(
        host->context,
        child,
        "value",
        FSIM_SC_OUTPUT,
        FSIM_SC_UNSIGNED,
        static_cast<std::uint32_t>(width),
        value);
    if (status != FSIM_SC_OK) {
        return status;
    }
    auto* state = new (std::nothrow) BridgeState{host};
    if (state == nullptr) {
        return FSIM_SC_RUNTIME_ERROR;
    }
    status = host->register_lifecycle(
        host->context,
        module,
        before_end_of_elaboration,
        end_of_elaboration,
        start_of_simulation,
        end_of_simulation,
        state);
    if (status != FSIM_SC_OK) {
        delete state;
        return status;
    }
    record(constructed);
    *result = state;
    return FSIM_SC_OK;
}

void destroy_bridge(void*, void* module) {
    record(destroyed);
    delete static_cast<BridgeState*>(module);
}

void fiber_entry(void* user) {
    auto* state = static_cast<FiberState*>(user);
    record(fiber_entered);
    if (state == nullptr || state->host == nullptr
        || state->host->wait_time == nullptr) {
        record(fiber_stopped);
        return;
    }
    bool stopping = false;
    for (std::size_t wait = 0; wait < 3; ++wait) {
        stopping =
            state->host->wait_time(state->host->context, 5)
                != FSIM_SC_OK
            || stopping;
    }
    if (stopping) {
        record(fiber_stopped);
    }
}

fsim_sc_status_v1 elaborate_fiber_bridge(
    void* user,
    const char* instance_name,
    const fsim_sc_handle_v1 module,
    fsim_sc_handle_v1,
    void** result) {
    const auto* host = static_cast<const fsim_sc_host_v1*>(user);
    if (host == nullptr || instance_name == nullptr
        || *instance_name == '\0' || result == nullptr
        || host->register_process == nullptr
        || host->register_lifecycle == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    auto* state = new (std::nothrow) FiberState{host};
    if (state == nullptr) {
        return FSIM_SC_RUNTIME_ERROR;
    }
    fsim_sc_handle_v1 process = 0;
    auto status = host->register_process(
        host->context,
        module,
        "suspended",
        FSIM_SC_THREAD,
        fiber_entry,
        state,
        &process);
    if (status == FSIM_SC_OK) {
        status = host->register_lifecycle(
            host->context,
            module,
            before_end_of_elaboration,
            end_of_elaboration,
            start_of_simulation,
            end_of_simulation,
            state);
    }
    if (status != FSIM_SC_OK) {
        delete state;
        return status;
    }
    record(constructed);
    *result = state;
    return FSIM_SC_OK;
}

void destroy_fiber_bridge(void*, void* module) {
    record(destroyed);
    delete static_cast<FiberState*>(module);
}

} // namespace

extern "C" FSIM_SC_EXPORT void fsim_test_reset_lifecycle_v1() {
    lifecycle_events.fill(0);
    lifecycle_event_count = 0;
}

extern "C" FSIM_SC_EXPORT std::size_t fsim_test_lifecycle_count_v1() {
    return lifecycle_event_count;
}

extern "C" FSIM_SC_EXPORT std::uint32_t fsim_test_lifecycle_event_v1(
    const std::size_t index) {
    return index < lifecycle_event_count ? lifecycle_events[index] : 0;
}

extern "C" FSIM_SC_EXPORT void fsim_test_ignore_duplicate_registration_v1(
    const std::uint8_t enabled) {
    ignore_duplicate_registration = enabled != 0;
}

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
    auto status = registrar->register_factory(
        registrar->context, "sample", create_module, destroy_module, nullptr);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = registrar->register_elaboration_factory(
        registrar->context,
        "bridge",
        elaborate_bridge,
        destroy_bridge,
        const_cast<fsim_sc_host_v1*>(host));
    if (status != FSIM_SC_OK
        || registrar->register_factory_parameter == nullptr) {
        return status != FSIM_SC_OK
            ? status
            : FSIM_SC_ABI_MISMATCH;
    }
    status = registrar->register_factory_parameter(
        registrar->context,
        "bridge",
        "WIDTH",
        FSIM_SC_CONSTRUCTION_POSITIVE,
        1,
        8);
    if (status != FSIM_SC_OK) {
        return status;
    }
    status = registrar->register_elaboration_factory(
        registrar->context,
        "fiber_bridge",
        elaborate_fiber_bridge,
        destroy_fiber_bridge,
        const_cast<fsim_sc_host_v1*>(host));
    if (status != FSIM_SC_OK) {
        return status;
    }
    if (ignore_duplicate_registration) {
        (void)registrar->register_elaboration_factory(
            registrar->context,
            "bridge",
            elaborate_bridge,
            destroy_bridge,
            const_cast<fsim_sc_host_v1*>(host));
    }
    return FSIM_SC_OK;
}

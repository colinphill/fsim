// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

#include <stddef.h>
#include <stdint.h>

// FSIM-CONFORMANCE CF-SC-ABI-001 source=SRC-SYSTEMC expectation=accept
_Static_assert(FSIM_SYSTEMC_ABI_VERSION == 3u, "unexpected SystemC ABI");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_process)
        > offsetof(fsim_sc_host_v1, register_port),
    "kernel bridge registration must follow port registration");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_signal)
        > offsetof(fsim_sc_host_v1, report),
    "typed signal registration must follow diagnostics");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_native_module)
        > offsetof(fsim_sc_host_v1, bind_port),
    "native hierarchy registration must follow port binding");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_lifecycle)
        > offsetof(fsim_sc_host_v1, register_native_module),
    "lifecycle registration must follow native hierarchy registration");
_Static_assert(
    offsetof(fsim_sc_host_v1, bind_export)
        > offsetof(fsim_sc_host_v1, register_export),
    "export binding must follow export registration");
_Static_assert(
    offsetof(fsim_sc_host_v1, wait_for_input_or_native_activity)
        > offsetof(fsim_sc_host_v1, set_export_writable),
    "Accellera scheduling must follow boundary metadata");
_Static_assert(
    offsetof(fsim_sc_host_v1, current_time_femtoseconds)
        > offsetof(fsim_sc_host_v1, wait_for_input_or_native_activity),
    "exact application time must remain the final host callback");
_Static_assert(
    offsetof(fsim_sc_registrar_v1, register_elaboration_factory)
        > offsetof(fsim_sc_registrar_v1, context),
    "official factory registration must follow registrar identity");
_Static_assert(
    offsetof(fsim_sc_registrar_v1, register_factory_parameter)
        > offsetof(fsim_sc_registrar_v1, register_elaboration_factory),
    "typed factory schemas must follow factory registration");
_Static_assert(
    sizeof(fsim_sc_handle_v1) == sizeof(uint64_t),
    "SystemC handles must remain opaque 64-bit values");

int main(void)
{
    fsim_sc_host_v1 host = {0};
    fsim_sc_registrar_v1 registrar = {0};
    fsim_sc_value_view_v1 value = {0};
    host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size = (uint32_t)sizeof(registrar);
    value.struct_size = (uint32_t)sizeof(value);
    return host.struct_size == 0u
            || registrar.struct_size == 0u
            || value.struct_size == 0u;
}

// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(FSIM_SYSTEMC_ABI_VERSION == 1u, "unexpected SystemC ABI");
_Static_assert(
    offsetof(fsim_sc_host_v1, set_process_initialize)
        > offsetof(fsim_sc_host_v1, connect_foreign_port),
    "process initialization must remain an append-only host field");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_event)
        > offsetof(fsim_sc_host_v1, set_process_initialize),
    "event registration must remain an append-only host field");
_Static_assert(
    offsetof(fsim_sc_host_v1, notify_event_mode)
        > offsetof(fsim_sc_host_v1, register_event),
    "notification modes must remain an append-only host field");
_Static_assert(
    offsetof(fsim_sc_host_v1, cancel_event)
        > offsetof(fsim_sc_host_v1, notify_event_mode),
    "event cancellation must remain an append-only host field");
_Static_assert(
    offsetof(fsim_sc_host_v1, wait_event_list)
        > offsetof(fsim_sc_host_v1, cancel_event),
    "event-list waits must remain an append-only host field");
_Static_assert(
    offsetof(fsim_sc_host_v1, notify_event_delayed)
        > offsetof(fsim_sc_host_v1, wait_event_list),
    "notify_delayed must remain an append-only host field");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_primitive_channel)
        > offsetof(fsim_sc_host_v1, notify_event_delayed),
    "primitive-channel registration must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, request_update)
        > offsetof(fsim_sc_host_v1, register_primitive_channel),
    "primitive-channel updates must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_signal)
        > offsetof(fsim_sc_host_v1, request_update),
    "typed signal registration must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, value_changed)
        > offsetof(fsim_sc_host_v1, register_signal),
    "signal event queries must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, bind_port)
        > offsetof(fsim_sc_host_v1, value_changed),
    "port/channel binding must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_native_module)
        > offsetof(fsim_sc_host_v1, bind_port),
    "native module registration must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_lifecycle)
        > offsetof(fsim_sc_host_v1, register_native_module),
    "lifecycle registration must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_export)
        > offsetof(fsim_sc_host_v1, register_lifecycle),
    "export registration must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, bind_export)
        > offsetof(fsim_sc_host_v1, register_export),
    "export binding must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, wait_static)
        > offsetof(fsim_sc_host_v1, bind_export),
    "plain thread wait must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, set_foreign_child_actual)
        > offsetof(fsim_sc_host_v1, wait_static),
    "foreign HDL construction actuals must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, get_construction_value)
        > offsetof(fsim_sc_host_v1, set_foreign_child_actual),
    "SystemC construction-value lookup must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, set_export_writable)
        > offsetof(fsim_sc_host_v1, get_construction_value),
    "SystemC export capabilities must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, register_metadata_object)
        > offsetof(fsim_sc_host_v1, set_export_writable),
    "SystemC custom metadata must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, set_primitive_channel_kind)
        > offsetof(fsim_sc_host_v1, register_metadata_object),
    "SystemC primitive-channel kinds must remain append-only");
_Static_assert(
    offsetof(fsim_sc_host_v1, wait_event_timeout)
        > offsetof(fsim_sc_host_v1, set_primitive_channel_kind),
    "SystemC timed event waits must remain append-only");
_Static_assert(
    offsetof(fsim_sc_registrar_v1, register_elaboration_factory)
        > offsetof(fsim_sc_registrar_v1, register_factory),
    "typed factory registration must remain append-only");
_Static_assert(
    offsetof(fsim_sc_registrar_v1, register_factory_parameter)
        > offsetof(fsim_sc_registrar_v1, register_elaboration_factory),
    "typed factory schemas must remain append-only");
_Static_assert(
    sizeof(fsim_sc_handle_v1) == sizeof(uint64_t),
    "SystemC handles must remain opaque 64-bit values");

int main(void) {
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

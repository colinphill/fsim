// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

#include <stddef.h>
#include <stdint.h>

#define FSIM_SC_LAYOUT(type, expected_size, expected_align)                 \
    _Static_assert(sizeof(type) == (expected_size), #type " size changed"); \
    _Static_assert(_Alignof(type) == (expected_align), #type " alignment changed")
#define FSIM_SC_OFFSET(type, field, expected) \
    _Static_assert(offsetof(type, field) == (expected), #type "." #field " moved")
#define FSIM_SC_TYPE(expression, type) \
    _Static_assert(                    \
        _Generic((expression), type: 1, default: 0), #expression " type changed")

// FSIM-CONFORMANCE CF-SC-ABI-001 source=SRC-SYSTEMC expectation=accept
_Static_assert(FSIM_SYSTEMC_ABI_VERSION == 4u, "unexpected SystemC ABI");
_Static_assert(sizeof(fsim_sc_handle_v1) == 8u, "SystemC handle width changed");
_Static_assert(sizeof(fsim_sc_status_v1) == 4u, "SystemC status width changed");
_Static_assert(sizeof(fsim_sc_edge_kind_v1) == 4u, "SystemC edge width changed");
_Static_assert(
    sizeof(fsim_sc_port_direction_v1) == 4u,
    "SystemC port direction width changed");
_Static_assert(
    sizeof(fsim_sc_value_encoding_v1) == 4u,
    "SystemC value encoding width changed");
_Static_assert(
    sizeof(fsim_sc_construction_type_v1) == 4u,
    "SystemC construction type width changed");

FSIM_SC_LAYOUT(fsim_sc_value_view_v1, 32u, 8u);
FSIM_SC_OFFSET(fsim_sc_value_view_v1, struct_size, 0u);
FSIM_SC_OFFSET(fsim_sc_value_view_v1, encoding, 4u);
FSIM_SC_OFFSET(fsim_sc_value_view_v1, width, 8u);
FSIM_SC_OFFSET(fsim_sc_value_view_v1, data, 16u);
FSIM_SC_OFFSET(fsim_sc_value_view_v1, data_size, 24u);

FSIM_SC_LAYOUT(fsim_sc_host_v1, 152u, 8u);
FSIM_SC_OFFSET(fsim_sc_host_v1, abi_version, 0u);
FSIM_SC_OFFSET(fsim_sc_host_v1, struct_size, 4u);
FSIM_SC_OFFSET(fsim_sc_host_v1, context, 8u);
FSIM_SC_OFFSET(fsim_sc_host_v1, register_port, 16u);
FSIM_SC_OFFSET(fsim_sc_host_v1, register_process, 24u);
FSIM_SC_OFFSET(fsim_sc_host_v1, add_sensitivity, 32u);
FSIM_SC_OFFSET(fsim_sc_host_v1, read_value, 40u);
FSIM_SC_OFFSET(fsim_sc_host_v1, write_value, 48u);
FSIM_SC_OFFSET(fsim_sc_host_v1, report, 56u);
FSIM_SC_OFFSET(fsim_sc_host_v1, register_signal, 64u);
FSIM_SC_OFFSET(fsim_sc_host_v1, bind_port, 72u);
FSIM_SC_OFFSET(fsim_sc_host_v1, register_native_module, 80u);
FSIM_SC_OFFSET(fsim_sc_host_v1, register_lifecycle, 88u);
FSIM_SC_OFFSET(fsim_sc_host_v1, register_export, 96u);
FSIM_SC_OFFSET(fsim_sc_host_v1, bind_export, 104u);
FSIM_SC_OFFSET(fsim_sc_host_v1, get_construction_value, 112u);
FSIM_SC_OFFSET(fsim_sc_host_v1, set_export_writable, 120u);
FSIM_SC_OFFSET(fsim_sc_host_v1, wait_for_input_or_native_activity, 128u);
FSIM_SC_OFFSET(fsim_sc_host_v1, current_time_femtoseconds, 136u);
FSIM_SC_OFFSET(fsim_sc_host_v1, scv_compatibility_identity, 144u);

FSIM_SC_LAYOUT(fsim_sc_registrar_v1, 32u, 8u);
FSIM_SC_OFFSET(fsim_sc_registrar_v1, abi_version, 0u);
FSIM_SC_OFFSET(fsim_sc_registrar_v1, struct_size, 4u);
FSIM_SC_OFFSET(fsim_sc_registrar_v1, context, 8u);
FSIM_SC_OFFSET(fsim_sc_registrar_v1, register_elaboration_factory, 16u);
FSIM_SC_OFFSET(fsim_sc_registrar_v1, register_factory_parameter, 24u);

FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->register_port,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, const char*,
        fsim_sc_port_direction_v1, fsim_sc_value_encoding_v1, uint32_t,
        fsim_sc_handle_v1*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->register_process,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, const char*,
        fsim_sc_process_entry_v1, void*, fsim_sc_handle_v1*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->add_sensitivity,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, fsim_sc_handle_v1,
        fsim_sc_edge_kind_v1));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->read_value,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, fsim_sc_value_view_v1*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->write_value,
    fsim_sc_status_v1 (*)(
        void*, fsim_sc_handle_v1, const fsim_sc_value_view_v1*));
FSIM_SC_TYPE(((fsim_sc_host_v1*)0)->report, void (*)(void*, int, const char*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->register_signal,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, const char*,
        fsim_sc_value_encoding_v1, uint32_t, const fsim_sc_value_view_v1*,
        fsim_sc_handle_v1*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->bind_port,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, fsim_sc_handle_v1));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->register_native_module,
    fsim_sc_status_v1 (*)(
        void*, fsim_sc_handle_v1, const char*, fsim_sc_handle_v1*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->register_lifecycle,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1,
        fsim_sc_lifecycle_entry_v1, fsim_sc_lifecycle_entry_v1,
        fsim_sc_lifecycle_entry_v1, fsim_sc_lifecycle_entry_v1, void*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->register_export,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, const char*,
        fsim_sc_value_encoding_v1, uint32_t, fsim_sc_handle_v1*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->bind_export,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, fsim_sc_handle_v1));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->get_construction_value,
    fsim_sc_status_v1 (*)(
        void*, fsim_sc_handle_v1, const char*, int64_t*));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->set_export_writable,
    fsim_sc_status_v1 (*)(void*, fsim_sc_handle_v1, uint8_t));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->wait_for_input_or_native_activity,
    fsim_sc_status_v1 (*)(void*, uint64_t));
FSIM_SC_TYPE(
    ((fsim_sc_host_v1*)0)->current_time_femtoseconds,
    fsim_sc_status_v1 (*)(void*, uint64_t*));
FSIM_SC_TYPE(
    ((fsim_sc_registrar_v1*)0)->register_elaboration_factory,
    fsim_sc_status_v1 (*)(void*, const char*, fsim_sc_module_elaborate_v1,
        fsim_sc_module_destroy_v1, void*));
FSIM_SC_TYPE(
    ((fsim_sc_registrar_v1*)0)->register_factory_parameter,
    fsim_sc_status_v1 (*)(void*, const char*, const char*,
        fsim_sc_construction_type_v1, uint8_t, int64_t));
FSIM_SC_TYPE(
    (fsim_plugin_init_v1_fn)0,
    fsim_sc_status_v1 (*)(const fsim_sc_host_v1*, fsim_sc_registrar_v1*));

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
_Static_assert(
    offsetof(fsim_sc_host_v1, scv_compatibility_identity)
        > offsetof(fsim_sc_host_v1, current_time_femtoseconds),
    "SCV producer identity must extend the host ABI append-only");

int main(void)
{
    fsim_sc_host_v1 host = { 0 };
    fsim_sc_registrar_v1 registrar = { 0 };
    fsim_sc_value_view_v1 value = { 0 };
    host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size = (uint32_t)sizeof(registrar);
    value.struct_size = (uint32_t)sizeof(value);
    return host.struct_size == 0u
        || registrar.struct_size == 0u
        || value.struct_size == 0u;
}

#undef FSIM_SC_TYPE
#undef FSIM_SC_OFFSET
#undef FSIM_SC_LAYOUT

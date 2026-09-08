// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_abi.h"

#include <stddef.h>

#define FSIM_VHPI_LAYOUT(type, size, alignment)            \
    _Static_assert(sizeof(type) == (size), #type " size"); \
    _Static_assert(_Alignof(type) == (alignment), #type " alignment")
#define FSIM_VHPI_OFFSET(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), #type " " #field)

_Static_assert(FSIM_VHPI_HOST_ABI_VERSION == 1u, "VHPI host ABI v1");
_Static_assert(FSIM_VHPI_HOST_ABI_VERSION_V2 == 2u, "VHPI host ABI v2");
_Static_assert(FSIM_VHPI_HOST_ABI_VERSION_V3 == 3u, "VHPI host ABI v3");
_Static_assert(FSIM_VHPI_PLUGIN_ABI_VERSION == 1u, "VHPI plug-in ABI v1");
_Static_assert(sizeof(void*) == 8u, "VHPI ABI requires x86-64 pointers");
_Static_assert(sizeof(fsim_vhpi_handle_v1) == 8u, "VHPI handles are 64-bit");
_Static_assert(sizeof(fsim_vhpi_status_v1) == 4u, "VHPI status enum width");
_Static_assert(
    sizeof(fsim_vhpi_error_severity_v1) == 4u,
    "VHPI severity enum width");

FSIM_VHPI_LAYOUT(fsim_vhpi_error_view_v1, 32u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_error_view_v1, severity, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_error_view_v1, code_size, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_error_view_v1, code, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_error_view_v1, message_size, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_error_view_v1, message, 24u);

FSIM_VHPI_LAYOUT(fsim_vhpi_host_v1, 40u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v1, abi_version, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v1, struct_size, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v1, pointer_bits, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v1, flags, 12u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v1, simulation_identity, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v1, context, 24u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v1, report, 32u);

FSIM_VHPI_LAYOUT(fsim_vhpi_service_request_v1, 48u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, struct_size, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, operation, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, flags, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, text_size, 12u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, handle, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, argument, 24u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, user_data, 32u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_request_v1, text, 40u);

FSIM_VHPI_LAYOUT(fsim_vhpi_service_result_v1, 40u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_result_v1, struct_size, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_result_v1, status, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_result_v1, flags, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_result_v1, reserved, 12u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_result_v1, handle, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_result_v1, value, 24u);
FSIM_VHPI_OFFSET(fsim_vhpi_service_result_v1, user_data, 32u);

FSIM_VHPI_LAYOUT(fsim_vhpi_host_v2, 56u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v2, v1, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v2, service_context, 40u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v2, invoke_service, 48u);

FSIM_VHPI_LAYOUT(fsim_vhpi_capabilities_v3, 32u, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, struct_size, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, vhdl_revision, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, object_kind_count, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, relationship_kind_count, 12u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, property_kind_count, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, maximum_index_dimensions, 20u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, maximum_package_dependencies, 24u);
FSIM_VHPI_OFFSET(fsim_vhpi_capabilities_v3, flags, 28u);

FSIM_VHPI_LAYOUT(fsim_vhpi_value_v3, 48u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, struct_size, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, format, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, flags, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, buffer_size, 12u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, object, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, integer, 24u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, real, 32u);
FSIM_VHPI_OFFSET(fsim_vhpi_value_v3, buffer, 40u);

FSIM_VHPI_LAYOUT(fsim_vhpi_tool_request_v3, 32u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_tool_request_v3, struct_size, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_tool_request_v3, action, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_tool_request_v3, flags, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_tool_request_v3, text_size, 12u);
FSIM_VHPI_OFFSET(fsim_vhpi_tool_request_v3, status, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_tool_request_v3, reserved, 20u);
FSIM_VHPI_OFFSET(fsim_vhpi_tool_request_v3, text, 24u);

FSIM_VHPI_LAYOUT(fsim_vhpi_host_v3, 80u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v3, v2, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v3, query_capabilities, 56u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v3, access_value, 64u);
FSIM_VHPI_OFFSET(fsim_vhpi_host_v3, execute_tool, 72u);

_Static_assert(
    FSIM_VHPI_CALLBACK_TOOL_EXECUTION == 13,
    "VHPI tool callback identity");
_Static_assert(
    FSIM_VHPI_VALUE_BINARY_STRING == 9,
    "VHPI binary string value identity");
_Static_assert(FSIM_VHPI_TOOL_SAVE == 4, "VHPI tool action identity");

FSIM_VHPI_LAYOUT(fsim_vhpi_plugin_v1, 48u, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, abi_version, 0u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, struct_size, 4u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, flags, 8u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, name_size, 12u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, name, 16u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, context, 24u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, startup, 32u);
FSIM_VHPI_OFFSET(fsim_vhpi_plugin_v1, shutdown, 40u);

size_t fsim_vhpi_abi_c_host_size(void) { return sizeof(fsim_vhpi_host_v1); }

const char* fsim_vhpi_abi_c_bind_symbol(void)
{
    return FSIM_VHPI_PLUGIN_BIND_SYMBOL;
}

void fsim_vhpi_abi_c_report(const fsim_vhpi_host_v1* host)
{
    static const char code[] = "FSIM-VHPI-ABI-TEST";
    static const char message[] = "C translation unit callback";
    const fsim_vhpi_error_view_v1 error = {
        FSIM_VHPI_ERROR_NOTE,
        (uint32_t)(sizeof(code) - 1),
        code,
        (uint32_t)(sizeof(message) - 1),
        message,
    };
    host->report(host->context, &error);
}

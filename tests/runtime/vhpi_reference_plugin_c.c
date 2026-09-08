// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_abi.h"

#include <string.h>

static const fsim_vhpi_host_v2* host;
static uint32_t service_calls;
static uint64_t checksum;

static fsim_vhpi_status_v1 call_service(
    const uint32_t operation,
    const uint32_t flags,
    const char* text) {
  fsim_vhpi_service_request_v1 request;
  fsim_vhpi_service_result_v1 result;
  memset(&request, 0, sizeof(request));
  memset(&result, 0, sizeof(result));
  request.struct_size = (uint32_t)sizeof(request);
  request.operation = operation;
  request.flags = flags;
  request.text_size = (uint32_t)strlen(text);
  request.handle = service_calls == 0 ? 0 : checksum;
  request.argument = (uint64_t)operation * 10u;
  request.user_data = UINT64_C(0xc000) + operation;
  request.text = text;
  result.struct_size = (uint32_t)sizeof(result);
  if (host->invoke_service(
          host->service_context, &request, &result)
          != FSIM_VHPI_STATUS_OK
      || result.status != FSIM_VHPI_STATUS_OK
      || result.struct_size < sizeof(result)
      || result.reserved != 0) {
    return FSIM_VHPI_STATUS_INTERNAL_ERROR;
  }
  ++service_calls;
  checksum ^= result.handle;
  checksum ^= result.value;
  checksum ^= result.user_data;
  return FSIM_VHPI_STATUS_OK;
}

static void report_startup(void) {
  static const char code[] = "FSIM-VHPI-REFERENCE-C";
  static const char message[] = "independent C image startup";
  const fsim_vhpi_error_view_v1 report = {
      FSIM_VHPI_ERROR_NOTE,
      (uint32_t)(sizeof(code) - 1),
      code,
      (uint32_t)(sizeof(message) - 1),
      message,
  };
  host->v1.report(host->v1.context, &report);
}

static fsim_vhpi_status_v1 FSIM_VHPI_CALL startup(void* context) {
  static const char* names[] = {
      "hierarchy", "type", "value", "driver", "control", "time",
      "callback", "foreign", "association", "io", "user-data",
      "checkpoint", "start-lifecycle", "property", "tool", "capability",
  };
  uint32_t operation;
  host = (const fsim_vhpi_host_v2*)context;
  report_startup();
  if (host->v1.abi_version == FSIM_VHPI_HOST_ABI_VERSION_V3) {
    const fsim_vhpi_host_v3* host_v3 = (const fsim_vhpi_host_v3*)host;
    fsim_vhpi_capabilities_v3 capabilities;
    fsim_vhpi_value_v3 value;
    fsim_vhpi_tool_request_v3 tool;
    memset(&capabilities, 0, sizeof(capabilities));
    capabilities.struct_size = (uint32_t)sizeof(capabilities);
    if (host_v3->query_capabilities(
            host_v3->v2.service_context, &capabilities)
            != FSIM_VHPI_STATUS_OK
        || capabilities.vhdl_revision != 2019u
        || (capabilities.flags & FSIM_VHPI_CAPABILITY_TOOL_EXECUTION) == 0u) {
      return FSIM_VHPI_STATUS_INTERNAL_ERROR;
    }
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    value.format = FSIM_VHPI_VALUE_INTEGER;
    if (host_v3->access_value(
            host_v3->v2.service_context, FSIM_VHPI_VALUE_READ, &value)
            != FSIM_VHPI_STATUS_OK
        || value.integer != 2019) {
      return FSIM_VHPI_STATUS_INTERNAL_ERROR;
    }
    memset(&tool, 0, sizeof(tool));
    tool.struct_size = (uint32_t)sizeof(tool);
    tool.action = FSIM_VHPI_TOOL_SAVE;
    tool.text = "reference-save.fsim";
    tool.text_size = (uint32_t)strlen(tool.text);
    if (host_v3->execute_tool(host_v3->v2.service_context, &tool)
        != FSIM_VHPI_STATUS_OK) {
      return FSIM_VHPI_STATUS_INTERNAL_ERROR;
    }
  }
  for (operation = FSIM_VHPI_SERVICE_HIERARCHY;
       operation <= FSIM_VHPI_SERVICE_CAPABILITY;
       ++operation) {
    const fsim_vhpi_status_v1 status =
        call_service(operation, 0, names[operation - 1]);
    if (status != FSIM_VHPI_STATUS_OK) {
      return status;
    }
  }
  return FSIM_VHPI_STATUS_OK;
}

static fsim_vhpi_status_v1 FSIM_VHPI_CALL shutdown(void* context) {
  (void)context;
  return call_service(
      FSIM_VHPI_SERVICE_LIFECYCLE, 1, "shutdown-lifecycle");
}

FSIM_VHPI_EXPORT fsim_vhpi_status_v1 FSIM_VHPI_CALL
fsim_vhpi_plugin_bind_v1(
    const fsim_vhpi_host_v1* host_v1,
    fsim_vhpi_plugin_v1* plugin) {
  static const char name[] = "fsim-vhpi-reference-c";
  if (host_v1 == NULL || plugin == NULL
      || (host_v1->abi_version != FSIM_VHPI_HOST_ABI_VERSION_V2
          && host_v1->abi_version != FSIM_VHPI_HOST_ABI_VERSION_V3)
      || host_v1->struct_size < sizeof(fsim_vhpi_host_v2)) {
    return FSIM_VHPI_STATUS_UNSUPPORTED;
  }
  host = (const fsim_vhpi_host_v2*)host_v1;
  service_calls = 0;
  checksum = 0;
  *plugin = (fsim_vhpi_plugin_v1){
      FSIM_VHPI_PLUGIN_ABI_VERSION,
      (uint32_t)sizeof(fsim_vhpi_plugin_v1),
      0,
      (uint32_t)(sizeof(name) - 1),
      name,
      (void*)host,
      startup,
      shutdown,
  };
  return FSIM_VHPI_STATUS_OK;
}

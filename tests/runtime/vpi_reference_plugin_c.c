// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_abi.h"

#include <string.h>

static const fsim_vpi_host_v2* host;
static uint32_t startup_calls;
static uint32_t shutdown_calls;
static uint32_t service_calls;
static uint64_t checksum;
static uint64_t last_user_data;

static fsim_vpi_status_v1 call_service(
    const uint32_t operation,
    const uint32_t flags,
    const char* text) {
  fsim_vpi_service_request_v1 request;
  fsim_vpi_service_result_v1 result;
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
          != FSIM_VPI_STATUS_OK
      || result.status != FSIM_VPI_STATUS_OK
      || result.struct_size < sizeof(result)) {
    return FSIM_VPI_STATUS_INTERNAL_ERROR;
  }
  ++service_calls;
  checksum ^= result.handle;
  checksum ^= result.value;
  checksum ^= result.user_data;
  last_user_data = result.user_data;
  return FSIM_VPI_STATUS_OK;
}

static fsim_vpi_status_v1 FSIM_VPI_CALL startup(void* context) {
  static const char* names[] = {
      "hierarchy", "value", "time", "callback", "control",
      "system-task", "system-function", "io", "user-data",
      "start-lifecycle",
  };
  uint32_t operation;
  host = (const fsim_vpi_host_v2*)context;
  ++startup_calls;
  for (operation = FSIM_VPI_SERVICE_HIERARCHY;
       operation <= FSIM_VPI_SERVICE_LIFECYCLE;
       ++operation) {
    const fsim_vpi_status_v1 status =
        call_service(operation, 0, names[operation - 1]);
    if (status != FSIM_VPI_STATUS_OK) {
      return status;
    }
  }
  return FSIM_VPI_STATUS_OK;
}

static fsim_vpi_status_v1 FSIM_VPI_CALL shutdown(void* context) {
  (void)context;
  ++shutdown_calls;
  return call_service(
      FSIM_VPI_SERVICE_LIFECYCLE, 1, "shutdown-lifecycle");
}

FSIM_VPI_EXPORT uint32_t FSIM_VPI_CALL
fsim_vpi_reference_startup_calls(void) {
  return startup_calls;
}

FSIM_VPI_EXPORT uint32_t FSIM_VPI_CALL
fsim_vpi_reference_shutdown_calls(void) {
  return shutdown_calls;
}

FSIM_VPI_EXPORT uint32_t FSIM_VPI_CALL
fsim_vpi_reference_service_calls(void) {
  return service_calls;
}

FSIM_VPI_EXPORT uint64_t FSIM_VPI_CALL
fsim_vpi_reference_checksum(void) {
  return checksum;
}

FSIM_VPI_EXPORT uint64_t FSIM_VPI_CALL
fsim_vpi_reference_last_user_data(void) {
  return last_user_data;
}

FSIM_VPI_EXPORT fsim_vpi_status_v1 FSIM_VPI_CALL
fsim_vpi_plugin_bind_v1(
    const fsim_vpi_host_v1* host_v1,
    fsim_vpi_plugin_v1* plugin) {
  static const char name[] = "fsim-vpi-reference-c";
  if (host_v1 == NULL || plugin == NULL
      || host_v1->abi_version != FSIM_VPI_HOST_ABI_VERSION_V2
      || host_v1->struct_size < sizeof(fsim_vpi_host_v2)) {
    return FSIM_VPI_STATUS_UNSUPPORTED;
  }
  host = (const fsim_vpi_host_v2*)host_v1;
  *plugin = (fsim_vpi_plugin_v1){
      FSIM_VPI_PLUGIN_ABI_VERSION,
      (uint32_t)sizeof(fsim_vpi_plugin_v1),
      0,
      (uint32_t)(sizeof(name) - 1),
      name,
      (void*)host,
      startup,
      shutdown,
  };
  return FSIM_VPI_STATUS_OK;
}

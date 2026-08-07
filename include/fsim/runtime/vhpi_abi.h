// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FSIM_VHPI_HOST_ABI_VERSION 1u
#define FSIM_VHPI_HOST_ABI_VERSION_V2 2u
#define FSIM_VHPI_PLUGIN_ABI_VERSION 1u
#define FSIM_VHPI_PLUGIN_BIND_SYMBOL "fsim_vhpi_plugin_bind_v1"

#if defined(_WIN32)
#define FSIM_VHPI_EXPORT __declspec(dllexport)
#define FSIM_VHPI_CALL __cdecl
#else
#define FSIM_VHPI_EXPORT __attribute__((visibility("default")))
#define FSIM_VHPI_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Handles are simulation-qualified integer identities. They never contain a
 * host address and remain distinct from VPI or any other native interface.
 */
typedef uint64_t fsim_vhpi_handle_v1;

typedef enum fsim_vhpi_status_v1 {
  FSIM_VHPI_STATUS_OK = 0,
  FSIM_VHPI_STATUS_INVALID_ARGUMENT = 1,
  FSIM_VHPI_STATUS_UNSUPPORTED = 2,
  FSIM_VHPI_STATUS_NOT_FOUND = 3,
  FSIM_VHPI_STATUS_STALE_HANDLE = 4,
  FSIM_VHPI_STATUS_WRONG_SIMULATION = 5,
  FSIM_VHPI_STATUS_RESOURCE_LIMIT = 6,
  FSIM_VHPI_STATUS_INTERNAL_ERROR = 7
} fsim_vhpi_status_v1;

typedef enum fsim_vhpi_error_severity_v1 {
  FSIM_VHPI_ERROR_NOTE = 0,
  FSIM_VHPI_ERROR_WARNING = 1,
  FSIM_VHPI_ERROR_ERROR = 2,
  FSIM_VHPI_ERROR_FAILURE = 3,
  FSIM_VHPI_ERROR_SYSTEM = 4,
  FSIM_VHPI_ERROR_INTERNAL = 5
} fsim_vhpi_error_severity_v1;

typedef struct fsim_vhpi_error_view_v1 {
  fsim_vhpi_error_severity_v1 severity;
  uint32_t code_size;
  const char* code;
  uint32_t message_size;
  const char* message;
} fsim_vhpi_error_view_v1;

typedef void(FSIM_VHPI_CALL *fsim_vhpi_report_v1)(
    void* context, const fsim_vhpi_error_view_v1* error);

/*
 * Every ABI table is append-only. Consumers must validate abi_version and
 * struct_size before reading a field. flags is reserved and must be zero.
 */
typedef struct fsim_vhpi_host_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t pointer_bits;
  uint32_t flags;
  uint64_t simulation_identity;
  void* context;
  fsim_vhpi_report_v1 report;
} fsim_vhpi_host_v1;

typedef enum fsim_vhpi_service_operation_v1 {
  FSIM_VHPI_SERVICE_HIERARCHY = 1,
  FSIM_VHPI_SERVICE_TYPE = 2,
  FSIM_VHPI_SERVICE_VALUE = 3,
  FSIM_VHPI_SERVICE_DRIVER = 4,
  FSIM_VHPI_SERVICE_CONTROL = 5,
  FSIM_VHPI_SERVICE_TIME = 6,
  FSIM_VHPI_SERVICE_CALLBACK = 7,
  FSIM_VHPI_SERVICE_FOREIGN = 8,
  FSIM_VHPI_SERVICE_ASSOCIATION = 9,
  FSIM_VHPI_SERVICE_IO = 10,
  FSIM_VHPI_SERVICE_USER_DATA = 11,
  FSIM_VHPI_SERVICE_CHECKPOINT = 12,
  FSIM_VHPI_SERVICE_LIFECYCLE = 13
} fsim_vhpi_service_operation_v1;

typedef struct fsim_vhpi_service_request_v1 {
  uint32_t struct_size;
  uint32_t operation;
  uint32_t flags;
  uint32_t text_size;
  fsim_vhpi_handle_v1 handle;
  uint64_t argument;
  uint64_t user_data;
  const char* text;
} fsim_vhpi_service_request_v1;

typedef struct fsim_vhpi_service_result_v1 {
  uint32_t struct_size;
  fsim_vhpi_status_v1 status;
  uint32_t flags;
  uint32_t reserved;
  fsim_vhpi_handle_v1 handle;
  uint64_t value;
  uint64_t user_data;
} fsim_vhpi_service_result_v1;

typedef fsim_vhpi_status_v1(FSIM_VHPI_CALL *fsim_vhpi_invoke_service_v1)(
    void* context,
    const fsim_vhpi_service_request_v1* request,
    fsim_vhpi_service_result_v1* result);

/*
 * v2 retains the complete v1 host as its prefix. Bind still receives a v1
 * pointer; a service-aware image checks version and size before casting.
 */
typedef struct fsim_vhpi_host_v2 {
  fsim_vhpi_host_v1 v1;
  void* service_context;
  fsim_vhpi_invoke_service_v1 invoke_service;
} fsim_vhpi_host_v2;

typedef fsim_vhpi_status_v1(FSIM_VHPI_CALL *fsim_vhpi_plugin_lifecycle_v1)(
    void* context);

typedef struct fsim_vhpi_plugin_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t flags;
  uint32_t name_size;
  const char* name;
  void* context;
  fsim_vhpi_plugin_lifecycle_v1 startup;
  fsim_vhpi_plugin_lifecycle_v1 shutdown;
} fsim_vhpi_plugin_v1;

typedef fsim_vhpi_status_v1(FSIM_VHPI_CALL *fsim_vhpi_plugin_bind_v1_fn)(
    const fsim_vhpi_host_v1* host, fsim_vhpi_plugin_v1* plugin);

#ifdef __cplusplus
}
#endif

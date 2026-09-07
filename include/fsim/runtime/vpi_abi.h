// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FSIM_VPI_HOST_ABI_VERSION 1u
#define FSIM_VPI_HOST_ABI_VERSION_V2 2u
#define FSIM_VPI_PLUGIN_ABI_VERSION 1u
#define FSIM_VPI_PLUGIN_BIND_SYMBOL "fsim_vpi_plugin_bind_v1"

#if defined(_WIN32)
#define FSIM_VPI_EXPORT __declspec(dllexport)
#define FSIM_VPI_CALL __cdecl
#else
#define FSIM_VPI_EXPORT __attribute__((visibility("default")))
#define FSIM_VPI_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t fsim_vpi_handle_v1;

/* IEEE 1800 VPI coverage enumeration identities. */
#ifndef vpiCoverageStart
#define vpiCoverageStart 750
#endif
#ifndef vpiCoverageStop
#define vpiCoverageStop 751
#endif
/* Some historical sv_vpi_user.h releases used this spelling. */
#ifndef vpiCoverageStOp
#define vpiCoverageStOp vpiCoverageStop
#endif
#ifndef vpiCoverageReset
#define vpiCoverageReset 752
#endif
#ifndef vpiCoverageCheck
#define vpiCoverageCheck 753
#endif
#ifndef vpiCoverageMerge
#define vpiCoverageMerge 754
#endif
#ifndef vpiCoverageSave
#define vpiCoverageSave 755
#endif
#ifndef vpiFsm
#define vpiFsm 758
#endif
#ifndef vpiFsmHandle
#define vpiFsmHandle 759
#endif
#ifndef vpiAssertCoverage
#define vpiAssertCoverage 760
#endif
#ifndef vpiFsmStateCoverage
#define vpiFsmStateCoverage 761
#endif
#ifndef vpiStatementCoverage
#define vpiStatementCoverage 762
#endif
#ifndef vpiToggleCoverage
#define vpiToggleCoverage 763
#endif
#ifndef vpiCovered
#define vpiCovered 765
#endif
#ifndef vpiCoveredMax
#define vpiCoveredMax 766
#endif
#ifndef vpiCoverMax
#define vpiCoverMax vpiCoveredMax
#endif
#ifndef vpiCoveredCount
#define vpiCoveredCount 767
#endif
#ifndef vpiAssertAttemptCovered
#define vpiAssertAttemptCovered 770
#endif
#ifndef vpiAssertSuccessCovered
#define vpiAssertSuccessCovered 771
#endif
#ifndef vpiAssertFailureCovered
#define vpiAssertFailureCovered 772
#endif
#ifndef vpiAssertVacuousSuccessCovered
#define vpiAssertVacuousSuccessCovered 773
#endif
#ifndef vpiAssertDisableCovered
#define vpiAssertDisableCovered 774
#endif
#ifndef vpiFsmStates
#define vpiFsmStates 775
#endif
#ifndef vpiFsmStateExpression
#define vpiFsmStateExpression 776
#endif
#ifndef vpiAssertKillCovered
#define vpiAssertKillCovered 777
#endif

typedef enum fsim_vpi_status_v1 {
  FSIM_VPI_STATUS_OK = 0,
  FSIM_VPI_STATUS_INVALID_ARGUMENT = 1,
  FSIM_VPI_STATUS_UNSUPPORTED = 2,
  FSIM_VPI_STATUS_NOT_FOUND = 3,
  FSIM_VPI_STATUS_STALE_HANDLE = 4,
  FSIM_VPI_STATUS_WRONG_SIMULATION = 5,
  FSIM_VPI_STATUS_RESOURCE_LIMIT = 6,
  FSIM_VPI_STATUS_INTERNAL_ERROR = 7
} fsim_vpi_status_v1;

typedef enum fsim_vpi_error_severity_v1 {
  FSIM_VPI_ERROR_NOTICE = 0,
  FSIM_VPI_ERROR_WARNING = 1,
  FSIM_VPI_ERROR_ERROR = 2,
  FSIM_VPI_ERROR_SYSTEM = 3,
  FSIM_VPI_ERROR_INTERNAL = 4
} fsim_vpi_error_severity_v1;

typedef struct fsim_vpi_error_view_v1 {
  fsim_vpi_error_severity_v1 severity;
  uint32_t code_size;
  const char* code;
  uint32_t message_size;
  const char* message;
} fsim_vpi_error_view_v1;

typedef void(FSIM_VPI_CALL *fsim_vpi_report_v1)(
    void* context, const fsim_vpi_error_view_v1* error);

/*
 * The host table is append-only. A plug-in must check both abi_version and
 * struct_size before reading a callback. Handles are stable integer identities
 * qualified by simulation_identity; they are never host object addresses.
 */
typedef struct fsim_vpi_host_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t pointer_bits;
  uint32_t flags;
  uint64_t simulation_identity;
  void* context;
  fsim_vpi_report_v1 report;
} fsim_vpi_host_v1;

typedef enum fsim_vpi_service_operation_v1 {
  FSIM_VPI_SERVICE_HIERARCHY = 1,
  FSIM_VPI_SERVICE_VALUE = 2,
  FSIM_VPI_SERVICE_TIME = 3,
  FSIM_VPI_SERVICE_CALLBACK = 4,
  FSIM_VPI_SERVICE_CONTROL = 5,
  FSIM_VPI_SERVICE_SYSTEM_TASK = 6,
  FSIM_VPI_SERVICE_SYSTEM_FUNCTION = 7,
  FSIM_VPI_SERVICE_IO = 8,
  FSIM_VPI_SERVICE_USER_DATA = 9,
  FSIM_VPI_SERVICE_LIFECYCLE = 10
} fsim_vpi_service_operation_v1;

typedef struct fsim_vpi_service_request_v1 {
  uint32_t struct_size;
  uint32_t operation;
  uint32_t flags;
  uint32_t text_size;
  fsim_vpi_handle_v1 handle;
  uint64_t argument;
  uint64_t user_data;
  const char* text;
} fsim_vpi_service_request_v1;

typedef struct fsim_vpi_service_result_v1 {
  uint32_t struct_size;
  fsim_vpi_status_v1 status;
  uint32_t flags;
  uint32_t reserved;
  fsim_vpi_handle_v1 handle;
  uint64_t value;
  uint64_t user_data;
} fsim_vpi_service_result_v1;

typedef fsim_vpi_status_v1(FSIM_VPI_CALL *fsim_vpi_invoke_service_v1)(
    void* context,
    const fsim_vpi_service_request_v1* request,
    fsim_vpi_service_result_v1* result);

/*
 * v2 retains the complete v1 host as its first member. The bind entry point
 * still receives a v1 pointer and may inspect abi_version/struct_size before
 * treating it as v2. This keeps old images source and binary compatible.
 */
typedef struct fsim_vpi_host_v2 {
  fsim_vpi_host_v1 v1;
  void* service_context;
  fsim_vpi_invoke_service_v1 invoke_service;
} fsim_vpi_host_v2;

typedef fsim_vpi_status_v1(FSIM_VPI_CALL *fsim_vpi_plugin_lifecycle_v1)(
    void* context);

/*
 * A successful bind publishes one complete descriptor. The loader owns the
 * copied descriptor fields and calls shutdown exactly once after a successful
 * startup, including failure containment paths added by later ABI extensions.
 */
typedef struct fsim_vpi_plugin_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t flags;
  uint32_t name_size;
  const char* name;
  void* context;
  fsim_vpi_plugin_lifecycle_v1 startup;
  fsim_vpi_plugin_lifecycle_v1 shutdown;
} fsim_vpi_plugin_v1;

#ifdef __cplusplus
typedef fsim_vpi_status_v1(FSIM_VPI_CALL *fsim_vpi_plugin_bind_v1_fn)(
    const fsim_vpi_host_v1* host,
    fsim_vpi_plugin_v1* plugin) noexcept(false);
#else
typedef fsim_vpi_status_v1(FSIM_VPI_CALL *fsim_vpi_plugin_bind_v1_fn)(
    const fsim_vpi_host_v1* host, fsim_vpi_plugin_v1* plugin);
#endif

#ifdef __cplusplus
}
#endif

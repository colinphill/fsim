// SPDX-License-Identifier: Apache-2.0
#ifndef FSIM_RUNTIME_VHPI_BRIDGE_H
#define FSIM_RUNTIME_VHPI_BRIDGE_H

#include "vhpi_user.h"

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#define FSIM_VHPI_BRIDGE_CALL __cdecl
#if defined(FSIM_VHPI_LINK_SURFACE_BUILD)
#define FSIM_VHPI_BRIDGE_API __declspec(dllexport)
#else
#define FSIM_VHPI_BRIDGE_API __declspec(dllimport)
#endif
#else
#define FSIM_VHPI_BRIDGE_CALL
#define FSIM_VHPI_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FSIM_VHPI_STD_HOST_ABI_VERSION 1u

/* The simulator side of the IEEE 1076-2019 clause 22 routines. The
 * standard entry points in the link surface forward each call to the
 * innermost entered host; formatted output arrives already formatted. */
typedef struct fsim_vhpi_std_host_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  void *user_data;
  vhpiHandleT (*register_cb)(void *, vhpiCbDataT *, int32_t);
  int (*remove_cb)(void *, vhpiHandleT);
  int (*disable_cb)(void *, vhpiHandleT);
  int (*enable_cb)(void *, vhpiHandleT);
  int (*get_cb_info)(void *, vhpiHandleT, vhpiCbDataT *);
  vhpiHandleT (*handle_by_name)(void *, const char *, vhpiHandleT);
  vhpiHandleT (*handle_by_index)(void *, vhpiOneToManyT, vhpiHandleT,
                                 int32_t);
  vhpiHandleT (*handle)(void *, vhpiOneToOneT, vhpiHandleT);
  vhpiHandleT (*iterator)(void *, vhpiOneToManyT, vhpiHandleT);
  vhpiHandleT (*scan)(void *, vhpiHandleT);
  vhpiIntT (*get)(void *, vhpiIntPropertyT, vhpiHandleT);
  const vhpiCharT *(*get_str)(void *, vhpiStrPropertyT, vhpiHandleT);
  vhpiRealT (*get_real)(void *, vhpiRealPropertyT, vhpiHandleT);
  vhpiPhysT (*get_phys)(void *, vhpiPhysPropertyT, vhpiHandleT);
  int (*get_value)(void *, vhpiHandleT, vhpiValueT *);
  int (*put_value)(void *, vhpiHandleT, vhpiValueT *, vhpiPutValueModeT);
  int (*schedule_transaction)(void *, vhpiHandleT, vhpiValueT *, uint32_t,
                              vhpiTimeT *, vhpiDelayModeT, vhpiTimeT *);
  int (*format_value)(void *, const vhpiValueT *, vhpiValueT *);
  void (*get_time)(void *, vhpiTimeT *, long *);
  int (*get_next_time)(void *, vhpiTimeT *);
  int (*control)(void *, vhpiSimControlT, int32_t);
  int (*print)(void *, const char *, size_t);
  int (*assertion)(void *, vhpiSeverityT, const char *, size_t);
  int (*compare_handles)(void *, vhpiHandleT, vhpiHandleT);
  int (*check_error)(void *, vhpiErrorInfoT *);
  int (*release_handle)(void *, vhpiHandleT);
  vhpiHandleT (*create)(void *, vhpiClassKindT, vhpiHandleT, vhpiHandleT);
  vhpiHandleT (*register_foreignf)(void *, vhpiForeignDataT *);
  int (*get_foreignf_info)(void *, vhpiHandleT, vhpiForeignDataT *);
  size_t (*get_data)(void *, int32_t, void *, size_t);
  size_t (*put_data)(void *, int32_t, void *, size_t);
  int (*protected_call)(void *, vhpiHandleT, vhpiUserFctT, void *);
} fsim_vhpi_std_host_v1;

FSIM_VHPI_BRIDGE_API int FSIM_VHPI_BRIDGE_CALL
fsim_vhpi_std_host_enter_v1(const fsim_vhpi_std_host_v1 *host);
FSIM_VHPI_BRIDGE_API int FSIM_VHPI_BRIDGE_CALL
fsim_vhpi_std_host_leave_v1(const fsim_vhpi_std_host_v1 *host);

#ifdef __cplusplus
}
#endif

#endif

// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FSIM_UVM_FOREIGN_ABI_VERSION 1u

#if defined(_WIN32)
#define FSIM_UVM_FOREIGN_CALL __cdecl
#else
#define FSIM_UVM_FOREIGN_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fsim_uvm_foreign_status_v1 {
  FSIM_UVM_FOREIGN_OK = 0,
  FSIM_UVM_FOREIGN_INVALID_ARGUMENT = 1,
  FSIM_UVM_FOREIGN_STALE_HANDLE = 2,
  FSIM_UVM_FOREIGN_WRONG_SIMULATION = 3,
  FSIM_UVM_FOREIGN_RESOURCE_LIMIT = 4,
  FSIM_UVM_FOREIGN_BUFFER_TOO_SMALL = 5,
  FSIM_UVM_FOREIGN_CALLBACK_FAILED = 6,
  FSIM_UVM_FOREIGN_INTERNAL_ERROR = 7
} fsim_uvm_foreign_status_v1;

typedef enum fsim_uvm_foreign_record_kind_v1 {
  FSIM_UVM_FOREIGN_PHASE = 1,
  FSIM_UVM_FOREIGN_PHASE_PROCESS = 2,
  FSIM_UVM_FOREIGN_OBJECTION = 3,
  FSIM_UVM_FOREIGN_DRAIN = 4,
  FSIM_UVM_FOREIGN_TLM1_ENDPOINT = 5,
  FSIM_UVM_FOREIGN_TLM1_FIFO = 6,
  FSIM_UVM_FOREIGN_TLM1_OPERATION = 7,
  FSIM_UVM_FOREIGN_TLM2_SOCKET = 8,
  FSIM_UVM_FOREIGN_TLM2_TRANSACTION = 9
} fsim_uvm_foreign_record_kind_v1;

typedef struct fsim_uvm_foreign_snapshot_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t simulation_identity;
  uint64_t generation;
  uint64_t time;
  uint64_t delta;
  uint64_t record_count;
} fsim_uvm_foreign_snapshot_v1;

typedef struct fsim_uvm_foreign_record_v1 {
  uint32_t struct_size;
  uint32_t kind;
  uint32_t state;
  uint32_t flags;
  uint64_t handle;
  uint64_t root;
  uint64_t value;
  uint64_t auxiliary;
  uint64_t identity_size;
  uint64_t detail_size;
  uint64_t payload_size;
} fsim_uvm_foreign_record_v1;

typedef struct fsim_uvm_foreign_activity_v1 {
  uint32_t struct_size;
  uint32_t kind;
  uint32_t action;
  uint32_t reserved;
  uint64_t root;
  uint64_t value;
  uint64_t time;
  uint64_t delta;
  uint64_t sequence;
  uint64_t identity_size;
  const char* identity;
  uint64_t detail_size;
  const char* detail;
} fsim_uvm_foreign_activity_v1;

/* identity/detail pointers are borrowed only for the callback invocation. */
typedef fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL
    *fsim_uvm_foreign_activity_callback_v1)(
        void* context, const fsim_uvm_foreign_activity_v1* event);

typedef fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL
    *fsim_uvm_foreign_capture_v1)(
        void* context, fsim_uvm_foreign_snapshot_v1* snapshot);
typedef fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL
    *fsim_uvm_foreign_copy_record_v1)(
        void* context, const fsim_uvm_foreign_snapshot_v1* snapshot,
        uint64_t index, fsim_uvm_foreign_record_v1* record,
        char* identity, uint64_t identity_capacity,
        char* detail, uint64_t detail_capacity,
        uint8_t* payload, uint64_t payload_capacity);
typedef fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL
    *fsim_uvm_foreign_release_v1)(
        void* context, const fsim_uvm_foreign_snapshot_v1* snapshot);
typedef fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL
    *fsim_uvm_foreign_add_callback_v1)(
        void* context, fsim_uvm_foreign_activity_callback_v1 callback,
        void* callback_context, uint64_t* token);
typedef fsim_uvm_foreign_status_v1(FSIM_UVM_FOREIGN_CALL
    *fsim_uvm_foreign_remove_callback_v1)(void* context, uint64_t token);

/* Append-only host table shared by DPI and VPI integrations. */
typedef struct fsim_uvm_foreign_host_v1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint64_t simulation_identity;
  void* context;
  fsim_uvm_foreign_capture_v1 capture;
  fsim_uvm_foreign_copy_record_v1 copy_record;
  fsim_uvm_foreign_release_v1 release;
  fsim_uvm_foreign_add_callback_v1 add_callback;
  fsim_uvm_foreign_remove_callback_v1 remove_callback;
} fsim_uvm_foreign_host_v1;

#ifdef __cplusplus
}
#endif

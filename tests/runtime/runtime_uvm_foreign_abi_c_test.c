// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_foreign_abi.h"

_Static_assert(FSIM_UVM_FOREIGN_ABI_VERSION == 1u, "foreign ABI version");
_Static_assert(sizeof(fsim_uvm_foreign_snapshot_v1) == 48u,
               "foreign snapshot layout");
_Static_assert(sizeof(fsim_uvm_foreign_record_v1) == 72u,
               "foreign record layout");
_Static_assert(offsetof(fsim_uvm_foreign_snapshot_v1, record_count) == 40u,
               "foreign snapshot record-count offset");
_Static_assert(offsetof(fsim_uvm_foreign_record_v1, payload_size) == 64u,
               "foreign record payload-size offset");
_Static_assert(
    sizeof(fsim_uvm_foreign_host_v1) == 24u + 5u * sizeof(void*),
               "foreign host layout");

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(fsim_uvm_foreign_activity_v1) == 88u,
               "foreign activity x64 layout");
_Static_assert(offsetof(fsim_uvm_foreign_activity_v1, identity) == 64u,
               "foreign activity identity offset");
_Static_assert(offsetof(fsim_uvm_foreign_activity_v1, detail) == 80u,
               "foreign activity detail offset");
_Static_assert(sizeof(fsim_uvm_foreign_host_v1) == 64u,
               "foreign host x64 layout");
_Static_assert(offsetof(fsim_uvm_foreign_host_v1, capture) == 24u,
               "foreign host capture offset");
_Static_assert(offsetof(fsim_uvm_foreign_host_v1, remove_callback) == 56u,
               "foreign host append-only tail offset");
#endif

static fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL
fsim_uvm_foreign_abi_callback(void* context,
                              const fsim_uvm_foreign_activity_v1* event) {
  return context == event ? FSIM_UVM_FOREIGN_INVALID_ARGUMENT
                          : FSIM_UVM_FOREIGN_OK;
}

int fsim_uvm_foreign_abi_c_probe(void) {
  fsim_uvm_foreign_snapshot_v1 snapshot = {0};
  fsim_uvm_foreign_activity_callback_v1 callback =
      fsim_uvm_foreign_abi_callback;
  snapshot.struct_size = (uint32_t)sizeof(snapshot);
  return callback == 0 ? 0 : (int)snapshot.struct_size;
}

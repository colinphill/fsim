// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_foreign_abi.h"

#define FSIM_UVM_LAYOUT(type, size, alignment)             \
    _Static_assert(sizeof(type) == (size), #type " size"); \
    _Static_assert(_Alignof(type) == (alignment), #type " alignment")
#define FSIM_UVM_OFFSET(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), #type " " #field)

_Static_assert(FSIM_UVM_FOREIGN_ABI_VERSION == 1u, "foreign ABI version");
_Static_assert(sizeof(void*) == 8u, "foreign ABI requires x86-64 pointers");
_Static_assert(
    sizeof(fsim_uvm_foreign_status_v1) == 4u,
    "foreign status enum width");
_Static_assert(
    sizeof(fsim_uvm_foreign_record_kind_v1) == 4u,
    "foreign record-kind enum width");

FSIM_UVM_LAYOUT(fsim_uvm_foreign_snapshot_v1, 48u, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_snapshot_v1, abi_version, 0u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_snapshot_v1, struct_size, 4u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_snapshot_v1, simulation_identity, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_snapshot_v1, generation, 16u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_snapshot_v1, time, 24u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_snapshot_v1, delta, 32u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_snapshot_v1, record_count, 40u);

FSIM_UVM_LAYOUT(fsim_uvm_foreign_record_v1, 72u, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, struct_size, 0u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, kind, 4u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, state, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, flags, 12u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, handle, 16u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, root, 24u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, value, 32u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, auxiliary, 40u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, identity_size, 48u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, detail_size, 56u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_record_v1, payload_size, 64u);

#if UINTPTR_MAX == UINT64_MAX
FSIM_UVM_LAYOUT(fsim_uvm_foreign_activity_v1, 88u, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, struct_size, 0u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, kind, 4u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, action, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, reserved, 12u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, root, 16u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, value, 24u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, time, 32u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, delta, 40u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, sequence, 48u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, identity_size, 56u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, identity, 64u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, detail_size, 72u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_activity_v1, detail, 80u);

FSIM_UVM_LAYOUT(fsim_uvm_foreign_host_v1, 64u, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, abi_version, 0u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, struct_size, 4u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, simulation_identity, 8u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, context, 16u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, capture, 24u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, copy_record, 32u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, release, 40u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, add_callback, 48u);
FSIM_UVM_OFFSET(fsim_uvm_foreign_host_v1, remove_callback, 56u);
#endif

static fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL
fsim_uvm_foreign_abi_callback(void* context,
    const fsim_uvm_foreign_activity_v1* event)
{
    return context == event ? FSIM_UVM_FOREIGN_INVALID_ARGUMENT
                            : FSIM_UVM_FOREIGN_OK;
}

int fsim_uvm_foreign_abi_c_probe(void)
{
    fsim_uvm_foreign_snapshot_v1 snapshot = { 0 };
    fsim_uvm_foreign_activity_callback_v1 callback = fsim_uvm_foreign_abi_callback;
    snapshot.struct_size = (uint32_t)sizeof(snapshot);
    return callback == 0 ? 0 : (int)snapshot.struct_size;
}

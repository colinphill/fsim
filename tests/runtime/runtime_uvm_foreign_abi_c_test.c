// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_foreign_abi.h"

_Static_assert(FSIM_UVM_FOREIGN_ABI_VERSION == 1u, "foreign ABI version");
_Static_assert(sizeof(fsim_uvm_foreign_snapshot_v1) == 48u,
               "foreign snapshot layout");
_Static_assert(sizeof(fsim_uvm_foreign_record_v1) == 72u,
               "foreign record layout");
_Static_assert(
    sizeof(fsim_uvm_foreign_host_v1) == 24u + 5u * sizeof(void*),
               "foreign host layout");

int fsim_uvm_foreign_abi_c_probe(void) {
  fsim_uvm_foreign_snapshot_v1 snapshot = {0};
  snapshot.struct_size = (uint32_t)sizeof(snapshot);
  return (int)snapshot.struct_size;
}

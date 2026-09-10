// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/tf_plugin_abi.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/vpi_bridge.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

_Static_assert(sizeof(vpiHandle) == sizeof(void*), "installed VPI handle");
_Static_assert(sizeof(s_vpi_time) == 24u, "installed VPI time layout");
_Static_assert(sizeof(s_vpi_value) == 16u, "installed VPI value layout");
_Static_assert(vpiPackage == 600, "installed SystemVerilog object identity");
_Static_assert(vpiTrvsObj == 800, "installed reader object identity");

int main(void) {
  s_vecval value = {0, 0};
  fsim_tf_registration_v3 registration = {0};
  registration.struct_size = (uint32_t)sizeof(registration);
  registration.kind = FSIM_TF_REGISTRATION_TASK;
  fsim_tf_registration_table_v3 table = {
      FSIM_TF_REGISTRATION_TABLE_ABI_VERSION,
      (uint32_t)sizeof(fsim_tf_registration_table_v3),
      0,
      1,
      (uint32_t)sizeof(fsim_tf_registration_v3),
      0,
      &registration,
  };
  return FSIM_NATIVE_PLUGIN_ABI_VERSION == 3u &&
                 table.abi_version == 3u && table.entry_count == 1u &&
                 sizeof(value.avalbits) == sizeof(PLI_INT32) &&
                 fsim_vpi_current_call_context_v1() == 0 &&
                 vpi_get(vpiSize, (vpiHandle)1) == vpiUndefined &&
                 tf_getp(1) == 0 && tf_igetp(1, 0) == 0 &&
                 tf_getinstance() == 0 && tf_synchronize() != 0
             ? 0
             : 1;
}

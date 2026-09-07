// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/tf_plugin_abi.h"
#include "fsim/runtime/veriuser.h"

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
                 tf_getp(1) == 0 && tf_igetp(1, 0) == 0 &&
                 tf_getinstance() == 0 && tf_synchronize() != 0
             ? 0
             : 1;
}

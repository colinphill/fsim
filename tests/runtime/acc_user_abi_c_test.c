// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include <stddef.h>

_Static_assert(sizeof(HANDLE) == sizeof(void*), "ACC handle width changed");
_Static_assert(sizeof(handle) == sizeof(void*), "ACC handle alias changed");
_Static_assert(sizeof(s_acc_vecval) == 8, "ACC vector word layout changed");
_Static_assert(offsetof(s_acc_vecval, bval) == 4,
               "ACC unknown-mask offset changed");
_Static_assert(offsetof(s_acc_time, type) == 0, "ACC time type moved");
_Static_assert(offsetof(s_setval_delay, time) == 0,
               "ACC delay time moved");
_Static_assert(offsetof(s_setval_value, format) == 0,
               "ACC value format moved");
_Static_assert(offsetof(s_vc_record, vc_reason) == 0,
               "ACC callback reason moved");
_Static_assert(sizeof(s_strengths) == 3, "ACC strength layout changed");
_Static_assert(sizeof(s_timescale_info) == 4,
               "ACC timescale layout changed");

int fsim_acc_user_abi_c_test(void) {
  s_acc_vecval vector = {1, 2};
  s_setval_value value = {0};
  s_setval_delay delay = {0};
  s_vc_record record = {0};

  value.format = accVectorVal;
  value.value.vector = &vector;
  delay.time.type = accSimTime;
  delay.model = accInertialDelay;
  record.vc_reason = vector_value_change;
  record.out_value.vector_handle = (handle)0;

  return value.value.vector->aval == 1 && value.value.vector->bval == 2 &&
         delay.time.type == 2 && delay.model == 1 &&
         record.vc_reason == 4 && accModule == 20 && accNet == 25 &&
         accTimeVar == 283 && accSetuphold == 377 && accMinTypMax == 696 &&
         VCL_VERILOG_STRENGTH == 3;
}

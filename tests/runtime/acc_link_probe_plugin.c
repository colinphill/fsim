// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#if defined(_WIN32)
#define FSIM_ACC_PROBE_EXPORT __declspec(dllexport)
#else
#define FSIM_ACC_PROBE_EXPORT __attribute__((visibility("default")))
#endif

FSIM_ACC_PROBE_EXPORT PLI_INT32 fsim_acc_link_probe(void) {
  char enabled[] = "true";

  if (acc_initialize() != 1 || acc_error_flag != 0 ||
      acc_product_type() != accSimulator || acc_product_version() == 0 ||
      acc_version() == 0) {
    acc_close();
    return 1;
  }
  if (acc_configure(7, enabled) != 0 || acc_error_flag != 1) {
    acc_close();
    return 2;
  }
  if (acc_configure(accDisplayWarnings, enabled) != 1 ||
      acc_error_flag != 0) {
    acc_close();
    return 3;
  }
  acc_reset_buffer();
  if (acc_error_flag != 0) {
    acc_close();
    return 4;
  }
  acc_close();
  return acc_error_flag == 0 ? 182 : 5;
}

// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include <string_view>

#if defined(_WIN32)
#define FSIM_ACC_PROBE_EXPORT __declspec(dllexport)
#else
#define FSIM_ACC_PROBE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" FSIM_ACC_PROBE_EXPORT PLI_INT32 fsim_acc_cpp_probe() {
  char enabled[] = "true";
  if (acc_initialize() != 1 || acc_error_flag != 0 ||
      std::string_view{acc_version()} != "IEEE 1364-2005 ACC" ||
      std::string_view{acc_product_version()}.empty()) {
    acc_close();
    return 1;
  }
  if (acc_configure(accEnableArgs, enabled) != 1 || acc_error_flag != 0) {
    acc_close();
    return 2;
  }
  if (acc_handle_by_name(nullptr, nullptr) != nullptr ||
      acc_error_flag != 1) {
    acc_close();
    return 3;
  }
  if (acc_product_type() != accSimulator || acc_error_flag != 0) {
    acc_close();
    return 4;
  }
  acc_close();
  return acc_error_flag == 0 ? 183 : 5;
}

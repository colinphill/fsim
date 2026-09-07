// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include <cstddef>
#include <iostream>
#include <type_traits>

extern "C" int fsim_acc_user_abi_c_test(void);

namespace {

using HandleByName = handle (*)(PLI_BYTE8*, handle);
using FetchValue = PLI_BYTE8* (*)(handle, PLI_BYTE8*, p_acc_value);
using SetValue = PLI_INT32 (*)(handle, p_setval_value, p_setval_delay);
using ValueChange = void (*)(handle, PLI_INT32 (*)(p_vc_record),
                             PLI_BYTE8*, PLI_INT32);

static_assert(sizeof(PLI_INT32) == 4);
static_assert(sizeof(PLI_INT16) == 2);
static_assert(sizeof(s_acc_vecval) == 8);
static_assert(offsetof(s_acc_vecval, bval) == 4);
static_assert(std::is_standard_layout_v<s_acc_time>);
static_assert(std::is_standard_layout_v<s_setval_delay>);
static_assert(std::is_standard_layout_v<s_setval_value>);
static_assert(std::is_standard_layout_v<s_vc_record>);
static_assert(std::is_same_v<decltype(&acc_handle_by_name), HandleByName>);
static_assert(std::is_same_v<decltype(&acc_fetch_value), FetchValue>);
static_assert(std::is_same_v<decltype(&acc_set_value), SetValue>);
static_assert(std::is_same_v<decltype(&acc_vcl_add), ValueChange>);
static_assert(std::is_same_v<decltype(&acc_vcl_delete), ValueChange>);
static_assert(accRegister == accReg);
static_assert(accIntParam == accIntegerParam);
static_assert(accNoChange == accNochange);
static_assert(accVectorVal == 10);
static_assert(accPureTransportDelay == 3);
static_assert(accMinTypMax == 696);

#if defined(bool) || defined(true) || defined(false)
#error "acc_user.h must not hide C++ keywords"
#endif

}  // namespace

int main() {
  if (fsim_acc_user_abi_c_test() == 0) {
    std::cerr << "C ACC ABI witness failed\n";
    return 1;
  }
  return 0;
}

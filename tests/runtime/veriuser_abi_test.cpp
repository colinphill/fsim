// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/veriuser.h"

#include <cstddef>
#include <iostream>
#include <type_traits>

extern "C" int fsim_veriuser_abi_c_test(void);

namespace {

using GetParameter = PLI_INT32 (*)(PLI_INT32);
using GetInstanceParameter = PLI_INT32 (*)(PLI_INT32, PLI_BYTE8*);
using ExpressionInfo = p_tfexprinfo (*)(PLI_INT32, p_tfexprinfo);
using NodeInfo = p_tfnodeinfo (*)(PLI_INT32, p_tfnodeinfo);

static_assert(sizeof(PLI_INT32) == 4);
static_assert(sizeof(PLI_UINT32) == 4);
static_assert(sizeof(PLI_INT16) == 2);
static_assert(sizeof(PLI_UINT16) == 2);
static_assert(sizeof(PLI_BYTE8) == 1);
static_assert(sizeof(s_vecval) == 8);
static_assert(offsetof(s_vecval, bvalbits) == 4);
static_assert(std::is_standard_layout_v<s_tfexprinfo>);
static_assert(std::is_standard_layout_v<s_tfnodeinfo>);
static_assert(std::is_same_v<decltype(&tf_getp), GetParameter>);
static_assert(std::is_same_v<decltype(&tf_igetp), GetInstanceParameter>);
static_assert(std::is_same_v<decltype(&tf_exprinfo), ExpressionInfo>);
static_assert(std::is_same_v<decltype(&tf_nodeinfo), NodeInfo>);
static_assert(reason_checktf == REASON_CHECKTF);
static_assert(reason_endofcompile == REASON_ENDOFCOMPILE);
static_assert(reason_startofsave == 27);
static_assert(reason_startofrestart == 28);
static_assert(tf_readonlyreal == TF_READONLYREAL);
static_assert(tf_real_node == TF_REAL_NODE);

#if defined(bool) || defined(true) || defined(false)
#error "veriuser.h must not hide C++ keywords"
#endif

}  // namespace

int main() {
  if (fsim_veriuser_abi_c_test() == 0) {
    std::cerr << "C veriuser ABI witness failed\n";
    return 1;
  }
  return 0;
}

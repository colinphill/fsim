// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/veriuser.h"

#include <stddef.h>

_Static_assert(sizeof(PLI_INT32) == 4, "PLI_INT32 must be 32 bits");
_Static_assert(sizeof(PLI_UINT32) == 4, "PLI_UINT32 must be 32 bits");
_Static_assert(sizeof(PLI_INT16) == 2, "PLI_INT16 must be 16 bits");
_Static_assert(sizeof(PLI_UINT16) == 2, "PLI_UINT16 must be 16 bits");
_Static_assert(sizeof(PLI_BYTE8) == 1, "PLI_BYTE8 must be one byte");
_Static_assert(sizeof(s_vecval) == 8, "vector word layout changed");
_Static_assert(offsetof(s_vecval, bvalbits) == 4,
               "vector unknown-mask offset changed");
_Static_assert(sizeof(s_strengthval) == 8, "strength layout changed");
_Static_assert(offsetof(s_tfexprinfo, expr_type) == 0,
               "expression type offset changed");
_Static_assert(offsetof(s_tfexprinfo, expr_value_p) >= 4,
               "expression vector pointer offset changed");
_Static_assert(offsetof(s_tfnodeinfo, node_type) == 0,
               "node type offset changed");
_Static_assert(offsetof(s_tfnodeinfo, node_value) >= 4,
               "node value offset changed");

int fsim_veriuser_abi_c_test(void) {
  s_vecval value = {1, 2};
  s_strengthval strength = {3, 4};
  s_tfexprinfo expression = {0};
  s_tfnodeinfo node = {0};

  expression.expr_type = tf_readwrite;
  expression.expr_value_p = &value;
  node.node_type = tf_netscalar_node;
  node.node_value.strengthval_p = &strength;

  return expression.expr_type == TF_READWRITE &&
         expression.expr_value_p->bvalbits == 2 &&
         node.node_type == TF_NETSCALAR_NODE &&
         node.node_value.strengthval_p->strength1 == 4 &&
         reason_rosynch == REASON_ROSYNCH && REASON_MAX == 28 &&
         ERR_SYSTEM == 5;
}

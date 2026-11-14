// SPDX-License-Identifier: Apache-2.0
// Plug-in for vpiWith of array method calls (IEEE 1800-2023 37.42 detail 1).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include <stdio.h>
#include <string.h>
static PLI_INT32 start(p_cb_data d)
{
    vpiHandle m = vpi_handle_by_name("top", NULL), it, p, body, si, s; (void)d;
    it = vpi_iterate(vpiProcess, m);
    while (it && (p = vpi_scan(it))) {
        body = vpi_handle(vpiStmt, p);
        si = body ? vpi_iterate(vpiStmt, body) : NULL;
        while (si && (s = vpi_scan(si))) {
            vpiHandle rhs = vpi_handle(vpiRhs, s);
            if (!rhs) continue;
            vpiHandle w = vpi_handle(vpiWith, rhs);
            vpiHandle pre = vpi_handle(vpiPrefix, rhs);
            vpi_printf("#c line %d rhs type %d name %s prefix %s with %s\n", (int)vpi_get(vpiLineNo, s), (int)vpi_get(vpiType, rhs),
              vpi_get_str(vpiName, rhs) ? vpi_get_str(vpiName, rhs) : "-", pre ? vpi_get_str(vpiName, pre) : "-", w ? "yes" : "none");
            if (w) { vpiHandle o = vpi_iterate(vpiOperand, w); int n = 0; while (o && vpi_scan(o)) n++; vpi_printf("#c   with type %d operands %d\n", (int)vpi_get(vpiType, w), n); }
        }
    }
    return 0;
}
static void reg(void) { s_cb_data c; memset(&c, 0, sizeof c); c.reason = cbStartOfSimulation; c.cb_rtn = start; vpi_register_cb(&c); }
FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { reg, 0 };

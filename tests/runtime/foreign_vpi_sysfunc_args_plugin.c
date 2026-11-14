// SPDX-License-Identifier: Apache-2.0
// Plug-in for the arguments of system function calls over automatic variables
// (IEEE 1800-2023 37.42 detail 7, 37.43): at each assignment of task t the
// value of the call and of its arguments is read. Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include <stdio.h>
#include <string.h>
static void val(const char* tag, vpiHandle h)
{
    s_vpi_value v; memset(&v, 0, sizeof v); v.format = vpiIntVal;
    vpi_get_value(h, &v);
    vpi_printf("#c %s type=%d value=%s%d\n", tag, (int)vpi_get(vpiType, h), v.format == vpiSuppressVal ? "(none)" : "", v.format == vpiSuppressVal ? 0 : (int)v.value.integer);
}
static PLI_INT32 stmt(p_cb_data d)
{
    vpiHandle rhs = vpi_handle(vpiRhs, d->obj);
    vpiHandle it, a; (void)d;
    if (!rhs) return 0;
    vpi_printf("#c stmt line %d rhs type %d\n", (int)vpi_get(vpiLineNo, d->obj), (int)vpi_get(vpiType, rhs));
    val("rhs", rhs);
    if (vpi_get(vpiType, rhs) == vpiSysFuncCall) {
        it = vpi_iterate(vpiArgument, rhs);
        while (it && (a = vpi_scan(it))) val("  arg", a);
    } else {
        it = vpi_iterate(vpiOperand, rhs);
        while (it && (a = vpi_scan(it))) { val("  operand", a); vpiHandle it2 = vpi_iterate(vpiArgument, a), b; while (it2 && (b = vpi_scan(it2))) val("    arg", b); }
    }
    return 0;
}
static PLI_INT32 start(p_cb_data d)
{
    vpiHandle t = vpi_handle_by_name("top.t", NULL), body, it, s; (void)d;
    body = vpi_handle(vpiStmt, t);
    it = vpi_iterate(vpiStmt, body);
    while (it && (s = vpi_scan(it))) {
        s_cb_data c; memset(&c, 0, sizeof c); c.reason = cbStmt; c.obj = s; c.cb_rtn = stmt; vpi_register_cb(&c);
    }
    return 0;
}
static void reg(void) { s_cb_data c; memset(&c, 0, sizeof c); c.reason = cbStartOfSimulation; c.cb_rtn = start; vpi_register_cb(&c); }
FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { reg, 0 };

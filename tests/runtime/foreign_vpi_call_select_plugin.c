// SPDX-License-Identifier: Apache-2.0
// Selects of a function call's result (IEEE 1800-2023 37.59): at time 1,
// prints the type, parent type and value of the right-hand side of each
// assignment of top.main. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static PLI_INT32 report(p_cb_data data)
{
    vpiHandle block = vpi_handle_by_name("top.main", NULL);
    vpiHandle list = block != NULL ? vpi_iterate(vpiStmt, block) : NULL;
    vpiHandle statement;
    (void)data;
    while (list != NULL && (statement = vpi_scan(list)) != NULL) {
        vpiHandle rhs = vpi_handle(vpiRhs, statement);
        vpiHandle parent;
        s_vpi_value value;
        if (rhs == NULL) {
            continue;
        }
        parent = vpi_handle(vpiParent, rhs);
        memset(&value, 0, sizeof value);
        value.format = vpiBinStrVal;
        vpi_get_value(rhs, &value);
        /* The select of a call's result names no declared object: 37.59
           gives its expression kinds no vpiName or vpiFullName. */
        vpi_printf("#c line %d: type=%d parent=%d value=%s name=%s full=%s\n",
            (int)vpi_get(vpiLineNo, statement), (int)vpi_get(vpiType, rhs),
            parent != NULL ? (int)vpi_get(vpiType, parent) : 0,
            value.value.str != NULL ? value.value.str : "none",
            vpi_get_str(vpiName, rhs) != NULL ? "named" : "none",
            vpi_get_str(vpiFullName, rhs) != NULL ? "named" : "none");
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time delay = { vpiSimTime, 0, 1, 0.0 };
    s_cb_data later;
    (void)data;
    memset(&later, 0, sizeof later);
    later.reason = cbAfterDelay;
    later.cb_rtn = report;
    later.time = &delay;
    (void)vpi_register_cb(&later);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

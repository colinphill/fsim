// SPDX-License-Identifier: Apache-2.0
// The typespec of a cast to a user-defined type (IEEE 1800-2023 6.24.1,
// 37.59): at time 1, prints for the right-hand side of each assignment of
// top.main the cast's typespec (name, kind, range) and its value.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static int bound(vpiHandle spec, int which)
{
    s_vpi_value value;
    vpiHandle constant = vpi_handle(which, spec);
    if (constant == NULL) {
        return -1;
    }
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    vpi_get_value(constant, &value);
    return (int)value.value.integer;
}

static PLI_INT32 report(p_cb_data data)
{
    vpiHandle block = vpi_handle_by_name("top.main", NULL);
    vpiHandle list = block != NULL ? vpi_iterate(vpiStmt, block) : NULL;
    vpiHandle statement;
    (void)data;
    while (list != NULL && (statement = vpi_scan(list)) != NULL) {
        vpiHandle rhs = vpi_handle(vpiRhs, statement);
        vpiHandle spec = rhs != NULL ? vpi_handle(vpiTypespec, rhs) : NULL;
        s_vpi_value value;
        if (spec == NULL) {
            vpi_printf("#c line %d: no typespec\n",
                (int)vpi_get(vpiLineNo, statement));
            continue;
        }
        memset(&value, 0, sizeof value);
        value.format = vpiBinStrVal;
        vpi_get_value(rhs, &value);
        vpi_printf("#c line %d: %s type=%d [%d:%d] value=%s\n",
            (int)vpi_get(vpiLineNo, statement), vpi_get_str(vpiName, spec),
            (int)vpi_get(vpiType, spec), bound(spec, vpiLeftRange),
            bound(spec, vpiRightRange),
            value.value.str != NULL ? value.value.str : "none");
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

// SPDX-License-Identifier: Apache-2.0
// The values of cast operations (IEEE 1800-2023 6.24.1, 37.78): at time 1,
// prints the vpiOpType and the value of the right-hand side of each
// assignment of top.main. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static PLI_INT32 report(p_cb_data data)
{
    vpiHandle block = vpi_handle_by_name("top.main", NULL);
    vpiHandle statements = block != NULL ? vpi_iterate(vpiStmt, block) : NULL;
    vpiHandle statement;
    (void)data;
    if (statements == NULL) {
        vpi_printf("#c missing top.main\n");
        return 0;
    }
    while ((statement = vpi_scan(statements)) != NULL) {
        vpiHandle rhs = vpi_handle(vpiRhs, statement);
        s_vpi_value value;
        const int line = (int)vpi_get(vpiLineNo, statement);
        if (rhs == NULL || vpi_get(vpiType, rhs) != vpiOperation) {
            vpi_printf("#c line %d: no operation\n", line);
            continue;
        }
        memset(&value, 0, sizeof value);
        value.format = vpiObjTypeVal;
        vpi_get_value(rhs, &value);
        if (value.format == vpiRealVal) {
            vpi_printf("#c line %d op=%d real=%g\n", line,
                (int)vpi_get(vpiOpType, rhs), value.value.real);
            continue;
        }
        if (value.format == 0 || value.format == vpiSuppressVal) {
            vpi_printf("#c line %d op=%d no value\n", line,
                (int)vpi_get(vpiOpType, rhs));
            continue;
        }
        memset(&value, 0, sizeof value);
        value.format = vpiBinStrVal;
        vpi_get_value(rhs, &value);
        vpi_printf("#c line %d op=%d bin=%s\n", line,
            (int)vpi_get(vpiOpType, rhs),
            value.value.str != NULL ? value.value.str : "(null)");
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

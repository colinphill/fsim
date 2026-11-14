// SPDX-License-Identifier: Apache-2.0
// Clocking blocks named through a virtual interface (IEEE 1800-2023 37.48):
// at time 1, prints for each event control of top.main the clocking block it
// waits on, its prefix and its actual. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static PLI_INT32 report(p_cb_data data)
{
    vpiHandle block = vpi_handle_by_name("top.main", NULL);
    vpiHandle list = block != NULL ? vpi_iterate(vpiStmt, block) : NULL;
    vpiHandle statement;
    (void)data;
    while (list != NULL && (statement = vpi_scan(list)) != NULL) {
        vpiHandle condition;
        vpiHandle prefix;
        vpiHandle actual;
        if (vpi_get(vpiType, statement) != vpiEventControl) {
            continue;
        }
        condition = vpi_handle(vpiCondition, statement);
        if (condition != NULL && vpi_get(vpiType, condition) == vpiOperation) {
            /* The event of the control is the operation's operand. */
            vpiHandle operands = vpi_iterate(vpiOperand, condition);
            condition = operands != NULL ? vpi_scan(operands) : NULL;
        }
        if (condition == NULL || vpi_get(vpiType, condition) != vpiClockingBlock) {
            vpi_printf("#c line %d: no clocking block (%d)\n",
                (int)vpi_get(vpiLineNo, statement),
                condition != NULL ? (int)vpi_get(vpiType, condition) : 0);
            continue;
        }
        prefix = vpi_handle(vpiPrefix, condition);
        actual = vpi_handle(vpiActual, condition);
        vpi_printf("#c line %d: %s %s prefix=%s(%d) actual=%s\n",
            (int)vpi_get(vpiLineNo, statement),
            text(vpi_get_str(vpiName, condition)),
            text(vpi_get_str(vpiFullName, condition)),
            prefix != NULL ? text(vpi_get_str(vpiFullName, prefix)) : "(none)",
            prefix != NULL ? (int)vpi_get(vpiType, prefix) : 0,
            actual != NULL ? text(vpi_get_str(vpiFullName, actual)) : "(none)");
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

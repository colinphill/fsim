// SPDX-License-Identifier: Apache-2.0
// Prints the clocking event of concurrent assertions whose clock is inferred
// from the default clocking or from an always procedure, and of one that
// names its clock (IEEE 1800-2023 16.14.6, 37.50). Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = vpi_get_str(property, object);
    return value != NULL ? value : "";
}

static void clock_of(const char* name)
{
    vpiHandle const assertion = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    vpiHandle clock;
    char event[96] = "none";
    if (assertion == NULL) {
        vpi_printf("#c clock %s missing\n", name);
        return;
    }
    clock = vpi_handle(vpiClockingEvent, assertion);
    if (clock != NULL) {
        vpiHandle const operands = vpi_iterate(vpiOperand, clock);
        vpiHandle const first = operands != NULL ? vpi_scan(operands) : NULL;
        const PLI_INT32 op = vpi_get(vpiOpType, clock);
        snprintf(event, sizeof event, "%s(%s)",
            op == vpiPosedgeOp ? "posedge"
                : op == vpiNegedgeOp ? "negedge" : "other",
            first != NULL ? text(vpiName, first) : "");
        if (operands != NULL && first != NULL) {
            vpi_free_object(operands);
        }
    }
    vpi_printf("#c clock %s inferred=%d event=%s\n", name,
        (int)vpi_get(vpiIsClockInferred, assertion), event);
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    clock_of("top.p_def");
    clock_of("top.p_proc");
    clock_of("top.p_own");
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

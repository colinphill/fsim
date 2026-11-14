// SPDX-License-Identifier: Apache-2.0
// Stateful system function calls at one line and column of two source files
// (IEEE 1800-2023 38.15, 37.59): at the end of the run, the size, sign and
// value of the call of each instance. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static void report(const char* instance_name)
{
    vpiHandle const instance = vpi_handle_by_name((PLI_BYTE8*)instance_name, NULL);
    vpiHandle const processes
        = instance != NULL ? vpi_iterate(vpiProcess, instance) : NULL;
    vpiHandle process;
    while (processes != NULL && (process = vpi_scan(processes)) != NULL) {
        /* A block of one statement is that statement. */
        vpiHandle const body = vpi_handle(vpiStmt, process);
        vpiHandle const list = body != NULL && vpi_get(vpiType, body) != vpiAssignment
            ? vpi_iterate(vpiStmt, body) : NULL;
        vpiHandle statement = list == NULL ? body : NULL;
        while (statement != NULL
            || (list != NULL && (statement = vpi_scan(list)) != NULL)) {
            vpiHandle const rhs = vpi_handle(vpiRhs, statement);
            vpiHandle const lhs = vpi_handle(vpiLhs, statement);
            s_vpi_value called;
            s_vpi_value stored;
            if (rhs != NULL && lhs != NULL
                && vpi_get(vpiType, rhs) == vpiSysFuncCall) {
                memset(&called, 0, sizeof called);
                memset(&stored, 0, sizeof stored);
                called.format = vpiDecStrVal;
                stored.format = vpiDecStrVal;
                vpi_get_value(rhs, &called);
                vpi_get_value(lhs, &stored);
                vpi_printf("#c %s signed=%d size=%d value=%s\n", instance_name,
                    (int)vpi_get(vpiSigned, rhs), (int)vpi_get(vpiSize, rhs),
                    called.format == vpiDecStrVal && called.value.str != NULL
                            && stored.value.str != NULL
                            && strcmp(called.value.str, stored.value.str) == 0
                        ? "same-as-target"
                        : "differs");
            }
            if (list == NULL) {
                break;
            }
            statement = NULL;
        }
    }
}

static PLI_INT32 after(p_cb_data data)
{
    (void)data;
    report("top.u1");
    report("top.u2");
    report("top.u3");
    report("top.u4");
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbEndOfSimulation;
    data.cb_rtn = after;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

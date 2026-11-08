// SPDX-License-Identifier: Apache-2.0
// Value of user function calls (IEEE 1800-2023 37.42, 38.15): reads the
// right-hand side of each assignment of task top.calls before the first
// evaluation and at the end of the run. A run without the execution probes
// reports an error that names them. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static void report(const char* tag)
{
    vpiHandle const task = vpi_handle_by_name("top.calls", NULL);
    vpiHandle const body = task != NULL ? vpi_handle(vpiStmt, task) : NULL;
    vpiHandle const items = body != NULL ? vpi_iterate(vpiStmt, body) : NULL;
    vpiHandle item;
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpiHandle const rhs = vpi_handle(vpiRhs, item);
        int const line = (int)vpi_get(vpiLineNo, item);
        s_vpi_value value;
        s_vpi_error_info error;
        char text[64] = "none";
        memset(&value, 0, sizeof value);
        memset(&error, 0, sizeof error);
        value.format = line == 31 ? vpiRealVal : vpiDecStrVal;
        if (rhs != NULL) {
            vpi_get_value(rhs, &value);
        }
        if (value.format == vpiRealVal) {
            snprintf(text, sizeof text, "%g", value.value.real);
        } else if (value.format == vpiDecStrVal && value.value.str != NULL) {
            snprintf(text, sizeof text, "%s", value.value.str);
        }
        vpi_printf("#c %s line=%d value=%s error=%s\n", tag, line, text,
            vpi_chk_error(&error) == 0 ? "none"
                : strstr(error.message, "--foreign-probes") != NULL
                ? "names-option"
                : "other");
    }
}

static PLI_INT32 before(p_cb_data data)
{
    (void)data;
    report("before");
    return 0;
}

static PLI_INT32 after(p_cb_data data)
{
    (void)data;
    report("after");
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = before;
    (void)vpi_register_cb(&data);
    memset(&data, 0, sizeof data);
    data.reason = cbEndOfSimulation;
    data.cb_rtn = after;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

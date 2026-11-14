// SPDX-License-Identifier: Apache-2.0
// Value of system function calls with state (IEEE 1800-2023 38.15, 37.58):
// reads the right-hand side of each assignment of task top.draws before the
// first evaluation and at the end of the run, and compares it with the value
// the assignment stored in its target. A run without the execution probes
// reports an error that names them. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void report(const char* tag)
{
    vpiHandle const task = vpi_handle_by_name("top.draws", NULL);
    vpiHandle const body = task != NULL ? vpi_handle(vpiStmt, task) : NULL;
    vpiHandle const items = body != NULL ? vpi_iterate(vpiStmt, body) : NULL;
    vpiHandle item;
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpiHandle const rhs = vpi_handle(vpiRhs, item);
        vpiHandle const lhs = vpi_handle(vpiLhs, item);
        int const line = (int)vpi_get(vpiLineNo, item);
        s_vpi_value called;
        s_vpi_value stored;
        s_vpi_error_info error;
        char text[64] = "none";
        int failed;
        memset(&called, 0, sizeof called);
        memset(&stored, 0, sizeof stored);
        memset(&error, 0, sizeof error);
        called.format = vpiDecStrVal;
        stored.format = vpiDecStrVal;
        if (rhs != NULL) {
            vpi_get_value(rhs, &called);
        }
        /* The error of the call, before the target is read. */
        failed = vpi_chk_error(&error);
        if (lhs != NULL) {
            vpi_get_value(lhs, &stored);
        }
        if (called.format == vpiDecStrVal && called.value.str != NULL) {
            snprintf(text, sizeof text, "%s",
                stored.format == vpiDecStrVal && stored.value.str != NULL
                        && strcmp(called.value.str, stored.value.str) == 0
                    ? "same-as-target"
                    : "differs");
            if (line == 24) {
                strcat(text, atoi(called.value.str) < 0 ? " negative"
                                                         : " not-negative");
            }
            if (line == 22) {
                int const number = atoi(called.value.str);
                strcat(text, number >= 5 && number <= 9 ? " in-range"
                                                        : " out-of-range");
            }
        }
        vpi_printf("#c %s line=%d signed=%d size=%d value=%s error=%s\n", tag,
            line, rhs != NULL ? (int)vpi_get(vpiSigned, rhs) : -2,
            rhs != NULL ? (int)vpi_get(vpiSize, rhs) : -2, text,
            failed == 0 ? "none"
                : strstr(error.message, "--foreign-probes") != NULL
                ? "names-option"
                : "other");
    }
}

/* The call in the body of package function pk::draw, whose last call was the
   assignment to top.w. */
static void package_call(const char* tag)
{
    vpiHandle const function = vpi_handle_by_name("pk::draw", NULL);
    vpiHandle const body = function != NULL ? vpi_handle(vpiStmt, function) : NULL;
    vpiHandle const items = body != NULL ? vpi_iterate(vpiStmt, body) : NULL;
    vpiHandle const target = vpi_handle_by_name("top.w", NULL);
    vpiHandle item;
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpiHandle const rhs = vpi_handle(vpiRhs, item);
        s_vpi_value called;
        s_vpi_value stored;
        const char* text = "none";
        if (rhs == NULL || vpi_get(vpiType, rhs) != vpiSysFuncCall) {
            continue;
        }
        memset(&called, 0, sizeof called);
        memset(&stored, 0, sizeof stored);
        called.format = vpiDecStrVal;
        stored.format = vpiDecStrVal;
        vpi_get_value(rhs, &called);
        if (target != NULL) {
            vpi_get_value(target, &stored);
        }
        if (called.format == vpiDecStrVal && called.value.str != NULL) {
            text = stored.format == vpiDecStrVal && stored.value.str != NULL
                    && strcmp(called.value.str, stored.value.str) == 0
                ? "same-as-last-caller"
                : "differs";
        }
        vpi_printf("#c %s package-call value=%s\n", tag, text);
    }
}

static PLI_INT32 before(p_cb_data data)
{
    (void)data;
    report("before");
    package_call("before");
    return 0;
}

static PLI_INT32 after(p_cb_data data)
{
    (void)data;
    report("after");
    package_call("after");
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

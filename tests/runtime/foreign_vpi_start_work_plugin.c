// SPDX-License-Identifier: Apache-2.0
// Work scheduled from cbStartOfSimulation (IEEE 1800-2023 38.36.3): the
// package variable it reads has its initializer's value, a cbAfterDelay it
// registers fires at its time, and a value put there is seen by a
// cbValueChange registered there. Lines starting with "#c " form the trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static PLI_UINT32 now(void)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    return time.low;
}

static PLI_INT32 later(p_cb_data data)
{
    (void)data;
    vpi_printf("#c t=%u after-delay callback\n", now());
    return 0;
}

static PLI_INT32 changed(p_cb_data data)
{
    vpi_printf("#c t=%u q changed to %d\n", now(), data->value->value.integer);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    vpiHandle px = vpi_handle_by_name("pkg::px", NULL);
    if (px != NULL) {
        vpi_get_value(px, &value);
    }
    vpi_printf("#c t=%u start px=%d\n", now(), px != NULL ? value.value.integer : -1);

    s_cb_data request;
    s_vpi_time delay = { vpiSimTime, 0, 1, 0.0 };
    memset(&request, 0, sizeof request);
    request.reason = cbAfterDelay;
    request.cb_rtn = later;
    request.time = &delay;
    vpi_register_cb(&request);

    vpiHandle q = vpi_handle_by_name("top.q", NULL);
    if (q != NULL) {
        s_vpi_value seen;
        s_vpi_time none = { vpiSuppressTime, 0, 0, 0.0 };
        memset(&request, 0, sizeof request);
        memset(&seen, 0, sizeof seen);
        seen.format = vpiIntVal;
        request.reason = cbValueChange;
        request.cb_rtn = changed;
        request.obj = q;
        request.time = &none;
        request.value = &seen;
        vpi_register_cb(&request);
        value.format = vpiIntVal;
        value.value.integer = 5;
        s_vpi_time put_time = { vpiSimTime, 0, 0, 0.0 };
        vpi_put_value(q, &value, &put_time, vpiNoDelay);
    }
    return 0;
}

static void startup(void)
{
    s_cb_data request;
    memset(&request, 0, sizeof request);
    request.reason = cbStartOfSimulation;
    request.cb_rtn = start;
    vpi_register_cb(&request);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

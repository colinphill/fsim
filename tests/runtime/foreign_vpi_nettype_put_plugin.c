// SPDX-License-Identifier: Apache-2.0
// VPI plug-in that deposits on, forces and releases nets of a user-defined
// nettype (IEEE 1800-2023 6.6.7, 38.34), integral ones with vpiIntVal and
// an element of an array of real ones with vpiRealVal, and reads each back
// at once and at the end of the time slot. The design is foreign_vpi_nettype_put.sv. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

typedef struct action {
    PLI_UINT32 time;
    const char* net;
    double value;
    PLI_INT32 flag;
    const char* label;
    int real;
} action;

static const action actions[] = {
    { 5, "top.n", 100, vpiNoDelay, "deposit", 0 },
    { 7, "top.a[1]", 200, vpiNoDelay, "deposit", 0 },
    { 9, "top.r[1]", 8.5, vpiNoDelay, "deposit", 1 },
    { 15, "top.n", 50, vpiForceFlag, "force", 0 },
    { 17, "top.a[1]", 60, vpiForceFlag, "force", 0 },
    { 19, "top.r[1]", 9.25, vpiForceFlag, "force", 1 },
    { 25, "top.n", 0, vpiReleaseFlag, "release", 0 },
    { 27, "top.a[1]", 0, vpiReleaseFlag, "release", 0 },
    { 29, "top.r[1]", 0, vpiReleaseFlag, "release", 1 },
};

#define ACTION_COUNT (sizeof actions / sizeof actions[0])

static void print_value(vpiHandle net, int real)
{
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    value.format = real ? vpiRealVal : vpiIntVal;
    vpi_get_value(net, &value);
    if (value.format == vpiRealVal) {
        vpi_printf("%0.2f\n", value.value.real);
    } else {
        vpi_printf("%d\n", (int)value.value.integer);
    }
}

static PLI_INT32 settled(p_cb_data data)
{
    const action* item = (const action*)data->user_data;
    vpiHandle net = vpi_handle_by_name((PLI_BYTE8*)item->net, NULL);
    vpi_printf("#c %u %s %s settled=", (unsigned)item->time, item->net,
        item->label);
    print_value(net, item->real);
    return 0;
}

static PLI_INT32 perform(p_cb_data data)
{
    static s_vpi_time slot_end = { vpiSimTime, 0, 0, 0.0 };
    const action* item = (const action*)data->user_data;
    vpiHandle net = vpi_handle_by_name((PLI_BYTE8*)item->net, NULL);
    s_vpi_value value;
    s_cb_data callback;
    if (net == NULL) {
        vpi_printf("#c %s missing\n", item->net);
        return 0;
    }
    memset(&value, 0, sizeof value);
    if (item->real) {
        value.format = vpiRealVal;
        value.value.real = item->value;
    } else {
        value.format = vpiIntVal;
        value.value.integer = (PLI_INT32)item->value;
    }
    vpi_put_value(net, &value, NULL, item->flag);
    vpi_printf("#c %u %s %s now=", (unsigned)item->time, item->net,
        item->label);
    print_value(net, item->real);
    memset(&callback, 0, sizeof callback);
    callback.reason = cbReadOnlySynch;
    callback.cb_rtn = settled;
    callback.time = &slot_end;
    callback.user_data = data->user_data;
    vpi_register_cb(&callback);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time times[ACTION_COUNT];
    size_t index;
    (void)data;
    for (index = 0; index < ACTION_COUNT; ++index) {
        s_cb_data callback;
        times[index].type = vpiSimTime;
        times[index].high = 0;
        times[index].low = actions[index].time;
        memset(&callback, 0, sizeof callback);
        callback.reason = cbAfterDelay;
        callback.cb_rtn = perform;
        callback.time = &times[index];
        callback.user_data = (PLI_BYTE8*)&actions[index];
        vpi_register_cb(&callback);
    }
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

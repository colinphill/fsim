// SPDX-License-Identifier: Apache-2.0
// VPI plug-in that deposits, forces and releases nets whose loads have
// per-source SDF INTERCONNECT delays (IEEE 1800-2023 38.34, IEEE 1364-2005
// 16.2.4). The writes land on the nets themselves, no source made them, and
// reach each load after the smallest delay of its sources. The expected
// trace is derived in foreign_vpi_net_foreign.sv. Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

typedef struct action {
    PLI_UINT32 time_ns;
    const char* net;
    const char* value;
    PLI_INT32 flag;
    const char* label;
} action;

static const action actions[] = {
    { 20, "top.wd", "1", vpiNoDelay, "D 1" },
    { 25, "top.wf", "1", vpiForceFlag, "F force 1" },
    { 30, "top.wv", "01", vpiNoDelay, "V 01" },
    { 40, "top.wd", "0", vpiNoDelay, "D 0" },
    { 45, "top.wf", "0", vpiForceFlag, "F force 0" },
    { 50, "top.wv", "10", vpiNoDelay, "V 10" },
    { 65, "top.wf", "0", vpiReleaseFlag, "F release" },
    { 70, "top.wr", "0", vpiNoDelay, "R 0" },
    { 80, "top.wr", "1", vpiForceFlag, "R force 1" },
    { 90, "top.wr", "0", vpiReleaseFlag, "R release" },
    { 100, "top.wd", "1", vpiNoDelay, "D 1" },
    { 101, "top.wd", "0", vpiNoDelay, "D 0" },
    { 110, "top.wd", "1", vpiNoDelay, "D 1" },
    { 140, "top.wd", "0", vpiNoDelay, "D 0" },
    { 141, "top.wd", "1", vpiNoDelay, "D 1" },
    { 200, "top.wd", "0", vpiNoDelay, "D 0" },
};

#define ACTION_COUNT (sizeof actions / sizeof actions[0])

static PLI_INT32 perform(p_cb_data data)
{
    const action* item = (const action*)data->user_data;
    vpiHandle net = vpi_handle_by_name((PLI_BYTE8*)item->net, NULL);
    s_vpi_value value;
    if (net == NULL) {
        vpi_printf("#c put %s missing\n", item->label);
        return 0;
    }
    memset(&value, 0, sizeof value);
    value.format = vpiBinStrVal;
    value.value.str = (PLI_BYTE8*)item->value;
    vpi_put_value(net, &value, NULL, item->flag);
    vpi_printf("#c put %s\n", item->label);
    return 0;
}

// The simulation time is in picoseconds (timescale 1ns/1ps).
static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time times[ACTION_COUNT];
    size_t index;
    (void)data;
    for (index = 0; index < ACTION_COUNT; ++index) {
        s_cb_data callback;
        times[index].type = vpiSimTime;
        times[index].high = 0;
        times[index].low = actions[index].time_ns * 1000U;
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

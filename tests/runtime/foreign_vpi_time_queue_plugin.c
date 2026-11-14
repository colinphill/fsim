// SPDX-License-Identifier: Apache-2.0
// VPI time queue conformance plug-in (IEEE 1800-2023 37.80, 37.81, 38.36.2):
// vpi_iterate(vpiTimeQueue, NULL) in increasing time order, vpi_get_time on a
// time queue, and vpi_iterate(vpiCallback, time queue) for the timed
// callbacks due in it. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static unsigned now_of(void)
{
    s_vpi_time now;
    memset(&now, 0, sizeof now);
    now.type = vpiSimTime;
    vpi_get_time(NULL, &now);
    return (unsigned)now.low;
}

static PLI_INT32 timed(p_cb_data data)
{
    vpi_printf("#c %s at %u\n", (const char*)data->user_data, now_of());
    return 0;
}

static void register_timed(PLI_INT32 reason, unsigned when, const char* label)
{
    s_vpi_time time;
    s_cb_data callback;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    time.low = when;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = timed;
    callback.time = &time;
    callback.user_data = (PLI_BYTE8*)label;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c register %s failed\n", label);
    }
}

static PLI_INT32 probe(p_cb_data data)
{
    vpiHandle queues;
    vpiHandle queue;
    (void)data;
    register_timed(cbAtStartOfSimTime, 8U, "start-of-time");
    register_timed(cbAfterDelay, 20U, "after-delay");
    queues = vpi_iterate(vpiTimeQueue, NULL);
    while (queues != NULL && (queue = vpi_scan(queues)) != NULL) {
        s_vpi_time time;
        vpiHandle callbacks;
        int count = 0;
        memset(&time, 0, sizeof time);
        time.type = vpiSimTime;
        vpi_get_time(queue, &time);
        // The current time step, when listed, is not of interest.
        if (time.low <= now_of()) {
            continue;
        }
        callbacks = vpi_iterate(vpiCallback, queue);
        while (callbacks != NULL && vpi_scan(callbacks) != NULL) {
            ++count;
        }
        vpi_printf("#c queue %s time=%u callbacks=%d\n",
            vpi_get_str(vpiType, queue), (unsigned)time.low, count);
    }
    return 0;
}

// The probe at time 1 is registered once the simulation starts: a startup
// routine may register only the reasons IEEE 1800-2023 36.9.1 allows.
static PLI_INT32 start(p_cb_data data)
{
    s_vpi_time time;
    s_cb_data callback;
    (void)data;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    time.low = 1U;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = probe;
    callback.time = &time;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c register probe failed\n");
    }
    return 0;
}

static void startup(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    (void)vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

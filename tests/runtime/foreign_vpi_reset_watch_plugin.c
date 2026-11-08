// SPDX-License-Identifier: Apache-2.0
// A value-change watch registered after the run started, then $reset (IEEE
// 1800-2023 D.8, 38.36.3 cbStartOfReset and cbEndOfReset). A cbAfterDelay
// callback at t=5 watches top.sig, which is a plan change of the running design
// (the signal is observed from then on). The reset builds its time-zero state
// from the artifact and replays that watch onto it, so the restore succeeds.
// The start-of-reset callback removes the watch. Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static vpiHandle watch_handle;

static PLI_INT32 now(void)
{
    s_vpi_time time;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    vpi_get_time(NULL, &time);
    return (PLI_INT32)time.low;
}

static PLI_INT32 value_changed(p_cb_data data)
{
    vpi_printf("#c vpi: t=%d sig=%d\n", now(), (int)data->value->value.integer);
    return 0;
}

static PLI_INT32 watch(p_cb_data data)
{
    static s_vpi_value value;
    static s_vpi_time time;
    s_cb_data callback;
    (void)data;
    memset(&callback, 0, sizeof callback);
    value.format = vpiIntVal;
    time.type = vpiSimTime;
    callback.reason = cbValueChange;
    callback.cb_rtn = value_changed;
    callback.obj = vpi_handle_by_name("top.sig", NULL);
    callback.time = &time;
    callback.value = &value;
    watch_handle = vpi_register_cb(&callback);
    vpi_printf("#c vpi: t=%d watching sig\n", now());
    return 0;
}

static PLI_INT32 start_of_reset(p_cb_data data)
{
    (void)data;
    vpi_printf("#c vpi: start of reset t=%d\n", now());
    if (watch_handle != NULL) {
        vpi_remove_cb(watch_handle);
        watch_handle = NULL;
    }
    return 0;
}

static PLI_INT32 end_of_reset(p_cb_data data)
{
    (void)data;
    vpi_printf("#c vpi: end of reset t=%d\n", now());
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    s_cb_data callback;
    static s_vpi_time delay;
    (void)data;
    memset(&callback, 0, sizeof callback);
    delay.type = vpiSimTime;
    delay.low = 5;
    callback.reason = cbAfterDelay;
    callback.cb_rtn = watch;
    callback.time = &delay;
    vpi_register_cb(&callback);
    return 0;
}

static void register_reason(PLI_INT32 reason, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = routine;
    vpi_register_cb(&callback);
}

// The reset callbacks cannot be registered from a startup routine (IEEE
// 1800-2023 36.10.2 allows only cbEndOfCompile, cbStartOfSimulation,
// cbEndOfSimulation, cbUnresolvedSystf, cbError and cbPLIError), so the start
// of simulation registers them.
static PLI_INT32 register_reset_callbacks(p_cb_data data)
{
    (void)data;
    register_reason(cbStartOfReset, start_of_reset);
    register_reason(cbEndOfReset, end_of_reset);
    return 0;
}

static void startup(void)
{
    register_reason(cbStartOfSimulation, start_of_simulation);
    register_reason(cbStartOfSimulation, register_reset_callbacks);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

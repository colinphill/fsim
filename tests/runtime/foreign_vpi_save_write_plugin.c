// SPDX-License-Identifier: Apache-2.0
// A delayed vpi_put_value that is still pending when the run is saved (IEEE
// 1800-2023 38.34 vpi_put_value with a time, 38.36.3). A cbAfterDelay callback
// at t=5 puts 42 on top.q with a delay of 25 (vpiInertialDelay), so the write
// is due at t=30. The design saves at t=10; a restart reads the pending write
// from the file, because nothing of this library runs again at a restart (no
// cbStartOfSimulation, and the cbAfterDelay record of the saving run is gone).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static PLI_INT32 now(void)
{
    s_vpi_time time;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    vpi_get_time(NULL, &time);
    return (PLI_INT32)time.low;
}

static PLI_INT32 put_later(p_cb_data data)
{
    vpiHandle q = vpi_handle_by_name("top.q", NULL);
    s_vpi_value value;
    s_vpi_time delay;
    (void)data;
    memset(&value, 0, sizeof value);
    memset(&delay, 0, sizeof delay);
    value.format = vpiIntVal;
    value.value.integer = 42;
    delay.type = vpiSimTime;
    delay.low = 25;
    vpi_put_value(q, &value, &delay, vpiInertialDelay);
    vpi_printf("#c vpi: t=%d put q=42 after 25\n", now());
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time delay;
    (void)data;
    memset(&callback, 0, sizeof callback);
    memset(&delay, 0, sizeof delay);
    delay.type = vpiSimTime;
    delay.low = 5;
    callback.reason = cbAfterDelay;
    callback.cb_rtn = put_later;
    callback.time = &delay;
    vpi_register_cb(&callback);
    return 0;
}

static void startup(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start_of_simulation;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

// SPDX-License-Identifier: Apache-2.0
// A vpi_put_delays on a gate declared without a delay, across $save and a
// fresh-process restart and across $reset (IEEE 1800-2023 38.32, 28.16, D.8,
// D.9, 38.36.3). A cbAfterDelay callback at t=10 puts rise 3 and fall 5 (with
// minimum 2 and 4 and maximum 4 and 6) on top.g, which splits the gate into a
// delay slot at the next safe point; a get with mtm_flag reads all six values
// back. cbStartOfSave registers cbEndOfRestart (a callback registered in a
// startup routine or at the start of simulation does not survive a restart,
// 38.36.3), which reads the delays again after the restore. Lines starting
// with "#c " form the checked trace.
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

static void read_delays(const char* label)
{
    s_vpi_time times[6];
    s_vpi_delay delays;
    vpiHandle gate = vpi_handle_by_name((PLI_BYTE8*)"top.g", NULL);
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = 2;
    delays.time_type = vpiSimTime;
    delays.mtm_flag = 1;
    delays.append_flag = 0;
    delays.pulsere_flag = 0;
    vpi_get_delays(gate, &delays);
    vpi_printf("#c vpi: t=%d %s rise %u,%u,%u fall %u,%u,%u\n", now(), label,
        (unsigned)times[0].low, (unsigned)times[1].low, (unsigned)times[2].low,
        (unsigned)times[3].low, (unsigned)times[4].low, (unsigned)times[5].low);
}

static PLI_INT32 put_delays(p_cb_data data)
{
    s_vpi_time times[6];
    s_vpi_delay delays;
    vpiHandle gate = vpi_handle_by_name((PLI_BYTE8*)"top.g", NULL);
    (void)data;
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = 2;
    delays.time_type = vpiSimTime;
    delays.mtm_flag = 1;
    delays.append_flag = 0;
    delays.pulsere_flag = 0;
    times[0].type = times[1].type = times[2].type = vpiSimTime;
    times[3].type = times[4].type = times[5].type = vpiSimTime;
    times[0].low = 2;
    times[1].low = 3;
    times[2].low = 4;
    times[3].low = 4;
    times[4].low = 5;
    times[5].low = 6;
    vpi_put_delays(gate, &delays);
    read_delays("after put");
    return 0;
}

static PLI_INT32 end_of_restart(p_cb_data data)
{
    (void)data;
    read_delays("after restart");
    return 0;
}

static PLI_INT32 register_reason(PLI_INT32 reason,
    PLI_INT32 (*routine)(p_cb_data), const s_vpi_time* time)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = routine;
    callback.time = (p_vpi_time)time;
    return vpi_register_cb(&callback) != NULL;
}

static PLI_INT32 start_of_save(p_cb_data data)
{
    (void)data;
    register_reason(cbEndOfRestart, end_of_restart, NULL);
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    static s_vpi_time delay;
    (void)data;
    delay.type = vpiSimTime;
    delay.low = 10;
    register_reason(cbAfterDelay, put_delays, &delay);
    register_reason(cbStartOfSave, start_of_save, NULL);
    return 0;
}

// vlog_startup_routines may register only cbEndOfCompile, cbStartOfSimulation,
// cbEndOfSimulation, cbUnresolvedSystf, cbError and cbPLIError (IEEE 1800-2023
// 36.10.2), so cbStartOfSave is registered from the start of simulation.
static void startup(void)
{
    register_reason(cbStartOfSimulation, start_of_simulation, NULL);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

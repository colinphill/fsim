// SPDX-License-Identifier: Apache-2.0
// VPI save and restart session plug-in (IEEE 1800-2023 38.9 vpi_get_data,
// 38.31 vpi_put_data, 38.36.3): cbStartOfSave puts two data chunks under a
// vpiSaveRestartID and registers cbStartOfRestart (with the id as user_data,
// as the 38.36.3 NOTE advises) and cbEndOfRestart; a fresh process that
// restarts from the file reads the chunks back in other chunk sizes' order. At
// a restart every other callback is removed, so the cbEndOfSimulation callback
// of the startup routine is gone and the plug-in registers a new one in
// cbEndOfRestart. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdint.h>
#include <string.h>

static PLI_INT32 now(void)
{
    s_vpi_time time;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    vpi_get_time(NULL, &time);
    return (PLI_INT32)time.low;
}

static PLI_INT32 register_reason(PLI_INT32 reason,
    PLI_INT32 (*routine)(p_cb_data), PLI_BYTE8* user_data)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = routine;
    callback.user_data = user_data;
    return vpi_register_cb(&callback) != NULL;
}

static PLI_INT32 start_of_save(p_cb_data data);

// vlog_startup_routines may register only cbEndOfCompile, cbStartOfSimulation,
// cbEndOfSimulation, cbUnresolvedSystf, cbError and cbPLIError (IEEE 1800-2023
// 36.10.2), so cbStartOfSave is registered from the start of simulation.
static PLI_INT32 start_of_simulation(p_cb_data data)
{
    (void)data;
    vpi_printf("#c vpi: start of simulation\n");
    register_reason(cbStartOfSave, start_of_save, NULL);
    return 0;
}

static PLI_INT32 end_of_simulation(p_cb_data data)
{
    vpi_printf("#c vpi: end of simulation t=%d%s\n", now(),
        data->user_data != NULL ? " (after restart)" : "");
    return 0;
}

static PLI_INT32 end_of_restart(p_cb_data data)
{
    (void)data;
    vpi_printf("#c vpi: end of restart\n");
    register_reason(cbEndOfSimulation, end_of_simulation, (PLI_BYTE8*)1);
    return 0;
}

static PLI_INT32 start_of_restart(p_cb_data data)
{
    PLI_INT32 id = (PLI_INT32)(intptr_t)data->user_data;
    PLI_INT32 first = 0;
    PLI_INT32 second = 0;
    PLI_INT32 read_first = vpi_get_data(id, (PLI_BYTE8*)&first, 4);
    PLI_INT32 read_second = vpi_get_data(id, (PLI_BYTE8*)&second, 4);
    vpi_printf("#c vpi: start of restart data=%d,%d read=%d,%d\n", first,
        second, read_first, read_second);
    return 0;
}

static PLI_INT32 end_of_save(p_cb_data data)
{
    (void)data;
    vpi_printf("#c vpi: end of save\n");
    return 0;
}

static PLI_INT32 start_of_save(p_cb_data data)
{
    PLI_INT32 id = vpi_get(vpiSaveRestartID, NULL);
    PLI_INT32 first = 7;
    PLI_INT32 second = 9;
    PLI_INT32 put_first = vpi_put_data(id, (PLI_BYTE8*)&first, 4);
    PLI_INT32 put_second = vpi_put_data(id, (PLI_BYTE8*)&second, 4);
    const char* location = vpi_get_str(vpiSaveRestartLocation, NULL);
    (void)data;
    vpi_printf("#c vpi: start of save id=%d put=%d,%d location=%s\n", id,
        put_first, put_second,
        location != NULL && *location != '\0' ? "set" : "missing");
    register_reason(cbStartOfRestart, start_of_restart,
        (PLI_BYTE8*)(intptr_t)id);
    register_reason(cbEndOfRestart, end_of_restart, NULL);
    register_reason(cbEndOfSave, end_of_save, NULL);
    return 0;
}

static void startup(void)
{
    register_reason(cbStartOfSimulation, start_of_simulation, NULL);
    register_reason(cbEndOfSimulation, end_of_simulation, NULL);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

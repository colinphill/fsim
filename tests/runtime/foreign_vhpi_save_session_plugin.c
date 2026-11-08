// SPDX-License-Identifier: Apache-2.0
// VHPI save and restart session plug-in (IEEE 1076-2019 20.7, 20.8, 23.12,
// 23.27): vhpiCbStartOfSave puts two data chunks under a vhpiIdP identifier
// and registers vhpiCbStartOfRestart (the id as user_data, 21.3.8.3) and
// vhpiCbEndOfRestart; a fresh process that restarts reads them back. The
// restart phase keeps only those two callbacks, so the end-of-simulation
// callback of the startup routine is gone and the plug-in registers another
// in vhpiCbEndOfRestart. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void register_reason(int32_t reason, void (*routine)(const vhpiCbDataT*),
    void* user_data)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    data.user_data = user_data;
    (void)vhpi_register_cb(&data, 0);
}

static void start_of_simulation(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c vhpi: start of simulation\n");
    fflush(stdout);
}

static void end_of_simulation(const vhpiCbDataT* data)
{
    vhpiTimeT time;
    memset(&time, 0, sizeof time);
    vhpi_get_time(&time, NULL);
    printf("#c vhpi: end of simulation t=%u%s\n", (unsigned)time.low,
        data->user_data != NULL ? " (after restart)" : "");
    fflush(stdout);
}

static void end_of_restart(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c vhpi: end of restart phase=%d\n", (int)vhpi_get(vhpiPhaseP, NULL));
    fflush(stdout);
    register_reason(vhpiCbEndOfSimulation, end_of_simulation, (void*)1);
}

static void start_of_restart(const vhpiCbDataT* data)
{
    int32_t id = (int32_t)(intptr_t)data->user_data;
    int32_t first = 0;
    int32_t second = 0;
    size_t read_first = vhpi_get_data(id, &first, 4);
    size_t read_second = vhpi_get_data(id, &second, 4);
    printf("#c vhpi: start of restart data=%d,%d read=%d,%d\n", (int)first,
        (int)second, (int)read_first, (int)read_second);
    fflush(stdout);
}

static void end_of_save(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c vhpi: end of save\n");
    fflush(stdout);
}

static void start_of_save(const vhpiCbDataT* data)
{
    int32_t id = (int32_t)vhpi_get(vhpiIdP, NULL);
    int32_t first = 7;
    int32_t second = 9;
    size_t put_first = vhpi_put_data(id, &first, 4);
    size_t put_second = vhpi_put_data(id, &second, 4);
    const char* location = (const char*)vhpi_get_str(vhpiSaveRestartLocationP, NULL);
    (void)data;
    printf("#c vhpi: start of save id=%d put=%d,%d location=%s\n", (int)id,
        (int)put_first, (int)put_second,
        location != NULL && *location != '\0' ? "set" : "missing");
    fflush(stdout);
    register_reason(vhpiCbStartOfRestart, start_of_restart,
        (void*)(intptr_t)id);
    register_reason(vhpiCbEndOfRestart, end_of_restart, NULL);
    register_reason(vhpiCbEndOfSave, end_of_save, NULL);
}

static void startup(void)
{
    register_reason(vhpiCbStartOfSimulation, start_of_simulation, NULL);
    register_reason(vhpiCbStartOfSave, start_of_save, NULL);
    register_reason(vhpiCbEndOfSimulation, end_of_simulation, NULL);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

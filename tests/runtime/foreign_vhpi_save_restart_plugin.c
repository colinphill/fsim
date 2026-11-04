// SPDX-License-Identifier: Apache-2.0
// VHPI save/restart data conformance plug-in (IEEE 1076-2019 20.7, 20.8,
// 21.3.8, 23.12, 23.27): vhpi_get(vhpiIdP) issues increasing identifiers,
// the save/restart location and the data functions are errors outside a
// save or restart, and vhpiCbStartOfRestart registers only during a save.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void unused(const vhpiCbDataT* data)
{
    (void)data;
}

static void check_reason(const char* name, int32_t reason, int removable)
{
    vhpiCbDataT data;
    vhpiHandleT handle;
    int failed;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = unused;
    handle = vhpi_register_cb(&data, vhpiReturnCb);
    failed = vhpi_check_error(NULL);
    printf("#c register %s: handle=%d error=%d\n", name, handle != NULL,
        failed);
    if (handle != NULL && removable) {
        printf("#c remove %s: rc=%d\n", name, vhpi_remove_cb(handle));
    }
}

static void start_of_simulation(const vhpiCbDataT* data)
{
    char buffer[4] = { 1, 2, 3, 4 };
    int32_t first;
    int32_t second;
    const char* location;
    int failed;
    size_t count;
    (void)data;
    first = (int32_t)vhpi_get(vhpiIdP, NULL);
    second = (int32_t)vhpi_get(vhpiIdP, NULL);
    printf("#c id positive=%d\n", first > 0);
    printf("#c ids differ=%d\n", first != second);
    printf("#c ids increase=%d\n", second > first);
    location = (const char*)vhpi_get_str(vhpiSaveRestartLocationP, NULL);
    failed = vhpi_check_error(NULL);
    printf("#c location outside save: null=%d error=%d\n", location == NULL,
        failed);
    count = vhpi_put_data(first, buffer, sizeof buffer);
    failed = vhpi_check_error(NULL);
    printf("#c put_data outside save: rc=%d error=%d\n", (int)count, failed);
    count = vhpi_get_data(first, buffer, sizeof buffer);
    failed = vhpi_check_error(NULL);
    printf("#c get_data outside restart: rc=%d error=%d\n", (int)count,
        failed);
    check_reason("vhpiCbStartOfSave", vhpiCbStartOfSave, 1);
    check_reason("vhpiCbEndOfSave", vhpiCbEndOfSave, 1);
    check_reason("vhpiCbEndOfRestart", vhpiCbEndOfRestart, 1);
    // Registered only during the save phase (21.3.8.3).
    {
        vhpiCbDataT restart;
        vhpiHandleT handle;
        memset(&restart, 0, sizeof restart);
        restart.reason = vhpiCbStartOfRestart;
        restart.cb_rtn = unused;
        handle = vhpi_register_cb(&restart, vhpiReturnCb);
        failed = vhpi_check_error(NULL);
        printf("#c register vhpiCbStartOfRestart outside save: handle=%d "
               "error=%d\n",
            handle != NULL, failed);
    }
    printf("#c automatic restore=%d\n",
        (int)vhpi_get(vhpiAutomaticRestoreP, vhpi_handle(vhpiTool, NULL)));
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start_of_simulation;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

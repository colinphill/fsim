// SPDX-License-Identifier: Apache-2.0
// VPI save/restart data conformance plug-in (IEEE 1800-2023 38.9, 38.31,
// 38.36.3): vpi_get(vpiSaveRestartID) issues increasing identifiers, the
// save/restart location and the data routines refuse to work outside a save
// or restart, and the save, restart and reset reasons register and remove.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static PLI_INT32 unused(p_cb_data data)
{
    (void)data;
    return 0;
}

static void check_reason(const char* name, PLI_INT32 reason)
{
    s_cb_data callback;
    vpiHandle handle;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = unused;
    handle = vpi_register_cb(&callback);
    vpi_printf("#c register %s: handle=%d\n", name, handle != NULL);
    if (handle != NULL) {
        vpi_printf("#c remove %s=%d\n", name, (int)vpi_remove_cb(handle));
    }
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    PLI_BYTE8 buffer[4] = { 1, 2, 3, 4 };
    PLI_INT32 first;
    PLI_INT32 second;
    const char* location;
    (void)data;
    first = vpi_get(vpiSaveRestartID, NULL);
    second = vpi_get(vpiSaveRestartID, NULL);
    vpi_printf("#c id positive=%d\n", first > 0);
    vpi_printf("#c ids differ=%d\n", first != second);
    vpi_printf("#c ids increase=%d\n", second > first);
    location = vpi_get_str(vpiSaveRestartLocation, NULL);
    vpi_printf("#c location outside save=%s\n",
        location != NULL ? location : "(null)");
    vpi_printf("#c put_data outside save=%d\n",
        (int)vpi_put_data(first, buffer, (PLI_INT32)sizeof buffer));
    vpi_printf("#c get_data outside restart=%d\n",
        (int)vpi_get_data(first, buffer, (PLI_INT32)sizeof buffer));
    check_reason("cbStartOfSave", cbStartOfSave);
    check_reason("cbEndOfSave", cbEndOfSave);
    check_reason("cbStartOfRestart", cbStartOfRestart);
    check_reason("cbEndOfRestart", cbEndOfRestart);
    check_reason("cbStartOfReset", cbStartOfReset);
    check_reason("cbEndOfReset", cbEndOfReset);
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start_of_simulation;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

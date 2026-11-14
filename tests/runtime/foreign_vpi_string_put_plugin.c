// SPDX-License-Identifier: Apache-2.0
// Writes three strings to top.s of foreign_vpi_string_put.sv with
// zero-delay inertial puts (IEEE 1800-2023 38.34), as cocotb writes them,
// from cbAfterDelay callbacks at times 10, 20 and 30. Each put wakes the
// design's always block, which prints the "#c " trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stddef.h>

static const char* const values[] = { "sample", "bytes", "yellow" };
static int next;

static PLI_INT32 write_next(p_cb_data data)
{
    s_vpi_value value = { vpiStringVal, { 0 } };
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    (void)data;
    value.value.str = (PLI_BYTE8*)values[next++];
    vpi_put_value(vpi_handle_by_name((PLI_BYTE8*)"top.s", NULL), &value,
        &time, vpiInertialDelay);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time times[3];
    (void)data;
    for (int index = 0; index < 3; ++index) {
        s_cb_data callback = { 0 };
        times[index].type = vpiSimTime;
        times[index].low = (PLI_UINT32)(10 * (index + 1));
        callback.reason = cbAfterDelay;
        callback.time = &times[index];
        callback.cb_rtn = write_next;
        vpi_register_cb(&callback);
    }
    return 0;
}

static void startup(void)
{
    s_cb_data callback = { 0 };
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, NULL };

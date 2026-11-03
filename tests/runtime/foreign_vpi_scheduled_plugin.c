// SPDX-License-Identifier: Apache-2.0
// VPI scheduled-write conformance plug-in (IEEE 1800-2023 38.35): writes
// with a zero delay, made before time 0, to variables no process writes,
// take effect in time 0's update events, before any later time step. The
// HDL prints the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static void put(const char* name, PLI_INT32 value, PLI_INT32 flags)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    s_vpi_value data;
    vpiHandle handle = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    if (handle == NULL) {
        vpi_printf("#c no %s\n", name);
        return;
    }
    data.format = vpiIntVal;
    data.value.integer = value;
    (void)vpi_put_value(handle, &data, &time, flags);
}

static PLI_INT32 end_of_compile(p_cb_data data)
{
    (void)data;
    put("top.transport", 1, vpiPureTransportDelay);
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    (void)data;
    put("top.inertial", 5, vpiInertialDelay);
    put("top.modified", 9, vpiTransportDelay);
    return 0;
}

static void register_action(PLI_INT32 reason, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    (void)vpi_register_cb(&data);
}

static void startup(void)
{
    register_action(cbEndOfCompile, end_of_compile);
    register_action(cbStartOfSimulation, start_of_simulation);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

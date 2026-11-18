// SPDX-License-Identifier: Apache-2.0
// VPI late per-write value-change plug-in (IEEE 1800-2023 38.36.1): a
// cbValueChange registered at time 2, after the variable's writer has run,
// reports each later blocking write that changes the value.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static PLI_INT32 changed(p_cb_data data)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    vpi_printf("#c change %u %s %s\n", (unsigned)time.low,
        vpi_get_str(vpiFullName, data->obj), data->value->value.str);
    return 0;
}

static PLI_INT32 watch(p_cb_data data)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    static s_vpi_value value = { vpiHexStrVal, { 0 } };
    s_vpi_time now = { vpiSimTime, 0, 0, 0.0 };
    s_cb_data watch_data;
    vpiHandle handle = vpi_handle_by_name((PLI_BYTE8*)"top.x", NULL);
    (void)data;
    vpi_get_time(NULL, &now);
    vpi_printf("#c watch %u\n", (unsigned)now.low);
    if (handle == NULL) {
        vpi_printf("#c no top.x\n");
        return 0;
    }
    memset(&watch_data, 0, sizeof watch_data);
    watch_data.reason = cbValueChange;
    watch_data.cb_rtn = changed;
    watch_data.obj = handle;
    watch_data.time = &time;
    watch_data.value = &value;
    (void)vpi_register_cb(&watch_data);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time delay = { vpiSimTime, 0, 2, 0.0 };
    s_cb_data later;
    (void)data;
    memset(&later, 0, sizeof later);
    later.reason = cbAfterDelay;
    later.cb_rtn = watch;
    later.time = &delay;
    (void)vpi_register_cb(&later);
    return 0;
}

static void register_start(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

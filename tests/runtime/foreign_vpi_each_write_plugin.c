// SPDX-License-Identifier: Apache-2.0
// VPI per-write value-change plug-in (IEEE 1800-2023 38.36.1): a
// cbValueChange on a narrow and on a wide variable reports each blocking
// write that changes the value, with the value the write left.
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

static void watch(const char* name)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    static s_vpi_value value = { vpiHexStrVal, { 0 } };
    s_cb_data data;
    vpiHandle handle = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    if (handle == NULL) {
        vpi_printf("#c no %s\n", name);
        return;
    }
    memset(&data, 0, sizeof data);
    data.reason = cbValueChange;
    data.cb_rtn = changed;
    data.obj = handle;
    data.time = &time;
    data.value = &value;
    (void)vpi_register_cb(&data);
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    watch("top.x");
    watch("top.w");
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

// SPDX-License-Identifier: Apache-2.0
// cbDisable on blocks a process has left (IEEE 1800-2023 38.36.1): places a
// callback on top.left, top.outer, top.outer.inner and top.waiting and
// reports each call. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static PLI_INT32 disabled(p_cb_data data)
{
    vpi_printf("#c cbDisable %s t=%u\n",
        vpi_get_str(vpiFullName, data->obj),
        data->time != NULL ? (unsigned)data->time->low : 0U);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static const char* const names[] = {
        "top.left",
        "top.outer",
        "top.outer.inner",
        "top.waiting",
    };
    unsigned index;
    (void)data;
    for (index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
        s_cb_data disable;
        memset(&disable, 0, sizeof disable);
        disable.reason = cbDisable;
        disable.cb_rtn = disabled;
        disable.obj = vpi_handle_by_name((PLI_BYTE8*)names[index], NULL);
        disable.time = &time;
        vpi_printf("#c %s accepted=%d\n", names[index],
            disable.obj != NULL && vpi_register_cb(&disable) != NULL);
    }
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

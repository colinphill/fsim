// SPDX-License-Identifier: Apache-2.0
// cbUnresolvedSystf conformance plug-in (IEEE 1800-2023 38.36.2-3): the
// plug-in registers $late_task only when a callback names it as unresolved.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static unsigned long long now(void)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static PLI_INT32 late_task(PLI_BYTE8* user_data)
{
    vpiHandle arguments = vpi_iterate(vpiArgument, vpi_handle(vpiSysTfCall, NULL));
    vpiHandle argument = arguments != NULL ? vpi_scan(arguments) : NULL;
    s_vpi_value value;
    (void)user_data;
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    if (argument != NULL) {
        vpi_get_value(argument, &value);
        vpi_release_handle(arguments);
    }
    vpi_printf("#c %llu late task arg=%d\n", now(), (int)value.value.integer);
    return 0;
}

// user_data names the system task or function (38.36.3).
static PLI_INT32 unresolved(p_cb_data data)
{
    s_vpi_systf_data systf;
    const char* const name = (const char*)data->user_data;
    vpi_printf("#c unresolved %s\n", name != NULL ? name : "?");
    if (name != NULL && strcmp(name, "$late_task") == 0) {
        memset(&systf, 0, sizeof systf);
        systf.type = vpiSysTask;
        systf.tfname = "$late_task";
        systf.calltf = late_task;
        (void)vpi_register_systf(&systf);
    }
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbUnresolvedSystf;
    data.cb_rtn = unresolved;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

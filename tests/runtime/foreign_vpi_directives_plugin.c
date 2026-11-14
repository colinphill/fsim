// SPDX-License-Identifier: Apache-2.0
// Module directive state (IEEE 1800-2023 37.5): at time 1, prints the
// vpiDefDecayTime of each instance and the condition of its
// vpiDefaultDisableIff. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static void report_instance(char* path)
{
    vpiHandle instance = vpi_handle_by_name(path, NULL);
    vpiHandle condition;
    s_vpi_value value;
    if (instance == NULL) {
        vpi_printf("#c %s missing\n", path);
        return;
    }
    vpi_printf("#c %s decay=%d\n", path, (int)vpi_get(vpiDefDecayTime, instance));
    condition = vpi_handle(vpiDefaultDisableIff, instance);
    if (condition == NULL) {
        vpi_printf("#c %s disable iff: none\n", path);
        return;
    }
    memset(&value, 0, sizeof value);
    value.format = vpiBinStrVal;
    vpi_get_value(condition, &value);
    vpi_printf("#c %s disable iff: type=%d name=%s value=%s\n", path,
        (int)vpi_get(vpiType, condition),
        vpi_get_str(vpiName, condition) != NULL ? vpi_get_str(vpiName, condition)
                                                : "-",
        value.value.str != NULL ? value.value.str : "none");
}

static PLI_INT32 report(p_cb_data data)
{
    (void)data;
    report_instance("top");
    report_instance("top.u_dis");
    report_instance("top.u_real");
    report_instance("top.u_inf");
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time delay = { vpiSimTime, 0, 1, 0.0 };
    s_cb_data later;
    (void)data;
    memset(&later, 0, sizeof later);
    later.reason = cbAfterDelay;
    later.cb_rtn = report;
    later.time = &delay;
    (void)vpi_register_cb(&later);
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

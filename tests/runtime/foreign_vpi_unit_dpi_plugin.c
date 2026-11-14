// SPDX-License-Identifier: Apache-2.0
// A DPI import of the compilation-unit scope (IEEE 1800-2023 3.12.1, 37.10,
// 37.42): at time 1, prints the `$unit` package and its functions. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static PLI_INT32 report(p_cb_data data)
{
    vpiHandle unit = vpi_handle_by_name("$unit", NULL);
    vpiHandle list;
    vpiHandle function;
    vpiHandle named = vpi_handle_by_name("$unit::unit_add", NULL);
    (void)data;
    if (unit == NULL) {
        vpi_printf("#c no $unit package\n");
        return 0;
    }
    vpi_printf("#c package %s unit=%d\n", vpi_get_str(vpiFullName, unit),
        (int)vpi_get(vpiUnit, unit));
    list = vpi_iterate(vpiFunction, unit);
    while (list != NULL && (function = vpi_scan(list)) != NULL) {
        vpi_printf("#c function %s access=%d size=%d\n",
            vpi_get_str(vpiFullName, function),
            (int)vpi_get(vpiAccessType, function),
            (int)vpi_get(vpiSize, function));
    }
    vpi_printf("#c by name: %s\n",
        named != NULL ? vpi_get_str(vpiFullName, named) : "(none)");
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

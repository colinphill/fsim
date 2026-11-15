// SPDX-License-Identifier: Apache-2.0
// The processes of modules whose variables have declaration initializers:
// an initializer is no process of the source (IEEE 1800-2023 6.8, 37.63).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static void processes(char* name)
{
    vpiHandle scope = vpi_handle_by_name(name, NULL);
    vpiHandle items = scope != NULL ? vpi_iterate(vpiProcess, scope) : NULL;
    vpiHandle item;
    int count = 0;
    vpi_printf("#c %s processes:\n", name);
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpi_printf("#c   %s\n", vpi_get_str(vpiType, item));
        ++count;
    }
    vpi_printf("#c %s count=%d\n", name, count);
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    processes("top");
    processes("top.u");
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

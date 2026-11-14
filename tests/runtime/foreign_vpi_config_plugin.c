// SPDX-License-Identifier: Apache-2.0
// VPI library binding properties of a configured design (IEEE 1800-2023
// 33.7): vpiLibrary, vpiCell and vpiConfig of the instances a configuration
// controls. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static PLI_INT32 check(p_cb_data data)
{
    static char* names[] = { "top", "top.m", "top.m.l0" };
    size_t index;
    (void)data;
    for (index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        vpiHandle object = vpi_handle_by_name(names[index], NULL);
        if (object == NULL) {
            vpi_printf("#c %s missing\n", names[index]);
            continue;
        }
        vpi_printf("#c %s library=%s cell=%s config=%s\n", names[index],
            text(vpi_get_str(vpiLibrary, object)),
            text(vpi_get_str(vpiCell, object)),
            text(vpi_get_str(vpiConfig, object)));
    }
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = check;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

// SPDX-License-Identifier: Apache-2.0
// VPI port kind conformance plug-in (IEEE 1800-2023 23.2.2.3, 37.16,
// 37.17): the object a port name finds is a net or a variable as the
// port's declaration says, with its net type, not as the collapsed signal
// it shares with the instance above.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static void show(const char* name)
{
    vpiHandle object = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    if (object == NULL) {
        vpi_printf("#c %s null\n", name);
        return;
    }
    vpi_printf("#c %s type=%d nettype=%d\n", name, (int)vpi_get(vpiType, object),
        (int)vpi_get(vpiNetType, object));
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    show("top.c.a");
    show("top.c.y");
    show("top.c.z");
    show("top.c.t");
    show("top.cy");
    show("top.cz");
    show("top.ct");
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

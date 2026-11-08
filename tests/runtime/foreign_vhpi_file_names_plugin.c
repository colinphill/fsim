// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for vhpiLogicalNameP of file declarations (IEEE 1076-2019
// 6.4.2.5, 19.4.3, Figure 10). Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void show(const char* name)
{
    vhpiHandleT file = vhpi_handle_by_name(name, NULL);
    const vhpiCharT* logical
        = file != NULL ? vhpi_get_str(vhpiLogicalNameP, file) : NULL;
    printf("#c %s logical=%s\n", name + 4,
        logical != NULL ? (const char*)logical : "-");
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    show("top.f1");
    show("top.f2");
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

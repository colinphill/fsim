// SPDX-License-Identifier: Apache-2.0
// Typespec chains through vpiElemTypespec (IEEE 1800-2023 37.25 detail 8):
// each level's type and typedef name, for packed dimensions declared over a
// typedef (7.4.1).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* name_of(vpiHandle object)
{
    const char* const value = vpi_get_str(vpiName, object);
    return value != NULL && value[0] != '\0' ? value : "-";
}

static void chain(const char* label, const char* variable)
{
    vpiHandle typespec = vpi_handle(vpiTypespec, vpi_handle_by_name((char*)variable, NULL));
    int level = 0;
    while (typespec != NULL && level < 3) {
        vpi_printf("#c %s.%d type=%s name=%s\n", label, level,
            vpi_get_str(vpiType, typespec), name_of(typespec));
        typespec = vpi_handle(vpiElemTypespec, typespec);
        ++level;
    }
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    chain("v", "top.v");
    chain("w", "top.w");
    chain("a", "top.a");
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

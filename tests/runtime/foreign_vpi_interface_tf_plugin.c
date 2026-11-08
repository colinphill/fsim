// SPDX-License-Identifier: Apache-2.0
// Iterates the interface tf decls of an interface instance and prints each
// one's full name, object type and vpiAccessType (IEEE 1800-2023 37.6, 37.8).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = vpi_get_str(property, object);
    return value != NULL ? value : "";
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle const interface_object
        = vpi_handle_by_name((PLI_BYTE8*)"top.sb", NULL);
    vpiHandle iterator;
    vpiHandle declaration;
    (void)data;
    if (interface_object == NULL) {
        vpi_printf("#c interface missing\n");
        return 0;
    }
    iterator = vpi_iterate(vpiInterfaceTfDecl, interface_object);
    while (iterator != NULL && (declaration = vpi_scan(iterator)) != NULL) {
        const PLI_INT32 type = vpi_get(vpiType, declaration);
        vpi_printf("#c tf %s %s access=%d\n", text(vpiFullName, declaration),
            type == vpiTask ? "task" : type == vpiFunction ? "function" : "other",
            (int)vpi_get(vpiAccessType, declaration));
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

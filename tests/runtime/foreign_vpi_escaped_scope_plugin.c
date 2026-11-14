// SPDX-License-Identifier: Apache-2.0
// Iterates the internal scopes and variables of top in
// foreign_vpi_escaped_scope.sv (IEEE 1800-2023 37.10, 37.12): a variable
// with an escaped identifier and an initializer must not end an iteration.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stddef.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static void list(const char* label, PLI_INT32 relation, vpiHandle parent)
{
    vpiHandle iterator = vpi_iterate(relation, parent);
    vpiHandle object = NULL;
    s_vpi_error_info error;
    const int failed = vpi_chk_error(&error);
    vpi_printf("#c %s %s%s\n", label, iterator != NULL ? "iterator" : "none",
        failed != 0 ? " error" : "");
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c   [%s] type %d\n",
            text(vpi_get_str(vpiFullName, object)), (int)vpi_get(vpiType, object));
    }
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name((PLI_BYTE8*)"top", NULL);
    (void)data;
    list("internal scopes", vpiInternalScope, top);
    list("regs", vpiReg, top);
    list("variables", vpiVariables, top);
    return 0;
}

static void startup(void)
{
    s_cb_data callback = { 0 };
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, NULL };

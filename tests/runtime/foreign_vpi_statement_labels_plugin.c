// SPDX-License-Identifier: Apache-2.0
// Walks the internal scopes of top in foreign_vpi_statement_labels.sv and
// finds the static variables of labeled loops by full name (IEEE 1800-2023
// 9.3.5, 12.7.1, 12.7.3, 27.6, 37.12): a labeled for or foreach loop is a
// scope named by its label, a labeled statement is a named begin-end block,
// and the blocks of a generate block's procedures are scopes beneath it.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stddef.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static void walk(vpiHandle parent, int depth)
{
    vpiHandle iterator = vpi_iterate(vpiInternalScope, parent);
    vpiHandle scope = NULL;
    while (iterator != NULL && (scope = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %*sscope [%s] name [%s] type %d\n", depth * 2, "",
            text(vpi_get_str(vpiFullName, scope)),
            text(vpi_get_str(vpiName, scope)), (int)vpi_get(vpiType, scope));
        walk(scope, depth + 1);
    }
}

static void find(const char* name)
{
    vpiHandle object = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    vpiHandle scope = object != NULL ? vpi_handle(vpiScope, object) : NULL;
    vpi_printf("#c find %s: %s scope [%s] type %d\n", name,
        object != NULL ? text(vpi_get_str(vpiFullName, object)) : "none",
        scope != NULL ? text(vpi_get_str(vpiFullName, scope)) : "-",
        scope != NULL ? (int)vpi_get(vpiType, scope) : 0);
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name((PLI_BYTE8*)"top", NULL);
    (void)data;
    walk(top, 0);
    find("top.lbl.lb.w");
    find("top.fl.blk.v");
    find("top.m2.b2.c");
    find("top.st");
    find("top.lbl");
    find("top.g.nb.s");
    find("top.g.gl.glb.gw");
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

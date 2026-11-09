// SPDX-License-Identifier: Apache-2.0
// Formal and result typespecs of tasks and functions declared with a typedef
// (IEEE 1800-2023 26.3, 37.25, 37.41): the typedef the name denotes where the
// callable is declared (wildcard import, explicit import, a local typedef
// hiding a wildcard-imported one).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = object != NULL ? vpi_get_str(property, object) : NULL;
    return value != NULL ? value : "";
}

static int range_bound(PLI_INT32 relation, vpiHandle typespec)
{
    s_vpi_value value;
    vpiHandle const bound = vpi_handle(relation, typespec);
    if (bound == NULL) {
        return -1;
    }
    value.format = vpiIntVal;
    vpi_get_value(bound, &value);
    return value.value.integer;
}

static void show(const char* what, const char* name, vpiHandle object)
{
    vpiHandle const typespec = object != NULL ? vpi_handle(vpiTypespec, object) : NULL;
    if (typespec == NULL) {
        vpi_printf("#c %s: %s typespec=none\n", name, what);
        return;
    }
    vpi_printf("#c %s: %s typespec=%s name=%s left=%d right=%d\n", name, what,
        text(vpiType, typespec), text(vpiName, typespec),
        range_bound(vpiLeftRange, typespec),
        range_bound(vpiRightRange, typespec));
}

static void formals(char* name)
{
    vpiHandle const callable = vpi_handle_by_name(name, NULL);
    vpiHandle const items = callable != NULL ? vpi_iterate(vpiIODecl, callable) : NULL;
    vpiHandle item;
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        show(text(vpiName, item), name, item);
    }
    show("result", name, callable != NULL ? vpi_handle(vpiReturn, callable) : NULL);
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    formals("top.wild");
    formals("top.expl");
    formals("top.local_t");
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

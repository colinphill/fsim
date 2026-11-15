// SPDX-License-Identifier: Apache-2.0
// The vpiParamAssign objects of module instances and the vpiDefParam
// objects of a module (IEEE 1800-2023 37.28): the parameter each sets
// (vpiLhs), its right side (vpiRhs) and vpiConnByName.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

// The right side: an expression with its value, or a typespec.
static void print_rhs(vpiHandle rhs, char* text, size_t size)
{
    s_vpi_value value;
    PLI_INT32 type;
    if (rhs == NULL) {
        snprintf(text, size, "-");
        return;
    }
    type = vpi_get(vpiType, rhs);
    if (type == vpiConstant || type == vpiOperation || type == vpiParameter) {
        value.format = vpiIntVal;
        value.value.integer = -1;
        vpi_get_value(rhs, &value);
        snprintf(text, size, "%s:%d", vpi_get_str(vpiType, rhs),
            (int)value.value.integer);
        return;
    }
    snprintf(text, size, "%s", vpi_get_str(vpiType, rhs));
}

static void assignments(char* name, PLI_INT32 kind)
{
    vpiHandle scope = vpi_handle_by_name(name, NULL);
    vpiHandle items = scope != NULL ? vpi_iterate(kind, scope) : NULL;
    vpiHandle item;
    vpi_printf("#c %s %s:\n", name,
        kind == vpiParamAssign ? "param assigns" : "def params");
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpiHandle lhs = vpi_handle(vpiLhs, item);
        vpiHandle parent = vpi_handle(vpiParent, item);
        char text[128];
        print_rhs(vpi_handle(vpiRhs, item), text, sizeof text);
        vpi_printf("#c %s lhs=%s parent=%s rhs=%s named=%d\n",
            vpi_get_str(vpiType, item),
            lhs != NULL ? vpi_get_str(vpiFullName, lhs) : "-",
            parent != NULL ? vpi_get_str(vpiFullName, parent) : "-", text,
            (int)vpi_get(vpiConnByName, item));
    }
}

// The parameters of a scope: value parameters and type parameters
// (37.28), with vpiLocalParam and a type parameter's typespec (detail 2).
static void parameters(char* name)
{
    vpiHandle scope = vpi_handle_by_name(name, NULL);
    vpiHandle items = scope != NULL ? vpi_iterate(vpiParameter, scope) : NULL;
    vpiHandle item;
    vpi_printf("#c %s parameters:\n", name);
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpiHandle typespec = vpi_handle(vpiTypespec, item);
        vpi_printf("#c %s %s local=%d typespec=%s\n",
            vpi_get_str(vpiType, item), vpi_get_str(vpiFullName, item),
            (int)vpi_get(vpiLocalParam, item),
            typespec != NULL ? vpi_get_str(vpiType, typespec) : "-");
    }
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle w = vpi_handle_by_name("top.u_def.W", NULL);
    s_vpi_value value;
    (void)data;
    assignments("top.u_ord", vpiParamAssign);
    assignments("top.u_named", vpiParamAssign);
    assignments("top.u_def", vpiParamAssign);
    assignments("top", vpiDefParam);
    assignments("top.u_def", vpiDefParam);
    parameters("top.u_named");
    parameters("top.u_def");
    vpi_printf("#c by name top.u_named.T: %s\n",
        vpi_handle_by_name("top.u_named.T", NULL) != NULL ? "found" : "none");
    value.format = vpiIntVal;
    value.value.integer = -1;
    if (w != NULL) {
        vpi_get_value(w, &value);
    }
    vpi_printf("#c top.u_def.W value=%d\n", (int)value.value.integer);
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

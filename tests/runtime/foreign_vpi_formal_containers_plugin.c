// SPDX-License-Identifier: Apache-2.0
// VPI plug-in for the typespec of a formal or result of a class or container
// type, and the ranges of one declared with a typedef (IEEE 1800-2023 37.41,
// 37.25). Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* kind_name(PLI_INT32 type)
{
    switch (type) {
    case vpiClassTypespec:
        return "vpiClassTypespec";
    case vpiArrayTypespec:
        return "vpiArrayTypespec";
    case vpiLogicTypespec:
        return "vpiLogicTypespec";
    case vpiIntTypespec:
        return "vpiIntTypespec";
    case vpiStringTypespec:
        return "vpiStringTypespec";
    default:
        return "other";
    }
}

static const char* array_name(PLI_INT32 type)
{
    switch (type) {
    case vpiQueueArray:
        return "queue";
    case vpiDynamicArray:
        return "dynamic";
    case vpiAssocArray:
        return "associative";
    case vpiStaticArray:
        return "static";
    default:
        return "other";
    }
}

static long bound(PLI_INT32 which, vpiHandle object)
{
    vpiHandle handle = object != NULL ? vpi_handle(which, object) : NULL;
    s_vpi_value value;
    if (handle == NULL) {
        return -99;
    }
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    return (long)value.value.integer;
}

static void show(const char* function, const char* what, vpiHandle object)
{
    vpiHandle typespec = vpi_handle(vpiTypespec, object);
    vpiHandle element = typespec != NULL ? vpi_handle(vpiElemTypespec, typespec) : NULL;
    vpi_printf("#c %s %s: ", function, what);
    if (typespec == NULL) {
        vpi_printf("no typespec\n");
        return;
    }
    vpi_printf("%s", kind_name(vpi_get(vpiType, typespec)));
    if (vpi_get(vpiType, typespec) == vpiArrayTypespec) {
        vpi_printf(" %s", array_name(vpi_get(vpiArrayType, typespec)));
        if (element != NULL) {
            vpi_printf(" of %s", kind_name(vpi_get(vpiType, element)));
        }
    }
    vpi_printf(" decl %ld:%ld\n", bound(vpiLeftRange, object),
        bound(vpiRightRange, object));
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle module = vpi_handle_by_name("top", NULL);
    vpiHandle functions = vpi_iterate(vpiFunction, module);
    vpiHandle function;
    (void)data;
    while (functions != NULL && (function = vpi_scan(functions)) != NULL) {
        const char* name = vpi_get_str(vpiName, function);
        vpiHandle formals = vpi_iterate(vpiIODecl, function);
        vpiHandle formal;
        vpiHandle result = vpi_handle(vpiReturn, function);
        while (formals != NULL && (formal = vpi_scan(formals)) != NULL) {
            show(name, vpi_get_str(vpiName, formal), formal);
        }
        if (result != NULL) {
            show(name, "result", result);
        }
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

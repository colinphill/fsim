// SPDX-License-Identifier: Apache-2.0
// VPI virtual interface conformance plug-in (IEEE 1800-2023 37.29, 37.30):
// the virtual interface variables of a module and of a class (its defn and
// a live object) with vpiTypespec, vpiIsModPort, vpiExpr and vpiActual, and
// the interface typespec with vpiDefName, vpiIsModPort and vpiParent.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static const char* full_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiFullName, object)) : "-";
}

static void variable(const char* label, vpiHandle object)
{
    vpiHandle spec = vpi_handle(vpiTypespec, object);
    vpiHandle parent = spec != NULL ? vpi_handle(vpiParent, spec) : NULL;
    vpi_printf("#c %s type=%s name=%s modport=%d expr=%s actual=%s\n", label,
        text(vpi_get_str(vpiType, object)), text(vpi_get_str(vpiName, object)),
        (int)vpi_get(vpiIsModPort, object),
        full_of(vpi_handle(vpiExpr, object)),
        full_of(vpi_handle(vpiActual, object)));
    vpi_printf("#c %s typespec type=%s defname=%s modport=%d parent=%s\n", label,
        spec != NULL ? text(vpi_get_str(vpiType, spec)) : "-",
        spec != NULL ? text(vpi_get_str(vpiDefName, spec)) : "-",
        spec != NULL ? (int)vpi_get(vpiIsModPort, spec) : -1,
        parent != NULL ? text(vpi_get_str(vpiDefName, parent)) : "-");
}

static void class_variables(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiVariables, owner);
    vpiHandle object;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        char name[64];
        if (vpi_get(vpiType, object) != vpiVirtualInterfaceVar) {
            continue;
        }
        snprintf(name, sizeof name, "%s.%s", label,
            text(vpi_get_str(vpiName, object)));
        variable(name, object);
    }
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle iterator;
    vpiHandle object;
    vpiHandle defn = vpi_handle_by_name("pk::Xactor", NULL);
    vpiHandle holder = vpi_handle_by_name("top.t", NULL);
    int count = 0;
    (void)data;

    variable("vi", vpi_handle_by_name("top.vi", NULL));
    variable("vn", vpi_handle_by_name("top.vn", NULL));
    variable("vm", vpi_handle_by_name("top.vm", NULL));
    vpi_printf("#c plain type=%s\n",
        text(vpi_get_str(vpiType, vpi_handle_by_name("top.plain", NULL))));
    iterator = vpi_iterate(vpiVirtualInterfaceVar, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c top virtual %s\n", text(vpi_get_str(vpiName, object)));
    }
    iterator = vpi_iterate(vpiVariables, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        ++count;
    }
    vpi_printf("#c top variables=%d\n", count);
    class_variables("Xactor", defn);
    object = vpi_handle(vpiClassObj, holder);
    class_variables("t", object);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = check;
    callback.time = &time;
    vpi_register_cb(&callback);
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

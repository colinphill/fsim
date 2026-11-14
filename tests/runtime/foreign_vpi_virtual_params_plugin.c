// SPDX-License-Identifier: Apache-2.0
// The parameter assigns of interface typespecs (IEEE 1800-2023 37.30, 37.32
// detail 3): every parameter of the interface, its right side the actual the
// type gives (evaluated where the type is written) or the declared default.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "none";
}

static void assigns(const char* label, vpiHandle typespec)
{
    vpiHandle iterator;
    vpiHandle assign;
    char values[128] = "";
    int count = 0;
    if (typespec == NULL) {
        vpi_printf("#c %s no typespec\n", label);
        return;
    }
    iterator = vpi_iterate(vpiParamAssign, typespec);
    while (iterator != NULL && (assign = vpi_scan(iterator)) != NULL) {
        vpiHandle left = vpi_handle(vpiLhs, assign);
        vpiHandle right = vpi_handle(vpiRhs, assign);
        s_vpi_value value;
        char one[64];
        memset(&value, 0, sizeof value);
        value.format = vpiIntVal;
        if (right != NULL) {
            vpi_get_value(right, &value);
        }
        snprintf(one, sizeof one, "%s%s=%d", count > 0 ? "," : "",
            left != NULL ? text(vpi_get_str(vpiName, left)) : "none",
            (int)value.value.integer);
        strncat(values, one, sizeof values - strlen(values) - 1U);
        ++count;
    }
    vpi_printf("#c %s defname=%s assigns=%d %s\n", label,
        text(vpi_get_str(vpiDefName, typespec)), count, values);
}

static void variable(const char* name)
{
    vpiHandle object = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    assigns(name, object != NULL ? vpi_handle(vpiTypespec, object) : NULL);
}

static void properties(const char* label, vpiHandle owner)
{
    vpiHandle list = owner != NULL ? vpi_iterate(vpiVirtualInterfaceVar, owner)
                                   : NULL;
    vpiHandle object;
    while (list != NULL && (object = vpi_scan(list)) != NULL) {
        char name[96];
        snprintf(name, sizeof name, "%s.%s", label,
            text(vpi_get_str(vpiName, object)));
        assigns(name, vpi_handle(vpiTypespec, object));
    }
}

static PLI_INT32 report(p_cb_data data)
{
    vpiHandle list;
    vpiHandle object;
    (void)data;
    variable("top.vd");
    variable("top.vl");
    variable("top.vq");
    variable("top.vn");
    variable("top.ve");
    variable("pkg::pv");
    list = vpi_iterate(vpiTypedef, vpi_handle_by_name((PLI_BYTE8*)"pkg::", NULL));
    while (list != NULL && (object = vpi_scan(list)) != NULL) {
        assigns("pkg typedef", object);
    }
    properties("pkg::Box", vpi_handle_by_name((PLI_BYTE8*)"pkg::Box", NULL));
    object = vpi_handle_by_name((PLI_BYTE8*)"top.b5", NULL);
    properties("top.b5", object != NULL ? vpi_handle(vpiClassObj, object) : NULL);
    object = vpi_handle_by_name((PLI_BYTE8*)"top.b", NULL);
    properties("top.b", object != NULL ? vpi_handle(vpiClassObj, object) : NULL);
    object = vpi_handle_by_name((PLI_BYTE8*)"top.b0", NULL);
    properties("top.b0", object != NULL ? vpi_handle(vpiClassObj, object) : NULL);
    object = vpi_handle_by_name((PLI_BYTE8*)"top.s7", NULL);
    properties("top.s7", object != NULL ? vpi_handle(vpiClassObj, object) : NULL);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbEndOfSimulation;
    data.cb_rtn = report;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

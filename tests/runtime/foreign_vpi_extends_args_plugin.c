// SPDX-License-Identifier: Apache-2.0
// The vpiArgument objects of a class's extends object: the constructor
// arguments `extends B(args)` passes (IEEE 1800-2023 8.17, 37.32).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static void arguments(const char* name, const char* label)
{
    vpiHandle defn = vpi_handle_by_name((char*)name, NULL);
    vpiHandle extends = defn != NULL ? vpi_handle(vpiExtends, defn) : NULL;
    vpiHandle items = extends != NULL ? vpi_iterate(vpiArgument, extends) : NULL;
    vpiHandle item;
    int index = 0;
    vpi_printf("#c %s extends arguments:\n", label);
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        s_vpi_value value;
        value.format = vpiIntVal;
        vpi_get_value(item, &value);
        vpi_printf("#c arg %d type=%s value=%d\n", index++,
            vpi_get_str(vpiType, item), (int)value.value.integer);
    }
}

static PLI_INT32 late(p_cb_data data);

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle defn;
    vpiHandle extends;
    (void)data;
    // A delayed callback is registered from a callback of the running
    // simulation, not from the startup routine.
    {
        s_cb_data callback;
        s_vpi_time delay;
        memset(&callback, 0, sizeof callback);
        memset(&delay, 0, sizeof delay);
        delay.type = vpiSimTime;
        delay.low = 1;
        callback.reason = cbAfterDelay;
        callback.time = &delay;
        callback.cb_rtn = late;
        (void)vpi_register_cb(&callback);
    }
    arguments("top.Derived", "Derived");
    arguments("top.Named", "Named");
    defn = vpi_handle_by_name("top.Base", NULL);
    extends = defn != NULL ? vpi_handle(vpiExtends, defn) : NULL;
    vpi_printf("#c Base extends: %s\n", extends == NULL ? "none" : "present");
    {
        vpiHandle typespec = vpi_handle(vpiTypespec, vpi_handle_by_name("top.pv", NULL));
        vpiHandle assigns = typespec != NULL ? vpi_iterate(vpiParamAssign, typespec) : NULL;
        vpiHandle assign;
        vpi_printf("#c P#(8) assignments:\n");
        while (assigns != NULL && (assign = vpi_scan(assigns)) != NULL) {
            s_vpi_value value;
            vpiHandle lhs = vpi_handle(vpiLhs, assign);
            vpiHandle rhs = vpi_handle(vpiRhs, assign);
            value.format = vpiIntVal;
            if (rhs != NULL) {
                vpi_get_value(rhs, &value);
            } else {
                value.value.integer = -1;
            }
            vpi_printf("#c assign lhs=%s type=%s rhs=%d\n",
                lhs != NULL ? vpi_get_str(vpiName, lhs) : "-",
                vpi_get_str(vpiType, assign), (int)value.value.integer);
        }
    }
    {
        vpiHandle derived = vpi_handle_by_name("top.PD", NULL);
        vpiHandle base = derived != NULL ? vpi_handle(vpiExtends, derived) : NULL;
        vpiHandle spec = base != NULL ? vpi_handle(vpiClassTypespec, base) : NULL;
        vpiHandle assigns = spec != NULL ? vpi_iterate(vpiParamAssign, spec) : NULL;
        vpiHandle assign;
        vpi_printf("#c PD base assignments:\n");
        while (assigns != NULL && (assign = vpi_scan(assigns)) != NULL) {
            vpiHandle lhs = vpi_handle(vpiLhs, assign);
            s_vpi_value value;
            value.format = vpiIntVal;
            value.value.integer = -1;
            if (vpi_handle(vpiRhs, assign) != NULL) {
                vpi_get_value(vpi_handle(vpiRhs, assign), &value);
            }
            vpi_printf("#c base assign lhs=%s rhs=%d\n",
                lhs != NULL ? vpi_get_str(vpiName, lhs) : "-",
                (int)value.value.integer);
        }
    }
    return 0;
}

// A non-static data member of the object a class variable refers to is found
// by its full name (IEEE 1800-2023 37.33 detail 9).
static PLI_INT32 late(p_cb_data data)
{
    vpiHandle member = vpi_handle_by_name("top.d.a", NULL);
    s_vpi_value value;
    (void)data;
    value.format = vpiIntVal;
    value.value.integer = -1;
    if (member != NULL) {
        vpi_get_value(member, &value);
    }
    vpi_printf("#c member top.d.a %s value=%d\n",
        member != NULL ? "found" : "none", (int)value.value.integer);
    vpi_printf("#c member top.d.zzz %s\n",
        vpi_handle_by_name("top.d.zzz", NULL) != NULL ? "found" : "none");
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

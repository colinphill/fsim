// SPDX-License-Identifier: Apache-2.0
// The variables of a package as VPI objects (IEEE 1800-2023 37.10, 37.17,
// 37.29): found by name, listed by vpiVariables and vpiVirtualInterfaceVar
// of the package, with their scope, value, a write, and value-change
// callbacks. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "none";
}

static PLI_UINT32 now(void)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    return time.low;
}

// The value of an object as text: a decimal string, a real, a string, or
// for a virtual interface variable the interface it refers to.
static void value_text(vpiHandle object, char* out, size_t size)
{
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    switch (vpi_get(vpiType, object)) {
    case vpiVirtualInterfaceVar: {
        vpiHandle actual = vpi_handle(vpiActual, object);
        snprintf(out, size, "actual=%s",
            actual != NULL ? text(vpi_get_str(vpiFullName, actual)) : "null");
        return;
    }
    case vpiRealVar:
        value.format = vpiRealVal;
        vpi_get_value(object, &value);
        snprintf(out, size, "%g", value.value.real);
        return;
    case vpiStringVar:
        value.format = vpiStringVal;
        vpi_get_value(object, &value);
        snprintf(out, size, "\"%s\"", text(value.value.str));
        return;
    case vpiRegArray:
        snprintf(out, size, "array");
        return;
    default:
        value.format = vpiDecStrVal;
        vpi_get_value(object, &value);
        snprintf(out, size, "%s", text(value.value.str));
        return;
    }
}

static void show(const char* label, vpiHandle object)
{
    char value[128];
    vpiHandle scope = vpi_handle(vpiScope, object);
    vpiHandle typespec = vpi_handle(vpiTypespec, object);
    value_text(object, value, sizeof value);
    vpi_printf("#c t=%u %s %s type=%s full=%s scope=%s size=%d typespec=%s "
               "value=%s\n",
        (unsigned)now(), label, text(vpi_get_str(vpiName, object)),
        text(vpi_get_str(vpiType, object)),
        text(vpi_get_str(vpiFullName, object)),
        scope != NULL ? text(vpi_get_str(vpiFullName, scope)) : "null",
        (int)vpi_get(vpiSize, object),
        typespec != NULL ? text(vpi_get_str(vpiType, typespec)) : "none",
        value);
}

static void list(const char* label, PLI_INT32 relation, vpiHandle scope)
{
    vpiHandle iterator = vpi_iterate(relation, scope);
    vpiHandle object;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        show(label, object);
    }
}

static PLI_INT32 mid(p_cb_data data);

// The value-change callbacks print the new value; the one of pkg::pl also
// writes two other variables and asks for a snapshot of every package
// variable once the time step's active events are done.
static PLI_INT32 changed(p_cb_data data)
{
    char value[128];
    value_text(data->obj, value, sizeof value);
    vpi_printf("#c t=%u cb %s %s\n", (unsigned)now(),
        text(vpi_get_str(vpiFullName, data->obj)), value);
    if (strcmp(text(vpi_get_str(vpiName, data->obj)), "pl") == 0) {
        s_cb_data callback;
        s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
        s_vpi_value put;
        memset(&put, 0, sizeof put);
        put.format = vpiIntVal;
        put.value.integer = 42;
        vpi_put_value(vpi_handle_by_name((PLI_BYTE8*)"pkg::pu", NULL), &put,
            NULL, vpiNoDelay);
        put.value.integer = 7;
        vpi_put_value(vpi_handle_by_index(
                          vpi_handle_by_name((PLI_BYTE8*)"pkg::pa", NULL), 1),
            &put, NULL, vpiNoDelay);
        memset(&callback, 0, sizeof callback);
        callback.reason = cbReadOnlySynch;
        callback.cb_rtn = mid;
        callback.time = &time;
        (void)vpi_register_cb(&callback);
    }
    return 0;
}

static void watch(const char* name)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    static s_vpi_value value = { vpiSuppressVal, { 0 } };
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbValueChange;
    data.cb_rtn = changed;
    data.obj = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    data.time = &time;
    data.value = &value;
    if (data.obj == NULL) {
        vpi_printf("#c no %s\n", name);
        return;
    }
    (void)vpi_register_cb(&data);
}

static void report(const char* label)
{
    vpiHandle package = vpi_handle_by_name((PLI_BYTE8*)"pkg::", NULL);
    vpiHandle word;
    if (package == NULL) {
        vpi_printf("#c %s no package\n", label);
        return;
    }
    list(label, vpiVariables, package);
    list("vi", vpiVirtualInterfaceVar, package);
    word = vpi_handle_by_index(
        vpi_handle_by_name((PLI_BYTE8*)"pkg::pa", NULL), 1);
    if (word != NULL) {
        show("word", word);
    }
}

static PLI_INT32 mid(p_cb_data data)
{
    (void)data;
    report("mid");
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    watch("pkg::px");
    watch("pkg::pu");
    watch("pkg::pl");
    watch("pkg::pv");
    return 0;
}

static PLI_INT32 finish(p_cb_data data)
{
    (void)data;
    report("end");
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
    data.reason = cbEndOfSimulation;
    data.cb_rtn = finish;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

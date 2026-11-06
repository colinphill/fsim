// SPDX-License-Identifier: Apache-2.0
// VPI list-operation and string-watch plug-in: the vpiListOp connection expression of an instance
// array (IEEE 1800-2023 37.11 detail 1), the typespec of a generate scope's
// parameter declared with a typedef (37.28, 37.85), and cbValueChange on a
// string element of an unpacked array and on a string class property
// (38.36.1).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static void connections(const char* array_name)
{
    vpiHandle array = vpi_handle_by_name((PLI_BYTE8*)array_name, NULL);
    vpiHandle list = array != NULL ? vpi_handle(vpiExpr, array) : NULL;
    vpiHandle parent;
    vpiHandle iterator;
    vpiHandle operand;
    int count = 0;
    if (list == NULL) {
        vpi_printf("#c %s expr missing\n", array_name);
        return;
    }
    iterator = vpi_iterate(vpiOperand, list);
    while (iterator != NULL && (operand = vpi_scan(iterator)) != NULL) {
        ++count;
    }
    parent = vpi_handle(vpiParent, list);
    vpi_printf("#c %s expr type=%s optype=%d operands=%d parent=%s\n",
        array_name, text(vpi_get_str(vpiType, list)),
        (int)vpi_get(vpiOpType, list), count,
        parent != NULL ? text(vpi_get_str(vpiName, parent)) : "-");
    count = 0;
    iterator = vpi_iterate(vpiOperand, list);
    while (iterator != NULL && (operand = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %s operand %d type=%s name=%s optype=%d\n", array_name,
            count, text(vpi_get_str(vpiType, operand)),
            text(vpi_get_str(vpiName, operand)),
            (int)vpi_get(vpiOpType, operand));
        ++count;
    }
}

static void generate_parameter(const char* name)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    char path[32] = "top.g.";
    vpiHandle object;
    vpiHandle typespec;
    strcat(path, name);
    object = vpi_handle_by_name(path, NULL);
    if (object == NULL) {
        vpi_printf("#c g %s missing\n", name);
        return;
    }
    typespec = vpi_handle(vpiTypespec, object);
    vpi_get_value(object, &value);
    vpi_printf("#c g %s type=%s typespec=%s name=%s value=%d\n", name,
        text(vpi_get_str(vpiType, object)),
        typespec != NULL ? text(vpi_get_str(vpiType, typespec)) : "-",
        typespec != NULL ? text(vpi_get_str(vpiName, typespec)) : "-",
        (int)value.value.integer);
}

static PLI_INT32 changed(p_cb_data data)
{
    vpi_printf("#c change %s value=%s\n", (const char*)data->user_data,
        data->value != NULL && data->value->format == vpiStringVal
            ? text(data->value->value.str)
            : "-");
    return 0;
}

static void watch(vpiHandle object, const char* label)
{
    static s_vpi_value value = { vpiStringVal, { 0 } };
    s_cb_data data;
    if (object == NULL) {
        vpi_printf("#c watch %s missing\n", label);
        return;
    }
    memset(&data, 0, sizeof data);
    data.reason = cbValueChange;
    data.cb_rtn = changed;
    data.obj = object;
    data.value = &value;
    data.user_data = (PLI_BYTE8*)label;
    vpi_printf("#c watch %s %d\n", label, vpi_register_cb(&data) != NULL);
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle array = vpi_handle_by_name("top.sa", NULL);
    vpiHandle iterator;
    vpiHandle member;
    vpiHandle object;
    (void)data;
    connections("top.ua");
    connections("top.ub");
    generate_parameter("NP");
    generate_parameter("GP");
    generate_parameter("BP");
    // A string element of an unpacked array.
    watch(array != NULL ? vpi_handle_by_index(array, 1) : NULL, "sa[1]");
    // A string property of a live class object.
    object = vpi_handle_by_name("top.it", NULL);
    object = object != NULL ? vpi_handle(vpiClassObj, object) : NULL;
    iterator = object != NULL ? vpi_iterate(vpiVariables, object) : NULL;
    while (iterator != NULL && (member = vpi_scan(iterator)) != NULL) {
        if (strcmp(text(vpi_get_str(vpiName, member)), "label") == 0) {
            watch(member, "label");
        }
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 2, 0.0 };
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

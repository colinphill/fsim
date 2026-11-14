// SPDX-License-Identifier: Apache-2.0
// cbValueChange on class object properties (IEEE 1800-2023 37.33, 38.36.1):
// at t=1 watches the integral property w, the element arr[2] of an array
// property and the element sarr[1] of a static array property, reports the
// refusal of a string, a queue and a class-handle property, and at t=5
// reads the static element. Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static PLI_INT32 changed(p_cb_data data)
{
    vpi_printf("#c change %s value=%d t=%u\n", (const char*)data->user_data,
        data->value != NULL ? (int)data->value->value.integer : -1,
        data->time != NULL ? (unsigned)data->time->low : 0U);
    return 0;
}

static vpiHandle member(vpiHandle object, const char* name)
{
    vpiHandle iterator = vpi_iterate(vpiVariables, object);
    vpiHandle item;
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        const char* text = vpi_get_str(vpiName, item);
        if (text != NULL && strcmp(text, name) == 0) {
            vpi_free_object(iterator);
            return item;
        }
    }
    return NULL;
}

static void watch(vpiHandle object, PLI_BYTE8* label)
{
    static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    static s_vpi_value value = { vpiIntVal, { 0 } };
    s_cb_data data;
    s_vpi_error_info error;
    if (object == NULL) {
        vpi_printf("#c watch %s missing\n", label);
        return;
    }
    memset(&data, 0, sizeof data);
    data.reason = cbValueChange;
    data.cb_rtn = changed;
    data.obj = object;
    data.time = &time;
    data.value = &value;
    data.user_data = label;
    if (vpi_register_cb(&data) != NULL) {
        vpi_printf("#c watch %s accepted\n", label);
        return;
    }
    memset(&error, 0, sizeof error);
    vpi_printf("#c watch %s refused: %s\n", label,
        vpi_chk_error(&error) != 0 && error.message != NULL ? error.message
                                                             : "no error");
}

static vpiHandle object_of(void)
{
    vpiHandle variable = vpi_handle_by_name("top.it", NULL);
    return variable != NULL ? vpi_handle(vpiClassObj, variable) : NULL;
}

static PLI_INT32 read_static(p_cb_data data)
{
    vpiHandle object = object_of();
    vpiHandle array = object != NULL ? member(object, "sarr") : NULL;
    vpiHandle element = array != NULL ? vpi_handle_by_index(array, 1) : NULL;
    s_vpi_value value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    if (element != NULL) {
        vpi_get_value(element, &value);
    }
    vpi_printf("#c read sarr[1] value=%d\n",
        element != NULL ? (int)value.value.integer : -1);
    return 0;
}

static PLI_INT32 place(p_cb_data data)
{
    vpiHandle object = object_of();
    vpiHandle array;
    (void)data;
    if (object == NULL) {
        vpi_printf("#c no object\n");
        return 0;
    }
    watch(member(object, "w"), "w");
    array = member(object, "arr");
    watch(array != NULL ? vpi_handle_by_index(array, 2) : NULL, "arr[2]");
    array = member(object, "sarr");
    watch(array != NULL ? vpi_handle_by_index(array, 1) : NULL, "sarr[1]");
    watch(member(object, "label"), "label");
    watch(member(object, "q"), "q");
    watch(member(object, "next"), "next");
    return 0;
}

static void at(unsigned when, PLI_INT32 (*routine)(p_cb_data))
{
    s_vpi_time time;
    s_cb_data data;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    time.low = when;
    memset(&data, 0, sizeof data);
    data.reason = cbAfterDelay;
    data.cb_rtn = routine;
    data.time = &time;
    (void)vpi_register_cb(&data);
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    at(1U, place);
    at(5U, read_static);
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

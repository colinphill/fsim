// SPDX-License-Identifier: Apache-2.0
// Container properties of a live class object (IEEE 1800-2023 37.17, 37.20,
// 37.33, 38.15, 38.34, 38.36.1): size, array type, elements by iteration and
// by index with their declared types, class handle, structure and nested
// container elements, a value written to an element, the array value
// routines, and a size-change callback. A multi-dimensional fixed array is
// one array level per dimension, selected by index or by multi-index, and a
// fixed array is indexed by its declared range (7.4.5, 37.20, 38.16). At the end of time 1 the plug-in
// reads them; the queue then grows at time 2.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static int integer_of(vpiHandle object)
{
    s_vpi_value value;
    value.format = vpiIntVal;
    value.value.integer = -1;
    if (object != NULL) {
        vpi_get_value(object, &value);
    }
    return (int)value.value.integer;
}

// One element, member or nested array, by what it is.
static void dump(const char* label, vpiHandle object)
{
    char text[96];
    vpiHandle items;
    vpiHandle item;
    int index = 0;
    switch (vpi_get(vpiType, object)) {
    case vpiArrayVar:
        vpi_printf("#c %s array size=%d\n", label, (int)vpi_get(vpiSize, object));
        items = vpi_iterate(vpiReg, object);
        while (items != NULL && (item = vpi_scan(items)) != NULL) {
            snprintf(text, sizeof text, "%s[%d]", label, index++);
            dump(text, item);
        }
        break;
    case vpiStructVar:
        vpi_printf("#c %s struct members=%d\n", label,
            (int)vpi_get(vpiSize, object));
        items = vpi_iterate(vpiMember, object);
        while (items != NULL && (item = vpi_scan(items)) != NULL) {
            snprintf(text, sizeof text, "%s member %d", label, index++);
            dump(text, item);
        }
        break;
    case vpiRealVar: {
        s_vpi_value value;
        value.format = vpiRealVal;
        value.value.real = -1.0;
        vpi_get_value(object, &value);
        vpi_printf("#c %s type=vpiRealVar value=%g\n", label, value.value.real);
        break;
    }
    case vpiClassVar:
        vpi_printf("#c %s class obj=%s\n", label,
            vpi_handle(vpiClassObj, object) != NULL ? "set" : "null");
        break;
    default:
        vpi_printf("#c %s type=%s value=%d\n", label,
            vpi_get_str(vpiType, object), integer_of(object));
        break;
    }
}

static void show(const char* name, int key)
{
    char path[64];
    char text[96];
    vpiHandle array;
    vpiHandle items;
    vpiHandle item;
    int index = 0;
    snprintf(path, sizeof path, "top.b.%s", name);
    array = vpi_handle_by_name(path, NULL);
    if (array == NULL) {
        vpi_printf("#c %s missing\n", name);
        return;
    }
    vpi_printf("#c %s type=%s size=%d arraytype=%d\n", name,
        vpi_get_str(vpiType, array), (int)vpi_get(vpiSize, array),
        (int)vpi_get(vpiArrayType, array));
    items = vpi_iterate(vpiReg, array);
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        snprintf(text, sizeof text, "%s[%d]", name, index++);
        dump(text, item);
    }
    if (key >= 0) {
        vpi_printf("#c %s by index %d value=%d\n", name, key,
            integer_of(vpi_handle_by_index(array, key)));
    }
}

static PLI_INT32 resized(p_cb_data data)
{
    vpi_printf("#c sizechange q size=%d\n", (int)data->value->value.integer);
    return 0;
}

static PLI_INT32 finished(p_cb_data data)
{
    (void)data;
    vpi_printf("#c t2 done\n");
    return 0;
}

// A real element read and written through vpiRealVal.
static void real_write(void)
{
    vpiHandle first_real = vpi_handle_by_index(
        vpi_handle_by_name("top.b.rl", NULL), 0);
    s_vpi_value value;
    value.format = vpiRealVal;
    value.value.real = 3.25;
    (void)vpi_put_value(first_real, &value, NULL, vpiNoDelay);
    value.value.real = -1.0;
    vpi_get_value(first_real, &value);
    vpi_printf("#c put rl[0] value=%g\n", value.value.real);
}

// A value written to an element, and the array routines over a dynamic
// array (38.15, 38.34).
static void writes(void)
{
    vpiHandle dyn = vpi_handle_by_name("top.b.dyn", NULL);
    vpiHandle second = vpi_handle_by_index(dyn, 1);
    s_vpi_value value;
    s_vpi_arrayvalue array;
    PLI_INT32 values[2] = { 100, 200 };
    PLI_INT32 out[2] = { 0, 0 };
    PLI_INT32 untouched[3] = { -1, -1, -1 };
    PLI_INT32 start = 0;
    vpiHandle fixed = vpi_handle_by_name("top.b.bs", NULL);
    value.format = vpiIntVal;
    value.value.integer = 77;
    (void)vpi_put_value(second, &value, NULL, vpiNoDelay);
    vpi_printf("#c put dyn[1] value=%d\n", integer_of(second));
    // The array routines take a fixed-size unpacked array (38.16, 38.33).
    memset(&array, 0, sizeof array);
    array.format = vpiIntVal;
    array.value.integers = values;
    vpi_put_value_array(fixed, &array, &start, 2);
    array.flags = vpiUserAllocFlag;
    array.value.integers = out;
    vpi_get_value_array(fixed, &array, &start, 2);
    vpi_printf("#c array bs %d %d\n", (int)out[0], (int)out[1]);
    array.value.integers = untouched;
    vpi_get_value_array(dyn, &array, &start, 3);
    vpi_printf("#c array dyn %s\n", untouched[0] == -1 ? "refused" : "read");
}

// The levels of a multi-dimensional fixed array and a fixed array with a
// descending declared range.
static void dimensions(void)
{
    vpiHandle grid = vpi_handle_by_name("top.b.grid", NULL);
    vpiHandle down = vpi_handle_by_name("top.b.down", NULL);
    vpiHandle row = vpi_handle_by_index(grid, 1);
    PLI_INT32 at[2] = { 1, 2 };
    PLI_INT32 from[2] = { 0, 2 };
    PLI_INT32 start[1] = { 3 };
    PLI_INT32 out[3] = { -1, -1, -1 };
    s_vpi_arrayvalue array;
    vpi_printf("#c grid[1] type=%s size=%d\n", vpi_get_str(vpiType, row),
        (int)vpi_get(vpiSize, row));
    vpi_printf("#c grid[1][2] by index value=%d multi value=%d\n",
        integer_of(vpi_handle_by_index(row, 2)),
        integer_of(vpi_handle_by_multi_index(grid, 2, at)));
    vpi_printf("#c grid[2] %s\n",
        vpi_handle_by_index(grid, 2) == NULL ? "null" : "found");
    memset(&array, 0, sizeof array);
    array.format = vpiIntVal;
    array.flags = vpiUserAllocFlag;
    array.value.integers = out;
    vpi_get_value_array(grid, &array, from, 3);
    vpi_printf("#c array grid from [0][2] %d %d %d\n", (int)out[0],
        (int)out[1], (int)out[2]);
    vpi_printf("#c down[4] value=%d down[1] value=%d down[0] %s\n",
        integer_of(vpi_handle_by_index(down, 4)),
        integer_of(vpi_handle_by_index(down, 1)),
        vpi_handle_by_index(down, 0) == NULL ? "null" : "found");
    vpi_printf("#c down[3] index=%d\n",
        (int)vpi_get(vpiIndex, vpi_handle_by_index(down, 3)));
    out[0] = out[1] = -1;
    vpi_get_value_array(down, &array, start, 2);
    vpi_printf("#c array down from [3] %d %d\n", (int)out[0], (int)out[1]);
}

static PLI_INT32 first(p_cb_data data)
{
    s_cb_data watch;
    s_vpi_value value;
    s_vpi_time delay;
    (void)data;
    show("q", 1);
    show("dyn", -1);
    show("aa", 5);
    show("bs", -1);
    show("by", -1);
    show("sh", -1);
    show("col", -1);
    show("peers", -1);
    show("ps", -1);
    show("nest", -1);
    show("rl", -1);
    show("grid", -1);
    show("down", -1);
    dimensions();
    real_write();
    writes();
    memset(&watch, 0, sizeof watch);
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    watch.reason = cbSizeChange;
    watch.cb_rtn = resized;
    watch.obj = vpi_handle_by_name("top.b.q", NULL);
    watch.value = &value;
    (void)vpi_register_cb(&watch);
    memset(&watch, 0, sizeof watch);
    memset(&delay, 0, sizeof delay);
    delay.type = vpiSimTime;
    delay.low = 1;
    watch.reason = cbReadOnlySynch;
    watch.time = &delay;
    watch.cb_rtn = finished;
    (void)vpi_register_cb(&watch);
    return 0;
}

// Delayed callbacks are registered from a callback of the running
// simulation, not from the startup routine.
static PLI_INT32 begin(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time delay;
    (void)data;
    memset(&callback, 0, sizeof callback);
    memset(&delay, 0, sizeof delay);
    delay.type = vpiSimTime;
    delay.low = 1;
    callback.reason = cbReadOnlySynch;
    callback.time = &delay;
    callback.cb_rtn = first;
    (void)vpi_register_cb(&callback);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = begin;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

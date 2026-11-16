// SPDX-License-Identifier: Apache-2.0
// VPI conformance plug-in for memories with millions of words (IEEE
// 1800-2023 37.17, 38.34, 38.36.1): words by index, by name and by
// iteration, their names, sizes and values, vpi_compare_objects on two
// handles to one word, vpi_put_value at once and with an inertial delay,
// value change callbacks on words, the count and order of an iteration
// over every word, and puts with an inertial delay of zero (as cocotb makes
// them) to every word of a memory.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static vpiHandle memory(const char* name)
{
    return vpi_handle_by_name((PLI_BYTE8*)name, NULL);
}

static int value_of(vpiHandle object)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpi_get_value(object, &value);
    return (int)value.value.integer;
}

static PLI_UINT32 now(void)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    return time.low;
}

static void put(vpiHandle object, int value, PLI_UINT32 delay)
{
    s_vpi_value put_value = { vpiIntVal, { 0 } };
    s_vpi_time time = { vpiSimTime, 0, delay, 0.0 };
    put_value.value.integer = value;
    vpi_put_value(object, &put_value, delay == 0U ? NULL : &time,
        delay == 0U ? vpiNoDelay : vpiInertialDelay);
}

// An inertial put of no delay, as cocotb makes it.
static void put_inertial(vpiHandle object, int value)
{
    s_vpi_value put_value = { vpiIntVal, { 0 } };
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    put_value.value.integer = value;
    vpi_put_value(object, &put_value, &time, vpiInertialDelay);
}

// Every word of `name` takes its index plus one, through an inertial put
// of no delay.
static void fill(const char* name)
{
    vpiHandle array = memory(name);
    const int size = (int)vpi_get(vpiSize, array);
    for (int index = 0; index < size; ++index) {
        put_inertial(vpi_handle_by_index(array, index), (index + 1) & 0xFF);
    }
}

static void describe(const char* what, vpiHandle word)
{
    vpi_printf("#c %s %s %s type %d size %d value %d\n", what,
        vpi_get_str(vpiName, word), vpi_get_str(vpiFullName, word),
        (int)vpi_get(vpiType, word), (int)vpi_get(vpiSize, word),
        value_of(word));
}

static PLI_INT32 changed(p_cb_data data)
{
    vpi_printf("#c t=%u cb %s=%d\n", (unsigned)now(),
        vpi_get_str(vpiFullName, data->obj),
        (int)data->value->value.integer);
    return 0;
}

static void watch(vpiHandle word)
{
    static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    static s_vpi_value value = { vpiIntVal, { 0 } };
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbValueChange;
    callback.cb_rtn = changed;
    callback.obj = word;
    callback.time = &time;
    callback.value = &value;
    vpi_register_cb(&callback);
}

static void walk(const char* name)
{
    vpiHandle iterator = vpi_iterate(vpiMemoryWord, memory(name));
    vpiHandle word;
    long count = 0;
    long sum = 0;
    while ((word = vpi_scan(iterator)) != NULL) {
        if (count < 2) {
            vpi_printf("#c %s word %ld %s\n", name, count,
                vpi_get_str(vpiName, word));
        }
        sum += value_of(word);
        ++count;
    }
    vpi_printf("#c %s walked %ld words, sum %ld\n", name, count, sum);
}

static PLI_INT32 at1(p_cb_data data)
{
    (void)data;
    vpiHandle mem = memory("top.mem");
    vpi_printf("#c top.mem size %d\n", (int)vpi_get(vpiSize, mem));
    describe("last", vpi_handle_by_index(mem, 16777215));
    describe("down[1]", vpi_handle_by_index(memory("top.down"), 1));
    vpiHandle row = vpi_handle_by_index(memory("top.two"), -3);
    describe("two[-3][1000]", vpi_handle_by_index(row, 1000));
    vpiHandle named = memory("top.mem[12345678]");
    vpiHandle indexed = vpi_handle_by_index(mem, 12345678);
    vpi_printf("#c same word %d, out of range %d\n",
        (int)vpi_compare_objects(named, indexed),
        vpi_handle_by_index(mem, 16777216) == NULL);
    put(named, 42, 0U);
    vpi_printf("#c put 42 reads %d\n", value_of(indexed));
    put(vpi_handle_by_index(mem, 8), 77, 1U);
    watch(vpi_handle_by_index(mem, 7));
    watch(vpi_handle_by_index(memory("top.down"), 2));
    walk("top.mem");
    walk("top.down");
    fill("top.bridged");
    // A later inertial put to a word cancels its earlier ones, and one to
    // the word's own object cancels those made before it had one.
    vpiHandle bridged = memory("top.bridged");
    put_inertial(vpi_handle_by_index(bridged, 10), 99);
    put_inertial(vpi_handle_by_index(bridged, 10), 11);
    watch(vpi_handle_by_index(bridged, 10));
    watch(vpi_handle_by_index(bridged, 40));
    put_inertial(vpi_handle_by_index(bridged, 40), 5);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    s_cb_data callback;
    static s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = at1;
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

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = {
    register_start,
    NULL,
};

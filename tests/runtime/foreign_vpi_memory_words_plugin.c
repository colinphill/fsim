// SPDX-License-Identifier: Apache-2.0
// VPI memory word conformance plug-in (IEEE 1800-2023 37.17, 38.34,
// 38.36.1): vpi_put_value on words of a variable array and of a net array,
// at once and with an inertial delay, each read back at once (a word with a
// value change callback too), and value change callbacks on words that
// hear those puts and a process's write.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static vpiHandle word(const char* array, int index)
{
    return vpi_handle_by_index(
        vpi_handle_by_name((PLI_BYTE8*)array, NULL), index);
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

// Puts `number` on array[index] (vpiNoDelay, or an inertial delay of
// `delay`) and reads the word back at once.
static void put(const char* array, int index, int number, PLI_UINT32 delay)
{
    vpiHandle object = word(array, index);
    s_vpi_value value = { vpiIntVal, { 0 } };
    s_vpi_time time = { vpiSimTime, 0, delay, 0.0 };
    value.value.integer = number;
    vpi_put_value(object, &value, delay != 0U ? &time : NULL,
        delay != 0U ? vpiInertialDelay : vpiNoDelay);
    vpi_printf("#c t=%u put %s=%d after %u read %d\n", (unsigned)now(),
        vpi_get_str(vpiFullName, object), number, (unsigned)delay,
        value_of(object));
}

static PLI_INT32 word_changed(p_cb_data data)
{
    vpi_printf("#c cb %s=%d t=%u\n", vpi_get_str(vpiFullName, data->obj),
        (int)data->value->value.integer, (unsigned)data->time->low);
    return 0;
}

static void watch(const char* array, int index)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    s_vpi_value value = { vpiIntVal, { 0 } };
    memset(&callback, 0, sizeof callback);
    callback.reason = cbValueChange;
    callback.cb_rtn = word_changed;
    callback.obj = word(array, index);
    callback.time = &time;
    callback.value = &value;
    vpi_register_cb(&callback);
}

static void after(PLI_UINT32 when, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, when, 0.0 };
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = routine;
    callback.time = &time;
    vpi_register_cb(&callback);
}

static PLI_INT32 at6(p_cb_data data)
{
    (void)data;
    put("top.w", 2, 9, 0U);
    return 0;
}

static PLI_INT32 at5(p_cb_data data)
{
    (void)data;
    put("top.mem", 1, 7, 0U);
    put("top.mem", 2, 9, 0U);
    put("top.w", 1, 7, 0U);
    put("top.mem", 3, 6, 2U);
    return 0;
}

static PLI_INT32 at1(p_cb_data data)
{
    (void)data;
    watch("top.mem", 1);
    watch("top.mem", 3);
    watch("top.w", 2);
    after(4U, at5);
    after(5U, at6);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    after(1U, at1);
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

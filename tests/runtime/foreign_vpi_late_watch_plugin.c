// SPDX-License-Identifier: Apache-2.0
// VPI late value-change watch plug-in (IEEE 1800-2023 38.36.1): a
// cbValueChange registered after time 0 on an output a continuous
// assignment drives, and on one of its bits, reports every later change of
// the input the library writes; the output keeps following that input.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static vpiHandle find(const char* name)
{
    vpiHandle handle = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    if (handle == NULL) {
        vpi_printf("#c no %s\n", name);
    }
    return handle;
}

static void print_value(const char* label, vpiHandle handle)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    s_vpi_value value;
    value.format = vpiBinStrVal;
    vpi_get_value(handle, &value);
    vpi_get_time(NULL, &time);
    vpi_printf("#c %s %u %s %s\n", label, (unsigned)time.low,
        vpi_get_str(vpiFullName, handle), value.value.str);
}

static PLI_INT32 changed(p_cb_data data)
{
    print_value("change", data->obj);
    return 0;
}

static void put(PLI_INT32 value)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    s_vpi_value data;
    vpiHandle handle = find("top.a");
    if (handle == NULL) {
        return;
    }
    data.format = vpiIntVal;
    data.value.integer = value;
    (void)vpi_put_value(handle, &data, &time, vpiInertialDelay);
}

static void watch(vpiHandle handle)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    static s_vpi_value value = { vpiSuppressVal, { 0 } };
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbValueChange;
    data.cb_rtn = changed;
    data.obj = handle;
    data.time = &time;
    data.value = &value;
    (void)vpi_register_cb(&data);
}

static void after(PLI_UINT32 delay, PLI_INT32 (*routine)(p_cb_data))
{
    static s_vpi_time time;
    s_cb_data data;
    time.type = vpiSimTime;
    time.high = 0;
    time.low = delay;
    memset(&data, 0, sizeof data);
    data.reason = cbAfterDelay;
    data.cb_rtn = routine;
    data.time = &time;
    (void)vpi_register_cb(&data);
}

static PLI_INT32 read_back(p_cb_data data)
{
    vpiHandle y = find("top.y");
    (void)data;
    if (y != NULL) {
        print_value("read", y);
    }
    return 0;
}

static PLI_INT32 second_write(p_cb_data data)
{
    (void)data;
    put(2);
    after(2, read_back);
    return 0;
}

static PLI_INT32 start_watching(p_cb_data data)
{
    vpiHandle y = find("top.y");
    (void)data;
    if (y == NULL) {
        return 0;
    }
    watch(y);
    watch(vpi_handle_by_index(y, 0));
    put(3);
    after(2, second_write);
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    (void)data;
    after(5, start_watching);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start_of_simulation;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

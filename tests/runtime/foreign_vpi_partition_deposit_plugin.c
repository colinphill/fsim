// SPDX-License-Identifier: Apache-2.0
// VPI deposit into a partitioned combinational output (IEEE 1800-2023
// 38.34): the library sets a and b at time 1 and writes X into top.y,
// which always @(a) drives, at time 5; the value lasts until a changes at
// time 20, also after b changes at time 10 and wakes the other members
// fused with it.
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

static const char* binary(const char* name)
{
    static char text[3][8];
    static int next;
    s_vpi_value value;
    char* result = text[next++ % 3];
    vpiHandle handle = find(name);
    if (handle == NULL) {
        return "?";
    }
    value.format = vpiBinStrVal;
    vpi_get_value(handle, &value);
    strncpy(result, value.value.str, 7);
    result[7] = '\0';
    return result;
}

static PLI_INT32 read_back(p_cb_data data)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    const char* y = binary("top.y");
    const char* z = binary("top.z");
    const char* w = binary("top.w");
    (void)data;
    vpi_get_time(NULL, &time);
    vpi_printf("#c read %u %s %s %s\n", (unsigned)time.low, y, z, w);
    return 0;
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

static void put(const char* name, PLI_INT32 scalar)
{
    s_vpi_value value;
    vpiHandle handle = find(name);
    if (handle == NULL) {
        return;
    }
    value.format = vpiScalarVal;
    value.value.scalar = scalar;
    (void)vpi_put_value(handle, &value, NULL, vpiNoDelay);
}

static PLI_INT32 lower_a(p_cb_data data)
{
    (void)data;
    put("top.a", vpi0);
    return 0;
}

static PLI_INT32 lower_b(p_cb_data data)
{
    (void)data;
    put("top.b", vpi0);
    return 0;
}

static PLI_INT32 deposit(p_cb_data data)
{
    (void)data;
    put("top.y", vpiX);
    after(1, read_back);
    after(5, lower_b);
    after(7, read_back);
    after(15, lower_a);
    after(17, read_back);
    return 0;
}

static PLI_INT32 raise(p_cb_data data)
{
    (void)data;
    put("top.a", vpi1);
    put("top.b", vpi1);
    after(4, deposit);
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    (void)data;
    after(1, raise);
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

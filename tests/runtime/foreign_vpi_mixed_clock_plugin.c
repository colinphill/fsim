// SPDX-License-Identifier: Apache-2.0
// VPI clock driver for foreign_vpi_mixed_clock.sv: the clock, data and
// control ports of the top module are written with zero-delay inertial puts
// (IEEE 1800-2023 38.34), as cocotb writes them, the clock from cbAfterDelay
// callbacks. After each rising edge a cbReadOnlySynch callback prints both
// register stages; the VHDL stage lags the SystemVerilog one by one edge.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stddef.h>

static vpiHandle clock;
static vpiHandle data;
static vpiHandle first;
static vpiHandle second;
static int level;
static int edges;

static vpiHandle named(const char* name)
{
    return vpi_handle_by_name((PLI_BYTE8*)name, NULL);
}

static void put(vpiHandle object, int number)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    value.value.integer = number;
    vpi_put_value(object, &value, &time, vpiInertialDelay);
}

static int get(vpiHandle object)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpi_get_value(object, &value);
    return (int)value.value.integer;
}

static PLI_INT32 settled(p_cb_data data_in)
{
    (void)data_in;
    vpi_printf("#c edge %d q1=%d q2=%d\n", edges, get(first), get(second));
    return 0;
}

static PLI_INT32 toggle(p_cb_data data_in);

static void arm(void)
{
    s_vpi_time half = { vpiSimTime, 0, 5, 0.0 };
    s_cb_data callback = { 0 };
    callback.reason = cbAfterDelay;
    callback.time = &half;
    callback.cb_rtn = toggle;
    vpi_register_cb(&callback);
}

static PLI_INT32 toggle(p_cb_data data_in)
{
    (void)data_in;
    level ^= 1;
    put(clock, level);
    if (edges < 6) {
        arm();
    }
    return 0;
}

static PLI_INT32 edge(p_cb_data data_in)
{
    s_vpi_time now = { vpiSimTime, 0, 0, 0.0 };
    s_cb_data callback = { 0 };
    if (data_in->value->value.scalar != vpi1) {
        return 0;
    }
    ++edges;
    callback.reason = cbReadOnlySynch;
    callback.time = &now;
    callback.cb_rtn = settled;
    vpi_register_cb(&callback);
    put(data, edges * 10);
    return 0;
}

static PLI_INT32 start(p_cb_data data_in)
{
    static s_vpi_value scalar = { vpiScalarVal, { 0 } };
    static s_vpi_time stamp = { vpiSimTime, 0, 0, 0.0 };
    s_cb_data callback = { 0 };
    (void)data_in;
    clock = named("top.clk");
    data = named("top.din");
    first = named("top.q1");
    second = named("top.q2");
    put(named("top.reset_n"), 1);
    put(named("top.vin"), 1);
    put(clock, 0);
    put(data, 0);
    callback.reason = cbValueChange;
    callback.obj = clock;
    callback.value = &scalar;
    callback.time = &stamp;
    callback.cb_rtn = edge;
    vpi_register_cb(&callback);
    arm();
    return 0;
}

static void startup(void)
{
    s_cb_data callback = { 0 };
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, NULL };

// SPDX-License-Identifier: Apache-2.0
// VPI plug-in for vpi_put_delays on a gate declared without a delay (IEEE
// 1800-2023 38.32, 28.16): the first put gives the gate a delay slot at the
// next safe point. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdint.h>
#include <string.h>

static void put_delays(const char* name)
{
    vpiHandle gate = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    s_vpi_time times[2];
    s_vpi_delay delays;
    if (gate == NULL) {
        vpi_printf("#c no %s\n", name);
        return;
    }
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = 2;
    delays.time_type = vpiSimTime;
    delays.mtm_flag = 0;
    delays.append_flag = 0;
    delays.pulsere_flag = 0;
    times[0].type = vpiSimTime;
    times[0].low = 3;
    times[1].type = vpiSimTime;
    times[1].low = 5;
    vpi_put_delays(gate, &delays);
    memset(times, 0, sizeof times);
    vpi_get_delays(gate, &delays);
    vpi_printf("#c put %s rise/fall %u,%u\n", name, (unsigned)times[0].low,
        (unsigned)times[1].low);
}

// g is a buf and x1 a tranif1, both declared without a delay.
static PLI_INT32 put_at_ten(p_cb_data data)
{
    (void)data;
    put_delays("top.g");
    put_delays("top.x1");
    put_delays("top.gv");
    put_delays("top.gm");
    put_delays("top.gd");
    return 0;
}

// top.dead is read through VPI only (dead logic until the put).
static PLI_INT32 read_dead(p_cb_data data)
{
    vpiHandle net = vpi_handle_by_name((PLI_BYTE8*)"top.dead", NULL);
    s_vpi_value value;
    value.format = vpiBinStrVal;
    vpi_get_value(net, &value);
    vpi_printf("#c dead %u =%s\n", (unsigned)(uintptr_t)data->user_data, value.value.str);
    return 0;
}

static void read_dead_at(unsigned time)
{
    s_cb_data data;
    s_vpi_time when;
    memset(&data, 0, sizeof data);
    memset(&when, 0, sizeof when);
    when.type = vpiSimTime;
    when.low = time;
    data.reason = cbReadOnlySynch;
    data.time = &when;
    data.cb_rtn = read_dead;
    data.user_data = (PLI_BYTE8*)(uintptr_t)time;
    (void)vpi_register_cb(&data);
}

// cbAfterDelay and cbReadOnlySynch are not allowed in a startup routine
// (1800-2023 36.10.2 lists only vpi_register_systf and cbEndOfCompile,
// cbStartOfSimulation, cbEndOfSimulation, cbUnresolvedSystf, cbError and
// cbPLIError there), so they are registered here, at time 0; their delays
// are relative to the current time (38.36.1).
static PLI_INT32 start_of_simulation(p_cb_data cb)
{
    s_cb_data data;
    s_vpi_time when;
    memset(&data, 0, sizeof data);
    memset(&when, 0, sizeof when);
    when.type = vpiSimTime;
    when.low = 10;
    data.reason = cbAfterDelay;
    data.time = &when;
    data.cb_rtn = put_at_ten;
    (void)vpi_register_cb(&data);
    read_dead_at(21);
    read_dead_at(26);
    read_dead_at(47);
    (void)cb;
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

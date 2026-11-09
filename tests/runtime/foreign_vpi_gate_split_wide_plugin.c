// SPDX-License-Identifier: Apache-2.0
// VPI plug-in for vpi_put_delays on gates whose output slot is not a plain
// four-state scalar (IEEE 1800-2023 38.32, 28.16): a net with a weak driver
// (nine-valued) and a bit of a net alias family. It puts rise 3, fall 5 at
// 10 ns on each of top.gs, top.gl and top.gl2 that the design has. Lines
// starting with "#c " form the checked trace.
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

static PLI_INT32 put_at_ten(p_cb_data data)
{
    (void)data;
    put_delays("top.gs");
    put_delays("top.gl");
    put_delays("top.gl2");
    return 0;
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

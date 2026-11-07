// SPDX-License-Identifier: Apache-2.0
// VPI plug-in that puts, 1 ps into the run and before any change of the
// design, the delays that foreign_vpi_mipd_split_early.sdf annotates at
// elaboration: rise 5 / fall 3 on port a of top.u1 and rise 4 / fall 2 on bit 1
// of port a of top.u3 (IEEE 1800-2023 38.32, vpiMIPDelay). Without the SDF the
// first put splits each port's net (the lazy split); with it the put repeats
// the annotated delays, so both runs print the same trace, which
// foreign_vpi_mipd_split_early.expected derives. Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static vpiHandle input_port(void)
{
    vpiHandle module = vpi_handle_by_name((PLI_BYTE8*)"top.u1", NULL);
    vpiHandle iterator = module != NULL ? vpi_iterate(vpiPort, module) : NULL;
    vpiHandle port;
    while (iterator != NULL && (port = vpi_scan(iterator)) != NULL) {
        const char* name = vpi_get_str(vpiName, port);
        if (name != NULL && strcmp(name, "a") == 0) {
            vpi_free_object(iterator);
            return port;
        }
    }
    return NULL;
}

// Puts the rise and fall delays (in ns, the module's time unit) on the port
// and reads them back.
static void put(const char* label, double rise, double fall)
{
    s_vpi_time da[2];
    s_vpi_delay request = { da, 2, vpiScaledRealTime, 0, 0, 0 };
    vpiHandle port = input_port();
    if (port == NULL) {
        vpi_printf("#c put %s missing\n", label);
        return;
    }
    memset(da, 0, sizeof da);
    da[0].type = vpiScaledRealTime;
    da[0].real = rise;
    da[1].type = vpiScaledRealTime;
    da[1].real = fall;
    vpi_put_delays(port, &request);
    memset(da, 0, sizeof da);
    vpi_get_delays(port, &request);
    vpi_printf("#c put %s %.3f %.3f\n", label, da[0].real, da[1].real);
}

// The same on the left bit of the port a of u3.
static void put_bit(const char* label, double rise, double fall)
{
    s_vpi_time da[2];
    s_vpi_delay request = { da, 2, vpiScaledRealTime, 0, 0, 0 };
    vpiHandle module = vpi_handle_by_name((PLI_BYTE8*)"top.u3", NULL);
    vpiHandle ports = module != NULL ? vpi_iterate(vpiPort, module) : NULL;
    vpiHandle port = ports != NULL ? vpi_scan(ports) : NULL;
    vpiHandle bits = port != NULL ? vpi_iterate(vpiBit, port) : NULL;
    vpiHandle bit = bits != NULL ? vpi_scan(bits) : NULL;
    if (bit == NULL) {
        vpi_printf("#c put %s missing\n", label);
        return;
    }
    memset(da, 0, sizeof da);
    da[0].type = vpiScaledRealTime;
    da[0].real = rise;
    da[1].type = vpiScaledRealTime;
    da[1].real = fall;
    vpi_put_delays(bit, &request);
    memset(da, 0, sizeof da);
    vpi_get_delays(bit, &request);
    vpi_printf("#c put %s %.3f %.3f\n", label, da[0].real, da[1].real);
}

static PLI_INT32 first_put(p_cb_data data)
{
    (void)data;
    put("first", 5.0, 3.0);
    put_bit("bit", 4.0, 2.0);
    return 0;
}

static void at(PLI_UINT32 ticks, PLI_INT32 (*routine)(p_cb_data))
{
    static s_vpi_time times[1];
    static int used;
    s_cb_data callback;
    if (used == 1) {
        return;
    }
    times[used].type = vpiSimTime;
    times[used].high = 0;
    times[used].low = ticks;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = routine;
    callback.time = &times[used++];
    vpi_register_cb(&callback);
}

// The simulation time is in picoseconds (timescale 1ns/1ps).
static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    at(1, first_put);
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

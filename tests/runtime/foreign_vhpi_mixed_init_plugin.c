// SPDX-License-Identifier: Apache-2.0
// The values the initialization callbacks and the first simulation cycles
// see in a design that mixes VHDL and SystemVerilog (IEEE 1076-2019 14.7.5.2,
// 14.7.5.3, 21.3.7.5, 21.3.7.6). Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* names[] = { "top.s", "top.x", "top.z", "top.q" };

static unsigned long long now_fs(void)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    return ((unsigned long long)now.high << 32) | now.low;
}

static void show(const char* what)
{
    unsigned index;
    printf("#c %s at %llu fs:", what, now_fs());
    for (index = 0; index < sizeof names / sizeof names[0]; ++index) {
        char buffer[64];
        vhpiValueT value;
        vhpiHandleT object = vhpi_handle_by_name(names[index], NULL);
        memset(&value, 0, sizeof value);
        value.format = vhpiBinStrVal;
        value.bufSize = sizeof buffer;
        value.value.str = (vhpiCharT*)buffer;
        printf(" %s=%s", names[index] + 4,
            object != NULL && vhpi_get_value(object, &value) == 0 ? buffer : "?");
    }
    printf("\n");
    fflush(stdout);
}

static void on_end_of_initialization(const vhpiCbDataT* data)
{
    (void)data;
    show("end of initialization");
}

static void on_start_of_simulation(const vhpiCbDataT* data)
{
    (void)data;
    show("start of simulation");
}


static void later(const vhpiCbDataT* data)
{
    (void)data;
    show("1 ns");
}

static void register_reason(int reason, void (*routine)(const vhpiCbDataT*))
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    vhpi_register_cb(&data, 0);
}

static void startup(void)
{
    vhpiTimeT delay;
    vhpiCbDataT data;
    register_reason(vhpiCbEndOfInitialization, on_end_of_initialization);
    register_reason(vhpiCbStartOfSimulation, on_start_of_simulation);
    memset(&data, 0, sizeof data);
    delay.high = 0;
    delay.low = 1000000U;
    data.reason = vhpiCbAfterDelay;
    data.cb_rtn = later;
    data.time = &delay;
    vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

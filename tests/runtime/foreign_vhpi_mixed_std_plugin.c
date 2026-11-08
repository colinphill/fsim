// SPDX-License-Identifier: Apache-2.0
// VHPI reads of INTEGER signals in designs that mix VHDL units of different
// standards (IEEE 1076-2019 5.2.3.1, 22.4). With a VHDL-2019 unit in the
// design INTEGER is 64 bits for every unit, so a VHDL-2008 unit's INTEGER
// signal reads in vhpiLongIntVal; without one it is 32 bits and reads in
// vhpiIntVal. Each object a design does not have is skipped. Lines starting
// with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void native(const char* name)
{
    vhpiHandleT object = vhpi_handle_by_name(name, NULL);
    vhpiValueT value;
    int status;
    if (object == NULL) {
        return;
    }
    memset(&value, 0, sizeof value);
    value.format = vhpiObjTypeVal;
    status = vhpi_get_value(object, &value);
    if (value.format == vhpiLongIntVal) {
        printf("#c %s LongIntVal status=%d value=%lld\n", name, status,
            (long long)value.value.longintg);
    } else if (value.format == vhpiIntVal) {
        printf("#c %s IntVal status=%d value=%d\n", name, status,
            (int)value.value.intg);
    } else {
        printf("#c %s format=%d status=%d\n", name, (int)value.format, status);
    }
}

static void after(const vhpiCbDataT* data)
{
    (void)data;
    native("top.u.hi");
    native("top.src");
    native("top.dst");
    native("top.n");
    native("top.m");
    native("top.w");
    native("top.wl");
    fflush(stdout);
}

static void start(const vhpiCbDataT* data)
{
    vhpiCbDataT next;
    vhpiTimeT time;
    (void)data;
    memset(&next, 0, sizeof next);
    time.high = 0;
    time.low = 2000000U;
    next.reason = vhpiCbAfterDelay;
    next.cb_rtn = after;
    next.time = &time;
    (void)vhpi_register_cb(&next, 0);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

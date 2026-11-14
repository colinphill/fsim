// SPDX-License-Identifier: Apache-2.0
// VHPI transaction and value-change callbacks on drivers (IEEE 1076-2019
// 21.3.2.2, 21.3.2.5): each driver of a scalar subelement (14.7.2) reports
// the transactions of its own bits and the changes of its own value. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* value_of(vhpiHandleT object)
{
    static char buffer[64];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    if (vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

static void driver_event(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    vhpiHandleT signal = vhpi_handle(DEPRECATED_vhpiBasicSignal, data->obj);
    vhpiHandleT process = vhpi_handle(vhpiEqProcessStmt, data->obj);
    printf("#c %s %s by %s at %u fs: %s\n",
        data->reason == vhpiCbTransaction ? "transaction" : "change",
        signal ? (const char*)vhpi_get_str(vhpiFullNameP, signal) : "-",
        process ? (const char*)vhpi_get_str(vhpiNameP, process) : "-",
        (unsigned)now.low, value_of(data->obj));
    fflush(stdout);
}

static void watch_drivers(const char* name)
{
    vhpiHandleT signal = vhpi_handle_by_name(name, NULL);
    vhpiHandleT drivers = signal ? vhpi_iterator(vhpiDrivers, signal) : NULL;
    vhpiHandleT driver;
    int count = 0;
    while (drivers && (driver = vhpi_scan(drivers)) != NULL) {
        vhpiCbDataT request;
        memset(&request, 0, sizeof request);
        request.cb_rtn = driver_event;
        request.obj = driver;
        request.reason = vhpiCbTransaction;
        vhpi_register_cb(&request, 0);
        request.reason = vhpiCbValueChange;
        vhpi_register_cb(&request, 0);
        ++count;
    }
    printf("#c %s drivers %d\n", name, count);
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    watch_drivers("top.r");
    watch_drivers("top.v");
    watch_drivers("top.t");
    watch_drivers("top.i");
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    // Before the initialization phase, so the transactions of the first
    // simulation cycle are seen (IEEE 1076-2019 14.7.5.2, 21.3.7.5).
    data.reason = vhpiCbStartOfInitialization;
    data.cb_rtn = start;
    vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

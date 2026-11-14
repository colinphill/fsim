// SPDX-License-Identifier: Apache-2.0
// Transaction and value-change callbacks registered at the start of the
// simulation see the transactions of the first simulation cycle (IEEE
// 1076-2019 14.7.5.3, 21.3.2.5, 21.3.7.6). Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void event(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    char text[32];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof text;
    value.value.str = (vhpiCharT*)text;
    vhpi_get_time(&now, NULL);
    printf("#c %s %s at %u fs: %s\n",
        data->reason == vhpiCbTransaction ? "transaction" : "change",
        (const char*)vhpi_get_str(vhpiNameP, data->obj), (unsigned)now.low,
        vhpi_get_value(data->obj, &value) == 0 ? text : "?");
    fflush(stdout);
}

static void watch(const char* name, int reason)
{
    vhpiCbDataT request;
    memset(&request, 0, sizeof request);
    request.reason = reason;
    request.cb_rtn = event;
    request.obj = vhpi_handle_by_name(name, NULL);
    vhpi_register_cb(&request, 0);
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c start of simulation\n");
    watch("top.s", vhpiCbTransaction);
    watch("top.s", vhpiCbValueChange);
    watch("top.t", vhpiCbTransaction);
    watch("top.t", vhpiCbValueChange);
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for vhpiCbStmt on a concurrent procedure call whose procedure
// body is declared apart in a package body (IEEE 1076-2019 4.8, 21.3.4.2).
// Run with simulate --foreign-probes. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void report(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    const vhpiCharT* kind = vhpi_get_str(vhpiKindStrP, data->obj);
    vhpi_get_time(&now, NULL);
    printf("#c stmt cc %s line %d t=%u\n", kind != NULL ? (const char*)kind : "-",
        (int)vhpi_get(vhpiLineNoP, data->obj), (unsigned)now.low);
    fflush(stdout);
}

static void start(const vhpiCbDataT* data)
{
    vhpiCbDataT request;
    vhpiHandleT call = vhpi_handle_by_name("top.cc", NULL);
    (void)data;
    memset(&request, 0, sizeof request);
    request.reason = vhpiCbStmt;
    request.cb_rtn = report;
    request.obj = call;
    // Without vhpiReturnCb the routine returns no handle even on success
    // (IEEE 1076-2019 23.29), so the check asks for the handle.
    if (call == NULL || vhpi_register_cb(&request, vhpiReturnCb) == NULL) {
        printf("#c refused cc\n");
        fflush(stdout);
    }
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    // The concurrent call runs once, in the initialization phase (14.7.5.2), so
    // the statement callback is placed at its start.
    data.reason = vhpiCbStartOfInitialization;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

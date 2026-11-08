// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for disconnection specifications (class disconnectSpec, IEEE
// 1076-2019 7.4, Figure 32). Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[8][128];
    static int next;
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 8;
    if (value == NULL) {
        return "-";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffers[0];
         ++index) {
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT specs = vhpi_iterator(vhpiSpecs, root);
    vhpiHandleT spec;
    (void)data;
    while (specs != NULL && (spec = vhpi_scan(specs)) != NULL) {
        vhpiHandleT signals;
        vhpiHandleT signal;
        const char* separator = "";
        if (vhpi_get(vhpiKindP, spec) != vhpiDisconnectSpecK) {
            continue;
        }
        printf("#c spec kind=%s type=%s time=%s signals=",
            text_property(spec, vhpiKindStrP),
            text_property(vhpi_handle(vhpiType, spec), vhpiNameP),
            text_property(vhpi_handle(vhpiTimeExpr, spec), vhpiNameP));
        signals = vhpi_iterator(vhpiSignals, spec);
        while (signals != NULL && (signal = vhpi_scan(signals)) != NULL) {
            printf("%s%s", separator, text_property(signal, vhpiNameP));
            separator = ",";
        }
        printf(" all=%d others=%d\n", vhpi_handle(vhpiAll, spec) != NULL,
            vhpi_handle(vhpiOthers, spec) != NULL);
    }
    fflush(stdout);
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

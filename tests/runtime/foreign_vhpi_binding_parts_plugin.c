// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the port map of a binding indication whose formal and
// actual parts select parts of ports (IEEE 1076-2019 7.3.2.3, 6.5.7.1,
// 19.5). Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of eight buffers, or "-".
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
    vhpiHandleT instance = vhpi_handle_by_name("top.u", NULL);
    vhpiHandleT specification = vhpi_handle(vhpiConfigSpec, instance);
    vhpiHandleT associations = vhpi_iterator(vhpiPortAssocs, specification);
    vhpiHandleT association;
    (void)data;
    while (associations != NULL
        && (association = vhpi_scan(associations)) != NULL) {
        vhpiHandleT formal = vhpi_handle(vhpiFormal, association);
        vhpiHandleT actual = vhpi_handle(vhpiActual, association);
        printf("#c formal %s %s size=%d actual %s %s\n",
            text_property(formal, vhpiKindStrP), text_property(formal, vhpiNameP),
            formal != NULL ? (int)vhpi_get(vhpiSizeP, formal) : -1,
            text_property(actual, vhpiKindStrP), text_property(actual, vhpiNameP));
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

// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the protected type classes (IEEE 1076-2019 5.6, 19.4.3,
// Figures 5, 15 and 21): the type of a protected shared variable, the
// declaration/body relations, the methods and the protectedTypeInst region.
// Lines starting with "#c " form the checked trace.
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
    vhpiHandleT variable = vhpi_handle_by_name("top.ctr", NULL);
    vhpiHandleT type = vhpi_handle(vhpiType, variable);
    vhpiHandleT body = vhpi_handle(vhpiProtectedTypeBody, type);
    vhpiHandleT back = vhpi_handle(vhpiProtectedTypeDecl, body);
    vhpiHandleT methods = vhpi_iterator(vhpiDecls, type);
    vhpiHandleT method;
    vhpiHandleT instance = vhpi_handle(vhpiProtectedTypeInst, variable);
    vhpiHandleT other = vhpi_handle_by_name("top.s", NULL);
    (void)data;
    printf("#c type kind=%s name=%s\n", text_property(type, vhpiKindStrP),
        text_property(type, vhpiNameP));
    printf("#c body kind=%s name=%s\n", text_property(body, vhpiKindStrP),
        text_property(body, vhpiNameP));
    printf("#c body back=%s\n",
        back != NULL && vhpi_compare_handles(back, type) ? "same" : "other");
    while (methods != NULL && (method = vhpi_scan(methods)) != NULL) {
        printf("#c method %s kind=%s\n", text_property(method, vhpiNameP),
            text_property(method, vhpiKindStrP));
    }
    printf("#c inst kind=%s name=%s full=%s\n",
        text_property(instance, vhpiKindStrP),
        text_property(instance, vhpiNameP),
        text_property(instance, vhpiFullNameP));
    printf("#c signal inst=%s\n",
        vhpi_handle(vhpiProtectedTypeInst, other) == NULL ? "none" : "some");
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

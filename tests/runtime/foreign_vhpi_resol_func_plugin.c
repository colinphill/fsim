// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for vhpiResolFunc (IEEE 1076-2019 19.4.3, Figure 14), of a
// signal of STD_LOGIC (the function RESOLVED, 16.7), of a signal whose subtype
// indication names a user function and of an unresolved signal. Lines
// starting with "#c " form the checked trace.
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

static void signal_function(const char* name, const char* path)
{
    vhpiHandleT signal = vhpi_handle_by_name(path, NULL);
    vhpiHandleT function = vhpi_handle(vhpiResolFunc, signal);
    vhpiHandleT parameters;
    vhpiHandleT parameter;
    if (function == NULL) {
        printf("#c %s resolfunc=-\n", name);
        return;
    }
    printf("#c %s resolfunc=%s kind=%s params=%d pure=%d return=%s", name,
        text_property(function, vhpiNameP),
        text_property(function, vhpiKindStrP),
        (int)vhpi_get(vhpiNumParamsP, function),
        (int)vhpi_get(vhpiIsPureP, function),
        text_property(vhpi_handle(vhpiReturnType, function), vhpiNameP));
    parameters = vhpi_iterator(vhpiParamDecls, function);
    while (parameters != NULL && (parameter = vhpi_scan(parameters)) != NULL) {
        printf(" param=%s:%s", text_property(parameter, vhpiNameP),
            text_property(vhpi_handle(vhpiType, parameter), vhpiNameP));
    }
    printf("\n");
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    signal_function("a", "top.a");
    signal_function("b", "top.b");
    signal_function("c", "top.c");
    signal_function("rs", "top.rs");
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

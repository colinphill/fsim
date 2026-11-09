// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the drivers of a process that calls procedures which
// assign signals (IEEE 1076-2019 14.7.2, 19.12.1). Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of four buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[4][128];
    static int next;
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 4;
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

// The signals a process drives, comma separated.
static void print_process(const char* path, const char* label)
{
    vhpiHandleT process = vhpi_handle_by_name(path, NULL);
    vhpiHandleT drivers = vhpi_iterator(vhpiDrivers, process);
    vhpiHandleT driver;
    const char* separator = "";
    printf("#c %s drivers=", label);
    while (drivers != NULL && (driver = vhpi_scan(drivers)) != NULL) {
        printf("%s%s", separator,
            text_property(vhpi_handle(vhpiSignal, driver), vhpiNameP));
        separator = ",";
    }
    printf("\n");
}

// The number of drivers of a signal.
static void print_signal(const char* path, const char* label)
{
    vhpiHandleT drivers
        = vhpi_iterator(vhpiDrivers, vhpi_handle_by_name(path, NULL));
    int total = 0;
    while (drivers != NULL && vhpi_scan(drivers) != NULL) {
        ++total;
    }
    printf("#c signal %s drivers=%d\n", label, total);
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    print_process("top.p1", "p1");
    print_process("top.p2", "p2");
    print_signal("top.a", "a");
    print_signal("top.c", "c");
    print_signal("top.s", "s");
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

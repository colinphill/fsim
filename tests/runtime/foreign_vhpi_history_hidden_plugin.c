// SPDX-License-Identifier: Apache-2.0
// The signals VHPI lists for a design that reads 'LAST_VALUE, and the names
// it resolves (IEEE 1076-2019 16.2.4, 23.2). Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[4][128];
    static int next;
    const vhpiCharT* value = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 4;
    if (value == NULL) {
        return "-";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffers[0];
         ++index) {
        // A backslash (an extended identifier) would be read as an escape by
        // the checker's list handling.
        buffer[index] = value[index] == '\\' ? '|' : (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

static void start(const vhpiCbDataT* data)
{
    static const char* const names[] = { "top.s", "top.v", ":top:s",
        "top.s'last_value", ":top:s'last_value", "top.v'last_value",
        "top.\\s'last_value\\", ":top:\\s'last_value\\" };
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT iterator;
    vhpiHandleT item;
    size_t index;
    int count = 0;
    (void)data;
    iterator = vhpi_iterator(vhpiSigDecls, root);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c sig %s\n", text_property(item, vhpiFullNameP));
        ++count;
    }
    printf("#c signals %d\n", count);
    iterator = vhpi_iterator(vhpiDecls, root);
    count = 0;
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        ++count;
    }
    printf("#c decls %d\n", count);
    for (index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        item = vhpi_handle_by_name(names[index], NULL);
        printf("#c by name %s: %s\n", names[index],
            item == NULL ? "null" : text_property(item, vhpiFullNameP));
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

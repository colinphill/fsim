// SPDX-License-Identifier: Apache-2.0
// VHPI local loads and contributors of implicit signals (IEEE 1076-2019
// 16.2.4, 19.12.2.2, 19.12.2.3). Lines starting with "#c " form the checked
// trace.
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
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

// The objects of a relation, by full name (or name) and class.
static void list(const char* label, vhpiOneToManyT relation, vhpiHandleT object)
{
    vhpiHandleT iterator = vhpi_iterator(relation, object);
    vhpiHandleT item;
    printf("#c   %s:", label);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        const char* name = text_property(item, vhpiFullNameP);
        printf(" %s[%s]",
            strcmp(name, "-") == 0 ? text_property(item, vhpiNameP) : name,
            text_property(item, vhpiKindStrP));
    }
    printf("\n");
}

static void describe(vhpiHandleT signal)
{
    printf("#c %s %s\n", text_property(signal, vhpiNameP),
        text_property(signal, vhpiKindStrP));
    list("drivers", vhpiDrivers, signal);
    list("contributors", vhpiLocalContributors, signal);
    list("loads", vhpiLocalLoads, signal);
}

static void start(const vhpiCbDataT* data)
{
    static const char* const names[] = { "top.s", "top.r", "top.b.guard",
        "top.u.i", "top.u.j" };
    size_t index;
    (void)data;
    for (index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        vhpiHandleT signal = vhpi_handle_by_name(names[index], NULL);
        vhpiHandleT iterator;
        vhpiHandleT attribute;
        if (signal == NULL) {
            printf("#c %s: null\n", names[index]);
            continue;
        }
        describe(signal);
        // The implicit signals of the signal (vhpiSigAttrs).
        iterator = vhpi_iterator(vhpiSigAttrs, signal);
        while (iterator != NULL && (attribute = vhpi_scan(iterator)) != NULL) {
            describe(attribute);
        }
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

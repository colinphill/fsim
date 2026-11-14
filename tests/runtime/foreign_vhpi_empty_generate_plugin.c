// SPDX-License-Identifier: Apache-2.0
// VHPI generate statement occurrences that hold no signal, instance or
// process (IEEE 1076-2019 14.5.3, 19.4.2). Lines starting with "#c " form
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
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

// A region, its declarations, and the regions inside it.
static void describe(vhpiHandleT region, int depth)
{
    vhpiHandleT iterator;
    vhpiHandleT item;
    printf("#c %*s%s %s", depth * 2, "", text_property(region, vhpiFullNameP),
        text_property(region, vhpiKindStrP));
    if (vhpi_get(vhpiKindP, region) == vhpiForGenerateK) {
        printf(" index=%d", (int)vhpi_get(vhpiGenerateIndexP, region));
    }
    if (depth > 0) {
        printf(" label=%s", text_property(region, vhpiLabelNameP));
    }
    printf("\n");
    iterator = vhpi_iterator(vhpiDecls, region);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c %*s  decl %s\n", depth * 2, "",
            text_property(item, vhpiFullNameP));
    }
    iterator = vhpi_iterator(vhpiInternalRegions, region);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        describe(item, depth + 1);
    }
}

static void start(const vhpiCbDataT* data)
{
    static const char* const names[] = { "top.g(1)", "top.g(0)", "top.h(0)",
        "top.h(1)", "top.h(2)", "top.h(0).e(3)", "top.k", "top.k.d", "top.m",
        "top.n(1)", "top.n(2).q", "top.n(1).q", "top.n(2).q.p", "top.k.k",
        "top.x", "top.x.a", "top.x.b", "top.x.x", "top.y", "top.y.e",
        "top.y.y", "top.z", "top.z.f", "top.h", "top.h.c", "top.h.i",
        "top.n", "top.n.q", "top.h(1).c", ":top:h", "top.g", "top.g(0).x", ":top:g" };
    vhpiHandleT iterator;
    vhpiHandleT item;
    size_t index;
    (void)data;
    describe(vhpi_handle(vhpiRootInst, NULL), 0);
    // vhpiStmts lists the generate occurrences after the processes.
    iterator = vhpi_iterator(vhpiStmts, vhpi_handle(vhpiRootInst, NULL));
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c stmt %s\n", text_property(item, vhpiFullNameP));
    }
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

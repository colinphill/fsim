// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in printing the model of two instances of one component, one
// default-bound and one bound by a binding indication with a port map (IEEE
// 1076-2019 7.3.2.1, 7.3.3, 14.5.4): the ports of each instance with their
// modes, sizes and values, the instance's port association elements and
// the bound design unit. The two prints differ only in the names. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define NS 1000000ULL

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

static const char* mode_text(vhpiHandleT port)
{
    switch (port != NULL ? vhpi_get(vhpiModeP, port) : 0) {
    case vhpiInMode: return "in";
    case vhpiOutMode: return "out";
    case vhpiInoutMode: return "inout";
    case vhpiBufferMode: return "buffer";
    case vhpiLinkageMode: return "linkage";
    default: return "-";
    }
}

static const char* value_text(vhpiHandleT object)
{
    static char buffer[80];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

static void print_instance(const char* label)
{
    char path[32];
    vhpiHandleT instance;
    vhpiHandleT iterator;
    vhpiHandleT item;
    vhpiHandleT unit;
    snprintf(path, sizeof path, "top.%s", label);
    instance = vhpi_handle_by_name(path, NULL);
    iterator = vhpi_iterator(vhpiPortDecls, instance);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c %s port %s %s mode=%s size=%d value=%s\n", label,
            text_property(item, vhpiKindStrP), text_property(item, vhpiNameP),
            mode_text(item), (int)vhpi_get(vhpiSizeP, item), value_text(item));
    }
    iterator = vhpi_iterator(vhpiPortAssocs, instance);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        vhpiHandleT formal = vhpi_handle(vhpiFormal, item);
        vhpiHandleT actual = vhpi_handle(vhpiActual, item);
        printf("#c %s assoc formal %s %s actual %s %s\n", label,
            text_property(formal, vhpiKindStrP), text_property(formal, vhpiNameP),
            text_property(actual, vhpiKindStrP), text_property(actual, vhpiNameP));
    }
    unit = vhpi_handle(vhpiDesignUnit, instance);
    printf("#c %s unit %s arch %s\n", label,
        text_property(vhpi_handle(vhpiPrimaryUnit, unit), vhpiNameP),
        text_property(unit, vhpiNameP));
}

static void show(const vhpiCbDataT* data)
{
    (void)data;
    print_instance("u1");
    print_instance("u2");
    fflush(stdout);
}

static void start(const vhpiCbDataT* data)
{
    vhpiCbDataT after;
    vhpiTimeT time;
    (void)data;
    memset(&after, 0, sizeof after);
    time.high = 0;
    time.low = (uint32_t)(1U * NS);
    after.reason = vhpiCbAfterDelay;
    after.cb_rtn = show;
    after.time = &time;
    (void)vhpi_register_cb(&after, vhpiReturnCb);
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

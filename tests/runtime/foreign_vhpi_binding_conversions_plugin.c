// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the port map of a binding indication whose actual part
// and formal part convert the associated port (IEEE 1076-2019 7.3.2.1,
// 6.5.7.1, 19.12.2.2): the association elements of the configuration
// specification (a conversion stands for the part it converts, as vhpiFormal
// or vhpiActual), then the values both conversions produce (14.5.4,
// 14.7.3.3: the entity reads inv(sc) and sd is inv of the entity's output,
// so sd follows sc). Lines starting with "#c " form the checked trace.
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

// The binary string value of the object `name` names, or "?".
static const char* text(const char* name)
{
    static char buffers[4][80];
    static int next;
    char* buffer = buffers[next];
    vhpiValueT value;
    vhpiHandleT object = vhpi_handle_by_name(name, NULL);
    next = (next + 1) % 4;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffers[0];
    value.value.str = (vhpiCharT*)buffer;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

static unsigned long long now(void)
{
    vhpiTimeT time;
    vhpi_get_time(&time, NULL);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static void show(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu ns sc=%s sd=%s u.r=%s u.s=%s\n", now() / NS, text("top.sc"),
        text("top.sd"), text("top.u.r"), text("top.u.s"));
    fflush(stdout);
}

static void register_after(unsigned long long delay)
{
    vhpiCbDataT data;
    vhpiTimeT time;
    memset(&data, 0, sizeof data);
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    data.reason = vhpiCbAfterDelay;
    data.cb_rtn = show;
    data.time = &time;
    (void)vhpi_register_cb(&data, vhpiReturnCb);
}

// The size of a port part; a conversion object has none.
static const char* size_text(vhpiHandleT part)
{
    static char buffer[16];
    if (part == NULL || vhpi_get(vhpiKindP, part) == vhpiConvFuncK
        || vhpi_get(vhpiKindP, part) == vhpiTypeConvK) {
        return "-";
    }
    snprintf(buffer, sizeof buffer, "%d", (int)vhpi_get(vhpiSizeP, part));
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
        printf("#c formal %s %s size=%s actual %s %s\n",
            text_property(formal, vhpiKindStrP), text_property(formal, vhpiNameP),
            size_text(formal),
            text_property(actual, vhpiKindStrP), text_property(actual, vhpiNameP));
    }
    register_after(5U * NS);
    register_after(15U * NS);
    register_after(25U * NS);
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

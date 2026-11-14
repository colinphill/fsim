// SPDX-License-Identifier: Apache-2.0
// Event-array conformance plug-in (IEEE 1800-2023 7.4, 37.43): at times 1 to 3 the
// array `top.big` with bounds [5000:5001] is described, its element 5001
// triggered by index, and the type of the class event property `top.h.pe`
// read. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* type_name(vpiHandle object)
{
    switch (vpi_get(vpiType, object)) {
    case vpiNamedEventArray:
        return "vpiNamedEventArray";
    case vpiNamedEvent:
        return "vpiNamedEvent";
    default:
        return "other";
    }
}

// The array `name`: its type, size, the names of its elements in iteration
// order, whether the parent of an element is the array, and the element at
// `index` triggered.
static void probe_array(const char* name, int index)
{
    vpiHandle array = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    vpiHandle events = array != NULL ? vpi_iterate(vpiNamedEvent, array) : NULL;
    vpiHandle event;
    vpiHandle element = array != NULL ? vpi_handle_by_index(array, index) : NULL;
    vpiHandle parent = element != NULL ? vpi_handle(vpiParent, element) : NULL;
    char order[256] = "";
    s_vpi_value value;
    while (events != NULL && (event = vpi_scan(events)) != NULL) {
        strcat(order, " ");
        strcat(order, vpi_get_str(vpiName, event));
    }
    vpi_printf("#c %s %s size %d order%s parent %s\n", name,
        array != NULL ? type_name(array) : "none",
        array != NULL ? (int)vpi_get(vpiSize, array) : -1, order,
        parent != NULL && vpi_compare_objects(parent, array) ? "array" : "other");
    memset(&value, 0, sizeof value);
    value.format = vpiScalarVal;
    value.value.scalar = vpi1;
    if (element != NULL) {
        vpi_put_value(element, &value, NULL, vpiNoDelay);
    }
}

struct Probe {
    const char* name;
    int index;
    unsigned delay;
};

static struct Probe probes[] = {
    { "top.big", 5001, 1U }, { "top.d", 2, 2U }, { "top.g.ge", 8, 3U }
};

static PLI_INT32 at_delay(p_cb_data data)
{
    const struct Probe* probe = (const struct Probe*)data->user_data;
    probe_array(probe->name, probe->index);
    if (probe->delay == 1U) {
        vpiHandle property = vpi_handle_by_name("top.h.pe", NULL);
        vpi_printf("#c class pe %s\n",
            property != NULL ? type_name(property) : "none");
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    unsigned at;
    (void)data;
    for (at = 0; at < sizeof probes / sizeof probes[0]; ++at) {
        s_cb_data callback;
        s_vpi_time delay;
        memset(&callback, 0, sizeof callback);
        memset(&delay, 0, sizeof delay);
        delay.type = vpiSimTime;
        delay.low = probes[at].delay;
        callback.reason = cbAfterDelay;
        callback.cb_rtn = at_delay;
        callback.time = &delay;
        callback.user_data = (PLI_BYTE8*)&probes[at];
        vpi_register_cb(&callback);
    }
    return 0;
}

static void startup(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

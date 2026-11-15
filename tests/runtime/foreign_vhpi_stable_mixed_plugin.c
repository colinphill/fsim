// SPDX-License-Identifier: Apache-2.0
// VHPI conformance plug-in for S'STABLE and S'QUIET of a port whose events
// and transactions come from SystemVerilog work (IEEE 1076-2019 14.7.4,
// 16.2.4, 19.4.3 vhpiSigAttrs): both implicit signals turn FALSE in the
// cycle of the change and TRUE again after it, whichever language writes the
// prefix. The changes are recorded as they happen and printed at the end of
// the run, the stable signal's first. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { max_changes = 16, port_count = 2, kind_count = 2 };

struct Change {
    unsigned time;
    int value;
};

// Port p (std_logic) and port v (std_logic_vector); each has a 'STABLE
// and a 'QUIET signal. Both ports start from SystemVerilog static
// initializers, which are initialization, not transactions (IEEE 1800-2023
// 6.8; 1076-2019 14.7.5.2): neither implicit signal changes at time zero.
static const char* const port_names[port_count] = { "top.u.p", "top.u.v" };
static const char* const port_labels[port_count] = { "p", "v" };
static const char* const kind_labels[kind_count] = { "stable", "quiet" };
static struct Change changes[port_count][kind_count][max_changes];
static int counts[port_count][kind_count];
static vhpiHandleT signals[port_count][kind_count];

static int position_of(vhpiHandleT object)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    return vhpi_get_value(object, &value) == 0 ? (int)value.value.intg : -1;
}

static void changed(const vhpiCbDataT* data)
{
    int port;
    int kind;
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    for (port = 0; port < port_count; ++port) {
        for (kind = 0; kind < kind_count; ++kind) {
            int* count = &counts[port][kind];
            if (data->obj != signals[port][kind] || *count >= max_changes) {
                continue;
            }
            changes[port][kind][*count].time = (unsigned)now.low;
            changes[port][kind][*count].value = position_of(data->obj);
            ++*count;
        }
    }
}

static void end_of_simulation(const vhpiCbDataT* data)
{
    int port;
    int kind;
    int index;
    (void)data;
    for (port = 0; port < port_count; ++port) {
        for (kind = 0; kind < kind_count; ++kind) {
            for (index = 0; index < counts[port][kind]; ++index) {
                printf("#c %s'%s change value=%d at %u\n", port_labels[port],
                    kind_labels[kind], changes[port][kind][index].value,
                    changes[port][kind][index].time);
            }
        }
    }
    fflush(stdout);
}

static void watch_port(int port)
{
    vhpiHandleT handle = vhpi_handle_by_name(port_names[port], NULL);
    vhpiHandleT iterator = vhpi_iterator(vhpiSigAttrs, handle);
    vhpiHandleT item;
    vhpiCbDataT callback;
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        // The letter case of names is not specified.
        const vhpiCharT* name = vhpi_get_str(vhpiNameP, item);
        char lower[64];
        size_t length;
        int kind;
        if (name == NULL) {
            continue;
        }
        for (length = 0U; name[length] != 0 && length + 1U < sizeof lower;
             ++length) {
            lower[length] = (char)tolower(name[length]);
        }
        lower[length] = 0;
        if (strstr(lower, "'stable") != NULL) {
            kind = 0;
        } else if (strstr(lower, "'quiet") != NULL) {
            kind = 1;
        } else {
            continue;
        }
        signals[port][kind] = item;
        memset(&callback, 0, sizeof callback);
        callback.reason = vhpiCbValueChange;
        callback.cb_rtn = changed;
        callback.obj = item;
        (void)vhpi_register_cb(&callback, 0);
    }
    printf("#c %s stable=%d quiet=%d\n", port_labels[port],
        signals[port][0] != NULL, signals[port][1] != NULL);
}

static void start(const vhpiCbDataT* data)
{
    int port;
    (void)data;
    for (port = 0; port < port_count; ++port) {
        watch_port(port);
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
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbEndOfSimulation;
    data.cb_rtn = end_of_simulation;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

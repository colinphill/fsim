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

enum { max_changes = 16 };

struct Change {
    unsigned time;
    int value;
};

static struct Change stable_changes[max_changes];
static struct Change quiet_changes[max_changes];
static int stable_count;
static int quiet_count;
static vhpiHandleT stable_signal;
static vhpiHandleT quiet_signal;

static int position_of(vhpiHandleT object)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    return vhpi_get_value(object, &value) == 0 ? (int)value.value.intg : -1;
}

static void changed(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    struct Change* list = data->obj == stable_signal ? stable_changes
                                                      : quiet_changes;
    int* count = data->obj == stable_signal ? &stable_count : &quiet_count;
    vhpi_get_time(&now, NULL);
    if (*count < max_changes) {
        list[*count].time = (unsigned)now.low;
        list[*count].value = position_of(data->obj);
        ++*count;
    }
}

static void report(const char* label, const struct Change* list, int count)
{
    int index;
    for (index = 0; index < count; ++index) {
        printf("#c %s change value=%d at %u\n", label, list[index].value,
            list[index].time);
    }
}

static void end_of_simulation(const vhpiCbDataT* data)
{
    (void)data;
    report("p'stable", stable_changes, stable_count);
    report("p'quiet", quiet_changes, quiet_count);
    fflush(stdout);
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT port = vhpi_handle_by_name("top.u.p", NULL);
    vhpiHandleT iterator = vhpi_iterator(vhpiSigAttrs, port);
    vhpiHandleT item;
    vhpiCbDataT callback;
    (void)data;
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        // The letter case of names is not specified.
        const vhpiCharT* name = vhpi_get_str(vhpiNameP, item);
        char lower[64];
        size_t length;
        if (name == NULL) {
            continue;
        }
        for (length = 0U; name[length] != 0 && length + 1U < sizeof lower;
             ++length) {
            lower[length] = (char)tolower(name[length]);
        }
        lower[length] = 0;
        if (strstr(lower, "'stable") != NULL) {
            stable_signal = item;
        } else if (strstr(lower, "'quiet") != NULL) {
            quiet_signal = item;
        } else {
            continue;
        }
        memset(&callback, 0, sizeof callback);
        callback.reason = vhpiCbValueChange;
        callback.cb_rtn = changed;
        callback.obj = item;
        (void)vhpi_register_cb(&callback, 0);
    }
    printf("#c stable=%d quiet=%d\n", stable_signal != NULL,
        quiet_signal != NULL);
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

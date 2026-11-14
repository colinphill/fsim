// SPDX-License-Identifier: Apache-2.0
// Value-change callbacks on the variables of a running process (IEEE
// 1076-2019 21.3.2.2, 22.5.2, 23.28): two callbacks watch a variable and are
// removed one after the other, a callback registers another watch and deposits
// to another variable while the process runs, and a callback registered after
// the last one was removed watches again. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static vhpiHandleT a_;
static vhpiHandleT b_;
static vhpiHandleT c_;
static vhpiHandleT r_;
static vhpiHandleT first_;
static vhpiHandleT second_;
static int watching_c_;

static unsigned long long now_fs(void)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    return ((unsigned long long)now.high << 32) | now.low;
}

static int value_of(vhpiHandleT object)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    return vhpi_get_value(object, &value) == 0 ? (int)value.value.intg : -999;
}

static const char* name_of(vhpiHandleT object)
{
    const vhpiCharT* name = vhpi_get_str(vhpiNameP, object);
    return name != NULL ? (const char*)name : "-";
}

static void watch(vhpiHandleT object, void (*routine)(const vhpiCbDataT*),
    void* tag, vhpiHandleT* handle)
{
    vhpiCbDataT request;
    vhpiHandleT result;
    memset(&request, 0, sizeof request);
    request.reason = vhpiCbValueChange;
    request.cb_rtn = routine;
    request.obj = object;
    request.user_data = tag;
    result = vhpi_register_cb(&request, vhpiReturnCb);
    if (result == NULL) {
        printf("#c register on %s failed\n", name_of(object));
    }
    if (handle != NULL) {
        *handle = result;
    }
}

static void changed(const vhpiCbDataT* data)
{
    printf("#c %s %s at %llu fs: %d\n", (const char*)data->user_data,
        name_of(data->obj), now_fs(), value_of(data->obj));
    fflush(stdout);
}

static void on_a(const vhpiCbDataT* data)
{
    vhpiValueT value;
    changed(data);
    if (!watching_c_) {
        // The process runs on: its next store to c is seen.
        watching_c_ = 1;
        watch(c_, changed, (void*)"watch", NULL);
    }
    // A deposit while the process runs: the statement after the assignment
    // to a adds to this value, and r reads it.
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = 100 * value_of(a_);
    if (vhpi_put_value(b_, &value, vhpiDeposit) != 0) {
        printf("#c deposit failed\n");
    }
}

static void show(const char* what)
{
    printf("#c %s at %llu fs: a=%d b=%d c=%d r=%d\n", what, now_fs(),
        value_of(a_), value_of(b_), value_of(c_), value_of(r_));
    fflush(stdout);
}

static void step(const vhpiCbDataT* data)
{
    const int at = (int)(intptr_t)data->user_data;
    if (at == 2) {
        // One of two callbacks on a variable ends: the other still runs.
        printf("#c remove first: rc=%d\n", vhpi_remove_cb(first_));
    } else if (at == 4) {
        printf("#c remove second: rc=%d\n", vhpi_remove_cb(second_));
    } else if (at == 6) {
        watch(a_, changed, (void*)"again", NULL);
    }
    show("step");
}

static void after(int ns, int tag)
{
    vhpiTimeT delay;
    vhpiCbDataT request;
    memset(&request, 0, sizeof request);
    delay.high = 0;
    delay.low = (uint32_t)ns * 1000000U + 500000U;
    request.reason = vhpiCbAfterDelay;
    request.cb_rtn = step;
    request.time = &delay;
    request.user_data = (void*)(intptr_t)tag;
    vhpi_register_cb(&request, 0);
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    a_ = vhpi_handle_by_name("top.p.a", NULL);
    b_ = vhpi_handle_by_name("top.p.b", NULL);
    c_ = vhpi_handle_by_name("top.p.c", NULL);
    r_ = vhpi_handle_by_name("top.p.r", NULL);
    watch(a_, on_a, (void*)"first", &first_);
    watch(a_, changed, (void*)"second", &second_);
    after(2, 2);
    after(4, 4);
    after(6, 6);
    after(8, 8);
    show("start");
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

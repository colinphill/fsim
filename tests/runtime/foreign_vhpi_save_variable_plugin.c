// SPDX-License-Identifier: Apache-2.0
// Forcing variables across a save and a restart (IEEE 1076-2019 22.5.2,
// 20.7, 20.8): a library forces a variable and a subelement of another before
// the save; a fresh process that restarts finds them still forced, because a
// force is state of the simulation and not a callback. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static vhpiHandleT n_;
static vhpiHandleT v_;
static vhpiHandleT v3_;

static unsigned long long now_fs(void)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    return ((unsigned long long)now.high << 32) | now.low;
}

static void handles(void)
{
    n_ = vhpi_handle_by_name("top.p.n", NULL);
    v_ = vhpi_handle_by_name("top.p.v", NULL);
    v3_ = v_ != NULL ? vhpi_handle_by_index(vhpiIndexedNames, v_, 0) : NULL;
}

static void show(const char* what)
{
    char text[32];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    printf("#c %s at %llu fs: n=%d", what, now_fs(),
        vhpi_get_value(n_, &value) == 0 ? (int)value.value.intg : -1);
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof text;
    value.value.str = (vhpiCharT*)text;
    printf(" v=%s\n", vhpi_get_value(v_, &value) == 0 ? text : "?");
    fflush(stdout);
}

static void force_both(void)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = 100;
    if (vhpi_put_value(n_, &value, vhpiForce) != 0) {
        printf("#c force of n failed\n");
    }
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    value.value.enumv = vhpi1;
    if (vhpi_put_value(v3_, &value, vhpiForce) != 0) {
        printf("#c force of v(3) failed\n");
    }
}

static void release_both(void)
{
    if (vhpi_put_value(n_, NULL, vhpiRelease) != 0
        || vhpi_put_value(v3_, NULL, vhpiRelease) != 0) {
        printf("#c release failed\n");
    }
}

static void step(const vhpiCbDataT* data)
{
    const int at = (int)(intptr_t)data->user_data;
    if (at == 22) {
        force_both();
        show("forced");
    } else if (at == 60) {
        release_both();
        show("released");
    } else {
        show("step");
    }
}

static void after(int ns, int tag)
{
    vhpiTimeT delay;
    vhpiCbDataT request;
    memset(&request, 0, sizeof request);
    delay.high = 0;
    delay.low = (uint32_t)ns * 1000000U;
    request.reason = vhpiCbAfterDelay;
    request.cb_rtn = step;
    request.time = &delay;
    request.user_data = (void*)(intptr_t)tag;
    vhpi_register_cb(&request, 0);
}

static void register_reason(int32_t reason, void (*routine)(const vhpiCbDataT*))
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    (void)vhpi_register_cb(&data, 0);
}

static void start_of_simulation(const vhpiCbDataT* data)
{
    (void)data;
    handles();
    show("start");
    after(22, 22);
    after(30, 30);
    after(45, 45);
    after(58, 58);
    after(60, 60);
    after(70, 70);
    after(90, 90);
}

static void end_of_restart(const vhpiCbDataT* data)
{
    (void)data;
    handles();
    show("restarted");
    after(5, 55);
    after(10, 60);
    after(20, 70);
    after(40, 90);
}

static void start_of_restart(const vhpiCbDataT* data)
{
    (void)data;
}

static void start_of_save(const vhpiCbDataT* data)
{
    (void)data;
    show("saved");
    register_reason(vhpiCbStartOfRestart, start_of_restart);
    register_reason(vhpiCbEndOfRestart, end_of_restart);
}

static void startup(void)
{
    register_reason(vhpiCbStartOfSimulation, start_of_simulation);
    register_reason(vhpiCbStartOfSave, start_of_save);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

// SPDX-License-Identifier: Apache-2.0
// A VHPI deposit of 'H' into a std_logic design with no weak value of its
// own (IEEE 1076-2019 22.5.3; IEEE 1164 'H' is a weak 1). vhpiDepositPropagate
// schedules the deposit for the next signal update phase, so the read right
// after the call still sees the old value '0'. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static vhpiHandleT s;
static vhpiHandleT r;
static vhpiHandleT e;
static vhpiHandleT q;

static unsigned long long now(void)
{
    vhpiTimeT time;
    vhpi_get_time(&time, NULL);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static const char* text(vhpiHandleT object)
{
    static char buffer[4][16];
    static unsigned next;
    char* out = buffer[next++ % 4U];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = 16;
    value.value.str = (vhpiCharT*)out;
    if (vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return out;
}

static void after(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu s=%s r=%s e=%s q=%s\n", now(), text(s), text(r), text(e),
        text(q));
    fflush(stdout);
}

static void deposit(const vhpiCbDataT* data)
{
    vhpiValueT value;
    vhpiCbDataT next;
    vhpiTimeT time;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    value.value.enumv = vhpiH;
    vhpi_put_value(s, &value, vhpiDepositPropagate);
    printf("#c %llu s=%s after deposit\n", now(), text(s));
    fflush(stdout);
    memset(&next, 0, sizeof next);
    time.high = 0;
    time.low = 5000000U;
    next.reason = vhpiCbAfterDelay;
    next.cb_rtn = after;
    next.time = &time;
    (void)vhpi_register_cb(&next, 0);
}

static void start(const vhpiCbDataT* data)
{
    vhpiCbDataT first;
    vhpiTimeT time;
    (void)data;
    s = vhpi_handle_by_name("top.s", NULL);
    r = vhpi_handle_by_name("top.r", NULL);
    e = vhpi_handle_by_name("top.e", NULL);
    q = vhpi_handle_by_name("top.q", NULL);
    memset(&first, 0, sizeof first);
    time.high = 0;
    time.low = 5000000U;
    first.reason = vhpiCbAfterDelay;
    first.cb_rtn = deposit;
    first.time = &time;
    (void)vhpi_register_cb(&first, 0);
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

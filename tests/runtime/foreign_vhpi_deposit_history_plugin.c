// SPDX-License-Identifier: Apache-2.0
// A VHPI deposit into a signal is a scheduled deposit, which makes the
// signal active in the update phase it takes effect in, whether or not the
// value changes (IEEE 1076-2019 14.7.3.1, 22.5.3).
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void deposit(const vhpiCbDataT* data)
{
    vhpiValueT value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    value.value.enumv = vhpi1;
    vhpi_put_value(vhpi_handle_by_name("top.s", NULL), &value,
        vhpiDepositPropagate);
}

static void after(unsigned long long delay)
{
    vhpiCbDataT data;
    vhpiTimeT time;
    memset(&data, 0, sizeof data);
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    data.reason = vhpiCbAfterDelay;
    data.cb_rtn = deposit;
    data.time = &time;
    (void)vhpi_register_cb(&data, 0);
}

static long integer_value(const char* name)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    if (vhpi_get_value(vhpi_handle_by_name(name, NULL), &value) != 0) {
        return -2;
    }
    return (long)value.value.intg;
}

static void report(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c 6 ns: event=%ld active=%ld\n", integer_value("top.event6"),
        integer_value("top.active6"));
    printf("#c 9 ns: event=%ld active=%ld\n", integer_value("top.event9"),
        integer_value("top.active9"));
    fflush(stdout);
}

static void start(const vhpiCbDataT* data)
{
    vhpiCbDataT cb;
    vhpiTimeT time;
    (void)data;
    after(5000000U);
    after(8000000U);
    memset(&cb, 0, sizeof cb);
    time.high = 0;
    time.low = 10000000U;
    cb.reason = vhpiCbAfterDelay;
    cb.cb_rtn = report;
    cb.time = &time;
    (void)vhpi_register_cb(&cb, 0);
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

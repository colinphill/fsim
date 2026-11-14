// SPDX-License-Identifier: Apache-2.0
// Deposits around the first simulation cycle (IEEE 1076-2019 14.7.5.2,
// 14.7.5.3, 20.6, 21.3.2.2, 22.5.2).
//   vhpiCbStartOfInitialization, before step c) of initialization: top.ps
//   (vhpiDepositPropagate) and top.ds (vhpiDeposit). Step c) sets the
//   driving and current values from the drivers and drops a scheduled
//   deposit, so neither leaves a trace.
//   vhpiCbStartOfSimulation, after initialization: top.p
//   (vhpiDepositPropagate) is updated in the first simulation cycle, an
//   event and a transaction there; top.d (vhpiDeposit) takes the value at
//   once, neither an event, a transaction nor a vhpiCbValueChange trigger.
//   vhpiCbAfterDelay at 5 ns: top.q (vhpiDeposit), the same at a later time.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void event(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    vhpi_get_time(&now, NULL);
    printf("#c %s %s at %u fs: %s\n",
        data->reason == vhpiCbTransaction ? "transaction" : "change",
        (const char*)vhpi_get_str(vhpiNameP, data->obj), (unsigned)now.low,
        vhpi_get_value(data->obj, &value) != 0 ? "?"
            : value.value.enumv == vhpi1           ? "1"
            : value.value.enumv == vhpi0           ? "0"
                                                   : "other");
    fflush(stdout);
}

static void watch(const char* name, int reason)
{
    vhpiCbDataT request;
    memset(&request, 0, sizeof request);
    request.reason = reason;
    request.cb_rtn = event;
    request.obj = vhpi_handle_by_name(name, NULL);
    (void)vhpi_register_cb(&request, 0);
}

static void put(const char* name, vhpiPutValueModeT mode)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    value.value.enumv = vhpi1;
    if (vhpi_put_value(vhpi_handle_by_name(name, NULL), &value, mode) != 0) {
        printf("#c put %s failed\n", name);
    }
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
    printf("#c p: init event=%ld value=%ld wakes=%ld wake event=%ld "
           "active=%ld\n",
        integer_value("top.p_init_event"), integer_value("top.p_init_value"),
        integer_value("top.p_wakes"), integer_value("top.p_wake_event"),
        integer_value("top.p_wake_active"));
    printf("#c d: init event=%ld value=%ld wakes=%ld\n",
        integer_value("top.d_init_event"), integer_value("top.d_init_value"),
        integer_value("top.d_wakes"));
    printf("#c ps: init value=%ld wakes=%ld\n",
        integer_value("top.ps_init_value"), integer_value("top.ps_wakes"));
    printf("#c ds: init value=%ld wakes=%ld\n",
        integer_value("top.ds_init_value"), integer_value("top.ds_wakes"));
    printf("#c first cycle: p event=%ld active=%ld d event=%ld active=%ld "
           "value=%ld\n",
        integer_value("top.first_p_event"),
        integer_value("top.first_p_active"),
        integer_value("top.first_d_event"),
        integer_value("top.first_d_active"),
        integer_value("top.first_d_value"));
    printf("#c at 5 ns: q event=%ld active=%ld value=%ld wakes=%ld\n",
        integer_value("top.q_event"), integer_value("top.q_active"),
        integer_value("top.q_value"), integer_value("top.q_wakes"));
    fflush(stdout);
}

static void on(int reason, void (*routine)(const vhpiCbDataT*),
    const unsigned long long delay)
{
    vhpiCbDataT cb;
    vhpiTimeT time;
    memset(&cb, 0, sizeof cb);
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    cb.reason = reason;
    cb.cb_rtn = routine;
    if (reason == vhpiCbAfterDelay) {
        cb.time = &time;
    }
    (void)vhpi_register_cb(&cb, 0);
}

static void initialization(const vhpiCbDataT* data)
{
    static const char* const names[] = { "top.p", "top.d", "top.ps",
        "top.ds", "top.q" };
    (void)data;
    printf("#c start of initialization\n");
    fflush(stdout);
    for (unsigned index = 0U; index < sizeof names / sizeof names[0];
         ++index) {
        watch(names[index], vhpiCbValueChange);
        watch(names[index], vhpiCbTransaction);
    }
    put("top.ps", vhpiDepositPropagate);
    put("top.ds", vhpiDeposit);
}

static void five(const vhpiCbDataT* data)
{
    (void)data;
    put("top.q", vhpiDeposit);
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c start of simulation\n");
    fflush(stdout);
    put("top.p", vhpiDepositPropagate);
    put("top.d", vhpiDeposit);
    on(vhpiCbAfterDelay, five, 5000000U);
    on(vhpiCbAfterDelay, report, 6000000U);
}

static void startup(void)
{
    on(vhpiCbStartOfInitialization, initialization, 0U);
    on(vhpiCbStartOfSimulation, start, 0U);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

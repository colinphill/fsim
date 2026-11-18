// SPDX-License-Identifier: Apache-2.0
// Processes and drivers made by vhpi_create across a reset (IEEE 1076-2019
// 14.7.2, 14.7.3.2, 20.5, 20.9, 23.5, 23.6, 23.34). The foreign architecture
// summer has the user-resolved integer w (sum of its sources, default 10) and
// the out port y.
//   Elaboration (elab): two processes; the first drives y and w, the second
//   drives w, and a driver of no process drives w: three sources of w.
//   First life (exec): the sources drive 1, 2 and 100; at 5 ns a fourth
//   source of w is made with no process (driver default 10, then 1000 at
//   7 ns); at 9 ns transactions are scheduled for 14 ns on it and on y; at
//   10 ns vhpi_control(vhpiReset).
//   The reset returns the model to the beginning of the initialization phase
//   (20.9): the elaboration function does not run again, every created
//   process and driver still exists (no step of 20.9 removes one) and each
//   driver's projected output waveform is back at its initial contents
//   (20.9 c), so the 14 ns transactions never happen and w starts at the
//   sum of four defaults, 40 (14.7.5.2).
//   Second life (exec again, 20.5): the four sources drive 3, 4, 5 and, from
//   2 ns, 6; a second driver of w for the second process is the 23.6 error.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

#define NS 1000000ULL

static char library_name[] = "resetlib";
static char model_name[] = "summer";

static int life = 1;
static vhpiHandleT process_first;
static vhpiHandleT process_second;
static vhpiHandleT signal_w;
static vhpiHandleT driver_y;
static vhpiHandleT driver_w_first;
static vhpiHandleT driver_w_second;
static vhpiHandleT driver_w_none;
static vhpiHandleT driver_w_late;

static const char* phase_name(void)
{
    static const char* const names[] = { "", "registration", "analysis",
        "elaboration", "initialization", "simulation", "termination", "save",
        "restart", "reset" };
    const vhpiIntT current = vhpi_get(vhpiPhaseP, NULL);
    return current >= 0 && current <= 9 ? names[current] : "?";
}

static unsigned long long now(void)
{
    vhpiTimeT time;
    vhpi_get_time(&time, NULL);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static int value_w(void)
{
    vhpiValueT value;
    vhpiHandleT object = vhpi_handle_by_name("top.u1.w", NULL);
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        return -1;
    }
    return (int)value.value.intg;
}

static int value_y(void)
{
    vhpiValueT value;
    vhpiHandleT object = vhpi_handle_by_name("top.y", NULL);
    memset(&value, 0, sizeof value);
    value.format = vhpiEnumVal;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        return -1;
    }
    return (int)value.value.enumv;
}

static void register_cb(int32_t reason, void (*routine)(const vhpiCbDataT*),
    unsigned long long delay)
{
    vhpiCbDataT data;
    vhpiTimeT time;
    memset(&data, 0, sizeof data);
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    data.reason = reason;
    data.cb_rtn = routine;
    data.time = reason == vhpiCbAfterDelay ? &time : NULL;
    (void)vhpi_register_cb(&data, 0);
}

// A transaction on a scalar driver (IEEE 1076-2019 23.34).
static int schedule(vhpiHandleT driver, vhpiFormatT format, int number,
    unsigned long long delay)
{
    vhpiValueT value;
    vhpiTimeT time;
    memset(&value, 0, sizeof value);
    memset(&time, 0, sizeof time);
    value.format = format;
    if (format == vhpiIntVal) {
        value.value.intg = (vhpiIntT)number;
    } else {
        value.value.enumv = (vhpiEnumT)number;
    }
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    return vhpi_schedule_transaction(
        driver, &value, 1U, &time, vhpiTransport, NULL);
}

static void show(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c life %d %llu ns w=%d y=%d\n", life, now() / NS, value_w(),
        value_y());
    fflush(stdout);
}

// The elaboration function (IEEE 1076-2019 20.4.1): runs once, before the
// first initialization phase only.
static void elab(const vhpiCbDataT* data)
{
    vhpiHandleT instance = data->obj;
    vhpiHandleT architecture = vhpi_handle(vhpiDesignUnit, instance);
    printf("#c elab phase=%s\n", phase_name());
    process_first = vhpi_create(vhpiProcessStmtK, architecture, NULL);
    process_second = vhpi_create(vhpiProcessStmtK, architecture, NULL);
    driver_y = vhpi_create(
        vhpiDriverK, vhpi_handle_by_name("y", instance), process_first);
    signal_w = vhpi_handle_by_name("w", instance);
    driver_w_first = vhpi_create(vhpiDriverK, signal_w, process_first);
    driver_w_second = vhpi_create(vhpiDriverK, signal_w, process_second);
    driver_w_none = vhpi_create(vhpiDriverK, signal_w, NULL);
    printf("#c elab processes=%d%d drivers=%d%d%d%d\n", process_first != NULL,
        process_second != NULL, driver_y != NULL, driver_w_first != NULL,
        driver_w_second != NULL, driver_w_none != NULL);
    fflush(stdout);
}

// A fourth source of w, made in the simulation phase (23.6): the driver
// default 10 until its transaction at 7 ns.
static void late(const vhpiCbDataT* data)
{
    int scheduled;
    (void)data;
    driver_w_late = vhpi_create(vhpiDriverK, signal_w, NULL);
    scheduled = driver_w_late != NULL
        ? schedule(driver_w_late, vhpiIntVal, 1000, 2U * NS)
        : -1;
    printf("#c life %d %llu ns late driver=%d schedule=%d\n", life,
        now() / NS, driver_w_late != NULL, scheduled);
    fflush(stdout);
}

// Transactions still pending at the reset, due at 14 ns.
static void pending(const vhpiCbDataT* data)
{
    int scheduled;
    (void)data;
    scheduled = schedule(driver_w_late, vhpiIntVal, 7, 5U * NS)
        | schedule(driver_y, vhpiEnumVal, 1, 5U * NS);
    printf("#c life %d %llu ns pending schedule=%d\n", life, now() / NS,
        scheduled);
    fflush(stdout);
}

static void request_reset(const vhpiCbDataT* data)
{
    int rc;
    (void)data;
    printf("#c life %d %llu ns reset\n", life, now() / NS);
    fflush(stdout);
    rc = vhpi_control(vhpiReset);
    printf("#c life %d reset rc=%d error=%d\n", life, rc,
        vhpi_check_error(NULL));
    fflush(stdout);
}

static void start_of_reset(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c start of reset phase=%s at %llu ns\n", phase_name(),
        now() / NS);
    fflush(stdout);
}

// The created objects are still valid (20.9: they are not removed).
static void end_of_reset(const vhpiCbDataT* data)
{
    (void)data;
    life = 2;
    printf("#c end of reset phase=%s at %llu ns w=%d y=%d late foreign=%d\n",
        phase_name(), now() / NS, value_w(), value_y(),
        driver_w_late != NULL
            ? (int)vhpi_get(vhpiIsForeignP, driver_w_late)
            : -1);
    fflush(stdout);
}

// The execution function, at step g) of each initialization phase (IEEE
// 1076-2019 14.7.5.2, 20.5).
static void exec(const vhpiCbDataT* data)
{
    int scheduled;
    (void)data;
    printf("#c exec life=%d phase=%s at %llu ns w=%d y=%d\n", life,
        phase_name(), now() / NS, value_w(), value_y());
    if (life == 1) {
        scheduled = schedule(driver_w_first, vhpiIntVal, 1, 0U)
            | schedule(driver_w_second, vhpiIntVal, 2, 0U)
            | schedule(driver_w_none, vhpiIntVal, 100, 0U)
            | schedule(driver_y, vhpiEnumVal, 1, 0U);
        register_cb(vhpiCbAfterDelay, show, 1U * NS);
        register_cb(vhpiCbAfterDelay, late, 5U * NS);
        register_cb(vhpiCbAfterDelay, show, 6U * NS);
        register_cb(vhpiCbAfterDelay, show, 8U * NS);
        register_cb(vhpiCbAfterDelay, pending, 9U * NS);
        register_cb(vhpiCbAfterDelay, request_reset, 10U * NS);
    } else {
        // The second process still has its driver of w (23.6 error).
        printf("#c exec second driver of w again=%d\n",
            vhpi_create(vhpiDriverK, signal_w, process_second) != NULL);
        scheduled = schedule(driver_w_first, vhpiIntVal, 3, 0U)
            | schedule(driver_w_second, vhpiIntVal, 4, 0U)
            | schedule(driver_w_none, vhpiIntVal, 5, 0U)
            | schedule(driver_w_late, vhpiIntVal, 6, 2U * NS);
        register_cb(vhpiCbAfterDelay, show, 1U * NS);
        register_cb(vhpiCbAfterDelay, show, 3U * NS);
        register_cb(vhpiCbAfterDelay, show, 15U * NS);
    }
    printf("#c exec life=%d schedule=%d\n", life, scheduled);
    fflush(stdout);
}

static void startup(void)
{
    vhpiForeignDataT model;
    memset(&model, 0, sizeof model);
    model.kind = vhpiArchF;
    model.libraryName = library_name;
    model.modelName = model_name;
    model.elabf = elab;
    model.execf = exec;
    (void)vhpi_register_foreignf(&model);
    register_cb(vhpiCbStartOfReset, start_of_reset, 0U);
    register_cb(vhpiCbEndOfReset, end_of_reset, 0U);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

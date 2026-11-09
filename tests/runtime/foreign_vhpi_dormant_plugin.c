// SPDX-License-Identifier: Apache-2.0
// VHPI foreign architecture drivers before and after vhpi_create (IEEE
// 1076-2019 6.4.2.3, 14.7.3.2, 20.4.2, 23.6): each out port of the instance
// is a source driving its default until a driver for it is created, for the
// std_logic ports y and q and for the port w whose user resolution function
// sums its sources (w: top's initial 10 + the port's default 10 at the start
// of simulation, then 5 + the created driver's default 10, then 5 + its
// transaction 7). Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

#define NS 1000000ULL

static char library_name[] = "dormlib";
static char model_name[] = "dorm";

static vhpiHandleT foreign_architecture;
static vhpiHandleT port_y;
static vhpiHandleT port_w;
static vhpiHandleT driver_y;
static vhpiHandleT driver_w;

static unsigned long long now(void)
{
    vhpiTimeT time;
    vhpi_get_time(&time, NULL);
    return ((unsigned long long)time.high << 32U) | time.low;
}

// The binary string of the object `name` names, or "?". Each call returns
// its own buffer (a printf call uses several), the last four calls' kept.
static const char* text(const char* name)
{
    static char buffers[4][80];
    static unsigned next;
    char* buffer = buffers[next++ % 4U];
    vhpiValueT value;
    vhpiHandleT object = vhpi_handle_by_name(name, NULL);
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffers[0];
    value.value.str = (vhpiCharT*)buffer;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

// The integer value of the object `name` names, or -1.
static int integer_value(const char* name)
{
    vhpiValueT value;
    vhpiHandleT object = vhpi_handle_by_name(name, NULL);
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        return -1;
    }
    return (int)value.value.intg;
}

static int schedule_integer(
    vhpiHandleT driver, int number, unsigned long long delay)
{
    vhpiValueT value;
    vhpiTimeT time;
    memset(&value, 0, sizeof value);
    memset(&time, 0, sizeof time);
    value.format = vhpiIntVal;
    value.value.intg = number;
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    return vhpi_schedule_transaction(
        driver, &value, 1U, &time, vhpiTransport, NULL);
}

static int count_drivers(const char* name)
{
    int count = 0;
    vhpiHandleT iterator
        = vhpi_iterator(vhpiDrivers, vhpi_handle_by_name(name, NULL));
    if (iterator == NULL) {
        return 0;
    }
    while (vhpi_scan(iterator) != NULL) {
        ++count;
    }
    return count;
}

static void register_after(void (*routine)(const vhpiCbDataT*),
    unsigned long long delay)
{
    vhpiCbDataT data;
    vhpiTimeT time;
    memset(&data, 0, sizeof data);
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    data.reason = vhpiCbAfterDelay;
    data.cb_rtn = routine;
    data.time = &time;
    (void)vhpi_register_cb(&data, vhpiReturnCb);
}

// A std_logic transaction by position ('X' 1, '0' 2, '1' 3).
static int schedule_logic(
    vhpiHandleT driver, int position, unsigned long long delay)
{
    vhpiValueT value;
    vhpiTimeT time;
    memset(&value, 0, sizeof value);
    memset(&time, 0, sizeof time);
    value.format = vhpiEnumVal;
    value.value.enumv = (vhpiEnumT)position;
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    return vhpi_schedule_transaction(
        driver, &value, 1U, &time, vhpiTransport, NULL);
}

static void elab(const vhpiCbDataT* data)
{
    foreign_architecture = vhpi_handle(vhpiDesignUnit, data->obj);
    port_y = vhpi_handle_by_name("y", data->obj);
    port_w = vhpi_handle_by_name("w", data->obj);
    printf("#c elab foreign=%d\n", (int)vhpi_get(vhpiIsForeignP, foreign_architecture));
    fflush(stdout);
}

static void exec(const vhpiCbDataT* data)
{
    (void)data;
}

static void show(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu ns y=%s q=%s w=%d drivers y=%d q=%d w=%d\n", now() / NS,
        text("top.y"), text("top.q"), integer_value("top.w"),
        count_drivers("top.u1.y"), count_drivers("top.u1.q"),
        count_drivers("top.u1.w"));
    fflush(stdout);
}

// The first drivers of y and w: a created process and its drivers. Each
// signal holds the driver's default until the transaction, two nanoseconds
// later.
static void create(const vhpiCbDataT* data)
{
    vhpiHandleT process = vhpi_create(vhpiProcessStmtK, foreign_architecture, NULL);
    int scheduled = -1;
    int w_scheduled = -1;
    (void)data;
    driver_y = process != NULL ? vhpi_create(vhpiDriverK, port_y, process) : NULL;
    driver_w = process != NULL ? vhpi_create(vhpiDriverK, port_w, process) : NULL;
    if (driver_y != NULL) {
        scheduled = schedule_logic(driver_y, 3, 2U * NS);
    }
    if (driver_w != NULL) {
        w_scheduled = schedule_integer(driver_w, 7, 2U * NS);
    }
    printf("#c %llu ns create process=%d driver=%d schedule=%d wdriver=%d "
           "wschedule=%d\n",
        now() / NS, process != NULL, driver_y != NULL, scheduled,
        driver_w != NULL, w_scheduled);
    fflush(stdout);
}

static void again(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu ns schedule=%d\n", now() / NS,
        schedule_logic(driver_y, 2, 2U * NS));
    fflush(stdout);
}

static void on_start(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c start y=%s q=%s w=%d drivers y=%d q=%d w=%d\n", text("top.y"),
        text("top.q"), integer_value("top.w"), count_drivers("top.u1.y"),
        count_drivers("top.u1.q"), count_drivers("top.u1.w"));
    register_after(create, 5U * NS);
    register_after(show, 6U * NS);
    register_after(show, 8U * NS);
    register_after(again, 10U * NS);
    register_after(show, 13U * NS);
    fflush(stdout);
}

static void startup(void)
{
    vhpiForeignDataT model;
    vhpiCbDataT start;
    memset(&model, 0, sizeof model);
    model.kind = vhpiArchF;
    model.libraryName = library_name;
    model.modelName = model_name;
    model.elabf = elab;
    model.execf = exec;
    (void)vhpi_register_foreignf(&model);
    memset(&start, 0, sizeof start);
    start.reason = vhpiCbStartOfSimulation;
    start.cb_rtn = on_start;
    (void)vhpi_register_cb(&start, vhpiReturnCb);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

// SPDX-License-Identifier: Apache-2.0
// VHPI foreign architecture conformance plug-in (IEEE 1076-2019 14.5.1,
// 14.7.5.2, 20.2.4, 20.4, 20.5, 22.5.4, 22.6, 23.6, 23.34): a registered
// foreign architecture whose elaboration function creates a process and
// drivers and whose execution function schedules transactions. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

#define NS 1000000ULL

static char library_name[] = "archlib";
static char model_name[] = "buf";

static vhpiHandleT driver_y;
static vhpiHandleT driver_state;
static vhpiHandleT collection_v;
static vhpiHandleT signal_a;

static const char* phase_name(void)
{
    static const char* const names[] = { "", "registration", "analysis",
        "elaboration", "initialization", "simulation", "termination" };
    const vhpiIntT current = vhpi_get(vhpiPhaseP, NULL);
    return current >= 0 && current <= 6 ? names[current] : "?";
}

static unsigned long long now(void)
{
    vhpiTimeT time;
    vhpi_get_time(&time, NULL);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static const char* text(const char* name)
{
    static char buffer[80];
    vhpiValueT value;
    vhpiHandleT object = vhpi_handle_by_name(name, NULL);
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

static vhpiHandleT register_cb(int32_t reason, vhpiHandleT object,
    void (*routine)(const vhpiCbDataT*), unsigned long long delay, char* name)
{
    vhpiCbDataT data;
    vhpiTimeT time;
    memset(&data, 0, sizeof data);
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    data.reason = reason;
    data.cb_rtn = routine;
    data.obj = object;
    data.time = delay != 0U ? &time : NULL;
    data.user_data = name;
    return vhpi_register_cb(&data, vhpiReturnCb);
}

static void on_phase(const vhpiCbDataT* data)
{
    printf("#c cb %s phase=%s\n", (const char*)data->user_data, phase_name());
    fflush(stdout);
}

// A transaction with an enumeration value on a scalar driver (IEEE 1076-2019
// 23.34).
static int schedule_bit(vhpiHandleT driver, int bit, unsigned long long delay)
{
    vhpiValueT value;
    vhpiTimeT time;
    memset(&value, 0, sizeof value);
    memset(&time, 0, sizeof time);
    value.format = vhpiEnumVal;
    value.value.enumv = (vhpiEnumT)bit;
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    return vhpi_schedule_transaction(
        driver, &value, 1U, &time, vhpiTransport, NULL);
}

// A transaction on a collection of drivers with one array value.
static int schedule_vector(vhpiHandleT drivers, char* bits)
{
    vhpiValueT value;
    vhpiTimeT time;
    memset(&value, 0, sizeof value);
    memset(&time, 0, sizeof time);
    value.format = vhpiBinStrVal;
    value.numElems = (int32_t)strlen(bits);
    value.bufSize = (size_t)strlen(bits) + 1U;
    value.value.str = (vhpiCharT*)bits;
    return vhpi_schedule_transaction(
        drivers, &value, 1U, &time, vhpiInertial, NULL);
}

// The elaboration function of the foreign architecture (IEEE 1076-2019
// 20.4.1): it creates the process and a driver for each signal and each
// out port of the instance (23.6).
static void elab(const vhpiCbDataT* data)
{
    vhpiHandleT instance = data->obj;
    vhpiHandleT architecture = vhpi_handle(vhpiDesignUnit, instance);
    vhpiHandleT process;
    vhpiHandleT other;
    vhpiHandleT port_y = vhpi_handle_by_name("y", instance);
    vhpiHandleT port_v = vhpi_handle_by_name("v", instance);
    vhpiHandleT state = vhpi_handle_by_name("state", instance);
    vhpiHandleT iterator;
    vhpiHandleT basic;
    vhpiHandleT member;
    printf("#c elab %s phase=%s reason=%d foreign=%d\n",
        (const char*)vhpi_get_str(vhpiFullNameP, instance), phase_name(),
        data->reason == vhpiCbStartOfElaboration,
        (int)vhpi_get(vhpiIsForeignP, architecture));
    process = vhpi_create(vhpiProcessStmtK, architecture, NULL);
    printf("#c elab process foreign=%d passive=%d postponed=%d\n",
        (int)vhpi_get(vhpiIsForeignP, process),
        (int)vhpi_get(vhpiIsPassiveP, process),
        (int)vhpi_get(vhpiIsPostponedP, process));
    other = vhpi_create(vhpiProcessStmtK, architecture, NULL);
    printf("#c elab second process=%d\n", other != NULL);
    driver_y = vhpi_create(vhpiDriverK, port_y, process);
    printf("#c elab driver y write=%d foreign=%d\n",
        (int)(vhpi_get(vhpiAccessP, driver_y) & vhpiWrite) != 0,
        (int)vhpi_get(vhpiIsForeignP, driver_y));
    other = vhpi_create(vhpiDriverK, port_y, process);
    printf("#c elab duplicate driver=%d\n", other != NULL);
    iterator = vhpi_iterator(vhpiBasicSignals, port_v);
    while ((basic = vhpi_scan(iterator)) != NULL) {
        member = vhpi_create(vhpiDriverK, basic, process);
        collection_v = vhpi_create(vhpiDriverCollectionK, collection_v, member);
    }
    printf("#c elab driver v members=%d\n",
        (int)vhpi_get(vhpiNumMembersP, collection_v));
    driver_state = vhpi_create(vhpiDriverK, state, process);
    printf("#c elab driver state=%d\n", driver_state != NULL);
    // The members of a driverCollection are drivers of one declared signal.
    other = vhpi_create(vhpiDriverCollectionK, collection_v, driver_y);
    printf("#c elab mixed collection=%d\n", other != NULL);
    fflush(stdout);
}

// The model is an inverter with a delay of 3 ns on y (IEEE 1076-2019 22.6).
static void on_a(const vhpiCbDataT* data)
{
    int high = text("top.a")[0] == '1';
    int first = schedule_bit(driver_y, !high, 3U * NS);
    int second = schedule_vector(collection_v, high ? "01" : "10");
    (void)data;
    printf("#c %llu ns a=%d y=%d v=%d\n", now() / NS, high, first, second);
    fflush(stdout);
}

static void show(const vhpiCbDataT* data)
{
    char values[80];
    (void)data;
    snprintf(values, sizeof values, "a=%s", text("top.a"));
    printf("#c %llu ns %s", now() / NS, values);
    printf(" y=%s", text("top.y"));
    printf(" v=%s", text("top.v"));
    printf(" z=%s", text("top.z"));
    printf(" state=%s\n", text("top.u1.state"));
    fflush(stdout);
}

// The execution function runs at step g) of the initialization phase
// (IEEE 1076-2019 14.7.5.2, 20.5).
static void exec(const vhpiCbDataT* data)
{
    static char name[] = "show";
    int y;
    int v;
    int state;
    printf("#c exec %s phase=%s reason=%d\n",
        (const char*)vhpi_get_str(vhpiFullNameP, data->obj), phase_name(),
        data->reason == vhpiCbStartOfInitialization);
    printf("#c exec foreign kind=%d\n",
        (int)vhpi_get(vhpiForeignKindP,
            vhpi_handle_by_index(vhpiForeignfs, NULL, 0)));
    signal_a = vhpi_handle_by_name("top.a", NULL);
    (void)register_cb(vhpiCbValueChange, signal_a, on_a, 0U, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show, 1U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show, 11U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show, 14U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show, 21U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show, 24U * NS, name);
    y = schedule_bit(driver_y, 1, 0U);
    v = schedule_vector(collection_v, "10");
    state = schedule_bit(driver_state, 1, 0U);
    printf("#c exec schedule y=%d v=%d state=%d\n", y, v, state);
    fflush(stdout);
}

static void startup(void)
{
    static char start_elab[] = "start-of-elaboration";
    static char end_elab[] = "end-of-elaboration";
    static char start_init[] = "start-of-initialization";
    vhpiForeignDataT model;
    memset(&model, 0, sizeof model);
    model.kind = vhpiArchF;
    model.libraryName = library_name;
    model.modelName = model_name;
    model.elabf = elab;
    model.execf = exec;
    (void)vhpi_register_foreignf(&model);
    (void)register_cb(vhpiCbStartOfElaboration, NULL, on_phase, 0U, start_elab);
    (void)register_cb(vhpiCbEndOfElaboration, NULL, on_phase, 0U, end_elab);
    (void)register_cb(
        vhpiCbStartOfInitialization, NULL, on_phase, 0U, start_init);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

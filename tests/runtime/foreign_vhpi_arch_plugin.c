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
static vhpiHandleT driver_r_first;
static vhpiHandleT driver_r_second;
static vhpiHandleT driver_r_none;
static vhpiHandleT signal_w;
static vhpiHandleT driver_w_first;
static vhpiHandleT driver_w_second;
static vhpiHandleT driver_w_none;
static vhpiHandleT driver_w_late;
static vhpiHandleT foreign_architecture;
static vhpiHandleT basic_p[2];
static vhpiHandleT driver_p_first[2];

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

// A transaction with a std_logic value, by position ('X' 1, '0' 2, '1' 3,
// 'Z' 4).
static int schedule_logic(
    vhpiHandleT driver, int position, unsigned long long delay)
{
    return schedule_bit(driver, position, delay);
}

// A transaction with an integer value (IEEE 1076-2019 23.34).
static int schedule_int(vhpiHandleT driver, int number, unsigned long long delay)
{
    vhpiValueT value;
    vhpiTimeT time;
    memset(&value, 0, sizeof value);
    memset(&time, 0, sizeof time);
    value.format = vhpiIntVal;
    value.value.intg = (vhpiIntT)number;
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
    vhpiHandleT second_process;
    vhpiHandleT other;
    vhpiHandleT signal_r = vhpi_handle_by_name("r", instance);
    vhpiHandleT driver;
    int drivers = 0;
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
    // Any number of processes per instance (23.6).
    second_process = vhpi_create(vhpiProcessStmtK, architecture, NULL);
    printf("#c elab second process=%d foreign=%d\n", second_process != NULL,
        (int)vhpi_get(vhpiIsForeignP, second_process));
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
    driver_r_first = vhpi_create(vhpiDriverK, signal_r, process);
    driver_r_second = vhpi_create(vhpiDriverK, signal_r, second_process);
    driver_r_none = vhpi_create(vhpiDriverK, signal_r, NULL);
    printf("#c elab drivers r=%d%d%d\n", driver_r_first != NULL,
        driver_r_second != NULL, driver_r_none != NULL);
    // w resolves through a user-defined function (14.7.3.2): the same three
    // sources, the second and third created like those of r.
    signal_w = vhpi_handle_by_name("w", instance);
    driver_w_first = vhpi_create(vhpiDriverK, signal_w, process);
    driver_w_second = vhpi_create(vhpiDriverK, signal_w, second_process);
    driver_w_none = vhpi_create(vhpiDriverK, signal_w, NULL);
    printf("#c elab drivers w=%d%d%d\n", driver_w_first != NULL,
        driver_w_second != NULL, driver_w_none != NULL);
    // p resolves each of its two elements with the same function: the
    // placeholder process of the instance drives both.
    foreign_architecture = architecture;
    iterator = vhpi_iterator(vhpiBasicSignals,
        vhpi_handle_by_name("p", instance));
    for (int element = 0; element < 2; ++element) {
        basic_p[element] = vhpi_scan(iterator);
        driver_p_first[element] = basic_p[element] != NULL
            ? vhpi_create(vhpiDriverK, basic_p[element], process)
            : NULL;
    }
    printf("#c elab drivers p=%d%d\n", driver_p_first[0] != NULL,
        driver_p_first[1] != NULL);
    iterator = vhpi_iterator(vhpiDrivers, signal_r);
    while ((driver = vhpi_scan(iterator)) != NULL) {
        ++drivers;
    }
    printf("#c elab r drivers=%d\n", drivers);
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
    printf(" state=%s", text("top.u1.state"));
    printf(" r=%s\n", text("top.u1.r"));
    fflush(stdout);
}

// The user-resolved integer w, as a number.
static void show_w(const vhpiCbDataT* data)
{
    vhpiValueT value;
    vhpiHandleT object = vhpi_handle_by_name("top.u1.w", NULL);
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        printf("#c %llu ns w=?\n", now() / NS);
    } else {
        printf("#c %llu ns w=%d\n", now() / NS, (int)value.value.intg);
    }
    fflush(stdout);
}

// vhpi_create of a driver in the simulation phase (IEEE 1076-2019 23.6): a
// further source of w, at the signal's default value until its transaction.
static void late(const vhpiCbDataT* data)
{
    int scheduled;
    (void)data;
    driver_w_late = vhpi_create(vhpiDriverK, signal_w, NULL);
    scheduled = driver_w_late != NULL
        ? schedule_int(driver_w_late, 1000, 2U * NS)
        : -1;
    printf("#c %llu ns phase=%s late driver=%d schedule=%d\n", now() / NS,
        phase_name(), driver_w_late != NULL, scheduled);
    fflush(stdout);
}

// Twelve more drivers of w in the simulation phase: with the declared three
// and the late one, sixteen sources, more than the resolver programs built
// for a fixed driver count serve (IEEE 1076-2019 23.6, 14.7.3.2). Each holds
// the default 10 until its transaction; driver k then drives k.
static void many(const vhpiCbDataT* data)
{
    int created = 0;
    int failed = 0;
    (void)data;
    for (int index = 1; index <= 12; ++index) {
        vhpiHandleT driver = vhpi_create(vhpiDriverK, signal_w, NULL);
        if (driver == NULL) {
            continue;
        }
        ++created;
        failed |= schedule_int(driver, index, 2U * NS);
    }
    printf("#c %llu ns many drivers=%d schedule=%d\n", now() / NS, created,
        failed);
    fflush(stdout);
}

// The user-resolved array p, as the numbers of its two elements.
static void show_p(const vhpiCbDataT* data)
{
    vhpiValueT value;
    vhpiIntT elements[2] = { 0, 0 };
    vhpiHandleT object = vhpi_handle_by_name("top.u1.p", NULL);
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVecVal;
    value.bufSize = sizeof elements;
    value.value.intgs = elements;
    if (object == NULL || vhpi_get_value(object, &value) != 0) {
        printf("#c %llu ns p=?\n", now() / NS);
    } else {
        printf("#c %llu ns p=%d,%d\n", now() / NS, (int)elements[0],
            (int)elements[1]);
    }
    fflush(stdout);
}

// Ten more sources of p in the simulation phase, each a created process that
// drives both elements: with the declared one, eleven sources, more than the
// resolver programs built for a fixed driver count serve (IEEE 1076-2019
// 23.6, 14.7.3.2). Each holds the default (10, 20) until its transaction;
// source k then drives (k, 100 * k).
static void many_p(const vhpiCbDataT* data)
{
    int created = 0;
    int failed = 0;
    (void)data;
    for (int index = 1; index <= 10; ++index) {
        vhpiHandleT process
            = vhpi_create(vhpiProcessStmtK, foreign_architecture, NULL);
        vhpiHandleT first = process != NULL
            ? vhpi_create(vhpiDriverK, basic_p[0], process)
            : NULL;
        vhpiHandleT second = process != NULL
            ? vhpi_create(vhpiDriverK, basic_p[1], process)
            : NULL;
        if (first == NULL || second == NULL) {
            continue;
        }
        ++created;
        failed |= schedule_int(first, index, 2U * NS)
            | schedule_int(second, 100 * index, 2U * NS);
    }
    printf("#c %llu ns many p sources=%d schedule=%d\n", now() / NS, created,
        failed);
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
    int r;
    int w;
    int p;
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
    // r resolves '1' and '0' to 'X' until the second process drives 'Z' at
    // 12 ns; the driver of no process drives '0' at 22 ns.
    r = schedule_logic(driver_r_first, 3, 0U)
        | schedule_logic(driver_r_second, 2, 0U)
        | schedule_logic(driver_r_second, 4, 12U * NS)
        | schedule_logic(driver_r_none, 4, 0U)
        | schedule_logic(driver_r_none, 2, 22U * NS);
    printf("#c exec schedule y=%d v=%d state=%d r=%d\n", y, v, state, r);
    // w sums its sources, each starting at the default 10: the first source
    // drives 1, the second 2 and then 20 at 12 ns, the driver of no process
    // 100 at 13 ns and 0 at 22 ns; a fourth is created at 15 ns (late), and
    // twelve more at 26 ns (many).
    w = schedule_int(driver_w_first, 1, 0U)
        | schedule_int(driver_w_second, 2, 0U)
        | schedule_int(driver_w_second, 20, 12U * NS)
        | schedule_int(driver_w_none, 100, 13U * NS)
        | schedule_int(driver_w_none, 0, 22U * NS);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 3U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 12U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 13U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, late, 15U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 16U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 18U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 23U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, many, 26U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 27U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 28U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_w, 29U * NS, name);
    printf("#c exec schedule w=%d\n", w);
    // p's declared source drives (1, 3); ten created sources follow at 31 ns
    // (many_p), each drives (k, 100 * k) from 33 ns.
    p = schedule_int(driver_p_first[0], 1, 0U)
        | schedule_int(driver_p_first[1], 3, 0U);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_p, 30U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, many_p, 31U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_p, 32U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_p, 33U * NS, name);
    (void)register_cb(vhpiCbAfterDelay, NULL, show_p, 34U * NS, name);
    printf("#c exec schedule p=%d\n", p);
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

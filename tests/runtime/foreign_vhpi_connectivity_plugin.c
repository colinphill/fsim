// SPDX-License-Identifier: Apache-2.0
// VHPI connectivity conformance plug-in (IEEE 1076-2019 19.12): drivers,
// basic signals, local contributors and local loads, port associations,
// inPort and outPort, and driver callbacks. Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of four buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[4][128];
    static int next;
    const vhpiCharT* value = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 4;
    if (value == NULL) {
        return "-";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffers[0];
         ++index) {
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

// The object a relation leads to, by full name, or "-".
static const char* related(vhpiHandleT object, vhpiOneToOneT relation)
{
    vhpiHandleT target = vhpi_handle(relation, object);
    return target == NULL ? "-" : text_property(target, vhpiFullNameP);
}

// The number of objects of an iteration.
static int count(vhpiOneToManyT relation, vhpiHandleT object)
{
    vhpiHandleT iterator = vhpi_iterator(relation, object);
    int total = 0;
    while (iterator != NULL && vhpi_scan(iterator) != NULL) {
        ++total;
    }
    return total;
}

// A scalar value as a binary string.
static const char* value_of(vhpiHandleT object)
{
    static char buffer[16];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    if (vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

static const char* const signal_names[] = { "top.a", "top.v", "top.c" };
static const char* const process_names[] = { "top.p1", "top.p2", "top._p0" };
static const char* const extra_signal_names[] = { "top.w", "top.q" };
static const char* const extra_process_names[] = {
    "top.p3", "top.p4", "top.p5"
};
static const char* const end_names[] = {
    "top.x", "top.y", "top.u.i", "top.u.o", "top.u.n"
};

// The kind and full name of the object a relation leads to, or "-".
static const char* kind_related(vhpiHandleT object, vhpiOneToOneT relation)
{
    static char buffer[256];
    vhpiHandleT target = vhpi_handle(relation, object);
    if (target == NULL) {
        return "-";
    }
    snprintf(buffer, sizeof buffer, "%s=%s", text_property(target, vhpiKindStrP),
        text_property(target, vhpiFullNameP));
    return buffer;
}

// The structure of a signal: basic signals, their drivers, contributors
// and loads.
static void describe_signal(const char* path)
{
    vhpiHandleT signal = vhpi_handle_by_name(path, NULL);
    vhpiHandleT basics = vhpi_iterator(vhpiBasicSignals, signal);
    vhpiHandleT basic;
    printf("#c %s isbasic=%d drivers=%d\n", path + 4,
        (int)vhpi_get(vhpiIsBasicP, signal), count(vhpiDrivers, signal));
    while (basics != NULL && (basic = vhpi_scan(basics)) != NULL) {
        vhpiHandleT drivers = vhpi_iterator(vhpiDrivers, basic);
        vhpiHandleT driver;
        vhpiHandleT loads = vhpi_iterator(vhpiLocalLoads, basic);
        vhpiHandleT load;
        printf("#c   basic %s isbasic=%d simnet=%s\n",
            text_property(basic, vhpiFullNameP),
            (int)vhpi_get(vhpiIsBasicP, basic),
            related(basic, vhpiSimNet));
        while (drivers != NULL && (driver = vhpi_scan(drivers)) != NULL) {
            printf("#c     driver %s process=%s basic=%s access=%d size=%d "
                   "foreign=%d\n",
                text_property(driver, vhpiKindStrP),
                related(driver, vhpiEqProcessStmt),
                related(driver, DEPRECATED_vhpiBasicSignal),
                (int)vhpi_get(vhpiAccessP, driver),
                (int)vhpi_get(vhpiSizeP, driver),
                (int)vhpi_get(vhpiIsForeignP, driver));
        }
        printf("#c     contributors=%d\n", count(vhpiLocalContributors, basic));
        printf("#c     loads:");
        while (loads != NULL && (load = vhpi_scan(loads)) != NULL) {
            printf(" %s", text_property(load, vhpiFullNameP));
        }
        printf("\n");
    }
}

// The drivers of a process, by the basic signal each drives.
static void describe_process(const char* path)
{
    vhpiHandleT process = vhpi_handle_by_name(path, NULL);
    vhpiHandleT drivers = vhpi_iterator(vhpiDrivers, process);
    vhpiHandleT driver;
    printf("#c process %s drivers:", text_property(process, vhpiFullNameP));
    while (drivers != NULL && (driver = vhpi_scan(drivers)) != NULL) {
        printf(" %s", related(driver, DEPRECATED_vhpiBasicSignal));
    }
    printf("\n");
}

// The current value of every driver.
static void drivers_values(const char* path)
{
    vhpiHandleT signal = vhpi_handle_by_name(path, NULL);
    vhpiHandleT basics = vhpi_iterator(vhpiBasicSignals, signal);
    vhpiHandleT basic;
    while (basics != NULL && (basic = vhpi_scan(basics)) != NULL) {
        vhpiHandleT drivers = vhpi_iterator(vhpiDrivers, basic);
        vhpiHandleT driver;
        while (drivers != NULL && (driver = vhpi_scan(drivers)) != NULL) {
            printf("#c value %s of %s = %s\n", text_property(basic, vhpiFullNameP),
                related(driver, vhpiEqProcessStmt), value_of(driver));
        }
    }
}

// The objects of an iteration: a driver by its process, any other by its
// kind and full name.
static void list(const char* label, vhpiOneToManyT relation, vhpiHandleT object)
{
    vhpiHandleT items = vhpi_iterator(relation, object);
    vhpiHandleT item;
    printf("#c   %s:", label);
    while (items != NULL && (item = vhpi_scan(items)) != NULL) {
        if (vhpi_get(vhpiKindP, item) == vhpiDriverK) {
            printf(" driver[%s]", related(item, vhpiEqProcessStmt));
        } else {
            printf(" %s=%s", text_property(item, vhpiKindStrP),
                text_property(item, vhpiFullNameP));
        }
    }
    printf("\n");
}

// The contributors and loads of each basic signal.
static void describe_ends(const char* path)
{
    vhpiHandleT signal = vhpi_handle_by_name(path, NULL);
    vhpiHandleT basics = vhpi_iterator(vhpiBasicSignals, signal);
    vhpiHandleT basic;
    while (basics != NULL && (basic = vhpi_scan(basics)) != NULL) {
        printf("#c ends %s\n", text_property(basic, vhpiFullNameP));
        list("contributors", vhpiLocalContributors, basic);
        list("loads", vhpiLocalLoads, basic);
    }
}

// The port association elements of an instance.
static void describe_assocs(const char* path)
{
    vhpiHandleT instance = vhpi_handle_by_name(path, NULL);
    vhpiHandleT assocs = vhpi_iterator(vhpiPortAssocs, instance);
    vhpiHandleT assoc;
    printf("#c %s port assocs: %d\n", path + 4,
        count(vhpiPortAssocs, instance));
    while (assocs != NULL && (assoc = vhpi_scan(assocs)) != NULL) {
        printf("#c   assoc position=%d named=%d open=%d formal=%s actual=%s\n",
            (int)vhpi_get(vhpiPositionP, assoc),
            (int)vhpi_get(vhpiIsNamedP, assoc),
            (int)vhpi_get(vhpiIsOpenP, assoc), related(assoc, vhpiFormal),
            related(assoc, vhpiActual));
    }
}

// The mode of a port and its inPort and outPort.
static void describe_port(const char* path)
{
    vhpiHandleT port = vhpi_handle_by_name(path, NULL);
    printf("#c port %s mode=%d in=%s", path + 4, (int)vhpi_get(vhpiModeP, port),
        kind_related(port, vhpiInPort));
    printf(" out=%s\n", kind_related(port, vhpiOutPort));
}

// A driver callback: the driver's process, the time and its value.
static void driver_event(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    printf("#c driver %s of %s at %u fs: %s\n",
        data->reason == vhpiCbTransaction ? "transaction" : "change",
        related(data->obj, vhpiEqProcessStmt), (unsigned)now.low,
        value_of(data->obj));
    fflush(stdout);
}

static void later(const vhpiCbDataT* data)
{
    size_t index;
    (void)data;
    for (index = 0U; index < sizeof signal_names / sizeof signal_names[0];
         ++index) {
        drivers_values(signal_names[index]);
    }
    fflush(stdout);
}

static void ignore(const vhpiCbDataT* data)
{
    (void)data;
}

static void start(const vhpiCbDataT* data)
{
    vhpiCbDataT request;
    vhpiTimeT delay;
    vhpiValueT value;
    vhpiHandleT driver;
    vhpiHandleT drivers;
    size_t index;
    (void)data;
    for (index = 0U; index < sizeof signal_names / sizeof signal_names[0];
         ++index) {
        describe_signal(signal_names[index]);
    }
    for (index = 0U; index < sizeof extra_signal_names / sizeof extra_signal_names[0];
         ++index) {
        describe_signal(extra_signal_names[index]);
    }
    for (index = 0U; index < sizeof process_names / sizeof process_names[0];
         ++index) {
        describe_process(process_names[index]);
    }
    for (index = 0U;
         index < sizeof extra_process_names / sizeof extra_process_names[0];
         ++index) {
        describe_process(extra_process_names[index]);
    }
    describe_assocs("top.u");
    describe_port("top.u.i");
    describe_port("top.u.o");
    describe_port("top.u.n");
    for (index = 0U; index < sizeof end_names / sizeof end_names[0]; ++index) {
        describe_ends(end_names[index]);
    }
    // Callbacks on the driver that top.pd holds on s1.
    drivers = vhpi_iterator(vhpiDrivers, vhpi_handle_by_name("top.s1", NULL));
    driver = drivers != NULL ? vhpi_scan(drivers) : NULL;
    memset(&request, 0, sizeof request);
    request.reason = vhpiCbTransaction;
    request.cb_rtn = driver_event;
    request.obj = driver;
    (void)vhpi_register_cb(&request, 0);
    request.reason = vhpiCbValueChange;
    (void)vhpi_register_cb(&request, 0);
    // The values are read once the first delta cycles have run.
    memset(&request, 0, sizeof request);
    memset(&delay, 0, sizeof delay);
    delay.low = 1000000U;
    request.reason = vhpiCbAfterDelay;
    request.cb_rtn = later;
    request.time = &delay;
    (void)vhpi_register_cb(&request, 0);
    // An ordinary driver cannot be written; a transaction callback takes a
    // driver.
    drivers = vhpi_iterator(vhpiDrivers, vhpi_handle_by_name("top.a", NULL));
    driver = drivers != NULL ? vhpi_scan(drivers) : NULL;
    memset(&request, 0, sizeof request);
    request.reason = vhpiCbTransaction;
    request.cb_rtn = ignore;
    request.obj = driver;
    printf("#c transaction callback on a driver: %s\n",
        vhpi_register_cb(&request, vhpiReturnCb) == NULL ? "refused"
                                                          : "accepted");
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    value.value.enumv = vhpi1;
    printf("#c put_value on a driver: rc=%d\n",
        vhpi_put_value(driver, &value, vhpiDepositPropagate));
    printf("#c error pending: %d\n", vhpi_check_error(NULL) != 0);
    fflush(stdout);
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

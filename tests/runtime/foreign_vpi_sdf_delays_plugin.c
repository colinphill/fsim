// SPDX-License-Identifier: Apache-2.0
// VPI plug-in for the delays that SDF annotation creates: intermodule paths
// reached with vpi_handle_multi, their delays, and the delay devices and
// delay terms on the nets they join (IEEE 1800-2023 37.37, 37.45, 38.22).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static vpiHandle named(const char* name)
{
    return vpi_handle_by_name((PLI_BYTE8*)name, NULL);
}

static vpiHandle port_named(vpiHandle module, const char* name)
{
    vpiHandle iterator = vpi_iterate(vpiPort, module);
    vpiHandle port;
    while (iterator != NULL && (port = vpi_scan(iterator)) != NULL) {
        if (strcmp(text(vpi_get_str(vpiName, port)), name) == 0) {
            vpi_free_object(iterator);
            return port;
        }
    }
    return NULL;
}

static void value_of(const char* label, vpiHandle object)
{
    s_vpi_value value = { vpiBinStrVal, { 0 } };
    if (object == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    vpi_get_value(object, &value);
    vpi_printf("#c %s value=%s\n", label,
        value.format == vpiBinStrVal && value.value.str != NULL
            ? value.value.str
            : "none");
}

// How many of the objects `relation` reaches from `reference` are delay
// terms.
static int delay_terms(PLI_INT32 relation, vpiHandle reference)
{
    vpiHandle iterator = vpi_iterate(relation, reference);
    vpiHandle object;
    int count = 0;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        if (vpi_get(vpiType, object) == vpiDelayTerm) {
            ++count;
        }
    }
    return count;
}

// The nth delay term among what `relation` reaches from `reference`.
static vpiHandle nth_delay_term(
    PLI_INT32 relation, vpiHandle reference, int wanted)
{
    vpiHandle iterator = vpi_iterate(relation, reference);
    vpiHandle object;
    int count = 0;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        if (vpi_get(vpiType, object) == vpiDelayTerm && count++ == wanted) {
            vpi_free_object(iterator);
            return object;
        }
    }
    return NULL;
}

static int count_of(PLI_INT32 relation, vpiHandle reference)
{
    vpiHandle iterator = vpi_iterate(relation, reference);
    int count = 0;
    while (iterator != NULL && vpi_scan(iterator) != NULL) {
        ++count;
    }
    return count;
}

// The value is read only for an input term, which changes before the delay
// (IEEE 1800-2023 37.45 detail 1).
static void show_term(const char* label, vpiHandle term, int with_value)
{
    vpiHandle device;
    vpiHandle module;
    if (term == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    vpi_printf("#c %s type=%s delaytype=%d\n", label,
        text(vpi_get_str(vpiType, term)), (int)vpi_get(vpiDelayType, term));
    if (with_value) {
        value_of(label, term);
    }
    device = vpi_handle(vpiParent, term);
    if (device == NULL) {
        vpi_printf("#c %s device missing\n", label);
        return;
    }
    module = vpi_handle(vpiModule, device);
    vpi_printf("#c %s device type=%s delaytype=%d module=%s in=%d out=%d\n",
        label, text(vpi_get_str(vpiType, device)),
        (int)vpi_get(vpiDelayType, device),
        module != NULL ? text(vpi_get_str(vpiFullName, module)) : "-",
        count_of(vpiInTerm, device), count_of(vpiOutTerm, device));
}

static void delays(const char* label, vpiHandle path, int count, int mtm,
    int pulse, int type)
{
    s_vpi_time da[18];
    s_vpi_delay request = { da, count, type, mtm, 0, pulse };
    int total = count * (mtm ? 3 : 1) * (pulse ? 3 : 1);
    int index;
    memset(da, 0, sizeof da);
    vpi_get_delays(path, &request);
    vpi_printf("#c %s", label);
    for (index = 0; index < total; ++index) {
        if (type == vpiSimTime) {
            vpi_printf(" %u", (unsigned)da[index].low);
        } else {
            vpi_printf(" %.3f", da[index].real);
        }
    }
    vpi_printf("\n");
}

static void put(const char* label, vpiHandle path, double first, double second,
    int append)
{
    s_vpi_time da[2];
    s_vpi_delay request = { da, 2, vpiScaledRealTime, 0, append, 0 };
    memset(da, 0, sizeof da);
    da[0].type = vpiScaledRealTime;
    da[0].real = first;
    da[1].type = vpiScaledRealTime;
    da[1].real = second;
    vpi_put_delays(path, &request);
    delays(label, path, 3, 0, 0, vpiScaledRealTime);
}

// --- the load and the source of a delayed net (IEEE 1364-2005 16.2.4) -------
//
// The load port, and what lies at or below its instance, shows the delayed
// value and reports its changes late; the source side shows the net at once.

static void show(const char* name)
{
    s_vpi_value value = { vpiBinStrVal, { 0 } };
    vpiHandle object = named(name);
    if (object == NULL) {
        vpi_printf(" %s=missing", name);
        return;
    }
    vpi_get_value(object, &value);
    vpi_printf(" %s=%s", name, value.value.str != NULL ? value.value.str : "none");
}

// The low or high connection of the port `a` of top.u1, as a value.
static void show_connection(const char* label, PLI_INT32 relation)
{
    s_vpi_value value = { vpiBinStrVal, { 0 } };
    vpiHandle port = port_named(named("top.u1"), "a");
    vpiHandle object = port != NULL ? vpi_handle(relation, port) : NULL;
    if (object == NULL) {
        vpi_printf(" %s=missing", label);
        return;
    }
    vpi_get_value(object, &value);
    vpi_printf(" %s=%s", label, value.value.str != NULL ? value.value.str : "none");
}

static void begin_sample(void)
{
    s_vpi_time now = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &now);
    vpi_printf("#c at %u", (unsigned)now.low);
}

// The net of the INTERCONNECT u0.z to u1.a (which a PORT delay and the
// plug-in's puts retime): the source ports read the net, the load port and its
// low connection the delayed copy, its high connection the net.
static PLI_INT32 sample_interconnect(p_cb_data data)
{
    (void)data;
    begin_sample();
    show("top.w");
    show("top.u0.z");
    show("top.u1.a");
    show_connection("low", vpiLowConn);
    show_connection("high", vpiHighConn);
    vpi_printf("\n");
    return 0;
}

// The same for the INTERCONNECT u10.z to u11.a, whose changes the plug-in
// also watches.
static PLI_INT32 sample_watched(p_cb_data data)
{
    (void)data;
    begin_sample();
    show("top.w5");
    show("top.u11.a");
    vpi_printf("\n");
    return 0;
}

// A PORT delay on a port of the design itself: the plug-in drives the port,
// so the port object keeps what was driven while the instance below the
// design reads the delayed copy.
static PLI_INT32 sample_top_port(p_cb_data data)
{
    (void)data;
    begin_sample();
    show("top.tin");
    show("top.u9.a");
    vpi_printf("\n");
    return 0;
}

static PLI_INT32 changed(p_cb_data data)
{
    s_vpi_time now = { vpiSimTime, 0, 0, 0.0 };
    s_vpi_value value = { vpiBinStrVal, { 0 } };
    vpi_get_value(data->obj, &value);
    vpi_get_time(NULL, &now);
    vpi_printf("#c vcb %s=%s t=%u\n", (const char*)data->user_data,
        value.value.str != NULL ? value.value.str : "none", (unsigned)now.low);
    return 0;
}

// A value-change callback on the object `name`; the load's fires when the
// delayed copy changes, the source's with the net.
static void watch(const char* name)
{
    static s_vpi_time times[4];
    static s_vpi_value values[4];
    static int used;
    s_cb_data callback;
    vpiHandle object = named(name);
    if (object == NULL || used == 4) {
        vpi_printf("#c vcb %s missing\n", name);
        return;
    }
    times[used].type = vpiSuppressTime;
    values[used].format = vpiSuppressVal;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbValueChange;
    callback.cb_rtn = changed;
    callback.obj = object;
    callback.time = &times[used];
    callback.value = &values[used];
    callback.user_data = (PLI_BYTE8*)name;
    ++used;
    vpi_register_cb(&callback);
}

// The routine runs `ticks` simulation ticks after time zero.
static void at(PLI_UINT32 ticks, PLI_INT32 (*routine)(p_cb_data))
{
    static s_vpi_time times[32];
    static int used;
    s_cb_data callback;
    if (used == 32) {
        return;
    }
    times[used].type = vpiSimTime;
    times[used].high = 0;
    times[used].low = ticks;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = routine;
    callback.time = &times[used++];
    vpi_register_cb(&callback);
}

static void drive_top_port(PLI_INT32 bit)
{
    s_vpi_value value;
    vpiHandle object = named("top.tin");
    if (object == NULL) {
        return;
    }
    value.format = vpiIntVal;
    value.value.integer = bit;
    vpi_put_value(object, &value, NULL, vpiNoDelay);
}

// A delay put on bit 1 of the port a of u15, which two INTERCONNECT records
// name (the first all four bits, the second bit 1 alone, both with 4 ns): the
// put retimes the bit the second record owns and no other (IEEE 1800-2023
// 38.32; the run of bits that record owns is its own copy driver).
static PLI_INT32 put_bit_one(p_cb_data data)
{
    s_vpi_time da[2];
    s_vpi_delay request = { da, 2, vpiScaledRealTime, 0, 0, 0 };
    vpiHandle module = named("top.u15");
    vpiHandle ports = module != NULL ? vpi_iterate(vpiPort, module) : NULL;
    vpiHandle port = ports != NULL ? vpi_scan(ports) : NULL;
    vpiHandle bits = port != NULL ? vpi_iterate(vpiBit, port) : NULL;
    vpiHandle bit;
    int position = 0;
    (void)data;
    /* The bits of a port come from the left: 3, 2, 1, 0 for a[3:0], so bit 1
       is the third (a port bit has no vpiIndex). */
    while (bits != NULL && (bit = vpi_scan(bits)) != NULL) {
        if (position++ != 2) {
            continue;
        }
        memset(da, 0, sizeof da);
        da[0].type = vpiScaledRealTime;
        da[0].real = 9.0;
        da[1].type = vpiScaledRealTime;
        da[1].real = 9.0;
        vpi_put_delays(bit, &request);
        memset(da, 0, sizeof da);
        vpi_get_delays(bit, &request);
        vpi_printf("#c put bit1 %.3f %.3f\n", da[0].real, da[1].real);
        vpi_free_object(bits);
        return 0;
    }
    vpi_printf("#c put bit1 missing\n");
    return 0;
}

// A delay put on the INTERCONNECT path of a net whose group copies differ:
// the path retimes the runs of bits its record owns (0 and 2) and not bit 1,
// which a PORT record owns (IEEE 1364-2005 16.3, 16.2.4).
static PLI_INT32 put_group_path(p_cb_data data)
{
    vpiHandle z = port_named(named("top.v2"), "z");
    vpiHandle a = port_named(named("top.pl"), "a");
    vpiHandle path = vpi_handle_multi(vpiInterModPath, z, a);
    (void)data;
    if (path == NULL) {
        vpi_printf("#c group put missing\n");
        return 0;
    }
    put("group put", path, 9.0, 9.0, 0);
    return 0;
}

static PLI_INT32 drive_high(p_cb_data data)
{
    (void)data;
    drive_top_port(1);
    return 0;
}

static PLI_INT32 drive_low(p_cb_data data)
{
    (void)data;
    drive_top_port(0);
    return 0;
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle z;
    vpiHandle a;
    vpiHandle path;
    vpiHandle iterator;
    vpiHandle port;
    (void)data;

    // The delay terms on the nets of the annotated ports; a net that no SDF
    // delay names has none (IEEE 1800-2023 37.45, 37.46).
    vpi_printf("#c u0.z drivers=%d loads=%d\n",
        delay_terms(vpiDriver, named("top.u0.z")),
        delay_terms(vpiLoad, named("top.u0.z")));
    vpi_printf("#c u1.a drivers=%d loads=%d\n",
        delay_terms(vpiDriver, named("top.u1.a")),
        delay_terms(vpiLoad, named("top.u1.a")));
    vpi_printf("#c unannotated drivers=%d loads=%d\n",
        delay_terms(vpiDriver, named("top.a")),
        delay_terms(vpiLoad, named("top.a")));

    // The INTERCONNECT delay: its input term loads the source net, its output
    // term drives the destination net.
    show_term("ic in", nth_delay_term(vpiLoad, named("top.u0.z"), 0), 1);
    show_term("ic out", nth_delay_term(vpiDriver, named("top.u1.a"), 0), 0);
    vpi_printf("#c ic in drivers=%d loads=%d\n",
        delay_terms(vpiDriver, nth_delay_term(vpiLoad, named("top.u0.z"), 0)),
        delay_terms(vpiLoad, nth_delay_term(vpiLoad, named("top.u0.z"), 0)));
    vpi_printf("#c ic out loads=%d drivers=%d\n",
        delay_terms(vpiLoad, nth_delay_term(vpiDriver, named("top.u1.a"), 0)),
        delay_terms(vpiDriver, nth_delay_term(vpiDriver, named("top.u1.a"), 0)));

    // The PORT delay is a module input port delay.
    show_term("port in", nth_delay_term(vpiLoad, named("top.u1.a"), 0), 0);
    show_term("port out", nth_delay_term(vpiDriver, named("top.u1.a"), 1), 0);

    // vpi_handle_multi gives the intermodule path of an output and an input
    // port (IEEE 1800-2023 37.37 detail 1, 38.22).
    z = port_named(named("top.u0"), "z");
    a = port_named(named("top.u1"), "a");
    path = vpi_handle_multi(vpiInterModPath, z, a);
    if (path == NULL) {
        vpi_printf("#c multi missing\n");
    } else {
        vpi_printf("#c multi type=%s ports=%d\n", text(vpi_get_str(vpiType, path)),
            count_of(vpiPort, path));
        iterator = vpi_iterate(vpiPort, path);
        while (iterator != NULL && (port = vpi_scan(iterator)) != NULL) {
            vpi_printf("#c multi port=%s dir=%d\n", text(vpi_get_str(vpiName, port)),
                (int)vpi_get(vpiDirection, port));
        }
        delays("multi scaled", path, 3, 0, 0, vpiScaledRealTime);
        delays("multi simtime", path, 3, 0, 0, vpiSimTime);
        delays("multi two", path, 2, 0, 0, vpiScaledRealTime);
        delays("multi mtm", path, 2, 1, 0, vpiScaledRealTime);
        delays("multi pulse", path, 3, 0, 1, vpiScaledRealTime);
        put("multi put", path, 5.0, 6.0, 0);
        put("multi append", path, 1.0, 1.0, 1);
    }

    // Only an output port and an input port with an annotated delay between
    // them have a path.
    vpi_printf("#c multi swapped=%s\n",
        vpi_handle_multi(vpiInterModPath, a, z) == NULL ? "none" : "found");
    vpi_printf("#c multi unannotated=%s\n",
        vpi_handle_multi(vpiInterModPath, port_named(named("top.u1"), "z"),
            port_named(named("top.u0"), "a"))
                == NULL
            ? "none"
            : "found");
    vpi_printf("#c multi other type=%s\n",
        vpi_handle_multi(vpiModule, z, a) == NULL ? "none" : "found");
    return 0;
}

// The value changes of the source and of the load of an INTERCONNECT delay,
// watched once the start-up transitions of the delayed copy have settled
// (the simulation time is in picoseconds).
static PLI_INT32 start_watching(p_cb_data data)
{
    (void)data;
    watch("top.w5");
    watch("top.u11.a");
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = check;
    callback.time = &time;
    vpi_register_cb(&callback);
    // Samples between the changes of the delayed nets.
    at(5000, start_watching);
    at(23500, sample_interconnect);
    at(30000, sample_interconnect);
    at(43500, sample_interconnect);
    at(49000, sample_interconnect);
    at(11000, sample_watched);
    at(15000, sample_watched);
    at(26500, sample_watched);
    at(30500, sample_watched);
    at(56000, drive_high);
    at(57500, sample_top_port);
    at(60500, sample_top_port);
    at(62000, drive_low);
    at(64000, sample_top_port);
    at(68500, sample_top_port);
    at(470000, put_bit_one);
    at(590000, put_group_path);
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

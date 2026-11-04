// SPDX-License-Identifier: Apache-2.0
// VPI plug-in for the delays that SDF annotation creates: intermodule paths
// reached with vpi_handle_multi, their delays, and the delay devices and
// delay terms on the nets they join (IEEE 1800-2023 37.37, 37.45, 38.22).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
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

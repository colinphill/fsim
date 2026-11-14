// SPDX-License-Identifier: Apache-2.0
// Gate instance arrays with parameter bounds (IEEE 1800-2023 28.3.6, 37.35):
// at the end of the run, each array of two cells with its range, size and
// elements in order, each with its index and value, and the nets the arrays
// drive. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "none";
}

static int number(vpiHandle object)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    if (object == NULL) {
        return -999;
    }
    vpi_get_value(object, &value);
    return value.value.integer;
}

static void value_line(const char* label, const char* name)
{
    vpiHandle object = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    s_vpi_value value = { vpiBinStrVal, { 0 } };
    if (object != NULL) {
        vpi_get_value(object, &value);
    }
    vpi_printf("#c %s %s\n", label, object != NULL ? text(value.value.str) : "missing");
}

/* The rise and fall delays of an element and the primitive terms that drive
   the net of its output (IEEE 1800-2023 37.35, 37.21, 38.8). */
static void delays_of(const char* instance_name, const char* element_name,
    vpiHandle element)
{
    s_vpi_delay delay;
    s_vpi_time times[2];
    vpiHandle terms = vpi_iterate(vpiPrimTerm, element);
    vpiHandle term = terms != NULL ? vpi_scan(terms) : NULL;
    vpiHandle expr = term != NULL ? vpi_handle(vpiExpr, term) : NULL;
    vpiHandle drivers = expr != NULL ? vpi_iterate(vpiDriver, expr) : NULL;
    int driver_count = 0;
    memset(&delay, 0, sizeof delay);
    memset(times, 0, sizeof times);
    delay.da = times;
    delay.no_of_delays = 2;
    delay.time_type = vpiScaledRealTime;
    vpi_get_delays(element, &delay);
    while (drivers != NULL && vpi_scan(drivers) != NULL) {
        ++driver_count;
    }
    vpi_printf("#c %s %s delays rise=%g fall=%g output-drivers=%d\n",
        instance_name, element_name, times[0].real, times[1].real, driver_count);
}

static void gcell_report(const char* instance_name)
{
    vpiHandle instance = vpi_handle_by_name((PLI_BYTE8*)instance_name, NULL);
    vpiHandle arrays = instance != NULL ? vpi_iterate(vpiPrimitiveArray, instance) : NULL;
    vpiHandle scopes = instance != NULL ? vpi_iterate(vpiInternalScope, instance) : NULL;
    vpiHandle array;
    int scope_count = 0;
    while (scopes != NULL && vpi_scan(scopes) != NULL) {
        ++scope_count;
    }
    vpi_printf("#c %s internal scopes=%d\n", instance_name, scope_count);
    while (arrays != NULL && (array = vpi_scan(arrays)) != NULL) {
        vpiHandle elements = vpi_iterate(vpiPrimitive, array);
        vpiHandle element;
        vpi_printf("#c %s array %s type=%d size=%d left=%d right=%d\n",
            instance_name, text(vpi_get_str(vpiName, array)),
            (int)vpi_get(vpiType, array), (int)vpi_get(vpiSize, array),
            number(vpi_handle(vpiLeftRange, array)),
            number(vpi_handle(vpiRightRange, array)));
        while (elements != NULL && (element = vpi_scan(elements)) != NULL) {
            s_vpi_value value = { vpiBinStrVal, { 0 } };
            vpi_get_value(element, &value);
            delays_of(instance_name, text(vpi_get_str(vpiFullName, element)),
                element);
            vpi_printf("#c %s element %s index=%d value=%s parent=%s\n",
                instance_name, text(vpi_get_str(vpiFullName, element)),
                number(vpi_handle(vpiIndex, element)), text(value.value.str),
                text(vpi_get_str(vpiName, vpi_handle(vpiParent, element))));
        }
    }
}

static PLI_INT32 report(p_cb_data data)
{
    (void)data;
    gcell_report("top.c4");
    gcell_report("top.c3");
    gcell_report("top.e");
    gcell_report("top.blk[0]");
    gcell_report("top.blk[1]");
    value_line("up4", "top.up4");
    value_line("down4", "top.down4");
    value_line("up3", "top.up3");
    value_line("down3", "top.down3");
    value_line("slow4", "top.slow4");
    value_line("slow3", "top.slow3");
    value_line("ey", "top.ey");
    return 0;
}

/* The delays written to an element reach its driver: the output waits for
   the written delay (IEEE 1800-2023 37.35, 38.8). */
static PLI_INT32 sample(p_cb_data data)
{
    (void)data;
    value_line("slow4 sampled", "top.slow4");
    return 0;
}

static PLI_INT32 lengthen(p_cb_data data)
{
    static s_vpi_time at_15 = { vpiSimTime, 0, 15, 0.0 };
    static s_vpi_time at_25 = { vpiSimTime, 0, 25, 0.0 };
    s_cb_data later;
    vpiHandle element = vpi_handle_by_name((PLI_BYTE8*)"top.c4.buf_slow[0]", NULL);
    s_vpi_delay delay;
    s_vpi_time times[2];
    (void)data;
    memset(&delay, 0, sizeof delay);
    memset(times, 0, sizeof times);
    times[0].real = 8.0;
    times[1].real = 9.0;
    delay.da = times;
    delay.no_of_delays = 2;
    delay.time_type = vpiScaledRealTime;
    if (element != NULL) {
        vpi_put_delays(element, &delay);
    }
    memset(&later, 0, sizeof later);
    later.reason = cbAfterDelay;
    later.cb_rtn = sample;
    later.time = &at_15;
    (void)vpi_register_cb(&later);
    later.time = &at_25;
    (void)vpi_register_cb(&later);
    return 0;
}


static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbEndOfSimulation;
    data.cb_rtn = report;
    (void)vpi_register_cb(&data);
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = lengthen;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

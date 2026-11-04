// SPDX-License-Identifier: Apache-2.0
// VPI delay conformance plug-in (IEEE 1800-2023 38.10, 38.32, Tables 38-2
// and 38-4): vpi_get_delays and vpi_put_delays on gates, a UDP instance,
// continuous assignments, a module path and a timing check in each layout of
// the delay array, and the output times a put changes. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static vpiHandle named(const char* name)
{
    return vpi_handle_by_name((PLI_BYTE8*)name, NULL);
}

static vpiHandle first_of(PLI_INT32 type, vpiHandle scope)
{
    vpiHandle iterator = vpi_iterate(type, scope);
    return iterator == NULL ? NULL : vpi_scan(iterator);
}

// vpi_get_delays in the layout the flags choose, printed in da order.
static void show(const char* label, vpiHandle object, int count, int mtm,
    int pulsere, int time_type)
{
    s_vpi_time times[36];
    s_vpi_delay delays;
    int total = count * (mtm ? 3 : 1) * (pulsere ? 3 : 1);
    int index;
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = count;
    delays.time_type = time_type;
    delays.mtm_flag = mtm;
    delays.append_flag = 0;
    delays.pulsere_flag = pulsere;
    vpi_get_delays(object, &delays);
    if (vpi_chk_error(NULL) != 0) {
        vpi_printf("#c %s error\n", label);
        return;
    }
    vpi_printf("#c %s", label);
    for (index = 0; index < total; ++index) {
        if (time_type == vpiSuppressTime) {
            vpi_printf(" type=%d", (int)times[index].type);
        } else if (time_type == vpiSimTime) {
            vpi_printf(" %u", (unsigned)times[index].low);
        } else {
            vpi_printf(" %.3f", times[index].real);
        }
    }
    vpi_printf("\n");
}

// vpi_put_delays of scaled real values; returns whether it succeeded.
static int put(vpiHandle object, int count, int mtm, int append, int pulsere,
    const double* values)
{
    s_vpi_time times[36];
    s_vpi_delay delays;
    int total = count * (mtm ? 3 : 1) * (pulsere ? 3 : 1);
    int index;
    memset(times, 0, sizeof times);
    for (index = 0; index < total; ++index) {
        times[index].type = vpiScaledRealTime;
        times[index].real = values[index];
    }
    delays.da = times;
    delays.no_of_delays = count;
    delays.time_type = vpiScaledRealTime;
    delays.mtm_flag = mtm;
    delays.append_flag = append;
    delays.pulsere_flag = pulsere;
    vpi_put_delays(object, &delays);
    return vpi_chk_error(NULL) == 0;
}

static PLI_INT32 changed(p_cb_data data)
{
    // The values before 10 ns are the start-up transitions.
    if (data->time->low >= 10000U) {
        vpi_printf("#c change %s %u %s\n", data->user_data,
            (unsigned)data->time->low, data->value->value.str);
    }
    return 0;
}

static void watch(const char* name)
{
    static s_vpi_time times[8];
    static s_vpi_value values[8];
    static int used;
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    times[used].type = vpiSimTime;
    values[used].format = vpiBinStrVal;
    callback.reason = cbValueChange;
    callback.cb_rtn = changed;
    callback.obj = named(name);
    callback.time = &times[used];
    callback.value = &values[used];
    callback.user_data = (PLI_BYTE8*)(name + 4);
    vpi_register_cb(&callback);
    ++used;
}

static void report(void)
{
    vpiHandle top = named("top");
    vpiHandle iterator;
    vpiHandle assign;
    vpiHandle path = first_of(vpiModPath, named("top.p"));
    vpiHandle check = first_of(vpiTchk, named("top.p"));

    show("g_and get 2", named("top.g_and"), 2, 0, 0, vpiScaledRealTime);
    show("g_and get 3", named("top.g_and"), 3, 0, 0, vpiScaledRealTime);
    show("g_and get 3 mtm", named("top.g_and"), 3, 1, 0, vpiScaledRealTime);
    show("g_and get 2 sim", named("top.g_and"), 2, 0, 0, vpiSimTime);
    show("g_and get 2 suppress", named("top.g_and"), 2, 0, 0, vpiSuppressTime);
    show("g_and get 1", named("top.g_and"), 1, 0, 0, vpiScaledRealTime);
    show("g_and get 2 pulsere", named("top.g_and"), 2, 0, 1, vpiScaledRealTime);
    show("g_buf get 3", named("top.g_buf"), 3, 0, 0, vpiScaledRealTime);
    show("g_buf get 3 mtm", named("top.g_buf"), 3, 1, 0, vpiScaledRealTime);
    show("u_inv get 2", named("top.u_inv"), 2, 0, 0, vpiScaledRealTime);
    iterator = vpi_iterate(vpiContAssign, top);
    while (iterator != NULL && (assign = vpi_scan(iterator)) != NULL) {
        show("assign get 3", assign, 3, 0, 0, vpiScaledRealTime);
    }
    show("path get 2", path, 2, 0, 0, vpiScaledRealTime);
    show("path get 2 pulsere", path, 2, 0, 1, vpiScaledRealTime);
    show("path get 1 mtm pulsere", path, 1, 1, 1, vpiScaledRealTime);
    show("path get 3 mtm", path, 3, 1, 0, vpiScaledRealTime);
    show("path get 6", path, 6, 0, 0, vpiScaledRealTime);
    show("path get 12 sim", path, 12, 0, 0, vpiSimTime);
    show("path get 4", path, 4, 0, 0, vpiScaledRealTime);
    show("setup get 1", check, 1, 0, 0, vpiScaledRealTime);
    show("setup get 2", check, 2, 0, 0, vpiScaledRealTime);
    watch("top.y_and");
    watch("top.y_udp");
    watch("top.w_assign");
    watch("top.w_path");
}

// At 25 ns, between the second and third stimulus edges.
static PLI_INT32 change_delays(p_cb_data data)
{
    static const double and_delays[] = { 10, 20 };
    static const double buf_mtm[] = { 10, 11, 12, 20, 21, 22, 30, 31, 32 };
    static const double inv_append[] = { 1, 1 };
    static const double path_pulse[] = { 9, 3, 4, 11, 5, 6 };
    static const double limit[] = { 12 };
    static const double negative[] = { -1, 2 };
    vpiHandle top = named("top");
    vpiHandle path = first_of(vpiModPath, named("top.p"));
    vpiHandle assign = first_of(vpiContAssign, top);
    (void)data;
    vpi_printf("#c put g_and %d\n", put(named("top.g_and"), 2, 0, 0, 0, and_delays));
    show("g_and get 3", named("top.g_and"), 3, 0, 0, vpiScaledRealTime);
    vpi_printf("#c put g_buf mtm %d\n", put(named("top.g_buf"), 3, 1, 0, 0, buf_mtm));
    show("g_buf get 3 mtm", named("top.g_buf"), 3, 1, 0, vpiScaledRealTime);
    show("g_buf get 3", named("top.g_buf"), 3, 0, 0, vpiScaledRealTime);
    vpi_printf("#c append u_inv %d\n", put(named("top.u_inv"), 2, 0, 1, 0, inv_append));
    show("u_inv get 2", named("top.u_inv"), 2, 0, 0, vpiScaledRealTime);
    vpi_printf("#c put path pulsere %d\n", put(path, 2, 0, 0, 1, path_pulse));
    show("path get 2 pulsere", path, 2, 0, 1, vpiScaledRealTime);
    show("path get 2", path, 2, 0, 0, vpiScaledRealTime);
    vpi_printf("#c put setup %d\n", put(first_of(vpiTchk, named("top.p")), 1, 0, 0, 0, limit));
    show("setup get 1", first_of(vpiTchk, named("top.p")), 1, 0, 0, vpiScaledRealTime);
    vpi_printf("#c put negative %d\n", put(named("top.g_and"), 2, 0, 0, 0, negative));
    vpi_printf("#c put pulsere gate %d\n", put(named("top.g_and"), 2, 0, 0, 1, path_pulse));
    vpi_printf("#c put assign %d\n", put(assign, 2, 0, 0, 0, and_delays));
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 25000, 0.0 };
    (void)data;
    report();
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = change_delays;
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

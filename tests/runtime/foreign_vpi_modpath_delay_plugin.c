// SPDX-License-Identifier: Apache-2.0
// VPI plug-in for the module path delays of specparam triples (IEEE
// 1800-2023 30.4.1, 38.10), the delay devices of module paths (37.45, 37.46)
// and vpi_put_delays on a tranif (38.32, 28.16). Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static const char* name_of(vpiHandle handle)
{
    const char* name;
    if (handle == NULL) {
        return "NULL";
    }
    name = vpi_get_str(vpiFullName, handle);
    return name == NULL ? "?" : name;
}

// The first two delays of a path with minimum, typical and maximum values.
static void print_path(int index, vpiHandle path)
{
    s_vpi_time times[6];
    s_vpi_delay delays;
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = 2;
    delays.time_type = vpiSimTime;
    delays.mtm_flag = 1;
    delays.append_flag = 0;
    delays.pulsere_flag = 0;
    vpi_get_delays(path, &delays);
    vpi_printf("#c path %d rise %u,%u,%u fall %u,%u,%u\n", index,
        (unsigned)times[0].low, (unsigned)times[1].low, (unsigned)times[2].low,
        (unsigned)times[3].low, (unsigned)times[4].low,
        (unsigned)times[5].low);
}

static int count_of(PLI_INT32 relation, vpiHandle object)
{
    vpiHandle iterator = vpi_iterate(relation, object);
    int count = 0;
    while (iterator != NULL && vpi_scan(iterator) != NULL) {
        ++count;
    }
    return count;
}

// The delay terms on a net that belong to module paths, with their device.
static void print_terms(const char* label, PLI_INT32 relation, const char* path)
{
    vpiHandle net = vpi_handle_by_name((PLI_BYTE8*)path, NULL);
    vpiHandle iterator = net == NULL ? NULL : vpi_iterate(relation, net);
    vpiHandle item;
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        vpiHandle device;
        if (vpi_get(vpiType, item) != vpiDelayTerm) {
            continue;
        }
        device = vpi_handle(vpiParent, item);
        vpi_printf("#c %s %s term type=%s delaytype=%d device=%s "
                   "devicetype=%d module=%s parent=%s in=%d out=%d\n",
            label, path, vpi_get_str(vpiType, item),
            (int)vpi_get(vpiDelayType, item),
            vpi_get_str(vpiType, device), (int)vpi_get(vpiDelayType, device),
            name_of(vpi_handle(vpiModule, device)),
            name_of(vpi_handle(vpiParent, device)),
            count_of(vpiInTerm, device), count_of(vpiOutTerm, device));
    }
}

static void print_tranif(const char* name)
{
    vpiHandle gate = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    s_vpi_time times[2];
    s_vpi_delay delays;
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = 2;
    delays.time_type = vpiSimTime;
    delays.mtm_flag = 0;
    delays.append_flag = 0;
    delays.pulsere_flag = 0;
    if (gate == NULL) {
        vpi_printf("#c no %s\n", name);
        return;
    }
    times[0].type = vpiSimTime;
    times[0].low = 3;
    times[1].type = vpiSimTime;
    times[1].low = 7;
    vpi_put_delays(gate, &delays);
    memset(times, 0, sizeof times);
    vpi_get_delays(gate, &delays);
    vpi_printf("#c put %s turn-on/turn-off %u,%u\n", name,
        (unsigned)times[0].low, (unsigned)times[1].low);
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    vpiHandle module = vpi_handle_by_name((PLI_BYTE8*)"top.u", NULL);
    vpiHandle iterator;
    vpiHandle item;
    int index = 0;
    (void)data;
    if (module == NULL) {
        vpi_printf("#c no top.u\n");
        return 0;
    }
    iterator = vpi_iterate(vpiModPath, module);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        print_path(index++, item);
    }
    print_terms("driver", vpiDriver, "top.u.y");
    print_terms("load", vpiLoad, "top.u.c");
    print_terms("driver", vpiDriver, "top.u.z");
    // The put changes x1's turn-off delay to 7 ns for the transitions that
    // follow; x0 keeps 3 and 6.
    print_tranif("top.x1");
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start_of_simulation;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

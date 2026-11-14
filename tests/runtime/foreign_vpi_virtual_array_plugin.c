// SPDX-License-Identifier: Apache-2.0
// VPI conformance plug-in for an array of virtual interfaces (IEEE 1800-2023
// 37.29 example 1): the array var, its elements as virtual interface
// variables with vpiActual and vpiTypespec, and the vpiVirtualInterfaceVar
// and vpiVariables iterations of the module. Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static void element(vpiHandle array, int index)
{
    vpiHandle object = vpi_handle_by_index(array, index);
    vpiHandle actual;
    vpiHandle spec;
    if (object == NULL) {
        vpi_printf("#c missing element %d\n", index);
        return;
    }
    actual = vpi_handle(vpiActual, object);
    spec = vpi_handle(vpiTypespec, object);
    vpi_printf("#c %s type=%s name=%s modport=%d actual=%s defname=%s\n",
        text(vpi_get_str(vpiName, object)), text(vpi_get_str(vpiType, object)),
        text(vpi_get_str(vpiName, object)), (int)vpi_get(vpiIsModPort, object),
        actual != NULL ? text(vpi_get_str(vpiFullName, actual)) : "-",
        spec != NULL ? text(vpi_get_str(vpiDefName, spec)) : "-");
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle array = vpi_handle_by_name("top.va", NULL);
    vpiHandle iterator;
    vpiHandle object;
    int count = 0;
    (void)data;
    vpi_printf("#c va type=%s size=%d\n", text(vpi_get_str(vpiType, array)),
        (int)vpi_get(vpiSize, array));
    element(array, 0);
    element(array, 1);
    iterator = vpi_iterate(vpiVirtualInterfaceVar, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c top virtual %s\n", text(vpi_get_str(vpiName, object)));
    }
    iterator = vpi_iterate(vpiVariables, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        ++count;
    }
    vpi_printf("#c top variables=%d\n", count);
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

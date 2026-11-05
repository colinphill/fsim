// SPDX-License-Identifier: Apache-2.0
// VHPI standard direct binding conformance plug-in (IEEE 1076-2019
// 20.2.4.3): it registers no model; the design's FOREIGN attributes name
// the entry points below, which the tool binds by name. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* phase_name(void)
{
    static const char* const names[] = { "", "registration", "analysis",
        "elaboration", "initialization", "simulation", "termination" };
    const vhpiIntT current = vhpi_get(vhpiPhaseP, NULL);
    return current >= 0 && current <= 6 ? names[current] : "?";
}

static void put_integer(vhpiHandleT target, int number)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = number;
    (void)vhpi_put_value(target, &value, vhpiDeposit);
}

// The elaboration and execution functions of the architecture
// (IEEE 1076-2019 20.4.1, 20.5).
FSIM_VHPI_EXPORT void direct_elab(const vhpiCbDataT* data)
{
    printf("#c elab %s phase=%s\n",
        (const char*)vhpi_get_str(vhpiFullNameP, data->obj), phase_name());
    fflush(stdout);
}

FSIM_VHPI_EXPORT void direct_exec(const vhpiCbDataT* data)
{
    printf("#c exec %s phase=%s\n",
        (const char*)vhpi_get_str(vhpiFullNameP, data->obj), phase_name());
    fflush(stdout);
}

// A null execution specifier names the function after the subprogram
// designator, with the letter case of its declaration.
FSIM_VHPI_EXPORT void Answer(const vhpiCbDataT* data)
{
    put_integer(data->obj, 42);
}

FSIM_VHPI_EXPORT void direct_seven(const vhpiCbDataT* data)
{
    put_integer(data->obj, 7);
}

// The models the tool bound for the design's direct bindings are the
// registered foreign models, library and model name included.
static void on_tool(const vhpiCbDataT* data)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiForeignfs, NULL);
    vhpiHandleT item;
    vhpiForeignDataT info;
    (void)data;
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        memset(&info, 0, sizeof info);
        if (vhpi_get_foreignf_info(item, &info) == 0) {
            printf("#c foreignf %s %s kind=%d\n", info.libraryName,
                info.modelName, (int)info.kind);
        }
    }
    fflush(stdout);
}

static void on_end(const vhpiCbDataT* data)
{
    vhpiValueT value;
    vhpiHandleT result = vhpi_handle_by_name("top.r", NULL);
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    (void)vhpi_get_value(result, &value);
    printf("#c end r=%d\n", (int)value.value.intg);
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfTool;
    data.cb_rtn = on_tool;
    (void)vhpi_register_cb(&data, 0);
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbEndOfSimulation;
    data.cb_rtn = on_end;
    (void)vhpi_register_cb(&data, 0);
    printf("#c startup phase=%s\n", phase_name());
    fflush(stdout);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

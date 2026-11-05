// SPDX-License-Identifier: Apache-2.0
// VHPI tabular registry conformance plug-in (IEEE 1076-2019 20.2.2): its
// startup table registers nothing; a registry file names its foreign
// architecture, its foreign function, an application and a library of
// foreign models by their entry points. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static char library_name[] = "fsim_foreign_vhpi_registry_plugin";
static char seven_name[] = "seven";

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

FSIM_VHPI_EXPORT void registry_elab(const vhpiCbDataT* data)
{
    printf("#c elab %s phase=%s\n",
        (const char*)vhpi_get_str(vhpiFullNameP, data->obj), phase_name());
    fflush(stdout);
}

FSIM_VHPI_EXPORT void registry_exec(const vhpiCbDataT* data)
{
    printf("#c exec %s phase=%s\n",
        (const char*)vhpi_get_str(vhpiFullNameP, data->obj), phase_name());
    fflush(stdout);
}

// A foreign subprogram registry whose execution specifier is null names
// the execution function after the model.
FSIM_VHPI_EXPORT void answer(const vhpiCbDataT* data)
{
    put_integer(data->obj, 42);
}

static void exec_seven(const vhpiCbDataT* data)
{
    put_integer(data->obj, 7);
}

FSIM_VHPI_EXPORT void registry_application(void)
{
    printf("#c application phase=%s\n", phase_name());
    fflush(stdout);
}

// The registration function of the library of foreign models registers
// its model itself (IEEE 1076-2019 20.2.3).
FSIM_VHPI_EXPORT void registry_library(void)
{
    vhpiForeignDataT model;
    memset(&model, 0, sizeof model);
    model.kind = vhpiFuncF;
    model.libraryName = library_name;
    model.modelName = seven_name;
    model.execf = exec_seven;
    printf("#c library phase=%s registered=%d\n", phase_name(),
        vhpi_register_foreignf(&model) != NULL);
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
    data.reason = vhpiCbEndOfSimulation;
    data.cb_rtn = on_end;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

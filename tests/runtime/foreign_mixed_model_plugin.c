// SPDX-License-Identifier: Apache-2.0
// Mixed-language hierarchy plug-in, loaded as both a VPI and a VHPI
// library: each service sees the whole instance tree, and a full path
// through an instance of the other language resolves (cocotb plan A5).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "fsim/runtime/vpi_abi.h"
#include "vhpi_user.h"
#include "vpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

// The roots are modules; a scope's instances are among its internal
// scopes, a VHDL instance included with fsim's own object type (IEEE
// 1800-2023 37.10 has none for it).
static void vpi_walk(vpiHandle scope, int depth)
{
    vpiHandle iterator
        = vpi_iterate(scope == NULL ? vpiModule : vpiInternalScope, scope);
    vpiHandle module;
    while (iterator != NULL && (module = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c vpi %d %s %s def=%s\n", depth,
            text(vpi_get_str(vpiType, module)),
            text(vpi_get_str(vpiFullName, module)),
            text(vpi_get_str(vpiDefName, module)));
        vpi_walk(module, depth + 1);
    }
}

static PLI_INT32 vpi_start(p_cb_data data)
{
    // A label a VHDL architecture declares matches in either case (IEEE
    // 1076-2019 15.4.2); the names of a SystemVerilog scope do not (IEEE
    // 1800-2023 5.6).
    static char* names[] = { "top.m", "top.m.t", "top.m.l", "top.m.l.y",
        "top.m.l.g", "top.m.l.g.s", "top.m.L", "top.m.L.y", "top.m.L.Y",
        "top.M", "top.m.GV[1].V" };
    (void)data;
    vpi_walk(NULL, 0);
    for (size_t index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        vpiHandle object = vpi_handle_by_name(names[index], NULL);
        vpi_printf("#c vpi byname %s %s\n", names[index],
            object != NULL ? text(vpi_get_str(vpiType, object)) : "NULL");
    }
    return 0;
}

static void vpi_startup(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = vpi_start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { vpi_startup, 0 };

// A string property lower-cased: the letter case of VHDL names is not
// specified.
static const char* lower(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[2][128];
    static int next;
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = 1 - next;
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

static void vhpi_walk(vhpiHandleT region, int depth)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiInternalRegions, region);
    vhpiHandleT child;
    while (iterator != NULL && (child = vhpi_scan(iterator)) != NULL) {
        printf("#c vhpi %d %s %s\n", depth,
            lower(child, vhpiKindStrP), lower(child, vhpiFullNameP));
        vhpi_walk(child, depth + 1);
    }
}

static void vhpi_start(const vhpiCbDataT* data)
{
    static const char* const names[] = { "top.m", "top.m.t", "top.m.l",
        "top.m.l.g", "top.m.l.g.s", ":top:m:l:g:s", ":top:m:gv(1):v",
        ":top:m:gv(1):v:s", "top.m.gv(0).v" };
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    (void)data;
    printf("#c vhpi root %s %s\n", lower(root, vhpiKindStrP),
        lower(root, vhpiFullNameP));
    if (root != NULL) {
        vhpi_walk(root, 1);
    }
    for (size_t index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        vhpiHandleT object = vhpi_handle_by_name(names[index], NULL);
        printf("#c vhpi byname %s %s\n", names[index],
            lower(object, vhpiKindStrP));
    }
    fflush(stdout);
}

static void vhpi_startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = vhpi_start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { vhpi_startup, 0 };

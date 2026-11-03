// SPDX-License-Identifier: Apache-2.0
// VPI clocking block and nettype conformance plug-in (IEEE 1800-2023 37.23,
// 37.48): clocking blocks of a module with their events, skews, edges and
// clockvars, the default and global clocking, and nettype declarations.
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

static const char* name_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiName, object)) : "-";
}

static const char* edge_name(PLI_INT32 edge)
{
    if (edge == vpiPosedge) {
        return "posedge";
    }
    if (edge == vpiNegedge) {
        return "negedge";
    }
    return edge == vpiNoEdge ? "none" : "other";
}

static const char* direction_name(PLI_INT32 direction)
{
    return direction == vpiInput ? "input"
        : direction == vpiOutput ? "output"
        : direction == vpiInout  ? "inout"
                                 : "other";
}

// A skew is a delay control whose vpiDelay is a constant.
static void skew(const char* owner, const char* label, vpiHandle object,
    PLI_INT32 relation)
{
    vpiHandle delay_control = vpi_handle(relation, object);
    vpiHandle delay;
    s_vpi_value value = { vpiIntVal, { 0 } };
    if (delay_control == NULL) {
        vpi_printf("#c %s %s none\n", owner, label);
        return;
    }
    delay = vpi_handle(vpiDelay, delay_control);
    if (delay == NULL) {
        vpi_printf("#c %s %s type=%s delay=none\n", owner, label,
            text(vpi_get_str(vpiType, delay_control)));
        return;
    }
    vpi_get_value(delay, &value);
    vpi_printf("#c %s %s type=%s delay=%d\n", owner, label,
        text(vpi_get_str(vpiType, delay_control)), (int)value.value.integer);
}

static void clocking(vpiHandle block)
{
    vpiHandle event = vpi_handle(vpiClockingEvent, block);
    vpiHandle instance = vpi_handle(vpiInstance, block);
    vpiHandle iterator;
    vpiHandle io;
    char owner[64];
    const char* name = name_of(block);
    vpi_printf("#c %s type=%s full=%s instance=%s event=%s condition=%s\n",
        name, text(vpi_get_str(vpiType, block)),
        text(vpi_get_str(vpiFullName, block)), name_of(instance),
        event != NULL ? text(vpi_get_str(vpiType, event)) : "-",
        event != NULL ? name_of(vpi_handle(vpiCondition, event)) : "-");
    skew(name, "default-input", block, vpiInputSkew);
    skew(name, "default-output", block, vpiOutputSkew);
    vpi_printf("#c %s default-edges input=%s output=%s\n", name,
        edge_name((PLI_INT32)vpi_get(vpiInputEdge, block)),
        edge_name((PLI_INT32)vpi_get(vpiOutputEdge, block)));
    iterator = vpi_iterate(vpiClockingIODecl, block);
    while (iterator != NULL && (io = vpi_scan(iterator)) != NULL) {
        vpiHandle parent = vpi_handle(vpiParent, io);
        vpi_printf("#c %s.%s type=%s full=%s dir=%s parent=%s expr=%s\n", name,
            name_of(io), text(vpi_get_str(vpiType, io)),
            text(vpi_get_str(vpiFullName, io)),
            direction_name((PLI_INT32)vpi_get(vpiDirection, io)),
            name_of(parent), name_of(vpi_handle(vpiExpr, io)));
        snprintf(owner, sizeof owner, "%s.%s", name, name_of(io));
        skew(owner, "input-skew", io, vpiInputSkew);
        skew(owner, "output-skew", io, vpiOutputSkew);
    }
}

static void nettype(vpiHandle declaration)
{
    vpi_printf("#c nettype %s type=%s full=%s with=%s alias=%s\n",
        name_of(declaration), text(vpi_get_str(vpiType, declaration)),
        text(vpi_get_str(vpiFullName, declaration)),
        name_of(vpi_handle(vpiWith, declaration)),
        name_of(vpi_handle(vpiNetTypedefAlias, declaration)));
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle iterator;
    vpiHandle object;
    int count = 0;
    (void)data;

    iterator = vpi_iterate(vpiClockingBlock, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        clocking(object);
        ++count;
    }
    vpi_printf("#c clocking blocks=%d\n", count);
    vpi_printf("#c by name %s\n",
        name_of(vpi_handle_by_name("top.cb2", NULL)));
    vpi_printf("#c default clocking %s global clocking %s\n",
        name_of(vpi_handle(vpiDefaultClocking, top)),
        name_of(vpi_handle(vpiGlobalClocking, top)));

    count = 0;
    iterator = vpi_iterate(vpiNettypeDecl, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        nettype(object);
        ++count;
    }
    vpi_printf("#c nettypes=%d\n", count);
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

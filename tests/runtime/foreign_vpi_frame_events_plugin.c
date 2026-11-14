// SPDX-License-Identifier: Apache-2.0
// Frame conformance plug-in for an automatic event array, its elements, and
// the call a frame was activated from (IEEE 1800-2023 37.43, 38.34): at
// the delay of ev_arr the array is listed and its element 1 put; at the
// first statement of f and of work the frame's call is read back. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static vpiHandle element_handle;

static PLI_INT32 trigger(p_cb_data data)
{
    s_vpi_value value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vpiScalarVal;
    value.value.scalar = vpi1;
    vpi_put_value(element_handle, &value, NULL, vpiNoDelay);
    vpi_printf("#c element put\n");
    return 0;
}

static const char* type_name(vpiHandle object)
{
    switch (vpi_get(vpiType, object)) {
    case vpiNamedEventArray:
        return "vpiNamedEventArray";
    case vpiNamedEvent:
        return "vpiNamedEvent";
    default:
        return "other";
    }
}

static PLI_INT32 in_ev_arr(p_cb_data data)
{
    vpiHandle frame = vpi_handle(vpiFrame, NULL);
    vpiHandle iterator = vpi_iterate(vpiAutomatics, frame);
    vpiHandle variable;
    s_cb_data later;
    s_vpi_time delay;
    (void)data;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        if (strcmp(vpi_get_str(vpiName, variable), "ea") == 0) {
            element_handle = vpi_handle_by_index(variable, 1);
            vpi_printf("#c automatic ea %s element %s %s\n",
                type_name(variable),
                element_handle != NULL ? vpi_get_str(vpiName, element_handle) : "none",
                element_handle != NULL ? type_name(element_handle) : "none");
        }
    }
    memset(&later, 0, sizeof later);
    memset(&delay, 0, sizeof delay);
    delay.type = vpiSimTime;
    delay.low = 1;
    later.reason = cbAfterDelay;
    later.cb_rtn = trigger;
    later.time = &delay;
    vpi_register_cb(&later);
    return 0;
}

// An event declared through a typedef, and an array of them: each is listed
// with its type, and the element 1 of the array, which no thread has used,
// is put before the statement that waits on its `triggered` state.
static PLI_INT32 in_ev_typed(p_cb_data data)
{
    vpiHandle frame = vpi_handle(vpiFrame, NULL);
    vpiHandle iterator = vpi_iterate(vpiAutomatics, frame);
    vpiHandle variable;
    vpiHandle element = NULL;
    s_vpi_value value;
    (void)data;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c typed %s %s\n", vpi_get_str(vpiName, variable),
            type_name(variable));
        if (vpi_get(vpiType, variable) == vpiNamedEventArray) {
            element = vpi_handle_by_index(variable, 1);
        }
    }
    memset(&value, 0, sizeof value);
    value.format = vpiScalarVal;
    value.value.scalar = vpi1;
    if (element != NULL) {
        vpi_put_value(element, &value, NULL, vpiNoDelay);
    }
    vpi_printf("#c typed element put\n");
    return 0;
}

// The frame's call: its argument is the constant the call passed.
static PLI_INT32 in_call(p_cb_data data)
{
    vpiHandle frame = vpi_handle(vpiFrame, NULL);
    vpiHandle origin = frame != NULL ? vpi_handle(vpiOrigin, frame) : NULL;
    vpiHandle arguments = origin != NULL ? vpi_iterate(vpiArgument, origin) : NULL;
    vpiHandle argument = arguments != NULL ? vpi_scan(arguments) : NULL;
    s_vpi_value value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    if (argument != NULL) {
        vpi_get_value(argument, &value);
    }
    vpi_printf("#c %s call line %d argument %d\n", (const char*)data->user_data,
        origin != NULL ? (int)vpi_get(vpiLineNo, origin) : -1,
        (int)value.value.integer);
    return 0;
}

// The statement of a callable's body that begins on `line`.
static vpiHandle statement_at(const char* name, int function, int line)
{
    vpiHandle module = vpi_handle_by_name("top", NULL);
    vpiHandle iterator = vpi_iterate(function ? vpiFunction : vpiTask, module);
    vpiHandle callable;
    while (iterator != NULL && (callable = vpi_scan(iterator)) != NULL) {
        if (strcmp(vpi_get_str(vpiName, callable), name) == 0) {
            vpiHandle statements = vpi_iterate(vpiStmt, vpi_handle(vpiStmt, callable));
            vpiHandle statement;
            while (statements != NULL && (statement = vpi_scan(statements)) != NULL) {
                if (vpi_get(vpiLineNo, statement) == line) {
                    return statement;
                }
            }
        }
    }
    return NULL;
}

// A named event array of the module: its elements are named events too.
static PLI_INT32 fire_module(p_cb_data data)
{
    vpiHandle array = vpi_handle_by_name("top.sea", NULL);
    vpiHandle element = array != NULL ? vpi_handle_by_index(array, 2) : NULL;
    s_vpi_value value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vpiScalarVal;
    value.value.scalar = vpi1;
    vpi_put_value(element, &value, NULL, vpiNoDelay);
    vpi_printf("#c module element put\n");
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_cb_data module_put;
    s_vpi_time module_delay;
    vpiHandle array = vpi_handle_by_name("top.sea", NULL);
    vpiHandle events = array != NULL ? vpi_iterate(vpiNamedEvent, array) : NULL;
    int count = 0;
    (void)data;
    while (events != NULL && vpi_scan(events) != NULL) {
        ++count;
    }
    vpi_printf("#c module array %s size %d events %d\n",
        array != NULL ? type_name(array) : "none",
        array != NULL ? (int)vpi_get(vpiSize, array) : -1, count);
    memset(&module_put, 0, sizeof module_put);
    memset(&module_delay, 0, sizeof module_delay);
    module_delay.type = vpiSimTime;
    module_delay.low = 10;
    module_put.reason = cbAfterDelay;
    module_put.cb_rtn = fire_module;
    module_put.time = &module_delay;
    vpi_register_cb(&module_put);
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStmt;
    callback.obj = statement_at("ev_arr", 0, 17);
    callback.cb_rtn = in_ev_arr;
    vpi_register_cb(&callback);
    callback.obj = statement_at("ev_typed", 0, 59);
    callback.cb_rtn = in_ev_typed;
    if (callback.obj == NULL || vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c refused ev_typed\n");
    }
    callback.cb_rtn = in_call;
    callback.obj = statement_at("f", 1, 25);
    callback.cb_rtn = in_call;
    callback.user_data = (PLI_BYTE8*)"f";
    if (callback.obj == NULL || vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c refused f\n");
    }
    callback.obj = statement_at("work", 0, 32);
    callback.user_data = (PLI_BYTE8*)"work";
    if (callback.obj == NULL || vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c refused work\n");
    }
    return 0;
}

static void startup(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

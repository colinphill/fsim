// SPDX-License-Identifier: Apache-2.0
// Frames of methods the simulation's UVM services run (IEEE 1800-2023 37.43,
// 38.36.1): reports the start and end of the frame of p::pkt::quad (the
// frame whose automatics include `who`) and of the frames it calls, their
// relations (vpiStmt, vpiOrigin) and their automatic variables, and runs
// cbStmt on the statements of method twice, found through the class defn
// (37.31 vpiMethods, 37.41 vpiStmt). In the first nested frame it puts
// values with no delay to the caller's string and vector and to its own
// formal (37.43 detail 3), which the design's results show. A variable
// handle kept after its frame ended is gone (37.3.7). Without simulate
// --foreign-probes each registration fails with an error.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

// The depth of the frame of quad while it runs, 0 outside it.
static int enabled;
static int nested;
static vpiHandle kept_variable;

static int depth_of(vpiHandle frame)
{
    int depth = 0;
    while (frame != NULL) {
        ++depth;
        frame = vpi_handle(vpiParent, frame);
    }
    return depth;
}

static int line_of(vpiHandle object)
{
    return object != NULL ? (int)vpi_get(vpiLineNo, object) : 0;
}

static void show_value(const char* tag, vpiHandle variable)
{
    PLI_INT32 type = vpi_get(vpiType, variable);
    s_vpi_value value;
    char text[96];
    memset(&value, 0, sizeof value);
    value.format = type == vpiStringVar ? vpiStringVal
        : type == vpiReg || type == vpiLogicVar ? vpiHexStrVal
                                                : vpiIntVal;
    vpi_get_value(variable, &value);
    if (value.format == vpiSuppressVal) {
        snprintf(text, sizeof text, "none");
    } else if (value.format == vpiIntVal) {
        snprintf(text, sizeof text, "%d", (int)value.value.integer);
    } else {
        snprintf(text, sizeof text, "%s", value.value.str);
    }
    vpi_printf("#c %s %s %s size=%d signed=%d value=%s\n", tag,
        vpi_get_str(vpiName, variable), vpi_get_str(vpiType, variable),
        (int)vpi_get(vpiSize, variable), (int)vpi_get(vpiSigned, variable),
        text);
}

static int declares(vpiHandle frame, const char* name)
{
    vpiHandle iterator = vpi_iterate(vpiAutomatics, frame);
    vpiHandle variable;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        if (strcmp(vpi_get_str(vpiName, variable), name) == 0) {
            vpi_free_object(iterator);
            return 1;
        }
    }
    return 0;
}

static void put_value(vpiHandle frame, const char* name, PLI_INT32 format,
    PLI_INT32 integer, const char* text)
{
    vpiHandle iterator = vpi_iterate(vpiAutomatics, frame);
    vpiHandle variable;
    s_vpi_value value;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        if (strcmp(vpi_get_str(vpiName, variable), name) != 0) {
            continue;
        }
        memset(&value, 0, sizeof value);
        value.format = format;
        if (format == vpiStringVal) {
            value.value.str = (PLI_BYTE8*)text;
        } else {
            value.value.integer = integer;
        }
        vpi_put_value(variable, &value, NULL, vpiNoDelay);
        // A put with a delay is refused (37.43 detail 3).
        vpi_printf("#c put %s delayed=%s\n", name,
            vpi_put_value(variable, &value, NULL, vpiInertialDelay) == NULL
                ? "refused"
                : "accepted");
        vpi_free_object(iterator);
        return;
    }
    vpi_printf("#c put %s missing\n", name);
}

static PLI_INT32 frame_start(p_cb_data data)
{
    vpiHandle frame = data->obj;
    vpiHandle parent = vpi_handle(vpiParent, frame);
    vpiHandle thread = vpi_handle(vpiThread, frame);
    vpiHandle iterator;
    vpiHandle variable;
    if (!enabled) {
        if (!declares(frame, "who")) {
            return 0;
        }
        enabled = depth_of(frame);
    }
    vpi_printf("#c frame start depth=%d type=%s active=%d current=%d "
               "thread=%d caller=%d origin=%d\n",
        depth_of(frame), vpi_get_str(vpiType, frame),
        (int)vpi_get(vpiActive, frame),
        vpi_compare_objects(vpi_handle(vpiFrame, NULL), frame),
        thread != NULL
            && vpi_compare_objects(vpi_handle(vpiFrame, thread), frame),
        line_of(parent != NULL ? vpi_handle(vpiStmt, parent) : NULL),
        line_of(vpi_handle(vpiOrigin, frame)));
    iterator = vpi_iterate(vpiAutomatics, frame);
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        show_value("automatic", variable);
        if (kept_variable == NULL) {
            kept_variable = variable;
        }
    }
    if (parent != NULL && nested++ == 0) {
        // The caller's variables while its call of this frame runs.
        iterator = vpi_iterate(vpiAutomatics, parent);
        while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
            show_value("caller", variable);
        }
        put_value(parent, "tag", vpiStringVal, 0, "patched");
        put_value(parent, "code", vpiIntVal, 0x3c, NULL);
        put_value(frame, "x", vpiIntVal, 5, NULL);
    }
    return 0;
}

static PLI_INT32 frame_end(p_cb_data data)
{
    if (!enabled) {
        return 0;
    }
    vpi_printf("#c frame end depth=%d active=%d stmt=%d\n",
        depth_of(data->obj), (int)vpi_get(vpiActive, data->obj),
        line_of(vpi_handle(vpiStmt, data->obj)));
    if (depth_of(data->obj) == enabled) {
        enabled = 0;
    }
    return 0;
}

static PLI_INT32 statement(p_cb_data data)
{
    if (enabled) {
        vpi_printf("#c stmt %s line %d\n", vpi_get_str(vpiType, data->obj),
            line_of(data->obj));
    }
    return 0;
}

static PLI_INT32 ended(p_cb_data data)
{
    (void)data;
    if (kept_variable != NULL) {
        // The variables of a frame cease to exist with it (37.3.7).
        vpi_printf("#c stale automatic type=%d\n",
            (int)vpi_get(vpiType, kept_variable));
    }
    return 0;
}

static void place(const char* name, PLI_INT32 reason,
    PLI_INT32 (*routine)(p_cb_data))
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    s_cb_data data;
    s_vpi_error_info error;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    data.time = &time;
    if (vpi_register_cb(&data) != NULL) {
        return;
    }
    memset(&error, 0, sizeof error);
    if (vpi_chk_error(&error) != 0) {
        vpi_printf("#c refused %s: %s\n", name,
            error.message != NULL ? error.message : "");
    } else {
        vpi_printf("#c refused %s without an error\n", name);
    }
}

static void place_statement(vpiHandle object)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStmt;
    data.cb_rtn = statement;
    data.obj = object;
    data.time = &time;
    if (vpi_register_cb(&data) == NULL) {
        vpi_printf("#c refused cbStmt line %d\n", line_of(object));
    }
}

// cbStmt on the body of method twice of class p::pkt and on each statement
// of it.
static void place_twice(void)
{
    vpiHandle defn = vpi_handle_by_name("p::pkt", NULL);
    vpiHandle methods = defn != NULL ? vpi_iterate(vpiMethods, defn) : NULL;
    vpiHandle method;
    vpiHandle body;
    vpiHandle items;
    vpiHandle item;
    while (methods != NULL && (method = vpi_scan(methods)) != NULL) {
        if (strcmp(vpi_get_str(vpiName, method), "twice") != 0) {
            continue;
        }
        body = vpi_handle(vpiStmt, method);
        vpi_printf("#c twice %s body %s line %d\n",
            vpi_get_str(vpiType, method),
            body != NULL ? vpi_get_str(vpiType, body) : "none",
            line_of(body));
        vpi_free_object(methods);
        if (body == NULL) {
            return;
        }
        place_statement(body);
        items = vpi_iterate(vpiStmt, body);
        while (items != NULL && (item = vpi_scan(items)) != NULL) {
            place_statement(item);
        }
        return;
    }
    vpi_printf("#c twice missing defn=%d\n", defn != NULL);
}

static PLI_INT32 compiled(p_cb_data data)
{
    (void)data;
    place_twice();
    place("cbStartOfFrame", cbStartOfFrame, frame_start);
    place("cbEndOfFrame", cbEndOfFrame, frame_end);
    place("cbEndOfSimulation", cbEndOfSimulation, ended);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbEndOfCompile;
    data.cb_rtn = compiled;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

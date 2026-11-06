// SPDX-License-Identifier: Apache-2.0
// Frame variable and hierarchical cbStmt conformance plug-in (IEEE
// 1800-2023 37.43, 38.36.1.1): places a statement callback on the body of
// task bump of two instances, which the top module calls by hierarchical
// name, and reads the automatic variables of the running frame from the
// callback of the last statement. Strings, a queue and a static task's
// locals are listed or left out by lifetime (only the automatic ones are the
// frame's), strings and integers are put while the frame runs, and a value
// change callback on an automatic variable is refused (37.43 details 1-3).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static vpiHandle kept_variable;

static const char* type_name(PLI_INT32 type)
{
    switch (type) {
    case vpiBegin:
        return "vpiBegin";
    case vpiAssignment:
        return "vpiAssignment";
    default:
        return "other";
    }
}

static void dump_automatics(PLI_BYTE8* tag)
{
    vpiHandle frame = vpi_handle(vpiFrame, NULL);
    vpiHandle iterator;
    vpiHandle variable;
    s_vpi_value value;
    if (frame == NULL) {
        vpi_printf("#c %s no frame\n", tag);
        return;
    }
    iterator = vpi_iterate(vpiAutomatics, frame);
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        memset(&value, 0, sizeof value);
        value.format = vpiIntVal;
        vpi_get_value(variable, &value);
        vpi_printf("#c %s automatic %s %s size=%d value=%d\n", tag,
            vpi_get_str(vpiName, variable), vpi_get_str(vpiType, variable),
            (int)vpi_get(vpiSize, variable), (int)value.value.integer);
    }
    vpi_printf("#c %s frame active=%d parent=%d\n", tag,
        (int)vpi_get(vpiActive, frame), vpi_handle(vpiParent, frame) != NULL);
}

// Lists the automatics of the running frame with their full names and
// values, putting new values to a string named tag and an integer named mark.
static void describe_automatics(PLI_BYTE8* tag)
{
    vpiHandle frame = vpi_handle(vpiFrame, NULL);
    vpiHandle iterator = frame != NULL ? vpi_iterate(vpiAutomatics, frame) : NULL;
    vpiHandle variable;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        const char* name = vpi_get_str(vpiName, variable);
        PLI_INT32 type = vpi_get(vpiType, variable);
        s_vpi_value value;
        char text[96];
        memset(&value, 0, sizeof value);
        if (type == vpiStringVar) {
            value.format = vpiStringVal;
        } else {
            value.format = vpiIntVal;
        }
        vpi_get_value(variable, &value);
        if (value.format == vpiSuppressVal) {
            snprintf(text, sizeof text, "none");
        } else if (type == vpiStringVar) {
            snprintf(text, sizeof text, "%s", value.value.str);
        } else {
            snprintf(text, sizeof text, "%d", (int)value.value.integer);
        }
        vpi_printf("#c %s automatic %s %s full=%s", tag, name,
            vpi_get_str(vpiType, variable), vpi_get_str(vpiFullName, variable));
        if (type == vpiRegArray) {
            vpi_printf(" array=%s",
                vpi_get(vpiArrayType, variable) == vpiQueueArray
                    ? "vpiQueueArray"
                    : "other");
        }
        vpi_printf(" value=%s\n", text);
        if (strcmp(name, "tag") == 0) {
            memset(&value, 0, sizeof value);
            value.format = vpiStringVal;
            value.value.str = (PLI_BYTE8*)"patched";
            vpi_put_value(variable, &value, NULL, vpiNoDelay);
        } else if (strcmp(name, "mark") == 0) {
            memset(&value, 0, sizeof value);
            value.format = vpiIntVal;
            value.value.integer = 42;
            vpi_put_value(variable, &value, NULL, vpiNoDelay);
        }
    }
}

static PLI_INT32 statement(p_cb_data data)
{
    PLI_BYTE8* tag = data->user_data;
    int line = (int)vpi_get(vpiLineNo, data->obj);
    if (tag[0] == 'd' || tag[0] == 't') {
        if (line == 22 || line == 30) {
            describe_automatics(tag);
        }
        return 0;
    }
    vpi_printf("#c stmt %s %s line %d\n", tag,
        type_name(vpi_get(vpiType, data->obj)), line);
    if (line == 11) {
        dump_automatics(tag);
    }
    return 0;
}

static PLI_INT32 frame_start(p_cb_data data)
{
    int count = 0;
    vpiHandle iterator = vpi_iterate(vpiAutomatics, data->obj);
    vpiHandle variable;
    int bump = 0;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        const char* name = vpi_get_str(vpiName, variable);
        if (strcmp(name, "step") == 0) {
            bump = 1;
        }
        if (strcmp(name, "tag") == 0) {
            // An automatic variable takes no value change callback (37.43
            // detail 2).
            s_cb_data change;
            memset(&change, 0, sizeof change);
            change.reason = cbValueChange;
            change.cb_rtn = frame_start;
            change.obj = variable;
            vpi_printf("#c valuechange %s\n",
                vpi_register_cb(&change) == NULL ? "refused" : "accepted");
        }
        if (kept_variable == NULL) {
            kept_variable = variable;
        }
        ++count;
    }
    // Only the frames of bump are counted here.
    if (bump) {
        vpi_printf("#c frame start automatics=%d\n", count);
    }
    return 0;
}

static PLI_INT32 ended(p_cb_data data)
{
    (void)data;
    // The variables of a frame cease to exist with it (37.3.7).
    vpi_printf("#c stale automatic type=%d\n",
        (int)vpi_get(vpiType, kept_variable));
    return 0;
}

static void place(vpiHandle object, PLI_INT32 reason,
    PLI_INT32 (*routine)(p_cb_data), PLI_BYTE8* tag)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    data.obj = object;
    data.time = &time;
    data.user_data = tag;
    if (vpi_register_cb(&data) == NULL) {
        vpi_printf("#c refused %s\n", tag);
    }
}

static void place_task(PLI_BYTE8* path, PLI_BYTE8* tag)
{
    vpiHandle task = vpi_handle_by_name(path, NULL);
    vpiHandle body = task != NULL ? vpi_handle(vpiStmt, task) : NULL;
    vpiHandle iterator;
    vpiHandle item;
    if (body == NULL) {
        vpi_printf("#c missing %s\n", path);
        return;
    }
    place(body, cbStmt, statement, tag);
    iterator = vpi_iterate(vpiStmt, body);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        place(item, cbStmt, statement, tag);
    }
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data finish;
    (void)data;
    place(NULL, cbStartOfFrame, frame_start, "frame");
    place_task("top.a.bump", "a");
    place_task("top.b.bump", "b");
    place_task("top.a.describe", "d");
    place_task("top.a.tally", "t");
    memset(&finish, 0, sizeof finish);
    finish.reason = cbEndOfSimulation;
    finish.cb_rtn = ended;
    (void)vpi_register_cb(&finish);
    {
        // The callback objects of an object (37.80): the one statement
        // callback placed on the body of top.a.bump.
        vpiHandle task = vpi_handle_by_name("top.a.bump", NULL);
        vpiHandle body = task != NULL ? vpi_handle(vpiStmt, task) : NULL;
        vpiHandle iterator = body != NULL ? vpi_iterate(vpiCallback, body) : NULL;
        int count = 0;
        vpiHandle callback;
        while (iterator != NULL && (callback = vpi_scan(iterator)) != NULL) {
            s_cb_data info;
            memset(&info, 0, sizeof info);
            vpi_get_cb_info(callback, &info);
            count += info.reason == cbStmt && info.obj == body;
        }
        vpi_printf("#c callbacks on body=%d\n", count);
    }
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

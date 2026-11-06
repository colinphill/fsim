// SPDX-License-Identifier: Apache-2.0
// Frame, thread and cbCreateObj conformance plug-in (IEEE 1800-2023 37.43,
// 37.44, 38.36.1): reports each frame and thread of the design, the frame
// relations, and each class object a constructor completes. Without
// simulate --foreign-probes each registration fails with an error.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static int accepted;
// 0 until the fork starts its branch: the frames of the first phase are
// reported in full, those after it (a constructor) by depth only.
static int phase;
static int enters;
static vpiHandle item_typespec;
static vpiHandle ended_frame;

static int depth_of(vpiHandle frame)
{
    int depth = 0;
    while (frame != NULL) {
        ++depth;
        frame = vpi_handle(vpiParent, frame);
    }
    return depth;
}

static int count_threads(vpiHandle reference)
{
    int count = 0;
    vpiHandle iterator = vpi_iterate(vpiThread, reference);
    while (iterator != NULL && vpi_scan(iterator) != NULL) {
        ++count;
    }
    return count;
}

static int line_of(vpiHandle object)
{
    return object != NULL ? (int)vpi_get(vpiLineNo, object) : 0;
}

static const char* type_of(vpiHandle object)
{
    return object != NULL ? vpi_get_str(vpiType, object) : "none";
}

static PLI_INT32 thread_start(p_cb_data data)
{
    vpi_printf("#c thread start type=%s parent=%d threads=%d\n",
        type_of(data->obj), vpi_handle(vpiParent, data->obj) != NULL,
        count_threads(NULL));
    if (vpi_handle(vpiParent, data->obj) != NULL) {
        phase = 1;
    }
    return 0;
}

static PLI_INT32 thread_end(p_cb_data data)
{
    vpi_printf("#c thread end parent=%d children=%d\n",
        vpi_handle(vpiParent, data->obj) != NULL,
        count_threads(data->obj));
    return 0;
}

static PLI_INT32 thread_enter(p_cb_data data)
{
    (void)data;
    ++enters;
    return 0;
}

static PLI_INT32 frame_start(p_cb_data data)
{
    vpiHandle frame = data->obj;
    vpiHandle parent = vpi_handle(vpiParent, frame);
    vpiHandle origin = vpi_handle(vpiOrigin, frame);
    vpiHandle thread = vpi_handle(vpiThread, frame);
    vpiHandle iterator;
    if (phase != 0) {
        vpi_printf("#c frame start depth=%d\n", depth_of(frame));
        return 0;
    }
    vpi_printf("#c frame start depth=%d type=%s active=%d current=%d "
               "thread=%d origin=%s line=%d caller=%d\n",
        depth_of(frame), type_of(frame), (int)vpi_get(vpiActive, frame),
        vpi_compare_objects(vpi_handle(vpiFrame, NULL), frame),
        thread != NULL && vpi_compare_objects(vpi_handle(vpiFrame, thread), frame),
        type_of(origin), line_of(origin),
        line_of(parent != NULL ? vpi_handle(vpiStmt, parent) : NULL));
    if (depth_of(frame) == 1) {
        // Task outer declares no variable, so it has no automatics (the
        // variables of a frame are checked by foreign_vpi_frame_vars).
        iterator = vpi_iterate(vpiAutomatics, frame);
        vpi_printf("#c automatics=%d\n", iterator != NULL);
    }
    return 0;
}

static PLI_INT32 frame_end(p_cb_data data)
{
    vpiHandle frame = data->obj;
    if (phase != 0) {
        vpi_printf("#c frame end depth=%d\n", depth_of(frame));
    } else {
        vpi_printf("#c frame end depth=%d active=%d stmt=%d\n",
            depth_of(frame), (int)vpi_get(vpiActive, frame),
            line_of(vpi_handle(vpiStmt, frame)));
    }
    ended_frame = frame;
    return 0;
}

static PLI_INT32 twice_start(p_cb_data data)
{
    vpi_printf("#c twice start depth=%d\n", depth_of(data->obj));
    return 0;
}

static PLI_INT32 created(p_cb_data data)
{
    vpi_printf("#c cbCreateObj type=%s typespec=%d id=%d\n",
        type_of(data->obj),
        vpi_compare_objects(vpi_handle(vpiClassTypespec, data->obj),
            item_typespec),
        vpi_get(vpiObjId, data->obj) > 0);
    return 0;
}

static PLI_INT32 ended(p_cb_data data)
{
    (void)data;
    if (accepted != 0) {
        vpi_printf("#c enters=%d\n", enters);
        // A frame ceases to exist once its end callback has returned.
        vpi_printf("#c stale frame type=%d\n",
            (int)vpi_get(vpiType, ended_frame));
    }
    return 0;
}

static void place(const char* name, PLI_INT32 reason, PLI_INT32 (*routine)(p_cb_data),
    vpiHandle object)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    s_cb_data data;
    s_vpi_error_info error;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    data.obj = object;
    data.time = &time;
    if (vpi_register_cb(&data) != NULL) {
        ++accepted;
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

static PLI_INT32 compiled(p_cb_data data)
{
    s_cb_data finish;
    (void)data;
    item_typespec = vpi_handle(vpiClassTypespec,
        vpi_handle_by_name("top.it", NULL));
    place("cbStartOfThread", cbStartOfThread, thread_start, NULL);
    place("cbEndOfThread", cbEndOfThread, thread_end, NULL);
    place("cbEnterThread", cbEnterThread, thread_enter, NULL);
    place("cbStartOfFrame", cbStartOfFrame, frame_start, NULL);
    place("cbEndOfFrame", cbEndOfFrame, frame_end, NULL);
    place("cbStartOfFrame twice", cbStartOfFrame, twice_start,
        vpi_handle_by_name("top.twice", NULL));
    place("cbCreateObj", cbCreateObj, created, item_typespec);
    memset(&finish, 0, sizeof finish);
    finish.reason = cbEndOfSimulation;
    finish.cb_rtn = ended;
    (void)vpi_register_cb(&finish);
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

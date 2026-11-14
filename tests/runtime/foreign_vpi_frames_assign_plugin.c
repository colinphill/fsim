// SPDX-License-Identifier: Apache-2.0
// VPI conformance plug-in for the frames of calls in continuous assignments
// (IEEE 1800-2023 37.43 detail 1, 38.36.1): each frame's origin, callee and
// thread, cbStartOfFrame placed on a function reached by a hierarchical call
// and cbStmt on a statement of that function. Run with --foreign-probes.
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

static const char* name_of(PLI_INT32 property, vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(property, object)) : "-";
}

static int now(void)
{
    s_vpi_time time;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    vpi_get_time(NULL, &time);
    return (int)time.low;
}

static PLI_INT32 frame_event(p_cb_data data)
{
    vpiHandle frame = data->obj;
    vpiHandle origin = frame != NULL ? vpi_handle(vpiOrigin, frame) : NULL;
    vpiHandle callee = origin != NULL ? vpi_handle(vpiFunction, origin) : NULL;
    vpi_printf("#c t=%d %s origin=%s origin-type=%s callee=%s thread=%s "
               "parent=%s\n",
        now(), data->reason == cbStartOfFrame ? "start" : "end",
        name_of(vpiName, origin), name_of(vpiType, origin),
        name_of(vpiFullName, callee),
        frame != NULL && vpi_handle(vpiThread, frame) != NULL ? "set" : "null",
        frame != NULL && vpi_handle(vpiParent, frame) != NULL ? "set"
                                                              : "null");
    return 0;
}

static PLI_INT32 callee_frame(p_cb_data data)
{
    (void)data;
    vpi_printf("#c t=%d top.u.h frame\n", now());
    return 0;
}

static PLI_INT32 statement(p_cb_data data)
{
    vpi_printf("#c t=%d stmt line=%d\n", now(),
        data->obj != NULL ? (int)vpi_get(vpiLineNo, data->obj) : -1);
    return 0;
}

static void register_on(PLI_INT32 reason, vpiHandle object,
    PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.obj = object;
    callback.cb_rtn = routine;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c register %d failed\n", (int)reason);
    }
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle function = vpi_handle_by_name("top.u.h", NULL);
    vpiHandle body = function != NULL ? vpi_handle(vpiStmt, function) : NULL;
    vpiHandle first = NULL;
    (void)data;
    if (body != NULL && vpi_get(vpiType, body) == vpiBegin) {
        vpiHandle statements = vpi_iterate(vpiStmt, body);
        first = statements != NULL ? vpi_scan(statements) : NULL;
        if (first != NULL) {
            vpi_free_object(statements);
        }
    } else {
        first = body;
    }
    register_on(cbStartOfFrame, NULL, frame_event);
    register_on(cbEndOfFrame, NULL, frame_event);
    register_on(cbStartOfFrame, function, callee_frame);
    register_on(cbStmt, first, statement);
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

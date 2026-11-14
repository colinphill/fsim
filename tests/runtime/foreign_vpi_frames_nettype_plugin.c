// SPDX-License-Identifier: Apache-2.0
// VPI conformance plug-in for the frames of nettype resolution functions
// (IEEE 1800-2023 37.43 detail 6, 38.36.1): each frame's origin is the net
// it resolves, and it has no thread. Run with --foreign-probes. Lines
// starting with "#c " form the checked trace.
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
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    if (origin != NULL && vpi_get(vpiType, origin) == vpiNet) {
        vpi_get_value(origin, &value);
    }
    vpi_printf("#c t=%d %s origin=%s origin-type=%s value=%d thread=%s\n",
        now(), data->reason == cbStartOfFrame ? "start" : "end",
        name_of(vpiFullName, origin), name_of(vpiType, origin),
        (int)value.value.integer,
        frame != NULL && vpi_handle(vpiThread, frame) != NULL ? "set"
                                                              : "null");
    return 0;
}

static void register_on(PLI_INT32 reason, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = routine;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c register %d failed\n", (int)reason);
    }
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    register_on(cbStartOfFrame, frame_event);
    register_on(cbEndOfFrame, frame_event);
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

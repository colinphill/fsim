// SPDX-License-Identifier: Apache-2.0
// cbStmt and cbStartOfFrame over the tasks and functions of two instances of
// one module (IEEE 1800-2023 38.36.1.1, 37.43): places a statement callback
// on the body of each instance's task and function, and a frame callback on
// every frame, and reports the instance each one belongs to. Lines starting
// with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "(null)";
}

static PLI_INT32 statement(p_cb_data data)
{
    vpi_printf("#c cbStmt %s line %d t=%u\n", text(data->user_data),
        (int)vpi_get(vpiLineNo, data->obj),
        data->time != NULL ? (unsigned)data->time->low : 0U);
    return 0;
}

// The task or function a frame runs: its origin call's (37.43).
static PLI_INT32 frame(p_cb_data data)
{
    vpiHandle origin = vpi_handle(vpiOrigin, data->obj);
    vpiHandle callee = NULL;
    if (origin != NULL) {
        callee = vpi_handle(
            vpi_get(vpiType, origin) == vpiFuncCall ? vpiFunction : vpiTask,
            origin);
    }
    vpi_printf("#c cbStartOfFrame %s origin=%d %s t=%u\n",
        callee != NULL ? text(vpi_get_str(vpiFullName, callee)) : "none",
        origin != NULL ? (int)vpi_get(vpiType, origin) : 0,
        origin != NULL ? text(vpi_get_str(vpiName, origin)) : "-",
        data->time != NULL ? (unsigned)data->time->low : 0U);
    return 0;
}

static void place(const char* path, PLI_BYTE8* label)
{
    static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpiHandle callable = vpi_handle_by_name((PLI_BYTE8*)path, NULL);
    vpiHandle body = callable != NULL ? vpi_handle(vpiStmt, callable) : NULL;
    s_cb_data data;
    if (body == NULL) {
        vpi_printf("#c %s has no statement\n", path);
        return;
    }
    memset(&data, 0, sizeof data);
    data.reason = cbStmt;
    data.cb_rtn = statement;
    data.obj = body;
    data.time = &time;
    data.user_data = label;
    vpi_printf("#c cbStmt on %s accepted=%d\n", path,
        vpi_register_cb(&data) != NULL);
}

static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    s_cb_data frames;
    (void)data;
    place("top.a.bump", "a.bump");
    place("top.b.bump", "b.bump");
    place("top.a.twice", "a.twice");
    place("top.b.twice", "b.twice");
    memset(&frames, 0, sizeof frames);
    frames.reason = cbStartOfFrame;
    frames.cb_rtn = frame;
    frames.time = &time;
    vpi_printf("#c cbStartOfFrame accepted=%d\n",
        vpi_register_cb(&frames) != NULL);
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

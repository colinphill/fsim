// SPDX-License-Identifier: Apache-2.0
// cbStmt conformance plug-in (IEEE 1800-2023 38.36.1.1): places a callback
// on top.main, each of its statements and the body of task bump, and
// reports each call. Without simulate --foreign-probes the registration
// fails with an error. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* type_name(PLI_INT32 type)
{
    switch (type) {
    case vpiNamedBegin:
        return "vpiNamedBegin";
    case vpiDisable:
        return "vpiDisable";
    case vpiBegin:
        return "vpiBegin";
    case vpiAssignment:
        return "vpiAssignment";
    case vpiFor:
        return "vpiFor";
    case vpiWhile:
        return "vpiWhile";
    case vpiRepeat:
        return "vpiRepeat";
    case vpiDelayControl:
        return "vpiDelayControl";
    case vpiTaskCall:
        return "vpiTaskCall";
    case vpiEventControl:
        return "vpiEventControl";
    default:
        return "other";
    }
}

static PLI_INT32 statement(p_cb_data data)
{
    vpi_printf("#c cbStmt %s line %d t=%u time=%s value=%s index=%d\n",
        type_name(vpi_get(vpiType, data->obj)),
        (int)vpi_get(vpiLineNo, data->obj),
        data->time != NULL ? (unsigned)data->time->low : 0U,
        data->time != NULL ? "set" : "null",
        data->value != NULL ? "set" : "null", (int)data->index);
    return 0;
}

static PLI_INT32 disabled(p_cb_data data)
{
    vpi_printf("#c cbDisable %s %s t=%u\n",
        type_name(vpi_get(vpiType, data->obj)),
        vpi_get_str(vpiFullName, data->obj),
        data->time != NULL ? (unsigned)data->time->low : 0U);
    return 0;
}

static int place(vpiHandle object)
{
    static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    s_cb_data data;
    s_vpi_error_info error;
    memset(&data, 0, sizeof data);
    data.reason = cbStmt;
    data.cb_rtn = statement;
    data.obj = object;
    data.time = &time;
    if (vpi_register_cb(&data) != NULL) {
        return 1;
    }
    memset(&error, 0, sizeof error);
    if (vpi_chk_error(&error) != 0) {
        vpi_printf("#c refused %s: %s\n", type_name(vpi_get(vpiType, object)),
            error.message != NULL ? error.message : "");
    } else {
        vpi_printf("#c refused %s without an error\n",
            type_name(vpi_get(vpiType, object)));
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle main_block = vpi_handle_by_name("top.main", NULL);
    vpiHandle task = vpi_handle_by_name("top.bump", NULL);
    vpiHandle iterator;
    vpiHandle item;
    s_cb_data suppressed;
    (void)data;
    if (main_block == NULL || task == NULL) {
        vpi_printf("#c missing scopes\n");
        return 0;
    }
    if (!place(main_block)) {
        return 0;
    }
    iterator = vpi_iterate(vpiStmt, main_block);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        (void)place(item);
    }
    (void)place(vpi_handle(vpiStmt, task));
    {
        static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
        s_cb_data disable;
        memset(&disable, 0, sizeof disable);
        disable.reason = cbDisable;
        disable.cb_rtn = disabled;
        disable.obj = vpi_handle_by_name("top.worker", NULL);
        disable.time = &time;
        vpi_printf("#c cbDisable accepted=%d\n",
            vpi_register_cb(&disable) != NULL);
    }
    // A process is not a statement.
    iterator = vpi_iterate(vpiProcess, vpi_handle_by_name("top", NULL));
    item = iterator != NULL ? vpi_scan(iterator) : NULL;
    memset(&suppressed, 0, sizeof suppressed);
    suppressed.reason = cbStmt;
    suppressed.cb_rtn = statement;
    suppressed.obj = item;
    vpi_printf("#c process accepted=%d\n",
        item != NULL && vpi_register_cb(&suppressed) != NULL);
    if (iterator != NULL) {
        (void)vpi_release_handle(iterator);
    }
    // The statement of `always @(posedge g)` is its event control (37.44),
    // reached at each activation of the process.
    iterator = vpi_iterate(vpiProcess, vpi_handle_by_name("top", NULL));
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        if (vpi_get(vpiType, item) == vpiAlways) {
            (void)place(vpi_handle(vpiStmt, item));
            (void)vpi_release_handle(iterator);
            break;
        }
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

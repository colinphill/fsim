// SPDX-License-Identifier: Apache-2.0
// cbStmt conformance plug-in (IEEE 1800-2023 38.36.1.1): places a callback
// on top.main, each of its statements and the body of task bump, on the
// block and statement of a generate block's process, and reports each call. Without simulate --foreign-probes the registration
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
    case vpiSysTaskCall:
        return "vpiSysTaskCall";
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

// A system task call hears of the disabling of the named block that contains
// it (38.36.1): obj stays the call.
static PLI_INT32 disabled_call(p_cb_data data)
{
    vpi_printf("#c cbDisable call %s t=%u\n",
        type_name(vpi_get(vpiType, data->obj)),
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
    // A disable of a block that is not active reports nothing.
    {
        static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
        s_cb_data disable;
        memset(&disable, 0, sizeof disable);
        disable.reason = cbDisable;
        disable.cb_rtn = disabled;
        disable.obj = vpi_handle_by_name("top.idle", NULL);
        disable.time = &time;
        vpi_printf("#c cbDisable idle accepted=%d\n",
            vpi_register_cb(&disable) != NULL);
    }
    // A system task call in the block `worker`, disabled at time 2 while the
    // block waits at its second delay (38.36.1, Table 38-3).
    {
        static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
        s_cb_data disable;
        vpiHandle call = NULL;
        vpiHandle worker = vpi_handle_by_name("top.worker", NULL);
        vpiHandle statements = vpi_iterate(vpiStmt, worker);
        while (statements != NULL && (item = vpi_scan(statements)) != NULL) {
            if (vpi_get(vpiType, item) == vpiSysTaskCall) {
                call = item;
                vpi_free_object(statements);
                break;
            }
        }
        memset(&disable, 0, sizeof disable);
        disable.reason = cbDisable;
        disable.cb_rtn = disabled_call;
        disable.obj = call;
        disable.time = &time;
        vpi_printf("#c cbDisable call accepted=%d\n",
            call != NULL && vpi_register_cb(&disable) != NULL);
    }
    // The statements of a process of a generate block.
    {
        vpiHandle generated = vpi_handle_by_name("top.gen.gblk", NULL);
        if (generated == NULL) {
            vpi_printf("#c missing generate scope\n");
        } else if (place(generated)) {
            iterator = vpi_iterate(vpiStmt, generated);
            while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
                (void)place(item);
            }
        }
    }
    // The named block of an unlabelled generate block, beneath the scope
    // genblk<n> (IEEE 1800-2023 27.6, 37.43).
    {
        vpiHandle scopes = vpi_iterate(vpiInternalScope,
            vpi_handle_by_name("top", NULL));
        vpiHandle scope;
        int found = 0;
        while (scopes != NULL && (scope = vpi_scan(scopes)) != NULL) {
            const char* name = vpi_get_str(vpiName, scope);
            char path[128];
            vpiHandle unlabelled;
            if (name == NULL || strncmp(name, "genblk", 6U) != 0) {
                continue;
            }
            snprintf(path, sizeof path, "top.%s.ublk", name);
            unlabelled = vpi_handle_by_name(path, NULL);
            vpi_printf("#c implicit scope %s block=%d\n", name,
                unlabelled != NULL);
            found = 1;
            if (unlabelled != NULL && place(unlabelled)) {
                iterator = vpi_iterate(vpiStmt, unlabelled);
                while (iterator != NULL
                    && (item = vpi_scan(iterator)) != NULL) {
                    (void)place(item);
                }
            }
        }
        if (!found) {
            vpi_printf("#c missing implicit scope\n");
        }
    }
    // A generate scope created with an asserted process holds its named
    // blocks too.
    {
        vpiHandle eager = vpi_handle_by_name("top.eager.eblk", NULL);
        if (eager == NULL) {
            vpi_printf("#c missing eager generate block\n");
        } else if (place(eager)) {
            iterator = vpi_iterate(vpiStmt, eager);
            while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
                (void)place(item);
            }
        }
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

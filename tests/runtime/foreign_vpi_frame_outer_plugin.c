// SPDX-License-Identifier: Apache-2.0
// Frame conformance plug-in for an automatic event and the strings of the
// outer activations of a recursive task (IEEE 1800-2023 37.43, 38.34): a put
// to the automatic event of ev_task triggers it for the activation that owns
// it, and at each activation of rec the string of every frame of the chain is
// listed; the outermost is put once the chain is three deep. Lines starting
// with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static vpiHandle event_handle;

static PLI_INT32 trigger(p_cb_data data)
{
    s_vpi_value value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vpiScalarVal;
    value.value.scalar = vpi1;
    vpi_put_value(event_handle, &value, NULL, vpiNoDelay);
    vpi_printf("#c event put\n");
    return 0;
}

static PLI_INT32 at_wait(p_cb_data data)
{
    vpiHandle frame = vpi_handle(vpiFrame, NULL);
    vpiHandle iterator = vpi_iterate(vpiAutomatics, frame);
    vpiHandle variable;
    s_cb_data later;
    s_vpi_time delay;
    (void)data;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        if (strcmp(vpi_get_str(vpiName, variable), "e") == 0) {
            event_handle = variable;
            vpi_printf("#c event %s\n", vpi_get_str(vpiType, variable));
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

static PLI_INT32 in_rec(p_cb_data data)
{
    vpiHandle frame;
    vpiHandle outermost = NULL;
    int depth = 0;
    (void)data;
    for (frame = vpi_handle(vpiFrame, NULL); frame != NULL;
         frame = vpi_handle(vpiParent, frame)) {
        vpiHandle iterator = vpi_iterate(vpiAutomatics, frame);
        vpiHandle variable;
        while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
            if (strcmp(vpi_get_str(vpiName, variable), "s") == 0) {
                s_vpi_value value;
                memset(&value, 0, sizeof value);
                value.format = vpiStringVal;
                vpi_get_value(variable, &value);
                vpi_printf("#c depth %d s=%s\n", depth, value.value.str);
                outermost = variable;
            }
        }
        depth++;
    }
    if (depth == 3 && outermost != NULL) {
        s_vpi_value value;
        memset(&value, 0, sizeof value);
        value.format = vpiStringVal;
        value.value.str = (PLI_BYTE8*)"patched";
        vpi_put_value(outermost, &value, NULL, vpiNoDelay);
        vpi_printf("#c put outermost\n");
    }
    return 0;
}

// The statement of a task body that begins on `line`.
static vpiHandle statement_at(const char* task, int line)
{
    vpiHandle module = vpi_handle_by_name("top", NULL);
    vpiHandle iterator = vpi_iterate(vpiTask, module);
    vpiHandle callable;
    while (iterator != NULL && (callable = vpi_scan(iterator)) != NULL) {
        if (strcmp(vpi_get_str(vpiName, callable), task) == 0) {
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

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStmt;
    callback.obj = statement_at("ev_task", 13);
    callback.cb_rtn = at_wait;
    vpi_register_cb(&callback);
    callback.obj = statement_at("rec", 21);
    callback.cb_rtn = in_rec;
    vpi_register_cb(&callback);
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

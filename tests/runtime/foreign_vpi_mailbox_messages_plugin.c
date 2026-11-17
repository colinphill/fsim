// SPDX-License-Identifier: Apache-2.0
// VPI conformance plug-in for the messages of a mailbox (IEEE 1800-2023
// 37.33 detail 4): each message in order, its constant type and value; a
// message of an unpacked structure type has no one value (37.26 detail 1,
// 38.15), so vpi_get_value reports an error.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static void messages(PLI_BYTE8* name)
{
    vpiHandle object = vpi_handle(vpiClassObj, vpi_handle_by_name(name, NULL));
    vpiHandle iterator = object != NULL ? vpi_iterate(vpiMessages, object) : NULL;
    vpiHandle message;
    int count = 0;
    while (iterator != NULL && (message = vpi_scan(iterator)) != NULL) {
        s_vpi_value value;
        s_vpi_error_info error;
        int failed;
        memset(&value, 0, sizeof value);
        value.format = vpiObjTypeVal;
        vpi_get_value(message, &value);
        failed = vpi_chk_error(&error) != 0;
        vpi_printf("#c %s message type=%s const-type=%d error=%d",
            name, text(vpi_get_str(vpiType, message)),
            (int)vpi_get(vpiConstType, message), failed);
        if (!failed && value.format == vpiStringVal) {
            vpi_printf(" string=%s", text(value.value.str));
        } else if (!failed && value.format == vpiIntVal) {
            vpi_printf(" int=%d", (int)value.value.integer);
        }
        vpi_printf("\n");
        ++count;
    }
    vpi_printf("#c %s messages=%d\n", name, count);
}

static PLI_INT32 check(p_cb_data data)
{
    (void)data;
    messages("top.any");
    messages("top.tags");
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = check;
    callback.time = &time;
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

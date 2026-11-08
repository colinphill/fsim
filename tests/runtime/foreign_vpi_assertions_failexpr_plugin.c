// SPDX-License-Identifier: Apache-2.0
// Prints the failing expression of each failed attempt (IEEE 1800-2023 39.4.2:
// on a cbAssertionFailure callback detail.failExpr is valid). A plain name is
// the object it denotes, so its full name tells which boolean of the property
// rejected the attempt. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static PLI_INT32 failed(PLI_INT32 reason, p_vpi_time time, vpiHandle assertion,
    p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    const char* const name = vpi_get_str(vpiName, assertion);
    const char* expression = "none";
    (void)reason;
    (void)user_data;
    if (info != NULL && info->detail.failExpr != NULL) {
        const char* const full = vpi_get_str(vpiFullName, info->detail.failExpr);
        expression = full != NULL ? full : "unnamed";
    }
    vpi_printf("#c %u failure %s start=%u expr=%s\n", (unsigned)time->low,
        name != NULL ? name : "", info != NULL ? (unsigned)info->attemptStartTime.low : 0U,
        expression);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static const char* const names[] = { "top.p_imp", "top.p_seq", "top.p_not" };
    size_t index;
    (void)data;
    for (index = 0; index < sizeof names / sizeof names[0]; ++index) {
        vpiHandle const handle = vpi_handle_by_name((PLI_BYTE8*)names[index], NULL);
        if (handle == NULL) {
            vpi_printf("#c missing %s\n", names[index]);
            continue;
        }
        (void)vpi_register_assertion_cb(handle, cbAssertionFailure, failed, NULL);
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

// SPDX-License-Identifier: Apache-2.0
// Prints the attempt callbacks of p_dis and p_eos with the time each attempt
// started (IEEE 1800-2023 39.4.2: cbAssertionStart, cbAssertionSuccess,
// cbAssertionFailure and cbAssertionDisabledEvaluation carry attemptStartTime).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* reason_name(PLI_INT32 reason)
{
    switch (reason) {
    case cbAssertionStart:
        return "start";
    case cbAssertionSuccess:
        return "success";
    case cbAssertionFailure:
        return "failure";
    case cbAssertionDisabledEvaluation:
        return "disabled-evaluation";
    default:
        return "?";
    }
}

static PLI_INT32 observed(PLI_INT32 reason, p_vpi_time time,
    vpiHandle assertion, p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    const char* const name = vpi_get_str(vpiName, assertion);
    (void)user_data;
    if (info == NULL) {
        vpi_printf("#c %u %s %s info=no\n", (unsigned)time->low,
            reason_name(reason), name != NULL ? name : "");
        return 0;
    }
    vpi_printf("#c %u %s %s start=%u\n", (unsigned)time->low,
        reason_name(reason), name != NULL ? name : "",
        (unsigned)info->attemptStartTime.low);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static const char* const names[] = { "top.p_dis", "top.p_eos" };
    static const PLI_INT32 reasons[] = { cbAssertionStart, cbAssertionSuccess,
        cbAssertionFailure, cbAssertionDisabledEvaluation };
    size_t assertion;
    size_t reason;
    (void)data;
    for (assertion = 0; assertion < sizeof names / sizeof names[0]; ++assertion) {
        vpiHandle const handle
            = vpi_handle_by_name((PLI_BYTE8*)names[assertion], NULL);
        if (handle == NULL) {
            vpi_printf("#c missing %s\n", names[assertion]);
            continue;
        }
        for (reason = 0; reason < sizeof reasons / sizeof reasons[0]; ++reason) {
            (void)vpi_register_assertion_cb(
                handle, reasons[reason], observed, NULL);
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

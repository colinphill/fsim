// SPDX-License-Identifier: Apache-2.0
// Assertion step callbacks (IEEE 1800-2023 39.4.2, 39.5.2): enables stepping
// for the attempts of p_step that start at 5, 15, 25 and 35 at the start of
// simulation, disables it for 25 at once and tries to disable it for 15 at
// time 20, while that attempt runs: the stepping mode of an attempt cannot
// change after the attempt has started (39.5.2), so that control fails and
// vpi_chk_error reports it. Every step callback is printed with the
// expression it matched (matched_exprs, 39.4.2). Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static vpiHandle assertion_handle;

static PLI_INT32 observed(PLI_INT32 reason, p_vpi_time time,
    vpiHandle assertion, p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    const char* const name = vpi_get_str(vpiName, assertion);
    (void)user_data;
    if (info == NULL) {
        vpi_printf("#c %u missing info\n", (unsigned)time->low);
        return 0;
    }
    vpi_printf("#c %u %s %s start=%u from=%d to=%d matched=%d %s\n",
        (unsigned)time->low,
        reason == cbAssertionStepSuccess ? "step-success" : "step-failure",
        name != NULL ? name : "", (unsigned)info->attemptStartTime.low,
        (int)info->detail.step->stateFrom, (int)info->detail.step->stateTo,
        (int)info->detail.step->matched_expression_count,
        info->detail.step->matched_expression_count > 0
                && info->detail.step->matched_exprs != NULL
                && info->detail.step->matched_exprs[0] != NULL
            ? vpi_get_str(vpiFullName, info->detail.step->matched_exprs[0])
            : "-");
    return 0;
}

static void control(PLI_INT32 operation, unsigned start)
{
    s_vpi_time when;
    memset(&when, 0, sizeof when);
    when.type = vpiSimTime;
    when.low = start;
    if (operation == vpiAssertionEnableStep) {
        (void)vpi_control(operation, assertion_handle, &when,
            vpiAssertionClockSteps);
    } else {
        (void)vpi_control(operation, assertion_handle, &when);
    }
}

static PLI_INT32 disable_fifteen(p_cb_data data)
{
    s_vpi_error_info error;
    s_vpi_time when;
    (void)data;
    memset(&when, 0, sizeof when);
    when.type = vpiSimTime;
    when.low = 15;
    (void)vpi_control(vpiAssertionDisableStep, assertion_handle, &when);
    memset(&error, 0, sizeof error);
    if (vpi_chk_error(&error) != 0) {
        vpi_printf("#c 20 disable 15 refused: %s\n",
            error.message != NULL ? error.message : "");
    } else {
        vpi_printf("#c 20 disable 15 accepted\n");
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data delayed;
    s_vpi_time when;
    (void)data;
    assertion_handle = vpi_handle_by_name((PLI_BYTE8*)"top.p_step", NULL);
    if (assertion_handle == NULL) {
        vpi_printf("#c missing top.p_step\n");
        return 0;
    }
    (void)vpi_register_assertion_cb(
        assertion_handle, cbAssertionStepSuccess, observed, NULL);
    control(vpiAssertionEnableStep, 5);
    control(vpiAssertionEnableStep, 15);
    control(vpiAssertionEnableStep, 25);
    control(vpiAssertionEnableStep, 35);
    control(vpiAssertionDisableStep, 25);
    memset(&delayed, 0, sizeof delayed);
    memset(&when, 0, sizeof when);
    when.type = vpiSimTime;
    when.low = 20;
    delayed.reason = cbAfterDelay;
    delayed.cb_rtn = disable_fifteen;
    delayed.time = &when;
    (void)vpi_register_cb(&delayed);
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

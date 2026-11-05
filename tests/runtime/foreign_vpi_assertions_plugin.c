// SPDX-License-Identifier: Apache-2.0
// Assertion API conformance plug-in (IEEE 1800-2023 39): lists the
// concurrent assertions, follows a_req and c_hit through their attempt and
// control callbacks, and controls them at set times. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static vpiHandle a_req;
static vpiHandle c_hit;
static vpiHandle a_seq;
static vpiHandle seq_callbacks[4];

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = vpi_get_str(property, object);
    return value != NULL ? value : "";
}

static const char* reason_name(PLI_INT32 reason)
{
    switch (reason) {
    case cbAssertionStart:
        return "start";
    case cbAssertionSuccess:
        return "success";
    case cbAssertionFailure:
        return "failure";
    case cbAssertionVacuousSuccess:
        return "vacuous";
    case cbAssertionDisabledEvaluation:
        return "disabled-evaluation";
    case cbAssertionDisable:
        return "disable";
    case cbAssertionEnable:
        return "enable";
    case cbAssertionLock:
        return "lock";
    case cbAssertionUnlock:
        return "unlock";
    case cbAssertionKill:
        return "kill";
    case cbAssertionReset:
        return "reset";
    case cbAssertionDisablePassAction:
        return "disable-pass-action";
    case cbAssertionEnablePassAction:
        return "enable-pass-action";
    case cbAssertionSysInitialized:
        return "sys-initialized";
    case cbAssertionSysOn:
        return "sys-on";
    case cbAssertionSysOff:
        return "sys-off";
    case cbAssertionSysEnd:
        return "sys-end";
    default:
        return "?";
    }
}

static PLI_INT32 observed(PLI_INT32 reason, p_vpi_time time,
    vpiHandle assertion, p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    (void)user_data;
    vpi_printf("#c %u %s %s info=%s\n",
        time != NULL ? (unsigned)time->low : 0U, reason_name(reason),
        text(vpiName, assertion), info != NULL ? "yes" : "no");
    return 0;
}

static PLI_INT32 system_changed(p_cb_data data)
{
    s_vpi_time now;
    memset(&now, 0, sizeof now);
    now.type = vpiSimTime;
    vpi_get_time(NULL, &now);
    vpi_printf("#c %u %s\n", (unsigned)now.low, reason_name(data->reason));
    return 0;
}

// Each control at its time, with the result vpi_control returns.
struct Step {
    unsigned time;
    PLI_INT32 operation;
    int on_cover;
    const char* name;
};

static const struct Step steps[] = {
    { 14U, vpiAssertionDisablePassAction, 0, "disable-pass-action" },
    { 20U, vpiAssertionEnablePassAction, 0, "enable-pass-action" },
    { 40U, vpiAssertionLock, 0, "lock" },
    { 40U, vpiAssertionDisable, 0, "disable-while-locked" },
    { 40U, vpiAssertionUnlock, 0, "unlock" },
    { 40U, vpiAssertionDisable, 0, "disable" },
    { 50U, vpiAssertionEnable, 0, "enable" },
    { 51U, vpiAssertionKill, 1, "kill" },
    { 58U, vpiAssertionSysOff, -1, "sys-off" },
    { 68U, vpiAssertionSysOn, -1, "sys-on" },
};

static PLI_INT32 control(p_cb_data data)
{
    const struct Step* const step = (const struct Step*)data->user_data;
    vpiHandle const target = step->on_cover < 0 ? NULL
        : step->on_cover                         ? c_hit
                                                 : a_req;
    s_vpi_time start;
    memset(&start, 0, sizeof start);
    start.type = vpiSimTime;
    const PLI_INT32 status = step->operation == vpiAssertionKill
        ? vpi_control(step->operation, target, &start)
        : vpi_control(step->operation, target);
    vpi_printf("#c %u control %s -> %d\n", step->time, step->name, (int)status);
    return 0;
}

static void at(const struct Step* step)
{
    s_vpi_time delay;
    s_cb_data data;
    memset(&delay, 0, sizeof delay);
    memset(&data, 0, sizeof data);
    delay.type = vpiSimTime;
    delay.low = step->time;
    data.reason = cbAfterDelay;
    data.cb_rtn = control;
    data.time = &delay;
    data.user_data = (PLI_BYTE8*)step;
    (void)vpi_register_cb(&data);
}

static void list(vpiHandle scope, const char* label)
{
    vpiHandle const iterator = vpi_iterate(vpiAssertion, scope);
    vpiHandle assertion;
    while (iterator != NULL && (assertion = vpi_scan(iterator)) != NULL) {
        vpiHandle const parent = vpi_handle(vpiScope, assertion);
        vpi_printf("#c %s %s name=%s full=%s line=%d scope=%s\n", label,
            text(vpiType, assertion), text(vpiName, assertion),
            text(vpiFullName, assertion), (int)vpi_get(vpiLineNo, assertion),
            parent != NULL ? text(vpiFullName, parent) : "-");
        if (strstr(text(vpiFile, assertion), "foreign_vpi_assertions.sv") == NULL) {
            vpi_printf("#c %s has no file\n", text(vpiName, assertion));
        }
    }
}

// Attempt starts and outcomes of a_seq with the time each attempt started
// (39.4.2 attemptStartTime, the attempt's identity).
static PLI_INT32 attempt_observed(PLI_INT32 reason, p_vpi_time time,
    vpiHandle assertion, p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    (void)user_data;
    if (info == NULL) {
        vpi_printf("#c %u %s %s info=no\n", (unsigned)time->low,
            reason_name(reason), text(vpiName, assertion));
    } else {
        vpi_printf("#c %u %s %s start=%u\n", (unsigned)time->low,
            reason_name(reason), text(vpiName, assertion),
            (unsigned)info->attemptStartTime.low);
    }
    return 0;
}

// vpiAssertionKill takes an attempt start time and discards that attempt only.
static PLI_INT32 kill_attempt(p_cb_data data)
{
    s_vpi_time started;
    (void)data;
    memset(&started, 0, sizeof started);
    started.type = vpiSimTime;
    started.low = 15U;
    vpi_printf("#c 26 control kill-attempt-15 -> %d\n",
        (int)vpi_control(vpiAssertionKill, a_seq, &started));
    return 0;
}

static PLI_INT32 remove_seq_callbacks(p_cb_data data)
{
    int removed = 0;
    size_t index;
    (void)data;
    for (index = 0; index < sizeof seq_callbacks / sizeof seq_callbacks[0];
         ++index) {
        removed += (int)vpi_remove_cb(seq_callbacks[index]);
    }
    vpi_printf("#c 56 removed %d\n", removed);
    return 0;
}

static void after(unsigned time, PLI_INT32 (*routine)(p_cb_data))
{
    s_vpi_time delay;
    s_cb_data data;
    memset(&delay, 0, sizeof delay);
    memset(&data, 0, sizeof data);
    delay.type = vpiSimTime;
    delay.low = time;
    data.reason = cbAfterDelay;
    data.cb_rtn = routine;
    data.time = &delay;
    (void)vpi_register_cb(&data);
}

static void properties(vpiHandle assertion, const char* name)
{
    vpi_printf("#c props %s clock-inferred=%d cover-sequence=%d\n", name,
        (int)vpi_get(vpiIsClockInferred, assertion),
        (int)vpi_get(vpiIsCoverSequence, assertion));
}

static void sequence_assertions(vpiHandle top)
{
    static const PLI_INT32 reasons[] = { cbAssertionStart, cbAssertionSuccess,
        cbAssertionFailure, cbAssertionKill };
    size_t index;
    a_seq = vpi_handle_by_name("top.a_seq", NULL);
    properties(a_req, "a_req");
    properties(c_hit, "c_hit");
    properties(a_seq, "a_seq");
    properties(vpi_handle_by_name("cs_hit", top), "cs_hit");
    for (index = 0; index < sizeof reasons / sizeof reasons[0]; ++index) {
        seq_callbacks[index] = vpi_register_assertion_cb(
            a_seq, reasons[index], attempt_observed, NULL);
    }
    after(26U, kill_attempt);
    after(56U, remove_seq_callbacks);
}

static PLI_INT32 start(p_cb_data data)
{
    static const PLI_INT32 attempts[] = { cbAssertionSuccess,
        cbAssertionFailure, cbAssertionVacuousSuccess,
        cbAssertionDisabledEvaluation, cbAssertionDisable, cbAssertionEnable,
        cbAssertionLock, cbAssertionUnlock, cbAssertionKill,
        cbAssertionDisablePassAction, cbAssertionEnablePassAction };
    vpiHandle const top = vpi_handle_by_name("top", NULL);
    size_t index;
    (void)data;
    list(NULL, "design");
    list(top, "top");
    a_req = vpi_handle_by_name("top.a_req", NULL);
    c_hit = vpi_handle_by_name("c_hit", top);
    vpi_printf("#c lookup a_req=%s c_hit=%s\n", text(vpiType, a_req),
        text(vpiType, c_hit));
    vpi_printf("#c start-callback=%s\n",
        vpi_register_assertion_cb(a_req, cbAssertionStart, observed, NULL)
                != NULL
            ? "placed"
            : "refused");
    for (index = 0; index < sizeof attempts / sizeof attempts[0]; ++index) {
        (void)vpi_register_assertion_cb(a_req, attempts[index], observed, NULL);
    }
    (void)vpi_register_assertion_cb(c_hit, cbAssertionSuccess, observed, NULL);
    (void)vpi_register_assertion_cb(c_hit, cbAssertionKill, observed, NULL);
    for (index = 0; index < sizeof steps / sizeof steps[0]; ++index) {
        at(&steps[index]);
    }
    sequence_assertions(top);
    return 0;
}

// These reasons cannot be registered from a startup routine (IEEE 1800-2023
// 36.10.2), so the end of compile registers them.
static PLI_INT32 register_system_callbacks(p_cb_data compiled)
{
    static const PLI_INT32 reasons[] = { cbAssertionSysInitialized,
        cbAssertionSysOn, cbAssertionSysOff, cbAssertionSysEnd };
    s_cb_data data;
    size_t index;
    (void)compiled;
    for (index = 0; index < sizeof reasons / sizeof reasons[0]; ++index) {
        memset(&data, 0, sizeof data);
        data.reason = reasons[index];
        data.cb_rtn = system_changed;
        (void)vpi_register_cb(&data);
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
    memset(&data, 0, sizeof data);
    data.reason = cbEndOfCompile;
    data.cb_rtn = register_system_callbacks;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

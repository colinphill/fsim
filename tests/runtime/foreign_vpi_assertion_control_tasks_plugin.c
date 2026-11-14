// SPDX-License-Identifier: Apache-2.0
// The VPI callbacks of the design's assertion control tasks (IEEE 1800-2023
// 20.11 Table 20-8): every assertion system reason and every control reason
// of a1 and c1 prints its time. Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

struct Name {
    PLI_INT32 reason;
    const char* name;
};

// The first SYSTEM_NAMES entries are assertion system reasons (39.4.1), the
// rest the control reasons of one assertion (39.4.2).
#define SYSTEM_NAMES 14U

static const struct Name names[] = {
    { cbAssertionSysInitialized, "sys-initialized" },
    { cbAssertionSysOn, "sys-on" },
    { cbAssertionSysOff, "sys-off" },
    { cbAssertionSysKill, "sys-kill" },
    { cbAssertionSysLock, "sys-lock" },
    { cbAssertionSysUnlock, "sys-unlock" },
    { cbAssertionSysEnd, "sys-end" },
    { cbAssertionSysReset, "sys-reset" },
    { cbAssertionSysEnablePassAction, "sys-enable-pass-action" },
    { cbAssertionSysEnableFailAction, "sys-enable-fail-action" },
    { cbAssertionSysDisablePassAction, "sys-disable-pass-action" },
    { cbAssertionSysDisableFailAction, "sys-disable-fail-action" },
    { cbAssertionSysEnableNonvacuousAction, "sys-enable-nonvacuous-action" },
    { cbAssertionSysDisableVacuousAction, "sys-disable-vacuous-action" },
    { cbAssertionLock, "lock" },
    { cbAssertionUnlock, "unlock" },
    { cbAssertionDisable, "disable" },
    { cbAssertionEnable, "enable" },
    { cbAssertionReset, "reset" },
    { cbAssertionKill, "kill" },
    { cbAssertionEnablePassAction, "enable-pass-action" },
    { cbAssertionEnableFailAction, "enable-fail-action" },
    { cbAssertionDisablePassAction, "disable-pass-action" },
    { cbAssertionDisableFailAction, "disable-fail-action" },
    { cbAssertionEnableNonvacuousAction, "enable-nonvacuous-action" },
    { cbAssertionDisableVacuousAction, "disable-vacuous-action" },
};

static const char* reason_name(PLI_INT32 reason)
{
    size_t index;
    for (index = 0; index < sizeof names / sizeof names[0]; ++index) {
        if (names[index].reason == reason) {
            return names[index].name;
        }
    }
    return "?";
}

static unsigned now(void)
{
    s_vpi_time time;
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    vpi_get_time(NULL, &time);
    return (unsigned)time.low;
}

static PLI_INT32 controlled(PLI_INT32 reason, p_vpi_time time,
    vpiHandle assertion, p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    const char* const name = vpi_get_str(vpiName, assertion);
    (void)time;
    (void)info;
    (void)user_data;
    vpi_printf("#c %u %s %s\n", now(), reason_name(reason),
        name != NULL ? name : "?");
    return 0;
}

static PLI_INT32 system_changed(p_cb_data data)
{
    vpi_printf("#c %u %s\n", now(), reason_name(data->reason));
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static const char* const assertions[] = { "top.a1", "top.c1" };
    size_t which;
    size_t index;
    (void)data;
    for (which = 0; which < 2; ++which) {
        vpiHandle const assertion
            = vpi_handle_by_name((PLI_BYTE8*)assertions[which], NULL);
        for (index = 0; index < sizeof names / sizeof names[0]; ++index) {
            if (index < SYSTEM_NAMES) {
                continue;
            }
            if (vpi_register_assertion_cb(assertion, names[index].reason,
                    controlled, NULL)
                == NULL) {
                vpi_printf("#c refused %s %s\n", names[index].name,
                    assertions[which]);
            }
        }
    }
    return 0;
}

// The system reasons cannot be registered from a startup routine (IEEE
// 1800-2023 36.10.2), so the end of compile registers them.
static PLI_INT32 register_system_callbacks(p_cb_data compiled)
{
    s_cb_data data;
    size_t index;
    (void)compiled;
    for (index = 0; index < sizeof names / sizeof names[0]; ++index) {
        if (index >= SYSTEM_NAMES) {
            continue;
        }
        memset(&data, 0, sizeof data);
        data.reason = names[index].reason;
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

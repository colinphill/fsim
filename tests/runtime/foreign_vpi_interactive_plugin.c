// SPDX-License-Identifier: Apache-2.0
// Conformance plug-in for the interactive-mode interface (IEEE 1800-2023
// 38.4 vpiSetInteractiveScope, 38.36.3 cbEnterInteractive,
// cbExitInteractive, cbInteractiveScopeChange). A batch `fsim simulate`
// never enters interactive mode: its $stop fires neither interactive callback.
// A debugger session loaded with the plug-in enters it at a stop and leaves
// it when a command resumes the run.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static int enters_seen;
static int exits_seen;

static vpiHandle register_reason(PLI_INT32 reason, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    return vpi_register_cb(&data);
}

static vpiHandle register_after(PLI_INT32 delay, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data data;
    s_vpi_time time;
    memset(&data, 0, sizeof data);
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    time.low = (PLI_UINT32)delay;
    data.reason = cbAfterDelay;
    data.cb_rtn = routine;
    data.time = &time;
    return vpi_register_cb(&data);
}

// Callbacks run once; each routine asks for the next one.
static PLI_INT32 entered(p_cb_data data)
{
    (void)data;
    ++enters_seen;
    vpi_printf("#c interactive enter\n");
    (void)register_reason(cbEnterInteractive, entered);
    return 0;
}

static PLI_INT32 exited(p_cb_data data)
{
    (void)data;
    ++exits_seen;
    vpi_printf("#c interactive exit\n");
    (void)register_reason(cbExitInteractive, exited);
    return 0;
}

static PLI_INT32 scope_changed(p_cb_data data)
{
    vpi_printf("#c scope change to %s\n", vpi_get_str(vpiFullName, data->obj));
    (void)register_reason(cbInteractiveScopeChange, scope_changed);
    return 0;
}

static PLI_INT32 second(p_cb_data data)
{
    int result;
    (void)data;
    result = (int)vpi_control(
        vpiSetInteractiveScope, vpi_handle_by_name("top", NULL));
    vpi_printf("#c scope set top result=%d\n", result);
    return 0;
}

static PLI_INT32 first(p_cb_data data)
{
    int result;
    (void)data;
    result = (int)vpi_control(
        vpiSetInteractiveScope, vpi_handle_by_name("top.c", NULL));
    vpi_printf("#c scope set c result=%d\n", result);
    // A signal is not a scope: the request fails and changes nothing.
    result = (int)vpi_control(
        vpiSetInteractiveScope, vpi_handle_by_name("top.v", NULL));
    vpi_printf("#c scope set signal result=%d error=%d\n", result,
        vpi_chk_error(NULL) != 0);
    (void)register_after(1, second);
    return 0;
}

static PLI_INT32 end_of_simulation(p_cb_data data)
{
    (void)data;
    vpi_printf("#c end enters=%d exits=%d\n", enters_seen, exits_seen);
    return 0;
}

// The interactive reasons are not among the six a startup routine may
// register (IEEE 1800-2023 36.10.2), so they register at the end of compile.
static PLI_INT32 end_of_compile(p_cb_data data)
{
    (void)data;
    (void)register_reason(cbEnterInteractive, entered);
    (void)register_reason(cbExitInteractive, exited);
    (void)register_reason(cbInteractiveScopeChange, scope_changed);
    (void)register_after(1, first);
    return 0;
}

static void startup(void)
{
    (void)register_reason(cbEndOfCompile, end_of_compile);
    (void)register_reason(cbEndOfSimulation, end_of_simulation);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

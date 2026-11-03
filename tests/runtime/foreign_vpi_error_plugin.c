// SPDX-License-Identifier: Apache-2.0
// VPI run-time error conformance plug-in (IEEE 1800-2023 38.36.3): a
// run-time error runs the cbError callbacks, which read it with
// vpi_chk_error, before the end of simulation. Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static unsigned long long now(void)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static PLI_INT32 run_error(p_cb_data data)
{
    s_vpi_error_info info;
    const char* const expected = "maximum delta-cycle count";
    PLI_INT32 level;
    (void)data;
    memset(&info, 0, sizeof info);
    level = vpi_chk_error(&info);
    vpi_printf("#c %llu cbError level=%d message=%s\n", now(), (int)level,
        info.message != NULL
                && strncmp(info.message, expected, strlen(expected)) == 0
            ? expected
            : "?");
    return 0;
}

static PLI_INT32 end(p_cb_data data)
{
    (void)data;
    vpi_printf("#c %llu end of simulation\n", now());
    return 0;
}

static void register_action(PLI_INT32 reason, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    (void)vpi_register_cb(&data);
}

static void startup(void)
{
    register_action(cbError, run_error);
    register_action(cbEndOfSimulation, end);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

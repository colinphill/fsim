// SPDX-License-Identifier: Apache-2.0
// Registers cbAssertionStart in the middle of the run (a cbAfterDelay
// callback at time 12), not at start of simulation: the attempt-start
// reports are bound by that first registration (IEEE 1800-2023 39.4.2), so
// only the attempts that start after it are reported.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <string.h>

static PLI_INT32 observed(PLI_INT32 reason, p_vpi_time time,
    vpiHandle assertion, p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    const char* const name = vpi_get_str(vpiName, assertion);
    (void)reason;
    (void)user_data;
    vpi_printf("#c %u start %s start=%u\n", (unsigned)time->low,
        name != NULL ? name : "",
        info != NULL ? (unsigned)info->attemptStartTime.low : 0U);
    return 0;
}

static PLI_INT32 late(p_cb_data data)
{
    s_vpi_time now;
    vpiHandle const handle = vpi_handle_by_name((PLI_BYTE8*)"top.p1", NULL);
    (void)data;
    memset(&now, 0, sizeof now);
    now.type = vpiSimTime;
    vpi_get_time(NULL, &now);
    if (handle == NULL) {
        vpi_printf("#c missing top.p1\n");
        return 0;
    }
    if (vpi_register_assertion_cb(handle, cbAssertionStart, observed, NULL)
        == NULL) {
        vpi_printf("#c register failed\n");
        return 0;
    }
    vpi_printf("#c %u registered\n", (unsigned)now.low);
    return 0;
}

/* A startup routine may not register cbAfterDelay (36.10.2, refused by the
   host), so the delay is requested once the simulation has started. */
static PLI_INT32 started(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time delay;
    (void)data;
    memset(&callback, 0, sizeof callback);
    memset(&delay, 0, sizeof delay);
    delay.type = vpiSimTime;
    delay.low = 12;
    callback.reason = cbAfterDelay;
    callback.time = &delay;
    callback.cb_rtn = late;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c delay refused\n");
    }
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = started;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

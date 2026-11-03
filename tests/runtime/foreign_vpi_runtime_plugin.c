// SPDX-License-Identifier: Apache-2.0
// VPI runtime conformance plug-in (IEEE 1800-2023 38.2, 38.34, 38.36).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static vpiHandle clk;
static vpiHandle d;
static vpiHandle q;
static vpiHandle pending;
static int cancelled_at;

static unsigned long long now(void)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static int read_int(vpiHandle object)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpi_get_value(object, &value);
    return value.value.integer;
}

static void put_int(vpiHandle object, int number, PLI_INT32 flags,
    unsigned delay)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    s_vpi_time time = { vpiSimTime, 0, delay, 0.0 };
    value.value.integer = number;
    vpi_put_value(object, &value, &time, flags);
}

static vpiHandle register_cb(PLI_INT32 reason, vpiHandle object,
    PLI_INT32 (*routine)(p_cb_data), unsigned delay, PLI_INT32 format)
{
    s_cb_data data;
    s_vpi_time time = { vpiSimTime, 0, delay, 0.0 };
    s_vpi_value value = { format, { 0 } };
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    data.obj = object;
    data.time = &time;
    data.value = &value;
    return vpi_register_cb(&data);
}

static PLI_INT32 read_only(p_cb_data data)
{
    s_vpi_error_info info;
    int rejected;
    (void)data;
    put_int(d, 1, vpiNoDelay, 0U);
    memset(&info, 0, sizeof info);
    // Any other routine, vpi_get_time included, clears the error.
    rejected = vpi_chk_error(&info) == vpiError;
    vpi_printf("#c %llu read-only write rejected=%d\n", now(), rejected);
    return 0;
}

static PLI_INT32 on_clk(p_cb_data data)
{
    // The callback carries the new value; q still holds its value from
    // before the nonblocking update of this edge.
    if (data->value->value.integer == 1) {
        vpi_printf("#c %llu clk rise q=%d\n", now(), read_int(q));
        return 0;
    }
    // A zero-delay inertial write takes effect later in the time slot.
    put_int(d, !read_int(d), vpiInertialDelay, 0U);
    vpi_printf("#c %llu clk fall d=%d after inertial put\n", now(),
        read_int(d));
    return 0;
}

static PLI_INT32 on_change(p_cb_data data)
{
    vpi_printf("#c %llu %s=%d\n", now(), vpi_get_str(vpiName, data->obj),
        data->value->value.integer);
    return 0;
}

static PLI_INT32 check_cancel(p_cb_data data)
{
    (void)data;
    vpi_printf("#c %llu q after cancelled write=%d\n", now(), read_int(q));
    return 0;
}

static PLI_INT32 schedule_cancel(p_cb_data data)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    s_vpi_time time = { vpiSimTime, 0, 3U, 0.0 };
    (void)data;
    // q is 0 at 26 unless the cancelled write of 1 at 25 took effect.
    value.value.integer = 1;
    pending = vpi_put_value(q, &value, &time,
        vpiPureTransportDelay | vpiReturnEvent);
    vpi_printf("#c %llu scheduled=%d type=%d\n", now(),
        vpi_get(vpiScheduled, pending), vpi_get(vpiType, pending));
    put_int(pending, 0, vpiCancelEvent, 0U);
    cancelled_at = (int)now();
    (void)register_cb(cbAfterDelay, NULL, check_cancel, 4U, vpiSuppressVal);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_vpi_error_info info;
    vpiHandle handle;
    unsigned index;
    int failed = 0;
    (void)data;
    clk = vpi_handle_by_name("top.clk", NULL);
    d = vpi_handle_by_name("top.d", NULL);
    q = vpi_handle_by_name("top.q", NULL);
    // Removed callbacks and superseded writes do not count against any
    // limit (IEEE 1800-2023 38.36, 38.34).
    for (index = 0U; index < 70000U; ++index) {
        handle = register_cb(cbAfterDelay, NULL, check_cancel, 100U,
            vpiSuppressVal);
        if (handle == NULL || vpi_remove_cb(handle) == 0) {
            failed = 1;
        }
    }
    for (index = 0U; index < 70000U; ++index) {
        put_int(d, 0, vpiInertialDelay, 1000U);
    }
    vpi_printf("#c lifetime limits ok=%d\n", !failed);
    (void)vpi_get_value(NULL, NULL);
    memset(&info, 0, sizeof info);
    vpi_printf("#c chk_error after bad get_value=%d\n",
        vpi_chk_error(&info) == vpiError);
    vpi_printf("#c chk_error cleared=%d\n", vpi_chk_error(NULL) == 0);
    (void)register_cb(cbValueChange, clk, on_clk, 0U, vpiIntVal);
    (void)register_cb(cbValueChange, q, on_change, 0U, vpiIntVal);
    (void)register_cb(cbValueChange, d, on_change, 0U, vpiIntVal);
    (void)register_cb(cbAfterDelay, NULL, schedule_cancel, 22U,
        vpiSuppressVal);
    (void)register_cb(cbReadOnlySynch, NULL, read_only, 1U, vpiSuppressVal);
    return 0;
}

static PLI_INT32 end(p_cb_data data)
{
    (void)data;
    vpi_printf("#c %llu end of simulation\n", now());
    return 0;
}

static void startup(void)
{
    (void)register_cb(cbStartOfSimulation, NULL, start, 0U, vpiSuppressVal);
    (void)register_cb(cbEndOfSimulation, NULL, end, 0U, vpiSuppressVal);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

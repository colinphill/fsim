// SPDX-License-Identifier: Apache-2.0
// Forcing, releasing and watching variables (IEEE 1076-2019 22.5.2,
// 21.3.2.2-4): value-change callbacks on a variable and on a subelement of
// one, force and release callbacks on a variable and on every object, and
// puts at fixed times. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static vhpiHandleT n_;
static vhpiHandleT v_;
static vhpiHandleT v3_;
static vhpiHandleT t_;

static unsigned long long now_fs(void)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    return ((unsigned long long)now.high << 32) | now.low;
}

static const char* value_of(vhpiHandleT object)
{
    static char buffer[64];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    if (object == n_ || object == t_) {
        value.format = vhpiIntVal;
        if (vhpi_get_value(object, &value) != 0) {
            return "?";
        }
        snprintf(buffer, sizeof buffer, "%d", (int)value.value.intg);
        return buffer;
    }
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    if (vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

static const char* name_of(vhpiHandleT object)
{
    const vhpiCharT* name = object ? vhpi_get_str(vhpiFullNameP, object) : NULL;
    return name ? (const char*)name : "-";
}

static void event(const vhpiCbDataT* data)
{
    const char* what = data->reason == vhpiCbValueChange ? "change"
        : data->reason == vhpiCbForce                      ? "force"
                                                           : "release";
    printf("#c %s %s at %llu fs: %s\n", what, name_of(data->obj), now_fs(),
        value_of(data->obj == v3_ ? v3_ : data->obj));
    fflush(stdout);
}

static void show(void)
{
    printf("#c at %llu fs: n=%s", now_fs(), value_of(n_));
    printf(" v=%s", value_of(v_));
    printf(" t=%s\n", value_of(t_));
    fflush(stdout);
}

static void failed(const char* what, vhpiHandleT object)
{
    vhpiErrorInfoT info;
    memset(&info, 0, sizeof info);
    printf("#c %s %s failed: %s\n", what, name_of(object),
        vhpi_check_error(&info) && info.message ? (const char*)info.message : "-");
}

static void put_int(vhpiHandleT object, int number, vhpiPutValueModeT mode)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = number;
    if (vhpi_put_value(object, &value, mode) != 0) {
        failed("put to", object);
    }
}

static void put_str(vhpiHandleT object, const char* text, vhpiPutValueModeT mode)
{
    char buffer[16];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    strncpy(buffer, text, sizeof buffer - 1);
    buffer[sizeof buffer - 1] = 0;
    value.format = vhpiBinStrVal;
    value.bufSize = strlen(buffer) + 1;
    value.value.str = (vhpiCharT*)buffer;
    if (vhpi_put_value(object, &value, mode) != 0) {
        failed("put to", object);
    }
}

static void put_logic(vhpiHandleT object, vhpiEnumT logic, vhpiPutValueModeT mode)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    value.value.enumv = logic;
    if (vhpi_put_value(object, &value, mode) != 0) {
        failed("put to", object);
    }
}

static void release(vhpiHandleT object)
{
    if (vhpi_put_value(object, NULL, vhpiRelease) != 0) {
        failed("release of", object);
    }
}

static void step(const vhpiCbDataT* data)
{
    const int at = (int)(intptr_t)data->user_data;
    printf("#c step %d\n", at);
    switch (at) {
    case 2:
        // A forced variable is updated with the value (22.5.2).
        put_int(n_, 100, vhpiForce);
        put_logic(v3_, vhpiZ, vhpiForcePropagate);
        put_int(t_, 50, vhpiForce);
        break;
    case 4:
        // A deposit has no effect on the forced elements.
        put_int(n_, 7, vhpiDeposit);
        put_str(v_, "0000", vhpiDeposit);
        break;
    case 6:
        // Releasing a composite variable releases its subelements.
        release(n_);
        release(v_);
        release(t_);
        break;
    default:
        break;
    }
    show();
}

static void watch(vhpiHandleT object, int reason)
{
    vhpiCbDataT request;
    memset(&request, 0, sizeof request);
    request.cb_rtn = event;
    request.obj = object;
    request.reason = reason;
    if (vhpi_register_cb(&request, 0) == NULL && vhpi_check_error(NULL)) {
        printf("#c register on %s failed\n", name_of(object));
    }
}

static void at_time(int ns, int step_number)
{
    vhpiTimeT delay;
    vhpiCbDataT request;
    memset(&request, 0, sizeof request);
    delay.high = 0;
    delay.low = (uint32_t)ns * 1000000U + (step_number == 8 ? 500000U : 0U);
    request.cb_rtn = step;
    request.reason = vhpiCbAfterDelay;
    request.time = &delay;
    request.user_data = (void*)(intptr_t)step_number;
    vhpi_register_cb(&request, 0);
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    n_ = vhpi_handle_by_name("top.p.n", NULL);
    v_ = vhpi_handle_by_name("top.p.v", NULL);
    t_ = vhpi_handle_by_name("top.p.t", NULL);
    v3_ = v_ ? vhpi_handle_by_index(vhpiIndexedNames, v_, 0) : NULL;
    printf("#c handles %s %s %s %s\n", name_of(n_), name_of(v_), name_of(v3_),
        name_of(t_));
    watch(n_, vhpiCbValueChange);
    watch(v_, vhpiCbValueChange);
    watch(v3_, vhpiCbValueChange);
    watch(t_, vhpiCbValueChange);
    watch(n_, vhpiCbForce);
    watch(n_, vhpiCbRelease);
    watch(NULL, vhpiCbForce);
    watch(NULL, vhpiCbRelease);
    at_time(2, 2);
    at_time(4, 4);
    at_time(6, 6);
    at_time(8, 8);
    show();
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

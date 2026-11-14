// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for reading and updating shared variables of a VHDL-93 design
// while it runs (IEEE 1076-1993 4.3.1.3, IEEE 1076-2019 22.3, 22.5.2): at
// 1 ns the values are read, deposited and read again, and later the values
// the process saw and left are listed. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdio.h>
#include <string.h>
static int integer_of(const char* path)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    return vhpi_get_value(vhpi_handle_by_name(path, 0), &value) == 0
        ? (int)value.value.intg : -999;
}
static const char* bits_of(const char* path)
{
    static char text[16];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof text;
    value.value.str = (vhpiCharT*)text;
    return vhpi_get_value(vhpi_handle_by_name(path, 0), &value) == 0 ? text : "?";
}
static void listing(const char* when)
{
    printf("#c %s sv=%d sw=%s\n", when, integer_of("top.sv"), bits_of("top.sw"));
}
static void deposit(const vhpiCbDataT* data)
{
    vhpiValueT value;
    char text[] = "0101";
    (void)data;
    listing("before");
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = 40;
    printf("#c deposit sv %s\n",
        vhpi_put_value(vhpi_handle_by_name("top.sv", 0), &value, vhpiDeposit) == 0
            ? "accepted" : "refused");
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof text;
    value.value.str = (vhpiCharT*)text;
    printf("#c deposit sw %s\n",
        vhpi_put_value(vhpi_handle_by_name("top.sw", 0), &value, vhpiDeposit) == 0
            ? "accepted" : "refused");
    listing("after");
    fflush(stdout);
}
// A variable is updated at once, also where a signal is not: at the end of
// a time step (IEEE 1076-2019 22.5.2).
static void end_of_step(const vhpiCbDataT* data)
{
    vhpiValueT value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = 60;
    printf("#c deposit sv at the end of the step %s\n",
        vhpi_put_value(vhpi_handle_by_name("top.sv", 0), &value, vhpiDepositPropagate) == 0
            ? "accepted" : "refused");
    fflush(stdout);
}
static void later(const vhpiCbDataT* data)
{
    vhpiCbDataT r;
    (void)data;
    printf("#c process saw obs=%d\n", integer_of("top.obs"));
    listing("end");
    memset(&r, 0, sizeof r);
    r.reason = vhpiCbEndOfTimeStep;
    r.cb_rtn = end_of_step;
    vhpi_register_cb(&r, 0);
    fflush(stdout);
}
static void last(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c process left obs=%d\n", integer_of("top.obs"));
    listing("last");
    fflush(stdout);
}
static void startup(void)
{
    vhpiCbDataT r;
    vhpiTimeT t;
    memset(&r, 0, sizeof r);
    memset(&t, 0, sizeof t);
    t.low = 1000000U;
    r.reason = vhpiCbAfterDelay;
    r.cb_rtn = deposit;
    r.time = &t;
    vhpi_register_cb(&r, 0);
    memset(&r, 0, sizeof r);
    t.low = 2500000U;
    r.reason = vhpiCbAfterDelay;
    r.cb_rtn = later;
    r.time = &t;
    vhpi_register_cb(&r, 0);
    memset(&r, 0, sizeof r);
    t.low = 3500000U;
    r.reason = vhpiCbAfterDelay;
    r.cb_rtn = last;
    r.time = &t;
    vhpi_register_cb(&r, 0);
}
FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

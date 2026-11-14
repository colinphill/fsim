// SPDX-License-Identifier: Apache-2.0
// The parameter of a `for` loop of a process (IEEE 1076-2019 10.10, 14.6 a,
// 22.5.1): at the first statement of each pass the value of the parameter
// is listed, and a put to it is attempted. Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static vhpiHandleT parameters[2];
static void probe(const vhpiCbDataT* data)
{
    vhpiHandleT parameter = parameters[(intptr_t)data->user_data];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    if (vhpi_get_value(parameter, &value) != 0) {
        printf("#c %s=?\n", (const char*)vhpi_get_str(vhpiNameP, parameter));
    } else {
        int number = (int)value.value.intg;
        printf("#c %s=%d", (const char*)vhpi_get_str(vhpiNameP, parameter), number);
        value.value.intg = 99;
        printf(" put %s\n",
            vhpi_put_value(parameter, &value, vhpiDeposit) == 0 ? "accepted" : "refused");
    }
    fflush(stdout);
}
static vhpiHandleT nth_of(vhpiHandleT owner, int32_t kind, int skip)
{
    vhpiHandleT it = vhpi_iterator(vhpiSeqStmts, owner), s;
    while (it && (s = vhpi_scan(it))) {
        if (vhpi_get(vhpiKindP, s) == kind && skip-- == 0) {
            return s;
        }
    }
    return NULL;
}
static void start(const vhpiCbDataT* data)
{
    vhpiHandleT main_process = vhpi_handle_by_name("top.main", 0);
    long index;
    (void)data;
    for (index = 0; index < 2; ++index) {
        vhpiHandleT loop = nth_of(main_process, vhpiForLoopK, (int)index);
        vhpiCbDataT r;
        parameters[index] = vhpi_handle(vhpiParamDecl, loop);
        memset(&r, 0, sizeof r);
        r.reason = vhpiCbStmt;
        r.cb_rtn = probe;
        r.obj = nth_of(loop, vhpiVarAssignStmtK, 0);
        r.user_data = (char*)(intptr_t)index;
        if (!vhpi_register_cb(&r, vhpiReturnCb)) {
            printf("#c refused %ld\n", index);
        }
    }
}
static void startup(void)
{
    vhpiCbDataT d;
    memset(&d, 0, sizeof d);
    d.reason = vhpiCbStartOfInitialization;
    d.cb_rtn = start;
    vhpi_register_cb(&d, 0);
}
FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

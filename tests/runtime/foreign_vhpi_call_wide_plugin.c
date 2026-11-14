// SPDX-License-Identifier: Apache-2.0
// A deposit of a 100-bit vector into the local of an outer activation of a
// recursive procedure (IEEE 1076-2019 14.6 b, 22.5.2). At the first
// statement of each activation the local of the call object of the process
// is listed; at the third a pattern is deposited into the outermost
// activation. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdio.h>
#include <string.h>
static vhpiHandleT call;
static int probes;
static const char* text(vhpiHandleT object)
{
    static char buffer[128];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    return vhpi_get_value(object, &value) == 0 ? buffer : "?";
}
static vhpiHandleT local_of(void)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiVarDecls, call);
    return iterator ? vhpi_scan(iterator) : NULL;
}
static void probe(const vhpiCbDataT* data)
{
    vhpiHandleT local = vhpi_handle(vhpiCurStackFrame, call) ? local_of() : NULL;
    (void)data;
    printf("#c probe %d\n", ++probes);
    if (local == NULL) {
        printf("#c   no frame\n");
        return;
    }
    printf("#c   outer %s\n", text(local));
    if (probes == 3) {
        char pattern[101];
        vhpiValueT value;
        int bit;
        for (bit = 0; bit < 100; ++bit) {
            pattern[bit] = (bit % 3 == 0) ? '1' : (bit % 7 == 0) ? 'Z' : '0';
        }
        pattern[100] = 0;
        memset(&value, 0, sizeof value);
        value.format = vhpiBinStrVal;
        value.bufSize = sizeof pattern;
        value.value.str = (vhpiCharT*)pattern;
        printf("#c   deposit %s\n",
            vhpi_put_value(local, &value, vhpiDeposit) == 0 ? "accepted" : "refused");
        printf("#c   outer %s\n", text(local));
    }
    fflush(stdout);
}
static void show(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c acc %s\n", text(vhpi_handle_by_name("top.main.acc", 0)));
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
    vhpiCbDataT r;
    vhpiTimeT t;
    vhpiHandleT body = vhpi_handle(vhpiSubpBody, vhpi_handle_by_name("top.main.down", 0));
    (void)data;
    call = nth_of(vhpi_handle_by_name("top.main", 0), vhpiSeqProcCallStmtK, 0);
    memset(&r, 0, sizeof r);
    r.reason = vhpiCbStmt;
    r.cb_rtn = probe;
    r.obj = nth_of(body, vhpiVarAssignStmtK, 0);
    if (!vhpi_register_cb(&r, vhpiReturnCb)) {
        printf("#c refused\n");
    }
    memset(&r, 0, sizeof r);
    memset(&t, 0, sizeof t);
    t.low = 500000U;
    r.reason = vhpiCbAfterDelay;
    r.cb_rtn = show;
    r.time = &t;
    vhpi_register_cb(&r, 0);
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

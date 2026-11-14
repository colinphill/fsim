// SPDX-License-Identifier: Apache-2.0
// The formals and variables of the frames of calls inside the body of another
// subprogram (IEEE 1076-2019 14.6 b): at the first statement of each
// activation of the procedure inner every call object, the call of outer by
// the process and the three calls of inner in outer's body, that has a frame
// lists the formals and the variables of it. Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdio.h>
#include <string.h>
#define SITES 4
static vhpiHandleT calls[SITES];
static int probes;
static int val(vhpiHandleT o)
{
    vhpiValueT x;
    memset(&x, 0, sizeof x);
    x.format = vhpiIntVal;
    return vhpi_get_value(o, &x) ? -999 : (int)x.value.intg;
}
static void dump(int c)
{
    static const vhpiOneToManyT relations[] = { vhpiParamDecls, vhpiVarDecls };
    size_t r;
    if (!vhpi_handle(vhpiCurStackFrame, calls[c])) {
        return;
    }
    printf("#c   %s:", c == 0 ? "outer" : c == 1 ? "inner0" : c == 2 ? "inner1" : "inner2");
    for (r = 0U; r < 2U; ++r) {
        vhpiHandleT it = vhpi_iterator(relations[r], calls[c]);
        vhpiHandleT d;
        while (it && (d = vhpi_scan(it))) {
            printf(" %s=%d", (const char*)vhpi_get_str(vhpiNameP, d), val(d));
        }
    }
    printf("\n");
}
static void probe(const vhpiCbDataT* d)
{
    int c;
    (void)d;
    printf("#c probe %d\n", ++probes);
    for (c = 0; c < SITES; c++) {
        dump(c);
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
static void watch(const char* name)
{
    vhpiCbDataT r;
    vhpiHandleT body = vhpi_handle(vhpiSubpBody, vhpi_handle_by_name(name, 0));
    memset(&r, 0, sizeof r);
    r.reason = vhpiCbStmt;
    r.cb_rtn = probe;
    r.obj = nth_of(body, vhpiVarAssignStmtK, 0);
    if (!vhpi_register_cb(&r, vhpiReturnCb)) {
        printf("#c refused %s\n", name);
    }
}
static void start(const vhpiCbDataT* d)
{
    vhpiHandleT m = vhpi_handle_by_name("top.main", 0);
    vhpiHandleT outer = vhpi_handle(vhpiSubpBody, vhpi_handle_by_name("top.main.outer", 0));
    int i;
    (void)d;
    calls[0] = nth_of(m, vhpiSeqProcCallStmtK, 0);
    for (i = 1; i < SITES; i++) {
        calls[i] = nth_of(outer, vhpiSeqProcCallStmtK, i - 1);
    }
    watch("top.main.inner");
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

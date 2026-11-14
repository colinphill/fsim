// SPDX-License-Identifier: Apache-2.0
// The formals and variable of the frames of calls of a package function from
// two processes (IEEE 1076-2019 14.6 b): at the return statement of the
// function every call object that has a frame lists the formals and the
// variable of it. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdio.h>
#include <string.h>
#define SITES 4
static vhpiHandleT calls[SITES];
static const char* const names[SITES] = { "first.q", "second.q0", "second.q1", "second.q2" };
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
    if (!calls[c] || !vhpi_handle(vhpiCurStackFrame, calls[c])) {
        return;
    }
    printf("#c   %s:", names[c]);
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
static void start(const vhpiCbDataT* d)
{
    static const char* const candidates[] = { "work.fpkg.g", "fpkg.g", "top.fpkg.g" };
    vhpiHandleT g = NULL;
    vhpiHandleT body;
    vhpiCbDataT r;
    unsigned i;
    (void)d;
    calls[0] = vhpi_handle(vhpiRhsExpr, nth_of(vhpi_handle(vhpiSubpBody, vhpi_handle_by_name("top.first.q", 0)), vhpiVarAssignStmtK, 0));
    for (i = 0; i < 3U; i++) {
        calls[1 + i] = vhpi_handle(vhpiRhsExpr, nth_of(vhpi_handle(vhpiSubpBody, vhpi_handle_by_name("top.second.q", 0)), vhpiVarAssignStmtK, (int)i));
    }
    for (i = 0; i < 3U && !g; i++) {
        g = vhpi_handle_by_name(candidates[i], 0);
    }
    body = g ? vhpi_handle(vhpiSubpBody, g) : NULL;
    memset(&r, 0, sizeof r);
    r.reason = vhpiCbStmt;
    r.cb_rtn = probe;
    r.obj = body ? nth_of(body, vhpiReturnStmtK, 0) : NULL;
    if (!r.obj || !vhpi_register_cb(&r, vhpiReturnCb)) {
        printf("#c refused\n");
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

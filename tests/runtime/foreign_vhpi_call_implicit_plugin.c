// SPDX-License-Identifier: Apache-2.0
// Probes in the body of a resolution function (called by the simulation
// cycle, 14.7.3.2) and in the body of a function a concurrent assignment
// calls (IEEE 1076-2019 19.4.3, 19.10). Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdio.h>
#include <string.h>
static vhpiHandleT triple_call;
static vhpiHandleT sum_body;
static int resolves;
static int val(vhpiHandleT o)
{
    vhpiValueT x;
    memset(&x, 0, sizeof x);
    x.format = vhpiIntVal;
    return vhpi_get_value(o, &x) ? -999 : (int)x.value.intg;
}
static void list(vhpiHandleT owner, vhpiOneToManyT relation)
{
    vhpiHandleT it = vhpi_iterator(relation, owner);
    vhpiHandleT d;
    while (it && (d = vhpi_scan(it))) {
        printf(" %s=%d", (const char*)vhpi_get_str(vhpiNameP, d), val(d));
    }
}
static void in_triple(const vhpiCbDataT* data)
{
    (void)data;
    if (triple_call && vhpi_handle(vhpiCurStackFrame, triple_call)) {
        printf("#c triple:");
        list(triple_call, vhpiParamDecls);
        list(triple_call, vhpiVarDecls);
        printf("\n");
        fflush(stdout);
    }
}
static void in_sum(const vhpiCbDataT* data)
{
    vhpiHandleT it;
    (void)data;
    if (++resolves != 1) {
        return;
    }
    printf("#c sum_i statement reached\n");
    it = vhpi_iterator(vhpiVarDecls, sum_body);
    if (it) {
        vhpiHandleT d = vhpi_scan(it);
        vhpiValueT x;
        memset(&x, 0, sizeof x);
        x.format = vhpiIntVal;
        printf("#c   read of %s: %s\n", (const char*)vhpi_get_str(vhpiNameP, d),
            vhpi_get_value(d, &x) == 0 ? "value" : "error");

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
    vhpiHandleT top = vhpi_handle_by_name("top", 0);
    vhpiHandleT triple_body = vhpi_handle(vhpiSubpBody, vhpi_handle_by_name("top.triple", 0));
    vhpiHandleT it = vhpi_iterator(vhpiStmts, top);
    vhpiHandleT s;
    vhpiCbDataT r;
    (void)data;
    sum_body = vhpi_handle(vhpiSubpBody, vhpi_handle_by_name("top.sum_i", 0));
    while (it && (s = vhpi_scan(it))) {
        if (vhpi_get(vhpiKindP, s) == vhpiSimpleSigAssignStmtK && !triple_call) {
            vhpiHandleT elements = vhpi_iterator(vhpiWaveformElems, s);
            vhpiHandleT element = elements ? vhpi_scan(elements) : NULL;
            triple_call = element ? vhpi_handle(vhpiValExpr, element) : NULL;
            if (triple_call && vhpi_get(vhpiKindP, triple_call) != vhpiFuncCallK) {
                triple_call = NULL;
            }
        }
    }
    memset(&r, 0, sizeof r);
    r.reason = vhpiCbStmt;
    r.cb_rtn = in_triple;
    r.obj = nth_of(triple_body, vhpiVarAssignStmtK, 0);
    if (!vhpi_register_cb(&r, vhpiReturnCb)) {
        printf("#c refused triple\n");
    }
    memset(&r, 0, sizeof r);
    r.reason = vhpiCbStmt;
    r.cb_rtn = in_sum;
    r.obj = nth_of(nth_of(sum_body, vhpiForLoopK, 0), vhpiVarAssignStmtK, 0);
    if (!r.obj || !vhpi_register_cb(&r, vhpiReturnCb)) {
        printf("#c refused sum_i\n");
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

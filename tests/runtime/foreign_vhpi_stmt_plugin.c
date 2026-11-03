// SPDX-License-Identifier: Apache-2.0
// VHPI statement callback conformance plug-in (IEEE 1076-2019 21.3.4):
// places vhpiCbStmt on processes, loops and a case alternative,
// vhpiCbResume/vhpiCbSuspend on a process, vhpiCbSensitivity on a signal
// (21.3.3.3) and vhpiCbStartOfSubpCall/vhpiCbEndOfSubpCall on a procedure
// call and a procedure declaration (21.3.4.5-6), and reports each call. Also
// function calls and declarations, a statement of a subprogram body, elsif and
// else branches, a concurrent procedure call and a sensitivity set of two
// signals with its bitmap (21.3.3.3). Without
// simulate --foreign-probes the registration fails with an error. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* reason_name(int32_t reason)
{
    switch (reason) {
    case vhpiCbStmt:
        return "stmt";
    case vhpiCbResume:
        return "resume";
    case vhpiCbSuspend:
        return "suspend";
    case vhpiCbSensitivity:
        return "sensitivity";
    case vhpiCbStartOfSubpCall:
        return "startcall";
    case vhpiCbEndOfSubpCall:
        return "endcall";
    default:
        return "other";
    }
}

static void report(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    const vhpiCharT* kind = vhpi_get_str(vhpiKindStrP, data->obj);
    const vhpiCharT* owner = (const vhpiCharT*)data->user_data;
    // A collection has no source position.
    int set = vhpi_get(vhpiKindP, data->obj) == vhpiAnyCollectionK;
    vhpi_get_time(&now, NULL);
    printf("#c %s %s %s line %d t=%u", reason_name(data->reason),
        owner != NULL ? (const char*)owner : "-",
        kind != NULL ? (const char*)kind : "-",
        set ? -1 : (int)vhpi_get(vhpiLineNoP, data->obj), (unsigned)now.low);
    // A sensitivity callback reports the time of its event when asked to,
    // and for a set the bitmap of the signals that had one.
    if (data->reason == vhpiCbSensitivity) {
        printf(" event=%u", data->time != NULL ? (unsigned)data->time->low : 0U);
        if (set && data->value != NULL) {
            printf(" bitmap=%d elems=%d", (int)data->value->value.intgs[0],
                (int)data->value->numElems);
        }
    }
    printf("\n");
    fflush(stdout);
}

static vhpiTimeT event_time;
static vhpiValueT bitmap_value;

static int place(int32_t reason, vhpiHandleT object, const char* label)
{
    vhpiCbDataT data;
    vhpiErrorInfoT error;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    if (reason == vhpiCbSensitivity) {
        data.time = &event_time;
        if (object != NULL && vhpi_get(vhpiKindP, object) == vhpiAnyCollectionK) {
            bitmap_value.format = vhpiIntVecVal;
            data.value = &bitmap_value;
        }
    }
    data.cb_rtn = report;
    data.obj = object;
    data.user_data = (char*)label;
    if (vhpi_register_cb(&data, vhpiReturnCb) != NULL) {
        return 1;
    }
    memset(&error, 0, sizeof error);
    if (vhpi_check_error(&error) != 0) {
        printf("#c refused %s %s: %s\n", reason_name(reason), label,
            error.message != NULL ? (const char*)error.message : "");
    } else {
        printf("#c refused %s %s without an error\n", reason_name(reason),
            label);
    }
    return 0;
}

// The first statement of a process or region of the given kind.
static vhpiHandleT first_of(vhpiHandleT owner, int32_t kind)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiSeqStmts, owner);
    vhpiHandleT statement;
    while (iterator != NULL && (statement = vhpi_scan(iterator)) != NULL) {
        if (vhpi_get(vhpiKindP, statement) == kind) {
            vhpi_release_handle(iterator);
            return statement;
        }
    }
    return NULL;
}

// Function calls and declarations, a statement of a body, elsif and else
// branches, a concurrent procedure call and a sensitivity set.
static void function_calls(void)
{
    vhpiHandleT fn = vhpi_handle_by_name("top.fn", NULL);
    vhpiHandleT loop = first_of(fn, vhpiForLoopK);
    vhpiHandleT call
        = vhpi_handle(vhpiRhsExpr, first_of(loop, vhpiVarAssignStmtK));
    vhpiHandleT inc = vhpi_handle_by_name("top.fn.inc", NULL);
    vhpiHandleT body = vhpi_handle(vhpiSubpBody, inc);
    vhpiHandleT branches
        = vhpi_iterator(vhpiBranchs, first_of(loop, vhpiIfStmtK));
    vhpiHandleT concurrent = vhpi_handle_by_name("top.blk.cc", NULL);
    vhpiHandleT set = vhpi_create(vhpiAnyCollectionK, NULL,
        vhpi_handle_by_name("top.clk", NULL));
    vhpiHandleT branch;
    (void)place(vhpiCbStartOfSubpCall, call, "fn.call");
    (void)place(vhpiCbEndOfSubpCall, call, "fn.call");
    (void)place(vhpiCbStartOfSubpCall, inc, "inc");
    (void)place(vhpiCbEndOfSubpCall, inc, "inc");
    (void)place(vhpiCbStmt, first_of(body, vhpiReturnStmtK), "inc.return");
    // The if branch, the elsif branch and the else branch.
    (void)vhpi_scan(branches);
    branch = vhpi_scan(branches);
    (void)place(vhpiCbStmt, branch, "fn.elsif");
    branch = vhpi_scan(branches);
    (void)place(vhpiCbStmt, branch, "fn.else");
    (void)place(vhpiCbStmt, concurrent, "cc");
    (void)place(vhpiCbStartOfSubpCall, concurrent, "cc");
    (void)place(vhpiCbEndOfSubpCall, concurrent, "cc");
    set = vhpi_create(vhpiAnyCollectionK, set,
        vhpi_handle_by_name("top.blk.sg", NULL));
    (void)place(vhpiCbSensitivity, set, "set");
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT clock = vhpi_handle_by_name("top.clock", NULL);
    vhpiHandleT watch = vhpi_handle_by_name("top.watch", NULL);
    vhpiHandleT counter = vhpi_handle_by_name("top.counter", NULL);
    vhpiHandleT branches;
    vhpiHandleT branch;
    (void)data;
    if (!place(vhpiCbStmt, clock, "clock")) {
        fflush(stdout);
        return;
    }
    (void)place(vhpiCbStmt, first_of(clock, vhpiForLoopK), "clock.for");
    (void)place(vhpiCbResume, clock, "clock");
    (void)place(vhpiCbSuspend, clock, "clock");
    (void)place(vhpiCbStmt, first_of(counter, vhpiWhileLoopK), "counter.while");
    (void)place(vhpiCbResume, watch, "watch");
    branches = vhpi_iterator(vhpiBranchs, first_of(watch, vhpiCaseStmtK));
    branch = branches != NULL ? vhpi_scan(branches) : NULL;
    (void)place(vhpiCbStmt, branch, "watch.when2");
    // A signal is not a statement.
    (void)place(vhpiCbStmt, vhpi_handle_by_name("top.clk", NULL), "clk");
    (void)place(vhpiCbSensitivity, vhpi_handle_by_name("top.clk", NULL), "clk");
    // The first call of `calls`, and every call of `bump` through its
    // declaration; the end of a call needs a call object.
    (void)place(vhpiCbStartOfSubpCall,
        first_of(vhpi_handle_by_name("top.calls", NULL), vhpiSeqProcCallStmtK),
        "calls.first");
    (void)place(vhpiCbEndOfSubpCall,
        first_of(vhpi_handle_by_name("top.calls", NULL), vhpiSeqProcCallStmtK),
        "calls.first");
    (void)place(vhpiCbStartOfSubpCall,
        vhpi_handle_by_name("top.calls.bump", NULL), "bump");
    (void)place(vhpiCbEndOfSubpCall,
        vhpi_handle_by_name("top.calls.bump", NULL), "bump");
    function_calls();
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfInitialization;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

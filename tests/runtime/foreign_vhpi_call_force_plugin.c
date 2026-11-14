// SPDX-License-Identifier: Apache-2.0
// Forcing the variable of a running subprogram call (IEEE 1076-2019 14.6,
// 22.5.2): at the first statement of a recursive procedure the library forces
// the variable of the second activation, and forces and releases that of the
// third. The force of an activation ends with its call. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static vhpiHandleT inner_call;
static vhpiHandleT total_;
static int probes;

static int value_of(vhpiHandleT object)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    return vhpi_get_value(object, &value) == 0 ? (int)value.value.intg : -1;
}

// The variable `depth` of the newest activation of the running call.
static vhpiHandleT depth_of(vhpiHandleT call)
{
    vhpiHandleT frame = vhpi_handle(vhpiCurStackFrame, call);
    vhpiHandleT iterator = frame != NULL ? vhpi_iterator(vhpiVarDecls, call) : NULL;
    vhpiHandleT declaration;
    while (iterator != NULL && (declaration = vhpi_scan(iterator)) != NULL) {
        const char* text = (const char*)vhpi_get_str(vhpiNameP, declaration);
        if (text != NULL && tolower((unsigned char)text[0]) == 'd') {
            return declaration;
        }
    }
    return NULL;
}

static void put(vhpiHandleT object, int number, vhpiPutValueModeT mode)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = number;
    if (vhpi_put_value(object, &value, mode) != 0) {
        printf("#c   put refused\n");
    }
}

static void probe(const vhpiCbDataT* data)
{
    vhpiHandleT depth;
    (void)data;
    ++probes;
    depth = probes == 1 || probes > 3 ? NULL : depth_of(inner_call);
    if (probes == 2) {
        printf("#c activation 2: force depth=100 (depth=%d)\n", value_of(depth));
        put(depth, 100, vhpiForce);
    } else if (probes == 3) {
        printf("#c activation 3: force depth=200, release (depth=%d)\n",
            value_of(depth));
        put(depth, 200, vhpiForce);
        if (vhpi_put_value(depth, NULL, vhpiRelease) != 0) {
            printf("#c   release refused\n");
        }
    }
    fflush(stdout);
}

// The calls' frames are kept while a call callback is registered.
static void ended(const vhpiCbDataT* data)
{
    (void)data;
}

static void show(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    (void)data;
    vhpi_get_time(&now, NULL);
    printf("#c total=%d at %u fs\n", value_of(total_), (unsigned)now.low);
    fflush(stdout);
}

static vhpiHandleT nth_of(vhpiHandleT owner, int32_t kind, int skip)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiSeqStmts, owner);
    vhpiHandleT statement;
    while (iterator != NULL && (statement = vhpi_scan(iterator)) != NULL) {
        if (vhpi_get(vhpiKindP, statement) == kind && skip-- == 0) {
            vhpi_release_handle(iterator);
            return statement;
        }
    }
    return NULL;
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT down_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.down", NULL));
    vhpiHandleT branches
        = vhpi_iterator(vhpiBranchs, nth_of(down_body, vhpiIfStmtK, 0));
    vhpiHandleT then_branch = vhpi_scan(branches);
    vhpiCbDataT request;
    vhpiTimeT delay;
    (void)data;
    inner_call = nth_of(then_branch, vhpiSeqProcCallStmtK, 0);
    total_ = vhpi_handle_by_name("top.main.total", NULL);
    memset(&request, 0, sizeof request);
    request.reason = vhpiCbStmt;
    request.cb_rtn = probe;
    request.obj = nth_of(down_body, vhpiVarAssignStmtK, 0);
    if (vhpi_register_cb(&request, vhpiReturnCb) == NULL) {
        printf("#c refused probe\n");
    }
    {
        memset(&request, 0, sizeof request);
        request.reason = vhpiCbEndOfSubpCall;
        request.cb_rtn = ended;
        request.obj = inner_call;
        if (vhpi_register_cb(&request, vhpiReturnCb) == NULL) {
            printf("#c refused call callback\n");
        }
    }
    memset(&request, 0, sizeof request);
    delay.high = 0;
    delay.low = 500000U;
    request.reason = vhpiCbAfterDelay;
    request.cb_rtn = show;
    request.time = &delay;
    vhpi_register_cb(&request, 0);
    delay.low = 1500000U;
    vhpi_register_cb(&request, 0);
    delay.low = 2500000U;
    vhpi_register_cb(&request, 0);
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

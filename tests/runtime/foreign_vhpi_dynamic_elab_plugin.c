// SPDX-License-Identifier: Apache-2.0
// VHPI subprogram call stack frame conformance plug-in (IEEE 1076-2019 14.6,
// 19.10, Figure 35): at a statement of a recursive procedure, at one of a
// function and at the end of its calls, walks vhpiCurStackFrame of the call
// objects, vhpiUpStack and vhpiFrameLevelP, and reads the formal and the
// variable the call elaborated (vhpiParamDecls and vhpiVarDecls of a
// subpCall). Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static vhpiHandleT outer_call;
static vhpiHandleT inner_call;
static vhpiHandleT leaf_call;

// The formals and variables a running call elaborated, with their values.
static void variables(vhpiHandleT call)
{
    static const vhpiOneToManyT relations[] = { vhpiParamDecls, vhpiVarDecls };
    size_t index;
    for (index = 0U; index < sizeof relations / sizeof relations[0]; ++index) {
        vhpiHandleT iterator = vhpi_iterator(relations[index], call);
        vhpiHandleT declaration;
        while (iterator != NULL && (declaration = vhpi_scan(iterator)) != NULL) {
            vhpiValueT value;
            memset(&value, 0, sizeof value);
            value.format = vhpiIntVal;
            if (vhpi_get_value(declaration, &value) == 0) {
                printf(" %s=%d",
                    (const char*)vhpi_get_str(vhpiNameP, declaration),
                    (int)value.value.intg);
            } else {
                printf(" %s=?",
                    (const char*)vhpi_get_str(vhpiNameP, declaration));
            }
        }
    }
}

// The frame of a running call, its level and the levels above it.
static void describe(const char* label, vhpiHandleT call, int with_variables)
{
    vhpiHandleT frame = vhpi_handle(vhpiCurStackFrame, call);
    vhpiHandleT up;
    if (frame == NULL) {
        printf("#c   %s none\n", label);
        return;
    }
    printf("#c   %s level=%d", label, (int)vhpi_get(vhpiFrameLevelP, frame));
    if (with_variables) {
        variables(call);
    }
    printf(" up:");
    for (up = vhpi_handle(vhpiUpStack, frame); up != NULL;
         up = vhpi_handle(vhpiUpStack, up)) {
        printf(" %d", (int)vhpi_get(vhpiFrameLevelP, up));
    }
    printf("\n");
}

static void report(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    const char* label = (const char*)data->user_data;
    vhpi_get_time(&now, NULL);
    printf("#c %s %s %s line %d t=%u\n",
        data->reason == vhpiCbStmt ? "stmt" : "endcall", label,
        (const char*)vhpi_get_str(vhpiKindStrP, data->obj),
        (int)vhpi_get(vhpiLineNoP, data->obj), (unsigned)now.low);
    if (strcmp(label, "probe") == 0) {
        describe("outer", outer_call, 1);
        describe("inner", inner_call, 1);
    } else if (strcmp(label, "leaf.return") == 0) {
        describe("leaf", leaf_call, 0);
    } else if (strcmp(label, "inner") == 0) {
        describe("inner", inner_call, 1);
    } else {
        describe("outer", outer_call, 1);
    }
    fflush(stdout);
}

static void place(int32_t reason, vhpiHandleT object, const char* label)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = report;
    data.obj = object;
    data.user_data = (char*)label;
    if (vhpi_register_cb(&data, vhpiReturnCb) == NULL) {
        printf("#c refused %s\n", label);
    }
}

// The first statement of a process, body or branch of the given kind.
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

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT tool = vhpi_handle(vhpiTool, NULL);
    vhpiHandleT main_process = vhpi_handle_by_name("top.main", NULL);
    vhpiHandleT down_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.down", NULL));
    vhpiHandleT leaf_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.leaf", NULL));
    vhpiHandleT branches
        = vhpi_iterator(vhpiBranchs, first_of(down_body, vhpiIfStmtK));
    vhpiHandleT then_branch = vhpi_scan(branches);
    vhpiHandleT else_branch = vhpi_scan(branches);
    vhpiIntT capabilities = vhpi_get(vhpiCapabilitiesP, tool);
    (void)data;
    // Dynamic elaboration needs the debug and runtime simulation set, which
    // fsim does not provide completely, so neither is claimed.
    printf("#c capabilities dynamic=%d debug=%d\n",
        (capabilities & vhpiProvidesDynamicElab) != 0,
        (capabilities & vhpiProvidesDebugRuntime) != 0);
    outer_call = first_of(main_process, vhpiSeqProcCallStmtK);
    inner_call = first_of(then_branch, vhpiSeqProcCallStmtK);
    leaf_call = vhpi_handle(vhpiRhsExpr, first_of(else_branch, vhpiVarAssignStmtK));
    place(vhpiCbStmt, first_of(down_body, vhpiVarAssignStmtK), "probe");
    place(vhpiCbStmt, first_of(leaf_body, vhpiReturnStmtK), "leaf.return");
    place(vhpiCbEndOfSubpCall, inner_call, "inner");
    place(vhpiCbEndOfSubpCall, outer_call, "outer");
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

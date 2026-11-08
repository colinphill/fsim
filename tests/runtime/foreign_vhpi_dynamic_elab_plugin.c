// SPDX-License-Identifier: Apache-2.0
// VHPI subprogram call stack frame conformance plug-in (IEEE 1076-2019 14.6,
// 19.10, Figure 35): at the return statement of a function with a signal
// parameter it lists that formal's contributors; at a statement of a recursive procedure, at one of a
// function and at the end of its calls, walks vhpiCurStackFrame of the call
// objects, vhpiUpStack and vhpiFrameLevelP, and reads the formal and the
// variable the call elaborated (vhpiParamDecls and vhpiVarDecls of a
// subpCall). At a statement of a procedure with signal parameters it lists
// the basic signals and local contributors of the formals the call
// elaborated (19.12.2.2 c). Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static vhpiHandleT outer_call;
static vhpiHandleT inner_call;
static vhpiHandleT leaf_call;
static vhpiHandleT drive_call;
static vhpiHandleT sample_call;
// The number of times the probe in the body of the recursive procedure ran.
static int probes;

// The number of objects of an iteration.
static int count(vhpiOneToManyT relation, vhpiHandleT object)
{
    vhpiHandleT iterator = vhpi_iterator(relation, object);
    int total = 0;
    while (iterator != NULL && vhpi_scan(iterator) != NULL) {
        ++total;
    }
    return total;
}

// Whether a driver belongs to the process "main" (letter case of names is
// not specified).
static int main_driver(vhpiHandleT driver)
{
    vhpiHandleT process = vhpi_handle(vhpiEqProcessStmt, driver);
    const char* name
        = process != NULL ? (const char*)vhpi_get_str(vhpiNameP, process) : NULL;
    return name != NULL && tolower((unsigned char)name[0]) == 'm'
        && tolower((unsigned char)name[1]) == 'a'
        && tolower((unsigned char)name[2]) == 'i'
        && tolower((unsigned char)name[3]) == 'n' && name[4] == 0;
}

// The basic signals and local contributors of the signal parameters a
// running call elaborated: a formal of mode in has those of its actual, one
// of mode out the driver the calling process holds for it.
static void formals(vhpiHandleT call)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiParamDecls, call);
    vhpiHandleT formal;
    while (iterator != NULL && (formal = vhpi_scan(iterator)) != NULL) {
        vhpiHandleT contributors
            = vhpi_iterator(vhpiLocalContributors, formal);
        vhpiHandleT contributor;
        printf("#c   %s basics=%d contributors:",
            (const char*)vhpi_get_str(vhpiNameP, formal),
            count(vhpiBasicSignals, formal));
        while (contributors != NULL
            && (contributor = vhpi_scan(contributors)) != NULL) {
            printf(" %s(main=%d)",
                (const char*)vhpi_get_str(vhpiKindStrP, contributor),
                main_driver(contributor));
        }
        printf("\n");
    }
}

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

// Deposits a value into the variable `name` that the running call
// elaborated (IEEE 1076-2019 14.6 b, 22.5.2) and reads it back.
static void deposit_variable(vhpiHandleT call, const char* name, int new_value)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiVarDecls, call);
    vhpiHandleT declaration;
    while (iterator != NULL && (declaration = vhpi_scan(iterator)) != NULL) {
        vhpiValueT value;
        const char* text = (const char*)vhpi_get_str(vhpiNameP, declaration);
        size_t index;
        for (index = 0U; text != NULL && name[index] != 0
             && tolower((unsigned char)text[index]) == name[index];
             ++index) {
        }
        if (text == NULL || name[index] != 0 || text[index] != 0) {
            continue;
        }
        memset(&value, 0, sizeof value);
        value.format = vhpiIntVal;
        value.value.intg = new_value;
        if (vhpi_put_value(declaration, &value, vhpiDeposit) != 0) {
            printf("#c   put %s refused\n", name);
            return;
        }
        value.value.intg = 0;
        if (vhpi_get_value(declaration, &value) != 0) {
            printf("#c   put %s readback failed\n", name);
            return;
        }
        printf("#c   put %s=%d readback=%d\n", name, new_value,
            (int)value.value.intg);
        return;
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
        // In the newest activation (n = 1) the variable is deposited; the
        // statements that follow overwrite it, so the trace after this
        // line is the one without the deposit.
        if (++probes == 3) {
            deposit_variable(inner_call, "depth", 55);
        }
    } else if (strcmp(label, "leaf.return") == 0) {
        describe("leaf", leaf_call, 0);
    } else if (strcmp(label, "drive") == 0) {
        formals(drive_call);
    } else if (strcmp(label, "fsig") == 0) {
        // A function's signal parameter (mode in) denotes its actual.
        formals(sample_call);
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

// The statement of the given kind, counting from zero, in a process, body
// or branch.
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

// The first statement of a process, body or branch of the given kind.
static vhpiHandleT first_of(vhpiHandleT owner, int32_t kind)
{
    return nth_of(owner, kind, 0);
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT tool = vhpi_handle(vhpiTool, NULL);
    vhpiHandleT main_process = vhpi_handle_by_name("top.main", NULL);
    vhpiHandleT down_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.down", NULL));
    vhpiHandleT leaf_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.leaf", NULL));
    vhpiHandleT drive_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.drive", NULL));
    vhpiHandleT fp_process = vhpi_handle_by_name("top.fp", NULL);
    vhpiHandleT sample_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.fp.sample", NULL));
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
    drive_call = nth_of(main_process, vhpiSeqProcCallStmtK, 1);
    place(vhpiCbStmt, first_of(down_body, vhpiVarAssignStmtK), "probe");
    place(vhpiCbStmt, first_of(drive_body, vhpiSeqSigAssignStmtK), "drive");
    place(vhpiCbStmt, first_of(leaf_body, vhpiReturnStmtK), "leaf.return");
    sample_call
        = vhpi_handle(vhpiRhsExpr, first_of(fp_process, vhpiVarAssignStmtK));
    place(vhpiCbStmt, first_of(sample_body, vhpiReturnStmtK), "fsig");
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

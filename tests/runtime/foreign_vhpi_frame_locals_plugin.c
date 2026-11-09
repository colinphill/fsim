// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the formals and locals of a running function call and the
// parameter of a `for` loop of a running procedure (IEEE 1076-2019 14.6 a, b,
// 19.10, 22.3): at a statement of each it lists the values of the call's
// formals and variables and of the loop parameter. Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static vhpiHandleT function_call;
static vhpiHandleT procedure_call;
static vhpiHandleT loop_parameter;

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

static void report(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    const char* label = (const char*)data->user_data;
    vhpi_get_time(&now, NULL);
    printf("#c stmt %s %s line %d t=%u\n", label,
        (const char*)vhpi_get_str(vhpiKindStrP, data->obj),
        (int)vhpi_get(vhpiLineNoP, data->obj), (unsigned)now.low);
    if (strcmp(label, "f.return") == 0) {
        printf("#c   f");
        variables(function_call);
    } else {
        vhpiValueT value;
        printf("#c   p");
        variables(procedure_call);
        memset(&value, 0, sizeof value);
        value.format = vhpiIntVal;
        if (vhpi_get_value(loop_parameter, &value) == 0) {
            printf(" i=%d", (int)value.value.intg);
        } else {
            printf(" i=?");
        }
    }
    printf("\n");
    fflush(stdout);
}

static void place(vhpiHandleT object, const char* label)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStmt;
    data.cb_rtn = report;
    data.obj = object;
    data.user_data = (char*)label;
    if (vhpi_register_cb(&data, vhpiReturnCb) == NULL) {
        printf("#c refused %s\n", label);
    }
}

// The nth statement of the given kind in a process, body or loop.
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
    vhpiHandleT main_process = vhpi_handle_by_name("top.main", NULL);
    vhpiHandleT f_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.f", NULL));
    vhpiHandleT p_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.p", NULL));
    vhpiHandleT loop = nth_of(p_body, vhpiForLoopK, 0);
    (void)data;
    function_call = vhpi_handle(vhpiRhsExpr,
        nth_of(vhpi_handle(vhpiSubpBody, vhpi_handle_by_name("top.main.q", NULL)),
            vhpiVarAssignStmtK, 0));
    procedure_call = nth_of(main_process, vhpiSeqProcCallStmtK, 1);
    loop_parameter = vhpi_handle(vhpiParamDecl, loop);
    place(nth_of(f_body, vhpiReturnStmtK, 0), "f.return");
    place(nth_of(loop, vhpiVarAssignStmtK, 0), "p.loop");
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

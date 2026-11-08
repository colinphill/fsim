// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the full names of the declarations a running call
// elaborated (IEEE 1076-2019 14.6 b, 19.4.3): at the first statement of the
// inner procedure, called by the outer one, which the process main called, it
// prints the full name and value of the formals and variables of both calls.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static vhpiHandleT outer_call;
static vhpiHandleT inner_call;

static const char* lowered(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[8][128];
    static int next;
    const vhpiCharT* value = vhpi_get_str(property, object);
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 8;
    if (value == NULL) {
        return "-";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffers[0];
         ++index) {
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

static void declarations(const char* label, vhpiHandleT call,
    vhpiOneToManyT relation, const char* what)
{
    vhpiHandleT iterator = vhpi_iterator(relation, call);
    vhpiHandleT declaration;
    while (iterator != NULL && (declaration = vhpi_scan(iterator)) != NULL) {
        vhpiValueT value;
        memset(&value, 0, sizeof value);
        value.format = vhpiIntVal;
        if (vhpi_get_value(declaration, &value) != 0) {
            value.value.intg = -1;
        }
        printf("#c   %s %s %s = %d\n", label, what,
            lowered(declaration, vhpiFullNameP), (int)value.value.intg);
    }
}

static void probe(const vhpiCbDataT* data)
{
    printf("#c probe line %d\n", (int)vhpi_get(vhpiLineNoP, data->obj));
    declarations("inner", inner_call, vhpiParamDecls, "param");
    declarations("inner", inner_call, vhpiVarDecls, "var");
    declarations("outer", outer_call, vhpiParamDecls, "param");
    declarations("outer", outer_call, vhpiVarDecls, "var");
    fflush(stdout);
}

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
    vhpiHandleT main_process = vhpi_handle_by_name("top.main", NULL);
    vhpiHandleT inner_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.inner", NULL));
    vhpiHandleT outer_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.outer", NULL));
    vhpiCbDataT callback;
    (void)data;
    outer_call = first_of(main_process, vhpiSeqProcCallStmtK);
    inner_call = first_of(outer_body, vhpiSeqProcCallStmtK);
    memset(&callback, 0, sizeof callback);
    callback.reason = vhpiCbStmt;
    callback.cb_rtn = probe;
    callback.obj = first_of(inner_body, vhpiVarAssignStmtK);
    if (vhpi_register_cb(&callback, vhpiReturnCb) == NULL) {
        printf("#c refused\n");
    }
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

// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for a signal parameter that the body of one subprogram passes
// to another, whole or as an element or a slice (IEEE 1076-2019 4.2.2.3,
// 19.12.2.2 c): at the first statement of each nested subprogram it lists
// the basic signals and local contributors of the formal the nested call
// elaborated. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A probe: the label, and the nested calls of the subprogram it probes; the
// one that is running is listed.
struct probe {
    const char* label;
    vhpiHandleT calls[3];
};
static struct probe probes[4] = { { "whole.assign", { NULL, NULL, NULL } },
    { "elem.assign", { NULL, NULL, NULL } },
    { "part.assign", { NULL, NULL, NULL } },
    { "peek.return", { NULL, NULL, NULL } } };

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

// Whether a driver belongs to the process "main".
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

// The contributors of a formal: drivers of main, each with the signal it
// drives, sorted by that name.
static void contributors_of(vhpiHandleT formal)
{
    vhpiHandleT contributors = vhpi_iterator(vhpiLocalContributors, formal);
    vhpiHandleT contributor;
    char names[8][96];
    int total = 0;
    int first;
    int second;
    while (contributors != NULL && total < 8
        && (contributor = vhpi_scan(contributors)) != NULL) {
        const char* signal = "-";
        vhpiHandleT driven = vhpi_handle(vhpiSignal, contributor);
        const char* kind = (const char*)vhpi_get_str(vhpiKindStrP, contributor);
        if (driven != NULL) {
            signal = (const char*)vhpi_get_str(vhpiFullNameP, driven);
        }
        snprintf(names[total], sizeof names[total], "%s(main=%d,%s)", kind,
            main_driver(contributor), signal);
        ++total;
    }
    for (first = 0; first < total; ++first) {
        for (second = first + 1; second < total; ++second) {
            if (strcmp(names[second], names[first]) < 0) {
                char swap[96];
                strcpy(swap, names[first]);
                strcpy(names[first], names[second]);
                strcpy(names[second], swap);
            }
        }
    }
    for (first = 0; first < total; ++first) {
        printf(" %s", names[first]);
    }
}

// The formals of a running nested call.
static void formals(vhpiHandleT call)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiParamDecls, call);
    vhpiHandleT formal;
    while (iterator != NULL && (formal = vhpi_scan(iterator)) != NULL) {
        printf("#c   %s basics=%d contributors:",
            (const char*)vhpi_get_str(vhpiNameP, formal),
            count(vhpiBasicSignals, formal));
        contributors_of(formal);
        printf("\n");
    }
}

static void report(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    const struct probe* probe = (const struct probe*)data->user_data;
    int index;
    vhpi_get_time(&now, NULL);
    printf("#c stmt %s %s line %d t=%u\n", probe->label,
        (const char*)vhpi_get_str(vhpiKindStrP, data->obj),
        (int)vhpi_get(vhpiLineNoP, data->obj), (unsigned)now.low);
    for (index = 0; index < 3; ++index) {
        // the call that is running, of those that call this subprogram
        if (probe->calls[index] != NULL
            && vhpi_handle(vhpiCurStackFrame, probe->calls[index]) != NULL) {
            formals(probe->calls[index]);
        }
    }
    fflush(stdout);
}

// The nth statement of the given kind in a process or body.
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

static void place(const char* body_name, int32_t kind, struct probe* probe)
{
    vhpiHandleT body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name(body_name, NULL));
    vhpiCbDataT request;
    memset(&request, 0, sizeof request);
    request.reason = vhpiCbStmt;
    request.cb_rtn = report;
    request.obj = nth_of(body, kind, 0);
    request.user_data = (char*)probe;
    if (vhpi_register_cb(&request, vhpiReturnCb) == NULL) {
        printf("#c refused %s\n", probe->label);
    }
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT outer_body = vhpi_handle(
        vhpiSubpBody, vhpi_handle_by_name("top.main.outer", NULL));
    (void)data;
    // whole(x); elem(x(2)); part(x(1 downto 0)); v := peek(x(2));
    // elem(x(c)); part(x(c + 1 downto c)); elem(x(twice(1)))
    probes[0].calls[0] = nth_of(outer_body, vhpiSeqProcCallStmtK, 0);
    probes[1].calls[0] = nth_of(outer_body, vhpiSeqProcCallStmtK, 1);
    probes[1].calls[1] = nth_of(outer_body, vhpiSeqProcCallStmtK, 3);
    probes[1].calls[2] = nth_of(outer_body, vhpiSeqProcCallStmtK, 5);
    probes[2].calls[0] = nth_of(outer_body, vhpiSeqProcCallStmtK, 2);
    probes[2].calls[1] = nth_of(outer_body, vhpiSeqProcCallStmtK, 4);
    probes[3].calls[0]
        = vhpi_handle(vhpiRhsExpr, nth_of(outer_body, vhpiVarAssignStmtK, 0));
    place("top.main.whole", vhpiSeqSigAssignStmtK, &probes[0]);
    place("top.main.elem", vhpiSeqSigAssignStmtK, &probes[1]);
    place("top.main.part", vhpiSeqSigAssignStmtK, &probes[2]);
    place("top.main.peek", vhpiReturnStmtK, &probes[3]);
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

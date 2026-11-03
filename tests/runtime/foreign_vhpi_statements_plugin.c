// SPDX-License-Identifier: Apache-2.0
// VHPI processes and statements conformance plug-in (IEEE 1076-2019 19.4.2,
// 19.11). Walks the statements of the root instance and prints the tree.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of four buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[4][128];
    static int next;
    const vhpiCharT* value = vhpi_get_str(property, object);
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 4;
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

// The file name without its directory.
static const char* file_of(vhpiHandleT object)
{
    const vhpiCharT* file = vhpi_get_str(vhpiFileNameP, object);
    const char* base = file != NULL ? strrchr((const char*)file, '/') : NULL;
    return base != NULL ? base + 1 : (file != NULL ? (const char*)file : "-");
}

static void line(int depth, const char* label)
{
    printf("#c %*s%s", depth * 2, "", label);
}

// The object a relation leads to, by class and full name, or "-".
static void expression(
    int depth, const char* label, vhpiHandleT object, vhpiOneToOneT relation)
{
    vhpiHandleT target = vhpi_handle(relation, object);
    line(depth, label);
    if (target == NULL) {
        printf("=-\n");
        return;
    }
    printf("=%s %s\n", text_property(target, vhpiKindStrP),
        text_property(target, vhpiFullNameP));
}

// The full names of the objects of an iteration.
static void names(
    int depth, const char* label, vhpiOneToManyT relation, vhpiHandleT object)
{
    vhpiHandleT iterator = vhpi_iterator(relation, object);
    vhpiHandleT item;
    line(depth, label);
    printf(":");
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf(" %s", text_property(item, vhpiFullNameP));
    }
    printf("\n");
}

static void statements(int depth, vhpiOneToManyT relation, vhpiHandleT owner);

// The waveform elements of an assignment, or of one of its parts.
static void waveform(int depth, vhpiHandleT owner)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiWaveformElems, owner);
    vhpiHandleT element;
    while (iterator != NULL && (element = vhpi_scan(iterator)) != NULL) {
        line(depth, "waveform elem");
        printf(" unaffected=%d line=%d\n",
            (int)vhpi_get(vhpiIsUnaffectedP, element),
            (int)vhpi_get(vhpiLineNoP, element));
        expression(depth + 1, "val", element, vhpiValExpr);
        expression(depth + 1, "time", element, vhpiTimeExpr);
    }
}

// The choices of a case branch or selected waveform.
static void choices(int depth, vhpiHandleT owner)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiChoices, owner);
    vhpiHandleT choice;
    line(depth, "choices:");
    while (iterator != NULL && (choice = vhpi_scan(iterator)) != NULL) {
        printf(" %s %s", text_property(choice, vhpiKindStrP),
            text_property(choice, vhpiFullNameP));
    }
    printf("\n");
}

// The branches of an if or case statement.
static void branches(int depth, vhpiHandleT owner)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiBranchs, owner);
    vhpiHandleT branch;
    while (iterator != NULL && (branch = vhpi_scan(iterator)) != NULL) {
        line(depth, "branch");
        printf(" kind=%s line=%d\n", text_property(branch, vhpiKindStrP),
            (int)vhpi_get(vhpiLineNoP, branch));
        expression(depth + 1, "cond", branch, vhpiCondExpr);
        if (vhpi_get(vhpiKindP, owner) == vhpiCaseStmtK) {
            choices(depth + 1, branch);
        }
        statements(depth + 1, vhpiSeqStmts, branch);
    }
}

static void describe(int depth, vhpiHandleT statement);

// The statements below an object.
static void statements(int depth, vhpiOneToManyT relation, vhpiHandleT owner)
{
    vhpiHandleT iterator = vhpi_iterator(relation, owner);
    vhpiHandleT statement;
    while (iterator != NULL && (statement = vhpi_scan(iterator)) != NULL) {
        describe(depth, statement);
    }
}

// The declarations in a process or loop.
static void declarations(int depth, vhpiHandleT owner)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiDecls, owner);
    vhpiHandleT declaration;
    while (iterator != NULL && (declaration = vhpi_scan(iterator)) != NULL) {
        line(depth, "decl");
        printf(" %s %s line=%d\n", text_property(declaration, vhpiKindStrP),
            text_property(declaration, vhpiFullNameP),
            (int)vhpi_get(vhpiLineNoP, declaration));
    }
}

static void describe(int depth, vhpiHandleT statement)
{
    vhpiIntT kind = vhpi_get(vhpiKindP, statement);
    vhpiHandleT parameter;
    line(depth, text_property(statement, vhpiKindStrP));
    printf(" name=%s case=%s full=%s label=%s line=%d file=%s seq=%d\n",
        text_property(statement, vhpiNameP),
        text_property(statement, vhpiCaseNameP),
        text_property(statement, vhpiFullNameP),
        text_property(statement, vhpiLabelNameP),
        (int)vhpi_get(vhpiLineNoP, statement), file_of(statement),
        kind == vhpiRootInstK ? -1 : (int)vhpi_get(vhpiIsSeqStmtP, statement));
    switch (kind) {
    case vhpiRootInstK:
    case vhpiCompInstStmtK:
    case vhpiBlockStmtK:
    case vhpiForGenerateK:
    case vhpiIfGenerateK:
        statements(depth + 1, vhpiStmts, statement);
        break;
    case vhpiProcessStmtK:
        line(depth + 1, "process");
        printf(" postponed=%d passive=%d foreign=%d\n",
            (int)vhpi_get(vhpiIsPostponedP, statement),
            (int)vhpi_get(vhpiIsPassiveP, statement),
            (int)vhpi_get(vhpiIsForeignP, statement));
        printf("#c %*sparent is region=%d\n", depth * 2 + 2, "",
            vhpi_compare_handles(vhpi_handle(vhpiParent, statement),
                vhpi_handle(vhpiUpperRegion, statement)));
        names(depth + 1, "sensitivities", vhpiSensitivitys, statement);
        declarations(depth + 1, statement);
        statements(depth + 1, vhpiSeqStmts, statement);
        break;
    case vhpiSimpleSigAssignStmtK:
    case vhpiSeqSigAssignStmtK:
        line(depth + 1, "assign");
        printf(" guarded=%d transport=%d postponed=%d\n",
            (int)vhpi_get(vhpiIsGuardedP, statement),
            (int)vhpi_get(vhpiIsTransportP, statement),
            kind == vhpiSimpleSigAssignStmtK
                ? (int)vhpi_get(vhpiIsPostponedP, statement)
                : 0);
        expression(depth + 1, "lhs", statement, vhpiLhsExpr);
        expression(depth + 1, "reject", statement, vhpiRejectTime);
        waveform(depth + 1, statement);
        break;
    case vhpiCondSigAssignStmtK: {
        vhpiHandleT iterator = vhpi_iterator(vhpiCondWaveforms, statement);
        vhpiHandleT part;
        expression(depth + 1, "lhs", statement, vhpiLhsExpr);
        while (iterator != NULL && (part = vhpi_scan(iterator)) != NULL) {
            line(depth + 1, "cond waveform");
            printf(" line=%d\n", (int)vhpi_get(vhpiLineNoP, part));
            expression(depth + 2, "cond", part, vhpiCondExpr);
            waveform(depth + 2, part);
        }
        break;
    }
    case vhpiSelectSigAssignStmtK: {
        vhpiHandleT iterator = vhpi_iterator(vhpiSelectWaveforms, statement);
        vhpiHandleT part;
        expression(depth + 1, "lhs", statement, vhpiLhsExpr);
        expression(depth + 1, "select", statement, vhpiSelectExpr);
        while (iterator != NULL && (part = vhpi_scan(iterator)) != NULL) {
            line(depth + 1, "select waveform");
            printf(" line=%d\n", (int)vhpi_get(vhpiLineNoP, part));
            choices(depth + 2, part);
            waveform(depth + 2, part);
        }
        break;
    }
    case vhpiConcAssertStmtK:
    case vhpiSeqAssertStmtK:
        line(depth + 1, "assert");
        printf(" passive=%d\n",
            kind == vhpiConcAssertStmtK
                ? (int)vhpi_get(vhpiIsPassiveP, statement)
                : -1);
        expression(depth + 1, "cond", statement, vhpiCondExpr);
        expression(depth + 1, "report", statement, vhpiReportExpr);
        expression(depth + 1, "severity", statement, vhpiSeverityExpr);
        break;
    case vhpiReportStmtK:
        expression(depth + 1, "report", statement, vhpiReportExpr);
        expression(depth + 1, "severity", statement, vhpiSeverityExpr);
        break;
    case vhpiVarAssignStmtK:
        expression(depth + 1, "lhs", statement, vhpiLhsExpr);
        expression(depth + 1, "rhs", statement, vhpiRhsExpr);
        break;
    case vhpiIfStmtK:
        branches(depth + 1, statement);
        break;
    case vhpiCaseStmtK:
        expression(depth + 1, "case", statement, vhpiCaseExpr);
        branches(depth + 1, statement);
        break;
    case vhpiForLoopK:
        parameter = vhpi_handle(vhpiParamDecl, statement);
        line(depth + 1, "param");
        printf(" %s %s\n", text_property(parameter, vhpiKindStrP),
            text_property(parameter, vhpiNameP));
        statements(depth + 1, vhpiSeqStmts, statement);
        break;
    case vhpiWhileLoopK:
        expression(depth + 1, "cond", statement, vhpiCondExpr);
        statements(depth + 1, vhpiSeqStmts, statement);
        break;
    case vhpiForeverLoopK:
        statements(depth + 1, vhpiSeqStmts, statement);
        break;
    case vhpiExitStmtK:
    case vhpiNextStmtK:
        line(depth + 1, "loop label");
        printf("=%s\n", text_property(statement, vhpiLoopLabelNameP));
        expression(depth + 1, "cond", statement, vhpiCondExpr);
        break;
    case vhpiReturnStmtK:
        expression(depth + 1, "return", statement, vhpiReturnExpr);
        break;
    case vhpiWaitStmtK:
        names(depth + 1, "signames", vhpiSigNames, statement);
        expression(depth + 1, "cond", statement, vhpiCondExpr);
        expression(depth + 1, "timeout", statement, vhpiTimeOutExpr);
        break;
    default:
        break;
    }
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT by_name;
    (void)data;
    describe(0, root);
    // A process is found by its label, an unlabeled one by its implicit one.
    by_name = vhpi_handle_by_name("top.clock", NULL);
    printf("#c by name clock=%s\n", text_property(by_name, vhpiKindStrP));
    by_name = vhpi_handle_by_name("top.blk._p0", NULL);
    printf("#c by name blk._p0=%s\n", text_property(by_name, vhpiKindStrP));
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

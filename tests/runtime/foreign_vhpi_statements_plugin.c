// SPDX-License-Identifier: Apache-2.0
// VHPI processes and statements conformance plug-in (IEEE 1076-2019 19.4.2,
// 19.11). Walks the statements of the root instance and prints the tree.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Set once the walk reaches the block of expression objects: the relations
// to expressions are then printed as trees (IEEE 1076-2019 19.8).
static int deep;

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

static void tree(int depth, const char* label, vhpiHandleT target);

// The association elements of a call: their actuals and formals.
static void parameters(int depth, vhpiHandleT call)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiParamAssocs, call);
    vhpiHandleT assoc;
    while (iterator != NULL && (assoc = vhpi_scan(iterator)) != NULL) {
        vhpiHandleT formal = vhpi_handle(vhpiFormal, assoc);
        line(depth, "assoc");
        printf(" pos=%d named=%d open=%d\n", (int)vhpi_get(vhpiPositionP, assoc),
            (int)vhpi_get(vhpiIsNamedP, assoc),
            (int)vhpi_get(vhpiIsOpenP, assoc));
        tree(depth + 1, "actual", vhpi_handle(vhpiActual, assoc));
        line(depth + 1, "formal");
        printf("=%s\n", formal == NULL ? "-" : "set");
    }
}

static void choices(int depth, vhpiHandleT owner);

// The element associations of an aggregate.
static void elements(int depth, vhpiHandleT aggregate)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiElemAssocs, aggregate);
    vhpiHandleT assoc;
    while (iterator != NULL && (assoc = vhpi_scan(iterator)) != NULL) {
        line(depth, "elem assoc");
        printf(" named=%d\n", (int)vhpi_get(vhpiIsNamedP, assoc));
        choices(depth + 1, assoc);
        tree(depth + 1, "expr", vhpi_handle(vhpiExpr, assoc));
    }
}

// An expression object with what its class holds.
static void tree(int depth, const char* label, vhpiHandleT target)
{
    line(depth, label);
    if (target == NULL) {
        printf("=-\n");
        return;
    }
    printf("=%s %s", text_property(target, vhpiKindStrP),
        text_property(target, vhpiFullNameP));
    switch (vhpi_get(vhpiKindP, target)) {
    case vhpiFuncCallK:
        printf(" operator=%d params=%d\n",
            (int)vhpi_get(vhpiIsOperatorP, target),
            (int)vhpi_get(vhpiNumParamsP, target));
        parameters(depth + 1, target);
        break;
    case vhpiAggregateK:
        printf("\n");
        elements(depth + 1, target);
        break;
    case vhpiIntLiteralK:
        printf(" intval=%d\n", (int)vhpi_get(vhpiIntValP, target));
        break;
    default:
        printf("\n");
        break;
    }
}

// The object a relation leads to, by class and full name, or "-".
static void expression(
    int depth, const char* label, vhpiHandleT object, vhpiOneToOneT relation)
{
    vhpiHandleT target = vhpi_handle(relation, object);
    if (deep) {
        tree(depth, label, target);
        return;
    }
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

// The process equivalent to a concurrent statement.
static void equivalent(int depth, vhpiHandleT statement)
{
    vhpiHandleT process = vhpi_handle(vhpiEqProcessStmt, statement);
    line(depth, "eq process");
    if (process == NULL) {
        printf("=-\n");
        return;
    }
    printf("=%s postponed=%d passive=%d foreign=%d lines=%d/%d\n",
        text_property(process, vhpiKindStrP),
        (int)vhpi_get(vhpiIsPostponedP, process),
        (int)vhpi_get(vhpiIsPassiveP, process),
        (int)vhpi_get(vhpiIsForeignP, process),
        (int)vhpi_get(vhpiBeginLineNoP, process),
        (int)vhpi_get(vhpiEndLineNoP, process));
}

// The call a procedure call statement makes.
static void call(int depth, vhpiHandleT statement)
{
    line(depth, "call");
    printf(" params=%d\n", (int)vhpi_get(vhpiNumParamsP, statement));
    parameters(depth + 1, statement);
}

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
        if (strcmp(text_property(statement, vhpiNameP), "ext") == 0) {
            deep = 1;
        }
        // The block of subprograms is walked by name (sub_model).
        if (strcmp(text_property(statement, vhpiNameP), "sub") == 0) {
            break;
        }
        statements(depth + 1, vhpiStmts, statement);
        break;
    case vhpiProcessStmtK:
        line(depth + 1, "process");
        printf(" postponed=%d passive=%d foreign=%d\n",
            (int)vhpi_get(vhpiIsPostponedP, statement),
            (int)vhpi_get(vhpiIsPassiveP, statement),
            (int)vhpi_get(vhpiIsForeignP, statement));
        line(depth + 1, "lines");
        printf(" begin=%d end=%d\n", (int)vhpi_get(vhpiBeginLineNoP, statement),
            (int)vhpi_get(vhpiEndLineNoP, statement));
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
        if (deep && kind == vhpiSimpleSigAssignStmtK) {
            equivalent(depth + 1, statement);
        }
        break;
    case vhpiSeqProcCallStmtK:
        call(depth + 1, statement);
        break;
    case vhpiConcProcCallStmtK:
        call(depth + 1, statement);
        equivalent(depth + 1, statement);
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

// A user function run by vhpi_protected_call (IEEE 1076-2019 23.26).
static int touch(vhpiHandleT variable, void* data)
{
    int* count = (int*)data;
    ++*count;
    printf("#c protected function var=%s data=%d\n",
        text_property(variable, vhpiNameP), *count);
    return 40 + *count;
}

// A function that calls vhpi_protected_call for the variable it holds.
static int nested(vhpiHandleT variable, void* data)
{
    int inner = vhpi_protected_call(
        variable, (vhpiUserFctT)(uintptr_t)touch, data);
    printf("#c nested call result=%d error=%d\n", inner,
        (int)vhpi_check_error(NULL));
    return 7;
}

static void protected_variable(void)
{
    vhpiHandleT variable = vhpi_handle_by_name("top.ext.ctr", NULL);
    vhpiHandleT other = vhpi_handle_by_name("top.a", NULL);
    int count = 0;
    int result;
    printf("#c protected var kind=%s shared=%d protected=%d\n",
        text_property(variable, vhpiKindStrP),
        (int)vhpi_get(vhpiIsSharedP, variable),
        (int)vhpi_get(vhpiIsProtectedTypeP, variable));
    result = vhpi_protected_call(variable, (vhpiUserFctT)(uintptr_t)touch, &count);
    printf("#c protected call result=%d count=%d error=%d\n", result, count,
        (int)vhpi_check_error(NULL));
    result = vhpi_protected_call(
        variable, (vhpiUserFctT)(uintptr_t)nested, &count);
    printf("#c outer call result=%d count=%d error=%d\n", result, count,
        (int)vhpi_check_error(NULL));
    result = vhpi_protected_call(variable, (vhpiUserFctT)(uintptr_t)touch, &count);
    printf("#c call after nested result=%d count=%d error=%d\n", result, count,
        (int)vhpi_check_error(NULL));
    result = vhpi_protected_call(other, (vhpiUserFctT)(uintptr_t)touch, &count);
    printf("#c signal result=%d count=%d error=%d\n", result, count,
        (int)vhpi_check_error(NULL));
}

// The result of a vhpi_create that should have failed.
static void rejected(const char* label, vhpiHandleT created)
{
    vhpiErrorInfoT info;
    int failed = vhpi_check_error(&info);
    printf("#c %s: handle=%s error=%d\n", label,
        created == NULL ? "null" : "set", failed);
}

static void collections(vhpiHandleT root)
{
    vhpiHandleT a = vhpi_handle_by_name("top.a", NULL);
    vhpiHandleT b = vhpi_handle_by_name("top.b", NULL);
    vhpiHandleT s = vhpi_handle_by_name("top.s", NULL);
    vhpiHandleT first = vhpi_create(vhpiAnyCollectionK, NULL, a);
    vhpiHandleT same = vhpi_create(vhpiAnyCollectionK, first, b);
    vhpiHandleT copy = vhpi_create(vhpiAnyCollectionK, NULL, first);
    vhpiHandleT grown = vhpi_create(vhpiAnyCollectionK, copy, s);
    vhpiErrorInfoT info;
    printf("#c collection kind=%s first=%d copy=%d same=%d grown=%d\n",
        text_property(copy, vhpiKindStrP), (int)vhpi_get(vhpiNumMembersP, first),
        (int)vhpi_get(vhpiNumMembersP, copy),
        vhpi_compare_handles(first, same), vhpi_compare_handles(copy, grown));
    names(0, "members", vhpiMembers, copy);
    rejected("driver collection of a signal",
        vhpi_create(vhpiDriverCollectionK, NULL, a));
    rejected("driver collection of an any collection",
        vhpi_create(vhpiDriverCollectionK, NULL, first));
    rejected("handle1 not a collection", vhpi_create(vhpiAnyCollectionK, a, b));
    rejected("no member", vhpi_create(vhpiAnyCollectionK, NULL, NULL));
    rejected("process", vhpi_create(vhpiProcessStmtK, root, NULL));
    if (vhpi_check_error(&info)) {
        printf("#c process message not implemented=%d\n",
            strstr((const char*)info.message, "not implemented") != NULL);
    }
    rejected("driver", vhpi_create(vhpiDriverK, a, NULL));
}

// --- subprograms, expression classes and process variables (19.8, 19.10) ---

static const char* mode_name(vhpiIntT mode)
{
    switch (mode) {
    case vhpiInMode:
        return "in";
    case vhpiOutMode:
        return "out";
    case vhpiInoutMode:
        return "inout";
    default:
        return "-";
    }
}

static const char* type_name(vhpiHandleT object)
{
    vhpiHandleT type = vhpi_handle(vhpiType, object);
    return type == NULL ? "-" : text_property(type, vhpiNameP);
}

static void expr_tree(int depth, const char* label, vhpiHandleT target);

// The association elements of a call with their formals and actuals.
static void assocs(int depth, vhpiHandleT call)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiParamAssocs, call);
    vhpiHandleT assoc;
    while (iterator != NULL && (assoc = vhpi_scan(iterator)) != NULL) {
        vhpiHandleT formal = vhpi_handle(vhpiFormal, assoc);
        line(depth, "assoc");
        printf(" pos=%d formal=%s %s\n", (int)vhpi_get(vhpiPositionP, assoc),
            formal == NULL ? "-" : text_property(formal, vhpiKindStrP),
            formal == NULL ? "-" : text_property(formal, vhpiNameP));
        expr_tree(depth + 1, "actual", vhpi_handle(vhpiActual, assoc));
    }
}

// An expression with the properties and relations of its class.
static void expr_tree(int depth, const char* label, vhpiHandleT target)
{
    vhpiIntT kind;
    vhpiHandleT iterator;
    vhpiHandleT item;
    line(depth, label);
    if (target == NULL) {
        printf("=-\n");
        return;
    }
    kind = vhpi_get(vhpiKindP, target);
    printf("=%s %s", text_property(target, vhpiKindStrP),
        text_property(target, vhpiFullNameP));
    switch (kind) {
    case vhpiFuncCallK:
        // The type of a call is printed for a function, not for a predefined
        // operator.
        if (vhpi_get(vhpiIsBuiltInP, target)) {
            printf(" builtin=1 static=%d params=%d\n",
                (int)vhpi_get(vhpiStaticnessP, target),
                (int)vhpi_get(vhpiNumParamsP, target));
        } else {
            printf(" builtin=0 static=%d type=%s params=%d\n",
                (int)vhpi_get(vhpiStaticnessP, target), type_name(target),
                (int)vhpi_get(vhpiNumParamsP, target));
        }
        assocs(depth + 1, target);
        break;
    case vhpiAggregateK:
        printf(" static=%d\n", (int)vhpi_get(vhpiStaticnessP, target));
        break;
    case vhpiTypeConvK:
    case vhpiQualifiedExprK:
        printf(" type=%s static=%d\n", type_name(target),
            (int)vhpi_get(vhpiStaticnessP, target));
        expr_tree(depth + 1, "expr", vhpi_handle(vhpiExpr, target));
        break;
    case vhpiSimpAttrNameK:
    case vhpiParamAttrNameK:
    case vhpiUserAttrNameK:
        printf("\n");
        item = vhpi_handle(vhpiPrefix, target);
        line(depth + 1, "prefix");
        printf("=%s\n", item == NULL ? "-" : text_property(item, vhpiFullNameP));
        iterator = vhpi_iterator(vhpiParamExprs, target);
        while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
            expr_tree(depth + 1, "param", item);
        }
        break;
    case vhpiIndexedNameK:
    case vhpiSliceNameK:
        printf("\n");
        item = vhpi_handle(vhpiPrefix, target);
        line(depth + 1, "prefix");
        printf("=%s\n", item == NULL ? "-" : text_property(item, vhpiFullNameP));
        iterator = vhpi_iterator(vhpiIndexExprs, target);
        while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
            expr_tree(depth + 1, "index", item);
        }
        break;
    default:
        printf("\n");
        break;
    }
}

static void body_statements(int depth, vhpiHandleT owner);

// A statement of a subprogram body or process, with its expressions.
static void body_statement(int depth, vhpiHandleT statement)
{
    vhpiIntT kind = vhpi_get(vhpiKindP, statement);
    vhpiHandleT iterator;
    vhpiHandleT branch;
    line(depth, text_property(statement, vhpiKindStrP));
    printf(" line=%d\n", (int)vhpi_get(vhpiLineNoP, statement));
    switch (kind) {
    case vhpiVarAssignStmtK:
        expr_tree(depth + 1, "lhs", vhpi_handle(vhpiLhsExpr, statement));
        expr_tree(depth + 1, "rhs", vhpi_handle(vhpiRhsExpr, statement));
        break;
    case vhpiSeqProcCallStmtK:
        printf("#c %*sbuiltin=%d params=%d\n", depth * 2 + 2, "",
            (int)vhpi_get(vhpiIsBuiltInP, statement),
            (int)vhpi_get(vhpiNumParamsP, statement));
        assocs(depth + 1, statement);
        break;
    case vhpiReportStmtK:
        expr_tree(depth + 1, "report", vhpi_handle(vhpiReportExpr, statement));
        break;
    case vhpiIfStmtK:
        iterator = vhpi_iterator(vhpiBranchs, statement);
        while (iterator != NULL && (branch = vhpi_scan(iterator)) != NULL) {
            line(depth + 1, "branch");
            printf(" line=%d\n", (int)vhpi_get(vhpiLineNoP, branch));
            expr_tree(depth + 2, "cond", vhpi_handle(vhpiCondExpr, branch));
            body_statements(depth + 2, branch);
        }
        break;
    default:
        break;
    }
}

static void body_statements(int depth, vhpiHandleT owner)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiSeqStmts, owner);
    vhpiHandleT statement;
    while (iterator != NULL && (statement = vhpi_scan(iterator)) != NULL) {
        body_statement(depth, statement);
    }
}

// A subprogram declaration, its parameters and its body (Figure 34).
static void subprogram(const char* path)
{
    vhpiHandleT declaration = vhpi_handle_by_name(path, NULL);
    vhpiHandleT body;
    vhpiHandleT iterator;
    vhpiHandleT item;
    int procedure;
    if (declaration == NULL) {
        printf("#c subprogram %s missing\n", path);
        return;
    }
    procedure = vhpi_get(vhpiKindP, declaration) == vhpiProcDeclK;
    printf("#c subprogram %s %s params=%d", text_property(declaration, vhpiNameP),
        text_property(declaration, vhpiKindStrP),
        (int)vhpi_get(vhpiNumParamsP, declaration));
    if (!procedure) {
        printf(" pure=%d return=%s", (int)vhpi_get(vhpiIsPureP, declaration),
            text_property(vhpi_handle(vhpiReturnType, declaration), vhpiNameP));
    }
    printf("\n");
    iterator = vhpi_iterator(vhpiParamDecls, declaration);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c   param %s %s", text_property(item, vhpiKindStrP),
            text_property(item, vhpiNameP));
        if (procedure) {
            printf(" mode=%s", mode_name(vhpi_get(vhpiModeP, item)));
        }
        printf(" type=%s\n", type_name(item));
    }
    body = vhpi_handle(vhpiSubpBody, declaration);
    if (body == NULL) {
        printf("#c   no body\n");
        return;
    }
    printf("#c   body %s lines=%d/%d foreign=%d\n",
        text_property(body, vhpiKindStrP), (int)vhpi_get(vhpiBeginLineNoP, body),
        (int)vhpi_get(vhpiEndLineNoP, body), (int)vhpi_get(vhpiIsForeignP, body));
    iterator = vhpi_iterator(vhpiDecls, body);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c   decl %s %s\n", text_property(item, vhpiKindStrP),
            text_property(item, vhpiNameP));
    }
    body_statements(1, body);
}

// A package subprogram whose body is declared in the package body
// (IEEE 1076-2019 4.8, Figure 34).
static void package_subprogram(const char* package_name)
{
    vhpiHandleT packages = vhpi_iterator(vhpiPackInsts, NULL);
    vhpiHandleT package;
    while (packages != NULL && (package = vhpi_scan(packages)) != NULL) {
        vhpiHandleT declarations;
        vhpiHandleT item;
        if (strcmp(text_property(package, vhpiNameP), package_name) != 0) {
            continue;
        }
        declarations = vhpi_iterator(vhpiDecls, package);
        while (declarations != NULL && (item = vhpi_scan(declarations)) != NULL) {
            vhpiHandleT body;
            if (vhpi_get(vhpiKindP, item) != vhpiFuncDeclK) {
                continue;
            }
            body = vhpi_handle(vhpiSubpBody, item);
            printf("#c package %s subprogram %s body=%d", package_name,
                text_property(item, vhpiNameP), body != NULL);
            if (body != NULL) {
                printf(" %s lines=%d/%d", text_property(body, vhpiKindStrP),
                    (int)vhpi_get(vhpiBeginLineNoP, body),
                    (int)vhpi_get(vhpiEndLineNoP, body));
            }
            printf("\n");
        }
    }
}

// The guard expression of a block statement.
static void guard_expression(const char* path)
{
    vhpiHandleT block = vhpi_handle_by_name(path, NULL);
    vhpiHandleT guard = vhpi_handle(vhpiGuardExpr, block);
    printf("#c block %s guard=%s\n", text_property(block, vhpiNameP),
        guard == NULL ? "none" : text_property(guard, vhpiKindStrP));
    // The implicit signal GUARD (11.2, Figure 23).
    guard = vhpi_handle(vhpiGuardSig, block);
    printf("#c block %s guardsig=%s %s guarded=%d\n",
        text_property(block, vhpiNameP),
        guard == NULL ? "none" : text_property(guard, vhpiKindStrP),
        guard == NULL ? "-" : text_property(guard, vhpiFullNameP),
        (int)vhpi_get(vhpiIsGuardedP, block));
}

// The interface of a component declaration.
static void component(const char* path)
{
    vhpiHandleT declaration = vhpi_handle_by_name(path, NULL);
    vhpiHandleT iterator;
    vhpiHandleT item;
    printf("#c component %s\n", text_property(declaration, vhpiNameP));
    iterator = vhpi_iterator(vhpiGenericDecls, declaration);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c   generic %s %s\n", text_property(item, vhpiKindStrP),
            text_property(item, vhpiNameP));
    }
    iterator = vhpi_iterator(vhpiPortDecls, declaration);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c   port %s %s mode=%s\n", text_property(item, vhpiKindStrP),
            text_property(item, vhpiNameP),
            mode_name(vhpi_get(vhpiModeP, item)));
    }
}

static int read_int(vhpiHandleT variable, int* out)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    if (vhpi_get_value(variable, &value) != 0) {
        return 0;
    }
    *out = (int)value.value.intg;
    return 1;
}

static int deposit(vhpiHandleT variable, int number, vhpiPutValueModeT mode)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = number;
    return vhpi_put_value(variable, &value, mode);
}

// Variables of a process are read and deposited (22.5.2).
static void variables(void)
{
    vhpiHandleT r = vhpi_handle_by_name("top.sub.expr.r", NULL);
    vhpiHandleT count = vhpi_handle_by_name("top.clock.count", NULL);
    int before = -1;
    int after = -1;
    int put;
    int forced;
    int read = read_int(r, &before);
    put = deposit(r, 9, vhpiDeposit);
    (void)read_int(r, &after);
    forced = deposit(r, 1, vhpiForce);
    printf("#c variable %s read=%d value=%d put=%d now=%d force=%d\n",
        text_property(r, vhpiNameP), read, before, put, after, forced != 0);
    // The process `clock` resumes at 5 ns and counts on from the deposit.
    before = -1;
    read = read_int(count, &before);
    put = deposit(count, 10, vhpiDepositPropagate);
    printf("#c variable %s read=%d value=%d put=%d\n",
        text_property(count, vhpiNameP), read, before, put);
}

static void later(const vhpiCbDataT* data)
{
    vhpiHandleT count = vhpi_handle_by_name("top.clock.count", NULL);
    int now = -1;
    (void)data;
    (void)read_int(count, &now);
    printf("#c variable count after the process ran: %d\n", now);
    fflush(stdout);
}

static void sub_model(void)
{
    static vhpiTimeT delay;
    vhpiCbDataT data;
    vhpiHandleT process = vhpi_handle_by_name("top.sub.expr", NULL);
    vhpiHandleT concurrent = vhpi_handle_by_name("top.sub.cp", NULL);
    subprogram("top.sub.add1");
    subprogram("top.sub.note");
    subprogram("top.sub.drive");
    package_subprogram("pk");
    // The guard expression of a guarded block (11.2, Figure 23).
    guard_expression("top.gb");
    guard_expression("top.sub");
    component("top.sub.cmp");
    printf("#c process expr\n");
    body_statements(1, process);
    printf("#c concurrent call %s params=%d\n",
        text_property(concurrent, vhpiKindStrP),
        (int)vhpi_get(vhpiNumParamsP, concurrent));
    assocs(1, concurrent);
    variables();
    memset(&data, 0, sizeof data);
    delay.low = 6000000U;
    data.reason = vhpiCbAfterDelay;
    data.time = &delay;
    data.cb_rtn = later;
    (void)vhpi_register_cb(&data, 0);
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
    protected_variable();
    collections(root);
    sub_model();
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

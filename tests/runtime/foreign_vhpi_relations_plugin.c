// SPDX-License-Identifier: Apache-2.0
// VHPI relation conformance plug-in (IEEE 1076-2019 19.4, 19.10, 19.12):
// component declarations, subprogram declarations of calls, the signals a
// procedure call drives, integer range bounds, simulated nets, library
// declarations and design units, and the string properties that go with
// them. Lines starting with "#c " form the checked trace.
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
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
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

// The full name of the object a relation leads to, or "-".
static const char* related(vhpiHandleT object, vhpiOneToOneT relation)
{
    vhpiHandleT target = vhpi_handle(relation, object);
    return target == NULL ? "-" : text_property(target, vhpiFullNameP);
}

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

static void instances(void)
{
    vhpiHandleT u1 = vhpi_handle_by_name("top.u1", NULL);
    vhpiHandleT u2 = vhpi_handle_by_name("top.u2", NULL);
    const char* name;
    int failed;
    printf("#c u1 kind=%d compdecl=%s compname=%s\n",
        (int)vhpi_get(vhpiCompInstKindP, u1), related(u1, vhpiCompDecl),
        text_property(u1, vhpiCompNameP));
    // A direct entity instantiation has no component.
    name = text_property(u2, vhpiCompNameP);
    failed = vhpi_check_error(NULL) != 0;
    printf("#c u2 kind=%d compdecl=%s compname=%s error=%d\n",
        (int)vhpi_get(vhpiCompInstKindP, u2), related(u2, vhpiCompDecl), name,
        failed);
}

static void calls(void)
{
    vhpiHandleT process = vhpi_handle_by_name("top.p", NULL);
    vhpiHandleT statements = vhpi_iterator(vhpiSeqStmts, process);
    vhpiHandleT assign = vhpi_scan(statements);
    vhpiHandleT call = vhpi_handle(vhpiRhsExpr, assign);
    vhpiHandleT sigs;
    vhpiHandleT driven;
    printf("#c call %s funcdecl=%s subpdecl=%s\n",
        text_property(call, vhpiNameP), related(call, vhpiFuncDecl),
        related(call, vhpiSubpDecl));
    call = vhpi_scan(statements);
    sigs = vhpi_iterator(vhpiDrivenSigs, call);
    driven = sigs != NULL ? vhpi_scan(sigs) : NULL;
    printf("#c call %s funcdecl=%s subpdecl=%s drivensigs=%s\n",
        text_property(call, vhpiNameP), related(call, vhpiFuncDecl),
        related(call, vhpiSubpDecl), text_property(driven, vhpiFullNameP));
    assign = vhpi_scan(statements);
    call = vhpi_handle(vhpiRhsExpr, assign);
    printf("#c call %s isoperator=%d opname=%s subpdecl=%s\n",
        text_property(call, vhpiNameP), (int)vhpi_get(vhpiIsOperatorP, call),
        text_property(call, (vhpiStrPropertyT)DEPRECATED_vhpiOpNameP),
        related(call, vhpiSubpDecl));
}

static void ranges(void)
{
    vhpiHandleT subtype = vhpi_handle_by_name("top.small", NULL);
    vhpiHandleT range = vhpi_handle(vhpiConstraint, subtype);
    printf("#c range kind=%s left=%s right=%s\n",
        text_property(range, vhpiKindStrP),
        text_property(vhpi_handle(vhpiLeftExpr, range), vhpiNameP),
        text_property(vhpi_handle(vhpiRightExpr, range), vhpiNameP));
}

static void nets(void)
{
    vhpiHandleT s = vhpi_handle_by_name("top.s", NULL);
    vhpiHandleT t = vhpi_handle_by_name("top.t", NULL);
    vhpiHandleT process = vhpi_handle_by_name("top.p", NULL);
    vhpiHandleT q = vhpi_handle_by_name("top.q", NULL);
    printf("#c simnet of s=%s of t=%s of a process=%s\n",
        related(s, vhpiSimNet), related(t, vhpiSimNet),
        related(process, vhpiSimNet));
    printf("#c contributors of t=%d local=%d\n",
        count((vhpiOneToManyT)DEPRECATED_vhpiContributors, t),
        count(vhpiLocalContributors, t));
    printf("#c sensitivities of q=%d signames=%d\n",
        count(vhpiSensitivities, q), count(vhpiSigNames, q));
}

static void libraries(void)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT unit = vhpi_handle(vhpiDesignUnit, root);
    vhpiHandleT library = vhpi_handle(vhpiLibraryDecl, unit);
    vhpiHandleT units = vhpi_iterator(vhpiDesignUnits, library);
    vhpiHandleT item;
    printf("#c library %s kind=%s\n", text_property(library, vhpiLibLogicalNameP),
        text_property(library, vhpiKindStrP));
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        printf("#c unit %s %s\n", text_property(item, vhpiKindStrP),
            text_property(item, vhpiNameP));
    }
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    instances();
    calls();
    ranges();
    nets();
    libraries();
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

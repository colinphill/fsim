// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for operators whose operands are literals, which the design
// folds but the static model keeps as calls of the predefined operator
// (IEEE 1076-2019 19.8, 9.4.2). Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of eight buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[8][128];
    static int next;
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
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

static const char* staticness(vhpiIntT value)
{
    switch (value) {
    case vhpiLocallyStatic:
        return "locally";
    case vhpiGloballyStatic:
        return "globally";
    case vhpiDynamic:
        return "dynamic";
    default:
        return "?";
    }
}

static void operands(vhpiHandleT call)
{
    vhpiHandleT associations = vhpi_iterator(vhpiParamAssocs, call);
    vhpiHandleT association;
    const char* separator = "";
    printf(" operands=");
    while (associations != NULL
        && (association = vhpi_scan(associations)) != NULL) {
        vhpiHandleT actual = vhpi_handle(vhpiActual, association);
        printf("%s%s", separator, text_property(actual, vhpiKindStrP));
        if (actual != NULL && vhpi_get(vhpiKindP, actual) == vhpiIntLiteralK) {
            printf(":%d", (int)vhpi_get(vhpiIntValP, actual));
        }
        separator = ",";
    }
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT statements
        = vhpi_iterator(vhpiStmts, vhpi_handle(vhpiRootInst, NULL));
    vhpiHandleT statement;
    (void)data;
    while (statements != NULL && (statement = vhpi_scan(statements)) != NULL) {
        vhpiHandleT elements;
        vhpiHandleT element;
        vhpiHandleT value;
        if (vhpi_get(vhpiKindP, statement) != vhpiSimpleSigAssignStmtK) {
            continue;
        }
        elements = vhpi_iterator(vhpiWaveformElems, statement);
        element = elements != NULL ? vhpi_scan(elements) : NULL;
        value = element != NULL ? vhpi_handle(vhpiValExpr, element) : NULL;
        printf("#c %s %s name=%s operator=%d builtin=%d static=%s params=%d",
            text_property(vhpi_handle(vhpiLhsExpr, statement), vhpiNameP),
            text_property(value, vhpiKindStrP), text_property(value, vhpiNameP),
            value != NULL ? (int)vhpi_get(vhpiIsOperatorP, value) : -1,
            value != NULL ? (int)vhpi_get(vhpiIsBuiltInP, value) : -1,
            value != NULL ? staticness(vhpi_get(vhpiStaticnessP, value)) : "-",
            value != NULL ? (int)vhpi_get(vhpiNumParamsP, value) : -1);
        if (value != NULL) {
            operands(value);
        }
        printf("\n");
    }
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

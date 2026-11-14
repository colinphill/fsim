// SPDX-License-Identifier: Apache-2.0
// The match items of sequence operands (IEEE 1800-2023 16.10, 37.54
// vpiMatchItem): every operand of a sequence expression is asked for its
// match items, which are printed in order with their kind, operator, left and
// right sides or call name. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static const char* type_name(PLI_INT32 type)
{
    switch (type) {
    case vpiOperation:
        return "vpiOperation";
    case vpiAssignment:
        return "vpiAssignment";
    case vpiSysTaskCall:
        return "vpiSysTaskCall";
    case vpiSysFuncCall:
        return "vpiSysFuncCall";
    case vpiConstant:
        return "vpiConstant";
    case vpiRefObj:
        return "vpiRefObj";
    case vpiReg:
        return "vpiReg";
    case vpiIntVar:
        return "vpiIntVar";
    default:
        return "other";
    }
}

// Whether a reference object prints its vpiActual (the s_pn sequence).
static int show_actual;

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = object != NULL ? vpi_get_str(property, object) : NULL;
    return value != NULL ? value : "none";
}

static const char* indent(int depth)
{
    static const char* const spaces[] = { "", "  ", "    ", "      ", "        " };
    return spaces[depth > 4 ? 4 : depth];
}

static void items(vpiHandle operand, int depth)
{
    vpiHandle const iterator = vpi_iterate(vpiMatchItem, operand);
    vpiHandle item;
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        PLI_INT32 const type = (PLI_INT32)vpi_get(vpiType, item);
        if (type == vpiAssignment) {
            vpiHandle const rhs = vpi_handle(vpiRhs, item);
            vpi_printf("#c %sitem %s op=%d lhs=%s rhs=%s\n", indent(depth),
                type_name(type), (int)vpi_get(vpiOpType, item),
                text(vpiName, vpi_handle(vpiLhs, item)),
                rhs != NULL ? type_name((PLI_INT32)vpi_get(vpiType, rhs)) : "none");
        } else if (type == vpiOperation) {
            vpiHandle const operands = vpi_iterate(vpiOperand, item);
            vpiHandle const first = operands != NULL ? vpi_scan(operands) : NULL;
            vpi_printf("#c %sitem %s op=%d operand=%s\n", indent(depth),
                type_name(type), (int)vpi_get(vpiOpType, item),
                text(vpiName, first));
            if (first != NULL) {
                vpi_free_object(operands);
            }
        } else {
            vpi_printf("#c %sitem %s name=%s\n", indent(depth), type_name(type),
                text(vpiName, item));
        }
    }
}

// An expression tree: each operation with its operands and the match items of
// every operand.
static void dump(vpiHandle object, int depth)
{
    PLI_INT32 const type = (PLI_INT32)vpi_get(vpiType, object);
    vpiHandle iterator;
    vpiHandle child;
    if (type == vpiOperation) {
        vpi_printf("#c %sop %d\n", indent(depth), (int)vpi_get(vpiOpType, object));
    } else if (type == vpiRefObj && show_actual) {
        vpiHandle const actual = vpi_handle(vpiActual, object);
        vpi_printf("#c %s%s %s actual=%s %s\n", indent(depth), type_name(type),
            text(vpiName, object),
            actual != NULL ? type_name((PLI_INT32)vpi_get(vpiType, actual)) : "none",
            text(vpiName, actual));
    } else {
        vpi_printf("#c %s%s %s\n", indent(depth), type_name(type), text(vpiName, object));
    }
    items(object, depth + 1);
    if (type == vpiOperation) {
        iterator = vpi_iterate(vpiOperand, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1);
        }
    }
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle const sequence = vpi_handle_by_name("top.s_mi", NULL);
    vpiHandle const cover = vpi_handle_by_name("top.c1", NULL);
    vpiHandle plain;
    vpiHandle expression;
    (void)data;
    expression = sequence != NULL ? vpi_handle(vpiExpr, sequence) : NULL;
    vpi_printf("#c sequence %s\n", text(vpiFullName, sequence));
    if (expression != NULL) {
        dump(expression, 1);
    }
    expression = cover != NULL ? vpi_handle(vpiProperty, cover) : NULL;
    if (expression != NULL && vpi_get(vpiType, expression) == vpiPropertySpec) {
        expression = vpi_handle(vpiPropertyExpr, expression);
    }
    vpi_printf("#c cover %s\n", text(vpiFullName, cover));
    if (expression != NULL) {
        dump(expression, 1);
    }
    plain = vpi_handle_by_name("top.s_pn", NULL);
    expression = plain != NULL ? vpi_handle(vpiExpr, plain) : NULL;
    vpi_printf("#c sequence %s\n", text(vpiFullName, plain));
    show_actual = 1;
    if (expression != NULL) {
        dump(expression, 1);
    }
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

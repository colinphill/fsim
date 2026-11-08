// SPDX-License-Identifier: Apache-2.0
// Clocked properties, multiclock sequence expressions and case properties
// (IEEE 1800-2023 37.52, 37.56): walks the expression of each property and
// sequence declaration of top and prints its objects. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* op_name(PLI_INT32 op)
{
    switch (op) {
    case vpiOverlapImplyOp:
        return "vpiOverlapImplyOp";
    case vpiNotOp:
        return "vpiNotOp";
    case vpiPosedgeOp:
        return "vpiPosedgeOp";
    default:
        return "other";
    }
}

static void label(int depth, const char* what, const char* detail)
{
    char spaces[40];
    int length = 0;
    while (length < depth * 2 && length < 38) {
        spaces[length++] = ' ';
    }
    spaces[length] = '\0';
    vpi_printf("#c %s%s%s\n", spaces, what, detail);
}

static void dump(vpiHandle object, int depth);

// The object a relation reaches, under a label of its own.
static void part(vpiHandle object, PLI_INT32 relation, const char* name, int depth)
{
    vpiHandle const target = vpi_handle(relation, object);
    label(depth, name, target != NULL ? "" : " none");
    if (target != NULL) {
        dump(target, depth + 1);
    }
}

static void dump(vpiHandle object, int depth)
{
    PLI_INT32 const type = (PLI_INT32)vpi_get(vpiType, object);
    vpiHandle iterator;
    vpiHandle child;
    char detail[64];
    int count;
    switch (type) {
    case vpiOperation:
        label(depth, "op ", op_name((PLI_INT32)vpi_get(vpiOpType, object)));
        iterator = vpi_iterate(vpiOperand, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1);
        }
        break;
    case vpiClockedProp:
        label(depth, "clocked-prop", "");
        part(object, vpiClockingEvent, "clock", depth + 1);
        part(object, vpiPropertyExpr, "property", depth + 1);
        break;
    case vpiClockedSeq:
        label(depth, "clocked-seq", "");
        part(object, vpiClockingEvent, "clock", depth + 1);
        part(object, vpiExpr, "sequence", depth + 1);
        break;
    case vpiMulticlockSequenceExpr:
        label(depth, "multiclock", "");
        iterator = vpi_iterate(vpiClockedSeq, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1);
        }
        break;
    case vpiCaseProperty:
        label(depth, "case", "");
        part(object, vpiCondition, "condition", depth + 1);
        iterator = vpi_iterate(vpiCasePropertyItem, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1);
        }
        break;
    case vpiCasePropertyItem:
        count = 0;
        iterator = vpi_iterate(vpiExpr, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            ++count;
        }
        snprintf(detail, sizeof detail, " exprs=%d", count);
        label(depth, "item", detail);
        iterator = vpi_iterate(vpiExpr, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            label(depth + 1, "value", "");
            dump(child, depth + 2);
        }
        part(object, vpiPropertyExpr, "property", depth + 1);
        break;
    case vpiConstant: {
        s_vpi_value value;
        memset(&value, 0, sizeof value);
        value.format = vpiDecStrVal;
        vpi_get_value(object, &value);
        label(depth, "const ", value.value.str != NULL ? value.value.str : "none");
        break;
    }
    default: {
        const char* const name = vpi_get_str(vpiName, object);
        label(depth, "name ", name != NULL ? name : "none");
        break;
    }
    }
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle const top = vpi_handle_by_name("top", NULL);
    vpiHandle iterator;
    vpiHandle declaration;
    (void)data;
    iterator = vpi_iterate(vpiPropertyDecl, top);
    while (iterator != NULL && (declaration = vpi_scan(iterator)) != NULL) {
        vpiHandle const spec = vpi_handle(vpiPropertySpec, declaration);
        vpiHandle const expression
            = spec != NULL ? vpi_handle(vpiPropertyExpr, spec) : NULL;
        label(0, "property ", vpi_get_str(vpiName, declaration));
        if (expression != NULL) {
            dump(expression, 1);
        }
    }
    iterator = vpi_iterate(vpiSequenceDecl, top);
    while (iterator != NULL && (declaration = vpi_scan(iterator)) != NULL) {
        vpiHandle const expression = vpi_handle(vpiExpr, declaration);
        label(0, "sequence ", vpi_get_str(vpiName, declaration));
        if (expression != NULL) {
            dump(expression, 1);
        }
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

// SPDX-License-Identifier: Apache-2.0
// Let declarations and let expressions (IEEE 1800-2023 37.12, 37.57): the
// let declarations of a module with their formals and bodies, and the let
// expressions of its continuous assignments with their declarations,
// arguments and values. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static const char* type_name(PLI_INT32 type)
{
    switch (type) {
    case vpiLetDecl:
        return "vpiLetDecl";
    case vpiLetExpr:
        return "vpiLetExpr";
    case vpiSeqFormalDecl:
        return "vpiSeqFormalDecl";
    case vpiRefObj:
        return "vpiRefObj";
    case vpiReg:
        return "vpiReg";
    case vpiOperation:
        return "vpiOperation";
    case vpiConstant:
        return "vpiConstant";
    case vpiLogicTypespec:
        return "vpiLogicTypespec";
    default:
        return "other";
    }
}

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = vpi_get_str(property, object);
    return value != NULL ? value : "none";
}

static const char* indent(int depth)
{
    static const char* const spaces[] = { "", "  ", "    ", "      ", "        " };
    return spaces[depth > 4 ? 4 : depth];
}

static const char* decimal(vpiHandle object)
{
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    value.format = vpiDecStrVal;
    vpi_get_value(object, &value);
    return value.value.str != NULL ? value.value.str : "none";
}

static int integer_value(vpiHandle object)
{
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    vpi_get_value(object, &value);
    return (int)value.value.integer;
}

// An expression: operations with their operands, let expressions with
// their declaration, arguments and value, ref objs with their actual,
// constants by value, other objects by name.
static void dump(vpiHandle object, int depth)
{
    PLI_INT32 const type = (PLI_INT32)vpi_get(vpiType, object);
    vpiHandle iterator;
    vpiHandle child;
    if (type == vpiOperation) {
        PLI_INT32 const op = (PLI_INT32)vpi_get(vpiOpType, object);
        if (op == vpiConcatOp || op == vpiMultiConcatOp
            || op == vpiConditionOp) {
            s_vpi_value value;
            memset(&value, 0, sizeof value);
            value.format = vpiStringVal;
            vpi_get_value(object, &value);
            vpi_printf("#c %sop %d %s string=%s\n", indent(depth), (int)op,
                text(vpiDecompile, object),
                value.value.str != NULL ? value.value.str : "none");
        } else {
            vpi_printf("#c %sop %d %s value=%s\n", indent(depth), (int)op,
                text(vpiDecompile, object), decimal(object));
        }
        iterator = vpi_iterate(vpiOperand, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1);
        }
    } else if (type == vpiLetExpr) {
        vpiHandle const declaration = vpi_handle(vpiLetDecl, object);
        vpi_printf("#c %slet expr %s decl=%s value=%d\n", indent(depth),
            text(vpiDecompile, object),
            declaration != NULL ? text(vpiName, declaration) : "none",
            integer_value(object));
        iterator = vpi_iterate(vpiArgument, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1);
        }
    } else if (type == vpiRefObj) {
        vpiHandle const actual = vpi_handle(vpiActual, object);
        vpi_printf("#c %sref %s actual=%s %s\n", indent(depth),
            text(vpiName, object),
            actual != NULL ? type_name((PLI_INT32)vpi_get(vpiType, actual))
                           : "none",
            actual != NULL ? text(vpiFullName, actual) : "none");
    } else if (type == vpiSysFuncCall) {
        vpi_printf("#c %ssysfunc %s value=%s\n", indent(depth),
            text(vpiDecompile, object), decimal(object));
    } else if (type == vpiConstant) {
        vpi_printf("#c %sconst %d\n", indent(depth), integer_value(object));
    } else {
        vpi_printf("#c %s%s %s\n", indent(depth), type_name(type),
            text(vpiFullName, object));
    }
}

static void declarations(vpiHandle top)
{
    vpiHandle const iterator = vpi_iterate(vpiLetDecl, top);
    vpiHandle declaration;
    while (iterator != NULL && (declaration = vpi_scan(iterator)) != NULL) {
        vpiHandle const formals = vpi_iterate(vpiSeqFormalDecl, declaration);
        vpiHandle const body = vpi_handle(vpiExpr, declaration);
        vpiHandle formal;
        vpi_printf("#c decl %s type=%s line=%d\n", text(vpiName, declaration),
            type_name((PLI_INT32)vpi_get(vpiType, declaration)),
            (int)vpi_get(vpiLineNo, declaration));
        while (formals != NULL && (formal = vpi_scan(formals)) != NULL) {
            vpiHandle const initial = vpi_handle(vpiExpr, formal);
            vpiHandle const typespec = vpi_handle(vpiTypespec, formal);
            vpiHandle const parent = vpi_handle(vpiParent, formal);
            vpi_printf("#c   formal %s type=%s dir=%d parent=%s\n",
                text(vpiFullName, formal),
                type_name((PLI_INT32)vpi_get(vpiType, formal)),
                (int)vpi_get(vpiDirection, formal),
                parent != NULL ? text(vpiName, parent) : "none");
            if (initial != NULL) {
                vpi_printf("#c     default\n");
                dump(initial, 3);
            }
            if (typespec != NULL) {
                vpi_printf("#c     typespec %s name=%s\n",
                    type_name((PLI_INT32)vpi_get(vpiType, typespec)),
                    text(vpiName, typespec));
            }
        }
        vpi_printf("#c   body\n");
        if (body != NULL) {
            dump(body, 2);
        }
    }
}

static PLI_INT32 later(p_cb_data data)
{
    vpiHandle const top = vpi_handle_by_name("top", NULL);
    vpiHandle const assigns = vpi_iterate(vpiContAssign, top);
    vpiHandle assign;
    (void)data;
    declarations(top);
    while (assigns != NULL && (assign = vpi_scan(assigns)) != NULL) {
        vpiHandle const lhs = vpi_handle(vpiLhs, assign);
        vpiHandle const rhs = vpi_handle(vpiRhs, assign);
        vpi_printf("#c assign %s = %s\n",
            lhs != NULL ? text(vpiName, lhs) : "none",
            lhs != NULL ? decimal(lhs) : "none");
        if (rhs != NULL) {
            dump(rhs, 1);
        }
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time delay;
    (void)data;
    memset(&callback, 0, sizeof callback);
    memset(&delay, 0, sizeof delay);
    delay.type = vpiSimTime;
    delay.low = 1U;
    callback.reason = cbAfterDelay;
    callback.cb_rtn = later;
    callback.time = &delay;
    (void)vpi_register_cb(&callback);
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

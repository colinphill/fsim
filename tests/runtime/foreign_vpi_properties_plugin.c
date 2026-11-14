// SPDX-License-Identifier: Apache-2.0
// Property and sequence objects (IEEE 1800-2023 37.50-37.54): the
// declarations of a module, their formals, property specs, the instances an
// assertion names and the operations of the expressions. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static const char* type_name(PLI_INT32 type)
{
    switch (type) {
    case vpiPropertyDecl:
        return "vpiPropertyDecl";
    case vpiSequenceDecl:
        return "vpiSequenceDecl";
    case vpiPropFormalDecl:
        return "vpiPropFormalDecl";
    case vpiSeqFormalDecl:
        return "vpiSeqFormalDecl";
    case vpiPropertySpec:
        return "vpiPropertySpec";
    case vpiIntVar:
        return "vpiIntVar";
    case vpiReg:
        return "vpiReg";
    case vpiRefObj:
        return "vpiRefObj";
    case vpiIntTypespec:
        return "vpiIntTypespec";
    case vpiLogicTypespec:
        return "vpiLogicTypespec";
    case vpiArrayTypespec:
        return "vpiArrayTypespec";
    case vpiPropertyInst:
        return "vpiPropertyInst";
    case vpiSequenceInst:
        return "vpiSequenceInst";
    default:
        return "other";
    }
}

static const char* op_name(PLI_INT32 op)
{
    switch (op) {
    case vpiNonOverlapImplyOp:
        return "vpiNonOverlapImplyOp";
    case vpiOverlapImplyOp:
        return "vpiOverlapImplyOp";
    case vpiNotOp:
        return "vpiNotOp";
    case vpiUntilOp:
        return "vpiUntilOp";
    case vpiUnaryCycleDelayOp:
        return "vpiUnaryCycleDelayOp";
    case vpiCycleDelayOp:
        return "vpiCycleDelayOp";
    case vpiPosedgeOp:
        return "vpiPosedgeOp";
    case vpiCompAndOp:
        return "vpiCompAndOp";
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

// An expression tree: operations with their operands, instances with their
// arguments, constants by value, any other expression by name.
static void dump(vpiHandle object, int depth, int show_strong)
{
    PLI_INT32 const type = (PLI_INT32)vpi_get(vpiType, object);
    vpiHandle iterator;
    vpiHandle child;
    if (type == vpiOperation) {
        if (show_strong) {
            vpi_printf("#c %sop %s strong=%d\n", indent(depth),
                op_name((PLI_INT32)vpi_get(vpiOpType, object)),
                (int)vpi_get(vpiOpStrong, object));
        } else {
            vpi_printf("#c %sop %s\n", indent(depth),
                op_name((PLI_INT32)vpi_get(vpiOpType, object)));
        }
        iterator = vpi_iterate(vpiOperand, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1, show_strong);
        }
    } else if (type == vpiPropertyInst || type == vpiSequenceInst) {
        vpiHandle const declaration = vpi_handle(
            type == vpiPropertyInst ? vpiPropertyDecl : vpiSequenceDecl, object);
        vpi_printf("#c %sinst %s %s decl=%s\n", indent(depth), type_name(type),
            text(vpiName, object),
            declaration != NULL ? text(vpiFullName, declaration) : "none");
        iterator = vpi_iterate(vpiArgument, object);
        while (iterator != NULL && (child = vpi_scan(iterator)) != NULL) {
            dump(child, depth + 1, show_strong);
        }
    } else if (type == vpiConstant) {
        s_vpi_value value;
        memset(&value, 0, sizeof value);
        value.format = vpiDecStrVal;
        vpi_get_value(object, &value);
        vpi_printf("#c %sconst %s\n", indent(depth),
            value.value.str != NULL ? value.value.str : "none");
    } else {
        // A boolean leaf under strong() or weak() carries its strength
        // (37.52 detail 3); any other leaf has none.
        PLI_INT32 const strength = show_strong
            ? (PLI_INT32)vpi_get(vpiOpStrong, object) : vpiUndefined;
        if (strength == 0 || strength == 1) {
            vpi_printf("#c %sname %s type=%s strong=%d\n", indent(depth),
                text(vpiName, object), type_name(type), (int)strength);
        } else {
            vpi_printf("#c %sname %s\n", indent(depth), text(vpiName, object));
        }
    }
}

static void formals(vpiHandle declaration, PLI_INT32 iteration)
{
    vpiHandle const iterator = vpi_iterate(iteration, declaration);
    vpiHandle formal;
    while (iterator != NULL && (formal = vpi_scan(iterator)) != NULL) {
        vpiHandle const initial = vpi_handle(vpiExpr, formal);
        vpi_printf("#c formal %s type=%s dir=%d default=%s\n",
            text(vpiFullName, formal),
            type_name((PLI_INT32)vpi_get(vpiType, formal)),
            (int)vpi_get(vpiDirection, formal),
            initial != NULL ? text(vpiName, initial) : "none");
    }
}

// The local variables of a declaration (37.51, 37.53 vpiVariables).
static void variables(vpiHandle declaration)
{
    vpiHandle const iterator = vpi_iterate(vpiVariables, declaration);
    vpiHandle variable;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        vpiHandle const parent = vpi_handle(vpiParent, variable);
        vpi_printf("#c variable %s type=%s name=%s parent=%s\n",
            text(vpiFullName, variable),
            type_name((PLI_INT32)vpi_get(vpiType, variable)),
            text(vpiName, variable),
            parent != NULL ? text(vpiFullName, parent) : "none");
    }
}

static void property(vpiHandle declaration)
{
    vpiHandle const spec = vpi_handle(vpiPropertySpec, declaration);
    vpiHandle const disable = vpi_handle(vpiDisableCondition, declaration);
    vpiHandle clock;
    vpiHandle expression;
    vpi_printf("#c property %s full=%s type=%s\n", text(vpiName, declaration),
        text(vpiFullName, declaration),
        type_name((PLI_INT32)vpi_get(vpiType, declaration)));
    formals(declaration, vpiPropFormalDecl);
    variables(declaration);
    vpi_printf("#c disable %s\n", disable != NULL ? text(vpiName, disable) : "none");
    if (spec == NULL) {
        vpi_printf("#c no spec\n");
        return;
    }
    vpi_printf("#c spec type=%s\n", type_name((PLI_INT32)vpi_get(vpiType, spec)));
    clock = vpi_handle(vpiClockingEvent, spec);
    vpi_printf("#c clock\n");
    if (clock != NULL) {
        dump(clock, 1, 0);
    }
    expression = vpi_handle(vpiPropertyExpr, spec);
    vpi_printf("#c expr\n");
    if (expression != NULL) {
        dump(expression, 1, 1);
    }
}

static void sequence(vpiHandle declaration)
{
    vpiHandle const expression = vpi_handle(vpiExpr, declaration);
    vpi_printf("#c sequence %s full=%s type=%s\n", text(vpiName, declaration),
        text(vpiFullName, declaration),
        type_name((PLI_INT32)vpi_get(vpiType, declaration)));
    formals(declaration, vpiSeqFormalDecl);
    variables(declaration);
    vpi_printf("#c expr\n");
    if (expression != NULL) {
        dump(expression, 1, 1);
    }
}

static void assertion_with(const char* path, int details)
{
    vpiHandle const object = vpi_handle_by_name((PLI_BYTE8*)path, NULL);
    vpiHandle const named = object != NULL ? vpi_handle(vpiProperty, object) : NULL;
    if (named == NULL) {
        vpi_printf("#c %s no property\n", path);
        return;
    }
    vpi_printf("#c %s property type=%s\n", path,
        type_name((PLI_INT32)vpi_get(vpiType, named)));
    if (vpi_get(vpiType, named) == vpiPropertySpec) {
        vpiHandle const expression = vpi_handle(vpiPropertyExpr, named);
        if (details) {
            vpiHandle const clock = vpi_handle(vpiClockingEvent, named);
            vpiHandle const disable = vpi_handle(vpiDisableCondition, named);
            vpi_printf("#c   cover sequence=%d disable=%s\n",
                (int)vpi_get(vpiIsCoverSequence, object),
                disable != NULL ? text(vpiName, disable) : "none");
            if (clock != NULL) {
                dump(clock, 2, 0);
            }
        }
        if (expression != NULL) {
            dump(expression, 1, 1);
        }
    } else {
        dump(named, 1, 1);
    }
}

static void assertion(const char* path)
{
    assertion_with(path, 0);
}

// A typespec: its type, name (a typedef's), ranges and element typespec.
static void typespec(vpiHandle object, int depth)
{
    vpiHandle iterator;
    vpiHandle range;
    vpiHandle element;
    vpi_printf("#c %stypespec %s name=%s\n", indent(depth),
        type_name((PLI_INT32)vpi_get(vpiType, object)), text(vpiName, object));
    iterator = vpi_iterate(vpiRange, object);
    while (iterator != NULL && (range = vpi_scan(iterator)) != NULL) {
        s_vpi_value left;
        s_vpi_value right;
        vpiHandle const left_expr = vpi_handle(vpiLeftRange, range);
        vpiHandle const right_expr = vpi_handle(vpiRightRange, range);
        memset(&left, 0, sizeof left);
        memset(&right, 0, sizeof right);
        left.format = vpiIntVal;
        right.format = vpiIntVal;
        if (left_expr != NULL && right_expr != NULL) {
            vpi_get_value(left_expr, &left);
            vpi_get_value(right_expr, &right);
            vpi_printf("#c %s  range [%d:%d]\n", indent(depth),
                (int)left.value.integer, (int)right.value.integer);
        }
    }
    element = vpi_handle(vpiElemTypespec, object);
    if (element != NULL) {
        typespec(element, depth + 1);
    }
}

// The typespecs of a declaration's formals and local variables.
static void typespecs(const char* path)
{
    vpiHandle const declaration = vpi_handle_by_name((PLI_BYTE8*)path, NULL);
    PLI_INT32 const iterations[] = { vpiSeqFormalDecl, vpiVariables };
    int round;
    if (declaration == NULL) {
        vpi_printf("#c %s none\n", path);
        return;
    }
    for (round = 0; round < 2; ++round) {
        vpiHandle const iterator = vpi_iterate(iterations[round], declaration);
        vpiHandle object;
        while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
            vpiHandle const type = vpi_handle(vpiTypespec, object);
            vpi_printf("#c typed %s\n", text(vpiFullName, object));
            if (type != NULL) {
                typespec(type, 1);
            } else {
                vpi_printf("#c   no typespec\n");
            }
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
        property(declaration);
    }
    iterator = vpi_iterate(vpiSequenceDecl, top);
    while (iterator != NULL && (declaration = vpi_scan(iterator)) != NULL) {
        sequence(declaration);
    }
    declaration = vpi_handle_by_name("top.p_imp", NULL);
    vpi_printf("#c by name %s\n",
        declaration != NULL ? text(vpiFullName, declaration) : "none");
    declaration = vpi_handle_by_name("top.u_chk$cp", NULL);
    vpi_printf("#c checker declaration by name %s\n",
        declaration != NULL ? text(vpiFullName, declaration) : "none");
    // A checker's assertion belongs to the instantiating module under its
    // checker instance's path, and names the declaration it instantiates
    // by its source name (17.2, 37.49, 37.53).
    {
        vpiHandle const checked = vpi_handle_by_name("top.u_chk.ca", NULL);
        vpiHandle assertions = vpi_iterate(vpiAssertion, top);
        vpiHandle item;
        vpi_printf("#c checker assertion by name %s\n",
            checked != NULL ? text(vpiFullName, checked) : "none");
        while (assertions != NULL && (item = vpi_scan(assertions)) != NULL) {
            vpiHandle const used = vpi_handle(vpiProperty, item);
            vpiHandle const named = used != NULL
                    && vpi_get(vpiType, used) == vpiPropertyInst
                ? vpi_handle(vpiPropertyDecl, used)
                : NULL;
            vpi_printf("#c assertion %s name %s", text(vpiFullName, item),
                text(vpiName, item));
            if (named != NULL) {
                vpi_printf(" uses %s (%s)", text(vpiName, named),
                    text(vpiFullName, named));
            }
            vpi_printf("\n");
        }
    }
    assertion("top.a1");
    assertion("top.a2");
    assertion("top.a3");
    assertion("top.a4");
    assertion_with("top.a5", 1);
    typespecs("top.s_ty");
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

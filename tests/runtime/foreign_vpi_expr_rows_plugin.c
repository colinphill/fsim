// SPDX-License-Identifier: Apache-2.0
// Expression and statement rows of the SystemVerilog VPI model: at time 1,
// prints the right-hand side of each assignment of top.main (type, vpiOpType,
// name, typespec, operands, value), the alias statements of top, the formals
// of top.sum and the terminals of the primitive top.g. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static void value_of(const char* label, vpiHandle object)
{
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    value.format = vpiBinStrVal;
    vpi_get_value(object, &value);
    vpi_printf("#c   %s value=%s\n", label,
        value.format == vpiBinStrVal && value.value.str != NULL
            ? value.value.str
            : "none");
}

static void describe(vpiHandle object, int depth)
{
    vpiHandle spec;
    vpiHandle operands;
    vpiHandle operand;
    const int type = (int)vpi_get(vpiType, object);
    vpi_printf("#c %*stype=%d op=%d name=%s full=%s", depth * 2, "", type,
        type == vpiOperation ? (int)vpi_get(vpiOpType, object) : -1,
        text(vpi_get_str(vpiName, object)),
        text(vpi_get_str(vpiFullName, object)));
    if (type != vpiOperation) {
        vpi_printf(" size=%d", (int)vpi_get(vpiSize, object));
    }
    vpi_printf("\n");
    if (type == vpiOperation && vpi_get(vpiOpType, object) == vpiCastOp) {
        spec = vpi_handle(vpiTypespec, object);
        vpi_printf("#c %*scast typespec=%s kind=%d\n", depth * 2, "",
            spec != NULL ? text(vpi_get_str(vpiName, spec)) : "(none)",
            spec != NULL ? (int)vpi_get(vpiType, spec) : 0);
    }
    value_of("", object);
    if (type == vpiOperation) {
        operands = vpi_iterate(vpiOperand, object);
        while (operands != NULL && (operand = vpi_scan(operands)) != NULL) {
            describe(operand, depth + 1);
        }
    }
}

static void statements(void)
{
    vpiHandle block = vpi_handle_by_name("top.main", NULL);
    vpiHandle list = block != NULL ? vpi_iterate(vpiStmt, block) : NULL;
    vpiHandle statement;
    while (list != NULL && (statement = vpi_scan(list)) != NULL) {
        vpi_printf("#c line %d\n", (int)vpi_get(vpiLineNo, statement));
        describe(vpi_handle(vpiRhs, statement), 1);
    }
}

static void aliases(void)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle list = vpi_iterate(vpiAliasStmt, top);
    vpiHandle alias;
    while (list != NULL && (alias = vpi_scan(list)) != NULL) {
        vpiHandle lhs = vpi_handle(vpiLhs, alias);
        vpiHandle rhs = vpi_handle(vpiRhs, alias);
        vpi_printf("#c alias type=%d line=%d lhs=%s rhs=%s parent=%s\n",
            (int)vpi_get(vpiType, alias), (int)vpi_get(vpiLineNo, alias),
            lhs != NULL ? text(vpi_get_str(vpiFullName, lhs)) : "(none)",
            rhs != NULL ? text(vpi_get_str(vpiFullName, rhs)) : "(none)",
            text(vpi_get_str(vpiFullName, vpi_handle(vpiParent, alias))));
    }
}

static void formals(void)
{
    vpiHandle function = vpi_handle_by_name("top.sum", NULL);
    vpiHandle list = function != NULL ? vpi_iterate(vpiIODecl, function) : NULL;
    vpiHandle formal;
    while (list != NULL && (formal = vpi_scan(list)) != NULL) {
        vpi_printf("#c formal %s size=%d signed=%d\n",
            text(vpi_get_str(vpiName, formal)), (int)vpi_get(vpiSize, formal),
            (int)vpi_get(vpiSigned, formal));
    }
}

static void terminals(void)
{
    vpiHandle gate = vpi_handle_by_name("top.g", NULL);
    vpiHandle list = gate != NULL ? vpi_iterate(vpiPrimTerm, gate) : NULL;
    vpiHandle term;
    while (list != NULL && (term = vpi_scan(list)) != NULL) {
        vpi_printf("#c term %d dir=%d\n", (int)vpi_get(vpiTermIndex, term),
            (int)vpi_get(vpiDirection, term));
        value_of("term", term);
    }
}

static PLI_INT32 report(p_cb_data data)
{
    (void)data;
    statements();
    aliases();
    formals();
    terminals();
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static s_vpi_time delay = { vpiSimTime, 0, 1, 0.0 };
    s_cb_data later;
    (void)data;
    memset(&later, 0, sizeof later);
    later.reason = cbAfterDelay;
    later.cb_rtn = report;
    later.time = &delay;
    (void)vpi_register_cb(&later);
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

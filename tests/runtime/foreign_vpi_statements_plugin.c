// SPDX-License-Identifier: Apache-2.0
// Statement model conformance plug-in (IEEE 1800-2023 37.43-37.79): at the
// start of simulation the plug-in prints the processes, continuous
// assignments and the statement of a task as a tree. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = vpi_get_str(property, object);
    return value != NULL ? value : "";
}

// Statements and expressions are walked; objects they name are leaves.
static int walked(PLI_INT32 type)
{
    switch (type) {
    case vpiAlways:
    case vpiInitial:
    case vpiFinal:
    case vpiContAssign:
    case vpiAssignment:
    case vpiForce:
    case vpiRelease:
    case vpiAssignStmt:
    case vpiDeassign:
    case vpiBegin:
    case vpiNamedBegin:
    case vpiFork:
    case vpiNamedFork:
    case vpiIf:
    case vpiIfElse:
    case vpiCase:
    case vpiCaseItem:
    case vpiFor:
    case vpiRepeat:
    case vpiWhile:
    case vpiForever:
    case vpiDelayControl:
    case vpiEventControl:
    case vpiTaskCall:
    case vpiSysTaskCall:
    case vpiOperation:
    case vpiBitSelect:
    case vpiPartSelect:
        return 1;
    default:
        return 0;
    }
}

static void dump(vpiHandle object, int depth, const char* role);

static void child(vpiHandle parent, PLI_INT32 relation, int depth,
    const char* role)
{
    vpiHandle const found = vpi_handle(relation, parent);
    if (found != NULL) {
        dump(found, depth, role);
    }
}

static void children(vpiHandle parent, PLI_INT32 relation, int depth,
    const char* role)
{
    vpiHandle const items = vpi_iterate(relation, parent);
    vpiHandle item;
    if (items == NULL) {
        return;
    }
    while ((item = vpi_scan(items)) != NULL) {
        dump(item, depth, role);
    }
}

static void dump(vpiHandle object, int depth, const char* role)
{
    const PLI_INT32 type = vpi_get(vpiType, object);
    char detail[160];
    detail[0] = '\0';
    if (type == vpiConstant) {
        s_vpi_value value;
        memset(&value, 0, sizeof value);
        value.format = vpiBinStrVal;
        vpi_get_value(object, &value);
        snprintf(detail, sizeof detail, " %s const=%d size=%d bin=%s",
            text(vpiDecompile, object), (int)vpi_get(vpiConstType, object),
            (int)vpi_get(vpiSize, object),
            value.value.str != NULL ? value.value.str : "?");
    } else if (type == vpiOperation) {
        snprintf(detail, sizeof detail, " op=%d", (int)vpi_get(vpiOpType, object));
    } else if (type == vpiAssignment) {
        snprintf(detail, sizeof detail, " blocking=%d",
            (int)vpi_get(vpiBlocking, object));
    } else if (type == vpiAlways) {
        snprintf(detail, sizeof detail, " always_type=%d",
            (int)vpi_get(vpiAlwaysType, object));
    } else if (type == vpiSysTaskCall || type == vpiTaskCall
        || type == vpiNamedBegin) {
        snprintf(detail, sizeof detail, " %s", text(vpiName, object));
    } else if (!walked(type)) {
        snprintf(detail, sizeof detail, " %s", text(vpiFullName, object));
    }
    vpi_printf("#c %*s%s%s line=%d%s\n", depth * 2, "", role,
        text(vpiType, object), (int)vpi_get(vpiLineNo, object), detail);
    if (!walked(type)) {
        return;
    }
    ++depth;
    child(object, vpiForInitStmt, depth, "init: ");
    child(object, vpiCondition, depth, "cond: ");
    child(object, vpiForIncStmt, depth, "inc: ");
    child(object, vpiDelay, depth, "delay: ");
    child(object, vpiDelayControl, depth, "control: ");
    child(object, vpiLhs, depth, "lhs: ");
    child(object, vpiRhs, depth, "rhs: ");
    child(object, vpiIndex, depth, "index: ");
    child(object, vpiLeftRange, depth, "left: ");
    child(object, vpiRightRange, depth, "right: ");
    children(object, vpiOperand, depth, "");
    children(object, vpiArgument, depth, "arg: ");
    children(object, vpiExpr, depth, "choice: ");
    children(object, vpiCaseItem, depth, "");
    if (type == vpiBegin || type == vpiNamedBegin || type == vpiFork
        || type == vpiNamedFork) {
        children(object, vpiStmt, depth, "");
    } else {
        child(object, vpiStmt, depth, "");
    }
    child(object, vpiElseStmt, depth, "else: ");
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle const top = vpi_handle_by_name("top", NULL);
    vpiHandle task;
    (void)data;
    if (top == NULL) {
        vpi_printf("#c no top\n");
        return 0;
    }
    children(top, vpiContAssign, 0, "");
    children(top, vpiProcess, 0, "");
    task = vpi_handle_by_name("top.pulse", NULL);
    if (task != NULL) {
        vpi_printf("#c task %s\n", text(vpiFullName, task));
        child(task, vpiStmt, 1, "");
    }
    return 0;
}

// obj is the statement, value the target's value after it (38.36.1).
static PLI_INT32 forced(p_cb_data data)
{
    const char* const reason = data->reason == cbForce ? "force"
        : data->reason == cbRelease                    ? "release"
        : data->reason == cbAssign                     ? "assign"
                                                       : "deassign";
    vpiHandle const target = vpi_handle(vpiLhs, data->obj);
    vpi_printf("#c %u %s %s line=%d %s=%s\n",
        data->time != NULL ? (unsigned)data->time->low : 0U, reason,
        text(vpiType, data->obj), (int)vpi_get(vpiLineNo, data->obj),
        target != NULL ? text(vpiFullName, target) : "?",
        data->value != NULL && data->value->value.str != NULL
            ? data->value->value.str
            : "?");
    return 0;
}

static void startup(void)
{
    static s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    static s_vpi_value value = { vpiBinStrVal, { 0 } };
    static const PLI_INT32 reasons[] = { cbForce, cbRelease, cbAssign, cbDeassign };
    s_cb_data data;
    size_t index;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
    for (index = 0; index < sizeof reasons / sizeof reasons[0]; ++index) {
        memset(&data, 0, sizeof data);
        data.reason = reasons[index];
        data.cb_rtn = forced;
        data.time = &time;
        data.value = &value;
        (void)vpi_register_cb(&data);
    }
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

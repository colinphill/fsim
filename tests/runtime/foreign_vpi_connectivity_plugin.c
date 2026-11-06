// SPDX-License-Identifier: Apache-2.0
// Connectivity conformance plug-in (IEEE 1800-2023 37.21, 37.41, 37.42,
// 37.46, 37.58, 37.74): at the start of simulation the plug-in prints the
// drivers, loads and uses of nets and variables, the var selects of the
// initial process, the task and function declarations and calls, and the
// DPI imports and exports (37.40) with the calls of an import.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(PLI_INT32 property, vpiHandle object)
{
    const char* const value = vpi_get_str(property, object);
    return value != NULL ? value : "";
}

// The first object `relation` reaches from `from`.
static vpiHandle first(PLI_INT32 relation, vpiHandle from)
{
    vpiHandle const items = vpi_iterate(relation, from);
    vpiHandle const item = items != NULL ? vpi_scan(items) : NULL;
    if (items != NULL) {
        (void)vpi_free_object(items);
    }
    return item;
}

// The objects `relation` reaches from the object `name`.
static void connections(char* name, PLI_INT32 relation, const char* label)
{
    vpiHandle const object = vpi_handle_by_name(name, NULL);
    vpiHandle items;
    vpiHandle item;
    if (object == NULL) {
        vpi_printf("#c %s %s: missing\n", label, name);
        return;
    }
    items = vpi_iterate(relation, object);
    if (items == NULL) {
        vpi_printf("#c %s %s: none\n", label, name);
        return;
    }
    while ((item = vpi_scan(items)) != NULL) {
        if (vpi_get(vpiType, item) == vpiPort) {
            vpi_printf("#c %s %s: %s %s\n", label, name, text(vpiType, item),
                text(vpiName, item));
        } else {
            vpi_printf("#c %s %s: %s line=%d\n", label, name,
                text(vpiType, item), (int)vpi_get(vpiLineNo, item));
        }
    }
}

static void object_connections(char* name)
{
    connections(name, vpiDriver, "driver");
    connections(name, vpiLoad, "load");
    connections(name, vpiUse, "use");
}

// An index expression: a constant's text or an object's name.
static const char* index_text(vpiHandle index)
{
    return vpi_get(vpiType, index) == vpiConstant
        ? text(vpiDecompile, index)
        : text(vpiFullName, index);
}

static void var_select(const char* role, vpiHandle select)
{
    vpiHandle parent;
    vpiHandle items;
    vpiHandle item;
    if (select == NULL) {
        vpi_printf("#c %s: missing\n", role);
        return;
    }
    parent = vpi_handle(vpiParent, select);
    vpi_printf("#c %s: %s parent=%s const=%d\n", role, text(vpiType, select),
        parent != NULL ? text(vpiFullName, parent) : "?",
        (int)vpi_get(vpiConstantSelect, select));
    items = vpi_iterate(vpiIndex, select);
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpi_printf("#c %s:   index %s\n", role, index_text(item));
    }
}

static void signature(char* name)
{
    vpiHandle const callable = vpi_handle_by_name(name, NULL);
    vpiHandle items;
    vpiHandle item;
    vpiHandle result;
    if (callable == NULL) {
        vpi_printf("#c %s: missing\n", name);
        return;
    }
    vpi_printf("#c %s: %s method=%d virtual=%d visibility=%d access=%d\n",
        name, text(vpiType, callable), (int)vpi_get(vpiMethod, callable),
        (int)vpi_get(vpiVirtual, callable),
        (int)vpi_get(vpiVisibility, callable),
        (int)vpi_get(vpiAccessType, callable));
    if (vpi_get(vpiType, callable) == vpiFunction) {
        vpi_printf("#c %s: functype=%d size=%d signed=%d\n", name,
            (int)vpi_get(vpiFuncType, callable), (int)vpi_get(vpiSize, callable),
            (int)vpi_get(vpiSigned, callable));
    }
    items = vpi_iterate(vpiIODecl, callable);
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpi_printf("#c %s:   %s %s dir=%d size=%d signed=%d\n", name,
            text(vpiType, item), text(vpiName, item),
            (int)vpi_get(vpiDirection, item), (int)vpi_get(vpiSize, item),
            (int)vpi_get(vpiSigned, item));
    }
    result = vpi_handle(vpiReturn, callable);
    if (result == NULL) {
        vpi_printf("#c %s:   no return\n", name);
    } else {
        vpi_printf("#c %s:   return %s %s size=%d\n", name,
            text(vpiType, result), text(vpiName, result),
            (int)vpi_get(vpiSize, result));
    }
}

// The C functions the design's DPI imports bind to (IEEE 1800-2023 35.5).
FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL c_conn_twice(const int x)
{
    return 2 * x;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL c_conn_note(const int x)
{
    (void)x;
    return 0;
}

// The DPI properties of a task or function (IEEE 1800-2023 37.40 details
// 6-10).
static void dpi_signature(char* name)
{
    vpiHandle const callable = vpi_handle_by_name(name, NULL);
    if (callable == NULL) {
        vpi_printf("#c %s: missing\n", name);
        return;
    }
    vpi_printf("#c %s: %s access=%d pure=%d context=%d cstr=%d cid=%s\n",
        name, text(vpiType, callable), (int)vpi_get(vpiAccessType, callable),
        (int)vpi_get(vpiDPIPure, callable),
        (int)vpi_get(vpiDPIContext, callable),
        (int)vpi_get(vpiDPICStr, callable),
        text(vpiDPICIdentifier, callable));
    vpi_printf("#c %s: method=%d virtual=%d visibility=%d\n", name,
        (int)vpi_get(vpiMethod, callable), (int)vpi_get(vpiVirtual, callable),
        (int)vpi_get(vpiVisibility, callable));
    if (vpi_get(vpiType, callable) == vpiFunction) {
        vpi_printf("#c %s: functype=%d size=%d signed=%d\n", name,
            (int)vpi_get(vpiFuncType, callable), (int)vpi_get(vpiSize, callable),
            (int)vpi_get(vpiSigned, callable));
    }
}

// The calls to a DPI import: the statement's task, or the function of the
// call an assignment reads (IEEE 1800-2023 37.40, 37.42, 37.43).
static void dpi_calls(vpiHandle top)
{
    vpiHandle const processes = vpi_iterate(vpiProcess, top);
    vpiHandle process;
    while (processes != NULL && (process = vpi_scan(processes)) != NULL) {
        vpiHandle const body = vpi_handle(vpiStmt, process);
        vpiHandle const items = body != NULL ? vpi_iterate(vpiStmt, body) : NULL;
        vpiHandle item;
        while (items != NULL && (item = vpi_scan(items)) != NULL) {
            vpiHandle call = item;
            vpiHandle callee;
            if (vpi_get(vpiType, item) == vpiAssignment) {
                call = vpi_handle(vpiRhs, item);
            }
            if (call == NULL) {
                continue;
            }
            callee = vpi_get(vpiType, call) == vpiTaskCall
                ? vpi_handle(vpiTask, call)
                : vpi_get(vpiType, call) == vpiFuncCall
                ? vpi_handle(vpiFunction, call)
                : NULL;
            if (callee != NULL
                && vpi_get(vpiAccessType, callee) == vpiDPIImportAcc) {
                vpi_printf("#c dpi call line %d: %s %s access=%d",
                    (int)vpi_get(vpiLineNo, item), text(vpiType, call),
                    text(vpiFullName, callee),
                    (int)vpi_get(vpiAccessType, callee));
                if (vpi_get(vpiType, call) == vpiFuncCall) {
                    vpi_printf(" functype=%d", (int)vpi_get(vpiFuncType, call));
                }
                vpi_printf("\n");
            }
        }
    }
}

// The statements of the module's initial process, by line.
static void statements(vpiHandle top)
{
    vpiHandle const process = first(vpiProcess, top);
    vpiHandle const body = process != NULL ? vpi_handle(vpiStmt, process) : NULL;
    vpiHandle const items = body != NULL ? vpi_iterate(vpiStmt, body) : NULL;
    vpiHandle item;
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        const int line = (int)vpi_get(vpiLineNo, item);
        if (line == 35) {
            var_select("line 35 lhs", vpi_handle(vpiLhs, item));
        } else if (line == 37) {
            vpiHandle const sum = vpi_handle(vpiRhs, item);
            var_select("line 37 lhs", vpi_handle(vpiLhs, item));
            var_select("line 37 rhs",
                sum != NULL ? first(vpiOperand, sum) : NULL);
        } else if (line == 38) {
            var_select("line 38 rhs", vpi_handle(vpiRhs, item));
        } else if (line == 39) {
            vpiHandle const condition = vpi_handle(vpiCondition, item);
            vpiHandle const variable
                = condition != NULL ? first(vpiOperand, condition) : NULL;
            vpiHandle const parent
                = variable != NULL ? vpi_handle(vpiParent, variable) : NULL;
            vpi_printf("#c line 39: %s localvardecls=%d\n",
                text(vpiType, item), (int)vpi_get(vpiLocalVarDecls, item));
            vpi_printf("#c line 39: loop variable %s parent=%s\n",
                variable != NULL ? text(vpiType, variable) : "?",
                parent != NULL ? text(vpiFullName, parent) : "?");
        } else if (line == 41) {
            vpiHandle const task = vpi_handle(vpiTask, item);
            vpiHandle const arguments = vpi_iterate(vpiArgument, item);
            int count = 0;
            while (arguments != NULL && vpi_scan(arguments) != NULL) {
                ++count;
            }
            vpi_printf("#c line 41: %s task=%s arguments=%d\n",
                text(vpiType, item), task != NULL ? text(vpiFullName, task) : "?",
                count);
        } else if (line == 42) {
            vpiHandle const arguments = vpi_iterate(vpiArgument, item);
            vpiHandle argument;
            vpi_printf("#c line 42: %s userdefn=%d\n", text(vpiType, item),
                (int)vpi_get(vpiUserDefn, item));
            while (arguments != NULL && (argument = vpi_scan(arguments)) != NULL) {
                if (vpi_get(vpiType, argument) == vpiFuncCall) {
                    vpiHandle const function = vpi_handle(vpiFunction, argument);
                    vpi_printf("#c line 42: %s function=%s functype=%d\n",
                        text(vpiType, argument),
                        function != NULL ? text(vpiFullName, function) : "?",
                        (int)vpi_get(vpiFuncType, argument));
                }
            }
        }
    }
}

// A continuous assignment's net declaration flag and scalar bits (37.47).
static void assignment_bits(char* name)
{
    vpiHandle const net = vpi_handle_by_name(name, NULL);
    vpiHandle const drivers = net != NULL ? vpi_iterate(vpiDriver, net) : NULL;
    vpiHandle const driver = drivers != NULL ? vpi_scan(drivers) : NULL;
    vpiHandle bits;
    vpiHandle bit;
    if (driver == NULL) {
        vpi_printf("#c %s: no driver\n", name);
        return;
    }
    vpi_printf("#c %s: %s netdeclassign=%d\n", name, text(vpiType, driver),
        (int)vpi_get(vpiNetDeclAssign, driver));
    bits = vpi_iterate(vpiBit, driver);
    while (bits != NULL && (bit = vpi_scan(bits)) != NULL) {
        vpiHandle const parent = vpi_handle(vpiParent, bit);
        vpi_printf("#c %s:   %s offset=%d size=%d parent=%s\n", name,
            text(vpiType, bit), (int)vpi_get(vpiOffset, bit),
            (int)vpi_get(vpiSize, bit),
            parent != NULL ? text(vpiType, parent) : "?");
    }
}

// The system function calls of the module's last process (37.42).
static void system_calls(vpiHandle top)
{
    vpiHandle const processes = vpi_iterate(vpiProcess, top);
    vpiHandle process;
    vpiHandle last = NULL;
    vpiHandle body;
    vpiHandle items;
    vpiHandle item;
    while (processes != NULL && (process = vpi_scan(processes)) != NULL) {
        last = process;
    }
    body = last != NULL ? vpi_handle(vpiStmt, last) : NULL;
    items = body != NULL ? vpi_iterate(vpiStmt, body) : NULL;
    while (items != NULL && (item = vpi_scan(items)) != NULL) {
        vpiHandle const call = vpi_handle(vpiRhs, item);
        if (call == NULL || vpi_get(vpiType, call) != vpiSysFuncCall) {
            continue;
        }
        vpi_printf("#c sysfunc %s functype=%d", text(vpiName, call),
            (int)vpi_get(vpiFuncType, call));
        if (strcmp(text(vpiName, call), "$time") == 0) {
            s_vpi_value value;
            memset(&value, 0, sizeof value);
            value.format = vpiIntVal;
            vpi_get_value(call, &value);
            vpi_printf(" value=%d", (int)value.value.integer);
        }
        vpi_printf("\n");
    }
}

static PLI_INT32 start(p_cb_data data)
{
    vpiHandle const top = vpi_handle_by_name("top", NULL);
    (void)data;
    if (top == NULL) {
        vpi_printf("#c no top\n");
        return 0;
    }
    object_connections("top.d");
    object_connections("top.q");
    object_connections("top.mem");
    object_connections("top.idx");
    object_connections("top.w");
    object_connections("top.s_in");
    object_connections("top.s_out");
    object_connections("top.u.a");
    object_connections("top.u.y");
    object_connections("top.gv");
    statements(top);
    signature("top.add");
    signature("top.wide");
    signature("top.bump");
    dpi_signature("top.dpi_twice");
    dpi_signature("top.dpi_note");
    dpi_signature("top.dpi_exp");
    dpi_calls(top);
    assignment_bits("top.w");
    assignment_bits("top.nd");
    system_calls(top);
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

// SPDX-License-Identifier: Apache-2.0
// Virtual interface variables (IEEE 1800-2023 37.29, 37.30). At the end of
// the run, the module lists its vpiVirtualInterfaceVar and vpiVariables
// objects with their types, actuals and typespecs, its typedefs with the
// parameter values their actuals assign, and the class a property array of
// virtual interfaces belongs to. Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "none";
}

static const char* full_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiFullName, object)) : "null";
}

static void variable(const char* scope, const char* relation, vpiHandle object)
{
    vpiHandle actual = vpi_handle(vpiActual, object);
    vpiHandle typespec = vpi_handle(vpiTypespec, object);
    vpiHandle expr = vpi_handle(vpiExpr, object);
    vpi_printf("#c %s %s %s type=%s actual=%s defname=%s expr=%s\n", scope,
        relation, text(vpi_get_str(vpiName, object)),
        text(vpi_get_str(vpiType, object)), full_of(actual),
        typespec != NULL ? text(vpi_get_str(vpiDefName, typespec)) : "none",
        full_of(expr));
}

// An interface typespec: its name, the interface it denotes, the values its
// actuals assign (37.30), and the typedef iteration's handle for it.
static void interface_typespec(const char* label, vpiHandle typespec)
{
    vpiHandle iterator = vpi_iterate(vpiParamAssign, typespec);
    vpiHandle assign;
    char values[128] = "";
    int count = 0;
    while (iterator != NULL && (assign = vpi_scan(iterator)) != NULL) {
        vpiHandle left = vpi_handle(vpiLhs, assign);
        vpiHandle right = vpi_handle(vpiRhs, assign);
        s_vpi_value value;
        char one[64];
        memset(&value, 0, sizeof value);
        value.format = vpiIntVal;
        if (right != NULL) {
            vpi_get_value(right, &value);
        }
        snprintf(one, sizeof one, "%s%s=%d", count > 0 ? "," : "",
            left != NULL ? text(vpi_get_str(vpiName, left)) : "none",
            (int)value.value.integer);
        strncat(values, one, sizeof values - strlen(values) - 1U);
        ++count;
    }
    vpi_printf("#c %s typespec type=%s name=%s defname=%s assigns=%d%s%s\n",
        label, text(vpi_get_str(vpiType, typespec)),
        text(vpi_get_str(vpiName, typespec)),
        text(vpi_get_str(vpiDefName, typespec)), count,
        count > 0 ? " " : "", values);
}

static void typespec_of(const char* label, vpiHandle object)
{
    vpiHandle typespec = object != NULL ? vpi_handle(vpiTypespec, object)
                                        : NULL;
    if (typespec == NULL) {
        vpi_printf("#c %s no typespec\n", label);
        return;
    }
    interface_typespec(label, typespec);
}

static void scope(const char* label, vpiHandle handle)
{
    PLI_INT32 relations[2] = { vpiVirtualInterfaceVar, vpiVariables };
    const char* names[2] = { "vi", "var" };
    int index;
    if (handle == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    for (index = 0; index < 2; ++index) {
        vpiHandle list = vpi_iterate(relations[index], handle);
        vpiHandle object;
        while (list != NULL && (object = vpi_scan(list)) != NULL) {
            variable(label, names[index], object);
        }
    }
}

static void typedefs(const char* label, vpiHandle handle)
{
    vpiHandle list = vpi_iterate(vpiTypedef, handle);
    vpiHandle object;
    while (list != NULL && (object = vpi_scan(list)) != NULL) {
        char name[64];
        snprintf(name, sizeof name, "%s typedef", label);
        interface_typespec(name, object);
    }
}

// The variables of a class defn and of a live object of it: the virtual
// interface properties, an array of them listing as its elements (37.29
// example 1, 37.33 detail 7).
static void class_variables(const char* label, vpiHandle owner)
{
    vpiHandle list = vpi_iterate(vpiVariables, owner);
    vpiHandle object;
    while (list != NULL && (object = vpi_scan(list)) != NULL) {
        vpi_printf("#c %s var %s type=%s\n", label,
            text(vpi_get_str(vpiName, object)),
            text(vpi_get_str(vpiType, object)));
    }
    list = vpi_iterate(vpiVirtualInterfaceVar, owner);
    while (list != NULL && (object = vpi_scan(list)) != NULL) {
        vpiHandle typespec = vpi_handle(vpiTypespec, object);
        vpi_printf("#c %s vi %s type=%s modport=%d actual=%s defname=%s\n",
            label, text(vpi_get_str(vpiName, object)),
            text(vpi_get_str(vpiType, object)),
            (int)vpi_get(vpiIsModPort, object),
            full_of(vpi_handle(vpiActual, object)),
            typespec != NULL ? text(vpi_get_str(vpiDefName, typespec))
                             : "none");
    }
}

static void holder(void)
{
    vpiHandle defn = vpi_handle_by_name((PLI_BYTE8*)"pkg::Holder", NULL);
    vpiHandle variable = vpi_handle_by_name((PLI_BYTE8*)"top.h", NULL);
    if (defn != NULL) {
        class_variables("Holder", defn);
    }
    if (variable != NULL) {
        class_variables("top.h", vpi_handle(vpiClassObj, variable));
    }
}

// The assignments of the module's processes whose left side is a virtual
// interface variable: both sides are objects (37.29 example 2).
static void side(const char* name, vpiHandle object)
{
    vpi_printf(" %s=%s:%s actual=%s", name,
        text(vpi_get_str(vpiType, object)),
        text(vpi_get_str(vpiFullName, object)),
        full_of(vpi_handle(vpiActual, object)));
}

static void assignments(vpiHandle scope)
{
    vpiHandle processes = vpi_iterate(vpiProcess, scope);
    vpiHandle process;
    while (processes != NULL && (process = vpi_scan(processes)) != NULL) {
        vpiHandle body = vpi_handle(vpiStmt, process);
        vpiHandle statements = vpi_iterate(vpiStmt, body);
        vpiHandle statement;
        while (statements != NULL
            && (statement = vpi_scan(statements)) != NULL) {
            vpiHandle left;
            vpiHandle right;
            if (vpi_get(vpiType, statement) != vpiAssignment) {
                continue;
            }
            left = vpi_handle(vpiLhs, statement);
            right = vpi_handle(vpiRhs, statement);
            if (left == NULL || right == NULL
                || vpi_get(vpiType, left) != vpiVirtualInterfaceVar) {
                continue;
            }
            vpi_printf("#c assign");
            side("lhs", left);
            side("rhs", right);
            vpi_printf("\n");
        }
    }
}

static PLI_INT32 report(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name((PLI_BYTE8*)"top", NULL);
    (void)data;
    scope("top", top);
    scope("top.gen[0]", vpi_handle_by_name((PLI_BYTE8*)"top.gen[0]", NULL));
    scope("top.gen[1]", vpi_handle_by_name((PLI_BYTE8*)"top.gen[1]", NULL));
    scope("pkg", vpi_handle_by_name((PLI_BYTE8*)"pkg::", NULL));
    typedefs("top", top);
    typedefs("top.gen[0]",
        vpi_handle_by_name((PLI_BYTE8*)"top.gen[0]", NULL));
    typedefs("pkg", vpi_handle_by_name((PLI_BYTE8*)"pkg::", NULL));
    typespec_of("top.v1", vpi_handle_by_name((PLI_BYTE8*)"top.v1", NULL));
    typespec_of("top.v16", vpi_handle_by_name((PLI_BYTE8*)"top.v16", NULL));
    typespec_of("top.tv", vpi_handle_by_name((PLI_BYTE8*)"top.tv", NULL));
    holder();
    assignments(top);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbEndOfSimulation;
    data.cb_rtn = report;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

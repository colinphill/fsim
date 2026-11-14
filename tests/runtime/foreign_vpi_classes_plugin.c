// SPDX-License-Identifier: Apache-2.0
// VPI class conformance plug-in (IEEE 1800-2023 37.31, 37.32, 37.33): class
// defns of a package and a module with their extends, derived classes,
// methods, variables, parameters and typedefs, the class typespecs of their
// specializations, and class variables. A class variable's object (vpiObjId,
// vpiClassObj) is live state and is not published: 0 and NULL.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static const char* name_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiName, object)) : "-";
}

static const char* type_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiType, object)) : "-";
}

static const char* visibility_name(PLI_INT32 visibility)
{
    return visibility == vpiPublicVis      ? "public"
        : visibility == vpiProtectedVis    ? "protected"
        : visibility == vpiLocalVis        ? "local"
                                           : "other";
}

static const char* rand_name(PLI_INT32 kind)
{
    return kind == vpiNotRand ? "none"
        : kind == vpiRand     ? "rand"
        : kind == vpiRandC    ? "randc"
                              : "other";
}

static vpiHandle relation(PLI_INT32 type, vpiHandle object)
{
    return object != NULL ? vpi_handle(type, object) : NULL;
}

static int count_of(PLI_INT32 type, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(type, owner);
    int count = 0;
    while (iterator != NULL && vpi_scan(iterator) != NULL) {
        ++count;
    }
    return count;
}

// A method's formals, the variable that holds a function's value and its
// body (IEEE 1800-2023 37.31, 37.41): a constructor and a task have no
// return variable.
static void method_parts(const char* label, vpiHandle method)
{
    vpiHandle const formals = vpi_iterate(vpiIODecl, method);
    vpiHandle const returned = vpi_handle(vpiReturn, method);
    vpiHandle const body = vpi_handle(vpiStmt, method);
    vpiHandle formal;
    char line[240];
    snprintf(line, sizeof line, "#c %s method-parts %s io=", label,
        name_of(method));
    while (formals != NULL && (formal = vpi_scan(formals)) != NULL) {
        snprintf(line + strlen(line), sizeof line - strlen(line),
            "[%s %s dir=%d size=%d scope=%s]", type_of(formal),
            text(vpi_get_str(vpiFullName, formal)),
            (int)vpi_get(vpiDirection, formal), (int)vpi_get(vpiSize, formal),
            name_of(relation(vpiScope, formal)));
    }
    vpi_printf("%s return=%s %s size=%d stmt=%s\n", line, type_of(returned),
        returned != NULL ? text(vpi_get_str(vpiFullName, returned)) : "-",
        returned != NULL ? (int)vpi_get(vpiSize, returned) : 0,
        type_of(body));
}

static void methods(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiMethods, owner);
    vpiHandle method;
    while (iterator != NULL && (method = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %s method %s type=%s method=%d virtual=%d vis=%s "
                   "automatic=%d scope=%s\n",
            label, name_of(method), type_of(method),
            (int)vpi_get(vpiMethod, method), (int)vpi_get(vpiVirtual, method),
            visibility_name((PLI_INT32)vpi_get(vpiVisibility, method)),
            (int)vpi_get(vpiAutomatic, method),
            name_of(relation(vpiScope, method)));
        method_parts(label, method);
    }
}

static void variables(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiVariables, owner);
    vpiHandle variable;
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        s_vpi_value value = { vpiIntVal, { 0 } };
        vpi_get_value(variable, &value);
        vpi_printf("#c %s variable %s type=%s vis=%s automatic=%d rand=%s "
                   "value-format=%s\n",
            label, name_of(variable), type_of(variable),
            visibility_name((PLI_INT32)vpi_get(vpiVisibility, variable)),
            (int)vpi_get(vpiAutomatic, variable),
            rand_name((PLI_INT32)vpi_get(vpiRandType, variable)),
            value.format == vpiSuppressVal ? "none" : "read");
    }
}

static void parameters(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiParameter, owner);
    vpiHandle parameter;
    while (iterator != NULL && (parameter = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %s parameter %s type=%s local=%d\n", label,
            name_of(parameter), type_of(parameter),
            (int)vpi_get(vpiLocalParam, parameter));
    }
}

// The value and typespec of each parameter of a defn or class typespec, and
// the parameter assignments of a class typespec (IEEE 1800-2023 37.28,
// 37.32): a type parameter has no value; its typespec, and the Rhs of its
// assignment, is the type it names.
static void parameter_values(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiParameter, owner);
    vpiHandle item;
    s_vpi_value value;
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        char name[64];
        vpiHandle spec = vpi_handle(vpiTypespec, item);
        snprintf(name, sizeof name, "%s", name_of(item));
        memset(&value, 0, sizeof value);
        value.format = vpiDecStrVal;
        vpi_get_value(item, &value);
        vpi_printf("#c %s parameter-value %s type=%s value=%s typespec=%s\n",
            label, name, type_of(item),
            value.format == vpiSuppressVal || value.value.str == NULL
                ? "none" : value.value.str,
            type_of(spec));
    }
    iterator = vpi_iterate(vpiParamAssign, owner);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        char name[64];
        vpiHandle rhs = vpi_handle(vpiRhs, item);
        snprintf(name, sizeof name, "%s", name_of(vpi_handle(vpiLhs, item)));
        vpi_printf("#c %s param-assign %s lhs-type=%s rhs=%s\n", label, name,
            type_of(vpi_handle(vpiLhs, item)), type_of(rhs));
    }
}

static void typedefs(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiTypedef, owner);
    vpiHandle alias;
    while (iterator != NULL && (alias = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %s typedef %s type=%s\n", label, name_of(alias),
            type_of(alias));
    }
}

static void typespec(const char* label, vpiHandle spec)
{
    vpi_printf("#c %s typespec %s type=%s class-type=%d defn=%s extends=%s "
               "variables=%d methods=%d parameters=%d\n",
        label, name_of(spec), type_of(spec), (int)vpi_get(vpiClassType, spec),
        name_of(relation(vpiClassDefn, spec)),
        name_of(relation(vpiClassTypespec, relation(vpiExtends, spec))),
        count_of(vpiVariables, spec), count_of(vpiMethods, spec),
        count_of(vpiParameter, spec));
    {
        char spec_label[96];
        snprintf(spec_label, sizeof spec_label, "%s %s", label, name_of(spec));
        parameter_values(spec_label, spec);
    }
}

static void defn(vpiHandle object)
{
    // The vpi_get_str buffer is overwritten by the next call (38.12).
    char label[64];
    vpiHandle extends;
    vpiHandle iterator;
    vpiHandle item;
    s_vpi_value value = { vpiIntVal, { 0 } };

    snprintf(label, sizeof label, "%s", name_of(object));
    extends = vpi_handle(vpiExtends, object);
    vpi_get_value(object, &value);
    vpi_printf("#c %s defn type=%s full=%s virtual=%d automatic=%d line=%d "
               "scope=%s value-format=%s\n",
        label, type_of(object), text(vpi_get_str(vpiFullName, object)),
        (int)vpi_get(vpiVirtual, object), (int)vpi_get(vpiAutomatic, object),
        vpi_get(vpiLineNo, object) > 0,
        name_of(relation(vpiScope, object)),
        value.format == vpiSuppressVal ? "none" : "read");
    vpi_printf("#c %s extends type=%s base=%s base-type=%s\n", label,
        type_of(extends),
        name_of(relation(vpiClassTypespec, extends)),
        type_of(relation(vpiClassTypespec, extends)));
    iterator = vpi_iterate(vpiDerivedClasses, object);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %s derived %s\n", label, name_of(item));
    }
    methods(label, object);
    variables(label, object);
    parameters(label, object);
    parameter_values(label, object);
    typedefs(label, object);
    vpi_printf("#c %s constraints=%d\n", label,
        count_of(vpiConstraint, object));
    iterator = vpi_iterate(vpiClassTypespec, object);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        typespec(label, item);
    }
}

static void classes_of(const char* label, vpiHandle scope)
{
    vpiHandle iterator = vpi_iterate(vpiClassDefn, scope);
    vpiHandle object;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %s declares %s\n", label, name_of(object));
        defn(object);
    }
}

static void class_variable(vpiHandle variable)
{
    vpiHandle spec = vpi_handle(vpiTypespec, variable);
    vpiHandle class_spec = vpi_handle(vpiClassTypespec, variable);
    vpi_printf("#c var %s type=%s typespec=%s typespec-type=%s "
               "same=%d defn=%s class-type=%d obj-id=%d obj=%s\n",
        name_of(variable), type_of(variable), name_of(spec), type_of(spec),
        class_spec != NULL && spec != NULL && vpi_compare_objects(spec, class_spec),
        name_of(relation(vpiClassDefn, spec)),
        spec != NULL ? (int)vpi_get(vpiClassType, spec) : 0,
        (int)vpi_get(vpiObjId, variable),
        vpi_handle(vpiClassObj, variable) == NULL ? "null" : "set");
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle iterator;
    vpiHandle object;
    vpiHandle package = NULL;
    (void)data;

    iterator = vpi_iterate(vpiPackage, NULL);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        if (strcmp(name_of(object), "pk") == 0) {
            package = object;
        }
    }
    classes_of("pk", package);
    classes_of("top", top);
    vpi_printf("#c by name %s %s %s %s\n",
        name_of(vpi_handle_by_name("pk::Base", NULL)),
        name_of(vpi_handle_by_name("pk::Derived", NULL)),
        name_of(vpi_handle_by_name("top.Local", NULL)),
        name_of(vpi_handle_by_name("pk::Missing", NULL)));

    iterator = vpi_iterate(vpiVariables, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        class_variable(object);
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = check;
    callback.time = &time;
    vpi_register_cb(&callback);
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

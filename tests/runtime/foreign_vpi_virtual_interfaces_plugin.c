// SPDX-License-Identifier: Apache-2.0
// VPI virtual interface conformance plug-in (IEEE 1800-2023 37.29, 37.30):
// the virtual interface variables of a module and of a class (its defn and
// a live object) with vpiTypespec, vpiIsModPort, vpiExpr and vpiActual, and
// the interface typespec with vpiDefName, vpiIsModPort and vpiParent; and
// the sides of the constructor's `bus = s;` (37.29 example 2), in the class
// definition and inside the call `new(s2)` (simulate --foreign-probes).
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

static const char* full_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiFullName, object)) : "-";
}

static void variable(const char* label, vpiHandle object)
{
    vpiHandle spec = vpi_handle(vpiTypespec, object);
    vpiHandle parent = spec != NULL ? vpi_handle(vpiParent, spec) : NULL;
    vpi_printf("#c %s type=%s name=%s modport=%d expr=%s actual=%s\n", label,
        text(vpi_get_str(vpiType, object)), text(vpi_get_str(vpiName, object)),
        (int)vpi_get(vpiIsModPort, object),
        full_of(vpi_handle(vpiExpr, object)),
        full_of(vpi_handle(vpiActual, object)));
    vpi_printf("#c %s typespec type=%s defname=%s modport=%d parent=%s\n", label,
        spec != NULL ? text(vpi_get_str(vpiType, spec)) : "-",
        spec != NULL ? text(vpi_get_str(vpiDefName, spec)) : "-",
        spec != NULL ? (int)vpi_get(vpiIsModPort, spec) : -1,
        parent != NULL ? text(vpi_get_str(vpiDefName, parent)) : "-");
}

static void class_variables(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiVariables, owner);
    vpiHandle object;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        char name[64];
        if (vpi_get(vpiType, object) != vpiVirtualInterfaceVar) {
            continue;
        }
        snprintf(name, sizeof name, "%s.%s", label,
            text(vpi_get_str(vpiName, object)));
        variable(name, object);
    }
}

// The vpiVirtualInterfaceVar iteration of a class defn or class obj (37.31,
// 37.33 detail 7).
static void class_virtual(const char* label, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(vpiVirtualInterfaceVar, owner);
    vpiHandle object;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c %s virtual %s\n", label,
            text(vpi_get_str(vpiName, object)));
    }
}

// The sides of the constructor's `bus = s;` in the class definition (37.29
// example 2): both are virtual interface variables of type SBus; in the
// class definition no call to new is selected, so neither has an actual.
static void constructor_sides(vpiHandle defn)
{
    vpiHandle iterator = defn != NULL ? vpi_iterate(vpiMethods, defn) : NULL;
    vpiHandle method;
    while (iterator != NULL && (method = vpi_scan(iterator)) != NULL) {
        vpiHandle body;
        const char* const name = vpi_get_str(vpiName, method);
        if (name == NULL || strcmp(name, "new") != 0) {
            continue;
        }
        body = vpi_handle(vpiStmt, method);
        vpi_printf("#c new stmt=%s\n",
            body != NULL ? text(vpi_get_str(vpiType, body)) : "-");
        if (body != NULL && vpi_get(vpiType, body) == vpiAssignment) {
            variable("new.lhs", vpi_handle(vpiLhs, body));
            variable("new.rhs", vpi_handle(vpiRhs, body));
        }
    }
}

// Inside the call `t = new(s2)`: each side is the variable of that call
// (the formal `s` of its frame, the property `bus` of the object being
// constructed), whose actual is the interface the call was given; the
// property's only once the assignment has run, at the frame's end.
static vpiHandle call_lhs = NULL;
static vpiHandle call_rhs = NULL;

static PLI_INT32 constructor_statement(p_cb_data data)
{
    vpiHandle frame = vpi_handle(vpiFrame, NULL);
    vpi_printf("#c call stmt frame=%s\n",
        frame != NULL ? text(vpi_get_str(vpiType, frame)) : "-");
    call_lhs = vpi_handle(vpiLhs, data->obj);
    call_rhs = vpi_handle(vpiRhs, data->obj);
    variable("call.lhs", call_lhs);
    variable("call.rhs", call_rhs);
    return 0;
}

static PLI_INT32 frame_end(p_cb_data data)
{
    (void)data;
    if (call_lhs == NULL && call_rhs == NULL) {
        return 0;
    }
    vpi_printf("#c call end lhs actual=%s rhs actual=%s\n",
        call_lhs != NULL ? full_of(vpi_handle(vpiActual, call_lhs)) : "-",
        call_rhs != NULL ? full_of(vpi_handle(vpiActual, call_rhs)) : "-");
    call_lhs = NULL;
    call_rhs = NULL;
    return 0;
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle iterator;
    vpiHandle object;
    vpiHandle defn = vpi_handle_by_name("pk::Xactor", NULL);
    vpiHandle holder = vpi_handle_by_name("top.t", NULL);
    int count = 0;
    (void)data;

    variable("vi", vpi_handle_by_name("top.vi", NULL));
    variable("vn", vpi_handle_by_name("top.vn", NULL));
    variable("vm", vpi_handle_by_name("top.vm", NULL));
    vpi_printf("#c plain type=%s\n",
        text(vpi_get_str(vpiType, vpi_handle_by_name("top.plain", NULL))));
    iterator = vpi_iterate(vpiVirtualInterfaceVar, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c top virtual %s\n", text(vpi_get_str(vpiName, object)));
    }
    iterator = vpi_iterate(vpiVariables, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        ++count;
    }
    vpi_printf("#c top variables=%d\n", count);
    class_variables("Xactor", defn);
    object = vpi_handle(vpiClassObj, holder);
    class_variables("t", object);
    class_virtual("Xactor", defn);
    class_virtual("t", object);
    constructor_sides(defn);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    vpiHandle defn = vpi_handle_by_name("pk::Xactor", NULL);
    vpiHandle iterator = defn != NULL ? vpi_iterate(vpiMethods, defn) : NULL;
    vpiHandle method;
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = check;
    callback.time = &time;
    vpi_register_cb(&callback);
    while (iterator != NULL && (method = vpi_scan(iterator)) != NULL) {
        const char* const name = vpi_get_str(vpiName, method);
        if (name == NULL || strcmp(name, "new") != 0) {
            continue;
        }
        memset(&callback, 0, sizeof callback);
        callback.reason = cbStmt;
        callback.obj = vpi_handle(vpiStmt, method);
        callback.cb_rtn = constructor_statement;
        if (callback.obj == NULL || vpi_register_cb(&callback) == NULL) {
            vpi_printf("#c cbStmt on new failed\n");
        }
    }
    memset(&callback, 0, sizeof callback);
    callback.reason = cbEndOfFrame;
    callback.cb_rtn = frame_end;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c cbEndOfFrame failed\n");
    }
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

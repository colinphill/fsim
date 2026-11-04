// SPDX-License-Identifier: Apache-2.0
// VPI live class object conformance plug-in (IEEE 1800-2023 37.17 detail 14,
// 37.33, 37.34, 37.38, 38.36.1): class variables and class objects, the
// properties of a live object, its constraints, cbValueChange on a class
// variable, cbSizeChange on a dynamic array and a queue, and cbReclaimObj at
// the end of the simulation.
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

static int count_of(PLI_INT32 type, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(type, owner);
    int count = 0;
    while (iterator != NULL && vpi_scan(iterator) != NULL) {
        ++count;
    }
    return count;
}

// The first object of an iteration, or NULL.
static vpiHandle first_of(PLI_INT32 type, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(type, owner);
    vpiHandle object = iterator != NULL ? vpi_scan(iterator) : NULL;
    if (object != NULL) {
        vpi_free_object(iterator);
    }
    return object;
}

static PLI_INT32 value_changed(p_cb_data data)
{
    vpi_printf("#c valuechange %s value=%s objid-set=%d\n", name_of(data->obj),
        data->value == NULL ? "null" : "set",
        vpi_get(vpiObjId, data->obj) > 0);
    return 0;
}

static PLI_INT32 size_changed(p_cb_data data)
{
    vpi_printf("#c sizechange %s size=%d\n", name_of(data->obj),
        (int)vpi_get(vpiSize, data->obj));
    return 0;
}

static PLI_INT32 reclaimed(p_cb_data data)
{
    vpi_printf("#c reclaim %s type=%s objid-set=%d typespec=%s\n",
        (const char*)data->user_data, type_of(data->obj),
        vpi_get(vpiObjId, data->obj) > 0,
        name_of(vpi_handle(vpiClassTypespec, data->obj)));
    return 0;
}

static PLI_INT32 ended(p_cb_data data)
{
    (void)data;
    vpi_printf("#c end of simulation\n");
    return 0;
}

static void register_cb(PLI_INT32 reason, PLI_INT32 (*routine)(p_cb_data),
    vpiHandle object, const char* user)
{
    s_cb_data callback;
    s_vpi_value value = { vpiIntVal, { 0 } };
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = routine;
    callback.obj = object;
    callback.value = &value;
    callback.user_data = (PLI_BYTE8*)user;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c register %d failed\n", (int)reason);
    }
}

static void variable(const char* label, vpiHandle object)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpiHandle obj = vpi_handle(vpiClassObj, object);
    vpiHandle spec = vpi_handle(vpiClassTypespec, object);
    vpiHandle obj_spec = obj != NULL ? vpi_handle(vpiClassTypespec, obj) : NULL;

    vpi_get_value(object, &value);
    vpi_printf("#c var %s type=%s value-format=%s obj=%s obj-type=%s "
               "ids-equal=%d typespec=%s obj-typespec=%s\n",
        label, type_of(object),
        value.format == vpiSuppressVal ? "none" : "read",
        obj == NULL ? "null" : "set", type_of(obj),
        obj == NULL ? (vpi_get(vpiObjId, object) == 0)
                    : (vpi_get(vpiObjId, object) == vpi_get(vpiObjId, obj)
                        && vpi_get(vpiObjId, obj) > 0),
        name_of(spec), name_of(obj_spec));
}

static void property_of(vpiHandle member)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpiHandle obj = vpi_handle(vpiClassObj, member);
    char name[64];

    snprintf(name, sizeof name, "%s", name_of(member));
    vpi_get_value(member, &value);
    vpi_printf("#c property %s type=%s full=%s value=%s class-obj=%s "
               "objid-set=%d\n",
        name, type_of(member), text(vpi_get_str(vpiFullName, member)),
        value.format == vpiSuppressVal ? "none" : "read",
        obj == NULL ? "null" : "set", vpi_get(vpiObjId, member) > 0);
}

static void constraint_item(const char* indent, vpiHandle item);

static void items(const char* indent, PLI_INT32 relation, vpiHandle owner)
{
    vpiHandle iterator = vpi_iterate(relation, owner);
    vpiHandle item;
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        constraint_item(indent, item);
    }
}

static void constraint_item(const char* indent, vpiHandle item)
{
    char deeper[32];
    PLI_INT32 type = vpi_get(vpiType, item);
    vpiHandle parent = vpi_handle(vpiParent, item);

    snprintf(deeper, sizeof deeper, "%s  ", indent);
    vpi_printf("#c %sitem %s soft=%d parent=%s\n", indent, type_of(item),
        (int)vpi_get(vpiSoft, item), type_of(parent));
    if (type == vpiImplication || type == vpiConstrIf
        || type == vpiConstrIfElse) {
        vpi_printf("#c %scondition %s\n", deeper,
            type_of(vpi_handle(vpiCondition, item)));
        items(deeper, vpiConstraintExpr, item);
        items(deeper, vpiElseConst, item);
    } else if (type == vpiConstrForEach) {
        vpi_printf("#c %sloop-vars=%d array=%s\n", deeper,
            count_of(vpiLoopVars, item),
            type_of(vpi_handle(vpiVariables, item)));
        items(deeper, vpiConstraintExpr, item);
    } else if (type == vpiDistribution) {
        vpiHandle iterator = vpi_iterate(vpiDistItem, item);
        vpiHandle dist;
        vpi_printf("#c %sexpr %s\n", deeper,
            type_of(vpi_handle(vpiExpr, item)));
        while (iterator != NULL && (dist = vpi_scan(iterator)) != NULL) {
            vpi_printf("#c %sdist %s dist-type=%d weight=%s\n", deeper,
                type_of(dist), (int)vpi_get(vpiDistType, dist),
                type_of(vpi_handle(vpiWeight, dist)));
        }
    } else if (type == vpiConstraintOrdering) {
        vpi_printf("#c %sbefore=%d after=%d\n", deeper,
            count_of(vpiSolveBefore, item), count_of(vpiSolveAfter, item));
    }
}

static void constraint_of(vpiHandle constraint)
{
    char name[64];
    snprintf(name, sizeof name, "%s", name_of(constraint));
    vpi_printf("#c constraint %s type=%s enabled=%d automatic=%d access=%d "
               "alloc=%d parent=%s\n",
        name, type_of(constraint),
        (int)vpi_get(vpiIsConstraintEnabled, constraint),
        (int)vpi_get(vpiAutomatic, constraint),
        (int)vpi_get(vpiAccessType, constraint),
        (int)vpi_get(vpiAllocScheme, constraint),
        type_of(vpi_handle(vpiParent, constraint)));
    items("", vpiConstraintItem, constraint);
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle a = vpi_handle_by_name("top.a", NULL);
    vpiHandle b = vpi_handle_by_name("top.b", NULL);
    vpiHandle c = vpi_handle_by_name("top.c", NULL);
    vpiHandle obj = vpi_handle(vpiClassObj, a);
    vpiHandle iterator;
    vpiHandle member;
    s_vpi_value put = { vpiIntVal, { 0 } };
    s_vpi_value read = { vpiIntVal, { 0 } };
    (void)data;

    variable("a", a);
    variable("b", b);
    variable("c", c);

    iterator = vpi_iterate(vpiVariables, obj);
    while (iterator != NULL && (member = vpi_scan(iterator)) != NULL) {
        property_of(member);
    }
    // vpi_put_value on a property of a live object.
    member = first_of(vpiVariables, obj);
    put.value.integer = 9;
    vpi_put_value(member, &put, NULL, vpiNoDelay);
    vpi_get_value(member, &read);
    vpi_printf("#c put %s value=%d\n", name_of(member), (int)read.value.integer);

    vpi_printf("#c object methods=%d parameters=%d constraints=%d\n",
        count_of(vpiMethods, obj), count_of(vpiParameter, obj),
        count_of(vpiConstraint, obj));
    iterator = vpi_iterate(vpiConstraint, obj);
    while (iterator != NULL && (member = vpi_scan(iterator)) != NULL) {
        constraint_of(member);
    }
    register_cb(cbReclaimObj, reclaimed, obj, "own");
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 6, 0.0 };
    vpiHandle a = vpi_handle_by_name("top.a", NULL);
    (void)data;
    register_cb(cbValueChange, value_changed, a, "");
    register_cb(cbSizeChange, size_changed,
        vpi_handle_by_name("top.dyn", NULL), "");
    register_cb(cbSizeChange, size_changed,
        vpi_handle_by_name("top.q", NULL), "");
    register_cb(cbReclaimObj, reclaimed, vpi_handle(vpiClassTypespec, a),
        "typespec");
    register_cb(cbEndOfSimulation, ended, NULL, "");
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

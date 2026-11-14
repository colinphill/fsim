// SPDX-License-Identifier: Apache-2.0
// VPI conformance plug-in for mailbox and semaphore class objects (IEEE
// 1800-2023 37.33 details 3, 4), the inline constraint of a randomize call
// (37.42 detail 1), a task reached through an interface port (37.41) and
// the properties of a class typespec that represents a specialization
// (37.32 details 5, 6). Run with --foreign-probes, so that the processes
// waiting on the semaphore are threads (37.44).
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

static vpiHandle relation(PLI_INT32 type, vpiHandle object)
{
    return object != NULL ? vpi_handle(type, object) : NULL;
}

static int same(vpiHandle left, vpiHandle right)
{
    return left != NULL && right != NULL && vpi_compare_objects(left, right);
}

// The integer value of an object, or -1 when it has none.
static int integer_value(vpiHandle object)
{
    s_vpi_value value;
    memset(&value, 0, sizeof value);
    value.format = vpiIntVal;
    vpi_get_value(object, &value);
    return value.format == vpiIntVal ? (int)value.value.integer : -1;
}

static void mailbox_object(void)
{
    vpiHandle variable = vpi_handle_by_name("top.mq", NULL);
    vpiHandle object = relation(vpiClassObj, variable);
    vpiHandle iterator;
    vpiHandle message;
    vpi_printf("#c mailbox type=%s class-type=%d defn=%s id-same=%d "
               "id-set=%d\n",
        type_of(object), (int)vpi_get(vpiClassType, object),
        name_of(relation(vpiClassDefn, object)),
        object != NULL
            && vpi_get(vpiObjId, variable) == vpi_get(vpiObjId, object),
        object != NULL && vpi_get(vpiObjId, object) != 0);
    iterator = object != NULL ? vpi_iterate(vpiMessages, object) : NULL;
    while (iterator != NULL && (message = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c mailbox message type=%s const-type=%d size=%d "
                   "value=%d parent=%d\n",
            type_of(message), (int)vpi_get(vpiConstType, message),
            (int)vpi_get(vpiSize, message), integer_value(message),
            same(relation(vpiParent, message), object));
    }
}

static void semaphore_object(void)
{
    vpiHandle object
        = relation(vpiClassObj, vpi_handle_by_name("top.sem", NULL));
    vpiHandle iterator;
    vpiHandle thread;
    int count = 0;
    vpi_printf("#c semaphore type=%s class-type=%d messages=%s\n",
        type_of(object), (int)vpi_get(vpiClassType, object),
        object != NULL && vpi_iterate(vpiMessages, object) != NULL ? "some"
                                                                   : "none");
    iterator = object != NULL ? vpi_iterate(vpiWaitingProcesses, object) : NULL;
    while (iterator != NULL && (thread = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c semaphore waiting type=%s\n", type_of(thread));
        ++count;
    }
    vpi_printf("#c semaphore waiting count=%d\n", count);
}

static void port_task(void)
{
    vpiHandle through = vpi_handle_by_name("top.u.bus.bump", NULL);
    vpiHandle direct = vpi_handle_by_name("top.bus.bump", NULL);
    vpiHandle parent = relation(vpiParent, through);
    vpi_printf("#c port task type=%s name=%s parent=%s parent-type=%s "
               "direct-parent-type=%s same=%d\n",
        type_of(through), name_of(through), name_of(parent), type_of(parent),
        type_of(relation(vpiParent, direct)), same(through, direct));
}

static void inline_constraint(vpiHandle top)
{
    vpiHandle processes = vpi_iterate(vpiProcess, top);
    vpiHandle process;
    while (processes != NULL && (process = vpi_scan(processes)) != NULL) {
        vpiHandle block = relation(vpiStmt, process);
        vpiHandle statements;
        vpiHandle statement;
        if (block == NULL || vpi_get(vpiType, block) != vpiBegin) {
            continue;
        }
        statements = vpi_iterate(vpiStmt, block);
        while (statements != NULL && (statement = vpi_scan(statements)) != NULL) {
            vpiHandle call = relation(vpiRhs, statement);
            vpiHandle with = relation(vpiWith, call);
            vpiHandle items;
            vpiHandle item;
            PLI_INT32 rhs_type = call != NULL ? vpi_get(vpiType, call) : 0;
            if (rhs_type == vpiFuncCall) {
                s_vpi_value value;
                memset(&value, 0, sizeof value);
                value.format = vpiStringVal;
                vpi_get_value(call, &value);
                vpi_printf("#c result %s value=%s\n", name_of(call),
                    value.format == vpiStringVal ? text(value.value.str)
                                                 : "none");
            }
            if (rhs_type == vpiBitSelect || rhs_type == vpiVarSelect) {
                vpi_printf("#c result select type=%s value=%d\n",
                    type_of(call), integer_value(call));
            }
            if (with == NULL) {
                continue;
            }
            vpi_printf("#c with call=%s type=%s name=%s enabled=%d "
                       "virtual=%d parent=%d\n",
                type_of(call), type_of(with), name_of(with),
                (int)vpi_get(vpiIsConstraintEnabled, with),
                (int)vpi_get(vpiVirtual, with),
                same(relation(vpiParent, with), call));
            items = vpi_iterate(vpiConstraintItem, with);
            while (items != NULL && (item = vpi_scan(items)) != NULL) {
                vpi_printf("#c with item type=%s op=%d\n", type_of(item),
                    (int)vpi_get(vpiOpType, item));
            }
        }
    }
}

static void constraint_values(void)
{
    vpiHandle object
        = relation(vpiClassObj, vpi_handle_by_name("top.p", NULL));
    vpiHandle constraints
        = object != NULL ? vpi_iterate(vpiConstraint, object) : NULL;
    vpiHandle constraint;
    while (constraints != NULL
        && (constraint = vpi_scan(constraints)) != NULL) {
        vpiHandle items = vpi_iterate(vpiConstraintItem, constraint);
        vpiHandle item;
        while (items != NULL && (item = vpi_scan(items)) != NULL) {
            vpiHandle operands = vpi_iterate(vpiOperand, item);
            vpiHandle operand;
            vpi_printf("#c constraint %s item type=%s value=%d parent=%d\n",
                name_of(constraint), type_of(item), integer_value(item),
                same(relation(vpiParent, item), constraint));
            while (operands != NULL && (operand = vpi_scan(operands)) != NULL) {
                vpi_printf("#c constraint operand type=%s name=%s value=%d\n",
                    type_of(operand), name_of(operand), integer_value(operand));
            }
        }
    }
    {
        vpiHandle defn = relation(vpiClassDefn, object);
        vpiHandle defn_constraints = defn != NULL ? vpi_iterate(vpiConstraint, defn) : NULL;
        vpiHandle first = defn_constraints != NULL ? vpi_scan(defn_constraints) : NULL;
        vpiHandle defn_items = first != NULL ? vpi_iterate(vpiConstraintItem, first) : NULL;
        vpiHandle defn_item = defn_items != NULL ? vpi_scan(defn_items) : NULL;
        vpi_printf("#c constraint defn item value=%d\n", integer_value(defn_item));
        if (defn_items != NULL && defn_item != NULL) {
            vpi_free_object(defn_items);
        }
        if (defn_constraints != NULL && first != NULL) {
            vpi_free_object(defn_constraints);
        }
    }
}

static void specialization(void)
{
    vpiHandle spec = relation(vpiTypespec, vpi_handle_by_name("top.b", NULL));
    vpiHandle iterator = spec != NULL ? vpi_iterate(vpiVariables, spec) : NULL;
    vpiHandle variable;
    vpi_printf("#c box typespec=%s\n", name_of(spec));
    while (iterator != NULL && (variable = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c box variable %s type=%s automatic=%d size=%d "
                   "typespec=%s parent=%d scope=%d value=%d\n",
            name_of(variable), type_of(variable),
            (int)vpi_get(vpiAutomatic, variable),
            (int)vpi_get(vpiSize, variable),
            type_of(relation(vpiTypespec, variable)),
            same(relation(vpiParent, variable), spec),
            same(relation(vpiScope, variable), spec),
            integer_value(variable));
    }
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    (void)data;
    mailbox_object();
    semaphore_object();
    port_task();
    inline_constraint(top);
    constraint_values();
    specialization();
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

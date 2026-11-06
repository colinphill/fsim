// SPDX-License-Identifier: Apache-2.0
// VHPI relation conformance plug-in (IEEE 1076-2019 19.4, 19.10, 19.12):
// component declarations, subprogram declarations of calls, the signals a
// procedure call drives, integer range bounds, simulated nets, library
// declarations and design units, and the string properties that go with
// them. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of four buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[8][128];
    static int next;
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 8;
    if (value == NULL) {
        return "-";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffers[0];
         ++index) {
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

// The full name of the object a relation leads to, or "-".
static const char* related(vhpiHandleT object, vhpiOneToOneT relation)
{
    vhpiHandleT target = vhpi_handle(relation, object);
    return target == NULL ? "-" : text_property(target, vhpiFullNameP);
}

// The number of objects of an iteration.
static int count(vhpiOneToManyT relation, vhpiHandleT object)
{
    vhpiHandleT iterator = vhpi_iterator(relation, object);
    int total = 0;
    while (iterator != NULL && vhpi_scan(iterator) != NULL) {
        ++total;
    }
    return total;
}

// The kind and lower-cased name of the object a relation leads to.
static void kind_name(const char* label, vhpiHandleT object)
{
    printf(" %s=%s:%s", label, text_property(object, vhpiKindStrP),
        text_property(object, vhpiNameP));
}

// The generic and port maps of a component instance, and of the
// configuration specification that binds it (IEEE 1076-2019 19.5, 19.10).
static void maps(const char* path)
{
    vhpiHandleT instance = vhpi_handle_by_name(path, NULL);
    vhpiHandleT specification = vhpi_handle(vhpiConfigSpec, instance);
    vhpiHandleT iterator;
    vhpiHandleT assoc;
    printf("#c %s configspec=%d generics=%d\n", path,
        specification != NULL, count(vhpiGenericAssocs, instance));
    iterator = vhpi_iterator(vhpiGenericAssocs, instance);
    while (iterator != NULL && (assoc = vhpi_scan(iterator)) != NULL) {
        printf("#c   generic assoc");
        kind_name("formal", vhpi_handle(vhpiFormal, assoc));
        printf(" actual=%s\n",
            text_property(vhpi_handle(vhpiActual, assoc), vhpiKindStrP));
    }
    if (specification == NULL) {
        return;
    }
    printf("#c   spec compname=%s ports=%d generics=%d\n",
        text_property(specification, vhpiCompNameP),
        count(vhpiPortAssocs, specification),
        count(vhpiGenericAssocs, specification));
    iterator = vhpi_iterator(vhpiPortAssocs, specification);
    while (iterator != NULL && (assoc = vhpi_scan(iterator)) != NULL) {
        printf("#c   spec port assoc");
        kind_name("formal", vhpi_handle(vhpiFormal, assoc));
        kind_name("actual", vhpi_handle(vhpiActual, assoc));
        printf("\n");
    }
}

static void instances(void)
{
    vhpiHandleT u1 = vhpi_handle_by_name("top.u1", NULL);
    vhpiHandleT u2 = vhpi_handle_by_name("top.u2", NULL);
    const char* name;
    int failed;
    printf("#c u1 kind=%d compdecl=%s compname=%s\n",
        (int)vhpi_get(vhpiCompInstKindP, u1), related(u1, vhpiCompDecl),
        text_property(u1, vhpiCompNameP));
    // A direct entity instantiation has no component.
    name = text_property(u2, vhpiCompNameP);
    failed = vhpi_check_error(NULL) != 0;
    printf("#c u2 kind=%d compdecl=%s compname=%s error=%d\n",
        (int)vhpi_get(vhpiCompInstKindP, u2), related(u2, vhpiCompDecl), name,
        failed);
}

static void specifications(void)
{
    vhpiHandleT unit = vhpi_handle(vhpiDesignUnit, vhpi_handle_by_name("top", NULL));
    printf("#c top configspecs=%d\n", count(vhpiConfigSpecs, unit));
    maps("top.u1");
    maps("top.u2");
}

static void calls(void)
{
    vhpiHandleT process = vhpi_handle_by_name("top.p", NULL);
    vhpiHandleT statements = vhpi_iterator(vhpiSeqStmts, process);
    vhpiHandleT assign = vhpi_scan(statements);
    vhpiHandleT call = vhpi_handle(vhpiRhsExpr, assign);
    vhpiHandleT sigs;
    vhpiHandleT driven;
    vhpiHandleT params;
    vhpiHandleT actual;
    printf("#c call %s funcdecl=%s subpdecl=%s\n",
        text_property(call, vhpiNameP), related(call, vhpiFuncDecl),
        related(call, vhpiSubpDecl));
    call = vhpi_scan(statements);
    sigs = vhpi_iterator(vhpiDrivenSigs, call);
    driven = sigs != NULL ? vhpi_scan(sigs) : NULL;
    printf("#c call %s funcdecl=%s subpdecl=%s drivensigs=%s\n",
        text_property(call, vhpiNameP), related(call, vhpiFuncDecl),
        related(call, vhpiSubpDecl), text_property(driven, vhpiFullNameP));
    assign = vhpi_scan(statements);
    call = vhpi_handle(vhpiRhsExpr, assign);
    printf("#c call %s isoperator=%d opname=%s subpdecl=%s\n",
        text_property(call, vhpiNameP), (int)vhpi_get(vhpiIsOperatorP, call),
        text_property(call, (vhpiStrPropertyT)DEPRECATED_vhpiOpNameP),
        related(call, vhpiSubpDecl));
    // A static call that the design folds to its value stays a funcCall of
    // the static model, with its actual and its staticness.
    assign = vhpi_scan(statements);
    call = vhpi_handle(vhpiRhsExpr, assign);
    params = vhpi_iterator(vhpiParamAssocs, call);
    actual = params != NULL ? vhpi_handle(vhpiActual, vhpi_scan(params)) : NULL;
    printf("#c call %s kind=%s funcdecl=%s isoperator=%d params=%d "
           "static=%d actual=%s %s\n",
        text_property(call, vhpiNameP), text_property(call, vhpiKindStrP),
        related(call, vhpiFuncDecl), (int)vhpi_get(vhpiIsOperatorP, call),
        (int)vhpi_get(vhpiNumParamsP, call),
        (int)vhpi_get(vhpiStaticnessP, call),
        text_property(actual, vhpiKindStrP),
        text_property(actual, vhpiFullNameP));
}

static void ranges(void)
{
    vhpiHandleT subtype = vhpi_handle_by_name("top.small", NULL);
    vhpiHandleT range = vhpi_handle(vhpiConstraint, subtype);
    printf("#c range kind=%s left=%s right=%s\n",
        text_property(range, vhpiKindStrP),
        text_property(vhpi_handle(vhpiLeftExpr, range), vhpiNameP),
        text_property(vhpi_handle(vhpiRightExpr, range), vhpiNameP));
}

static void nets(void)
{
    vhpiHandleT s = vhpi_handle_by_name("top.s", NULL);
    vhpiHandleT t = vhpi_handle_by_name("top.t", NULL);
    vhpiHandleT process = vhpi_handle_by_name("top.p", NULL);
    vhpiHandleT q = vhpi_handle_by_name("top.q", NULL);
    printf("#c simnet of s=%s of t=%s of a process=%s\n",
        related(s, vhpiSimNet), related(t, vhpiSimNet),
        related(process, vhpiSimNet));
    printf("#c contributors of t=%d local=%d\n",
        count((vhpiOneToManyT)DEPRECATED_vhpiContributors, t),
        count(vhpiLocalContributors, t));
    printf("#c sensitivities of q=%d signames=%d\n",
        count(vhpiSensitivities, q), count(vhpiSigNames, q));
}

static void libraries(void)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT unit = vhpi_handle(vhpiDesignUnit, root);
    vhpiHandleT library = vhpi_handle(vhpiLibraryDecl, unit);
    vhpiHandleT units = vhpi_iterator(vhpiDesignUnits, library);
    vhpiHandleT item;
    printf("#c library %s kind=%s\n", text_property(library, vhpiLibLogicalNameP),
        text_property(library, vhpiKindStrP));
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        printf("#c unit %s %s\n", text_property(item, vhpiKindStrP),
            text_property(item, vhpiNameP));
    }
}

static int delayed_changes;

// A change of s'DELAYED(2 ns), which follows s by two nanoseconds.
static void delayed_changed(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    (void)vhpi_get_value(data->obj, &value);
    vhpi_get_time(&now, NULL);
    printf("#c delayed change %d value=%d at %u\n", ++delayed_changes,
        (int)value.value.intg, (unsigned)now.low);
    fflush(stdout);
}

// The integer position of an object's value, or -1.
static int position_of(vhpiHandleT object)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    return vhpi_get_value(object, &value) == 0 ? (int)value.value.intg : -1;
}

static vhpiHandleT stable_signal;
static vhpiHandleT quiet_signal;

// A change of s'STABLE(5 ns) or s'QUIET(3 ns): FALSE in the cycle of the
// event or transaction on s, TRUE again after the time (IEEE 1076-2019
// 14.7.4).
static void state_changed(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    printf("#c %s change value=%d at %u\n",
        text_property(data->obj, vhpiNameP), position_of(data->obj),
        (unsigned)now.low);
    fflush(stdout);
}

// An event on s reads the implicit signals in its own cycle, where they are
// already updated (IEEE 1076-2019 14.7.4).
static void s_changed(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    vhpi_get_time(&now, NULL);
    printf("#c s change value=%d stable=%d quiet=%d at %u\n",
        position_of(data->obj), position_of(stable_signal),
        position_of(quiet_signal), (unsigned)now.low);
    fflush(stdout);
}

// The implicit signals attached to a declared signal, by name
// (IEEE 1076-2019 14.7.3.3, 19.4.3 vhpiSigAttrs).
static void implicit_signals(const char* path)
{
    vhpiHandleT signal = vhpi_handle_by_name(path, NULL);
    vhpiHandleT iterator = vhpi_iterator(vhpiSigAttrs, signal);
    vhpiHandleT items[8];
    size_t total = 0U;
    size_t index;
    size_t other;
    vhpiHandleT item;
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL
        && total < 8U) {
        items[total++] = item;
    }
    // Elaboration creates them in the order the design uses them; the trace
    // sorts them by name.
    for (index = 1U; index < total; ++index) {
        for (other = index; other > 0U
             && strcmp(text_property(items[other - 1U], vhpiNameP),
                    text_property(items[other], vhpiNameP))
                > 0;
             --other) {
            item = items[other];
            items[other] = items[other - 1U];
            items[other - 1U] = item;
        }
    }
    printf("#c %s has %d implicit signals\n", path, (int)total);
    for (index = 0U; index < total; ++index) {
        vhpiHandleT attribute = items[index];
        printf("#c   %s %s attrkind=%d predef=%d prefix=%s basic=%d value=%d\n",
            text_property(attribute, vhpiKindStrP),
            text_property(attribute, vhpiFullNameP),
            (int)vhpi_get(vhpiAttrKindP, attribute),
            (int)vhpi_get(vhpiPredefAttrP, attribute),
            related(attribute, vhpiPrefix),
            (int)vhpi_get(vhpiIsBasicP, attribute), position_of(attribute));
    }
}

static void implicit(void)
{
    vhpiHandleT s = vhpi_handle_by_name("top.s", NULL);
    vhpiHandleT iterator = vhpi_iterator(vhpiSigAttrs, s);
    vhpiHandleT item;
    vhpiCbDataT data;
    // Only the attributes the design uses exist; d has none.
    implicit_signals("top.s");
    implicit_signals("top.t");
    implicit_signals("top.d");
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        if (strcmp(text_property(item, vhpiNameP), "s'delayed") == 0) {
            memset(&data, 0, sizeof data);
            data.reason = vhpiCbValueChange;
            data.cb_rtn = delayed_changed;
            data.obj = item;
            (void)vhpi_register_cb(&data, 0);
        } else if (strcmp(text_property(item, vhpiNameP), "s'stable") == 0
            || strcmp(text_property(item, vhpiNameP), "s'quiet") == 0) {
            if (text_property(item, vhpiNameP)[2] == 's') {
                stable_signal = item;
            } else {
                quiet_signal = item;
            }
            memset(&data, 0, sizeof data);
            data.reason = vhpiCbValueChange;
            data.cb_rtn = state_changed;
            data.obj = item;
            (void)vhpi_register_cb(&data, 0);
        }
    }
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbValueChange;
    data.cb_rtn = s_changed;
    data.obj = s;
    (void)vhpi_register_cb(&data, 0);
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    instances();
    specifications();
    calls();
    ranges();
    nets();
    libraries();
    implicit();
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

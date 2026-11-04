// SPDX-License-Identifier: Apache-2.0
// VPI primitive conformance plug-in: gate, switch, pull and UDP instances,
// primitive arrays, prim terms, UDP definitions and table entries, the value
// and strength of primitives and nets, and vpi_put_value on UDPs (IEEE
// 1800-2023 37.35, 37.36, 38.15, 38.34).
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

static vpiHandle named(const char* name)
{
    return vpi_handle_by_name((PLI_BYTE8*)name, NULL);
}

static int int_of(vpiHandle object)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    if (object == NULL) {
        return -9;
    }
    vpi_get_value(object, &value);
    return (int)value.value.integer;
}

static vpiHandle first_of(PLI_INT32 relation, vpiHandle reference)
{
    vpiHandle iterator = vpi_iterate(relation, reference);
    return iterator != NULL ? vpi_scan(iterator) : NULL;
}

static void value_of(const char* label, vpiHandle object)
{
    s_vpi_value value = { vpiBinStrVal, { 0 } };
    if (object == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    vpi_get_value(object, &value);
    vpi_printf("#c %s value=%s\n", label,
        value.format == vpiBinStrVal && value.value.str != NULL
            ? value.value.str
            : "none");
}

static void strength_of(const char* label, vpiHandle object)
{
    s_vpi_value value = { vpiStrengthVal, { 0 } };
    if (object == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    vpi_get_value(object, &value);
    if (value.format != vpiStrengthVal || value.value.strength == NULL) {
        vpi_printf("#c %s strength none\n", label);
        return;
    }
    vpi_printf("#c %s logic=%d s0=%d s1=%d\n", label,
        (int)value.value.strength->logic, (int)value.value.strength->s0,
        (int)value.value.strength->s1);
}

static void show(const char* label, vpiHandle object)
{
    if (object == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    vpi_printf("#c %s type=%s name=%s full=%s def=%s prim=%d s0=%d s1=%d "
               "member=%d\n",
        label, text(vpi_get_str(vpiType, object)),
        text(vpi_get_str(vpiName, object)),
        text(vpi_get_str(vpiFullName, object)),
        text(vpi_get_str(vpiDefName, object)), (int)vpi_get(vpiPrimType, object),
        (int)vpi_get(vpiStrength0, object), (int)vpi_get(vpiStrength1, object),
        (int)vpi_get(vpiArrayMember, object));
}

static void sized(const char* label, vpiHandle object)
{
    vpi_printf("#c %s size=%d line=%d\n", label, (int)vpi_get(vpiSize, object),
        (int)vpi_get(vpiLineNo, object));
}

static void terms(const char* label, vpiHandle object)
{
    vpiHandle iterator = vpi_iterate(vpiPrimTerm, object);
    vpiHandle term;
    int count = 0;
    while (iterator != NULL && (term = vpi_scan(iterator)) != NULL) {
        vpiHandle net = vpi_handle(vpiExpr, term);
        s_vpi_value value = { vpiBinStrVal, { 0 } };
        vpi_get_value(term, &value);
        vpi_printf("#c %s term[%d] type=%s dir=%d index=%d value=%s expr=%s\n",
            label, count, text(vpi_get_str(vpiType, term)),
            (int)vpi_get(vpiDirection, term), (int)vpi_get(vpiTermIndex, term),
            value.format == vpiBinStrVal && value.value.str != NULL
                ? value.value.str
                : "none",
            net != NULL ? text(vpi_get_str(vpiFullName, net)) : "-");
        ++count;
    }
    vpi_printf("#c %s terms=%d\n", label, count);
}

static int count_of(PLI_INT32 relation, vpiHandle reference)
{
    vpiHandle iterator = vpi_iterate(relation, reference);
    int count = 0;
    while (iterator != NULL && vpi_scan(iterator) != NULL) {
        ++count;
    }
    return count;
}

// The prim terms among the drivers or loads of a net or variable.
static int prim_terms_of(PLI_INT32 relation, vpiHandle object)
{
    vpiHandle iterator = vpi_iterate(relation, object);
    vpiHandle connection;
    int count = 0;
    while (iterator != NULL && (connection = vpi_scan(iterator)) != NULL) {
        if (vpi_get(vpiType, connection) == vpiPrimTerm) {
            ++count;
        }
    }
    return count;
}

static void table_entries(const char* label, vpiHandle definition)
{
    vpiHandle iterator = vpi_iterate(vpiTableEntry, definition);
    vpiHandle entry;
    int count = 0;
    while (iterator != NULL && (entry = vpi_scan(iterator)) != NULL) {
        s_vpi_value value = { vpiStringVal, { 0 } };
        s_vpi_value vector = { vpiVectorVal, { 0 } };
        s_vpi_value other = { vpiIntVal, { 0 } };
        vpi_get_value(entry, &value);
        vpi_printf("#c %s entry[%d] type=%s size=%d text=%s\n", label, count,
            text(vpi_get_str(vpiType, entry)), (int)vpi_get(vpiSize, entry),
            value.format == vpiStringVal ? text(value.value.str) : "none");
        if (count == 0) {
            vpi_get_value(entry, &vector);
            vpi_printf("#c %s entry[0] aval=%08x bval=%08x\n", label,
                (unsigned)vector.value.vector[0].aval,
                (unsigned)vector.value.vector[0].bval);
            other.format = vpiIntVal;
            vpi_get_value(entry, &other);
            vpi_printf("#c %s entry[0] int=%s\n", label,
                other.format == vpiIntVal ? "given" : "none");
        }
        ++count;
    }
    vpi_printf("#c %s entries=%d\n", label, count);
}

static void definition(const char* label, vpiHandle udp)
{
    vpiHandle defn = vpi_handle(vpiUdpDefn, udp);
    vpiHandle initial;
    if (defn == NULL) {
        vpi_printf("#c %s defn missing\n", label);
        return;
    }
    vpi_printf("#c %s defn type=%s def=%s size=%d prim=%d protected=%d\n",
        label, text(vpi_get_str(vpiType, defn)),
        text(vpi_get_str(vpiDefName, defn)), (int)vpi_get(vpiSize, defn),
        (int)vpi_get(vpiPrimType, defn), (int)vpi_get(vpiProtected, defn));
    table_entries(label, defn);
    initial = vpi_handle(vpiInitial, defn);
    vpi_printf("#c %s initial=%s count=%d\n", label,
        initial != NULL ? text(vpi_get_str(vpiType, initial)) : "-",
        count_of(vpiInitial, defn));
}

// A rejected vpi_put_value leaves an error behind (IEEE 1800-2023 38.34).
static void put(const char* label, vpiHandle object, PLI_INT32 flags)
{
    s_vpi_value value = { vpiScalarVal, { 0 } };
    s_vpi_time delay = { vpiSimTime, 0, 1, 0.0 };
    s_vpi_error_info info;
    value.value.scalar = vpi0;
    (void)vpi_chk_error(&info);
    vpi_put_value(object, &value, flags == vpiNoDelay ? NULL : &delay, flags);
    vpi_printf("#c put %s rejected=%d\n", label,
        vpi_chk_error(&info) == vpiError);
}

static PLI_INT32 after_put(p_cb_data data)
{
    (void)data;
    value_of("u_seq_i after put", named("top.u_seq_i"));
    value_of("u_seq net after put", named("top.u_seq"));
    return 0;
}

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle top = named("top");
    vpiHandle gate;
    vpiHandle array;
    vpiHandle index;
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    const char* file;
    int unnamed = 0;
    vpiHandle iterator;
    vpiHandle object;
    (void)data;

    vpi_printf("#c counts primitive=%d gate=%d switch=%d udp=%d\n",
        count_of(vpiPrimitive, top), count_of(vpiGate, top),
        count_of(vpiSwitch, top), count_of(vpiUdp, top));
    vpi_printf("#c counts array=%d gate_array=%d switch_array=%d udp_array=%d\n",
        count_of(vpiPrimitiveArray, top), count_of(vpiGateArray, top),
        count_of(vpiSwitchArray, top), count_of(vpiUdpArray, top));
    iterator = vpi_iterate(vpiPrimitive, top);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        if (vpi_get_str(vpiName, object) == NULL) {
            ++unnamed;
            vpi_printf("#c unnamed type=%s def=%s size=%d value=",
                text(vpi_get_str(vpiType, object)),
                text(vpi_get_str(vpiDefName, object)),
                (int)vpi_get(vpiSize, object));
            {
                s_vpi_value value = { vpiBinStrVal, { 0 } };
                vpi_get_value(object, &value);
                vpi_printf("%s\n", text(value.value.str));
            }
        }
    }
    vpi_printf("#c unnamed count=%d\n", unnamed);

    // Gates.
    gate = named("top.g_and");
    show("g_and", gate);
    sized("g_and", gate);
    value_of("g_and", gate);
    terms("g_and", gate);
    vpi_printf("#c g_and parent=%s module=%s\n",
        text(vpi_get_str(vpiName, vpi_handle(vpiParent, gate))),
        text(vpi_get_str(vpiName, vpi_handle(vpiModule, gate))));
    file = vpi_get_str(vpiFile, gate);
    vpi_printf("#c g_and file_ok=%d index=%d\n",
        file != NULL && strstr(file, "foreign_vpi_primitives.sv") != NULL,
        vpi_handle(vpiIndex, gate) == NULL);
    show("scoped g_and", vpi_handle_by_name((PLI_BYTE8*)"g_and", top));
    show("gen g_or", named("top.gen.g_or"));
    value_of("gen g_or", named("top.gen.g_or"));
    gate = named("top.g_buf");
    show("g_buf", gate);
    sized("g_buf", gate);
    value_of("g_buf", gate);
    gate = named("top.g_pu");
    show("g_pu", gate);
    sized("g_pu", gate);
    value_of("g_pu", gate);

    // Switches.
    gate = named("top.g_nmos");
    show("g_nmos", gate);
    sized("g_nmos", gate);
    value_of("g_nmos", gate);
    gate = named("top.g_tran");
    show("g_tran", gate);
    value_of("g_tran", gate);
    terms("g_tran", gate);

    // An instance array and its elements.
    gate = named("top.ga[1]");
    show("ga[1]", gate);
    sized("ga[1]", gate);
    value_of("ga[1]", gate);
    terms("ga[1]", gate);
    index = vpi_handle(vpiIndex, gate);
    vpi_printf("#c ga[1] index=%d\n", int_of(index));
    show("ga[0]", named("top.ga[0]"));
    value_of("ga[0]", named("top.ga[0]"));
    array = vpi_handle(vpiPrimitiveArray, gate);
    vpi_printf("#c array type=%s name=%s full=%s left=%d right=%d elements=%d\n",
        array != NULL ? text(vpi_get_str(vpiType, array)) : "-",
        array != NULL ? text(vpi_get_str(vpiName, array)) : "-",
        array != NULL ? text(vpi_get_str(vpiFullName, array)) : "-",
        int_of(array != NULL ? vpi_handle(vpiLeftRange, array) : NULL),
        int_of(array != NULL ? vpi_handle(vpiRightRange, array) : NULL),
        count_of(vpiPrimitive, array));

    // UDP instances and definitions.
    gate = named("top.u_comb_i");
    show("u_comb_i", gate);
    sized("u_comb_i", gate);
    value_of("u_comb_i", gate);
    terms("u_comb_i", gate);
    definition("u_comb_i", gate);
    gate = named("top.u_seq_i");
    show("u_seq_i", gate);
    sized("u_seq_i", gate);
    value_of("u_seq_i", gate);
    terms("u_seq_i", gate);
    definition("u_seq_i", gate);
    vpi_printf("#c udp defns=%d\n", count_of(vpiUdpDefn, NULL));

    // Strength of scalars and terminals (IEEE 1800-2023 38.15).
    strength_of("w_and net", named("top.w_and"));
    strength_of("w_pull net", named("top.w_pull"));
    strength_of("d1 reg", named("top.d1"));
    strength_of("arr_a vector", named("top.arr_a"));
    strength_of("g_and term0", first_of(vpiPrimTerm, named("top.g_and")));

    // Prim terms among the drivers and loads of a net (IEEE 1800-2023
    // 37.16).
    vpi_printf("#c w_and drivers=%d loads=%d\n",
        prim_terms_of(vpiDriver, named("top.w_and")),
        prim_terms_of(vpiLoad, named("top.w_and")));
    vpi_printf("#c w_bufif drivers=%d loads=%d\n",
        prim_terms_of(vpiDriver, named("top.w_bufif")),
        prim_terms_of(vpiLoad, named("top.w_bufif")));
    vpi_printf("#c en drivers=%d loads=%d\n",
        prim_terms_of(vpiDriver, named("top.en")),
        prim_terms_of(vpiLoad, named("top.en")));
    object = first_of(vpiDriver, named("top.w_and"));
    vpi_printf("#c w_and first driver type=%s\n",
        object != NULL ? text(vpi_get_str(vpiType, object)) : "-");

    // Only a sequential UDP takes a value, and only without a delay.
    put("g_and", named("top.g_and"), vpiNoDelay);
    put("u_comb_i", named("top.u_comb_i"), vpiNoDelay);
    put("u_seq_i delayed", named("top.u_seq_i"), vpiInertialDelay);
    put("u_seq_i", named("top.u_seq_i"), vpiNoDelay);
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = after_put;
    callback.time = &time;
    vpi_register_cb(&callback);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 22, 0.0 };
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

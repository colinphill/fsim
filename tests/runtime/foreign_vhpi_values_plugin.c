// SPDX-License-Identifier: Apache-2.0
// VHPI foreign function results, access values and cycle counts conformance
// plug-in (IEEE 1076-2019 22.4, 22.5.5, 23.18). Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static char library_name[] = "vals";
static char nibble_name[] = "nibble";
static char grow_name[] = "grow";
static char text_name[] = "text";
static char pair_name[] = "pair";

static vhpiHandleT d3;

// Define an array result or element from characters (IEEE 1076-2019
// 22.5.5): the value structure holds the string and its terminator.
static int put_text(vhpiHandleT target, vhpiFormatT format, char* text)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = format;
    value.bufSize = (unsigned)strlen(text) + 1U;
    value.numElems = (int32_t)strlen(text);
    value.value.str = (vhpiCharT*)text;
    return vhpi_put_value(target, &value, vhpiDeposit);
}

// The execution function of an unconstrained result sets its size first.
static int put_size(vhpiHandleT target, int32_t elements)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiObjTypeVal;
    value.numElems = elements;
    return vhpi_put_value(target, &value, vhpiSizeConstraint);
}

static void report_exec(const char* name)
{
    printf("#c exec %s: errors=%d\n", name, vhpi_check_error(NULL) != 0);
}

static void exec_nibble(const vhpiCbDataT* data)
{
    char bits[] = "1010";
    (void)put_text(data->obj, vhpiBinStrVal, bits);
    report_exec("nibble");
}

static void exec_grow(const vhpiCbDataT* data)
{
    char bits[] = "11110000";
    (void)put_size(data->obj, 8);
    (void)put_text(data->obj, vhpiBinStrVal, bits);
    report_exec("grow");
}

static void exec_text(const vhpiCbDataT* data)
{
    char text[] = "hello";
    (void)put_size(data->obj, 5);
    (void)put_text(data->obj, vhpiStrVal, text);
    report_exec("text");
}

// A record result: its element names are reached from the call.
static void exec_pair(const vhpiCbDataT* data)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiSelectedNames, data->obj);
    vhpiHandleT element;
    char bits[] = "1001";
    vhpiValueT number;
    int index = 0;
    memset(&number, 0, sizeof number);
    number.format = vhpiIntVal;
    number.value.intg = 42;
    while (iterator != NULL && (element = vhpi_scan(iterator)) != NULL) {
        if (index == 0) {
            (void)put_text(element, vhpiBinStrVal, bits);
        } else {
            (void)vhpi_put_value(element, &number, vhpiDeposit);
        }
        ++index;
    }
    report_exec("pair");
}

static void register_function(char* name, void (*routine)(const vhpiCbDataT*))
{
    vhpiForeignDataT model;
    memset(&model, 0, sizeof model);
    model.kind = vhpiFuncF;
    model.libraryName = library_name;
    model.modelName = name;
    model.execf = routine;
    (void)vhpi_register_foreignf(&model);
}

// The delta cycles of the time step, the cycle in progress included, and the
// cycles of the simulation phase (IEEE 1076-2019 23.18).
static void on_d3(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    long cycles = 0;
    long total = 0;
    vhpiHandleT pointer = vhpi_handle_by_name("top.main.ptr", NULL);
    vhpiValueT value;
    (void)data;
    vhpi_get_time(&now, &cycles);
    vhpi_get_time(NULL, &total);
    printf("#c cycles at d3: high=%d low=%u delta cycles=%ld total>=5=%d\n",
        (int)now.high, (unsigned)now.low, cycles, total >= 5);
    memset(&value, 0, sizeof value);
    value.format = vhpiPtrVal;
    value.value.ptr = &value;
    (void)vhpi_get_value(pointer, &value);
    printf("#c ptr: error=%d nonnull=%d elems=%d\n",
        vhpi_check_error(NULL) != 0, value.value.ptr != NULL,
        (int)value.numElems);
    value.value.ptr = NULL;
    (void)vhpi_put_value(pointer, &value, vhpiDeposit);
    printf("#c ptr set null: error=%d", vhpi_check_error(NULL) != 0);
    value.value.ptr = &value;
    (void)vhpi_get_value(pointer, &value);
    printf(" null=%d\n", value.value.ptr == NULL);
}

static void show_bits(const char* label, const char* path)
{
    vhpiHandleT object = vhpi_handle_by_name(path, NULL);
    vhpiValueT value;
    char buffer[64];
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    (void)vhpi_get_value(object, &value);
    printf("#c %s=%s\n", label, buffer);
}

static void end_of_simulation(const vhpiCbDataT* data)
{
    vhpiHandleT text = vhpi_handle_by_name("top.txt", NULL);
    vhpiHandleT number = vhpi_handle_by_name("top.p.b", NULL);
    vhpiValueT value;
    char buffer[64];
    (void)data;
    show_bits("nib", "top.nib");
    show_bits("grown", "top.grown");
    memset(&value, 0, sizeof value);
    value.format = vhpiStrVal;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    (void)vhpi_get_value(text, &value);
    printf("#c txt=%s\n", buffer);
    printf("#c pair a=");
    {
        vhpiHandleT element = vhpi_handle_by_name("top.p.a", NULL);
        memset(&value, 0, sizeof value);
        value.format = vhpiBinStrVal;
        value.bufSize = sizeof buffer;
        value.value.str = (vhpiCharT*)buffer;
        (void)vhpi_get_value(element, &value);
        printf("%s", buffer);
    }
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    (void)vhpi_get_value(number, &value);
    printf(" b=%d\n", (int)value.value.intg);
    fflush(stdout);
}

static void start_of_simulation(const vhpiCbDataT* data)
{
    vhpiCbDataT change;
    (void)data;
    d3 = vhpi_handle_by_name("top.d3", NULL);
    memset(&change, 0, sizeof change);
    change.reason = vhpiCbValueChange;
    change.cb_rtn = on_d3;
    change.obj = d3;
    (void)vhpi_register_cb(&change, vhpiReturnCb);
}

static void register_cb_reason(
    int32_t reason, void (*routine)(const vhpiCbDataT*))
{
    vhpiCbDataT callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = reason;
    callback.cb_rtn = routine;
    (void)vhpi_register_cb(&callback, vhpiReturnCb);
}

static void startup(void)
{
    register_function(nibble_name, exec_nibble);
    register_function(grow_name, exec_grow);
    register_function(text_name, exec_text);
    register_function(pair_name, exec_pair);
    register_cb_reason(vhpiCbStartOfSimulation, start_of_simulation);
    register_cb_reason(vhpiCbEndOfSimulation, end_of_simulation);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

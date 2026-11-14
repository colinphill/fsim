// SPDX-License-Identifier: Apache-2.0
// VPI associative array word conformance plug-in (IEEE 1800-2023 37.3.8,
// 37.17 detail 28): words found by their key names, string keys written as
// string literals, word handles that stay valid while their keys exist, and
// cbValueChange on a word only when that key's value changes.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static vpiHandle word5;
static vpiHandle word9;

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static int value_of(vpiHandle object)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpi_get_value(object, &value);
    return (int)value.value.integer;
}

static void show(const char* label, vpiHandle object)
{
    char name[128];
    if (object == NULL) {
        vpi_printf("#c %s null\n", label);
        return;
    }
    snprintf(name, sizeof name, "%s", text(vpi_get_str(vpiName, object)));
    vpi_printf("#c %s name=%s full=%s value=%d\n", label, name,
        text(vpi_get_str(vpiFullName, object)), value_of(object));
}

static PLI_INT32 word_changed(p_cb_data data)
{
    vpi_printf("#c valuechange t=%u value=%d\n",
        (unsigned)data->time->low, (int)data->value->value.integer);
    return 0;
}

static void after(PLI_UINT32 when, PLI_INT32 (*routine)(p_cb_data))
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, when, 0.0 };
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = routine;
    callback.time = &time;
    vpi_register_cb(&callback);
}

static PLI_INT32 first_keys(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    s_vpi_value value = { vpiIntVal, { 0 } };
    (void)data;
    word5 = vpi_handle_by_name("top.aa[5]", NULL);
    word9 = vpi_handle_by_name("top.aa[9]", NULL);
    show("t3 aa[5]", word5);
    show("t3 aa[9]", word9);
    show("t3 sa[a.b]", vpi_handle_by_name("top.sa[\"a.b\"]", NULL));
    show("t3 sa[q\"t]", vpi_handle_by_name("top.sa[\"q\\\"t\"]", NULL));
    vpi_printf("#c t3 aa size=%d\n",
        (int)vpi_get(vpiSize, vpi_handle_by_name("top.aa", NULL)));
    memset(&callback, 0, sizeof callback);
    callback.reason = cbValueChange;
    callback.cb_rtn = word_changed;
    callback.obj = word5;
    callback.time = &time;
    callback.value = &value;
    if (vpi_register_cb(&callback) == NULL) {
        vpi_printf("#c register valuechange failed\n");
    }
    return 0;
}

static PLI_INT32 after_insert(p_cb_data data)
{
    vpiHandle found = vpi_handle_by_name("top.aa[5]", NULL);
    (void)data;
    show("t5 held aa[5]", word5);
    show("t5 held aa[9]", word9);
    show("t5 aa[2]", vpi_handle_by_name("top.aa[2]", NULL));
    vpi_printf("#c t5 lookup is held=%d\n",
        found != NULL && vpi_compare_objects(found, word5));
    return 0;
}

static PLI_INT32 after_delete(p_cb_data data)
{
    (void)data;
    show("t7 held aa[5]", word5);
    show("t7 aa[9]", vpi_handle_by_name("top.aa[9]", NULL));
    vpi_printf("#c t7 aa size=%d\n",
        (int)vpi_get(vpiSize, vpi_handle_by_name("top.aa", NULL)));
    return 0;
}

static PLI_INT32 after_change(p_cb_data data)
{
    (void)data;
    show("t9 held aa[5]", word5);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    after(3, first_keys);
    after(5, after_insert);
    after(7, after_delete);
    after(9, after_change);
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

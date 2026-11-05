// SPDX-License-Identifier: Apache-2.0
// Conformance plug-in for the startup restrictions (IEEE 1800-2023 36.10.2),
// the handle of a one-shot callback after it fires (38.8, 38.39), multiple
// indexes (38.20), scoped names (38.21), iterators (38.23, 38.40) and
// vpi_release_handle (38.38). Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

// What the startup routine learned; the host prints nothing that early.
static int startup_vlog_info;
static int startup_vlog_error;
static int startup_printf;
static int startup_printf_error;
static vpiHandle startup_after_delay;
static int startup_after_delay_error;
static vpiHandle startup_end_of_compile;
static int startup_end_of_compile_error;
static vpiHandle startup_systf;
static int startup_systf_error;

static vpiHandle first_callback;

static unsigned long long now(void)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpi_get_time(NULL, &time);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static const char* reason_name(PLI_INT32 reason)
{
    return reason == cbAfterDelay ? "cbAfterDelay" : "other";
}

static PLI_INT32 noop(PLI_BYTE8* user_data)
{
    (void)user_data;
    return 0;
}

static PLI_INT32 after_placeholder(p_cb_data data)
{
    (void)data;
    return 0;
}

static vpiHandle register_after(PLI_INT32 delay, PLI_INT32 (*routine)(p_cb_data),
    const char* label)
{
    s_cb_data data;
    s_vpi_time time;
    memset(&data, 0, sizeof data);
    memset(&time, 0, sizeof time);
    time.type = vpiSimTime;
    time.low = (PLI_UINT32)delay;
    data.reason = cbAfterDelay;
    data.cb_rtn = routine;
    data.time = &time;
    data.user_data = (PLI_BYTE8*)label;
    return vpi_register_cb(&data);
}

static PLI_INT32 first_fired(p_cb_data data)
{
    (void)data;
    vpi_printf("#c cb first fired at %llu\n", now());
    return 0;
}

static PLI_INT32 check(p_cb_data data)
{
    s_cb_data info;
    s_vpi_value value;
    (void)data;
    memset(&value, 0, sizeof value);
    value.format = vpiHexStrVal;
    vpi_get_value(vpi_handle_by_name("top.v", NULL), &value);
    vpi_printf("#c cb check at %llu v=%s\n", now(), value.value.str);
    memset(&info, 0, sizeof info);
    info.reason = -1;
    vpi_get_cb_info(first_callback, &info);
    vpi_printf("#c cb info of fired reason=%s user=%s\n",
        reason_name(info.reason), (const char*)info.user_data);
    {
        int first = vpi_release_handle(first_callback);
        int again = vpi_release_handle(first_callback);
        vpi_printf("#c cb release fired=%d again=%d\n", first, again);
    }
    memset(&info, 0, sizeof info);
    info.reason = -1;
    vpi_get_cb_info(first_callback, &info);
    vpi_printf("#c cb info after release reason=%d\n", (int)info.reason);
    return 0;
}

static void multi(vpiHandle object, const char* label, int count, PLI_INT32* indexes,
    int show_name)
{
    vpiHandle found = vpi_handle_by_multi_index(object, count, indexes);
    if (found == NULL) {
        vpi_printf("#c multi %s null\n", label);
        return;
    }
    if (show_name) {
        vpi_printf("#c multi %s name=%s size=%d\n", label,
            vpi_get_str(vpiFullName, found), (int)vpi_get(vpiSize, found));
    } else {
        vpi_printf("#c multi %s size=%d\n", label, (int)vpi_get(vpiSize, found));
    }
}

static void report_name(const char* label, vpiHandle found)
{
    vpi_printf("#c name %s %s\n", label, found != NULL ? "ok" : "null");
}

static PLI_INT32 end_of_compile(p_cb_data data)
{
    (void)data;
    vpi_printf("#c startup vlog_info=%d refused=%d\n", startup_vlog_info,
        startup_vlog_error != 0);
    vpi_printf("#c startup printf=%d refused=%d\n", startup_printf,
        startup_printf_error != 0);
    vpi_printf("#c startup after_delay=%s refused=%d\n",
        startup_after_delay == NULL ? "null" : "ok",
        startup_after_delay_error != 0);
    vpi_printf("#c startup end_of_compile=%s refused=%d\n",
        startup_end_of_compile == NULL ? "null" : "ok",
        startup_end_of_compile_error != 0);
    vpi_printf("#c startup systf=%s refused=%d\n",
        startup_systf == NULL ? "null" : "ok", startup_systf_error != 0);
    vpi_printf("#c end of compile\n");
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle iterator;
    vpiHandle object;
    int count = 0;
    s_cb_data info;
    s_vpi_value put;
    s_vpi_time delay;
    vpiHandle event;
    (void)data;

    // vpi_handle_by_multi_index: one index per dimension, leftmost first,
    // then an optional bit-select index (38.20).
    {
        vpiHandle mem = vpi_handle_by_name("top.mem", NULL);
        vpiHandle pk = vpi_handle_by_name("top.pk", NULL);
        PLI_INT32 a[] = { 1, 2 };
        PLI_INT32 b[] = { 1, 2, 3 };
        PLI_INT32 c[] = { 2, 0 };
        PLI_INT32 d[] = { 1, 3 };
        PLI_INT32 e[] = { 1, 2, 8 };
        PLI_INT32 f[] = { 1, 2, 3, 0 };
        PLI_INT32 g[] = { 2 };
        PLI_INT32 h[] = { 2, 5 };
        PLI_INT32 i[] = { 4 };
        multi(mem, "mem[1][2]", 2, a, 1);
        multi(mem, "mem[1][2][3]", 3, b, 1);
        multi(mem, "mem[2][0]", 2, c, 1);
        multi(mem, "mem[1][3]", 2, d, 1);
        multi(mem, "mem[1][2][8]", 3, e, 1);
        multi(mem, "mem[1][2][3][0]", 4, f, 1);
        multi(mem, "mem[]", 0, a, 1);
        multi(pk, "pk[2]", 1, g, 0);
        multi(pk, "pk[2][5]", 2, h, 0);
        multi(pk, "pk[4]", 1, i, 0);
    }

    // vpi_handle_by_name searches within a given scope only (38.21).
    report_name("v in top", vpi_handle_by_name((PLI_BYTE8*)"v", top));
    report_name("c.w in top", vpi_handle_by_name((PLI_BYTE8*)"c.w", top));
    {
        vpiHandle found = vpi_handle_by_name((PLI_BYTE8*)"top.v", top);
        vpi_printf("#c name top.v in top %s\n", found != NULL ? "ok" : "null");
        found = vpi_handle_by_name((PLI_BYTE8*)"w", top);
        vpi_printf("#c name w in top %s\n", found != NULL ? "ok" : "null");
        found = vpi_handle_by_name((PLI_BYTE8*)"top.v", NULL);
        vpi_printf("#c name top.v from root %s\n", found != NULL ? "ok" : "null");
    }

    // Iterators: the reference object, the end of a scan and release (38.23,
    // 38.40, 38.38).
    iterator = vpi_iterate(vpiReg, top);
    vpi_printf("#c iterator use=%d\n",
        (int)vpi_compare_objects(vpi_handle(vpiUse, iterator), top));
    while ((object = vpi_scan(iterator)) != NULL) {
        ++count;
    }
    vpi_printf("#c iterator reg count=%d\n", count);
    vpi_printf("#c iterator scan after end %s\n",
        vpi_scan(iterator) == NULL ? "null" : "object");
    vpi_printf("#c iterator release after end=%d\n",
        (int)vpi_release_handle(iterator));
    vpi_printf("#c iterator nets %s\n",
        vpi_iterate(vpiNet, top) == NULL ? "null" : "object");
    iterator = vpi_iterate(vpiReg, top);
    (void)vpi_scan(iterator);
    vpi_printf("#c iterator early release=%d\n",
        (int)vpi_release_handle(iterator));
    vpi_printf("#c iterator second release=%d\n",
        (int)vpi_release_handle(iterator));

    // A one-shot callback's handle stays valid after it fires, until it is
    // released (38.8, 38.39).
    first_callback = register_after(2, first_fired, "first");
    memset(&info, 0, sizeof info);
    info.reason = -1;
    vpi_get_cb_info(first_callback, &info);
    vpi_printf("#c cb before reason=%s user=%s\n", reason_name(info.reason),
        (const char*)info.user_data);

    // Releasing a vpiSchedEvent handle does not affect the event (38.34).
    memset(&put, 0, sizeof put);
    put.format = vpiHexStrVal;
    put.value.str = (PLI_BYTE8*)"5a";
    memset(&delay, 0, sizeof delay);
    delay.type = vpiSimTime;
    delay.low = 3U;
    event = vpi_put_value(vpi_handle_by_name("top.v", NULL), &put, &delay,
        vpiTransportDelay | vpiReturnEvent);
    {
        int scheduled = event != NULL ? (int)vpi_get(vpiScheduled, event) : -1;
        int released = vpi_release_handle(event);
        int again = vpi_release_handle(event);
        vpi_printf("#c cb event scheduled=%d release=%d again=%d\n", scheduled,
            released, again);
    }
    (void)register_after(4, check, "check");
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    s_vpi_vlog_info information;
    s_vpi_systf_data systf;

    // Only vpi_register_systf and vpi_register_cb for the six reasons can be
    // called here (36.10.2); fsim also allows vpi_printf (an empty string,
    // so the elaboration pass prints nothing).
    memset(&information, 0, sizeof information);
    startup_vlog_info = (int)vpi_get_vlog_info(&information);
    startup_vlog_error = (int)vpi_chk_error(NULL);
    startup_printf = (int)vpi_printf("%s", "");
    startup_printf_error = (int)vpi_chk_error(NULL);

    startup_after_delay = register_after(1, after_placeholder, "x");
    startup_after_delay_error = (int)vpi_chk_error(NULL);

    memset(&data, 0, sizeof data);
    data.reason = cbEndOfCompile;
    data.cb_rtn = end_of_compile;
    startup_end_of_compile = vpi_register_cb(&data);
    startup_end_of_compile_error = (int)vpi_chk_error(NULL);

    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start_of_simulation;
    (void)vpi_register_cb(&data);

    memset(&systf, 0, sizeof systf);
    systf.type = vpiSysTask;
    systf.tfname = "$life_unused";
    systf.calltf = noop;
    startup_systf = vpi_register_systf(&systf);
    startup_systf_error = (int)vpi_chk_error(NULL);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

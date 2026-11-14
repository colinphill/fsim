// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 delay and pulse routine conformance plug-in (IEEE 1364-2001 22.8,
// Clause 23, Tables 115-119 and 125-134): acc_fetch_delays,
// acc_replace_delays, acc_append_delays, acc_fetch_pulsere,
// acc_replace_pulsere, acc_append_pulsere, acc_set_pulsere and
// acc_fetch_delay_mode through a veriusertfs table. Lines starting with "#c "
// form the checked trace.
#include "fsim/runtime/acc_user.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static void config(PLI_INT32 item, const char* value)
{
    (void)acc_configure(item, (PLI_BYTE8*)value);
}

static void show_array(const char* label, const double* values, int count)
{
    int index;
    vpi_printf("#c %s", label);
    for (index = 0; index < count; ++index) {
        vpi_printf(" %.3f", values[index]);
    }
    vpi_printf("\n");
}

static void flag(const char* label, PLI_INT32 result)
{
    vpi_printf("#c %s ok=%d error=%d\n", label, (int)result, (int)acc_error_flag);
}

// The data path of an edge-sensitive module path has one input, the data
// source, and one output, the path's destination; a path without a data
// source has none (IEEE 1364-2001 23.42).
static void datapaths(void)
{
    handle const edge_path = acc_handle_modpath(acc_handle_object("top.pe"), "clk", "q");
    handle const plain_path = acc_handle_modpath(acc_handle_object("top.p"), "a", "w");
    handle const data = edge_path != NULL ? acc_handle_datapath(edge_path) : NULL;
    handle const input = data != NULL ? acc_next_input(data, NULL) : NULL;
    handle const output = data != NULL ? acc_next_output(data, NULL) : NULL;
    // Both names stay valid in one call: ACC strings take the next free
    // place in the shared string buffer (IEEE 1364-2001 22.9).
    vpi_printf("#c datapath type=%d fulltype=%d input=%s output=%s more=%d/%d\n",
        data != NULL ? (int)acc_fetch_type(data) : 0,
        data != NULL ? (int)acc_fetch_fulltype(data) : 0,
        input != NULL ? acc_fetch_fullname(acc_handle_conn(input)) : "-",
        output != NULL ? acc_fetch_fullname(acc_handle_conn(output)) : "-",
        input != NULL && acc_next_input(data, input) != NULL,
        output != NULL && acc_next_output(data, output) != NULL);
    vpi_printf("#c datapath of a plain path %s\n",
        plain_path != NULL && acc_handle_datapath(plain_path) == NULL ? "null" : "set");
}

static PLI_INT32 get_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    handle top;
    handle and_gate;
    handle buf_gate;
    handle udp;
    handle path;
    handle check;
    double d[12];
    double array[18];
    (void)data;
    (void)reason;
    (void)acc_initialize();
    top = acc_handle_object("top");
    and_gate = acc_handle_object("top.g_and");
    buf_gate = acc_handle_object("top.g_buf");
    udp = acc_handle_object("top.u_inv");
    path = acc_handle_modpath(acc_handle_object("top.p"), "a", "w");
    check = acc_handle_tchk(acc_handle_object("top.p"), accSetup, "d", accNoedge, "clk", accPosedge);

    vpi_printf("#c delay mode %d\n", (int)acc_fetch_delay_mode(top));
    // 2-state primitives: rise and fall; 3-state: rise, fall and toZ.
    flag("g_and fetch", acc_fetch_delays(and_gate, &d[0], &d[1]));
    show_array("g_and delays", d, 2);
    flag("g_buf fetch", acc_fetch_delays(buf_gate, &d[0], &d[1], &d[2]));
    show_array("g_buf delays", d, 3);
    flag("u_inv fetch", acc_fetch_delays(udp, &d[0], &d[1]));
    show_array("u_inv delays", d, 2);
    // A calculated toZ leaves two arguments (Table 117).
    config(accToHiZDelay, "max");
    flag("g_buf fetch 2", acc_fetch_delays(buf_gate, &d[0], &d[1]));
    show_array("g_buf delays 2", d, 2);
    config(accToHiZDelay, "from_user");
    // min:typ:max arrays hold nine values for every primitive (Table 118).
    config(accMinTypMaxDelays, "true");
    flag("g_and fetch mtm", acc_fetch_delays(and_gate, array));
    show_array("g_and mtm", array, 9);
    flag("g_buf fetch mtm", acc_fetch_delays(buf_gate, array));
    show_array("g_buf mtm", array, 9);
    config(accMinTypMaxDelays, "false");
    // Module paths: six delays by default.
    flag("path fetch 6", acc_fetch_delays(path, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5]));
    show_array("path delays 6", d, 6);
    config(accPathDelayCount, "2");
    flag("path fetch 2", acc_fetch_delays(path, &d[0], &d[1]));
    show_array("path delays 2", d, 2);
    config(accPathDelayCount, "12");
    flag("path fetch 12", acc_fetch_delays(path, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5],
        &d[6], &d[7], &d[8], &d[9], &d[10], &d[11]));
    show_array("path delays 12", d, 12);
    config(accPathDelayCount, "2");
    config(accMinTypMaxDelays, "true");
    flag("path fetch mtm", acc_fetch_delays(path, array));
    show_array("path mtm", array, 6);
    config(accMinTypMaxDelays, "false");
    flag("path pulsere", acc_fetch_pulsere(path, &d[0], &d[1], &d[2], &d[3]));
    show_array("path pulsere", d, 4);
    flag("setup fetch", acc_fetch_delays(check, &d[0]));
    show_array("setup limit", d, 1);
    flag("fetch on module", acc_fetch_delays(top, &d[0]));
    datapaths();
    acc_close();
    return 0;
}

static PLI_INT32 put_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    handle and_gate;
    handle buf_gate;
    handle udp;
    handle path;
    handle check;
    double d[12];
    static const double nine[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    (void)data;
    (void)reason;
    (void)acc_initialize();
    and_gate = acc_handle_object("top.g_and");
    buf_gate = acc_handle_object("top.g_buf");
    udp = acc_handle_object("top.u_inv");
    path = acc_handle_modpath(acc_handle_object("top.p"), "a", "w");
    check = acc_handle_tchk(acc_handle_object("top.p"), accSetup, "d", accNoedge, "clk", accPosedge);

    flag("g_and replace", acc_replace_delays(and_gate, 10.0, 20.0));
    flag("g_and fetch", acc_fetch_delays(and_gate, &d[0], &d[1]));
    show_array("g_and delays", d, 2);
    // Two delays and a calculated toZ: the average of the new rise and fall.
    config(accToHiZDelay, "average");
    flag("g_buf append", acc_append_delays(buf_gate, 1.0, 1.0));
    config(accToHiZDelay, "from_user");
    flag("g_buf fetch", acc_fetch_delays(buf_gate, &d[0], &d[1], &d[2]));
    show_array("g_buf delays", d, 3);
    // A min:typ:max replacement applies its typical values.
    config(accMinTypMaxDelays, "true");
    flag("u_inv replace mtm", acc_replace_delays(udp, nine));
    config(accMinTypMaxDelays, "false");
    flag("u_inv fetch", acc_fetch_delays(udp, &d[0], &d[1]));
    show_array("u_inv delays", d, 2);
    config(accPathDelayCount, "2");
    flag("path replace", acc_replace_delays(path, 9.0, 11.0));
    flag("path fetch", acc_fetch_delays(path, &d[0], &d[1]));
    show_array("path delays", d, 2);
    flag("path replace pulsere", acc_replace_pulsere(path, 3.0, 4.0, 5.0, 6.0));
    flag("path fetch pulsere", acc_fetch_pulsere(path, &d[0], &d[1], &d[2], &d[3]));
    show_array("path pulsere", d, 4);
    flag("path set pulsere", acc_set_pulsere(path, 50.0, 100.0));
    flag("path fetch pulsere", acc_fetch_pulsere(path, &d[0], &d[1], &d[2], &d[3]));
    show_array("path pulsere", d, 4);
    // A limit above the delay is truncated to it (23.2).
    flag("path append pulsere", acc_append_pulsere(path, 1.0, 1.0, 1.0, 1.0));
    flag("path fetch pulsere", acc_fetch_pulsere(path, &d[0], &d[1], &d[2], &d[3]));
    show_array("path pulsere", d, 4);
    flag("setup replace", acc_replace_delays(check, 12.0));
    flag("setup fetch", acc_fetch_delays(check, &d[0]));
    show_array("setup limit", d, 1);
    flag("negative replace", acc_replace_delays(and_gate, -1.0, 2.0));
    flag("pulsere on gate", acc_replace_pulsere(and_gate, 1.0, 2.0));
    acc_close();
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, get_calltf, NULL, "$acc_delays_get", 0, NULL, NULL, { 0 } },
    { usertask, 0, NULL, NULL, put_calltf, NULL, "$acc_delays_put", 0, NULL, NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void register_table(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_table, 0 };

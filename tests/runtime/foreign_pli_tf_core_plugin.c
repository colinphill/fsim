// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 TF routine conformance plug-in (IEEE 1364-2001 clause 25): the
// 64-bit arithmetic and conversion routines, mc_scan_plusargs, the return
// codes of the scheduling routines, argument reads (X and Z read as 0),
// puts onto real variables, tf_exprinfo and tf_nodeinfo of real arguments,
// tf_text with tf_message, and the time services. The task registers through
// veriusertfs_register_table from a startup routine. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/veriuser.h"

#include <stdio.h>
#include <string.h>

static PLI_BYTE8 work_marker;

static const char* text(const char* value)
{
    return value != NULL ? value : "(null)";
}

static void long_arithmetic(void)
{
    PLI_INT32 low = (PLI_INT32)0xffffffffu;
    PLI_INT32 high = 0;
    double real = 0.0;
    PLI_INT32 real_low = 0;
    PLI_INT32 real_high = 0;
    PLI_INT32 rc;
    /* 0xffffffff + 1 carries into the high word (25.4). */
    rc = tf_add_long(&low, &high, 1, 0);
    io_printf("#c add rc=%d hi=%08x lo=%08x\n", rc, (unsigned)high,
        (unsigned)low);
    low = 0;
    high = 1;
    rc = tf_subtract_long(&low, &high, 1, 0);
    io_printf("#c subtract rc=%d hi=%08x lo=%08x\n", rc, (unsigned)high,
        (unsigned)low);
    low = 0x10000;
    high = 0;
    tf_multiply_long(&low, &high, 0x10000, 0);
    io_printf("#c multiply hi=%08x lo=%08x\n", (unsigned)high, (unsigned)low);
    low = 0;
    high = 1;
    tf_divide_long(&low, &high, 16, 0);
    io_printf("#c divide hi=%08x lo=%08x\n", (unsigned)high, (unsigned)low);
    /* Two's complement operands (25.10): -100 / 7 truncates to -14. */
    low = (PLI_INT32)0xffffff9cu;
    high = (PLI_INT32)0xffffffffu;
    tf_divide_long(&low, &high, 7, 0);
    io_printf("#c divide signed hi=%08x lo=%08x\n", (unsigned)high,
        (unsigned)low);
    io_printf("#c compare %d %d %d\n", tf_compare_long(1, 0, 0, 1),
        tf_compare_long(5, 0, 5, 0), tf_compare_long(0, 2, 0, 1));
    tf_long_to_real(0, 1, &real);
    io_printf("#c long_to_real %.1f\n", real);
    tf_real_to_long(-2.5, &real_low, &real_high);
    io_printf("#c real_to_long hi=%08x lo=%08x\n", (unsigned)real_high,
        (unsigned)real_low);
    tf_real_to_long(2.4, &real_low, &real_high);
    io_printf("#c real_to_long hi=%08x lo=%08x\n", (unsigned)real_high,
        (unsigned)real_low);
    io_printf("#c longtime_tostr %s\n",
        text(tf_longtime_tostr((PLI_INT32)0xffffffffu, 1)));
}

static void plusargs(void)
{
    /* The simulate command line carries +tfcore=abc (25.3). */
    io_printf("#c plusarg tfcore -> %s\n",
        text(mc_scan_plusargs((PLI_BYTE8*)"tfcore")));
    io_printf("#c plusarg tfcore= -> %s\n",
        text(mc_scan_plusargs((PLI_BYTE8*)"tfcore=")));
    io_printf("#c plusarg tfcore=abc -> [%s]\n",
        text(mc_scan_plusargs((PLI_BYTE8*)"tfcore=abc")));
    io_printf("#c plusarg TFCORE -> %s\n",
        text(mc_scan_plusargs((PLI_BYTE8*)"TFCORE")));
    io_printf("#c plusarg absent -> %s\n",
        text(mc_scan_plusargs((PLI_BYTE8*)"absent")));
}

static void return_codes(void)
{
    PLI_INT32 on = tf_asynchon();
    PLI_INT32 off = tf_asynchoff();
    PLI_INT32 set = tf_setdelay(1);
    PLI_INT32 negative = tf_setdelay(-1);
    PLI_INT32 set_long = tf_setlongdelay(1, 0);
    PLI_INT32 set_real = tf_setrealdelay(1.5);
    PLI_INT32 cleared = tf_clearalldelays();
    PLI_INT32 stored = tf_setworkarea(&work_marker);
    PLI_INT32 same = tf_getworkarea() == &work_marker;
    io_printf("#c asynchon=%d asynchoff=%d\n", on, off);
    io_printf("#c setdelay=%d negative=%d long=%d real=%d\n", set, negative,
        set_long, set_real);
    io_printf("#c clearalldelays=%d\n", cleared);
    io_printf("#c setworkarea=%d same=%d\n", stored, same);
}

static void argument_reads(void)
{
    PLI_INT32 high = 0;
    PLI_INT32 low = 0;
    s_tfexprinfo expression;
    s_tfnodeinfo node;
    io_printf("#c nump=%d\n", tf_nump());
    /* An X or Z bit reads as 0 (24.3.1, 25.18, 25.21, 25.23). */
    io_printf("#c getp x=%d bits=%s\n", tf_getp(1),
        text(tf_strgetp(1, 'b')));
    low = tf_getlongp(&high, 2);
    io_printf("#c getlongp hi=%08x lo=%08x\n", (unsigned)high,
        (unsigned)low);
    io_printf("#c getrealp wide=%.1f\n", tf_getrealp(2));
    io_printf("#c real getp=%d getrealp=%.1f typep=%d\n", tf_getp(3),
        tf_getrealp(3), tf_typep(3));
    io_printf("#c cstring vector=%s string=%s real=%s\n",
        text(tf_getcstringp(6)), text(tf_getcstringp(7)),
        text(tf_getcstringp(3)));
    /* A real argument has no vector groups or bits (25.15, 25.35). */
    memset(&expression, 0, sizeof expression);
    if (tf_exprinfo(3, &expression) == &expression) {
        io_printf("#c exprinfo real type=%d ngroups=%d size=%d value=%.1f\n",
            (int)expression.expr_type, expression.expr_ngroups,
            expression.expr_vec_size, expression.real_value);
    }
    memset(&node, 0, sizeof node);
    if (tf_nodeinfo(4, &node) == &node) {
        io_printf("#c nodeinfo real type=%d ngroups=%d size=%d\n",
            (int)node.node_type, node.node_ngroups, node.node_vec_size);
    }
    if (tf_nodeinfo(1, &node) == &node) {
        io_printf("#c nodeinfo reg type=%d ngroups=%d size=%d\n",
            (int)node.node_type, node.node_ngroups, node.node_vec_size);
    }
    /* A scalar net reports its strength (25.35, Table 193). */
    if (tf_nodeinfo(8, &node) == &node && node.node_value.strengthval_p != NULL) {
        io_printf("#c nodeinfo scalar type=%d strength0=%d strength1=%d\n",
            (int)node.node_type, node.node_value.strengthval_p->strength0,
            node.node_value.strengthval_p->strength1);
    }
}

static void argument_writes(void)
{
    /* An integer written to a real variable converts (24.3.7). */
    io_printf("#c putp real rc=%d\n", tf_putp(4, 5));
    io_printf("#c putlongp real rc=%d\n", tf_putlongp(5, 0, 1));
}

static void time_services(void)
{
    PLI_INT32 low = 0;
    PLI_INT32 high = 0;
    PLI_INT32 code;
    /* Not in a read-only synchronize call: code 2, the current time (25.20). */
    code = tf_getnextlongtime(&low, &high);
    io_printf("#c nextlongtime code=%d hi=%d lo=%d\n", code, high, low);
    io_printf("#c time=%d long=%d real=%.1f str=%s\n", tf_gettime(),
        tf_getlongtime(&high), tf_getrealtime(), text(tf_strgettime()));
    io_printf("#c unit=%d precision=%d simulation=%d,%d\n", tf_gettimeunit(),
        tf_gettimeprecision(), tf_igettimeunit(NULL),
        tf_igettimeprecision(NULL));
}

static void stored_text(void)
{
    /* tf_message displays the text stored by tf_text first (25.60), in a
       format the product defines (25.31), so the trace keeps only the
       return codes. */
    PLI_INT32 stored = tf_text("stored ");
    PLI_INT32 shown = tf_message(ERR_MESSAGE, "TF", "T1", "joined %d\n", 5);
    io_printf("#c text rc=%d\n", stored);
    io_printf("#c message rc=%d\n", shown);
}

static PLI_INT32 calltf_core(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    long_arithmetic();
    plusargs();
    return_codes();
    argument_reads();
    argument_writes();
    time_services();
    stored_text();
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, calltf_core, NULL, "$pli_tf_core", 0, NULL, NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void startup(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 TF routine conformance plug-in (IEEE 1364-2001 clause 25), argument
// description: tf_strgetp of vectors of any width, tf_typep, tf_sizep and
// tf_nump over every argument class (null arguments counted), tf_nodeinfo
// node classes, bounds, memories and scalar net strength, tf_exprinfo
// selects, the module's time unit and precision against the simulation's,
// and reason_paramdrc. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/veriuser.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "(null)";
}

static void show_string(const char* label, PLI_INT32 narg, PLI_INT32 format)
{
    /* The result is one buffer, so each is printed before the next call. */
    io_printf("#c strgetp %s %c=%s\n", label, (char)format,
        text(tf_strgetp(narg, format)));
}

static void strings(void)
{
    /* The decimal form has no width limit, and negative signed vectors keep
       their sign (25.53). */
    show_string("wide", 1, 'd');
    show_string("signed", 2, 'd');
    /* x or z in every bit gives lower case, in some bits upper case. */
    show_string("mixed", 3, 'd');
    show_string("allx", 4, 'd');
    show_string("allz", 5, 'd');
    show_string("somez", 6, 'd');
    /* A string argument returns its text whatever the format. */
    show_string("string", 7, 'b');
    show_string("integer", 9, 'd');
    show_string("time", 10, 'd');
}

static void classes(void)
{
    PLI_INT32 narg;
    io_printf("#c nump=%d\n", tf_nump());
    for (narg = 1; narg <= tf_nump(); ++narg) {
        io_printf("#c arg %d type=%d size=%d\n", (int)narg, (int)tf_typep(narg),
            (int)tf_sizep(narg));
    }
    /* A null argument is a null node; an argument past the last has no type
       (25.35, 25.61). */
    io_printf("#c past typep=%d sizep=%d\n", (int)tf_typep(99),
        (int)tf_sizep(99));
}

static void node(const char* label, PLI_INT32 narg)
{
    s_tfnodeinfo info;
    memset(&info, 0, sizeof info);
    if (tf_nodeinfo(narg, &info) != &info) {
        io_printf("#c node %s error\n", label);
        return;
    }
    io_printf("#c node %s type=%d ngroups=%d size=%d sign=%d ms=%d ls=%d\n",
        label, (int)info.node_type, (int)info.node_ngroups,
        (int)info.node_vec_size, (int)info.node_sign, (int)info.node_ms_index,
        (int)info.node_ls_index);
}

static void nodes(void)
{
    s_tfnodeinfo info;
    unsigned index;
    node("reg", 1);
    node("integer", 9);
    node("time", 10);
    node("scalar net", 12);
    node("vector net", 13);
    node("word", 14);
    node("part select", 16);
    node("null", 15);
    /* A scalar net reports the strength of its driver (25.35, 7.10). */
    memset(&info, 0, sizeof info);
    if (tf_nodeinfo(12, &info) == &info && info.node_value.strengthval_p) {
        io_printf("#c node strength0=%d strength1=%d\n",
            (int)info.node_value.strengthval_p->strength0,
            (int)info.node_value.strengthval_p->strength1);
    }
    /* A memory gives every element, its aval bytes then its bval bytes
       (Figure 169). */
    memset(&info, 0, sizeof info);
    if (tf_nodeinfo(11, &info) == &info && info.node_value.memoryval_p) {
        io_printf("#c node memory type=%d groupbytes=%d size=%d elements=%d\n",
            (int)info.node_type, (int)info.node_ngroups,
            (int)info.node_vec_size, (int)info.node_mem_size);
        for (index = 0; index < (unsigned)info.node_mem_size; ++index) {
            const unsigned char* element = (const unsigned char*)
                info.node_value.memoryval_p + index * 2u * (unsigned)info.node_ngroups;
            io_printf("#c   element %u aval=%02x bval=%02x\n", index,
                (unsigned)element[0], (unsigned)element[info.node_ngroups]);
        }
    }
}

static void selects(void)
{
    s_tfexprinfo info;
    PLI_INT32 narg;
    static const PLI_INT32 arguments[] = { 14, 16, 17, 8 };
    for (narg = 0; narg < 4; ++narg) {
        memset(&info, 0, sizeof info);
        if (tf_exprinfo(arguments[narg], &info) == &info) {
            io_printf("#c expr %d type=%d ngroups=%d size=%d lhs=%d rhs=%d\n",
                (int)arguments[narg], (int)info.expr_type,
                (int)info.expr_ngroups, (int)info.expr_vec_size,
                (int)info.expr_lhs_select, (int)info.expr_rhs_select);
        }
    }
}

static PLI_INT32 calltf_args(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    strings();
    classes();
    nodes();
    selects();
    return 0;
}

/* The module's own unit and precision; a null instance is the simulation's
   (25.26, 25.27). */
static PLI_INT32 calltf_scales(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    io_printf("#c scales %s unit=%d precision=%d simulation=%d,%d\n",
        text(tf_mipname()), (int)tf_gettimeunit(), (int)tf_gettimeprecision(),
        (int)tf_igettimeunit(NULL), (int)tf_igettimeprecision(NULL));
    io_printf("#c scales time=%d string=%s\n", (int)tf_gettime(),
        text(tf_strgettime()));
    return 0;
}

static PLI_INT32 drivers;

static PLI_INT32 calltf_drc(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    io_printf("#c drc asynchon=%d\n", tf_asynchon());
    return 0;
}

/* A change of the value of a driver of a net argument calls misctf with
   reason_paramdrc and the argument number (21.3.2, 21.3.3, Table 88). */
static PLI_INT32 misctf_drc(PLI_INT32 data, PLI_INT32 reason, PLI_INT32 paramvc)
{
    (void)data;
    if (reason == reason_paramdrc) {
        ++drivers;
        io_printf("#c paramdrc change=%d argument=%d time=%d\n", (int)drivers,
            (int)paramvc, (int)tf_gettime());
    }
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, calltf_args, NULL, "$pli_tf_args", 0, NULL, NULL,
        { 0 } },
    { usertask, 0, NULL, NULL, calltf_scales, NULL, "$pli_tf_scales", 0, NULL,
        NULL, { 0 } },
    { usertask, 0, NULL, NULL, calltf_drc, misctf_drc, "$pli_tf_drc", 0, NULL,
        NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void startup(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

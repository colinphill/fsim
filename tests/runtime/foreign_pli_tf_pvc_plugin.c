// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 TF routine conformance plug-in (IEEE 1364-2001 clause 25): the
// parameter value change flags (tf_asynchon, tf_copypvc_flag,
// tf_movepvc_flag, tf_testpvc_flag, tf_getpchange) and the delayed string
// puts (tf_strdelputp, tf_strlongdelputp, tf_strrealdelputp and the
// instance forms). $pli_tf_pvc watches its two arguments; $pli_tf_put writes
// its two arguments later by each delay type. Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/veriuser.h"

#include <stdio.h>

static PLI_INT32 changes;

static PLI_INT32 calltf_pvc(PLI_INT32 data, PLI_INT32 reason)
{
    PLI_INT32 before;
    PLI_INT32 copied;
    (void)data;
    (void)reason;
    /* No flag is set before tf_asynchon (25.33). */
    before = tf_testpvc_flag(-1);
    copied = tf_copypvc_flag(-1);
    io_printf("#c pvc before test=%d copy=%d asynchon=%d\n", before, copied,
        tf_asynchon());
    return 0;
}

static void report_flags(void)
{
    io_printf("#c   test all=%d 1=%d 2=%d 0=%d 3=%d -2=%d\n",
        tf_testpvc_flag(-1), tf_testpvc_flag(1), tf_testpvc_flag(2),
        tf_testpvc_flag(0), tf_testpvc_flag(3), tf_testpvc_flag(-2));
    io_printf("#c   pchange after 0=%d 1=%d 2=%d\n", tf_getpchange(0),
        tf_getpchange(1), tf_getpchange(2));
}

/* The reason_paramvc call passes the number of the changed argument (25.6). */
static PLI_INT32 misctf_pvc(PLI_INT32 data, PLI_INT32 reason, PLI_INT32 paramvc)
{
    (void)data;
    if (reason != reason_paramvc) {
        return 0;
    }
    ++changes;
    io_printf("#c paramvc change=%d argument=%d\n", changes, paramvc);
    if (changes == 1) {
        /* A copy leaves the current flag set (25.9). */
        io_printf("#c   copy all=%d\n", tf_copypvc_flag(-1));
        report_flags();
    } else if (changes == 2) {
        /* Argument 1 is still current; moving only argument 2 clears it. */
        io_printf("#c   move 2=%d\n", tf_movepvc_flag(2));
        report_flags();
    } else if (changes == 3) {
        io_printf("#c   move all=%d\n", tf_imovepvc_flag(-1, tf_getinstance()));
        report_flags();
    } else if (changes == 4) {
        /* Argument 1 was moved: its flag is clear (25.33). */
        io_printf("#c   move 1=%d copy 2=%d\n", tf_movepvc_flag(1),
            tf_icopypvc_flag(2, tf_getinstance()));
        io_printf("#c   itest 2=%d igetpchange 0=%d null instance=%d\n",
            tf_itestpvc_flag(2, tf_getinstance()),
            tf_igetpchange(0, tf_getinstance()),
            tf_itestpvc_flag(2, NULL));
        /* No later change calls misctf (25.5). */
        io_printf("#c   asynchoff=%d\n", tf_asynchoff());
    }
    return 0;
}

static PLI_INT32 calltf_put(PLI_INT32 data, PLI_INT32 reason)
{
    PLI_BYTE8* instance = tf_getinstance();
    (void)data;
    (void)reason;
    /* o becomes 5a at 2: the inertial put removes nothing yet (25.52). */
    io_printf("#c put inertial=%d\n",
        tf_strdelputp(1, 8, 'h', (PLI_BYTE8*)"5a", 2, 0));
    /* A pure transport put removes nothing; ff at 9 is later removed by the
       modified transport put at 6, which removes the later events. */
    io_printf("#c put pure=%d\n",
        tf_istrdelputp(1, 8, 'H', (PLI_BYTE8*)"ff", 9, 2, instance));
    io_printf("#c put modified=%d\n",
        tf_strdelputp(1, 8, 'h', (PLI_BYTE8*)"a5", 6, 1));
    /* 64-bit and real delays (25.55, 25.56); p changes at 3, 4 and 15. */
    io_printf("#c put long=%d\n",
        tf_strlongdelputp(2, 8, 'b', (PLI_BYTE8*)"00001111", 3, 0, 2));
    io_printf("#c put real=%d\n",
        tf_istrrealdelputp(2, 8, 'd', (PLI_BYTE8*)"200", 4.0, 1, instance));
    io_printf("#c put long instance=%d\n",
        tf_istrlongdelputp(2, 8, 'o', (PLI_BYTE8*)"17", 15, 0, 2, instance));
    /* Argument 0, an unknown argument, a bad format, a negative delay, a bad
       delay type, an empty value size and another instance are errors. */
    io_printf("#c put errors zero=%d far=%d format=%d negative=%d type=%d "
              "bits=%d other=%d\n",
        tf_strdelputp(0, 8, 'h', (PLI_BYTE8*)"1", 1, 0),
        tf_strdelputp(9, 8, 'h', (PLI_BYTE8*)"1", 1, 0),
        tf_strdelputp(1, 8, 'x', (PLI_BYTE8*)"1", 1, 0),
        tf_strdelputp(1, 8, 'h', (PLI_BYTE8*)"1", -1, 0),
        tf_strdelputp(1, 8, 'h', (PLI_BYTE8*)"1", 1, 3),
        tf_strdelputp(1, 0, 'h', (PLI_BYTE8*)"1", 1, 0),
        tf_istrdelputp(1, 8, 'h', (PLI_BYTE8*)"1", 1, 0, NULL));
    io_printf("#c put errors real=%d long=%d\n",
        tf_strrealdelputp(1, 8, 'h', (PLI_BYTE8*)"1", -1.0, 0),
        tf_strlongdelputp(1, 8, 'h', (PLI_BYTE8*)"1", 0, -1, 0));
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, calltf_pvc, misctf_pvc, "$pli_tf_pvc", 0, NULL,
        NULL, { 0 } },
    { usertask, 0, NULL, NULL, calltf_put, NULL, "$pli_tf_put", 0, NULL, NULL,
        { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void startup(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

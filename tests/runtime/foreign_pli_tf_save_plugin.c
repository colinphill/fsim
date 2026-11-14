// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 tf_write_save and tf_read_restart (IEEE 1364-2001 25.65, 25.41,
// 21.1.4): misctf hears reason_startofsave and reason_save at $save, writes
// two integers with tf_write_save, and hears reason_startofrestart and
// reason_restart in a fresh process that restarts, reading them back with
// tf_read_restart. tf_write_save outside reason_save is an error (returns 0).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/veriuser.h"


static PLI_INT32 calltf_probe(PLI_INT32 data, PLI_INT32 reason)
{
    PLI_INT32 word = 1;
    (void)data;
    (void)reason;
    io_printf("#c tf: write outside reason_save=%d\n",
        tf_write_save((PLI_BYTE8*)&word, (PLI_INT32)sizeof word));
    return 0;
}

static PLI_INT32 misctf_probe(PLI_INT32 data, PLI_INT32 reason, PLI_INT32 paramvc)
{
    PLI_INT32 words[2] = { 11, 22 };
    (void)data;
    (void)paramvc;
    if (reason == reason_startofsave) {
        io_printf("#c tf: startofsave\n");
    } else if (reason == reason_save) {
        io_printf("#c tf: save wrote=%d\n",
            tf_write_save((PLI_BYTE8*)words, (PLI_INT32)sizeof words));
    } else if (reason == reason_startofrestart) {
        io_printf("#c tf: startofrestart\n");
    } else if (reason == reason_restart) {
        PLI_INT32 back[2] = { 0, 0 };
        PLI_INT32 ok = tf_read_restart((PLI_BYTE8*)back, (PLI_INT32)sizeof back);
        io_printf("#c tf: restart read=%d data=%d,%d\n", ok, back[0], back[1]);
    }
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, calltf_probe, misctf_probe, "$tf_probe", 0, NULL,
        NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void startup(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

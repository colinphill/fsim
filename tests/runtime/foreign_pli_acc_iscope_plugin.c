// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 interactive scope conformance plug-in (IEEE 1364-2001 23.46,
// 23.94): the scope before any setting is the first top-level module, and
// acc_set_interactive_scope changes it. $pli_acc_iscope is called from a
// module below the top, so the calling scope differs from the interactive
// one. The $scope system task (IEEE 1800-2023 D.11) changes the same
// interactive scope: the misctf routine of $pli_acc_iscope hears reason_scope
// for it, and $pli_acc_report shows what acc_handle_interactive_scope then
// reports. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/acc_user.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

static PLI_INT32 calltf_iscope(PLI_INT32 data, PLI_INT32 reason)
{
    handle scope;
    handle sub;
    (void)data;
    (void)reason;
    acc_initialize();
    scope = acc_handle_interactive_scope();
    io_printf("#c initial scope=%s\n",
        scope != NULL ? acc_fetch_fullname(scope) : "(null)");
    sub = acc_handle_by_name("top.sub", NULL);
    io_printf("#c set status=%d\n", (int)acc_set_interactive_scope(sub, 0));
    scope = acc_handle_interactive_scope();
    io_printf("#c changed scope=%s\n",
        scope != NULL ? acc_fetch_fullname(scope) : "(null)");
    acc_close();
    return 0;
}

static PLI_INT32 misctf_iscope(PLI_INT32 data, PLI_INT32 reason, PLI_INT32 paramvc)
{
    (void)data;
    (void)paramvc;
    if (reason == reason_scope) {
        io_printf("#c misctf reason_scope\n");
    }
    return 0;
}

static PLI_INT32 calltf_report(PLI_INT32 data, PLI_INT32 reason)
{
    handle scope;
    (void)data;
    (void)reason;
    acc_initialize();
    scope = acc_handle_interactive_scope();
    io_printf("#c report scope=%s\n",
        scope != NULL ? acc_fetch_fullname(scope) : "(null)");
    acc_close();
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, calltf_iscope, misctf_iscope, "$pli_acc_iscope",
        0, NULL, NULL, { 0 } },
    { usertask, 0, NULL, NULL, calltf_report, NULL, "$pli_acc_report", 0, NULL,
        NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void startup(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

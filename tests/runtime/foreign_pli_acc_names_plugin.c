// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 access routine conformance plug-in (IEEE 1364-2001 Clauses 22 and
// 23): acc_fetch_attribute, _int and _str over parameters and specparams
// named as attributes, accDefaultAttr0, and acc_handle_path. $pli_acc_names
// is registered through a veriusertfs table. Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/acc_user.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>

static PLI_INT32 calltf_names(PLI_INT32 data, PLI_INT32 reason)
{
    handle w1;
    handle w2;
    handle w3;
    handle u1;
    handle u2;
    handle first;
    handle output;
    handle input;
    char text[64];
    const char* result;
    (void)data;
    (void)reason;
    acc_initialize();
    w1 = acc_handle_by_name("w1", NULL);
    w2 = acc_handle_by_name("w2", NULL);
    w3 = acc_handle_by_name("w3", NULL);
    /* The attribute named with the object's name is found first (23.10). */
    io_printf("#c attribute specific=%.1f\n",
        acc_fetch_attribute(w1, "Cap$", 9.0));
    /* Failing that, the attribute string alone is a general attribute. */
    io_printf("#c attribute general=%.1f\n",
        acc_fetch_attribute(w2, "Cap$", 9.0));
    io_printf("#c attribute int=%d\n",
        (int)acc_fetch_attribute_int(w2, "Kind$", -1));
    /* No such attribute returns the default value passed (Table 137). */
    io_printf("#c attribute int default=%d\n",
        (int)acc_fetch_attribute_int(w1, "Kind$", -1));
    result = acc_fetch_attribute_str(w1, "Name$", "none");
    snprintf(text, sizeof text, "%s", result != NULL ? result : "(null)");
    io_printf("#c attribute str=%s\n", text);
    result = acc_fetch_attribute_str(w1, "Nosuch$", "none");
    snprintf(text, sizeof text, "%s", result != NULL ? result : "(null)");
    io_printf("#c attribute str default=%s\n", text);
    acc_configure(accDefaultAttr0, "true");
    io_printf("#c attribute zero=%.1f\n", acc_fetch_attribute(w3, "Nosuch$"));
    acc_configure(accDefaultAttr0, "false");
    /* An intermodule path joins an output port to an input port; one exists
       where a delay annotation made it (23.55), and none here. */
    u1 = acc_handle_by_name("u1", NULL);
    u2 = acc_handle_by_name("u2", NULL);
    first = acc_next_port(u1, NULL);
    output = acc_next_port(u1, first);
    input = acc_next_port(u2, NULL);
    io_printf("#c path %s\n",
        acc_handle_path(output, input) == NULL ? "none" : "found");
    acc_close();
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, calltf_names, NULL, "$pli_acc_names", 0, NULL,
        NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void startup(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

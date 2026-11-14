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
    handle v1;
    handle v2;
    handle vy_bit;
    handle va_bit;
    handle bit_path;
    handle p1;
    handle p2;
    handle io1;
    handle io2;
    handle io_path;
    double d[2];
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
    /* An intermodule path is a net path that connects an output port of one
       module to an input port of another (23.50): u1.y drives w2, which u2.a
       loads, so the path exists with no annotation; u2.y and u1.a lie on
       different nets, and an input port is no source. */
    u1 = acc_handle_by_name("u1", NULL);
    u2 = acc_handle_by_name("u2", NULL);
    first = acc_next_port(u1, NULL);
    output = acc_next_port(u1, first);
    input = acc_next_port(u2, NULL);
    io_printf("#c path %s\n",
        acc_handle_path(output, input) == NULL ? "none" : "found");
    io_printf("#c path reversed %s\n",
        acc_handle_path(input, output) == NULL ? "none" : "found");
    io_printf("#c path other net %s\n",
        acc_handle_path(acc_next_port(u2, input), first) == NULL
            ? "none"
            : "found");
    /* One bit of a vector output port to the same bit of a vector input
       port is an intermodule path; another bit is not on its net (23.50).
       Its delays are the module input port delay of the input bit. */
    v1 = acc_handle_by_name("v1", NULL);
    v2 = acc_handle_by_name("v2", NULL);
    vy_bit = acc_next_bit(acc_next_port(v1, acc_next_port(v1, NULL)), NULL);
    va_bit = acc_next_bit(acc_next_port(v2, NULL), NULL);
    bit_path = acc_handle_path(vy_bit, va_bit);
    io_printf("#c bit path %s\n", bit_path == NULL ? "none" : "found");
    io_printf("#c bit path other bit %s\n",
        acc_handle_path(vy_bit, acc_next_bit(acc_next_port(v2, NULL), va_bit))
                == NULL
            ? "none"
            : "found");
    io_printf("#c bit path whole port %s\n",
        acc_handle_path(vy_bit, acc_next_port(v2, NULL)) == NULL ? "none"
                                                                 : "found");
    (void)acc_configure(accPathDelayCount, "2");
    io_printf("#c bit path replace ok=%d\n",
        (int)acc_replace_delays(bit_path, 3.0, 4.0));
    io_printf("#c input bit fetch ok=%d",
        (int)acc_fetch_delays(va_bit, &d[0], &d[1]));
    io_printf(" %.1f %.1f\n", d[0], d[1]);
    io_printf("#c bit path fetch ok=%d",
        (int)acc_fetch_delays(bit_path, &d[0], &d[1]));
    io_printf(" %.1f %.1f\n", d[0], d[1]);
    /* An inout port is a source and a load alike (16.2.4): the path from the
       first pad's inout port to the second's exists, and its delays are the
       second port's module input port delay, applied to the signal that
       arrives at the port. */
    p1 = acc_handle_by_name("p1", NULL);
    p2 = acc_handle_by_name("p2", NULL);
    io1 = acc_next_port(p1, NULL);
    io2 = acc_next_port(p2, NULL);
    io_path = acc_handle_path(io1, io2);
    io_printf("#c inout path %s\n", io_path == NULL ? "none" : "found");
    io_printf("#c inout path reversed %s\n",
        acc_handle_path(io2, io1) == NULL ? "none" : "found");
    io_printf("#c inout path replace ok=%d\n",
        (int)acc_replace_delays(io_path, 5.0, 6.0));
    io_printf("#c inout input fetch ok=%d",
        (int)acc_fetch_delays(io2, &d[0], &d[1]));
    io_printf(" %.1f %.1f\n", d[0], d[1]);
    io_printf("#c inout path fetch ok=%d",
        (int)acc_fetch_delays(io_path, &d[0], &d[1]));
    io_printf(" %.1f %.1f\n", d[0], d[1]);
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

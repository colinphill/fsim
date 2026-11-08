// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 module input port delay conformance plug-in (IEEE 1364-2005
// 22.5.2, 23.1, 23.14, 23.90, Table 130): acc_fetch_delays,
// acc_replace_delays and acc_append_delays on module input ports, port bits
// and intermodule paths, with accMapToMipd, and the pulse routines on paths
// and ports, through a veriusertfs table.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/acc_user.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

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

/* Fetch two delays of an object and show them under one label. */
static void fetch2(const char* fetch_label, const char* delay_label, handle object)
{
    double d[2];
    flag(fetch_label, acc_fetch_delays(object, &d[0], &d[1]));
    show_array(delay_label, d, 2);
}

/* Fetch the reject and error limits of the two delays of an object. */
static void pulse4(const char* label, handle object)
{
    double p[4];
    flag(label, acc_fetch_pulsere(object, &p[0], &p[1], &p[2], &p[3]));
    show_array(label, p, 4);
}

static PLI_INT32 calltf_mipd(PLI_INT32 data, PLI_INT32 reason)
{
    handle u0;
    handle u1;
    handle u2;
    handle u3;
    handle out0;
    handle out2;
    handle in;
    handle ic0;
    handle ic2;
    handle vec_port;
    handle bit0;
    handle bit1;
    double d[12];
    (void)data;
    (void)reason;
    (void)acc_initialize();
    u0 = acc_handle_object("top.u0");
    u1 = acc_handle_object("top.u1");
    u2 = acc_handle_object("top.u2");
    u3 = acc_handle_object("top.u3");
    out0 = acc_next_port(u0, acc_next_port(u0, NULL));
    out2 = acc_next_port(u2, acc_next_port(u2, NULL));
    in = acc_next_port(u1, NULL);
    ic0 = acc_handle_path(out0, in);
    ic2 = acc_handle_path(out2, in);
    vec_port = acc_next_port(u3, NULL);
    bit0 = acc_next_bit(vec_port, NULL);
    bit1 = acc_next_bit(vec_port, bit0);

    /* The SDF PORT delay is the port's MIPD; each INTERCONNECT is the delay
       of its own intermodule path. */
    config(accPathDelayCount, "2");
    fetch2("mipd fetch", "mipd delays", in);
    fetch2("ic0 fetch", "ic0 delays", ic0);
    fetch2("ic2 fetch", "ic2 delays", ic2);
    /* Only an input port has a MIPD. */
    flag("output port fetch", acc_fetch_delays(out0, &d[0], &d[1]));
    config(accPathDelayCount, "12");
    flag("mipd fetch 12", acc_fetch_delays(in, &d[0], &d[1], &d[2], &d[3],
        &d[4], &d[5], &d[6], &d[7], &d[8], &d[9], &d[10], &d[11]));
    show_array("mipd delays 12", d, 12);
    config(accPathDelayCount, "2");

    /* A path delay written is mapped to the MIPD of its destination port:
       the longest (default), the shortest or the last of the delays of the
       paths that end there (Table 130). */
    config(accMapToMipd, "max");
    flag("ic0 replace max", acc_replace_delays(ic0, 6.0, 7.0));
    fetch2("ic0 fetch", "ic0 delays", ic0);
    fetch2("mipd fetch", "mipd delays", in);
    config(accMapToMipd, "min");
    flag("ic0 replace min", acc_replace_delays(ic0, 6.0, 7.0));
    fetch2("mipd fetch", "mipd delays", in);
    config(accMapToMipd, "latest");
    flag("ic2 replace latest", acc_replace_delays(ic2, 8.0, 9.0));
    fetch2("ic2 fetch", "ic2 delays", ic2);
    fetch2("ic0 fetch", "ic0 delays", ic0);
    fetch2("mipd fetch", "mipd delays", in);
    /* The MIPD takes delays of its own. */
    flag("mipd append", acc_append_delays(in, 1.0, 1.0));
    fetch2("mipd fetch", "mipd delays", in);
    /* A value outside Table 130 is diagnosed by the routine that uses it and
       leaves the path as it was. */
    config(accMapToMipd, "median");
    flag("ic0 replace bad mode", acc_replace_delays(ic0, 1.0, 1.0));
    config(accMapToMipd, "max");
    fetch2("ic0 fetch", "ic0 delays", ic0);

    /* A bit of a port holds a delay of its own; the port keeps its own. */
    fetch2("vec port fetch", "vec port delays", vec_port);
    flag("bit0 replace", acc_replace_delays(bit0, 7.0, 8.0));
    fetch2("bit0 fetch", "bit0 delays", bit0);
    fetch2("bit1 fetch", "bit1 delays", bit1);
    fetch2("vec port fetch", "vec port delays", vec_port);

    /* The pulse routines on an intermodule path and on input ports (23.2,
       23.27, 23.91, 23.94): without limits in the SDF a pulse is rejected
       and errors at the delay. */
    pulse4("ic0 pulsere", ic0);
    flag("ic0 replace pulsere", acc_replace_pulsere(ic0, 1.0, 2.0, 3.0, 4.0));
    pulse4("ic0 pulsere", ic0);
    flag("ic0 set pulsere", acc_set_pulsere(ic0, 50.0, 100.0));
    pulse4("ic0 pulsere", ic0);
    fetch2("ic0 fetch", "ic0 delays", ic0);
    pulse4("mipd pulsere", in);
    flag("mipd replace pulsere", acc_replace_pulsere(in, 2.0, 3.0, 4.0, 5.0));
    pulse4("mipd pulsere", in);
    fetch2("mipd fetch", "mipd delays", in);
    pulse4("bit0 pulsere", bit0);
    flag("bit0 replace pulsere", acc_replace_pulsere(bit0, 1.0, 2.0, 3.0, 4.0));
    pulse4("bit0 pulsere", bit0);
    pulse4("vec port pulsere", vec_port);
    /* A reject limit above the error limit is refused. */
    flag("mipd bad pulsere", acc_replace_pulsere(in, 3.0, 2.0, 4.0, 5.0));
    acc_close();
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, calltf_mipd, NULL, "$acc_mipd", 0, NULL, NULL,
        { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void register_table(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_table, 0 };

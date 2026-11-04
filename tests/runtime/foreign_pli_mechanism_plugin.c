// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 association mechanism and misctf reasons (IEEE 1364-2001 21.1,
// 21.3.2, Table 87, Table 88, Annex F). The library exports veriusertfs[]
// and endofcompile_routines[] and registers nothing itself: simulate reads
// them from the library. The misctf routine reports every reason it is
// called for. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/veriuser.h"

static int sizetf_calls;

static const char* reason_name(PLI_INT32 reason)
{
    switch (reason) {
    case reason_endofcompile: return "endofcompile";
    case reason_synch: return "synch";
    case reason_rosynch: return "rosynch";
    case reason_reactivate: return "reactivate";
    case reason_force: return "force";
    case reason_release: return "release";
    case reason_disable: return "disable";
    case reason_finish: return "finish";
    default: return "other";
    }
}

static PLI_INT32 end_of_compile_routine(void)
{
    io_printf("#c endofcompile_routines entry\n");
    return 0;
}

static PLI_INT32 mech_checktf(PLI_INT32 data, PLI_INT32 reason)
{
    io_printf("#c checktf data=%d reason=%d\n", (int)data, (int)reason);
    return 0;
}

static PLI_INT32 mech_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    io_printf("#c calltf data=%d reason=%d t=%d\n", (int)data, (int)reason,
        (int)tf_gettime());
    (void)tf_synchronize();
    (void)tf_rosynchronize();
    (void)tf_setdelay(10);
    return 0;
}

static PLI_INT32 mech_misctf(PLI_INT32 data, PLI_INT32 reason, PLI_INT32 paramvc)
{
    (void)data;
    if (reason == reason_endofcompile) {
        io_printf("#c misctf endofcompile\n");
    } else if (reason == reason_finish) {
        io_printf("#c misctf finish\n");
        io_printf("#c sizetf calls=%d\n", sizetf_calls);
    } else {
        io_printf("#c misctf t=%d reason=%s paramvc=%d\n", (int)tf_gettime(),
            reason_name(reason), (int)paramvc);
    }
    return 0;
}

static PLI_INT32 mech_fn_sizetf(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    ++sizetf_calls;
    return 16;
}

static PLI_INT32 mech_fn_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    (void)tf_putp(0, 0x1234);
    return 0;
}

FSIM_VPI_EXPORT s_tfcell veriusertfs[] = {
    { usertask, 7, mech_checktf, NULL, mech_calltf, mech_misctf, "$mech", 0, NULL, NULL, { 0 } },
    { userfunction, 9, NULL, mech_fn_sizetf, mech_fn_calltf, NULL,
        "$mech_fn", 0, NULL, NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

FSIM_VPI_EXPORT PLI_BYTE8* veriuser_version_str = "fsim pli-mechanism 1.0";

FSIM_VPI_EXPORT PLI_INT32 (*endofcompile_routines[])(void)
    = { end_of_compile_routine, 0 };

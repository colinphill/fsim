// SPDX-License-Identifier: Apache-2.0
// The class typespec of a handle declared `C #(N) c;` in a module is the
// specialization its instance's parameter selects (IEEE 1800-2023 8.25,
// 23.2.2, 37.32): top.u3.c names C#(3), top.u5.c names C#(5), top.u2.c the
// default parameter's C#(2); `d` reads `M = N + 1`, and C#(4) with the class's
// default W of 4 is the bare class C.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <string.h>

static void show(char* name)
{
    vpiHandle const object = vpi_handle_by_name(name, NULL);
    vpiHandle const typespec = object != NULL ? vpi_handle(vpiTypespec, object) : NULL;
    const char* const text = typespec != NULL ? vpi_get_str(vpiName, typespec) : NULL;
    vpi_printf("#c %s: typespec %s\n", name, text != NULL ? text : "none");
}

static PLI_INT32 start(p_cb_data data)
{
    (void)data;
    show("top.u3.c");
    show("top.u5.c");
    show("top.u2.c");
    show("top.u3.d");
    show("top.u2.d");
    show("top.u5.d");
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

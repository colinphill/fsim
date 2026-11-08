// SPDX-License-Identifier: Apache-2.0
// DPI export dispatch plug-in for a module and a generate block that export
// functions of one name (IEEE 1800-2023 35.5.3, 35.7): the same export name
// is called in the module instance and in the block instance.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

static void call_in(const char* const scope, const int value)
{
    int argument = value;
    void* arguments[] = { &argument };
    (void)svSetScope(svGetScopeFromName(scope));
    (void)fsim_svdpi_export_call_v3("dpi_same_note", arguments, 0);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_same_run(void)
{
    printf("#c run scope=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
    call_in("top.u", 1);
    call_in("top.u.blk", 2);
    call_in("top.u", 3);
    return 0;
}

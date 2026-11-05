// SPDX-License-Identifier: Apache-2.0
// DPI conformance plug-in for exports declared in generate blocks (IEEE
// 1800-2023 35.5.3, 35.7): a context import sets the scope to a generate
// block instance and calls the export that block declares. Lines starting
// with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

static void enter(const char* const name)
{
    (void)svSetScope(svGetScopeFromName(name));
    printf("#c run in=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
}

static void note(const int value)
{
    int argument = value;
    void* arguments[] = { &argument };
    (void)fsim_svdpi_export_call_v3("dpi_gen_note", arguments, 0);
}

static void wait_for(const int delay)
{
    int argument = delay;
    void* arguments[] = { &argument };
    const int status = fsim_svdpi_export_call_v3("dpi_gen_wait", arguments, 0);
    printf("#c run wait status=%d\n", status);
    fflush(stdout);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_gen_run(void)
{
    printf("#c run scope=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
    enter("top.h.blk");
    note(5);
    note(7);
    enter("top.h.lp[1]");
    wait_for(4);
    enter("top.h.lp[0]");
    wait_for(2);
    return 0;
}

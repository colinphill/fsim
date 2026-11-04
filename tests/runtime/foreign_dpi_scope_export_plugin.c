// SPDX-License-Identifier: Apache-2.0
// DPI export dispatch conformance plug-in (IEEE 1800-2023 35.5.3, 35.7,
// 35.8): a context import calls the exports of its instance, with and
// without svSetScope; a noncontext import calls an export, which is an
// error. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

static void note(const int value)
{
    int argument = value;
    void* arguments[] = { &argument };
    (void)fsim_svdpi_export_call_v3("dpi_scope_note", arguments, 0);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_scope_run(const int id)
{
    int delay = 3;
    void* arguments[] = { &delay };
    int status;
    printf("#c run%d scope=%s\n", id, svGetNameFromScope(svGetScope()));
    fflush(stdout);
    note(100 + id);
    if (id == 1) {
        const svScope previous = svSetScope(svGetScopeFromName("top.u1"));
        printf("#c run1 svSetScope previous=%s\n",
            svGetNameFromScope(previous));
        fflush(stdout);
        note(200 + id);
    }
    status = fsim_svdpi_export_call_v3("dpi_scope_wait", arguments, 0);
    printf("#c run%d wait status=%d\n", id, status);
    fflush(stdout);
    return 0;
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_scope_bad(void)
{
    (void)fsim_svdpi_export_call_v3("dpi_scope_note", 0, 0);
}

// SPDX-License-Identifier: Apache-2.0
// DPI conformance plug-in for the exports of the compilation unit (IEEE
// 1800-2023 35.5.2, 35.7): a context import calls a compilation-unit export
// in its own scope and in the scope svSetScope names, and an export of that
// scope. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_unit_run(void)
{
    int value = 1;
    void* arguments[] = { &value };
    svScope previous;
    printf("#c run scope=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
    (void)fsim_svdpi_export_call_v3("dpi_unit_note", arguments, 0);
    previous = svSetScope(svGetScopeFromName("top.u1"));
    printf("#c run svSetScope previous=%s\n", svGetNameFromScope(previous));
    fflush(stdout);
    value = 2;
    (void)fsim_svdpi_export_call_v3("dpi_unit_note", arguments, 0);
    value = 3;
    (void)fsim_svdpi_export_call_v3("dpi_unit_local", arguments, 0);
    return 0;
}

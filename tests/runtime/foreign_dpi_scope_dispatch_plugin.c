// SPDX-License-Identifier: Apache-2.0
// DPI export dispatch by scope conformance plug-in (IEEE 1800-2023 35.5.3,
// 35.7): a context import calls exports in other instances after svSetScope,
// a package is no scope name (H.9.3), and an export in a scope that does not
// declare it.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

// Calls the export `name` with one int argument in the scope `scope`, and
// restores the call chain's scope.
static int call_in(const char* const scope, const char* const name,
    const int value)
{
    int argument = value;
    void* arguments[] = { &argument };
    const svScope previous = svSetScope(svGetScopeFromName(scope));
    const int status = fsim_svdpi_export_call_v3(name, arguments, 0);
    (void)svSetScope(previous);
    return status;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_dispatch_run(void)
{
    int status;
    printf("#c run scope=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
    (void)call_in("top.u1", "dpi_dispatch_note", 10);
    (void)call_in("top.u2", "dpi_dispatch_note", 20);
    (void)call_in("top.u1", "dpi_dispatch_note", 5);
    status = call_in("top.u2", "dpi_dispatch_wait", 3);
    printf("#c run wait status=%d\n", status);
    fflush(stdout);
    // A package is not a scope svGetScopeFromName names: it names module,
    // program, interface and generate scopes (IEEE 1800-2023 H.9.3).
    printf("#c pkg scope=%s\n", svGetScopeFromName("pk") == NULL ? "null" : "set");
    printf("#c run scope after=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
    return 0;
}

// The scope of the import declaration, module top, declares no export.
FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_dispatch_bad(void)
{
    int argument = 1;
    void* arguments[] = { &argument };
    printf("#c bad scope=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
    (void)fsim_svdpi_export_call_v3("dpi_dispatch_note", arguments, 0);
    return 0;
}

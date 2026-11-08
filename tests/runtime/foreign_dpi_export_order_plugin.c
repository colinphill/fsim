// SPDX-License-Identifier: Apache-2.0
// DPI export dispatch plug-in for exports of instances elaborated after the
// calling instance (IEEE 1800-2023 35.5.3, 35.7): a context import of
// top.f sets the scope to leaf instances inside a loop generate and an
// instance array of top.l and calls their exports.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

// Calls the export `name` with one int argument in the scope `scope`.
static int call_in(const char* const scope, const char* const name,
    const int value)
{
    int argument = value;
    void* arguments[] = { &argument };
    (void)svSetScope(svGetScopeFromName(scope));
    return fsim_svdpi_export_call_v3(name, arguments, 0);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_ord_run(void)
{
    const svScope home = svGetScope();
    printf("#c run scope=%s\n", svGetNameFromScope(home));
    fflush(stdout);
    (void)call_in("top.l.lp[1].m[0].k", "dpi_ord_note", 10);
    (void)call_in("top.l.lp[0].m[1].k", "dpi_ord_note", 20);
    (void)call_in("top.l.lp[1].m[0].k", "dpi_ord_note", 5);
    (void)call_in("top.l.lp[1].m[1].k", "dpi_ord_note", 7);
    const int status = call_in("top.l.lp[0].m[1].k", "dpi_ord_wait", 3);
    printf("#c run wait status=%d\n", status);
    (void)svSetScope(home);
    printf("#c run scope after=%s\n", svGetNameFromScope(svGetScope()));
    fflush(stdout);
    return 0;
}

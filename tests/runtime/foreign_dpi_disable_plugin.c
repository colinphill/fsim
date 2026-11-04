// SPDX-License-Identifier: Apache-2.0
// DPI disable protocol conformance plug-in (IEEE 1800-2023 35.9): an
// imported task that is disabled while an exported task runs sees the
// export return 1 and svIsDisabledState() true, and returns 1. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_disable_run(const int id)
{
    int delay = id == 0 ? 10 : 3;
    void* arguments[] = { &delay };
    int status;
    printf("#c run%d state=%d\n", id, svIsDisabledState());
    fflush(stdout);
    status = fsim_svdpi_export_call_v3("dpi_disable_wait", arguments, 0);
    printf("#c run%d export status=%d disabled=%d\n", id, status,
        svIsDisabledState());
    if (svIsDisabledState()) {
        printf("#c run%d returns 1\n", id);
        fflush(stdout);
        return 1;
    }
    printf("#c run%d completed\n", id);
    fflush(stdout);
    return 0;
}

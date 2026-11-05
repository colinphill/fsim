// SPDX-License-Identifier: Apache-2.0
// DPI disable protocol conformance plug-in (IEEE 1800-2023 35.9): the
// exported task an imported task calls is disabled itself, so it returns 0,
// svIsDisabledState() stays false, and the imported task returns 0. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_target_run(void)
{
    int delay = 10;
    void* arguments[] = { &delay };
    int status;
    printf("#c run state=%d\n", svIsDisabledState());
    fflush(stdout);
    status = fsim_svdpi_export_call_v3("dpi_target_wait", arguments, 0);
    printf("#c run export status=%d disabled=%d\n", status,
        svIsDisabledState());
    if (svIsDisabledState()) {
        fflush(stdout);
        return 1;
    }
    printf("#c run completed\n");
    fflush(stdout);
    return 0;
}

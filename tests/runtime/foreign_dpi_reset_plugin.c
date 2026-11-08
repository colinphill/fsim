// SPDX-License-Identifier: Apache-2.0
// DPI reset conformance plug-in (IEEE 1800-2023 D.8, 35.9, 38.33): an imported
// context task that is suspended in an export call when $reset runs is
// disabled (D.8 a: "disables all concurrent activity"), so its export call
// returns 1 with svIsDisabledState() true and the task returns 1. The
// svPutUserData area of the scope is empty after the reset (38.33: "After a
// restart or a reset, subsequent calls to vpi_get_userdata() shall return
// NULL"; 35 states nothing for svGetUserData, so fsim applies the same rule).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

static int user_key;
static int user_value;

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_reset_run(const int id)
{
    int delay = id == 0 ? 10 : 3;
    void* arguments[] = { &delay };
    int status;
    const svScope scope = svGetScope();
    const int had_data = svGetUserData(scope, &user_key) != NULL;
    printf("#c run%d state=%d userdata=%s\n", id, svIsDisabledState(),
        had_data ? "set" : "none");
    fflush(stdout);
    if (!had_data) {
        svPutUserData(scope, &user_key, &user_value);
    }
    status = fsim_svdpi_export_call_v3("dpi_reset_wait", arguments, 0);
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

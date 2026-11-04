// SPDX-License-Identifier: Apache-2.0
// DPI time and scope service conformance plug-in (IEEE 1800-2023 35.5.3,
// H.9.3, H.13): a context import reports the time, time unit and precision
// of its calling scope and of the simulation (a NULL scope), and looks up
// another instance by name before that instance has made any call. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "svdpi.h"

#include <stdio.h>

static int user_key;
static int user_value = 7;

static unsigned long long sim_time(const svScope scope)
{
    svTimeVal time = { vpiSimTime, 0u, 0u, 0.0 };
    if (svGetTime(scope, &time) != 0) {
        return 0u;
    }
    return ((unsigned long long)time.high << 32U) | time.low;
}

static double scaled_time(const svScope scope)
{
    svTimeVal time = { vpiScaledRealTime, 0u, 0u, 0.0 };
    if (svGetTime(scope, &time) != 0) {
        return -1.0;
    }
    return time.real;
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_time_scope_probe(
    const int id)
{
    const svScope own = svGetScope();
    const svScope other = svGetScopeFromName("top.u1");
    int32_t unit = 0;
    int32_t precision = 0;
    int32_t null_unit = 0;
    int32_t null_precision = 0;
    int status;
    printf("#c probe%d scope=%s\n", id, svGetNameFromScope(own));
    printf("#c probe%d time sim=%llu scaled=%.3f null_scaled=%.3f\n", id,
        sim_time(own), scaled_time(own), scaled_time(NULL));
    status = svGetTimeUnit(own, &unit) + svGetTimePrecision(own, &precision)
        + svGetTimeUnit(NULL, &null_unit)
        + svGetTimePrecision(NULL, &null_precision);
    printf("#c probe%d scale status=%d unit=%d precision=%d null_unit=%d "
           "null_precision=%d\n",
        id, status, (int)unit, (int)precision, (int)null_unit,
        (int)null_precision);
    printf("#c probe%d lookup top=%d other=%s nope=%d\n", id,
        svGetScopeFromName("top") != NULL, svGetNameFromScope(other),
        svGetScopeFromName("top.nope") != NULL);
    if (id == 0) {
        // u1 has made no call yet; its scope still takes user data.
        printf("#c probe%d put=%d\n", id,
            svPutUserData(other, &user_key, &user_value));
    } else {
        printf("#c probe%d user own=%d by_name=%d same=%d u0=%d\n", id,
            svGetUserData(own, &user_key) == &user_value,
            svGetUserData(other, &user_key) == &user_value, own == other,
            svGetUserData(svGetScopeFromName("top.u0"), &user_key) != NULL);
    }
    fflush(stdout);
}

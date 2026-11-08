// SPDX-License-Identifier: Apache-2.0
// Assertion callbacks on immediate assertions (IEEE 1800-2023 39.4.1):
// cbAssertionStart, cbAssertionSuccess and cbAssertionFailure on each
// immediate assertion of top. They exist only with simulate --foreign-probes;
// without it the registration fails with an error that names the option.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <string.h>

static PLI_INT32 reached(PLI_INT32 reason, p_vpi_time time,
    vpiHandle assertion, p_vpi_attempt_info info, PLI_BYTE8* user)
{
    (void)assertion;
    (void)info;
    vpi_printf("#c %s %s t=%u\n", (const char*)user,
        reason == cbAssertionStart ? "start"
            : reason == cbAssertionSuccess ? "success" : "failure",
        (unsigned)time->low);
    return 0;
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    vpiHandle top = vpi_handle_by_name((PLI_BYTE8*)"top", NULL);
    /* Immediate assertions follow the concurrent ones in the instance's
       vpiAssertion iteration (37.49, 37.50). */
    vpiHandle iterator = top == NULL ? NULL : vpi_iterate(vpiAssertion, top);
    vpiHandle assertion;
    (void)data;
    while (iterator != NULL && (assertion = vpi_scan(iterator)) != NULL) {
        const char* name;
        if (vpi_get(vpiType, assertion) != vpiImmediateAssert) {
            continue;
        }
        name = vpi_get_str(vpiName, assertion);
        static const PLI_INT32 reasons[] = { cbAssertionStart,
            cbAssertionSuccess, cbAssertionFailure };
        unsigned each;
        for (each = 0U; each < 3U; ++each) {
            if (vpi_register_assertion_cb(assertion, reasons[each], reached,
                    (PLI_BYTE8*)name) == NULL) {
                s_vpi_error_info error;
                if (vpi_chk_error(&error) != 0) {
                    vpi_printf("#c register failed: %s\n", error.message);
                }
                return 0;
            }
        }
    }
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start_of_simulation;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

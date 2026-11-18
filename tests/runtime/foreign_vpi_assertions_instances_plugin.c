// SPDX-License-Identifier: Apache-2.0
// Assertion callbacks on sequence and property instances (IEEE 1800-2023
// 39.4.2): registers cbAssertionStart, cbAssertionSuccess and
// cbAssertionFailure on the sequence instance of top.a_seq and the property
// instance of top.a_prop at the start of simulation, tries
// cbAssertionVacuousSuccess on the property instance (refused: not a reason
// of an instance) and removes the three callbacks on the sequence instance
// at time 28. Every callback is printed with its instance and the attempt's
// start time. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"

#include <string.h>

static vpiHandle sequence_callbacks[3];

static const char* type_name(vpiHandle object)
{
    switch (vpi_get(vpiType, object)) {
    case vpiSequenceInst:
        return "vpiSequenceInst";
    case vpiPropertyInst:
        return "vpiPropertyInst";
    default:
        return "other";
    }
}

static PLI_INT32 observed(PLI_INT32 reason, p_vpi_time time,
    vpiHandle instance, p_vpi_attempt_info info, PLI_BYTE8* user_data)
{
    const char* const name = vpi_get_str(vpiName, instance);
    (void)user_data;
    if (info == NULL) {
        vpi_printf("#c %u missing info\n", (unsigned)time->low);
        return 0;
    }
    vpi_printf("#c %u %s %s %s start=%u\n", (unsigned)time->low,
        reason == cbAssertionStart       ? "start"
            : reason == cbAssertionSuccess ? "success"
            : reason == cbAssertionFailure ? "failure"
                                           : "other",
        type_name(instance), name != NULL ? name : "",
        (unsigned)info->attemptStartTime.low);
    return 0;
}

// The instance an assertion's property is or holds: the property itself, or
// the expression of its property specification.
static vpiHandle instance_of(const char* path)
{
    vpiHandle const assertion = vpi_handle_by_name((PLI_BYTE8*)path, NULL);
    vpiHandle property = assertion != NULL ? vpi_handle(vpiProperty, assertion) : NULL;
    if (property != NULL && vpi_get(vpiType, property) == vpiPropertySpec) {
        property = vpi_handle(vpiPropertyExpr, property);
    }
    if (property == NULL
        || (vpi_get(vpiType, property) != vpiSequenceInst
            && vpi_get(vpiType, property) != vpiPropertyInst)) {
        vpi_printf("#c %s has no instance\n", path);
        return NULL;
    }
    return property;
}

static PLI_INT32 remove_sequence_callbacks(p_cb_data data)
{
    int removed = 0;
    int index;
    (void)data;
    for (index = 0; index < 3; ++index) {
        if (sequence_callbacks[index] != NULL
            && vpi_remove_cb(sequence_callbacks[index]) != 0) {
            ++removed;
        }
    }
    vpi_printf("#c 28 removed %d\n", removed);
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    static const PLI_INT32 reasons[3]
        = { cbAssertionStart, cbAssertionSuccess, cbAssertionFailure };
    s_cb_data delayed;
    s_vpi_time when;
    s_vpi_error_info error;
    vpiHandle sequence;
    vpiHandle property;
    vpiHandle refused;
    int index;
    (void)data;
    sequence = instance_of("top.a_seq");
    property = instance_of("top.a_prop");
    if (sequence == NULL || property == NULL) {
        return 0;
    }
    for (index = 0; index < 3; ++index) {
        sequence_callbacks[index]
            = vpi_register_assertion_cb(sequence, reasons[index], observed, NULL);
        (void)vpi_register_assertion_cb(property, reasons[index], observed, NULL);
    }
    refused = vpi_register_assertion_cb(
        property, cbAssertionVacuousSuccess, observed, NULL);
    memset(&error, 0, sizeof error);
    if (refused == NULL && vpi_chk_error(&error) != 0) {
        vpi_printf("#c refused %s: %s\n", type_name(property),
            error.message != NULL ? error.message : "");
    } else {
        vpi_printf("#c accepted cbAssertionVacuousSuccess\n");
    }
    memset(&delayed, 0, sizeof delayed);
    memset(&when, 0, sizeof when);
    when.type = vpiSimTime;
    when.low = 28;
    delayed.reason = cbAfterDelay;
    delayed.cb_rtn = remove_sequence_callbacks;
    delayed.time = &when;
    (void)vpi_register_cb(&delayed);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

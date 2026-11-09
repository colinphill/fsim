// SPDX-License-Identifier: Apache-2.0
// VHPI runtime conformance plug-in (IEEE 1076-2019 14.7.5, 17.3, 21-23).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

static vhpiHandleT clk;
static vhpiHandleT d;
static vhpiHandleT n;
static vhpiHandleT p;

static unsigned long long now(void)
{
    vhpiTimeT time;
    vhpi_get_time(&time, NULL);
    return ((unsigned long long)time.high << 32U) | time.low;
}

static const char* text(vhpiHandleT object, vhpiFormatT format)
{
    static char buffer[80];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = format;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    if (vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

static void put_logic(vhpiHandleT object, vhpiEnumT logic,
    vhpiPutValueModeT mode)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiLogicVal;
    value.value.enumv = logic;
    vhpi_put_value(object, &value, mode);
}

static vhpiHandleT register_cb(int32_t reason, vhpiHandleT object,
    void (*routine)(const vhpiCbDataT*), unsigned long long delay)
{
    vhpiCbDataT data;
    vhpiTimeT time;
    memset(&data, 0, sizeof data);
    time.high = (int32_t)(delay >> 32U);
    time.low = (uint32_t)delay;
    data.reason = reason;
    data.cb_rtn = routine;
    data.obj = object;
    data.time = &time;
    return vhpi_register_cb(&data, vhpiReturnCb);
}

static void phase(const vhpiCbDataT* data)
{
    static const char* const names[] = { "", "registration", "analysis",
        "elaboration", "initialization", "simulation", "termination" };
    const vhpiIntT current = vhpi_get(vhpiPhaseP, NULL);
    printf("#c %s phase=%s\n", (const char*)data->user_data,
        current >= 0 && current <= 6 ? names[current] : "?");
    fflush(stdout);
}

static void on_clk(const vhpiCbDataT* data)
{
    vhpiTimeT next;
    const int activity = vhpi_get_next_time(&next);
    (void)data;
    // A STD_ULOGIC reads as its character in vhpiBinStrVal.
    printf("#c %llu clk=%s next=%llu%s\n", now(), text(clk, vhpiBinStrVal),
        ((unsigned long long)next.high << 32U) | next.low,
        activity == vhpiNoActivity ? " none" : "");
    if (strcmp(text(clk, vhpiBinStrVal), "0") == 0) {
        put_logic(d, vhpi1, vhpiDepositPropagate);
        printf("#c %llu d=%s after deposit-propagate\n", now(),
            text(d, vhpiBinStrVal));
    }
    fflush(stdout);
}

static void on_d(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu d changed to %s\n", now(), text(d, vhpiBinStrVal));
    fflush(stdout);
}

static int read_int(vhpiHandleT object)
{
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    (void)vhpi_get_value(object, &value);
    return value.value.intg;
}

static void at_12(const vhpiCbDataT* data)
{
    (void)data;
    // A deposit without propagation is not a value-change trigger.
    put_logic(d, vhpi0, vhpiDeposit);
    printf("#c %llu d=%s after deposit n=%d", now(), text(d, vhpiBinStrVal),
        read_int(n));
    printf(" bin=%s", text(n, vhpiBinStrVal));
    printf(" dec=%s", text(n, vhpiDecStrVal));
    printf(" hex=%s\n", text(n, vhpiHexStrVal));
    fflush(stdout);
    put_logic(p, vhpi1, vhpiForce);
    (void)raise(SIGINT);
}

// From vhpiCbStartOfPostponed to the end of the time step no update may
// cause a delta cycle (IEEE 1076-2019 14.7.5.3 h).
static void at_postponed(const vhpiCbDataT* data)
{
    int rejected;
    (void)data;
    put_logic(d, vhpi0, vhpiDepositPropagate);
    rejected = vhpi_check_error(NULL) != 0;
    printf("#c %llu start of postponed n=%d put rejected=%d\n", now(),
        read_int(n), rejected);
    fflush(stdout);
}

// A timeout runs after the cycle's signal updates, before the processes'
// new values are applied (IEEE 1076-2019 14.7.5.3 f 3).
static void timeout_15(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu timeout clk=%s n=%d\n", now(), text(clk, vhpiBinStrVal),
        read_int(n));
    fflush(stdout);
    (void)register_cb(vhpiCbStartOfPostponed, NULL, at_postponed, 0U);
    put_logic(p, vhpi0, vhpiRelease);
}

// Force and release callbacks carry the forced object and run with the
// value the force produced (IEEE 1076-2019 21.3.2.1, 21.3.2.3-4).
static void on_force(const vhpiCbDataT* data)
{
    printf("#c %llu %s %s=%s\n", now(), (const char*)data->user_data,
        (const char*)vhpi_get_str(vhpiFullNameP, data->obj),
        text(data->obj, vhpiBinStrVal));
    fflush(stdout);
}

// Every transaction makes the signal active, with or without an event
// (IEEE 1076-2019 21.3.2.5).
static void on_transaction(const vhpiCbDataT* data)
{
    printf("#c %llu transaction %s=%s\n", now(),
        (const char*)vhpi_get_str(vhpiFullNameP, data->obj),
        text(data->obj, vhpiBinStrVal));
    fflush(stdout);
}

static void object_cb(int32_t reason, vhpiHandleT object, const char* name,
    void (*routine)(const vhpiCbDataT*))
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    data.obj = object;
    data.user_data = (void*)name;
    (void)vhpi_register_cb(&data, 0);
}

// The routine's error, as vhpi_check_error reports it to a
// vhpiCbPLIError callback.
static void pli_error(const vhpiCbDataT* data)
{
    vhpiErrorInfoT info;
    (void)data;
    memset(&info, 0, sizeof info);
    printf("#c pli error=%d severity=%d\n", vhpi_check_error(&info),
        (int)info.severity);
    fflush(stdout);
}

// fsim's interrupt event is SIGINT (IEEE 1076-2019 21.3.7.10); the run
// goes on.
static void interrupt(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu interrupt\n", now());
    fflush(stdout);
}

static void quiescence(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c %llu quiescence\n", now());
    fflush(stdout);
}

static void start(const vhpiCbDataT* data)
{
    vhpiValueT value;
    vhpiHandleT handle;
    unsigned index;
    int failed = 0;
    phase(data);
    clk = vhpi_handle_by_name("top.clk", NULL);
    d = vhpi_handle_by_name("top.d", NULL);
    n = vhpi_handle_by_name("top.n", NULL);
    p = vhpi_handle_by_name("top.p", NULL);
    printf("#c capabilities=%d\n",
        vhpi_get(vhpiCapabilitiesP, vhpi_handle(vhpiTool, NULL))
            == (vhpiProvidesHierarchy | vhpiProvidesStaticAccess
                | vhpiProvidesPostAnalysis | vhpiProvidesForeignModel
                | vhpiProvidesAdvancedForeignModel));
    // A short buffer yields the size it needs (IEEE 1076-2019 23.19).
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    printf("#c binstr size=%d\n", vhpi_get_value(clk, &value));
    // A removed callback leaves the information model.
    for (index = 0U; index < 70000U; ++index) {
        handle = register_cb(vhpiCbAfterDelay, NULL, on_d, 1000000U);
        if (handle == NULL || vhpi_remove_cb(handle) != 0) {
            failed = 1;
        }
    }
    printf("#c lifetime limits ok=%d\n", !failed);
    (void)vhpi_format_value(NULL, NULL);
    printf("#c not implemented error=%d\n", vhpi_check_error(NULL) != 0);
    (void)register_cb(vhpiCbValueChange, clk, on_clk, 0U);
    (void)register_cb(vhpiCbValueChange, d, on_d, 0U);
    (void)register_cb(vhpiCbAfterDelay, NULL, at_12, 12000000U);
    (void)register_cb(vhpiCbTimeOut, NULL, timeout_15, 15000000U);
    object_cb(vhpiCbForce, NULL, "force", on_force);
    object_cb(vhpiCbForce, vhpi_handle_by_name("top.f(1)", NULL),
        "element force", on_force);
    object_cb(vhpiCbRelease, NULL, "release", on_force);
    object_cb(vhpiCbTransaction, vhpi_handle_by_name("top.t", NULL),
        "transaction", on_transaction);
    fflush(stdout);
}

static void phase_cb(int32_t reason, const char* name,
    void (*routine)(const vhpiCbDataT*))
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    data.user_data = (void*)name;
    (void)vhpi_register_cb(&data, 0);
}

static void startup(void)
{
    phase_cb(vhpiCbStartOfTool, "start of tool", phase);
    phase_cb(vhpiCbStartOfInitialization, "start of initialization", phase);
    phase_cb(vhpiCbEndOfProcesses, "end of processes", phase);
    phase_cb(vhpiCbEndOfInitialization, "end of initialization", phase);
    phase_cb(vhpiCbStartOfSimulation, "start of simulation", start);
    phase_cb(vhpiCbEndOfSimulation, "end of simulation", phase);
    phase_cb(vhpiCbEndOfTool, "end of tool", phase);
    phase_cb(vhpiCbPLIError, "pli error", pli_error);
    phase_cb(vhpiCbQuiescense, "quiescence", quiescence);
    phase_cb(vhpiCbSigInterrupt, "interrupt", interrupt);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

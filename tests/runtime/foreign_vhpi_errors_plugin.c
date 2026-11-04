// SPDX-License-Identifier: Apache-2.0
// VHPI error, control and registration conformance plug-in (IEEE 1076-2019
// 20.2, 23.3, 23.5, 23.18, 23.30). Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static vhpiHandleT initialization_handle;
static vhpiHandleT simulation_handle;

static char library_name[] = "lib";
static char alpha_name[] = "alpha";
static char beta_name[] = "beta";

static void model_exec(const vhpiCbDataT* data)
{
    (void)data;
}

static const char* base_name(const char* path)
{
    const char* slash = path == NULL ? NULL : strrchr(path, '/');
    if (path == NULL) {
        return "-";
    }
    return slash != NULL ? slash + 1 : path;
}

// A registration of the model with the name; whether it returned a handle
// and whether it raised an error.
static void register_model(
    const char* label, vhpiForeignKindT kind, char* name)
{
    vhpiForeignDataT model;
    vhpiHandleT handle;
    int failed;
    memset(&model, 0, sizeof model);
    model.kind = kind;
    model.libraryName = library_name;
    model.modelName = name;
    model.execf = model_exec;
    handle = vhpi_register_foreignf(&model);
    failed = vhpi_check_error(NULL);
    printf("#c %s: handle=%d error=%d\n", label, handle != NULL, failed);
}

// An unsupported property of the object fails; the error reports where the
// object is (IEEE 1076-2019 23.3).
static void location(const char* label, vhpiHandleT object)
{
    vhpiErrorInfoT info;
    memset(&info, 0, sizeof info);
    (void)vhpi_get(vhpiIsPLIP, object);
    if (vhpi_check_error(&info)) {
        printf("#c %s: severity=%d file=%s line=%d\n", label,
            (int)info.severity, base_name(info.file), (int)info.line);
    } else {
        printf("#c %s: no error\n", label);
    }
}

// vhpi_control and vhpi_get_time in the current phase; a stop request in the
// simulation phase would end the run.
static void routines(const char* phase, int simulating)
{
    vhpiTimeT now;
    long cycles;
    int rc;
    int failed;
    vhpi_get_time(NULL, NULL);
    printf("#c %s get_time(NULL,NULL): error=%d\n", phase,
        vhpi_check_error(NULL));
    vhpi_get_time(&now, NULL);
    printf("#c %s get_time(time): error=%d\n", phase, vhpi_check_error(NULL));
    vhpi_get_time(NULL, &cycles);
    printf("#c %s get_time(cycles): error=%d\n", phase,
        vhpi_check_error(NULL));
    if (!simulating) {
        rc = vhpi_control(vhpiStop);
        failed = vhpi_check_error(NULL);
        printf("#c %s control stop: rc=%d error=%d\n", phase, rc, failed);
    }
    rc = vhpi_control(vhpiReset);
    failed = vhpi_check_error(NULL);
    printf("#c %s control reset: rc=%d error=%d\n", phase, rc, failed);
}

static void termination(const vhpiCbDataT* data)
{
    vhpiTimeT now;
    int failed;
    (void)data;
    vhpi_get_time(&now, NULL);
    failed = vhpi_check_error(NULL);
    printf("#c termination get_time(time): error=%d\n", failed);
    printf("#c termination control finish: rc=%d", vhpi_control(vhpiFinish));
    printf(" error=%d\n", vhpi_check_error(NULL));
    fflush(stdout);
}

static void initialization(const vhpiCbDataT* data)
{
    vhpiHandleT current = vhpi_handle(vhpiCurCallback, NULL);
    (void)data;
    printf("#c initialization curcallback is the callback=%d\n",
        vhpi_compare_handles(current, initialization_handle));
    printf("#c initialization phase=%d\n", (int)vhpi_get(vhpiPhaseP, NULL));
    routines("initialization", 0);
    fflush(stdout);
}

static void simulation(const vhpiCbDataT* data)
{
    vhpiHandleT current = vhpi_handle(vhpiCurCallback, NULL);
    vhpiHandleT iterator;
    vhpiHandleT item;
    vhpiForeignDataT info;
    (void)data;
    printf("#c simulation curcallback is the callback=%d\n",
        vhpi_compare_handles(current, simulation_handle));
    printf("#c simulation phase=%d\n", (int)vhpi_get(vhpiPhaseP, NULL));
    routines("simulation", 1);
    printf("#c simulation control unknown: rc=%d", vhpi_control(99));
    printf(" error=%d\n", vhpi_check_error(NULL));
    register_model("simulation register", vhpiArchF, alpha_name);
    // The registered models, in registration order (23.30, Figure 52).
    iterator = vhpi_iterator(vhpiForeignfs, NULL);
    printf("#c foreignfs iterator type=%d\n",
        iterator != NULL && vhpi_get(vhpiIteratorTypeP, iterator)
            == vhpiForeignfs);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        memset(&info, 0, sizeof info);
        if (vhpi_get_foreignf_info(item, &info) == 0) {
            printf("#c foreignf %s %s kind=%d class=%d\n", info.libraryName,
                info.modelName, (int)info.kind,
                vhpi_get(vhpiKindP, item) == vhpiForeignfK);
        }
    }
    location("location of a signal", vhpi_handle_by_name("top.v", NULL));
    location("location of the tool", vhpi_handle(vhpiTool, NULL));
    fflush(stdout);
}

static vhpiHandleT phase_cb(int32_t reason, void (*routine)(const vhpiCbDataT*))
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = reason;
    data.cb_rtn = routine;
    return vhpi_register_cb(&data, vhpiReturnCb);
}

static void startup(void)
{
    int rc;
    int failed;
    register_model("register arch alpha", vhpiArchF, alpha_name);
    register_model("register duplicate alpha", vhpiArchF, alpha_name);
    register_model("register vhpiLibF", vhpiLibF, beta_name);
    register_model("register app beta", vhpiAppF, beta_name);
    register_model("register app beta again", vhpiAppF, beta_name);
    vhpi_get_time(NULL, NULL);
    printf("#c registration get_time(NULL,NULL): error=%d\n",
        vhpi_check_error(NULL));
    rc = vhpi_control(vhpiFinish);
    failed = vhpi_check_error(NULL);
    printf("#c registration control finish: rc=%d error=%d\n", rc, failed);
    printf("#c registration curcallback null=%d\n",
        vhpi_handle(vhpiCurCallback, NULL) == NULL);
    initialization_handle
        = phase_cb(vhpiCbStartOfInitialization, initialization);
    simulation_handle = phase_cb(vhpiCbStartOfSimulation, simulation);
    (void)phase_cb(vhpiCbEndOfTool, termination);
    fflush(stdout);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

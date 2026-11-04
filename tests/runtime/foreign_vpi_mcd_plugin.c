// SPDX-License-Identifier: Apache-2.0
// VPI multichannel descriptor conformance plug-in (IEEE 1800-2023 38.4,
// 38.5, 38.24-38.29): vpi_mcd_open shares the channel table with $fopen,
// vpi_mcd_printf writes channels but not fds, vpi_mcd_name names a single
// channel or an fd, vpi_mcd_close refuses channel 1 and accepts an fd.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static PLI_UINT32 mcd;

static const char* text(const char* value)
{
    return value != NULL ? value : "(null)";
}

static vpiHandle find(const char* name)
{
    vpiHandle handle = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    if (handle == NULL) {
        vpi_printf("#c no %s\n", name);
    }
    return handle;
}

static PLI_INT32 read_int(const char* name)
{
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpiHandle handle = find(name);
    if (handle != NULL) {
        vpi_get_value(handle, &value);
    }
    return value.value.integer;
}

static PLI_INT32 step_changed(p_cb_data data);
static PLI_INT32 end_of_simulation(p_cb_data data);

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    static s_vpi_value suppress = { vpiSuppressVal, { 0 } };
    s_cb_data callback;
    s_vpi_value value = { vpiIntVal, { 0 } };
    vpiHandle variable = find("top.mcd");
    vpiHandle step = find("top.step");
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbEndOfSimulation;
    callback.cb_rtn = end_of_simulation;
    (void)vpi_register_cb(&callback);
    mcd = vpi_mcd_open((PLI_BYTE8*)"mcd_out.txt");
    vpi_printf("#c open mcd=%u\n", (unsigned)mcd);
    // The same name returns the descriptor it has (38.27).
    vpi_printf("#c reopen same=%d\n",
        vpi_mcd_open((PLI_BYTE8*)"mcd_out.txt") == mcd);
    vpi_printf("#c name mcd=%s\n", text(vpi_mcd_name(mcd)));
    vpi_printf("#c name 1=%s\n", text(vpi_mcd_name(1)));
    // Channel 1 cannot be closed (38.24): the call returns its bit.
    vpi_printf("#c close 1=%u\n", (unsigned)vpi_mcd_close(1));
    if (variable != NULL) {
        value.value.integer = (PLI_INT32)mcd;
        (void)vpi_put_value(variable, &value, NULL, vpiNoDelay);
    }
    if (step != NULL) {
        memset(&callback, 0, sizeof callback);
        callback.reason = cbValueChange;
        callback.cb_rtn = step_changed;
        callback.obj = step;
        callback.time = &time;
        callback.value = &suppress;
        (void)vpi_register_cb(&callback);
    }
    return 0;
}

static PLI_INT32 step_changed(p_cb_data data)
{
    PLI_UINT32 fd = (PLI_UINT32)read_int("top.fd");
    PLI_INT32 step = read_int("top.step");
    (void)data;
    if (step == 1) {
        vpi_printf("#c name fd=%s\n", text(vpi_mcd_name(fd)));
        // An fd is not a channel set: nothing is written (38.28).
        vpi_printf("#c fd mcd_printf=%d\n",
            (int)vpi_mcd_printf(fd, (PLI_BYTE8*)"not written\n"));
        vpi_printf("#c mcd printf=%d\n",
            (int)vpi_mcd_printf(mcd, (PLI_BYTE8*)"from plug-in %d\n", 7));
        // Channel 1 and the file together.
        vpi_printf("#c both printf=%d\n",
            (int)vpi_mcd_printf(1U | mcd, (PLI_BYTE8*)"#c to-both %d\n", 8));
    } else if (step == 2) {
        vpi_printf("#c flush=%d\n", (int)vpi_mcd_flush(mcd));
        vpi_printf("#c close=%u\n", (unsigned)vpi_mcd_close(mcd));
        vpi_printf("#c name closed=%s\n", text(vpi_mcd_name(mcd)));
        // A closed channel is unclosed on a second call (38.24).
        vpi_printf("#c close again=%u\n", (unsigned)vpi_mcd_close(mcd));
        vpi_printf("#c close fd=%u\n", (unsigned)vpi_mcd_close(fd));
    }
    return 0;
}

// The file is read back with stdio from the workspace the run started in.
static PLI_INT32 end_of_simulation(p_cb_data data)
{
    char line[128];
    FILE* file = fopen("mcd_out.txt", "r");
    (void)data;
    if (file == NULL) {
        vpi_printf("#c file missing\n");
        return 0;
    }
    while (fgets(line, sizeof line, file) != NULL) {
        size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\n') {
            line[length - 1] = '\0';
        }
        vpi_printf("#c file: %s\n", line);
    }
    fclose(file);
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start_of_simulation;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };

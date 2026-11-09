// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the value of shared variables of a VHDL-93 design
// (IEEE 1076-1993 4.3.1.3, IEEE 1076-2019 22.3), read at the start of the
// initialization phase, before any process has run (14.7.5.2, 21.3.7.5).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* lower_text(const vhpiCharT* value)
{
    static char buffers[4][64];
    static int next;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 4;
    if (value == NULL) {
        return "-";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffers[0];
         ++index) {
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

static void integer_variable(const char* path)
{
    vhpiHandleT variable = vhpi_handle_by_name(path, NULL);
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    printf("#c %s %s shared=%d", lower_text(vhpi_get_str(vhpiNameP, variable)),
        lower_text(vhpi_get_str(vhpiKindStrP, variable)),
        (int)vhpi_get(vhpiIsSharedP, variable));
    if (vhpi_get_value(variable, &value) == 0) {
        printf(" value=%d\n", (int)value.value.intg);
    } else {
        printf(" value=?\n");
    }
}

static void vector_variable(const char* path)
{
    vhpiHandleT variable = vhpi_handle_by_name(path, NULL);
    vhpiValueT value;
    char text[16];
    memset(&value, 0, sizeof value);
    memset(text, 0, sizeof text);
    value.format = vhpiBinStrVal;
    value.bufSize = sizeof text;
    value.value.str = (vhpiCharT*)text;
    printf("#c %s %s shared=%d", lower_text(vhpi_get_str(vhpiNameP, variable)),
        lower_text(vhpi_get_str(vhpiKindStrP, variable)),
        (int)vhpi_get(vhpiIsSharedP, variable));
    if (vhpi_get_value(variable, &value) == 0) {
        printf(" value=%s\n", text);
    } else {
        printf(" value=?\n");
    }
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    integer_variable("top.sv");
    vector_variable("top.sw");
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfInitialization;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

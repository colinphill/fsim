// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for vhpiDerefObj of a variable of an access type (IEEE
// 1076-2019 19.8, 19.4.3, 22.3). Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* lowered(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffer[128];
    const vhpiCharT* value = object != NULL ? vhpi_get_str(property, object) : NULL;
    size_t index;
    if (value == NULL) {
        return "error";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffer; ++index) {
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT pointer = vhpi_handle_by_name("top.main.ptr", NULL);
    vhpiHandleT count = vhpi_handle_by_name("top.main.count", NULL);
    vhpiHandleT deref = vhpi_handle(vhpiDerefObj, pointer);
    vhpiValueT value;
    (void)data;
    if (deref == NULL) {
        printf("#c deref none\n");
    } else {
        printf("#c deref kind=%s\n", (const char*)vhpi_get_str(vhpiKindStrP, deref));
        printf("#c deref type=%s\n",
            lowered(vhpi_handle(vhpiType, deref), vhpiNameP));
        printf("#c deref prefix=%s\n",
            lowered(vhpi_handle(vhpiPrefix, deref), vhpiNameP));
        printf("#c deref name=%s\n", lowered(deref, vhpiNameP));
        memset(&value, 0, sizeof value);
        value.format = vhpiIntVal;
        printf("#c deref value=%s\n",
            vhpi_get_value(deref, &value) != 0 ? "error" : "read");
    }
    printf("#c count deref=%s\n",
        vhpi_handle(vhpiDerefObj, count) == NULL ? "none" : "some");
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

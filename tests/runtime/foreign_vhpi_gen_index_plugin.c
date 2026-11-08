// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for vhpiGenIndex of a block configuration whose generate index
// is a static expression of constants, not a literal (IEEE 1076-2019 3.4.2,
// 19.5, Figure 3). Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[8][128];
    static int next;
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 8;
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

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT unit = vhpi_handle(vhpiDesignUnit, root);
    vhpiHandleT library = vhpi_handle(vhpiLibraryDecl, unit);
    vhpiHandleT units = vhpi_iterator(vhpiDesignUnits, library);
    vhpiHandleT item;
    (void)data;
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        vhpiHandleT block;
        vhpiHandleT items;
        vhpiHandleT sub;
        if (vhpi_get(vhpiKindP, item) != vhpiConfigDeclK) {
            continue;
        }
        block = vhpi_handle(vhpiBlockConfig, item);
        items = vhpi_iterator(vhpiConfigItems, block);
        while (items != NULL && (sub = vhpi_scan(items)) != NULL) {
            vhpiHandleT index;
            vhpiValueT value;
            if (vhpi_get(vhpiKindP, sub) != vhpiBlockConfigK) {
                continue;
            }
            index = vhpi_handle(vhpiGenIndex, sub);
            memset(&value, 0, sizeof value);
            value.format = vhpiIntVal;
            if (index == NULL || vhpi_get_value(index, &value) != 0) {
                printf("#c genindex spec=%s index=-\n",
                    text_property(sub, vhpiSpecNameP));
            } else {
                printf("#c genindex spec=%s index=%d\n",
                    text_property(sub, vhpiSpecNameP), (int)value.value.intg);
            }
        }
    }
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

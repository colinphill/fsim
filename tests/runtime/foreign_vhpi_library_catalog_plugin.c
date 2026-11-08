// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the design units of a library (IEEE 1076-2019 19.4.5):
// every unit analyzed into the library is listed, those the design does not
// use included. The "name kind" lines are sorted so the trace does not
// depend on the order of the library. Lines starting with "#c " form the
// checked trace; the first line checks vhpiUse of the iterator.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAX_UNITS 64U

static void start(const vhpiCbDataT* data)
{
    static char lines[MAX_UNITS][96];
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT unit = vhpi_handle(vhpiDesignUnit, root);
    vhpiHandleT library = vhpi_handle(vhpiLibraryDecl, unit);
    vhpiHandleT units = vhpi_iterator(vhpiDesignUnits, library);
    vhpiHandleT item;
    vhpiHandleT base;
    const vhpiCharT* base_name;
    size_t count = 0U;
    size_t index;
    size_t other;
    (void)data;
    // The iterator's +Use association leads to the object it iterates from
    // (Figure 55): the library.
    base = vhpi_handle(vhpiUse, units);
    base_name = base != NULL ? vhpi_get_str(vhpiNameP, base) : NULL;
    printf("#c use ");
    for (index = 0U; base_name != NULL && base_name[index] != 0; ++index) {
        printf("%c", tolower(base_name[index]));
    }
    printf("\n");
    while (units != NULL && (item = vhpi_scan(units)) != NULL
        && count < MAX_UNITS) {
        const vhpiCharT* name = vhpi_get_str(vhpiNameP, item);
        const vhpiCharT* kind = vhpi_get_str(vhpiKindStrP, item);
        char* line = lines[count++];
        size_t length = 0U;
        for (index = 0U; name != NULL && name[index] != 0 && length < 40U; ++index) {
            line[length++] = (char)tolower(name[index]);
        }
        line[length++] = ' ';
        for (index = 0U; kind != NULL && kind[index] != 0 && length < 90U; ++index) {
            line[length++] = (char)tolower(kind[index]);
        }
        line[length] = 0;
    }
    for (index = 0U; index < count; ++index) {
        for (other = index + 1U; other < count; ++other) {
            if (strcmp(lines[other], lines[index]) < 0) {
                char swap[96];
                memcpy(swap, lines[index], sizeof swap);
                memcpy(lines[index], lines[other], sizeof swap);
                memcpy(lines[other], swap, sizeof swap);
            }
        }
    }
    for (index = 0U; index < count; ++index) {
        printf("#c %s\n", lines[index]);
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

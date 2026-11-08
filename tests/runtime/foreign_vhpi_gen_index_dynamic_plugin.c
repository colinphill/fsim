// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for vhpiGenerateIndexP of the occurrences of a for-generate
// statement whose range depends on a generic (IEEE 1076-2019 11.8, 19.4.2,
// Figure 38). Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT statements = vhpi_iterator(vhpiStmts, root);
    vhpiHandleT statement;
    int indexes[16];
    int count = 0;
    int first;
    int second;
    (void)data;
    while (statements != NULL && (statement = vhpi_scan(statements)) != NULL) {
        if (vhpi_get(vhpiKindP, statement) == vhpiForGenerateK && count < 16) {
            indexes[count++] = (int)vhpi_get(vhpiGenerateIndexP, statement);
        }
    }
    /* The order of the occurrences is not specified: sort the indexes. */
    for (first = 0; first < count; ++first) {
        for (second = first + 1; second < count; ++second) {
            if (indexes[second] < indexes[first]) {
                int swap = indexes[first];
                indexes[first] = indexes[second];
                indexes[second] = swap;
            }
        }
    }
    printf("#c occurrences=%d\n", count);
    for (first = 0; first < count; ++first) {
        printf("#c index=%d\n", indexes[first]);
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

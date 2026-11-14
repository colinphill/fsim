// SPDX-License-Identifier: Apache-2.0
// Drivers of nets (IEEE 1800-2023 37.21, 37.35): for each net and each bit
// of a vector net, the number of vpiDriver objects and their types. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static void drivers(const char* label, vpiHandle net)
{
    vpiHandle list = net != NULL ? vpi_iterate(vpiDriver, net) : NULL;
    vpiHandle driver;
    int primitive_terms = 0;
    int others = 0;
    while (list != NULL && (driver = vpi_scan(list)) != NULL) {
        if (vpi_get(vpiType, driver) == vpiPrimTerm) {
            ++primitive_terms;
        } else {
            ++others;
        }
    }
    vpi_printf("#c %s prim-term-drivers=%d other-drivers=%d\n", label,
        primitive_terms, others);
}

static void net(const char* name, int bits)
{
    vpiHandle object = vpi_handle_by_name((PLI_BYTE8*)name, NULL);
    int bit;
    drivers(name, object);
    for (bit = 0; object != NULL && bits > 1 && bit < bits; ++bit) {
        char label[64];
        snprintf(label, sizeof label, "%s[%d]", name, bit);
        drivers(label, vpi_handle_by_index(object, bit));
    }
}

static PLI_INT32 report(p_cb_data data)
{
    (void)data;
    net("top.v", 4);
    net("top.w", 1);
    net("top.s", 1);
    net("top.p", 2);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = report;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };

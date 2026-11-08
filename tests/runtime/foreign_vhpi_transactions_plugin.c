// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for vhpiTransactions of a driver (IEEE 1076-2019 14.7.2,
// 19.12.1, Figure 48): after the initialization phase it lists the
// transactions of the drivers of some signals with their time component
// (vhpi_get_phys, vhpiTimeP), IsNull and value component (vhpi_get_value).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

// Lists the transactions of every driver of the signal named `name`; `integer`
// reads integer values, else bit values by position.
static void transactions(const char* name, int integer)
{
    vhpiHandleT signal = vhpi_handle_by_name(name, NULL);
    vhpiHandleT drivers = vhpi_iterator(vhpiDrivers, signal);
    vhpiHandleT driver;
    int count = 0;
    while (drivers != NULL && vhpi_scan(drivers) != NULL) {
        ++count;
    }
    printf("#c %s drivers=%d\n", name, count);
    drivers = vhpi_iterator(vhpiDrivers, signal);
    while (drivers != NULL && (driver = vhpi_scan(drivers)) != NULL) {
        vhpiHandleT iterator = vhpi_iterator(vhpiTransactions, driver);
        vhpiHandleT transaction;
        int seen = 0;
        while (iterator != NULL && (transaction = vhpi_scan(iterator)) != NULL) {
            vhpiPhysT time = vhpi_get_phys(vhpiTimeP, transaction);
            long long femtoseconds
                = ((long long)time.high << 32) | (long long)time.low;
            vhpiValueT value;
            int result;
            memset(&value, 0, sizeof value);
            value.format = integer ? vhpiIntVal : vhpiEnumVal;
            result = vhpi_get_value(transaction, &value);
            printf("#c   tx time=%lld ns null=%d value=%d\n",
                femtoseconds / 1000000LL,
                (int)vhpi_get(vhpiIsNullP, transaction),
                result != 0 ? -1
                            : (integer ? (int)value.value.intg
                                       : (int)value.value.enumv));
            ++seen;
        }
        if (seen == 0) {
            printf("#c   none\n");
        }
    }
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    transactions("top.s", 0);
    transactions("top.v(1)", 0);
    transactions("top.v(0)", 0);
    transactions("top.n", 1);
    transactions("top.q", 0);
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbEndOfInitialization;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };

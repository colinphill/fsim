// SPDX-License-Identifier: Apache-2.0
// VHPI reads and writes of VHDL-2019 INTEGER values (IEEE 1076-2019 5.2.3.1:
// INTEGER covers at least -(2**63)+1 to (2**63)-1). The native format of an
// integer type is vhpiIntVal or vhpiLongIntVal, and vhpiObjTypeVal may give
// either only if the type's range fits the format (22.4); a vhpiIntT holds
// the value itself (22.2.3), so a value outside its range is not read
// truncated. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static const char* format_name(vhpiFormatT format)
{
    switch (format) {
    case vhpiIntVal:
        return "IntVal";
    case vhpiLongIntVal:
        return "LongIntVal";
    case vhpiIntVecVal:
        return "IntVecVal";
    case vhpiLongIntVecVal:
        return "LongIntVecVal";
    default:
        return "other";
    }
}

static void native(const char* name)
{
    vhpiHandleT object = vhpi_handle_by_name(name, NULL);
    vhpiValueT value;
    vhpiLongIntT elements[3];
    int status;
    memset(&value, 0, sizeof value);
    value.format = vhpiObjTypeVal;
    value.bufSize = sizeof elements;
    value.value.longintgs = elements;
    status = vhpi_get_value(object, &value);
    if (value.format == vhpiLongIntVal) {
        printf("#c %s native %s status=%d value=%lld\n", name,
            format_name(value.format), status, (long long)value.value.longintg);
    } else if (value.format == vhpiIntVal) {
        printf("#c %s native %s status=%d value=%d\n", name,
            format_name(value.format), status, (int)value.value.intg);
    } else {
        printf("#c %s native %s status=%d\n", name, format_name(value.format),
            status);
    }
}

static void as_int(const char* name)
{
    vhpiHandleT object = vhpi_handle_by_name(name, NULL);
    vhpiValueT value;
    int status;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    status = vhpi_get_value(object, &value);
    if (status == 0) {
        printf("#c %s IntVal status=0 value=%d\n", name, (int)value.value.intg);
    } else {
        printf("#c %s IntVal status=%d\n", name, status);
    }
}

static void vectors(void)
{
    vhpiHandleT object = vhpi_handle_by_name("top.v", NULL);
    vhpiValueT value;
    vhpiLongIntT longs[3];
    vhpiIntT ints[3];
    int status;
    memset(&value, 0, sizeof value);
    value.format = vhpiLongIntVecVal;
    value.bufSize = sizeof longs;
    value.value.longintgs = longs;
    status = vhpi_get_value(object, &value);
    printf("#c top.v LongIntVecVal status=%d n=%d %lld %lld %lld\n", status,
        (int)value.numElems, (long long)longs[0], (long long)longs[1],
        (long long)longs[2]);
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVecVal;
    value.bufSize = sizeof ints;
    value.value.intgs = ints;
    status = vhpi_get_value(object, &value);
    printf("#c top.v IntVecVal status=%d\n", status);
}

static void after(const vhpiCbDataT* data)
{
    (void)data;
    native("top.big");
    native("top.v");
    fflush(stdout);
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT big = vhpi_handle_by_name("top.big", NULL);
    vhpiHandleT v = vhpi_handle_by_name("top.v", NULL);
    vhpiValueT value;
    vhpiLongIntT longs[3] = { -(1LL << 62), 3, 1LL << 50 };
    vhpiCbDataT next;
    vhpiTimeT time;
    (void)data;
    native("top.big");
    native("top.small");
    native("top.narrow");
    as_int("top.big");
    as_int("top.small");
    vectors();
    memset(&value, 0, sizeof value);
    value.format = vhpiLongIntVal;
    value.value.longintg = (1LL << 40) * 3;
    printf("#c put top.big status=%d\n",
        vhpi_put_value(big, &value, vhpiDepositPropagate));
    memset(&value, 0, sizeof value);
    value.format = vhpiLongIntVecVal;
    value.bufSize = sizeof longs;
    value.value.longintgs = longs;
    printf("#c put top.v status=%d\n",
        vhpi_put_value(v, &value, vhpiDepositPropagate));
    fflush(stdout);
    memset(&next, 0, sizeof next);
    time.high = 0;
    time.low = 1000000U;
    next.reason = vhpiCbAfterDelay;
    next.cb_rtn = after;
    next.time = &time;
    (void)vhpi_register_cb(&next, 0);
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

// SPDX-License-Identifier: Apache-2.0
// DPI arrays of strings conformance plug-in (IEEE 1800-2023 35.5.6,
// 35.5.6.1, H.7.4, H.12): each element is a `const char*`; a sized array
// passes a pointer to its elements, lowest index first, and an open array
// an svOpenArrayHandle. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "svdpi.h"

#include <stdio.h>

static const char** element(const svOpenArrayHandle h, const int index)
{
    return (const char**)svGetArrElemPtr1(h, index);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_s_sized(
    const char* const* s, const char** o)
{
    printf("#c sized in %s,%s,%s\n", s[0], s[1], s[2]);
    o[0] = "out0";
    o[1] = "out1";
    fflush(stdout);
    return 3;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_s_open(
    const svOpenArrayHandle d, const svOpenArrayHandle m)
{
    int index;
    int column;
    printf("#c open d dimensions=%d left=%d right=%d low=%d high=%d size=%d\n",
        svDimensions(d), svLeft(d, 1), svRight(d, 1), svLow(d, 1),
        svHigh(d, 1), svSize(d, 1));
    for (index = svLow(d, 1); index <= svHigh(d, 1); ++index) {
        printf("#c open d[%d]=%s\n", index, *element(d, index));
    }
    printf("#c open m dimensions=%d\n", svDimensions(m));
    for (index = svLow(m, 1); index <= svHigh(m, 1); ++index) {
        for (column = svLow(m, 2); column <= svHigh(m, 2); ++column) {
            printf("#c open m[%d][%d]=%s\n", index, column,
                *(const char**)svGetArrElemPtr2(m, index, column));
        }
    }
    fflush(stdout);
    return svSize(d, 1) + svSize(m, 1) * svSize(m, 2);
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_s_swap(
    const svOpenArrayHandle x, const char** y)
{
    const char** first = element(x, svLow(x, 1));
    const char** second = element(x, svHigh(x, 1));
    const char* held = *first;
    *first = *second;
    *second = held;
    held = y[0];
    y[0] = y[1];
    y[1] = held;
    printf("#c swap in x=%s,%s y=%s,%s\n", *first, *second, y[0], y[1]);
    fflush(stdout);
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_s_fill(
    const svOpenArrayHandle r)
{
    *element(r, svLow(r, 1)) = "low";
    *element(r, svHigh(r, 1)) = "high";
}

/* A dynamic array or queue of strings as the actual of an open-array formal
   (35.5.6.1, H.12.2): the size is the actual's at the call, bounds 0 upwards;
   the strings the C code writes copy back in output and inout. */
FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_s_dyn(
    const svOpenArrayHandle q, const svOpenArrayHandle io,
    const svOpenArrayHandle o)
{
    static const char* const names[] = { "n0", "n1", "n2", "n3" };
    int index;
    printf("#c dyn q size=%d left=%d right=%d\n", svSize(q, 1), svLeft(q, 1),
        svRight(q, 1));
    for (index = svLow(q, 1); index <= svHigh(q, 1); ++index) {
        printf("#c dyn q[%d]=%s\n", index, *element(q, index));
    }
    for (index = svLow(io, 1); index <= svHigh(io, 1); ++index) {
        printf("#c dyn io[%d]=%s\n", index, *element(io, index));
    }
    *element(io, svLow(io, 1)) = "first";
    *element(io, svHigh(io, 1)) = "last";
    printf("#c dyn o size=%d\n", svSize(o, 1));
    for (index = svLow(o, 1); index <= svHigh(o, 1); ++index) {
        *element(o, index) = names[index - svLow(o, 1)];
    }
    fflush(stdout);
    return svSize(q, 1) + svSize(io, 1) + svSize(o, 1);
}

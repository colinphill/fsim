// SPDX-License-Identifier: Apache-2.0
// DPI argument type conformance plug-in (IEEE 1800-2023 35.5.6, 35.5.6.1,
// H.7.4, H.7.6, H.12): packed ranges that are constant expressions, open
// packed formals, sized unpacked arrays in their normalized layout, and
// arrays of real, shortreal, chandle and enumeration elements. Lines
// starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "svdpi.h"

#include <stdio.h>

static int marker;

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_t_range(
    const svBitVecVal* v, const svLogicVecVal* l)
{
    printf("#c range v=%x l aval=%x bval=%x\n", (unsigned)v[0],
        (unsigned)l[0].aval, (unsigned)l[0].bval);
    fflush(stdout);
    return (int)v[0];
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_t_open_packed(
    const svOpenArrayHandle h)
{
    const svBitVecVal* const data = (const svBitVecVal*)svGetArrayPtr(h);
    printf("#c open_packed dimensions=%d left=%d right=%d size=%d\n",
        svDimensions(h), svLeft(h, 0), svRight(h, 0), svSize(h, 0));
    fflush(stdout);
    return (int)data[0];
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_t_open_mixed(
    const svOpenArrayHandle h)
{
    svLogicVecVal element;
    int index;
    printf("#c open_mixed dimensions=%d packed=%d:%d unpacked=%d:%d size=%d\n",
        svDimensions(h), svLeft(h, 0), svRight(h, 0), svLeft(h, 1),
        svRight(h, 1), svSize(h, 1));
    for (index = svHigh(h, 1); index >= svLow(h, 1); --index) {
        svGetLogicArrElem1VecVal(&element, h, index);
        printf("#c open_mixed [%d] aval=%x bval=%x\n", index,
            (unsigned)element.aval, (unsigned)element.bval);
    }
    fflush(stdout);
    return svSize(h, 1);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_t_sized(
    const int* a, svBitVecVal* b, int* m)
{
    int index;
    // Normalized: the lowest SystemVerilog index is C index 0.
    printf("#c sized a=%d,%d,%d,%d\n", a[0], a[1], a[2], a[3]);
    printf("#c sized m=%d,%d,%d,%d,%d,%d\n", m[0], m[1], m[2], m[3], m[4],
        m[5]);
    b[0] = 0x11u;
    b[1] = 0x22u;
    for (index = 0; index < 6; ++index) {
        m[index] += 100;
    }
    fflush(stdout);
    return 7;
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_t_scalars(
    const double* r, float* f, void* const* h, void** o, const int* c)
{
    printf("#c scalars r=%.1f,%.1f,%.1f\n", r[0], r[1], r[2]);
    printf("#c scalars h=%d,%d\n", h[0] == NULL, h[1] == NULL);
    printf("#c scalars c=%d,%d,%d\n", c[0], c[1], c[2]);
    fflush(stdout);
    f[0] = 0.25f;
    f[1] = 0.5f;
    o[0] = &marker;
    o[1] = NULL;
}

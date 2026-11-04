// SPDX-License-Identifier: Apache-2.0
// DPI unpacked aggregate conformance plug-in (IEEE 1800-2023 35.5.3,
// 35.5.6, 35.5.6.1, H.7.8): an unpacked structure in the C compiler's
// layout, sized and open arrays of it, output and inout open arrays sized
// by a dynamic array or queue actual, and exports with sized unpacked and
// structure formals. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "svdpi.h"

#include <stdio.h>

typedef struct {
    int a;
    char b;
    double r;
    svBitVecVal v;
    int arr[2];
} st_t;

static void show(const char* const tag, const st_t* const s)
{
    printf("#c %s a=%d b=%d r=%.1f v=%x arr=%d,%d\n", tag, s->a, s->b, s->r,
        (unsigned)s->v, s->arr[0], s->arr[1]);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_a_struct(
    const st_t* s, st_t* o)
{
    show("struct in", s);
    o->a = s->a * 2;
    o->b = (char)(s->b + 1);
    o->r = s->r + 0.5;
    o->v = s->v ^ 0x3FFu;
    o->arr[0] = s->arr[1];
    o->arr[1] = s->arr[0];
    fflush(stdout);
    return 1;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_a_struct_sized(
    const st_t* s)
{
    show("sized [0]", &s[0]);
    show("sized [1]", &s[1]);
    fflush(stdout);
    return 2;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_a_struct_open(
    const svOpenArrayHandle h)
{
    int index;
    printf("#c open dimensions=%d low=%d high=%d\n", svDimensions(h),
        svLow(h, 1), svHigh(h, 1));
    for (index = svLow(h, 1); index <= svHigh(h, 1); ++index) {
        printf("#c open [%d] a=%d\n", index,
            ((const st_t*)svGetArrElemPtr1(h, index))->a);
    }
    fflush(stdout);
    return svSize(h, 1);
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_a_dynamic(
    const svOpenArrayHandle d, const svOpenArrayHandle q,
    const svOpenArrayHandle rows)
{
    int index;
    int column;
    printf("#c dynamic sizes d=%d q=%d rows=%d\n", svSize(d, 1),
        svSize(q, 1), svSize(rows, 1));
    for (index = svLow(d, 1); index <= svHigh(d, 1); ++index) {
        *(int*)svGetArrElemPtr1(d, index) *= 2;
    }
    for (index = svLow(q, 1); index <= svHigh(q, 1); ++index) {
        svBitVecVal value = 0x40u + (svBitVecVal)index;
        svPutBitArrElem1VecVal(q, &value, index);
    }
    for (index = svLow(rows, 1); index <= svHigh(rows, 1); ++index) {
        for (column = svLow(rows, 2); column <= svHigh(rows, 2); ++column) {
            *(int*)svGetArrElemPtr2(rows, index, column) += 10;
        }
    }
    fflush(stdout);
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_a_run(void)
{
    int v[3] = { 10, 20, 30 };
    int w[2] = { 0, 0 };
    void* sized_arguments[] = { v, w };
    st_t s = { 9, 2, 1.5, 0x2Au, { 100, 200 } };
    void* struct_arguments[] = { &s };
    int result = 0;
    (void)fsim_svdpi_export_call_v3(
        "dpi_a_export_sized", sized_arguments, 0);
    printf("#c run w=%d,%d\n", w[0], w[1]);
    fflush(stdout);
    (void)fsim_svdpi_export_call_v3(
        "dpi_a_export_struct", struct_arguments, &result);
    printf("#c run struct result=%d\n", result);
    fflush(stdout);
}

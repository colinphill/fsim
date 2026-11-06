// SPDX-License-Identifier: Apache-2.0
// DPI scalar, string and packed vector argument conformance plug-in (IEEE
// 1800-2023 35.5.5, H.7.4, H.10.1): byte, shortint, int, longint, real,
// shortreal, chandle, string, bit, logic, and packed vectors as svBitVecVal
// and svLogicVecVal. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/dpi_plugin_abi.h"
#include "svdpi.h"

#include <stdio.h>

static int marker;
static char out_text[] = "out";
static char result_text[] = "ret:hello";

FSIM_DPI_PLUGIN_EXPORT signed char FSIM_DPI_PLUGIN_CALL dpi_m_byte(
    signed char a, signed char* o, signed char* b)
{
    printf("#c byte a=%d b=%d\n", a, *b);
    fflush(stdout);
    *o = (signed char)(a + 1);
    *b = (signed char)(*b * 2);
    return (signed char)(a - 100);
}

FSIM_DPI_PLUGIN_EXPORT short FSIM_DPI_PLUGIN_CALL dpi_m_short(
    short a, short* o, short* b)
{
    printf("#c short a=%d b=%d\n", a, *b);
    fflush(stdout);
    *o = (short)(a + 1);
    *b = (short)(*b * 2);
    return (short)(a - 1000);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_m_int(
    int a, int* o, int* b)
{
    printf("#c int a=%d b=%d\n", a, *b);
    fflush(stdout);
    *o = a + 1;
    *b = *b * 3;
    return a - 1;
}

FSIM_DPI_PLUGIN_EXPORT long long FSIM_DPI_PLUGIN_CALL dpi_m_long(
    long long a, long long* o, long long* b)
{
    printf("#c long a=%lld b=%lld\n", a, *b);
    fflush(stdout);
    *o = a + 1;
    *b = *b + 1;
    return a * 2;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_m_unsigned(
    unsigned char a, unsigned short b, unsigned int c, unsigned long long d)
{
    printf("#c unsigned a=%u b=%u c=%u d=%llu\n", (unsigned)a, (unsigned)b, c,
        d);
    fflush(stdout);
    return (int)((unsigned)a + (unsigned)b);
}

FSIM_DPI_PLUGIN_EXPORT double FSIM_DPI_PLUGIN_CALL dpi_m_real(
    double a, double* o, double* b)
{
    const double in = *b;
    printf("#c real a=%.3f b=%.3f\n", a, in);
    fflush(stdout);
    *o = a * 2.0;
    *b = in + 0.125;
    return a + in;
}

FSIM_DPI_PLUGIN_EXPORT float FSIM_DPI_PLUGIN_CALL dpi_m_shortreal(
    float a, float* o, float* b)
{
    printf("#c shortreal a=%.3f b=%.3f\n", (double)a, (double)*b);
    fflush(stdout);
    *o = a * 2.0f;
    *b = *b + 0.25f;
    return a + 3.25f;
}

FSIM_DPI_PLUGIN_EXPORT void* FSIM_DPI_PLUGIN_CALL dpi_m_chandle(
    void* a, void** o)
{
    printf("#c chandle a=%d\n", a == NULL ? 0 : (a == &marker ? 2 : 1));
    fflush(stdout);
    *o = a;
    return &marker;
}

FSIM_DPI_PLUGIN_EXPORT const char* FSIM_DPI_PLUGIN_CALL dpi_m_string(
    const char* a, const char** o)
{
    printf("#c string a=%s\n", a);
    fflush(stdout);
    *o = out_text;
    return result_text;
}

FSIM_DPI_PLUGIN_EXPORT svBit FSIM_DPI_PLUGIN_CALL dpi_m_bit(
    svBit a, svBit* o, svBit* b)
{
    printf("#c bit a=%d b=%d\n", (int)a, (int)*b);
    fflush(stdout);
    *o = a;
    *b = (svBit)!*b;
    return (svBit)!a;
}

FSIM_DPI_PLUGIN_EXPORT svLogic FSIM_DPI_PLUGIN_CALL dpi_m_logic(
    svLogic a, svLogic* o, svLogic* b)
{
    printf("#c logic a=%d b=%d\n", (int)a, (int)*b);
    fflush(stdout);
    *o = sv_z;
    *b = sv_x;
    return sv_1;
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_m_bits(
    const svBitVecVal* a, const svBitVecVal* b, const svBitVecVal* c,
    svBitVecVal* o, svBitVecVal* io)
{
    printf("#c bits a=%x b=%x,%x c=%x,%x,%x,%x io=%x\n", (unsigned)a[0],
        (unsigned)b[0], (unsigned)b[1] & 1u, (unsigned)c[0], (unsigned)c[1],
        (unsigned)c[2], (unsigned)c[3] & 0xfu, (unsigned)io[0]);
    fflush(stdout);
    o[0] = 0xdeadbeefu;
    o[1] = 0x12345678u;
    o[2] = 0x9abcdef0u;
    // Bits past the declared width are not part of the value.
    o[3] = 0xfffffff7u;
    io[0] = (io[0] ^ 0x1fffu);
    return (int)(a[0] + 1u);
}

FSIM_DPI_PLUGIN_EXPORT int FSIM_DPI_PLUGIN_CALL dpi_m_logics(
    const svLogicVecVal* a, const svLogicVecVal* b, svLogicVecVal* o,
    svLogicVecVal* io)
{
    printf("#c logics a aval=%x bval=%x\n", (unsigned)a[0].aval,
        (unsigned)a[0].bval);
    printf("#c logics b aval=%x,%x,%x bval=%x\n", (unsigned)b[0].aval,
        (unsigned)b[1].aval, (unsigned)b[2].aval & 1u, (unsigned)b[0].bval);
    printf("#c logics io aval=%x bval=%x\n", (unsigned)io[0].aval,
        (unsigned)io[0].bval);
    fflush(stdout);
    o[0].aval = 0xA5A5A50Fu;
    o[0].bval = 0x000000FFu;
    o[1].aval = 0u;
    o[1].bval = 0u;
    // Only bit 64 belongs to the value; the rest of the word is ignored.
    o[2].aval = 0x3u;
    o[2].bval = 0x2u;
    io[0].aval = 0x3u;
    io[0].bval = 0xAu;
    return 7;
}

FSIM_DPI_PLUGIN_EXPORT void FSIM_DPI_PLUGIN_CALL dpi_m_void(int a)
{
    printf("#c void a=%d\n", a);
    fflush(stdout);
}

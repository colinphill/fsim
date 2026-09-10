// SPDX-License-Identifier: Apache-2.0
#ifndef FSIM_SVDPI_H
#define FSIM_SVDPI_H

#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifndef DPI_DLLISPEC
#if defined(FSIM_SVDPI_LINK_SURFACE_BUILD)
#define DPI_DLLISPEC __declspec(dllexport)
#else
#define DPI_DLLISPEC __declspec(dllimport)
#endif
#endif
#ifndef DPI_DLLESPEC
#define DPI_DLLESPEC __declspec(dllexport)
#endif
#else
#ifndef DPI_DLLISPEC
#define DPI_DLLISPEC
#endif
#ifndef DPI_DLLESPEC
#define DPI_DLLESPEC __attribute__((visibility("default")))
#endif
#endif

#ifndef DPI_EXTERN
#define DPI_EXTERN
#endif
#ifndef DPI_PROTOTYPES
#define DPI_PROTOTYPES
#define FSIM_SVDPI_OWNS_PROTOTYPES 1
#define XXTERN DPI_EXTERN DPI_DLLISPEC
#define EETERN DPI_EXTERN DPI_DLLESPEC
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t svScalar;
typedef svScalar svBit;
typedef svScalar svLogic;

#define sv_0 0
#define sv_1 1
#define sv_z 2
#define sv_x 3

#ifndef VPI_VECVAL
#define VPI_VECVAL
typedef struct t_vpi_vecval {
  uint32_t aval;
  uint32_t bval;
} s_vpi_vecval, *p_vpi_vecval;
#endif

typedef s_vpi_vecval svLogicVecVal;
typedef uint32_t svBitVecVal;
typedef void* svScope;
typedef void* svOpenArrayHandle;

#define SV_PACKED_DATA_NELEMS(width) (((width) + 31) >> 5)
#define SV_MASK(bits) (~(UINT32_C(0xffffffff) << (bits)))
#define SV_GET_UNSIGNED_BITS(value, bits) \
  ((bits) == 32 ? (value) : ((value) & SV_MASK(bits)))
#define SV_GET_SIGNED_BITS(value, bits) \
  ((bits) == 32 ? (value) \
                : (((value) & (UINT32_C(1) << (bits))) \
                    ? ((value) | ~SV_MASK(bits)) \
                    : ((value) & SV_MASK(bits))))

#ifndef VPI_TIME
#define VPI_TIME
typedef struct t_vpi_time {
  int32_t type;
  uint32_t high;
  uint32_t low;
  double real;
} s_vpi_time, *p_vpi_time;
#define vpiScaledRealTime 1
#define vpiSimTime 2
#define vpiSuppressTime 3
#endif

typedef s_vpi_time svTimeVal;
#define sv_scaled_real_time vpiScaledRealTime
#define sv_sim_time vpiSimTime

XXTERN const char* svDpiVersion(void);

XXTERN svBit svGetBitselBit(const svBitVecVal* source, int index);
XXTERN svLogic svGetBitselLogic(
    const svLogicVecVal* source, int index);
XXTERN void svPutBitselBit(
    svBitVecVal* destination, int index, svBit value);
XXTERN void svPutBitselLogic(
    svLogicVecVal* destination, int index, svLogic value);
XXTERN void svGetPartselBit(
    svBitVecVal* destination, const svBitVecVal* source,
    int index, int width);
XXTERN void svGetPartselLogic(
    svLogicVecVal* destination, const svLogicVecVal* source,
    int index, int width);
XXTERN void svPutPartselBit(
    svBitVecVal* destination, const svBitVecVal source,
    int index, int width);
XXTERN void svPutPartselLogic(
    svLogicVecVal* destination, const svLogicVecVal source,
    int index, int width);

XXTERN int svLeft(const svOpenArrayHandle handle, int dimension);
XXTERN int svRight(const svOpenArrayHandle handle, int dimension);
XXTERN int svLow(const svOpenArrayHandle handle, int dimension);
XXTERN int svHigh(const svOpenArrayHandle handle, int dimension);
XXTERN int svIncrement(
    const svOpenArrayHandle handle, int dimension);
XXTERN int svSize(const svOpenArrayHandle handle, int dimension);
XXTERN int svDimensions(const svOpenArrayHandle handle);
XXTERN void* svGetArrayPtr(const svOpenArrayHandle handle);
XXTERN int svSizeOfArray(const svOpenArrayHandle handle);
XXTERN void* svGetArrElemPtr(
    const svOpenArrayHandle handle, int index1, ...);
XXTERN void* svGetArrElemPtr1(
    const svOpenArrayHandle handle, int index1);
XXTERN void* svGetArrElemPtr2(
    const svOpenArrayHandle handle, int index1, int index2);
XXTERN void* svGetArrElemPtr3(
    const svOpenArrayHandle handle, int index1, int index2, int index3);

XXTERN void svPutBitArrElemVecVal(
    const svOpenArrayHandle destination, const svBitVecVal* source,
    int index1, ...);
XXTERN void svPutBitArrElem1VecVal(
    const svOpenArrayHandle destination, const svBitVecVal* source,
    int index1);
XXTERN void svPutBitArrElem2VecVal(
    const svOpenArrayHandle destination, const svBitVecVal* source,
    int index1, int index2);
XXTERN void svPutBitArrElem3VecVal(
    const svOpenArrayHandle destination, const svBitVecVal* source,
    int index1, int index2, int index3);
XXTERN void svPutLogicArrElemVecVal(
    const svOpenArrayHandle destination, const svLogicVecVal* source,
    int index1, ...);
XXTERN void svPutLogicArrElem1VecVal(
    const svOpenArrayHandle destination, const svLogicVecVal* source,
    int index1);
XXTERN void svPutLogicArrElem2VecVal(
    const svOpenArrayHandle destination, const svLogicVecVal* source,
    int index1, int index2);
XXTERN void svPutLogicArrElem3VecVal(
    const svOpenArrayHandle destination, const svLogicVecVal* source,
    int index1, int index2, int index3);

XXTERN void svGetBitArrElemVecVal(
    svBitVecVal* destination, const svOpenArrayHandle source,
    int index1, ...);
XXTERN void svGetBitArrElem1VecVal(
    svBitVecVal* destination, const svOpenArrayHandle source, int index1);
XXTERN void svGetBitArrElem2VecVal(
    svBitVecVal* destination, const svOpenArrayHandle source,
    int index1, int index2);
XXTERN void svGetBitArrElem3VecVal(
    svBitVecVal* destination, const svOpenArrayHandle source,
    int index1, int index2, int index3);
XXTERN void svGetLogicArrElemVecVal(
    svLogicVecVal* destination, const svOpenArrayHandle source,
    int index1, ...);
XXTERN void svGetLogicArrElem1VecVal(
    svLogicVecVal* destination, const svOpenArrayHandle source, int index1);
XXTERN void svGetLogicArrElem2VecVal(
    svLogicVecVal* destination, const svOpenArrayHandle source,
    int index1, int index2);
XXTERN void svGetLogicArrElem3VecVal(
    svLogicVecVal* destination, const svOpenArrayHandle source,
    int index1, int index2, int index3);

XXTERN svBit svGetBitArrElem(
    const svOpenArrayHandle source, int index1, ...);
XXTERN svBit svGetBitArrElem1(
    const svOpenArrayHandle source, int index1);
XXTERN svBit svGetBitArrElem2(
    const svOpenArrayHandle source, int index1, int index2);
XXTERN svBit svGetBitArrElem3(
    const svOpenArrayHandle source, int index1, int index2, int index3);
XXTERN svLogic svGetLogicArrElem(
    const svOpenArrayHandle source, int index1, ...);
XXTERN svLogic svGetLogicArrElem1(
    const svOpenArrayHandle source, int index1);
XXTERN svLogic svGetLogicArrElem2(
    const svOpenArrayHandle source, int index1, int index2);
XXTERN svLogic svGetLogicArrElem3(
    const svOpenArrayHandle source, int index1, int index2, int index3);
XXTERN void svPutLogicArrElem(
    const svOpenArrayHandle destination, svLogic value, int index1, ...);
XXTERN void svPutLogicArrElem1(
    const svOpenArrayHandle destination, svLogic value, int index1);
XXTERN void svPutLogicArrElem2(
    const svOpenArrayHandle destination, svLogic value,
    int index1, int index2);
XXTERN void svPutLogicArrElem3(
    const svOpenArrayHandle destination, svLogic value,
    int index1, int index2, int index3);
XXTERN void svPutBitArrElem(
    const svOpenArrayHandle destination, svBit value, int index1, ...);
XXTERN void svPutBitArrElem1(
    const svOpenArrayHandle destination, svBit value, int index1);
XXTERN void svPutBitArrElem2(
    const svOpenArrayHandle destination, svBit value,
    int index1, int index2);
XXTERN void svPutBitArrElem3(
    const svOpenArrayHandle destination, svBit value,
    int index1, int index2, int index3);

XXTERN svScope svGetScope(void);
XXTERN svScope svSetScope(const svScope scope);
XXTERN const char* svGetNameFromScope(const svScope scope);
XXTERN svScope svGetScopeFromName(const char* scope_name);
XXTERN int svPutUserData(
    const svScope scope, void* user_key, void* user_data);
XXTERN void* svGetUserData(const svScope scope, void* user_key);
XXTERN int svGetCallerInfo(
    const char** file_name, int* line_number);
XXTERN int svIsDisabledState(void);
XXTERN void svAckDisabledState(void);
XXTERN int svGetTime(const svScope scope, svTimeVal* time_value);
XXTERN int svGetTimeUnit(const svScope scope, int32_t* time_unit);
XXTERN int svGetTimePrecision(
    const svScope scope, int32_t* time_precision);

#ifdef FSIM_SVDPI_OWNS_PROTOTYPES
#undef EETERN
#undef XXTERN
#undef FSIM_SVDPI_OWNS_PROTOTYPES
#undef DPI_PROTOTYPES
#endif
#undef DPI_EXTERN

#ifdef __cplusplus
}
#endif

#endif

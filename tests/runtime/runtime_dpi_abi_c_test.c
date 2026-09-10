// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"

#include <stddef.h>

#define FSIM_DPI_OFFSET(field, expected)                              \
    _Static_assert(                                                   \
        offsetof(fsim_dpi_plugin_descriptor_v1, field) == (expected), \
        "DPI descriptor " #field " offset")

_Static_assert(FSIM_DPI_PLUGIN_ABI_VERSION == 1u, "DPI ABI version");
_Static_assert(sizeof(void*) == 8u, "DPI ABI requires x86-64 pointers");
_Static_assert(sizeof(svScalar) == 1u, "DPI scalar layout");
_Static_assert(sizeof(svBit) == 1u, "DPI bit layout");
_Static_assert(sizeof(svLogic) == 1u, "DPI logic layout");
_Static_assert(sizeof(svBitVecVal) == 4u, "DPI bit-vector word layout");
_Static_assert(sizeof(svLogicVecVal) == 8u, "DPI logic-vector word layout");
_Static_assert(_Alignof(svLogicVecVal) == 4u, "DPI logic-vector alignment");
_Static_assert(offsetof(svLogicVecVal, aval) == 0u, "DPI aval offset");
_Static_assert(offsetof(svLogicVecVal, bval) == 4u, "DPI bval offset");
_Static_assert(sizeof(svTimeVal) == 24u, "DPI time-value layout");
_Static_assert(_Alignof(svTimeVal) == 8u, "DPI time-value alignment");
_Static_assert(offsetof(svTimeVal, type) == 0u, "DPI time type offset");
_Static_assert(offsetof(svTimeVal, high) == 4u, "DPI time high offset");
_Static_assert(offsetof(svTimeVal, low) == 8u, "DPI time low offset");
_Static_assert(offsetof(svTimeVal, real) == 16u, "DPI real time offset");
_Static_assert(
    FSIM_SVDPI_CONTEXT_ABI_VERSION == 3u, "DPI context ABI version");
_Static_assert(
    sizeof(fsim_svdpi_call_context_v3) == 112u,
    "DPI context bridge layout");
_Static_assert(
    offsetof(fsim_svdpi_call_context_v3, get_time_precision) == 104u,
    "DPI context bridge final callback offset");
_Static_assert(sv_0 == 0 && sv_1 == 1 && sv_z == 2 && sv_x == 3,
    "DPI four-state identities");
_Static_assert(sv_scaled_real_time == 1 && sv_sim_time == 2,
    "DPI time-kind identities");
_Static_assert(SV_PACKED_DATA_NELEMS(0) == 0,
    "empty DPI packed vectors require no words");
_Static_assert(SV_PACKED_DATA_NELEMS(1) == 1,
    "one-bit DPI packed vectors require one word");
_Static_assert(SV_PACKED_DATA_NELEMS(33) == 2,
    "DPI packed vectors round up to 32-bit words");
_Static_assert(
    sizeof(fsim_dpi_plugin_descriptor_v1) == 32u,
    "DPI descriptor x86-64 layout");
_Static_assert(
    _Alignof(fsim_dpi_plugin_descriptor_v1) == 8u,
    "DPI descriptor x86-64 alignment");
FSIM_DPI_OFFSET(abi_version, 0u);
FSIM_DPI_OFFSET(struct_size, 4u);
FSIM_DPI_OFFSET(pointer_bits, 8u);
FSIM_DPI_OFFSET(flags, 12u);
FSIM_DPI_OFFSET(name_size, 16u);
FSIM_DPI_OFFSET(name, 24u);

typedef const fsim_dpi_plugin_descriptor_v1*(FSIM_DPI_PLUGIN_CALL* fsim_dpi_expected_descriptor_get_fn)(void);
_Static_assert(
    _Generic((fsim_dpi_plugin_descriptor_v1_get_fn)0,
        fsim_dpi_expected_descriptor_get_fn: 1,
        default: 0),
    "DPI descriptor getter C signature");

typedef const char* (*fsim_svdpi_version_fn)(void);
typedef svBit (*fsim_svdpi_get_bit_fn)(const svBitVecVal*, int);
typedef void (*fsim_svdpi_put_logic_fn)(
    svLogicVecVal*, int, svLogic);
typedef void* (*fsim_svdpi_element_fn)(
    const svOpenArrayHandle, int, int, int);
typedef svScope (*fsim_svdpi_set_scope_fn)(const svScope);
typedef int (*fsim_svdpi_caller_fn)(const char**, int*);
typedef int (*fsim_svdpi_time_fn)(const svScope, svTimeVal*);
typedef int (*fsim_svdpi_time_scale_fn)(const svScope, int32_t*);
_Static_assert(_Generic(&svDpiVersion, fsim_svdpi_version_fn: 1, default: 0),
    "DPI version C signature");
_Static_assert(_Generic(&svGetBitselBit,
    fsim_svdpi_get_bit_fn: 1, default: 0),
    "DPI bit select C signature");
_Static_assert(_Generic(&svPutBitselLogic,
    fsim_svdpi_put_logic_fn: 1, default: 0),
    "DPI logic select C signature");
_Static_assert(_Generic(&svGetArrElemPtr3,
    fsim_svdpi_element_fn: 1, default: 0),
    "DPI open-array C signature");
_Static_assert(_Generic(&svSetScope,
    fsim_svdpi_set_scope_fn: 1, default: 0),
    "DPI scope C signature");
_Static_assert(_Generic(&svGetCallerInfo,
    fsim_svdpi_caller_fn: 1, default: 0),
    "DPI caller-info C signature");
_Static_assert(_Generic(&svGetTime, fsim_svdpi_time_fn: 1, default: 0),
    "DPI simulation-time C signature");
_Static_assert(_Generic(&svGetTimeUnit,
    fsim_svdpi_time_scale_fn: 1, default: 0),
    "DPI time-unit C signature");
_Static_assert(_Generic(&svGetTimePrecision,
    fsim_svdpi_time_scale_fn: 1, default: 0),
    "DPI time-precision C signature");

size_t fsim_dpi_abi_c_descriptor_size(void)
{
    return sizeof(fsim_dpi_plugin_descriptor_v1);
}

const char* fsim_dpi_abi_c_descriptor_symbol(void)
{
    return FSIM_DPI_PLUGIN_DESCRIPTOR_SYMBOL;
}

// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_plugin_abi.h"

#include <stddef.h>

#define FSIM_DPI_OFFSET(field, expected)                              \
    _Static_assert(                                                   \
        offsetof(fsim_dpi_plugin_descriptor_v1, field) == (expected), \
        "DPI descriptor " #field " offset")

_Static_assert(FSIM_DPI_PLUGIN_ABI_VERSION == 1u, "DPI ABI version");
_Static_assert(sizeof(void*) == 8u, "DPI ABI requires x86-64 pointers");
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

size_t fsim_dpi_abi_c_descriptor_size(void)
{
    return sizeof(fsim_dpi_plugin_descriptor_v1);
}

const char* fsim_dpi_abi_c_descriptor_symbol(void)
{
    return FSIM_DPI_PLUGIN_DESCRIPTOR_SYMBOL;
}

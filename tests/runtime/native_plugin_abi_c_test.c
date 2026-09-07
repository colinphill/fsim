// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/native_plugin_abi.h"

#include <stddef.h>

size_t fsim_native_plugin_abi_c_descriptor_size(void)
{
    return sizeof(fsim_native_plugin_descriptor_v3);
}

const char* fsim_native_plugin_abi_c_descriptor_symbol(void)
{
    return FSIM_NATIVE_PLUGIN_DESCRIPTOR_SYMBOL;
}

fsim_native_plugin_descriptor_v3 fsim_native_plugin_abi_c_descriptor(void)
{
    static const char name[] = "portable-tf";
    static const char version[] = "3.0";
    static const char producer[] = "independent-c-probe";
    static const char build_id[] = "c-build-1";
    const fsim_native_plugin_descriptor_v3 descriptor = {
        FSIM_NATIVE_PLUGIN_ABI_VERSION,
        FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_BASE_SIZE,
        (uint32_t)(sizeof(void*) * 8u),
        0u,
        FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
        (uint32_t)(sizeof(name) - 1u),
        (uint32_t)(sizeof(version) - 1u),
        (uint32_t)(sizeof(producer) - 1u),
        (uint32_t)(sizeof(build_id) - 1u),
        name,
        version,
        producer,
        build_id,
        0u,
        0u,
        NULL,
    };
    return descriptor;
}

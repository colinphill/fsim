// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc_abi.h"

extern "C" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
    return host != nullptr && registrar != nullptr
            && host->abi_version == FSIM_SYSTEMC_ABI_VERSION
            && registrar->abi_version == FSIM_SYSTEMC_ABI_VERSION
        ? FSIM_SC_OK
        : FSIM_SC_ABI_MISMATCH;
}

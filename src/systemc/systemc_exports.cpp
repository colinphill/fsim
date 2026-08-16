// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/accellera.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace fsim::systemc::detail {
namespace {

export_descriptor*& export_descriptors() noexcept {
    static export_descriptor* first = nullptr;
    return first;
}

} // namespace

void add_export_descriptor(export_descriptor* descriptor) noexcept {
    if (descriptor == nullptr) {
        return;
    }
    descriptor->next = export_descriptors();
    export_descriptors() = descriptor;
}

} // namespace fsim::systemc::detail

extern "C" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
    try {
        const auto* const runtime_identity =
            fsim_systemc_accellera_runtime_identity();
        const auto* const compatibility_identity =
            fsim_systemc_accellera_compatibility_identity();
        if (runtime_identity == nullptr || *runtime_identity == '\0'
            || compatibility_identity == nullptr
            || *compatibility_identity == '\0') {
            return FSIM_SC_ABI_MISMATCH;
        }
        using fsim::systemc::detail::export_descriptor;
        std::vector<export_descriptor*> descriptors;
        for (auto* current =
                 fsim::systemc::detail::export_descriptors();
             current != nullptr;
             current = current->next) {
            if (current->public_name == nullptr
                || *current->public_name == '\0'
                || current->register_export == nullptr) {
                return FSIM_SC_INVALID_ARGUMENT;
            }
            descriptors.push_back(current);
        }
        std::sort(
            descriptors.begin(), descriptors.end(),
            [](const auto* left, const auto* right) {
                return std::string_view{left->public_name}
                    < std::string_view{right->public_name};
            });
        if (std::adjacent_find(
                descriptors.begin(), descriptors.end(),
                [](const auto* left, const auto* right) {
                    return std::string_view{left->public_name}
                        == std::string_view{right->public_name};
                }) != descriptors.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        for (const auto* descriptor : descriptors) {
            const auto status = descriptor->register_export(
                host, registrar, descriptor->public_name);
            if (status != FSIM_SC_OK) {
                return status;
            }
        }
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

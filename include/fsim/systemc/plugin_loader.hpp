// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/platform/dynamic_library.hpp"
#include "fsim/systemc_abi.h"

#include <filesystem>
#include <memory>
#include <string>

namespace fsim::systemc {

class HierarchyRegistry;

class Plugin final {
public:
    Plugin(Plugin&&) = delete;
    Plugin& operator=(Plugin&&) = delete;
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;
    ~Plugin() = default;

    [[nodiscard]] static std::unique_ptr<Plugin> load(
        const std::filesystem::path& path,
        const fsim_sc_host_v1& host,
        fsim_sc_registrar_v1& registrar,
        std::string& error);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    friend class HierarchyRegistry;

    [[nodiscard]] static std::unique_ptr<Plugin> load(
        const std::filesystem::path& path,
        const fsim_sc_host_v1& host,
        fsim_sc_registrar_v1& registrar,
        std::string& error,
        bool discard_registrar_state_on_failure);

    Plugin(
        std::filesystem::path path,
        const fsim_sc_host_v1& host,
        std::unique_ptr<platform::DynamicLibrary> library);

    std::filesystem::path path_;
    // Plug-ins may retain the host-table pointer after initialization. Keep a
    // stable copy alive until after the dynamic library is unloaded.
    std::unique_ptr<fsim_sc_host_v1> host_;
    std::unique_ptr<platform::DynamicLibrary> library_;
};

} // namespace fsim::systemc

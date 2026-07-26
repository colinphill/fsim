// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/plugin_loader.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool factory_registered = false;

fsim_sc_status_v1 register_factory(
    void*,
    const char* name,
    fsim_sc_module_factory_v1 factory,
    fsim_sc_module_destroy_v1 destroy,
    void*) {
    factory_registered =
        std::string{name} == "sample" && factory != nullptr && destroy != nullptr;
    return factory_registered ? FSIM_SC_OK : FSIM_SC_INVALID_ARGUMENT;
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 3);

    fsim_sc_host_v1 host{};
    host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    // Model a future caller with larger tables. The v1 loader copies only the
    // known prefix, so it must clamp the size advertised to a v1 plug-in.
    host.struct_size =
        static_cast<decltype(host.struct_size)>(sizeof(host) + 64);

    fsim_sc_registrar_v1 registrar{};
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size =
        static_cast<decltype(registrar.struct_size)>(
            sizeof(registrar) + 64);
    registrar.register_factory = register_factory;

    std::string error;
    const auto plugin =
        fsim::systemc::Plugin::load(std::filesystem::path{argv[1]}, host, registrar, error);
    assert(plugin != nullptr);
    assert(error.empty());
    assert(factory_registered);

    factory_registered = false;
    error.clear();
    const auto throwing_plugin =
        fsim::systemc::Plugin::load(
            std::filesystem::path{argv[2]}, host, registrar, error);
    assert(throwing_plugin == nullptr);
    assert(
        error
        == "SystemC plug-in initialization threw an exception: "
           "intentional plug-in failure");
    assert(!factory_registered);
    std::cout << "SystemC plug-in loader tests passed\n";
}

// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/plugin_loader.hpp"

#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace fsim::systemc {
namespace {

struct PendingFactory {
    std::string name;
    fsim_sc_module_factory_v1 factory{};
    fsim_sc_module_destroy_v1 destroy{};
    void* user{};
};

struct RegistrationBuffer {
    std::vector<PendingFactory> factories;
};

extern "C" fsim_sc_status_v1 buffer_factory(
    void* context,
    const char* name,
    const fsim_sc_module_factory_v1 factory,
    const fsim_sc_module_destroy_v1 destroy,
    void* user) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || factory == nullptr || destroy == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        static_cast<RegistrationBuffer*>(context)->factories.push_back(
            PendingFactory{name, factory, destroy, user});
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

/*
 * A registrar callback is outside fsim's control and might retain an earlier
 * factory before rejecting a later one. Keep code pointers valid in that rare
 * failure case. No failed plug-in callbacks are invoked by fsim itself.
 */
void quarantine(std::unique_ptr<Plugin> plugin) {
    static std::mutex mutex;
    static std::vector<std::unique_ptr<Plugin>> plugins;
    std::lock_guard lock(mutex);
    plugins.push_back(std::move(plugin));
}

} // namespace

Plugin::Plugin(
    std::filesystem::path path,
    const fsim_sc_host_v1& host,
    std::unique_ptr<platform::DynamicLibrary> library)
    : path_(std::move(path)),
      host_(std::make_unique<fsim_sc_host_v1>(host)),
      library_(std::move(library)) {}

std::unique_ptr<Plugin> Plugin::load(
    const std::filesystem::path& path,
    const fsim_sc_host_v1& host,
    fsim_sc_registrar_v1& registrar,
    std::string& error) {
    error.clear();
    if (host.abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host.struct_size < sizeof(fsim_sc_host_v1)
        || registrar.abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar.struct_size < sizeof(fsim_sc_registrar_v1)
        || registrar.register_factory == nullptr) {
        error = "SystemC host/registrar ABI mismatch";
        return nullptr;
    }

    auto library = platform::DynamicLibrary::open(path, error);
    if (!library) {
        return nullptr;
    }
    auto* raw_init = library->symbol("fsim_plugin_init_v1", error);
    if (raw_init == nullptr) {
        error = "SystemC plug-in does not export fsim_plugin_init_v1: " + error;
        return nullptr;
    }

    // Converting the loader-provided symbol address to a function pointer is
    // the platform ABI operation represented by dlsym/GetProcAddress.
    const auto init = reinterpret_cast<fsim_plugin_init_v1_fn>(raw_init);
    auto plugin = std::unique_ptr<Plugin>{
        new Plugin{path, host, std::move(library)}};
    // Only the v1 prefix was copied into current-sized storage. Never
    // advertise a larger caller-owned extent to plug-in code.
    plugin->host_->struct_size = sizeof(fsim_sc_host_v1);
    RegistrationBuffer registrations;
    auto buffered_registrar = registrar;
    buffered_registrar.struct_size = sizeof(fsim_sc_registrar_v1);
    buffered_registrar.context = &registrations;
    buffered_registrar.register_factory = buffer_factory;
    fsim_sc_status_v1 status = FSIM_SC_RUNTIME_ERROR;
    try {
        status = init(plugin->host_.get(), &buffered_registrar);
    } catch (const std::exception& exception) {
        error =
            "SystemC plug-in initialization threw an exception: "
            + std::string{exception.what()};
        return nullptr;
    } catch (...) {
        error = "SystemC plug-in initialization threw an unknown exception";
        return nullptr;
    }
    if (status != FSIM_SC_OK) {
        error = "SystemC plug-in initialization failed with status "
            + std::to_string(static_cast<std::uint32_t>(status));
        return nullptr;
    }
    for (const auto& factory : registrations.factories) {
        try {
            status = registrar.register_factory(
                registrar.context,
                factory.name.c_str(),
                factory.factory,
                factory.destroy,
                factory.user);
        } catch (const std::exception& exception) {
            error =
                "SystemC factory registration threw an exception: "
                + std::string{exception.what()};
            quarantine(std::move(plugin));
            return nullptr;
        } catch (...) {
            error = "SystemC factory registration threw an unknown exception";
            quarantine(std::move(plugin));
            return nullptr;
        }
        if (status != FSIM_SC_OK) {
            error =
                "SystemC factory registration failed with status "
                + std::to_string(static_cast<std::uint32_t>(status));
            quarantine(std::move(plugin));
            return nullptr;
        }
    }
    return plugin;
}

const std::filesystem::path& Plugin::path() const noexcept {
    return path_;
}

} // namespace fsim::systemc

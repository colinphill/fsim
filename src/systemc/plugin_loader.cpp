// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/plugin_loader.hpp"

#include <algorithm>
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
    fsim_sc_module_elaborate_v1 elaboration_factory{};
    fsim_sc_module_destroy_v1 destroy{};
    void* user{};
};

struct PendingParameter {
    std::string factory;
    std::string name;
    fsim_sc_construction_type_v1 type{
        FSIM_SC_CONSTRUCTION_INTEGER};
    std::uint8_t has_default{};
    std::int64_t default_value{};
};

struct RegistrationBuffer {
    std::vector<PendingFactory> factories;
    std::vector<PendingParameter> parameters;
    bool rejected{};
};

[[nodiscard]] fsim_sc_status_v1 reject_registration(void* context) noexcept {
    if (context != nullptr) {
        static_cast<RegistrationBuffer*>(context)->rejected = true;
    }
    return FSIM_SC_INVALID_ARGUMENT;
}

[[nodiscard]] bool valid_construction_default(
    const fsim_sc_construction_type_v1 type,
    const std::uint8_t has_default,
    const std::int64_t value) noexcept {
    if (has_default == 0) {
        return true;
    }
    switch (type) {
    case FSIM_SC_CONSTRUCTION_INTEGER: return true;
    case FSIM_SC_CONSTRUCTION_POSITIVE: return value > 0;
    case FSIM_SC_CONSTRUCTION_NATURAL: return value >= 0;
    case FSIM_SC_CONSTRUCTION_BOOLEAN:
    case FSIM_SC_CONSTRUCTION_BIT: return value == 0 || value == 1;
    }
    return false;
}

extern "C" fsim_sc_status_v1 buffer_factory(
    void* context,
    const char* name,
    const fsim_sc_module_factory_v1 factory,
    const fsim_sc_module_destroy_v1 destroy,
    void* user) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || factory == nullptr || destroy == nullptr) {
        return reject_registration(context);
    }
    try {
        auto& registrations = *static_cast<RegistrationBuffer*>(context);
        if (std::any_of(
                registrations.factories.begin(),
                registrations.factories.end(),
                [&](const auto& candidate) {
                    return candidate.name == name;
                })) {
            return reject_registration(context);
        }
        registrations.factories.push_back(
            PendingFactory{name, factory, nullptr, destroy, user});
        return FSIM_SC_OK;
    } catch (...) {
        static_cast<RegistrationBuffer*>(context)->rejected = true;
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 buffer_factory_parameter(
    void* context,
    const char* factory,
    const char* name,
    const fsim_sc_construction_type_v1 type,
    const std::uint8_t has_default,
    const std::int64_t default_value) noexcept {
    if (context == nullptr || factory == nullptr || *factory == '\0'
        || name == nullptr || *name == '\0'
        || type < FSIM_SC_CONSTRUCTION_INTEGER
        || type > FSIM_SC_CONSTRUCTION_BIT
        || has_default > 1
        || !valid_construction_default(type, has_default, default_value)) {
        return reject_registration(context);
    }
    try {
        auto& registrations =
            *static_cast<RegistrationBuffer*>(context);
        const auto factory_exists = std::any_of(
            registrations.factories.begin(),
            registrations.factories.end(),
            [&](const auto& candidate) {
                return candidate.name == factory;
            });
        const auto duplicate = std::any_of(
            registrations.parameters.begin(),
            registrations.parameters.end(),
            [&](const auto& candidate) {
                return candidate.factory == factory
                    && candidate.name == name;
            });
        if (!factory_exists || duplicate) {
            return reject_registration(context);
        }
        registrations.parameters.push_back({
            factory,
            name,
            type,
            has_default,
            default_value,
        });
        return FSIM_SC_OK;
    } catch (...) {
        static_cast<RegistrationBuffer*>(context)->rejected = true;
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 buffer_elaboration_factory(
    void* context,
    const char* name,
    const fsim_sc_module_elaborate_v1 factory,
    const fsim_sc_module_destroy_v1 destroy,
    void* user) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || factory == nullptr || destroy == nullptr) {
        return reject_registration(context);
    }
    try {
        auto& registrations = *static_cast<RegistrationBuffer*>(context);
        if (std::any_of(
                registrations.factories.begin(),
                registrations.factories.end(),
                [&](const auto& candidate) {
                    return candidate.name == name;
                })) {
            return reject_registration(context);
        }
        registrations.factories.push_back(
            PendingFactory{name, nullptr, factory, destroy, user});
        return FSIM_SC_OK;
    } catch (...) {
        static_cast<RegistrationBuffer*>(context)->rejected = true;
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
    return load(path, host, registrar, error, false);
}

std::unique_ptr<Plugin> Plugin::load(
    const std::filesystem::path& path,
    const fsim_sc_host_v1& host,
    fsim_sc_registrar_v1& registrar,
    std::string& error,
    const bool discard_registrar_state_on_failure) {
    error.clear();
    if (host.abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host.struct_size < sizeof(fsim_sc_host_v1)
        || registrar.abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar.struct_size < sizeof(fsim_sc_registrar_v1)
        || registrar.register_factory == nullptr
        || registrar.register_elaboration_factory == nullptr
        || registrar.register_factory_parameter == nullptr) {
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
    buffered_registrar.register_elaboration_factory =
        buffer_elaboration_factory;
    buffered_registrar.register_factory_parameter =
        buffer_factory_parameter;
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
        error = registrations.rejected
            ? "SystemC plug-in initialization attempted an invalid or "
              "duplicate factory registration"
            : "SystemC plug-in initialization failed with status "
                  + std::to_string(static_cast<std::uint32_t>(status));
        return nullptr;
    }
    if (registrations.rejected) {
        error = "SystemC plug-in initialization ignored an invalid or "
                "duplicate factory registration";
        return nullptr;
    }
    for (const auto& factory : registrations.factories) {
        try {
            status =
                factory.elaboration_factory != nullptr
                ? registrar.register_elaboration_factory(
                      registrar.context,
                      factory.name.c_str(),
                      factory.elaboration_factory,
                      factory.destroy,
                      factory.user)
                : registrar.register_factory(
                      registrar.context,
                      factory.name.c_str(),
                      factory.factory,
                      factory.destroy,
                      factory.user);
        } catch (const std::exception& exception) {
            error =
                "SystemC factory registration threw an exception: "
                + std::string{exception.what()};
            if (!discard_registrar_state_on_failure) {
                quarantine(std::move(plugin));
            }
            return nullptr;
        } catch (...) {
            error = "SystemC factory registration threw an unknown exception";
            if (!discard_registrar_state_on_failure) {
                quarantine(std::move(plugin));
            }
            return nullptr;
        }
        if (status != FSIM_SC_OK) {
            error =
                "SystemC factory registration failed with status "
                + std::to_string(static_cast<std::uint32_t>(status));
            if (!discard_registrar_state_on_failure) {
                quarantine(std::move(plugin));
            }
            return nullptr;
        }
    }
    for (const auto& parameter : registrations.parameters) {
        try {
            status = registrar.register_factory_parameter(
                registrar.context,
                parameter.factory.c_str(),
                parameter.name.c_str(),
                parameter.type,
                parameter.has_default,
                parameter.default_value);
        } catch (const std::exception& exception) {
            error =
                "SystemC factory-parameter registration threw an "
                "exception: "
                + std::string{exception.what()};
            if (!discard_registrar_state_on_failure) {
                quarantine(std::move(plugin));
            }
            return nullptr;
        } catch (...) {
            error = "SystemC factory-parameter registration threw an "
                    "unknown exception";
            if (!discard_registrar_state_on_failure) {
                quarantine(std::move(plugin));
            }
            return nullptr;
        }
        if (status != FSIM_SC_OK) {
            error =
                "SystemC factory-parameter registration failed with status "
                + std::to_string(static_cast<std::uint32_t>(status));
            if (!discard_registrar_state_on_failure) {
                quarantine(std::move(plugin));
            }
            return nullptr;
        }
    }
    return plugin;
}

const std::filesystem::path& Plugin::path() const noexcept {
    return path_;
}

} // namespace fsim::systemc

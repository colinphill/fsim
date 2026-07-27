// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/hierarchy.hpp"

#include "fsim/systemc/plugin_loader.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::systemc {

struct HierarchyRegistry::Impl {
    struct Factory {
        fsim_sc_module_factory_v1 legacy{};
        fsim_sc_module_elaborate_v1 elaborate{};
        fsim_sc_module_destroy_v1 destroy{};
        void* user{};
    };

    struct Object {
        fsim_sc_handle_v1 module{};
        std::size_t port{};
    };

    struct Child {
        fsim_sc_handle_v1 module{};
        std::size_t child{};
    };

    struct LiveModule {
        ModuleDescription description;
        fsim_sc_module_destroy_v1 destroy{};
        void* user{};
        void* object{};
    };

    std::filesystem::path path;
    std::unique_ptr<Plugin> plugin;
    std::unordered_map<std::string, Factory> factories;
    std::unordered_map<fsim_sc_handle_v1, ModuleDescription> pending;
    std::unordered_map<fsim_sc_handle_v1, Object> objects;
    std::unordered_map<fsim_sc_handle_v1, Child> children;
    std::vector<LiveModule> live;
    fsim_sc_handle_v1 next_handle{1};

    [[nodiscard]] std::optional<fsim_sc_handle_v1> allocate_handle() {
        if (next_handle == 0
            || next_handle
                == std::numeric_limits<fsim_sc_handle_v1>::max()) {
            return std::nullopt;
        }
        return next_handle++;
    }

    void rollback(const ModuleDescription& description) noexcept {
        for (const auto& port : description.ports) {
            objects.erase(port.handle);
        }
        for (const auto& child : description.foreign_children) {
            children.erase(child.handle);
        }
        pending.erase(description.handle);
    }
};

namespace {

[[nodiscard]] bool valid_direction(
    const fsim_sc_port_direction_v1 direction) noexcept {
    return direction == FSIM_SC_INPUT
        || direction == FSIM_SC_OUTPUT
        || direction == FSIM_SC_INOUT;
}

[[nodiscard]] bool valid_encoding(
    const fsim_sc_value_encoding_v1 encoding) noexcept {
    return encoding == FSIM_SC_BIT2
        || encoding == FSIM_SC_LOGIC4
        || encoding == FSIM_SC_SIGNED
        || encoding == FSIM_SC_UNSIGNED;
}

extern "C" fsim_sc_status_v1 registry_register_port(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_port_direction_v1 direction,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || result == nullptr || width == 0
        || !valid_direction(direction) || !valid_encoding(encoding)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()
            || std::any_of(
                found->second.ports.begin(),
                found->second.ports.end(),
                [&](const PortDescription& port) {
                    return port.name == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index = found->second.ports.size();
        found->second.ports.push_back(
            {*handle, name, direction, encoding, width});
        registry.objects.emplace(
            *handle,
            HierarchyRegistry::Impl::Object{module, index});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_foreign_child(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || result == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()
            || std::any_of(
                found->second.foreign_children.begin(),
                found->second.foreign_children.end(),
                [&](const ForeignChildDescription& child) {
                    return child.name == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index = found->second.foreign_children.size();
        found->second.foreign_children.push_back(
            {*handle, name, {}});
        registry.children.emplace(
            *handle,
            HierarchyRegistry::Impl::Child{module, index});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_connect_foreign_port(
    void* context,
    const fsim_sc_handle_v1 child,
    const char* name,
    const fsim_sc_port_direction_v1 direction,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    const fsim_sc_handle_v1 object) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || width == 0 || !valid_direction(direction)
        || !valid_encoding(encoding)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto child_found = registry.children.find(child);
        const auto object_found = registry.objects.find(object);
        if (child_found == registry.children.end()
            || object_found == registry.objects.end()
            || child_found->second.module != object_found->second.module) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto module =
            registry.pending.find(child_found->second.module);
        if (module == registry.pending.end()
            || child_found->second.child
                >= module->second.foreign_children.size()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        auto& ports =
            module->second
                .foreign_children[child_found->second.child]
                .ports;
        if (std::any_of(
                ports.begin(), ports.end(),
                [&](const ForeignPortDescription& port) {
                    return port.name == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        ports.push_back(
            {name, direction, encoding, width, object});
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 unsupported_register_process(
    void*,
    fsim_sc_handle_v1,
    const char*,
    fsim_sc_process_kind_v1,
    fsim_sc_process_entry_v1,
    void*,
    fsim_sc_handle_v1*) noexcept {
    return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 unsupported_add_sensitivity(
    void*,
    fsim_sc_handle_v1,
    fsim_sc_handle_v1,
    fsim_sc_edge_kind_v1) noexcept {
    return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 unsupported_read(
    void*, fsim_sc_handle_v1, fsim_sc_value_view_v1*) noexcept {
    return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 unsupported_write(
    void*,
    fsim_sc_handle_v1,
    const fsim_sc_value_view_v1*) noexcept {
    return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 unsupported_wait_time(
    void*, std::uint64_t) noexcept {
    return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 unsupported_wait_event(
    void*, fsim_sc_handle_v1) noexcept {
    return FSIM_SC_NOT_SUPPORTED;
}

extern "C" fsim_sc_status_v1 unsupported_notify(
    void*, fsim_sc_handle_v1, std::uint64_t) noexcept {
    return FSIM_SC_NOT_SUPPORTED;
}

extern "C" void ignore_report(void*, int, const char*) noexcept {}

extern "C" fsim_sc_status_v1 registry_register_factory(
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
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        return registry.factories.emplace(
                   name,
                   HierarchyRegistry::Impl::Factory{
                       factory, nullptr, destroy, user})
                       .second
            ? FSIM_SC_OK
            : FSIM_SC_INVALID_ARGUMENT;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_elaboration_factory(
    void* context,
    const char* name,
    const fsim_sc_module_elaborate_v1 factory,
    const fsim_sc_module_destroy_v1 destroy,
    void* user) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || factory == nullptr || destroy == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        return registry.factories.emplace(
                   name,
                   HierarchyRegistry::Impl::Factory{
                       nullptr, factory, destroy, user})
                       .second
            ? FSIM_SC_OK
            : FSIM_SC_INVALID_ARGUMENT;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

} // namespace

HierarchyRegistry::HierarchyRegistry(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

HierarchyRegistry::HierarchyRegistry(HierarchyRegistry&&) noexcept = default;
HierarchyRegistry& HierarchyRegistry::operator=(
    HierarchyRegistry&&) noexcept = default;

HierarchyRegistry::~HierarchyRegistry() {
    if (!impl_) {
        return;
    }
    for (auto instance = impl_->live.rbegin();
         instance != impl_->live.rend(); ++instance) {
        try {
            instance->destroy(instance->user, instance->object);
        } catch (...) {
            // Native destructors cannot report during stack unwinding and
            // must never cross the ABI boundary.
        }
    }
    impl_->live.clear();
    impl_->plugin.reset();
}

std::unique_ptr<HierarchyRegistry> HierarchyRegistry::load(
    const std::filesystem::path& path,
    std::string& error) {
    auto impl = std::make_unique<Impl>();
    impl->path = path;

    fsim_sc_host_v1 host{};
    host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    host.struct_size = sizeof(host);
    host.context = impl.get();
    host.register_port = registry_register_port;
    host.register_process = unsupported_register_process;
    host.add_sensitivity = unsupported_add_sensitivity;
    host.read_value = unsupported_read;
    host.write_value = unsupported_write;
    host.wait_time = unsupported_wait_time;
    host.wait_event = unsupported_wait_event;
    host.notify_event = unsupported_notify;
    host.report = ignore_report;
    host.register_foreign_child = registry_register_foreign_child;
    host.connect_foreign_port = registry_connect_foreign_port;

    fsim_sc_registrar_v1 registrar{};
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size = sizeof(registrar);
    registrar.context = impl.get();
    registrar.register_factory = registry_register_factory;
    registrar.register_elaboration_factory =
        registry_register_elaboration_factory;

    impl->plugin = Plugin::load(path, host, registrar, error);
    if (!impl->plugin) {
        return nullptr;
    }
    return std::unique_ptr<HierarchyRegistry>{
        new HierarchyRegistry{std::move(impl)}};
}

bool HierarchyRegistry::has_factory(
    const std::string_view name) const noexcept {
    return impl_ != nullptr
        && impl_->factories.contains(std::string{name});
}

bool HierarchyRegistry::has_elaboration_factory(
    const std::string_view name) const noexcept {
    if (impl_ == nullptr) {
        return false;
    }
    const auto found = impl_->factories.find(std::string{name});
    return found != impl_->factories.end()
        && found->second.elaborate != nullptr;
}

std::size_t HierarchyRegistry::factory_count() const noexcept {
    return impl_ == nullptr ? 0 : impl_->factories.size();
}

std::optional<ModuleDescription> HierarchyRegistry::instantiate(
    const std::string_view factory,
    const std::string_view instance,
    const fsim_sc_handle_v1 parent,
    std::string& error) {
    error.clear();
    if (impl_ == nullptr || factory.empty() || instance.empty()) {
        error = "invalid SystemC factory or instance name";
        return std::nullopt;
    }
    const auto found = impl_->factories.find(std::string{factory});
    if (found == impl_->factories.end()) {
        error = "SystemC factory '" + std::string{factory}
            + "' was not registered";
        return std::nullopt;
    }
    if (found->second.elaborate == nullptr) {
        error = "SystemC factory '" + std::string{factory}
            + "' uses the legacy registration form and has no typed "
              "elaboration surface";
        return std::nullopt;
    }
    const auto handle = impl_->allocate_handle();
    if (!handle) {
        error = "SystemC hierarchy handle space is exhausted";
        return std::nullopt;
    }
    ModuleDescription pending{
        *handle,
        parent,
        std::string{factory},
        std::string{instance},
        {},
        {}};
    impl_->pending.emplace(*handle, pending);

    void* object = nullptr;
    fsim_sc_status_v1 status = FSIM_SC_RUNTIME_ERROR;
    try {
        status = found->second.elaborate(
            found->second.user,
            pending.instance.c_str(),
            *handle,
            parent,
            &object);
    } catch (const std::exception& exception) {
        error = "SystemC factory '" + std::string{factory}
            + "' threw during elaboration: " + exception.what();
    } catch (...) {
        error = "SystemC factory '" + std::string{factory}
            + "' threw an unknown exception during elaboration";
    }

    const auto constructed = impl_->pending.find(*handle);
    if (error.empty() && status != FSIM_SC_OK) {
        error = "SystemC factory '" + std::string{factory}
            + "' failed with status "
            + std::to_string(static_cast<std::uint32_t>(status));
    }
    if (error.empty() && object == nullptr) {
        error = "SystemC factory '" + std::string{factory}
            + "' returned a null module object";
    }
    if (!error.empty() || constructed == impl_->pending.end()) {
        if (object != nullptr) {
            try {
                found->second.destroy(found->second.user, object);
            } catch (...) {
            }
        }
        if (constructed != impl_->pending.end()) {
            impl_->rollback(constructed->second);
        }
        return std::nullopt;
    }

    auto description = std::move(constructed->second);
    impl_->pending.erase(constructed);
    impl_->live.push_back(
        {description,
         found->second.destroy,
         found->second.user,
         object});
    return description;
}

const std::filesystem::path& HierarchyRegistry::path() const noexcept {
    return impl_->path;
}

} // namespace fsim::systemc

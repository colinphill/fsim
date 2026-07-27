// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/hierarchy.hpp"

#include "fsim/runtime/simir.hpp"
#include "fsim/systemc/plugin_loader.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
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
        fsim_sc_value_encoding_v1 encoding{FSIM_SC_BIT2};
        std::uint32_t width{};
    };

    struct Child {
        fsim_sc_handle_v1 module{};
        std::size_t child{};
    };

    struct Process {
        fsim_sc_handle_v1 module{};
        std::size_t process{};
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
    std::unordered_map<fsim_sc_handle_v1, Process> processes;
    std::unordered_map<fsim_sc_handle_v1, std::uint32_t> runtime_objects;
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
        for (const auto& process : description.processes) {
            processes.erase(process.handle);
        }
        pending.erase(description.handle);
    }
};

namespace {

struct ActiveInvocation {
    HierarchyRegistry::Impl* registry{};
    runtime::simir::ProcessExecutionContext* context{};
    std::vector<std::uint8_t> read_buffer;
    std::string failure;
};

thread_local ActiveInvocation* active_invocation = nullptr;

class InvocationScope final {
public:
    explicit InvocationScope(ActiveInvocation& invocation) noexcept {
        active_invocation = &invocation;
    }
    ~InvocationScope() { active_invocation = nullptr; }

    InvocationScope(const InvocationScope&) = delete;
    InvocationScope& operator=(const InvocationScope&) = delete;
};

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
            HierarchyRegistry::Impl::Object{
                module, index, encoding, width});
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

extern "C" fsim_sc_status_v1 registry_register_process(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_process_kind_v1 kind,
    const fsim_sc_process_entry_v1 entry,
    void* user,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || entry == nullptr || result == nullptr
        || (kind != FSIM_SC_METHOD && kind != FSIM_SC_THREAD
            && kind != FSIM_SC_CTHREAD)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()
            || std::any_of(
                found->second.processes.begin(),
                found->second.processes.end(),
                [&](const ProcessDescription& process) {
                    return process.name == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index = found->second.processes.size();
        found->second.processes.push_back(
            {*handle, name, kind, entry, user, {}, true});
        registry.processes.emplace(
            *handle,
            HierarchyRegistry::Impl::Process{module, index});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_add_sensitivity(
    void* context,
    const fsim_sc_handle_v1 process,
    const fsim_sc_handle_v1 object,
    const fsim_sc_edge_kind_v1 edge) noexcept {
    if (context == nullptr
        || (edge != FSIM_SC_ANY_EDGE && edge != FSIM_SC_POSEDGE
            && edge != FSIM_SC_NEGEDGE)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto process_found = registry.processes.find(process);
        const auto object_found = registry.objects.find(object);
        if (process_found == registry.processes.end()
            || object_found == registry.objects.end()
            || process_found->second.module
                != object_found->second.module) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto module =
            registry.pending.find(process_found->second.module);
        if (module == registry.pending.end()
            || process_found->second.process
                >= module->second.processes.size()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        auto& sensitivities =
            module->second
                .processes[process_found->second.process]
                .sensitivity;
        if (std::find_if(
                sensitivities.begin(), sensitivities.end(),
                [&](const SensitivityDescription& existing) {
                    return existing.object == object
                        && existing.edge == edge;
                }) == sensitivities.end()) {
            sensitivities.push_back({object, edge});
        }
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_set_process_initialize(
    void* context,
    const fsim_sc_handle_v1 process,
    const std::uint8_t initialize) noexcept {
    if (context == nullptr || initialize > 1) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto process_found = registry.processes.find(process);
        if (process_found == registry.processes.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto module =
            registry.pending.find(process_found->second.module);
        if (module == registry.pending.end()
            || process_found->second.process
                >= module->second.processes.size()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        module->second
            .processes[process_found->second.process]
            .initialize = initialize != 0;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

[[nodiscard]] std::size_t plane_size(
    const std::uint32_t width) noexcept {
    return (static_cast<std::size_t>(width) + 7U) / 8U;
}

extern "C" fsim_sc_status_v1 registry_read(
    void* context,
    const fsim_sc_handle_v1 object,
    fsim_sc_value_view_v1* result) noexcept {
    if (context == nullptr || result == nullptr
        || result->struct_size < sizeof(fsim_sc_value_view_v1)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        if (active_invocation == nullptr
            || active_invocation->registry != &registry
            || active_invocation->context == nullptr) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto metadata = registry.objects.find(object);
        const auto binding = registry.runtime_objects.find(object);
        if (metadata == registry.objects.end()
            || binding == registry.runtime_objects.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto value =
            active_invocation->context->read_signal(binding->second);
        if (value.width() != metadata->second.width) {
            active_invocation->failure =
                "SystemC runtime object width disagrees with DesignIR";
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto bytes = plane_size(metadata->second.width);
        const bool four_state =
            metadata->second.encoding != FSIM_SC_BIT2;
        active_invocation->read_buffer.assign(
            bytes * (four_state ? 2U : 1U), 0);
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            const auto state = value.get(bit);
            const auto byte = bit / 8U;
            const auto mask =
                static_cast<std::uint8_t>(1U << (bit % 8U));
            if (state == runtime::Logic4::one
                || state == runtime::Logic4::x) {
                active_invocation->read_buffer[byte] |= mask;
            }
            if (state == runtime::Logic4::x
                || state == runtime::Logic4::z) {
                if (!four_state) {
                    active_invocation->failure =
                        "SystemC two-state port observed X or Z";
                    return FSIM_SC_RUNTIME_ERROR;
                }
                active_invocation->read_buffer[bytes + byte] |= mask;
            }
        }
        result->encoding = metadata->second.encoding;
        result->width = metadata->second.width;
        result->data = active_invocation->read_buffer.data();
        result->data_size = active_invocation->read_buffer.size();
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_write(
    void* context,
    const fsim_sc_handle_v1 object,
    const fsim_sc_value_view_v1* value) noexcept {
    if (context == nullptr || value == nullptr
        || value->struct_size < sizeof(fsim_sc_value_view_v1)
        || value->data == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        if (active_invocation == nullptr
            || active_invocation->registry != &registry
            || active_invocation->context == nullptr) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto metadata = registry.objects.find(object);
        const auto binding = registry.runtime_objects.find(object);
        if (metadata == registry.objects.end()
            || binding == registry.runtime_objects.end()
            || value->encoding != metadata->second.encoding
            || value->width != metadata->second.width) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto bytes = plane_size(value->width);
        const bool four_state = value->encoding != FSIM_SC_BIT2;
        const auto expected = bytes * (four_state ? 2U : 1U);
        if (value->data_size != expected) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        runtime::PackedLogic4 converted(value->width);
        for (std::size_t bit = 0; bit < value->width; ++bit) {
            const auto byte = bit / 8U;
            const auto mask =
                static_cast<std::uint8_t>(1U << (bit % 8U));
            const bool aval = (value->data[byte] & mask) != 0;
            const bool bval = four_state
                && (value->data[bytes + byte] & mask) != 0;
            converted.set(
                bit,
                !bval
                    ? (aval ? runtime::Logic4::one
                            : runtime::Logic4::zero)
                    : (aval ? runtime::Logic4::x
                            : runtime::Logic4::z));
        }
        active_invocation->context->write_update(
            binding->second, std::move(converted));
        return FSIM_SC_OK;
    } catch (const std::exception& exception) {
        if (active_invocation != nullptr) {
            active_invocation->failure = exception.what();
        }
        return FSIM_SC_RUNTIME_ERROR;
    } catch (...) {
        if (active_invocation != nullptr) {
            active_invocation->failure =
                "unknown SystemC runtime write failure";
        }
        return FSIM_SC_RUNTIME_ERROR;
    }
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

extern "C" void registry_report(
    void* context, const int severity, const char* message) noexcept {
    if (context == nullptr || severity < 2
        || active_invocation == nullptr
        || active_invocation->registry != context) {
        return;
    }
    try {
        active_invocation->failure =
            message == nullptr || *message == '\0'
            ? "SystemC process reported a runtime failure"
            : message;
    } catch (...) {
    }
}

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
    host.register_process = registry_register_process;
    host.add_sensitivity = registry_add_sensitivity;
    host.read_value = registry_read;
    host.write_value = registry_write;
    host.wait_time = unsupported_wait_time;
    host.wait_event = unsupported_wait_event;
    host.notify_event = unsupported_notify;
    host.report = registry_report;
    host.register_foreign_child = registry_register_foreign_child;
    host.connect_foreign_port = registry_connect_foreign_port;
    host.set_process_initialize =
        registry_set_process_initialize;

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

void HierarchyRegistry::bind_runtime_object(
    const fsim_sc_handle_v1 object,
    const std::uint32_t signal) {
    if (impl_ == nullptr || !impl_->objects.contains(object)) {
        throw std::invalid_argument{
            "cannot bind an unknown SystemC object handle"};
    }
    const auto [found, inserted] =
        impl_->runtime_objects.emplace(object, signal);
    if (!inserted && found->second != signal) {
        throw std::logic_error{
            "SystemC object handle has conflicting runtime bindings"};
    }
}

void HierarchyRegistry::invoke_method(
    const fsim_sc_handle_v1 process,
    runtime::simir::ProcessExecutionContext& context) {
    if (impl_ == nullptr) {
        throw std::logic_error{"SystemC hierarchy registry is unavailable"};
    }
    const auto found = impl_->processes.find(process);
    if (found == impl_->processes.end()) {
        throw std::invalid_argument{"unknown SystemC process handle"};
    }
    const ProcessDescription* description = nullptr;
    for (const auto& module : impl_->live) {
        if (module.description.handle != found->second.module
            || found->second.process
                >= module.description.processes.size()) {
            continue;
        }
        description =
            &module.description.processes[found->second.process];
        break;
    }
    if (description == nullptr
        || description->kind != FSIM_SC_METHOD
        || description->entry == nullptr) {
        throw std::logic_error{
            "SystemC process is not an executable SC_METHOD"};
    }
    if (active_invocation != nullptr) {
        throw std::logic_error{
            "recursive SystemC process invocation is not supported"};
    }
    ActiveInvocation invocation{impl_.get(), &context, {}, {}};
    InvocationScope scope{invocation};
    try {
        description->entry(description->user);
    } catch (const std::exception& exception) {
        invocation.failure =
            "SystemC process callback escaped with an exception: "
            + std::string{exception.what()};
    } catch (...) {
        invocation.failure =
            "SystemC process callback escaped with an unknown exception";
    }
    if (!invocation.failure.empty()) {
        throw std::runtime_error{std::move(invocation.failure)};
    }
}

const std::filesystem::path& HierarchyRegistry::path() const noexcept {
    return impl_->path;
}

} // namespace fsim::systemc

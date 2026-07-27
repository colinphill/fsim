// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/hierarchy.hpp"

#include "fsim/runtime/simir.hpp"
#include "fsim/systemc/plugin_loader.hpp"

#if defined(FSIM_HAS_BOOST_CONTEXT)
#include <boost/context/fiber.hpp>
#endif

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::systemc {

#if defined(FSIM_HAS_BOOST_CONTEXT)
struct ThreadFiberState {
    boost::context::fiber process;
    boost::context::fiber caller;
    bool started{};
    bool terminated{};
    bool stopping{};
};
#endif

struct HierarchyRegistry::Impl {
    struct Factory {
        fsim_sc_module_factory_v1 legacy{};
        fsim_sc_module_elaborate_v1 elaborate{};
        fsim_sc_module_destroy_v1 destroy{};
        void* user{};
    };

    struct Object {
        enum class Kind : std::uint8_t {
            port,
            signal,
            export_object,
        };

        fsim_sc_handle_v1 module{};
        std::size_t index{};
        fsim_sc_value_encoding_v1 encoding{FSIM_SC_BIT2};
        std::uint32_t width{};
        Kind kind{Kind::port};
    };

    struct Child {
        fsim_sc_handle_v1 module{};
        std::size_t child{};
    };

    struct Process {
        fsim_sc_handle_v1 module{};
        std::size_t process{};
    };

    struct Event {
        fsim_sc_handle_v1 module{};
        std::size_t event{};
    };

    struct PrimitiveChannel {
        fsim_sc_handle_v1 module{};
        std::size_t channel{};
    };

    struct LiveModule {
        enum class LifecycleState : std::uint8_t {
            constructed,
            before_elaboration_complete,
            elaborated,
            started,
            ended,
            poisoned,
        };

        ModuleDescription description;
        fsim_sc_module_destroy_v1 destroy{};
        void* user{};
        void* object{};
        LifecycleState lifecycle{LifecycleState::constructed};
    };

    std::filesystem::path path;
    std::unique_ptr<Plugin> plugin;
    std::unordered_map<std::string, Factory> factories;
    std::unordered_map<fsim_sc_handle_v1, ModuleDescription> pending;
    std::unordered_map<
        fsim_sc_handle_v1, std::vector<fsim_sc_handle_v1>>
        native_children;
    std::unordered_map<fsim_sc_handle_v1, Object> objects;
    std::unordered_map<fsim_sc_handle_v1, Child> children;
    std::unordered_map<fsim_sc_handle_v1, Process> processes;
    std::unordered_map<fsim_sc_handle_v1, Event> events;
    std::unordered_map<fsim_sc_handle_v1, PrimitiveChannel>
        primitive_channels;
    std::unordered_set<fsim_sc_handle_v1> internal_signals;
    std::unordered_map<fsim_sc_handle_v1, std::uint32_t> runtime_objects;
#if defined(FSIM_HAS_BOOST_CONTEXT)
    std::unordered_map<
        fsim_sc_handle_v1, std::unique_ptr<ThreadFiberState>>
        thread_fibers;
#endif
    std::vector<LiveModule> live;
    fsim_sc_handle_v1 next_handle{1};
    std::uint64_t femtoseconds_per_tick{1};

    [[nodiscard]] std::optional<fsim_sc_handle_v1> allocate_handle() {
        if (next_handle == 0
            || next_handle
                == std::numeric_limits<fsim_sc_handle_v1>::max()) {
            return std::nullopt;
        }
        return next_handle++;
    }

    void rollback(const fsim_sc_handle_v1 module) noexcept {
        const auto descendants = native_children.find(module);
        if (descendants != native_children.end()) {
            for (const auto child : descendants->second) {
                rollback(child);
            }
            native_children.erase(descendants);
        }
        const auto found = pending.find(module);
        if (found == pending.end()) {
            return;
        }
        const auto& description = found->second;
        for (const auto& port : description.ports) {
            objects.erase(port.handle);
        }
        for (const auto& child : description.foreign_children) {
            children.erase(child.handle);
        }
        for (const auto& process : description.processes) {
            processes.erase(process.handle);
        }
        for (const auto& event : description.events) {
            events.erase(event.handle);
        }
        for (const auto& channel : description.primitive_channels) {
            primitive_channels.erase(channel.handle);
        }
        for (const auto& signal : description.internal_signals) {
            objects.erase(signal.handle);
            internal_signals.erase(signal.handle);
        }
        for (const auto& export_object : description.exports) {
            objects.erase(export_object.handle);
        }
        pending.erase(found);
    }

    [[nodiscard]] ModuleDescription collect(
        const fsim_sc_handle_v1 module) {
        auto found = pending.find(module);
        if (found == pending.end()) {
            throw std::logic_error{
                "missing pending native SystemC module"};
        }
        auto result = std::move(found->second);
        pending.erase(found);
        const auto descendants = native_children.find(module);
        if (descendants != native_children.end()) {
            result.native_children.reserve(descendants->second.size());
            for (const auto child : descendants->second) {
                result.native_children.push_back(collect(child));
            }
            native_children.erase(descendants);
        }
        return result;
    }
};

namespace {

struct ActiveInvocation {
    HierarchyRegistry::Impl* registry{};
    runtime::simir::ProcessExecutionContext* context{};
#if defined(FSIM_HAS_BOOST_CONTEXT)
    ThreadFiberState* thread{};
#endif
    std::vector<std::uint8_t> read_buffer;
    std::string failure;
    std::optional<MethodSuspendResult> suspension;
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

[[nodiscard]] fsim_sc_status_v1 suspend_thread() noexcept {
#if defined(FSIM_HAS_BOOST_CONTEXT)
    if (active_invocation != nullptr
        && active_invocation->thread != nullptr) {
        auto& state = *active_invocation->thread;
        state.caller = std::move(state.caller).resume();
        if (state.stopping) {
            return FSIM_SC_RUNTIME_ERROR;
        }
    }
#endif
    return FSIM_SC_OK;
}

void shutdown_threads(HierarchyRegistry::Impl& registry) noexcept {
#if defined(FSIM_HAS_BOOST_CONTEXT)
    for (auto& [handle, state] : registry.thread_fibers) {
        (void)handle;
        if (!state || !state->started || state->terminated
            || !state->process) {
            continue;
        }
        ActiveInvocation invocation{};
        invocation.registry = &registry;
        invocation.thread = state.get();
        InvocationScope scope{invocation};
        state->stopping = true;
        try {
            state->process = std::move(state->process).resume();
        } catch (...) {
            // The process facade contains callback exceptions. Reaching this
            // catch indicates a broken plug-in boundary; retain no further
            // executable state during best-effort teardown.
        }
    }
    registry.thread_fibers.clear();
#else
    (void)registry;
#endif
}

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

[[nodiscard]] const ModuleDescription* find_module_description(
    const ModuleDescription& root,
    const fsim_sc_handle_v1 handle) noexcept {
    if (root.handle == handle) {
        return &root;
    }
    for (const auto& child : root.native_children) {
        if (const auto* found =
                find_module_description(child, handle);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

[[nodiscard]] HierarchyRegistry::Impl::LiveModule* find_live_module(
    HierarchyRegistry::Impl& registry,
    const fsim_sc_handle_v1 handle) noexcept {
    const auto found = std::find_if(
        registry.live.begin(),
        registry.live.end(),
        [&](const HierarchyRegistry::Impl::LiveModule& module) {
            return module.description.handle == handle;
        });
    return found == registry.live.end() ? nullptr : &*found;
}

void invoke_lifecycle_entry(
    HierarchyRegistry::Impl& registry,
    const fsim_sc_lifecycle_entry_v1 entry,
    void* user,
    const std::string_view phase) {
    if (entry == nullptr || user == nullptr) {
        return;
    }
    if (active_invocation != nullptr) {
        throw std::logic_error{
            "recursive SystemC lifecycle invocation is not supported"};
    }
    ActiveInvocation invocation{};
    invocation.registry = &registry;
    InvocationScope scope{invocation};
    try {
        entry(user);
    } catch (const std::exception& exception) {
        invocation.failure =
            "SystemC " + std::string{phase}
            + " callback escaped with an exception: "
            + exception.what();
    } catch (...) {
        invocation.failure =
            "SystemC " + std::string{phase}
            + " callback escaped with an unknown exception";
    }
    if (!invocation.failure.empty()) {
        throw std::runtime_error{
            "SystemC " + std::string{phase}
            + " callback failed: " + invocation.failure};
    }
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
            {*handle, name, direction, encoding, width, 0});
        registry.objects.emplace(
            *handle,
            HierarchyRegistry::Impl::Object{
                module,
                index,
                encoding,
                width,
                HierarchyRegistry::Impl::Object::Kind::port});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_export(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || module == 0 || name == nullptr
        || *name == '\0' || width == 0 || result == nullptr
        || !valid_encoding(encoding)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()
            || std::any_of(
                found->second.exports.begin(),
                found->second.exports.end(),
                [&](const ExportDescription& export_object) {
                    return export_object.name == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index = found->second.exports.size();
        found->second.exports.push_back(
            {*handle, name, encoding, width, 0});
        registry.objects.emplace(
            *handle,
            HierarchyRegistry::Impl::Object{
                module,
                index,
                encoding,
                width,
                HierarchyRegistry::Impl::Object::Kind::
                    export_object});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_event(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || result == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        std::string event_name =
            name == nullptr || *name == '\0'
                ? "$event_"
                    + std::to_string(found->second.events.size())
                : std::string{name};
        if (std::any_of(
                found->second.events.begin(),
                found->second.events.end(),
                [&](const EventDescription& event) {
                    return event.name == event_name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index = found->second.events.size();
        found->second.events.push_back(
            {*handle, std::move(event_name)});
        registry.events.emplace(
            *handle,
            HierarchyRegistry::Impl::Event{module, index});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_primitive_channel(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_channel_update_v1 update,
    void* user,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || update == nullptr || user == nullptr
        || result == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        std::string channel_name =
            name == nullptr || *name == '\0'
                ? "$channel_"
                    + std::to_string(
                        found->second.primitive_channels.size())
                : std::string{name};
        if (std::any_of(
                found->second.primitive_channels.begin(),
                found->second.primitive_channels.end(),
                [&](const PrimitiveChannelDescription& channel) {
                    return channel.name == channel_name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index =
            found->second.primitive_channels.size();
        found->second.primitive_channels.push_back(
            {*handle, std::move(channel_name), update, user});
        registry.primitive_channels.emplace(
            *handle,
            HierarchyRegistry::Impl::PrimitiveChannel{
                module, index});
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
        const auto native = registry.native_children.find(module);
        if (found == registry.pending.end()
            || std::any_of(
                found->second.foreign_children.begin(),
                found->second.foreign_children.end(),
                [&](const ForeignChildDescription& child) {
                    return child.name == name;
                })
            || (native != registry.native_children.end()
                && std::any_of(
                    native->second.begin(),
                    native->second.end(),
                    [&](const fsim_sc_handle_v1 child) {
                        const auto description =
                            registry.pending.find(child);
                        return description != registry.pending.end()
                            && description->second.instance == name;
                    }))) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index = found->second.foreign_children.size();
        found->second.foreign_children.push_back(
            {*handle, name, {}, {}});
        registry.children.emplace(
            *handle,
            HierarchyRegistry::Impl::Child{module, index});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_set_foreign_child_actual(
    void* context,
    const fsim_sc_handle_v1 child,
    const char* name,
    const std::int64_t value) noexcept {
    if (context == nullptr || child == 0 || name == nullptr
        || *name == '\0') {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto child_found = registry.children.find(child);
        if (child_found == registry.children.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto module =
            registry.pending.find(child_found->second.module);
        if (module == registry.pending.end()
            || child_found->second.child
                >= module->second.foreign_children.size()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        auto& actuals =
            module->second
                .foreign_children[child_found->second.child]
                .construction_actuals;
        if (std::any_of(
                actuals.begin(),
                actuals.end(),
                [&](const auto& actual) {
                    return actual.first == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        actuals.emplace_back(name, value);
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
        const auto event_found = registry.events.find(object);
        if (process_found == registry.processes.end()
            || (object_found == registry.objects.end()
                && event_found == registry.events.end())) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto object_module =
            object_found != registry.objects.end()
                ? object_found->second.module
                : event_found->second.module;
        if (process_found->second.module != object_module
            || (event_found != registry.events.end()
                && edge != FSIM_SC_ANY_EDGE)) {
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

extern "C" fsim_sc_status_v1 registry_register_signal(
    void* context,
    const fsim_sc_handle_v1 module,
    const fsim_sc_handle_v1 channel,
    const char* name,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    const fsim_sc_value_view_v1* initial_value) noexcept {
    if (context == nullptr || width == 0
        || !valid_encoding(encoding) || initial_value == nullptr
        || initial_value->struct_size < sizeof(fsim_sc_value_view_v1)
        || initial_value->encoding != encoding
        || initial_value->width != width
        || initial_value->data == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto pending = registry.pending.find(module);
        const auto registered =
            registry.primitive_channels.find(channel);
        if (pending == registry.pending.end()
            || registered == registry.primitive_channels.end()
            || registered->second.module != module
            || registered->second.channel
                >= pending->second.primitive_channels.size()
            || registry.objects.contains(channel)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto bytes = plane_size(width);
        const bool four_state = encoding != FSIM_SC_BIT2;
        const auto expected = bytes * (four_state ? 2U : 1U);
        if (initial_value->data_size != expected) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        std::string signal_name =
            name == nullptr || *name == '\0'
                ? pending->second
                      .primitive_channels[registered->second.channel]
                      .name
                : std::string{name};
        if (std::any_of(
                pending->second.internal_signals.begin(),
                pending->second.internal_signals.end(),
                [&](const InternalSignalDescription& signal) {
                    return signal.name == signal_name;
                })
            || std::any_of(
                pending->second.ports.begin(),
                pending->second.ports.end(),
                [&](const PortDescription& port) {
                    return port.name == signal_name;
                })
            || std::any_of(
                pending->second.events.begin(),
                pending->second.events.end(),
                [&](const EventDescription& event) {
                    return event.name == signal_name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto index = pending->second.internal_signals.size();
        pending->second.internal_signals.push_back({
            channel,
            std::move(signal_name),
            encoding,
            width,
            std::vector<std::uint8_t>(
                initial_value->data,
                initial_value->data + expected),
        });
        registry.objects.emplace(
            channel,
            HierarchyRegistry::Impl::Object{
                module,
                index,
                encoding,
                width,
                HierarchyRegistry::Impl::Object::Kind::signal});
        registry.internal_signals.insert(channel);
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_bind_port(
    void* context,
    const fsim_sc_handle_v1 port,
    const fsim_sc_handle_v1 channel) noexcept {
    if (context == nullptr || port == 0 || channel == 0) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto port_object = registry.objects.find(port);
        const auto target_object = registry.objects.find(channel);
        if (port_object == registry.objects.end()
            || target_object == registry.objects.end()
            || port_object->second.kind
                != HierarchyRegistry::Impl::Object::Kind::port
            || port_object->second.encoding
                != target_object->second.encoding
            || port_object->second.width
                != target_object->second.width) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto pending =
            registry.pending.find(port_object->second.module);
        if (pending == registry.pending.end()
            || (target_object->second.kind
                    == HierarchyRegistry::Impl::Object::Kind::signal
                && target_object->second.module
                    != port_object->second.module
                && target_object->second.module
                    != pending->second.parent)
            || (target_object->second.kind
                    == HierarchyRegistry::Impl::Object::Kind::port
                && target_object->second.module
                    != pending->second.parent)
            || (target_object->second.kind
                    == HierarchyRegistry::Impl::Object::Kind::
                        export_object
                && target_object->second.module
                    != pending->second.parent)
            || port_object->second.index
                >= pending->second.ports.size()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        auto& description =
            pending->second.ports[port_object->second.index];
        if (description.bound_object != 0
            && description.bound_object != channel) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        description.bound_object = channel;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_bind_export(
    void* context,
    const fsim_sc_handle_v1 export_handle,
    const fsim_sc_handle_v1 target) noexcept {
    if (context == nullptr || export_handle == 0 || target == 0) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto export_object =
            registry.objects.find(export_handle);
        const auto target_object = registry.objects.find(target);
        if (export_object == registry.objects.end()
            || target_object == registry.objects.end()
            || export_object->second.kind
                != HierarchyRegistry::Impl::Object::Kind::
                    export_object
            || (target_object->second.kind
                    != HierarchyRegistry::Impl::Object::Kind::signal
                && target_object->second.kind
                    != HierarchyRegistry::Impl::Object::Kind::
                        export_object)
            || export_object->second.encoding
                != target_object->second.encoding
            || export_object->second.width
                != target_object->second.width) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto module =
            registry.pending.find(export_object->second.module);
        if (module == registry.pending.end()
            || (target_object->second.module
                    != export_object->second.module
                && target_object->second.module
                    != module->second.parent)
            || export_object->second.index
                >= module->second.exports.size()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        auto current = target;
        std::unordered_set<fsim_sc_handle_v1> visited;
        while (current != 0 && visited.insert(current).second) {
            if (current == export_handle) {
                return FSIM_SC_INVALID_ARGUMENT;
            }
            const auto object = registry.objects.find(current);
            if (object == registry.objects.end()
                || object->second.kind
                    != HierarchyRegistry::Impl::Object::Kind::
                        export_object) {
                break;
            }
            const auto owner =
                registry.pending.find(object->second.module);
            if (owner == registry.pending.end()
                || object->second.index >= owner->second.exports.size()) {
                return FSIM_SC_INVALID_ARGUMENT;
            }
            current =
                owner->second.exports[object->second.index]
                    .bound_object;
        }
        auto& description =
            module->second.exports[export_object->second.index];
        if (description.bound_object != 0
            && description.bound_object != target) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        description.bound_object = target;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_native_module(
    void* context,
    const fsim_sc_handle_v1 parent,
    const char* name,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || parent == 0 || name == nullptr
        || *name == '\0' || result == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto parent_module = registry.pending.find(parent);
        if (parent_module == registry.pending.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto descendants = registry.native_children.find(parent);
        if ((descendants != registry.native_children.end()
             && std::any_of(
                 descendants->second.begin(),
                 descendants->second.end(),
                 [&](const fsim_sc_handle_v1 child) {
                     const auto found = registry.pending.find(child);
                     return found != registry.pending.end()
                         && found->second.instance == name;
                 }))
            || std::any_of(
                parent_module->second.foreign_children.begin(),
                parent_module->second.foreign_children.end(),
                [&](const ForeignChildDescription& child) {
                    return child.name == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        ModuleDescription child;
        child.handle = *handle;
        child.parent = parent;
        child.instance = name;
        registry.pending.emplace(*handle, std::move(child));
        registry.native_children[parent].push_back(*handle);
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_lifecycle(
    void* context,
    const fsim_sc_handle_v1 module,
    const fsim_sc_lifecycle_entry_v1 before_end_of_elaboration,
    const fsim_sc_lifecycle_entry_v1 end_of_elaboration,
    const fsim_sc_lifecycle_entry_v1 start_of_simulation,
    const fsim_sc_lifecycle_entry_v1 end_of_simulation,
    void* user) noexcept {
    if (context == nullptr || module == 0
        || before_end_of_elaboration == nullptr
        || end_of_elaboration == nullptr
        || start_of_simulation == nullptr
        || end_of_simulation == nullptr || user == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()
            || found->second.lifecycle.user != nullptr) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        found->second.lifecycle = {
            before_end_of_elaboration,
            end_of_elaboration,
            start_of_simulation,
            end_of_simulation,
            user};
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
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

extern "C" fsim_sc_status_v1 registry_value_changed(
    void* context,
    const fsim_sc_handle_v1 object,
    std::uint8_t* result) noexcept {
    if (context == nullptr || result == nullptr
        || active_invocation == nullptr
        || active_invocation->registry != context
        || active_invocation->context == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto metadata = registry.objects.find(object);
        const auto binding = registry.runtime_objects.find(object);
        if (metadata == registry.objects.end()
            || binding == registry.runtime_objects.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        *result = active_invocation->context->signal_event(
                      binding->second)
            ? 1
            : 0;
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

[[nodiscard]] fsim_sc_status_v1 convert_delay(
    HierarchyRegistry::Impl& registry,
    const std::uint64_t femtoseconds,
    std::uint64_t& ticks) {
    if (registry.femtoseconds_per_tick == 0
        || femtoseconds % registry.femtoseconds_per_tick != 0) {
        if (active_invocation != nullptr) {
            active_invocation->failure =
                "SystemC time is not exactly representable at the "
                "project time resolution";
        }
        return FSIM_SC_RUNTIME_ERROR;
    }
    ticks = femtoseconds / registry.femtoseconds_per_tick;
    return FSIM_SC_OK;
}

extern "C" fsim_sc_status_v1 registry_wait_time(
    void* context, const std::uint64_t femtoseconds) noexcept {
    if (context == nullptr || active_invocation == nullptr
        || active_invocation->registry != context) {
        return FSIM_SC_RUNTIME_ERROR;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        std::uint64_t ticks = 0;
        const auto status =
            convert_delay(registry, femtoseconds, ticks);
        if (status != FSIM_SC_OK) {
            return status;
        }
        active_invocation->suspension =
            MethodSuspendResult{
                MethodSuspendKind::wait_for, ticks, {}, false};
        return suspend_thread();
    } catch (...) {
        active_invocation->failure =
            "SystemC next time trigger could not be recorded";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_wait_event(
    void* context, const fsim_sc_handle_v1 event) noexcept {
    if (context == nullptr || active_invocation == nullptr
        || active_invocation->registry != context) {
        return FSIM_SC_RUNTIME_ERROR;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto metadata = registry.events.find(event);
        const bool signal_event =
            registry.internal_signals.contains(event);
        const auto binding = registry.runtime_objects.find(event);
        if ((metadata == registry.events.end() && !signal_event)
            || binding == registry.runtime_objects.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        active_invocation->suspension =
            MethodSuspendResult{
                MethodSuspendKind::wait_event,
                0,
                {binding->second},
                false};
        return suspend_thread();
    } catch (...) {
        active_invocation->failure =
            "SystemC next event trigger could not be recorded";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_wait_event_list(
    void* context,
    const fsim_sc_handle_v1* events,
    const std::size_t event_count,
    const fsim_sc_event_list_kind_v1 kind) noexcept {
    if (context == nullptr || events == nullptr || event_count == 0
        || active_invocation == nullptr
        || active_invocation->registry != context
        || (kind != FSIM_SC_EVENT_OR_LIST
            && kind != FSIM_SC_EVENT_AND_LIST)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        std::vector<std::uint32_t> signals;
        signals.reserve(event_count);
        for (std::size_t index = 0; index < event_count; ++index) {
            const auto metadata = registry.events.find(events[index]);
            const bool signal_event =
                registry.internal_signals.contains(events[index]);
            const auto binding =
                registry.runtime_objects.find(events[index]);
            if ((metadata == registry.events.end() && !signal_event)
                || binding == registry.runtime_objects.end()) {
                return FSIM_SC_INVALID_ARGUMENT;
            }
            signals.push_back(binding->second);
        }
        std::sort(signals.begin(), signals.end());
        signals.erase(
            std::unique(signals.begin(), signals.end()),
            signals.end());
        active_invocation->suspension =
            MethodSuspendResult{
                MethodSuspendKind::wait_event,
                0,
                std::move(signals),
                kind == FSIM_SC_EVENT_AND_LIST};
        return suspend_thread();
    } catch (...) {
        active_invocation->failure =
            "SystemC event-list trigger could not be recorded";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_wait_static(
    void* context) noexcept {
    if (context == nullptr || active_invocation == nullptr
        || active_invocation->registry != context) {
        return FSIM_SC_RUNTIME_ERROR;
    }
    try {
        active_invocation->suspension =
            MethodSuspendResult{
                MethodSuspendKind::static_sensitivity,
                0,
                {},
                false};
        return suspend_thread();
    } catch (...) {
        active_invocation->failure =
            "SystemC static-sensitivity wait could not be recorded";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_notify_mode(
    void* context,
    const fsim_sc_handle_v1 event,
    const std::uint64_t femtoseconds,
    const fsim_sc_notification_kind_v1 kind) noexcept {
    if (context == nullptr || active_invocation == nullptr
        || active_invocation->registry != context
        || (kind != FSIM_SC_NOTIFY_IMMEDIATE
            && kind != FSIM_SC_NOTIFY_DELTA
            && kind != FSIM_SC_NOTIFY_TIMED)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto metadata = registry.events.find(event);
        const auto binding = registry.runtime_objects.find(event);
        if (metadata == registry.events.end()
            || binding == registry.runtime_objects.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        if ((kind == FSIM_SC_NOTIFY_IMMEDIATE && femtoseconds != 0)
            || (kind == FSIM_SC_NOTIFY_DELTA && femtoseconds != 0)
            || (kind == FSIM_SC_NOTIFY_TIMED && femtoseconds == 0)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        std::uint64_t ticks = 0;
        const auto status =
            convert_delay(registry, femtoseconds, ticks);
        if (status != FSIM_SC_OK) {
            return status;
        }
        auto notification_kind =
            runtime::simir::EventNotificationKind::immediate;
        switch (kind) {
        case FSIM_SC_NOTIFY_IMMEDIATE:
            break;
        case FSIM_SC_NOTIFY_DELTA:
            notification_kind =
                runtime::simir::EventNotificationKind::delta;
            break;
        case FSIM_SC_NOTIFY_TIMED:
            notification_kind =
                runtime::simir::EventNotificationKind::timed;
            break;
        }
        active_invocation->context->notify_event(
            binding->second,
            ticks,
            notification_kind);
        return FSIM_SC_OK;
    } catch (const std::exception& exception) {
        active_invocation->failure = exception.what();
        return FSIM_SC_RUNTIME_ERROR;
    } catch (...) {
        active_invocation->failure =
            "unknown SystemC event notification failure";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_notify_delayed(
    void* context,
    const fsim_sc_handle_v1 event,
    const std::uint64_t femtoseconds) noexcept {
    if (context == nullptr || active_invocation == nullptr
        || active_invocation->registry != context) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto metadata = registry.events.find(event);
        const auto binding = registry.runtime_objects.find(event);
        if (metadata == registry.events.end()
            || binding == registry.runtime_objects.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        std::uint64_t ticks = 0;
        const auto status =
            convert_delay(registry, femtoseconds, ticks);
        if (status != FSIM_SC_OK) {
            return status;
        }
        active_invocation->context->notify_event(
            binding->second,
            ticks,
            runtime::simir::EventNotificationKind::delayed);
        return FSIM_SC_OK;
    } catch (const std::exception& exception) {
        active_invocation->failure = exception.what();
        return FSIM_SC_RUNTIME_ERROR;
    } catch (...) {
        active_invocation->failure =
            "unknown SystemC delayed-event notification failure";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_cancel_event(
    void* context,
    const fsim_sc_handle_v1 event) noexcept {
    if (context == nullptr || active_invocation == nullptr
        || active_invocation->registry != context) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto metadata = registry.events.find(event);
        const auto binding = registry.runtime_objects.find(event);
        if (metadata == registry.events.end()
            || binding == registry.runtime_objects.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        active_invocation->context->cancel_event(binding->second);
        return FSIM_SC_OK;
    } catch (const std::exception& exception) {
        active_invocation->failure = exception.what();
        return FSIM_SC_RUNTIME_ERROR;
    } catch (...) {
        active_invocation->failure =
            "unknown SystemC event cancellation failure";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_request_update(
    void* context,
    const fsim_sc_handle_v1 channel) noexcept {
    if (context == nullptr || active_invocation == nullptr
        || active_invocation->registry != context
        || active_invocation->context == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        if (!registry.primitive_channels.contains(channel)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        active_invocation->context->request_channel_update(channel);
        return FSIM_SC_OK;
    } catch (const std::exception& exception) {
        active_invocation->failure = exception.what();
        return FSIM_SC_RUNTIME_ERROR;
    } catch (...) {
        active_invocation->failure =
            "unknown SystemC primitive-channel update failure";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_notify(
    void* context,
    const fsim_sc_handle_v1 event,
    const std::uint64_t femtoseconds) noexcept {
    return registry_notify_mode(
        context,
        event,
        femtoseconds,
        femtoseconds == 0
            ? FSIM_SC_NOTIFY_IMMEDIATE
            : FSIM_SC_NOTIFY_TIMED);
}

extern "C" void registry_report(
    void* context, const int severity, const char* message) noexcept {
    if (context == nullptr || severity < 2
        || active_invocation == nullptr
        || active_invocation->registry != context) {
        return;
    }
    try {
        if (active_invocation->failure.empty()) {
            active_invocation->failure =
                message == nullptr || *message == '\0'
                ? "SystemC process reported a runtime failure"
                : message;
        }
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
    // Suspended stacks may still hold plug-in code addresses and module
    // references, so release them before destroying modules or unloading the
    // dynamic library.
    shutdown_threads(*impl_);
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
    host.wait_time = registry_wait_time;
    host.wait_event = registry_wait_event;
    host.notify_event = registry_notify;
    host.report = registry_report;
    host.register_foreign_child = registry_register_foreign_child;
    host.connect_foreign_port = registry_connect_foreign_port;
    host.set_process_initialize =
        registry_set_process_initialize;
    host.register_event = registry_register_event;
    host.notify_event_mode = registry_notify_mode;
    host.cancel_event = registry_cancel_event;
    host.wait_event_list = registry_wait_event_list;
    host.notify_event_delayed = registry_notify_delayed;
    host.register_primitive_channel =
        registry_register_primitive_channel;
    host.request_update = registry_request_update;
    host.register_signal = registry_register_signal;
    host.value_changed = registry_value_changed;
    host.bind_port = registry_bind_port;
    host.register_native_module = registry_register_native_module;
    host.register_lifecycle = registry_register_lifecycle;
    host.register_export = registry_register_export;
    host.bind_export = registry_bind_export;
    host.wait_static = registry_wait_static;
    host.set_foreign_child_actual =
        registry_set_foreign_child_actual;

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
    ModuleDescription pending;
    pending.handle = *handle;
    pending.parent = parent;
    pending.factory = factory;
    pending.instance = instance;
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
            impl_->rollback(*handle);
        }
        return std::nullopt;
    }

    auto description = impl_->collect(*handle);
    impl_->live.push_back(
        {description,
         found->second.destroy,
         found->second.user,
         object,
         Impl::LiveModule::LifecycleState::constructed});
    return description;
}

void HierarchyRegistry::bind_runtime_object(
    const fsim_sc_handle_v1 object,
    const std::uint32_t signal) {
    if (impl_ == nullptr
        || (!impl_->objects.contains(object)
            && !impl_->events.contains(object))) {
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

void HierarchyRegistry::set_time_resolution(
    const std::uint64_t femtoseconds_per_tick) {
    if (impl_ == nullptr || femtoseconds_per_tick == 0) {
        throw std::invalid_argument{
            "SystemC time resolution must be a positive femtosecond value"};
    }
    impl_->femtoseconds_per_tick = femtoseconds_per_tick;
}

void HierarchyRegistry::complete_elaboration(
    const std::span<const fsim_sc_handle_v1> roots) {
    if (impl_ == nullptr) {
        throw std::logic_error{"SystemC hierarchy registry is unavailable"};
    }
    std::vector<Impl::LiveModule*> modules;
    modules.reserve(roots.size());
    std::unordered_set<fsim_sc_handle_v1> unique;
    for (const auto root : roots) {
        if (root == 0 || !unique.insert(root).second) {
            throw std::invalid_argument{
                "SystemC lifecycle root handles must be nonzero and unique"};
        }
        auto* module = find_live_module(*impl_, root);
        if (module == nullptr) {
            throw std::invalid_argument{
                "unknown SystemC lifecycle root handle"};
        }
        if (module->lifecycle
            == Impl::LiveModule::LifecycleState::poisoned) {
            throw std::logic_error{
                "SystemC lifecycle root is poisoned"};
        }
        modules.push_back(module);
    }
    for (auto* module : modules) {
        if (module->lifecycle
            != Impl::LiveModule::LifecycleState::constructed) {
            continue;
        }
        try {
            invoke_lifecycle_entry(
                *impl_,
                module->description.lifecycle
                    .before_end_of_elaboration,
                module->description.lifecycle.user,
                "before_end_of_elaboration");
            module->lifecycle =
                Impl::LiveModule::LifecycleState::
                    before_elaboration_complete;
        } catch (...) {
            module->lifecycle =
                Impl::LiveModule::LifecycleState::poisoned;
            throw;
        }
    }
    for (auto* module : modules) {
        if (module->lifecycle
            != Impl::LiveModule::LifecycleState::
                before_elaboration_complete) {
            continue;
        }
        try {
            invoke_lifecycle_entry(
                *impl_,
                module->description.lifecycle.end_of_elaboration,
                module->description.lifecycle.user,
                "end_of_elaboration");
            module->lifecycle =
                Impl::LiveModule::LifecycleState::elaborated;
        } catch (...) {
            module->lifecycle =
                Impl::LiveModule::LifecycleState::poisoned;
            throw;
        }
    }
}

void HierarchyRegistry::start_simulation(
    const std::span<const fsim_sc_handle_v1> roots) {
    if (impl_ == nullptr) {
        throw std::logic_error{"SystemC hierarchy registry is unavailable"};
    }
    for (const auto root : roots) {
        auto* module = find_live_module(*impl_, root);
        if (module == nullptr) {
            throw std::invalid_argument{
                "unknown SystemC lifecycle root handle"};
        }
        if (module->lifecycle
            == Impl::LiveModule::LifecycleState::started) {
            continue;
        }
        if (module->lifecycle
            != Impl::LiveModule::LifecycleState::elaborated) {
            throw std::logic_error{
                "SystemC lifecycle root was not elaborated"};
        }
        try {
            invoke_lifecycle_entry(
                *impl_,
                module->description.lifecycle.start_of_simulation,
                module->description.lifecycle.user,
                "start_of_simulation");
            module->lifecycle =
                Impl::LiveModule::LifecycleState::started;
        } catch (...) {
            module->lifecycle =
                Impl::LiveModule::LifecycleState::poisoned;
            throw;
        }
    }
}

void HierarchyRegistry::end_simulation(
    const std::span<const fsim_sc_handle_v1> roots) {
    if (impl_ == nullptr) {
        throw std::logic_error{"SystemC hierarchy registry is unavailable"};
    }
    shutdown_threads(*impl_);
    std::exception_ptr failure;
    for (auto root = roots.rbegin(); root != roots.rend(); ++root) {
        auto* module = find_live_module(*impl_, *root);
        if (module == nullptr) {
            if (!failure) {
                failure = std::make_exception_ptr(
                    std::invalid_argument{
                        "unknown SystemC lifecycle root handle"});
            }
            continue;
        }
        if (module->lifecycle
            != Impl::LiveModule::LifecycleState::started) {
            continue;
        }
        try {
            invoke_lifecycle_entry(
                *impl_,
                module->description.lifecycle.end_of_simulation,
                module->description.lifecycle.user,
                "end_of_simulation");
            module->lifecycle =
                Impl::LiveModule::LifecycleState::ended;
        } catch (...) {
            module->lifecycle =
                Impl::LiveModule::LifecycleState::poisoned;
            if (!failure) {
                failure = std::current_exception();
            }
        }
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

MethodSuspendResult HierarchyRegistry::invoke_process(
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
        const auto* owner = find_module_description(
            module.description, found->second.module);
        if (owner == nullptr
            || found->second.process >= owner->processes.size()) {
            continue;
        }
        description = &owner->processes[found->second.process];
        break;
    }
    if (description == nullptr || description->entry == nullptr) {
        throw std::logic_error{
            "SystemC process has no executable callback"};
    }
    if (active_invocation != nullptr) {
        throw std::logic_error{
            "recursive SystemC process invocation is not supported"};
    }
    if (description->kind == FSIM_SC_METHOD) {
        ActiveInvocation invocation{};
        invocation.registry = impl_.get();
        invocation.context = &context;
        InvocationScope scope{invocation};
        try {
            description->entry(description->user);
        } catch (const std::exception& exception) {
            invocation.failure =
                "SystemC process callback escaped with an exception: "
                + std::string{exception.what()};
        } catch (...) {
            invocation.failure =
                "SystemC process callback escaped with an "
                "unknown exception";
        }
        if (!invocation.failure.empty()) {
            throw std::runtime_error{std::move(invocation.failure)};
        }
        if (invocation.suspension) {
            return *invocation.suspension;
        }
        return {
            description->sensitivity.empty()
                ? MethodSuspendKind::halt
                : MethodSuspendKind::static_sensitivity,
            0,
            {},
            false};
    }

#if defined(FSIM_HAS_BOOST_CONTEXT)
    if (description->kind != FSIM_SC_THREAD
        && description->kind != FSIM_SC_CTHREAD) {
        throw std::logic_error{
            "SystemC process has an invalid process kind"};
    }
    auto& state = impl_->thread_fibers[process];
    if (!state) {
        state = std::make_unique<ThreadFiberState>();
    }
    if (state->terminated) {
        return {MethodSuspendKind::halt, 0, {}, false};
    }

    ActiveInvocation invocation{};
    invocation.registry = impl_.get();
    invocation.context = &context;
    invocation.thread = state.get();
    InvocationScope scope{invocation};
    try {
        if (!state->started) {
            state->started = true;
            const auto entry = description->entry;
            void* const user = description->user;
            auto* const fiber_state = state.get();
            state->process = boost::context::fiber{
                [entry, user, fiber_state](
                    boost::context::fiber&& caller) mutable {
                    fiber_state->caller = std::move(caller);
                    entry(user);
                    fiber_state->terminated = true;
                    return std::move(fiber_state->caller);
                }};
        }
        state->process = std::move(state->process).resume();
    } catch (const std::exception& exception) {
        invocation.failure =
            "SystemC thread fiber failed: "
            + std::string{exception.what()};
    } catch (...) {
        invocation.failure =
            "SystemC thread fiber failed with an unknown exception";
    }
    if (!invocation.failure.empty()) {
        state->terminated = true;
        throw std::runtime_error{std::move(invocation.failure)};
    }
    if (invocation.suspension) {
        return *invocation.suspension;
    }
    if (state->terminated) {
        return {MethodSuspendKind::halt, 0, {}, false};
    }
    throw std::runtime_error{
        "SystemC thread yielded without a wait request"};
#else
    throw std::logic_error{
        "SystemC thread execution requires Boost.Context 1.91.0"};
#endif
}

void HierarchyRegistry::invoke_primitive_channel(
    const fsim_sc_handle_v1 channel,
    runtime::simir::ProcessExecutionContext& context) {
    if (impl_ == nullptr) {
        throw std::logic_error{"SystemC hierarchy registry is unavailable"};
    }
    const auto found = impl_->primitive_channels.find(channel);
    if (found == impl_->primitive_channels.end()) {
        throw std::invalid_argument{
            "unknown SystemC primitive-channel handle"};
    }
    const PrimitiveChannelDescription* description = nullptr;
    for (const auto& module : impl_->live) {
        const auto* owner = find_module_description(
            module.description, found->second.module);
        if (owner == nullptr
            || found->second.channel
                >= owner->primitive_channels.size()) {
            continue;
        }
        description =
            &owner->primitive_channels[found->second.channel];
        break;
    }
    if (description == nullptr || description->update == nullptr) {
        throw std::logic_error{
            "SystemC primitive channel has no executable update callback"};
    }
    if (active_invocation != nullptr) {
        throw std::logic_error{
            "recursive SystemC callback invocation is not supported"};
    }
    ActiveInvocation invocation{};
    invocation.registry = impl_.get();
    invocation.context = &context;
    InvocationScope scope{invocation};
    try {
        description->update(description->user);
    } catch (const std::exception& exception) {
        invocation.failure =
            "SystemC primitive-channel callback escaped with an "
            "exception: " + std::string{exception.what()};
    } catch (...) {
        invocation.failure =
            "SystemC primitive-channel callback escaped with an "
            "unknown exception";
    }
    if (invocation.suspension && invocation.failure.empty()) {
        invocation.failure =
            "SystemC primitive-channel update cannot suspend";
    }
    if (!invocation.failure.empty()) {
        throw std::runtime_error{std::move(invocation.failure)};
    }
}

const std::filesystem::path& HierarchyRegistry::path() const noexcept {
    return impl_->path;
}

} // namespace fsim::systemc

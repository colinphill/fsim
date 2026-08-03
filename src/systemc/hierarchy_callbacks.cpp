// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_internal.hpp"

namespace fsim::systemc::hierarchy_detail {

thread_local ActiveInvocation* active_invocation = nullptr;

InvocationScope::InvocationScope(ActiveInvocation& invocation) noexcept  {
        active_invocation = &invocation;
    }

InvocationScope::~InvocationScope()  { active_invocation = nullptr; }







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
    constexpr std::size_t maximum_shutdown_resumes = 1024;
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
            std::size_t resumes = 0;
            while (!state->terminated && state->process
                   && resumes++ < maximum_shutdown_resumes) {
                state->process = std::move(state->process).resume();
            }
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

[[nodiscard]] bool compatible_port_chain(
    const fsim_sc_port_direction_v1 child,
    const fsim_sc_port_direction_v1 parent) noexcept {
    switch (child) {
    case FSIM_SC_INPUT:
        return parent == FSIM_SC_INPUT || parent == FSIM_SC_INOUT;
    case FSIM_SC_OUTPUT:
        return parent == FSIM_SC_OUTPUT || parent == FSIM_SC_INOUT;
    case FSIM_SC_INOUT:
        return parent == FSIM_SC_INOUT;
    }
    return false;
}

[[nodiscard]] bool valid_construction_type(
    const fsim_sc_construction_type_v1 type) noexcept {
    return type >= FSIM_SC_CONSTRUCTION_INTEGER
        && type <= FSIM_SC_CONSTRUCTION_BIT;
}

[[nodiscard]] bool valid_construction_value(
    const fsim_sc_construction_type_v1 type,
    const std::int64_t value) noexcept {
    switch (type) {
    case FSIM_SC_CONSTRUCTION_INTEGER:
        return true;
    case FSIM_SC_CONSTRUCTION_NATURAL:
        return value >= 0;
    case FSIM_SC_CONSTRUCTION_POSITIVE:
        return value > 0;
    case FSIM_SC_CONSTRUCTION_BOOLEAN:
    case FSIM_SC_CONSTRUCTION_BIT:
        return value == 0 || value == 1;
    }
    return false;
}

[[nodiscard]] bool object_name_in_use(
    const HierarchyRegistry::Impl& registry,
    const ModuleDescription& module,
    const std::string_view name,
    const fsim_sc_handle_v1 ignored = 0) {
    const auto named = [&](const auto& objects) {
        return std::any_of(
            objects.begin(), objects.end(),
            [&](const auto& object) {
                return object.handle != ignored && object.name == name;
            });
    };
    if (named(module.ports) || named(module.foreign_children)
        || named(module.processes) || named(module.events)
        || named(module.primitive_channels)
        || named(module.internal_signals) || named(module.exports)
        || named(module.metadata_objects)) {
        return true;
    }
    const auto children = registry.native_children.find(module.handle);
    return children != registry.native_children.end()
        && std::any_of(
            children->second.begin(), children->second.end(),
            [&](const fsim_sc_handle_v1 handle) {
                if (handle == ignored) {
                    return false;
                }
                const auto child = registry.pending.find(handle);
                return child != registry.pending.end()
                    && child->second.instance == name;
            });
}

[[nodiscard]] bool valid_metadata_kind(const char* kind) noexcept {
    if (kind == nullptr || *kind == '\0') {
        return false;
    }
    std::size_t size = 0;
    for (; kind[size] != '\0'; ++size) {
        const auto byte = static_cast<unsigned char>(kind[size]);
        if (size >= 127
            || (std::isalnum(byte) == 0 && kind[size] != '_'
                && kind[size] != ':' && kind[size] != '.'
                && kind[size] != '-')) {
            return false;
        }
    }
    return size != 0;
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
    if (invocation.suspension && invocation.failure.empty()) {
        invocation.failure =
            "SystemC lifecycle callbacks cannot suspend";
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
            || object_name_in_use(registry, found->second, name)) {
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
                HierarchyRegistry::Impl::Object::Kind::port,
                direction});
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
            || object_name_in_use(registry, found->second, name)) {
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
        if (object_name_in_use(registry, found->second, event_name)) {
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
        if (object_name_in_use(registry, found->second, channel_name)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index =
            found->second.primitive_channels.size();
        found->second.primitive_channels.push_back(
            {*handle,
             std::move(channel_name),
             update,
             user,
             "sc_prim_channel"});
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
        if (found == registry.pending.end()
            || object_name_in_use(registry, found->second, name)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        const auto index = found->second.foreign_children.size();
        found->second.foreign_children.push_back(
            {*handle, name, {}, {}, false});
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

extern "C" fsim_sc_status_v1 registry_get_construction_value(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    std::int64_t* result) noexcept {
    if (context == nullptr || module == 0 || name == nullptr
        || *name == '\0' || result == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto value = std::find_if(
            found->second.construction_values.begin(),
            found->second.construction_values.end(),
            [&](const auto& candidate) {
                return candidate.first == name;
            });
        if (value == found->second.construction_values.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        *result = value->second;
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
            {name, direction, encoding, width, object, 0});
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
            || object_name_in_use(registry, found->second, name)) {
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
        if (object_name_in_use(
                registry, pending->second, signal_name, channel)) {
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
                != target_object->second.width
            || (target_object->second.kind
                    == HierarchyRegistry::Impl::Object::Kind::port
                && !compatible_port_chain(
                    port_object->second.direction,
                    target_object->second.direction))
            || (target_object->second.kind
                    == HierarchyRegistry::Impl::Object::Kind::
                        export_object
                && port_object->second.direction != FSIM_SC_INPUT
                && !target_object->second.writable)) {
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
                != target_object->second.width
            || (export_object->second.writable
                && !target_object->second.writable)) {
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

extern "C" fsim_sc_status_v1 registry_set_export_writable(
    void* context,
    const fsim_sc_handle_v1 export_handle,
    const std::uint8_t writable) noexcept {
    if (context == nullptr || export_handle == 0 || writable > 1) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto object = registry.objects.find(export_handle);
        if (object == registry.objects.end()
            || object->second.kind
                != HierarchyRegistry::Impl::Object::Kind::export_object) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto module = registry.pending.find(object->second.module);
        if (module == registry.pending.end()
            || object->second.index >= module->second.exports.size()
            || module->second.exports[object->second.index].bound_object
                != 0) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        object->second.writable = writable != 0;
        module->second.exports[object->second.index].writable =
            writable != 0;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_metadata_object(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_metadata_category_v1 category,
    const char* kind,
    fsim_sc_handle_v1* result) noexcept {
    if (context == nullptr || name == nullptr || *name == '\0'
        || (category != FSIM_SC_METADATA_PORT
            && category != FSIM_SC_METADATA_EXPORT)
        || !valid_metadata_kind(kind) || result == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.pending.find(module);
        if (found == registry.pending.end()
            || object_name_in_use(registry, found->second, name)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto handle = registry.allocate_handle();
        if (!handle) {
            return FSIM_SC_RUNTIME_ERROR;
        }
        found->second.metadata_objects.push_back(
            {*handle, name, category, kind});
        *result = *handle;
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_set_primitive_channel_kind(
    void* context,
    const fsim_sc_handle_v1 channel,
    const char* kind) noexcept {
    if (context == nullptr || channel == 0
        || !valid_metadata_kind(kind)) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto registered = registry.primitive_channels.find(channel);
        if (registered == registry.primitive_channels.end()) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto module = registry.pending.find(registered->second.module);
        if (module == registry.pending.end()
            || registered->second.channel
                >= module->second.primitive_channels.size()
            || registry.internal_signals.contains(channel)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        module->second
            .primitive_channels[registered->second.channel]
            .kind = kind;
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
        if (object_name_in_use(
                registry, parent_module->second, name)) {
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

extern "C" fsim_sc_status_v1 registry_mark_hdl_module(
    void* context,
    const fsim_sc_handle_v1 module) noexcept {
    if (context == nullptr || module == 0) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        if (!registry.pending.contains(module)) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        const auto [found, inserted] =
            registry.hdl_modules.emplace(module, decltype(
                HierarchyRegistry::Impl::hdl_modules)::mapped_type{});
        (void)found;
        return inserted ? FSIM_SC_OK : FSIM_SC_INVALID_ARGUMENT;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_set_hdl_module_actual(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const std::int64_t value) noexcept {
    if (context == nullptr || module == 0 || name == nullptr
        || *name == '\0') {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.hdl_modules.find(module);
        if (found == registry.hdl_modules.end()
            || std::any_of(
                found->second.begin(), found->second.end(),
                [&](const auto& actual) {
                    return actual.first == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        found->second.emplace_back(name, value);
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
                MethodSuspendKind::wait_for,
                ticks,
                {},
                false,
                std::nullopt};
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
                false,
                std::nullopt};
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
                kind == FSIM_SC_EVENT_AND_LIST,
                std::nullopt};
        return suspend_thread();
    } catch (...) {
        active_invocation->failure =
            "SystemC event-list trigger could not be recorded";
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_wait_event_timeout(
    void* context,
    const std::uint64_t femtoseconds,
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
        std::uint64_t ticks = 0;
        const auto status =
            convert_delay(registry, femtoseconds, ticks);
        if (status != FSIM_SC_OK) {
            return status;
        }
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
        active_invocation->suspension = MethodSuspendResult{
            MethodSuspendKind::wait_event,
            0,
            std::move(signals),
            kind == FSIM_SC_EVENT_AND_LIST,
            ticks};
        return suspend_thread();
    } catch (...) {
        active_invocation->failure =
            "SystemC timed event-list trigger could not be recorded";
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
                false,
                std::nullopt};
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
        || active_invocation->context == nullptr
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
        || active_invocation->registry != context
        || active_invocation->context == nullptr) {
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
        || active_invocation->registry != context
        || active_invocation->context == nullptr) {
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
    if (context == nullptr || severity < 2) {
        return;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        if (active_invocation == nullptr
            || active_invocation->registry != context) {
            if (registry.elaboration_failure.empty()) {
                registry.elaboration_failure =
                    message == nullptr || *message == '\0'
                    ? "SystemC module construction reported a failure"
                    : message;
            }
            return;
        }
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
                       factory, nullptr, destroy, user, {}})
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
                       nullptr, factory, destroy, user, {}})
                       .second
            ? FSIM_SC_OK
            : FSIM_SC_INVALID_ARGUMENT;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}

extern "C" fsim_sc_status_v1 registry_register_factory_parameter(
    void* context,
    const char* factory,
    const char* name,
    const fsim_sc_construction_type_v1 type,
    const std::uint8_t has_default,
    const std::int64_t default_value) noexcept {
    if (context == nullptr || factory == nullptr || *factory == '\0'
        || name == nullptr || *name == '\0'
        || !valid_construction_type(type)
        || has_default > 1
        || (has_default != 0
            && !valid_construction_value(type, default_value))) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    try {
        auto& registry =
            *static_cast<HierarchyRegistry::Impl*>(context);
        const auto found = registry.factories.find(factory);
        if (found == registry.factories.end()
            || std::any_of(
                found->second.parameters.begin(),
                found->second.parameters.end(),
                [&](const auto& parameter) {
                    return parameter.name == name;
                })) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        found->second.parameters.push_back({
            name,
            type,
            has_default != 0
                ? std::optional<std::int64_t>{default_value}
                : std::nullopt,
        });
        return FSIM_SC_OK;
    } catch (...) {
        return FSIM_SC_RUNTIME_ERROR;
    }
}


} // namespace fsim::systemc::hierarchy_detail

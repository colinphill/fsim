// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_internal.hpp"

namespace fsim::systemc {
using namespace hierarchy_detail;

namespace {

void collect_object_info(
    const ModuleDescription& module,
    const std::string& path,
    const fsim_sc_handle_v1 parent,
    std::vector<HierarchyObjectInfo>& result) {
    result.push_back(
        {module.handle, parent, module.instance, path,
         HierarchyObjectKind::module});
    std::vector<HierarchyObjectInfo> direct;
    direct.reserve(
        module.ports.size() + module.foreign_children.size()
        + module.processes.size() + module.events.size()
        + module.primitive_channels.size()
        + module.internal_signals.size() + module.exports.size()
        + module.metadata_objects.size()
        + module.native_children.size());
    const auto append = [&](const auto& objects, const auto kind) {
        for (const auto& object : objects) {
            direct.push_back(
                {object.handle, module.handle, object.name,
                 path + "." + object.name, kind});
        }
    };
    append(module.ports, HierarchyObjectKind::port);
    append(module.foreign_children, HierarchyObjectKind::foreign_child);
    append(module.processes, HierarchyObjectKind::process);
    append(module.events, HierarchyObjectKind::event);
    const auto is_signal = [&](const fsim_sc_handle_v1 handle) {
        return std::any_of(
            module.internal_signals.begin(),
            module.internal_signals.end(),
            [&](const InternalSignalDescription& signal) {
                return signal.handle == handle;
            });
    };
    for (const auto& channel : module.primitive_channels) {
        if (!is_signal(channel.handle)) {
            direct.push_back(
                {channel.handle, module.handle, channel.name,
                 path + "." + channel.name,
                 HierarchyObjectKind::primitive_channel});
        }
    }
    append(module.internal_signals, HierarchyObjectKind::signal);
    append(module.exports, HierarchyObjectKind::export_object);
    for (const auto& object : module.metadata_objects) {
        direct.push_back({
            object.handle,
            module.handle,
            object.name,
            path + "." + object.name,
            object.category == FSIM_SC_METADATA_PORT
                ? HierarchyObjectKind::port
                : HierarchyObjectKind::export_object});
    }
    for (const auto& child : module.native_children) {
        direct.push_back(
            {child.handle, module.handle, child.instance,
             path + "." + child.instance,
             HierarchyObjectKind::module});
    }
    std::sort(
        direct.begin(), direct.end(),
        [](const auto& left, const auto& right) {
            return left.handle < right.handle;
        });
    for (const auto& object : direct) {
        if (object.kind != HierarchyObjectKind::module) {
            result.push_back(object);
            continue;
        }
        const auto child = std::find_if(
            module.native_children.begin(), module.native_children.end(),
            [&](const ModuleDescription& candidate) {
                return candidate.handle == object.handle;
            });
        if (child != module.native_children.end()) {
            collect_object_info(*child, object.path, module.handle, result);
        }
    }
}

[[nodiscard]] std::vector<HierarchyObjectInfo> live_object_info(
    const HierarchyRegistry::Impl& registry) {
    std::vector<HierarchyObjectInfo> result;
    for (const auto& root : registry.live) {
        collect_object_info(
            root.description,
            root.description.instance,
            root.description.parent,
            result);
    }
    return result;
}

} // namespace

[[nodiscard]] std::optional<fsim_sc_handle_v1> HierarchyRegistry::Impl::allocate_handle()  {
        if (next_handle == 0
            || next_handle
                == std::numeric_limits<fsim_sc_handle_v1>::max()) {
            return std::nullopt;
        }
        return next_handle++;
    }

void HierarchyRegistry::Impl::rollback(const fsim_sc_handle_v1 module) noexcept  {
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

[[nodiscard]] ModuleDescription HierarchyRegistry::Impl::collect(
    const fsim_sc_handle_v1 module)  {
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
    host.get_construction_value =
        registry_get_construction_value;
    host.set_export_writable = registry_set_export_writable;
    host.register_metadata_object = registry_register_metadata_object;
    host.set_primitive_channel_kind =
        registry_set_primitive_channel_kind;
    host.wait_event_timeout = registry_wait_event_timeout;

    fsim_sc_registrar_v1 registrar{};
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size = sizeof(registrar);
    registrar.context = impl.get();
    registrar.register_factory = registry_register_factory;
    registrar.register_elaboration_factory =
        registry_register_elaboration_factory;
    registrar.register_factory_parameter =
        registry_register_factory_parameter;

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

std::optional<HierarchyObjectInfo> HierarchyRegistry::object_info(
    const fsim_sc_handle_v1 handle) const {
    if (impl_ == nullptr || handle == 0) {
        return std::nullopt;
    }
    auto objects = live_object_info(*impl_);
    const auto found = std::find_if(
        objects.begin(), objects.end(),
        [&](const HierarchyObjectInfo& object) {
            return object.handle == handle;
        });
    return found == objects.end()
        ? std::nullopt
        : std::optional<HierarchyObjectInfo>{*found};
}

std::vector<HierarchyObjectInfo> HierarchyRegistry::child_objects(
    const fsim_sc_handle_v1 parent) const {
    std::vector<HierarchyObjectInfo> result;
    if (impl_ == nullptr || parent == 0) {
        return result;
    }
    for (auto& object : live_object_info(*impl_)) {
        if (object.parent == parent) {
            result.push_back(std::move(object));
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            return left.handle < right.handle;
        });
    return result;
}

std::optional<HierarchyObjectInfo> HierarchyRegistry::find_object(
    const fsim_sc_handle_v1 root,
    const std::string_view path) const {
    if (impl_ == nullptr || root == 0 || path.empty()) {
        return std::nullopt;
    }
    auto objects = live_object_info(*impl_);
    const auto root_object = std::find_if(
        objects.begin(), objects.end(),
        [&](const HierarchyObjectInfo& object) {
            return object.handle == root
                && object.kind == HierarchyObjectKind::module;
        });
    if (root_object == objects.end()) {
        return std::nullopt;
    }
    const auto absolute =
        path == root_object->path
        || path.starts_with(root_object->path + ".")
        ? std::string{path}
        : root_object->path + "." + std::string{path};
    for (const auto& candidate : objects) {
        if (candidate.path != absolute) {
            continue;
        }
        auto ancestor = candidate.handle;
        while (ancestor != 0 && ancestor != root) {
            const auto current = std::find_if(
                objects.begin(), objects.end(),
                [&](const HierarchyObjectInfo& object) {
                    return object.handle == ancestor;
                });
            if (current == objects.end()) {
                ancestor = 0;
                break;
            }
            ancestor = current->parent;
        }
        if (ancestor == root) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<ConstructionParameterDescription>>
HierarchyRegistry::factory_parameters(
    const std::string_view name) const {
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    const auto found = impl_->factories.find(std::string{name});
    if (found == impl_->factories.end()) {
        return std::nullopt;
    }
    return found->second.parameters;
}

std::optional<ModuleDescription> HierarchyRegistry::instantiate(
    const std::string_view factory,
    const std::string_view instance,
    const fsim_sc_handle_v1 parent,
    std::string& error) {
    return instantiate(factory, instance, parent, {}, error);
}

std::optional<ModuleDescription> HierarchyRegistry::instantiate(
    const std::string_view factory,
    const std::string_view instance,
    const fsim_sc_handle_v1 parent,
    const std::span<
        const std::pair<std::string, std::int64_t>>
        construction_actuals,
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
    std::vector<std::pair<std::string, std::int64_t>>
        construction_values;
    construction_values.reserve(found->second.parameters.size());
    for (const auto& parameter : found->second.parameters) {
        const auto actual = std::find_if(
            construction_actuals.begin(),
            construction_actuals.end(),
            [&](const auto& candidate) {
                return candidate.first == parameter.name;
            });
        const auto duplicate =
            actual != construction_actuals.end()
            && std::find_if(
                   std::next(actual),
                   construction_actuals.end(),
                   [&](const auto& candidate) {
                       return candidate.first == parameter.name;
                   })
                != construction_actuals.end();
        if (duplicate) {
            error = "SystemC construction parameter '"
                + parameter.name + "' receives more than one actual";
            return std::nullopt;
        }
        const auto value =
            actual != construction_actuals.end()
            ? std::optional<std::int64_t>{actual->second}
            : parameter.default_value;
        if (!value) {
            error = "SystemC construction parameter '"
                + parameter.name + "' requires an actual";
            return std::nullopt;
        }
        if (!valid_construction_value(parameter.type, *value)) {
            error = "SystemC construction parameter '"
                + parameter.name + "' violates its declared type";
            return std::nullopt;
        }
        construction_values.emplace_back(
            parameter.name, *value);
    }
    for (const auto& actual : construction_actuals) {
        if (std::none_of(
                found->second.parameters.begin(),
                found->second.parameters.end(),
                [&](const auto& parameter) {
                    return parameter.name == actual.first;
                })) {
            error = "unknown SystemC construction parameter '"
                + actual.first + "'";
            return std::nullopt;
        }
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
    pending.construction_values =
        std::move(construction_values);
    impl_->pending.emplace(*handle, pending);

    void* object = nullptr;
    fsim_sc_status_v1 status = FSIM_SC_RUNTIME_ERROR;
    impl_->elaboration_failure.clear();
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
    if (error.empty() && !impl_->elaboration_failure.empty()) {
        error = "SystemC factory '" + std::string{factory}
            + "' failed during elaboration: "
            + impl_->elaboration_failure;
    }
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
        impl_->elaboration_failure.clear();
        return std::nullopt;
    }

    auto description = impl_->collect(*handle);
    impl_->elaboration_failure.clear();
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
            false,
            std::nullopt};
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
        return {
            MethodSuspendKind::halt,
            0,
            {},
            false,
            std::nullopt};
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
        return {
            MethodSuspendKind::halt,
            0,
            {},
            false,
            std::nullopt};
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

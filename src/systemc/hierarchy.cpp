// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_internal.hpp"
#include "context_activation.hpp"

#include <atomic>

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
        module.ports.size() + module.processes.size()
        + module.internal_signals.size()
        + module.exports.size()
        + module.native_children.size());
    const auto append = [&](const auto& objects, const auto kind) {
        for (const auto& object : objects) {
            direct.push_back(
                {object.handle, module.handle, object.name,
                 path + "." + object.name, kind});
        }
    };
    append(module.ports, HierarchyObjectKind::port);
    append(module.processes, HierarchyObjectKind::process);
    append(module.internal_signals, HierarchyObjectKind::signal);
    append(module.exports, HierarchyObjectKind::export_object);
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
        static std::atomic<fsim_sc_handle_v1> next_handle{1};
        const auto handle = next_handle.fetch_add(1, std::memory_order_relaxed);
        if (handle == 0
            || handle == std::numeric_limits<fsim_sc_handle_v1>::max()) {
            return std::nullopt;
        }
        return handle;
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
        for (const auto& process : description.processes) {
            processes.erase(process.handle);
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
    HierarchyRegistry&& other) noexcept {
    if (this != &other) {
        reset();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

HierarchyRegistry::~HierarchyRegistry() {
    reset();
}

void HierarchyRegistry::reset() noexcept {
    if (!impl_) {
        return;
    }
    std::unique_ptr<detail::ContextActivation> activation;
    if (impl_->owned_context) {
        activation = std::make_unique<detail::ContextActivation>(
            impl_->owned_context.get());
    }
    // Suspended stacks may still hold plug-in code addresses and module
    // references, so release them before destroying modules or unloading the
    // dynamic library.
    // A caller may abandon a simulation after start without explicitly
    // entering its terminal phase. Give every still-started root exactly one
    // best-effort end_of_simulation callback while the image and host table
    // remain live. Explicitly ended and poisoned roots are never repeated.
    for (auto instance = impl_->live.rbegin();
         instance != impl_->live.rend(); ++instance) {
        if (instance->lifecycle
            != Impl::LiveModule::LifecycleState::started) {
            continue;
        }
        try {
            invoke_lifecycle_entry(
                *impl_,
                instance->description.lifecycle.end_of_simulation,
                instance->description.lifecycle.user,
                "end_of_simulation");
            instance->lifecycle =
                Impl::LiveModule::LifecycleState::ended;
        } catch (...) {
            instance->lifecycle =
                Impl::LiveModule::LifecycleState::poisoned;
        }
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
    impl_.reset();
}

std::unique_ptr<HierarchyRegistry> HierarchyRegistry::load(
    const std::filesystem::path& path,
    std::string& error,
    const bool isolated_kernel) {
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    if (isolated_kernel) {
        impl->owned_context = std::make_unique<sc_core::sc_simcontext>();
    }
    std::unique_ptr<detail::ContextActivation> activation;
    if (impl->owned_context) {
        activation = std::make_unique<detail::ContextActivation>(
            impl->owned_context.get());
    }

    fsim_sc_host_v1 host{};
    host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    host.struct_size = sizeof(host);
    host.context = impl.get();
    host.register_port = registry_register_port;
    host.register_process = registry_register_process;
    host.add_sensitivity = registry_add_sensitivity;
    host.read_value = registry_read;
    host.write_value = registry_write;
    host.report = registry_report;
    host.register_signal = registry_register_signal;
    host.bind_port = registry_bind_port;
    host.register_native_module = registry_register_native_module;
    host.register_lifecycle = registry_register_lifecycle;
    host.register_export = registry_register_export;
    host.bind_export = registry_bind_export;
    host.get_construction_value =
        registry_get_construction_value;
    host.set_export_writable = registry_set_export_writable;
    host.wait_for_input_or_native_activity =
        registry_wait_for_input_or_native_activity;
    host.current_time_femtoseconds =
        registry_current_time_femtoseconds;

    Impl staged_registrations;
    fsim_sc_registrar_v1 registrar{};
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size = sizeof(registrar);
    registrar.context = &staged_registrations;
    registrar.register_elaboration_factory =
        registry_register_elaboration_factory;
    registrar.register_factory_parameter =
        registry_register_factory_parameter;

    auto plugin = Plugin::load(path, host, registrar, error, true);
    if (!plugin) {
        return nullptr;
    }
    impl->factories.swap(staged_registrations.factories);
    impl->plugin = std::move(plugin);
    return std::unique_ptr<HierarchyRegistry>{
        new HierarchyRegistry{std::move(impl)}};
}

bool HierarchyRegistry::has_factory(
    const std::string_view name) const noexcept {
    return impl_ != nullptr
        && impl_->factories.contains(std::string{name});
}

std::size_t HierarchyRegistry::factory_count() const noexcept {
    return impl_ == nullptr ? 0 : impl_->factories.size();
}

std::vector<std::string> HierarchyRegistry::factory_names() const {
    std::vector<std::string> result;
    if (impl_ == nullptr) {
        return result;
    }
    result.reserve(impl_->factories.size());
    for (const auto& [name, factory] : impl_->factories) {
        (void)factory;
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool HierarchyRegistry::owns_handle(
    const fsim_sc_handle_v1 handle) const noexcept {
    if (impl_ == nullptr || handle == 0) {
        return false;
    }
    return impl_->objects.contains(handle)
        || impl_->processes.contains(handle)
        || impl_->pending.contains(handle)
        || std::ranges::any_of(
            impl_->live,
            [&](const auto& module) {
              return module.description.handle == handle;
            });
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

    std::unique_ptr<detail::ContextActivation> activation;
    if (impl_->owned_context) {
        activation = std::make_unique<detail::ContextActivation>(
            impl_->owned_context.get());
    }

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
        || !impl_->objects.contains(object)) {
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
    std::unique_ptr<detail::ContextActivation> activation;
    if (impl_->owned_context) {
        activation = std::make_unique<detail::ContextActivation>(
            impl_->owned_context.get());
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
    std::unique_ptr<detail::ContextActivation> activation;
    if (impl_->owned_context) {
        activation = std::make_unique<detail::ContextActivation>(
            impl_->owned_context.get());
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
    const std::span<const fsim_sc_handle_v1> roots,
    const std::uint64_t current_time) {
    if (impl_ == nullptr) {
        throw std::logic_error{"SystemC hierarchy registry is unavailable"};
    }
    std::unique_ptr<detail::ContextActivation> activation;
    if (impl_->owned_context) {
        activation = std::make_unique<detail::ContextActivation>(
            impl_->owned_context.get());
    }
    impl_->lifecycle_time = current_time;
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
    std::unique_ptr<detail::ContextActivation> activation;
    if (impl_->owned_context) {
        activation = std::make_unique<detail::ContextActivation>(
            impl_->owned_context.get());
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
            "SystemC process callback escaped with an unknown exception";
    }
    if (!invocation.failure.empty()) {
        throw std::runtime_error{std::move(invocation.failure)};
    }
    if (invocation.suspension) {
        auto suspension = std::move(*invocation.suspension);
        if (suspension.kind == MethodSuspendKind::wait_event
            && suspension.event_signals.empty()) {
            for (const auto& sensitivity : description->sensitivity) {
                const auto binding =
                    impl_->runtime_objects.find(sensitivity.object);
                if (binding == impl_->runtime_objects.end()) {
                    throw std::logic_error{
                        "SystemC bridge sensitivity is not runtime-bound"};
                }
                suspension.event_signals.push_back(binding->second);
            }
            std::ranges::sort(suspension.event_signals);
            const auto [first_duplicate, end] =
                std::ranges::unique(suspension.event_signals);
            suspension.event_signals.erase(first_duplicate, end);
            if (suspension.event_signals.empty()
                && suspension.timeout_ticks) {
                suspension.kind = MethodSuspendKind::wait_for;
                suspension.delay_ticks = *suspension.timeout_ticks;
                suspension.timeout_ticks.reset();
            }
        }
        return suspension;
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
const std::filesystem::path& HierarchyRegistry::path() const noexcept {
    return impl_->path;
}

} // namespace fsim::systemc

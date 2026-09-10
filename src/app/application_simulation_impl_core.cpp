// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

namespace fsim::app {

[[nodiscard]] std::filesystem::path Simulation::Impl::coverage_database_file(
    const std::string_view filename) const
{
    std::filesystem::path relative;
    try {
        relative = support::path_from_utf8(filename).lexically_normal();
    } catch (const std::exception&) {
        throw std::runtime_error {
            "coverage database filename is not a valid UTF-8 path"
        };
    }
    if (relative.empty() || relative.is_absolute()
        || relative.has_root_name()
        || std::ranges::any_of(relative, [](const auto& component) {
               return component == "..";
           })) {
        throw std::runtime_error {
            "coverage database filename must remain beneath the project file root"
        };
    }
    return (built.file_root / relative).lexically_normal();
}

void Simulation::Impl::load_coverage_database(const std::filesystem::path& path)
{
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error {
            "cannot inspect coverage database: " + error.message()
        };
    }
    if (bytes > runtime::simir::maximum_memory_file_bytes) {
        throw std::runtime_error {
            "coverage database exceeds the governed file-size budget"
        };
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error { "cannot open coverage database" };
    }
    std::string contents(
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { });
    if (!input.good() && !input.eof()) {
        throw std::runtime_error { "cannot read coverage database" };
    }
    diagnostic::Engine diagnostics;
    auto loaded = deserialize_systemverilog_coverage_state(
        contents, support::path_to_utf8(path), diagnostics);
    if (!loaded) {
        const auto& entries = diagnostics.diagnostics();
        throw std::runtime_error {
            entries.empty()
                ? "coverage database is malformed"
                : entries.front().message
        };
    }
    std::string merge_error;
    if (!frontend::merge_systemverilog_coverage_state(
            built.systemverilog_coverage, *loaded, merge_error)) {
        throw std::runtime_error { std::move(merge_error) };
    }
}

void Simulation::Impl::save_coverage_database()
{
    if (!coverage_database_path) {
        return;
    }
    diagnostic::Engine diagnostics;
    auto contents = serialize_systemverilog_coverage_state(
        built.systemverilog_coverage, diagnostics);
    if (!contents) {
        const auto& entries = diagnostics.diagnostics();
        throw std::runtime_error {
            entries.empty()
                ? "cannot serialize coverage database"
                : entries.front().message
        };
    }
    std::error_code error;
    std::filesystem::create_directories(
        coverage_database_path->parent_path(), error);
    if (error) {
        throw std::runtime_error {
            "cannot create coverage database directory: "
            + error.message()
        };
    }
    auto temporary = *coverage_database_path;
    temporary += ".fsim-tmp";
    {
        std::ofstream output(
            temporary, std::ios::binary | std::ios::trunc);
        output.write(contents->data(),
            static_cast<std::streamsize>(contents->size()));
        if (!output) {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error {
                "cannot write coverage database"
            };
        }
    }
    auto backup = *coverage_database_path;
    backup += ".fsim-old";
    std::filesystem::remove(backup, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error {
            "cannot prepare coverage database replacement: "
            + error.message()
        };
    }
    const auto had_existing = std::filesystem::exists(
        *coverage_database_path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error {
            "cannot inspect coverage database destination: "
            + error.message()
        };
    }
    if (had_existing) {
        std::filesystem::rename(
            *coverage_database_path, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error {
                "cannot preserve the previous coverage database: "
                + error.message()
            };
        }
    }
    std::filesystem::rename(
        temporary, *coverage_database_path, error);
    if (error) {
        const auto publish_error = error.message();
        if (had_existing) {
            std::error_code restore_error;
            std::filesystem::rename(
                backup, *coverage_database_path, restore_error);
        }
        std::filesystem::remove(temporary, error);
        throw std::runtime_error {
            "cannot publish coverage database: " + publish_error
        };
    }
    if (had_existing) {
        std::filesystem::remove(backup, error);
    }
}

void Simulation::Impl::control_coverage_database(
    const runtime::simir::CoverageDatabaseControlEvent& event)
{
    const auto path = coverage_database_file(event.filename);
    if (event.kind
        == runtime::simir::CoverageDatabaseControlKind::set_name) {
        coverage_database_path = path;
        return;
    }
    if (event.kind
        == runtime::simir::CoverageDatabaseControlKind::load) {
        load_coverage_database(path);
        return;
    }
    throw std::runtime_error {
        "unknown coverage database control kind"
    };
}

std::vector<runtime::SystemVerilogUvmCommandReportApplication>
Simulation::Impl::apply_uvm_report_settings(
    const std::string_view phase,
    const SimulationTick time)
{
    return uvm_command_line.apply_report_settings(
        uvm_components, uvm_reports, phase, time);
}

void Simulation::Impl::schedule_uvm_report_settings()
{
    auto& scheduler = interpreter->scheduler();
    for (const auto& handle : uvm_report_setting_tasks) {
        (void)scheduler.cancel(handle);
    }
    uvm_report_setting_tasks.clear();
    const auto now = scheduler.now();
    (void)apply_uvm_report_settings("time", now);
    std::set<SimulationTick> offsets;
    for (const auto& setting : uvm_command_line.settings().verbosity_settings) {
        if (setting.phase == "time" && setting.time_offset
            && *setting.time_offset > now) {
            offsets.insert(*setting.time_offset);
        }
    }
    runtime::StableOrder order { };
    for (const auto offset : offsets) {
        uvm_report_setting_tasks.push_back(
            scheduler.schedule_after_cancelable(
                offset - now, runtime::SchedulerPhase::active, order++,
                [this, offset](runtime::Scheduler&) {
                    (void)apply_uvm_report_settings("time", offset);
                }));
    }
}

void Simulation::Impl::validate_external_value(
    const SignalId signal,
    const PackedLogic4& value,
    const std::string_view operation) const
{
    const auto& info = built.design.signals().at(signal);
    if (info.width != value.width()) {
        throw std::invalid_argument(
            std::string { operation } + " width does not match signal '"
            + info.name + "'");
    }
    if (info.source_domain != frontend::ValueDomain::Bit2
        && info.source_domain != frontend::ValueDomain::Boolean) {
        return;
    }
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        const auto state = value.get(bit);
        if (state != runtime::Logic4::zero
            && state != runtime::Logic4::one) {
            throw std::invalid_argument(
                std::string { operation }
                + " would place an X/Z value into two-state signal '"
                + info.name + "'");
        }
    }
}

[[nodiscard]] const frontend::SystemVerilogClassSpecialization&
Simulation::Impl::class_specialization(const std::string_view identity) const
{
    return class_execution.class_specialization(identity);
}

[[nodiscard]] runtime::SystemVerilogUvmRootHandle Simulation::Impl::component_root(
    const std::string_view allocation_scope)
{
    return class_execution.component_root(allocation_scope);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_randomize(
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const std::string> selected_names,
    const std::span<const runtime::SystemVerilogConstraintTemplate>
        inline_constraints)
{
    return class_execution.invoke_source_randomize(
        handle, selected_names, inline_constraints);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_randomization_mode(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view method,
    const std::span<const runtime::PackedLogic4> actuals)
{
    return class_execution.invoke_source_randomization_mode(
        handle, method, actuals);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::resize_packed(
    const runtime::PackedLogic4& value,
    const std::size_t width)
{
    return application_detail::SystemVerilogClassExecution::resize_packed(
        value, width);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::packed_property_value(
    const runtime::SystemVerilogClassPropertyValue& value)
{
    return application_detail::SystemVerilogClassExecution::packed_property_value(
        value);
}

void Simulation::Impl::assign_property_value(
    runtime::SystemVerilogClassPropertyValue& destination,
    const std::string_view canonical_identity,
    const runtime::PackedLogic4& value)
{
    class_execution.assign_property_value(
        destination, canonical_identity, value);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_class_container(
    const runtime::SystemVerilogClassHandle receiver,
    const std::string_view operation,
    const std::span<const runtime::PackedLogic4> actuals)
{
    return class_execution.invoke_class_container(
        receiver, operation, actuals);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_checked_class_cast(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view operation) const
{
    return class_execution.invoke_checked_class_cast(handle, operation);
}

[[nodiscard]] std::pair<std::string_view, std::string_view>
Simulation::Impl::static_property_parts(const std::string_view identity)
{
    return application_detail::SystemVerilogClassExecution::static_property_parts(
        identity);
}

[[nodiscard]] std::optional<runtime::PackedLogic4>
Simulation::Impl::evaluate_constructor_expression(
    const frontend::Expression& expression,
    const runtime::SystemVerilogClassHandle handle,
    ConstructorEnvironment& environment)
{
    return class_execution.evaluate_constructor_expression(
        expression, handle, environment);
}

[[nodiscard]] Simulation::Impl::ConstructorEnvironment
Simulation::Impl::bind_constructor_actuals(
    const frontend::SystemVerilogClassMethodProfile& constructor,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> actual_names)
{
    return class_execution.bind_constructor_actuals(
        constructor, handle, actuals, actual_names);
}

void Simulation::Impl::execute_constructor_statements(
    const std::span<const frontend::Statement> statements,
    const runtime::SystemVerilogClassHandle handle,
    ConstructorEnvironment& environment,
    const bool native_uvm_library)
{
    class_execution.execute_constructor_statements(
        statements, handle, environment, native_uvm_library);
}

[[nodiscard]] runtime::SystemVerilogClassHandle Simulation::Impl::allocate_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type,
    const std::string_view allocation_scope)
{
    const auto& specialization = class_specialization(
        specialization_identity);
    runtime::SystemVerilogClassDescriptor descriptor;
    descriptor.dynamic_type = specialization.declaration_identity;
    descriptor.declared_type = declared_type.empty()
        ? descriptor.dynamic_type
        : std::string { declared_type };
    descriptor.specialization_identity = specialization.specialization_identity;
    const auto separator = allocation_scope.find('.');
    descriptor.random_root_identity = std::string {
        allocation_scope.substr(0, separator)
    };
    const auto* current = &specialization;
    while (current != nullptr) {
        descriptor.assignable_declared_types.push_back(
            current->declaration_identity);
        if (current->base_specialization_identity.empty())
            break;
        current = &class_specialization(
            current->base_specialization_identity);
    }
    for (const auto& property : specialization.properties) {
        if (!property.is_static) {
            descriptor.properties.push_back(
                class_property_descriptor(property, true));
        }
    }
    const auto declaration = std::ranges::find(
        built.systemverilog_hir.classes(),
        specialization.declaration_identity,
        &semantic::sv::ClassDeclaration::canonical_identity);
    if (declaration != built.systemverilog_hir.classes().end()) {
        for (const auto& constraint : declaration->composed_constraints) {
            if (constraint.override_legal) {
                descriptor.constraint_modes.emplace_back(
                    constraint.selected_identity,
                    constraint.mode_enabled);
            }
        }
    } else {
        descriptor.constraint_modes = specialization.constraint_modes;
    }
    return class_heap.allocate(descriptor);
}

[[nodiscard]] runtime::SystemVerilogClassHandle Simulation::Impl::construct_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals,
    const std::span<const std::string> actual_names,
    const std::string_view allocation_scope,
    const runtime::SystemVerilogUvmRootHandle requested_root)
{
    const auto& specialization = class_specialization(
        specialization_identity);
    const auto handle = allocate_class(
        specialization.specialization_identity,
        declared_type,
        allocation_scope);
    const auto before = packed_class_snapshot();
    const auto component_specialization = is_systemverilog_uvm_type(
        built.systemverilog_class_specializations,
        specialization,
        "uvm_component");
    bool object_initialized { };
    bool automatic_root_created { };
    runtime::SystemVerilogUvmRootHandle automatic_root { };
    std::string automatic_root_identity;
    try {
        invoke_source_constructor(
            specialization, handle, actuals, actual_names);
        if (is_systemverilog_uvm_type(
                built.systemverilog_class_specializations,
                specialization,
                "uvm_object")) {
            uvm_objects.initialize(handle);
            object_initialized = true;
            std::size_t name_index { };
            if (!actual_names.empty()) {
                const auto found = std::ranges::find(actual_names, "name");
                if (found != actual_names.end()) {
                    name_index = static_cast<std::size_t>(
                        std::distance(actual_names.begin(), found));
                }
            }
            if (name_index < string_actuals.size()
                && !string_actuals[name_index].empty()) {
                uvm_objects.set_name(handle, string_actuals[name_index]);
            }
        }
        if (component_specialization) {
            std::size_t name_index { };
            std::size_t parent_index { 1U };
            if (!actual_names.empty()) {
                const auto named_index = [&](const std::string_view name,
                                             const std::size_t fallback) {
                    const auto found = std::ranges::find(actual_names, name);
                    return found == actual_names.end()
                        ? fallback
                        : static_cast<std::size_t>(
                              std::distance(actual_names.begin(), found));
                };
                name_index = named_index("name", 0U);
                parent_index = named_index("parent", 1U);
            }
            std::string name = name_index < string_actuals.size()
                ? string_actuals[name_index]
                : std::string { };
            if (name.empty()) {
                name = "COMP_" + std::to_string(uvm_objects.instance_id(handle));
            }
            const auto parent = parent_index < actuals.size()
                ? actuals[parent_index].low_word().aval
                : runtime::SystemVerilogClassHandle { };
            auto root = requested_root;
            if (parent == 0 && root == 0) {
                automatic_root_identity = allocation_scope.empty()
                    ? "$simulation"
                    : std::string { allocation_scope };
                automatic_root_created = !uvm_roots_by_scope.contains(automatic_root_identity);
                root = component_root(allocation_scope);
                automatic_root = root;
            }
            uvm_components.initialize(handle, std::move(name), parent, root);
        }
    } catch (...) {
        if (component_specialization) {
            if (uvm_components.contains(handle)) {
                try {
                    uvm_components.release(handle);
                } catch (...) {
                }
            } else {
                if (object_initialized)
                    uvm_objects.erase(handle);
                (void)class_heap.release(handle);
            }
            if (automatic_root_created
                && uvm_components.contains_root(automatic_root)) {
                try {
                    uvm_phases.unparticipate_standard_root(automatic_root);
                } catch (...) {
                }
                try {
                    uvm_components.destroy_root(automatic_root);
                } catch (...) {
                }
                uvm_roots_by_scope.erase(automatic_root_identity);
            }
        }
        if (component_specialization && class_heap.contains(handle)) {
            (void)class_heap.release(handle);
        }
        throw;
    }
    notify_class_changes(before);
    return handle;
}

[[nodiscard]] Simulation::Impl::PackedSnapshot
Simulation::Impl::packed_class_snapshot() const
{
    PackedSnapshot result;
    for (const auto handle : class_heap.live_handles()) {
        const auto& object = class_heap.object(handle);
        for (std::size_t index = 0; index < object.properties.size(); ++index) {
            const auto& property = object.properties[index];
            if (property.packed.width() != 0) {
                result.emplace(
                    std::pair { handle, object.property_names[index] },
                    property.packed);
            }
        }
    }
    return result;
}

void Simulation::Impl::notify_class_changes(const PackedSnapshot& before)
{
    if (!class_property_change_hook)
        return;
    const auto after = packed_class_snapshot();
    for (const auto& [identity, value] : after) {
        const auto prior = before.find(identity);
        if (prior == before.end() || prior->second != value) {
            class_property_change_hook(
                identity.first, identity.second, value,
                interpreter->scheduler().now(),
                interpreter->scheduler().delta());
        }
    }
}

[[nodiscard]] Simulation::Impl::StaticPackedSnapshot
Simulation::Impl::packed_static_snapshot() const
{
    StaticPackedSnapshot result;
    for (const auto& state : class_static_store.snapshots()) {
        for (std::size_t index = 0; index < state.properties.size(); ++index) {
            if (state.properties[index].packed.width() != 0) {
                result.emplace(
                    std::pair { state.specialization_identity,
                        state.property_names[index] },
                    state.properties[index].packed);
            }
        }
    }
    return result;
}

void Simulation::Impl::notify_static_changes(const StaticPackedSnapshot& before)
{
    if (!class_static_property_change_hook)
        return;
    for (const auto& [identity, value] : packed_static_snapshot()) {
        const auto prior = before.find(identity);
        if (prior == before.end() || prior->second != value) {
            class_static_property_change_hook(
                identity.first, identity.second, value,
                interpreter->scheduler().now(), interpreter->scheduler().delta());
        }
    }
}

[[nodiscard]] runtime::SystemVerilogClassInvocationResult
Simulation::Impl::invoke_class_method(
    const std::string_view canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
    const std::optional<std::uint32_t> virtual_slot)
{
    const auto before = packed_class_snapshot();
    const auto static_before = packed_static_snapshot();
    auto result = virtual_slot
        ? class_methods.invoke_virtual(*virtual_slot, this_handle, actuals)
        : class_methods.invoke(canonical_method, this_handle, actuals);
    notify_class_changes(before);
    notify_static_changes(static_before);
    return result;
}

[[nodiscard]] std::optional<runtime::simir::SignalId>
Simulation::Impl::backdoor_signal(const std::string_view path) const noexcept
{
    const auto found = std::ranges::find_if(
        built.design_ir.objects(), [&](const auto& object) {
            return design_object_is_signal_bearing(object) && object.path == path && object.runtime_index <= std::numeric_limits<runtime::simir::SignalId>::max();
        });
    return found == built.design_ir.objects().end()
        ? std::nullopt
        : std::optional<runtime::simir::SignalId> { static_cast<
              runtime::simir::SignalId>(found->runtime_index) };
}

#if defined(FSIM_HAS_LLVM)
void Simulation::Impl::request_background_jit_compilation(
    const bool force_adaptive)
{
    {
        std::scoped_lock lock { jit_background_mutex };
        jit_background_requested = true;
        jit_background_forced |= force_adaptive;
    }
    jit_background_condition.notify_all();
}

void Simulation::Impl::cancel_background_jit_compilation() noexcept
{
    {
        std::scoped_lock lock { jit_background_mutex };
        jit_background_cancelled = true;
    }
    jit_background_condition.notify_all();
}

#endif

Simulation::Impl::~Impl()
{
#if defined(FSIM_HAS_LLVM)
    // Materialization jobs retain pointers to process programs owned by
    // the interpreter. Normal simulation may deliberately finish before
    // background promotion, so join while both the interpreter and JIT
    // are still alive rather than relying on reverse member destruction.
    cancel_background_jit_compilation();
    if (jit_materialization.valid()) {
        jit_materialization.wait();
    }
#endif
    if (coverage_database_path && lifecycle != Lifecycle::finished) {
        try {
            save_coverage_database();
        } catch (...) {
        }
    }
    if (vpi_started && !vpi_ended) {
        try {
            end_vpi();
        } catch (...) {
        }
    }
    if (systemc_start_attempted && !systemc_ended) {
        try {
            end_systemc();
        } catch (...) {
        }
    }
    if (vpi_registry && vpi_value_state_observer) {
        (void)vpi_registry->remove_value_state_observer(
            *vpi_value_state_observer);
    }
}

[[nodiscard]] runtime::SystemVerilogVpiStoredValue Simulation::Impl::vpi_value(
    const SignalId signal,
    PackedLogic4 value) const
{
    return systemverilog_vpi_signal_value(
        std::move(value), vpi_scalar_kinds.at(signal),
        vpi_categories.at(signal));
}

[[nodiscard]] runtime::SystemVerilogVpiStoredValue Simulation::Impl::vpi_driver_value(
    const SignalId signal,
    const SystemVerilogVpiDriverBinding& binding) const
{
    auto result = vpi_value(signal,
        interpreter->driver_value(binding.process, signal));
    result.strength = binding.strength;
    return result;
}

void Simulation::Impl::require_vpi_value(
    const runtime::SystemVerilogVpiValueError error,
    const std::string_view operation)
{
    if (error == runtime::SystemVerilogVpiValueError::None) {
        return;
    }
    throw std::logic_error { "live VPI " + std::string { operation }
        + " failed with error "
        + std::to_string(static_cast<unsigned>(error)) };
}

[[nodiscard]] runtime::SystemVerilogVpiValueError Simulation::Impl::apply_vpi_value_state(
    const runtime::SystemVerilogVpiValueStateUpdate& update)
{
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    try {
        const auto word = vpi_word_handles.find(update.object);
        if (word != vpi_word_handles.end()) {
            if (update.forced_value) {
                return runtime::SystemVerilogVpiValueError::ReadOnly;
            }
            auto value
                = interpreter->container_object_value(word->second.first);
            auto& element = value.elements.at(word->second.second);
            const auto replacement = systemverilog_vpi_packed_value(
                update.value,
                vpi_container_scalar_kinds.at(word->second.first),
                vpi_container_categories.at(word->second.first));
            if (element != replacement) {
                element = replacement;
                interpreter->deposit_container_object(
                    word->second.first, std::move(value));
            }
            return runtime::SystemVerilogVpiValueError::None;
        }
        const auto signal = vpi_handle_signals.find(update.object);
        if (signal == vpi_handle_signals.end()) {
            return runtime::SystemVerilogVpiValueError::None;
        }
        const auto stored = systemverilog_vpi_packed_value(
            update.value, vpi_scalar_kinds.at(signal->second),
            vpi_categories.at(signal->second));
        const bool was_forced
            = vpi_forced_signals.contains(signal->second);
        for (const auto alias : vpi_signal_handles.at(signal->second)) {
            if (alias == update.object) {
                continue;
            }
            require_vpi_value(
                vpi_registry->update_bound_value(alias, update.value),
                "alias stored-value publication");
            if (update.forced_value) {
                require_vpi_value(
                    vpi_registry->update_forced_value(
                        alias, *update.forced_value),
                    "alias forced-value publication");
            } else if (was_forced) {
                const auto released
                    = vpi_registry->release_bound_force(alias);
                if (released != runtime::SystemVerilogVpiValueError::None
                    && released
                        != runtime::SystemVerilogVpiValueError::NotForced) {
                    require_vpi_value(
                        released, "alias force release publication");
                }
            }
        }
        if (interpreter->stored_signal_value(signal->second) != stored) {
            interpreter->deposit_signal(signal->second, stored);
        }
        if (update.forced_value) {
            const auto forced = systemverilog_vpi_packed_value(
                *update.forced_value,
                vpi_scalar_kinds.at(signal->second),
                vpi_categories.at(signal->second));
            if (!interpreter->signal_is_forced(signal->second)
                || interpreter->signal_value(signal->second) != forced) {
                interpreter->force_signal(signal->second, forced);
            }
            vpi_forced_signals.insert(signal->second);
        } else {
            if (interpreter->signal_is_forced(signal->second)) {
                interpreter->release_signal(signal->second);
            }
            vpi_forced_signals.erase(signal->second);
        }
        return runtime::SystemVerilogVpiValueError::None;
    } catch (...) {
        return runtime::SystemVerilogVpiValueError::ResourceLimit;
    }
}

void Simulation::Impl::publish_vpi_stored_signal(const SignalId signal)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    if (vpi_event_handles.contains(signal)) {
        publish_vpi_event(signal);
    }
    const auto objects = vpi_signal_handles.find(signal);
    if (objects == vpi_signal_handles.end()) {
        return;
    }
    for (const auto object : objects->second) {
        require_vpi_value(
            vpi_registry->update_bound_value(
                object,
                vpi_value(signal,
                    interpreter->stored_signal_value(signal))),
            "stored-value publication");
    }
    const auto drivers = vpi_driver_bindings.find(signal);
    if (drivers != vpi_driver_bindings.end()) {
        for (const auto& driver : drivers->second) {
            require_vpi_value(
                vpi_registry->update_bound_value(
                    driver.handle,
                    vpi_driver_value(signal, driver)),
                "driver publication");
        }
    }
}

void Simulation::Impl::publish_vpi_driver(
    const runtime::simir::ProcessId process,
    const SignalId signal)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto drivers = vpi_driver_bindings.find(signal);
    if (drivers == vpi_driver_bindings.end()) {
        return;
    }
    for (const auto& driver : drivers->second) {
        if (driver.process != process) {
            continue;
        }
        require_vpi_value(
            vpi_registry->update_bound_value(
                driver.handle,
                vpi_driver_value(signal, driver)),
            "driver publication");
    }
}

void Simulation::Impl::publish_vpi_container(
    const runtime::simir::ContainerObjectId object)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto words = vpi_container_words.find(object);
    if (words == vpi_container_words.end()) {
        return;
    }
    const auto& value = interpreter->container_object_value(object);
    for (const auto& [handle, ordinal] : words->second) {
        require_vpi_value(
            vpi_registry->update_bound_value(handle,
                systemverilog_vpi_signal_value(
                    value.elements.at(ordinal),
                    vpi_container_scalar_kinds.at(object),
                    vpi_container_categories.at(object))),
            "memory-word publication");
    }
}

void Simulation::Impl::publish_vpi_event(const SignalId event)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto objects = vpi_event_handles.find(event);
    if (objects == vpi_event_handles.end()) {
        return;
    }
    for (const auto object : objects->second) {
        const auto dispatched = vpi_callbacks->dispatch_named_event(object);
        if (dispatched
            != runtime::SystemVerilogVpiCallbackError::None) {
            throw std::logic_error {
                "live VPI named-event callback dispatch failed with error "
                + std::to_string(static_cast<unsigned>(dispatched))
            };
        }
    }
}

void Simulation::Impl::publish_vpi_signal(
    const SignalId signal,
    const PackedLogic4& value)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto objects = vpi_signal_handles.find(signal);
    if (objects == vpi_signal_handles.end()) {
        return;
    }
    if (interpreter->signal_is_forced(signal)) {
        for (const auto object : objects->second) {
            require_vpi_value(
                vpi_registry->update_forced_value(
                    object, vpi_value(signal, value)),
                "forced-value publication");
        }
        vpi_forced_signals.insert(signal);
    } else {
        for (const auto object : objects->second) {
            require_vpi_value(
                vpi_registry->update_bound_value(
                    object,
                    vpi_value(signal,
                        interpreter->stored_signal_value(signal))),
                "effective-value publication");
        }
        if (vpi_forced_signals.erase(signal) != 0U) {
            for (const auto object : objects->second) {
                const auto released
                    = vpi_registry->release_bound_force(object);
                if (released != runtime::SystemVerilogVpiValueError::None
                    && released
                        != runtime::SystemVerilogVpiValueError::NotForced) {
                    require_vpi_value(
                        released, "force release publication");
                }
            }
        }
    }
}

void Simulation::Impl::publish_vpi_assertion(
    const ConcurrentAssertionEvent& event)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    const auto object = vpi_assertion_handles.find(event.process);
    if (object == vpi_assertion_handles.end() || !vpi_assertions) {
        return;
    }
    runtime::SystemVerilogVpiAssertionEvent published;
    published.kind
        = event.kind == ConcurrentAssertionCoverageKind::assumption
        ? runtime::SystemVerilogVpiAssertionKind::Assumption
        : event.kind == ConcurrentAssertionCoverageKind::cover
        ? runtime::SystemVerilogVpiAssertionKind::Cover
        : event.kind == ConcurrentAssertionCoverageKind::restriction
        ? runtime::SystemVerilogVpiAssertionKind::Restriction
        : runtime::SystemVerilogVpiAssertionKind::Assertion;
    published.outcome
        = event.outcome == ConcurrentAssertionOutcome::pass
        ? runtime::SystemVerilogVpiAssertionOutcome::Success
        : event.outcome == ConcurrentAssertionOutcome::failure
        ? runtime::SystemVerilogVpiAssertionOutcome::Failure
        : event.outcome == ConcurrentAssertionOutcome::disabled
        ? runtime::SystemVerilogVpiAssertionOutcome::Disabled
        : event.outcome == ConcurrentAssertionOutcome::vacuous
        ? runtime::SystemVerilogVpiAssertionOutcome::Vacuous
        : runtime::SystemVerilogVpiAssertionOutcome::Aborted;
    published.name = event.name;
    published.process = event.process;
    published.instance_identity = event.instance_identity;
    published.slot = event.slot;
    published.source_span = event.source_span;
    published.action_suppressed = event.action_suppressed;
    const auto error = vpi_assertions->observe(
        object->second, std::move(published));
    if (error != runtime::SystemVerilogVpiAssertionApiError::None) {
        throw std::logic_error {
            "live VPI assertion callback dispatch failed with error "
            + std::to_string(static_cast<unsigned>(error))
        };
    }
}

void Simulation::Impl::start_vpi()
{
    if (vpi_started) {
        return;
    }
    const auto sealed = vpi_systems->seal_registrations();
    if (sealed != runtime::SystemVerilogVpiSystemError::None) {
        throw std::logic_error { "failed to seal VPI system registrations" };
    }
    const auto callback = vpi_callbacks->dispatch_lifecycle_now(
        runtime::SystemVerilogVpiCallbackKind::StartOfSimulation);
    if (callback != runtime::SystemVerilogVpiCallbackError::None) {
        throw std::logic_error { "failed to dispatch VPI start callbacks" };
    }
    vpi_started = true;
}

void Simulation::Impl::end_vpi()
{
    if (!vpi_started || vpi_ended) {
        return;
    }
    const auto callback = vpi_callbacks->dispatch_lifecycle_now(
        runtime::SystemVerilogVpiCallbackKind::EndOfSimulation);
    if (callback != runtime::SystemVerilogVpiCallbackError::None) {
        throw std::logic_error { "failed to dispatch VPI end callbacks" };
    }
    vpi_control->mark_finished();
    vpi_ended = true;
}

void Simulation::Impl::start_systemc()
{
    if (systemc_start_attempted) {
        return;
    }
    systemc_start_attempted = true;
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
        registry.start_simulation(roots);
    });
}

void Simulation::Impl::end_systemc(
    const std::optional<runtime::SimulationTick> current_time)
{
    if (!systemc_start_attempted || systemc_ended) {
        return;
    }
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
        registry.end_simulation(
            roots,
            current_time.value_or(interpreter->scheduler().now()));
    });
    systemc_ended = true;
}
} // namespace fsim::app

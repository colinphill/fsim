// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "fsim/support/uvm_api_methods.hpp"

#include <charconv>
#include <set>

namespace fsim::app {
[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_function(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view canonical_identity,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names,
    const std::span<const std::uint8_t> actual_directions,
    const bool virtual_dispatch,
    const application_detail::SystemVerilogClassHirExecution::ActualReferences
        actual_references,
    const std::optional<runtime::simir::ProcessId> report_process)
{
    // The built-in randomization methods, called from a method body (IEEE
    // 1800-2017 18.6, 18.8).
    if (canonical_identity == "@builtin-randomize") {
        return invoke_source_randomize(
            handle, actual_names, actuals, { }, report_process);
    }
    if (canonical_identity.starts_with("@builtin-rand-mode:")
        || canonical_identity.starts_with("@builtin-constraint-mode:")) {
        return invoke_source_randomization_mode(
            handle, canonical_identity, actuals);
    }
    // A lowered constructor chain has reached the UVM library's constructor:
    // the simulation's UVM services make the base state it would, from its
    // actuals (IEEE 1800.2-2020 5.3.2, 13.1.2).
    if (canonical_identity == "@uvm-construct-base") {
        const auto pending = uvm_pending_constructions.find(handle);
        // A TLM port (no uvm_object) is allocated as any class: its chain
        // reaches uvm_port_base::new, whose actuals (name, parent, port
        // type) make its state (IEEE 1800.2-2020 5.5.2.1).
        if (pending == uvm_pending_constructions.end()) {
            const auto& specialization = class_specialization(
                class_heap.object(handle).specialization_identity);
            if (is_systemverilog_uvm_type(
                    built.compiled_systemverilog_class_specializations,
                    specialization, support::UvmRole::port_base)) {
                UvmBaseConstruction state;
                construct_uvm_base(specialization, handle, actuals,
                    string_actuals, actual_names, { }, { }, state);
            }
        }
        if (pending != uvm_pending_constructions.end()) {
            const auto request = std::move(pending->second);
            uvm_pending_constructions.erase(pending);
            if (request.base_made) {
                return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
            }
            const auto& specialization = class_specialization(
                class_heap.object(handle).specialization_identity);
            UvmBaseConstruction state;
            state.component = is_systemverilog_uvm_type(
                    built.compiled_systemverilog_class_specializations,
                    specialization, support::UvmRole::component)
                && !is_systemverilog_uvm_type(
                    built.compiled_systemverilog_class_specializations,
                    specialization, support::UvmRole::root);
            try {
                construct_uvm_base(specialization, handle, actuals,
                    string_actuals, actual_names, request.allocation_scope,
                    request.requested_root, state);
            } catch (...) {
                // The object an API request constructs is rejected (a
                // duplicate name): the request's caller raises the error
                // once the dispatch process has finished the request.
                if (uvm_api_request.kind != support::uvm_api_construct_kind
                    || uvm_api_request.receiver != handle) {
                    throw;
                }
                uvm_api_request.error = std::current_exception();
            }
        }
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    // The library method the call names, by its recorded role.
    using support::UvmRole;
    const auto called = uvm_method_role(canonical_identity);
    const auto method_name = called.role;
    const bool library_method = support::uvm_library(called.role);
    // The methods of the object `uvm_root::get()` and `uvm_top` hold are the
    // native hierarchy's (IEEE 1800.2-2020 13.2): the library's bodies would
    // run on an object without the state its constructor makes.
    if (library_method && is_uvm_root_handle(handle)) {
        if (auto result = invoke_uvm_root_method(
                method_name, actuals, string_actuals, actual_names)) {
            return std::move(*result);
        }
    }
    // The UVM library's phase callbacks of uvm_component (reached through
    // `super.build_phase(phase)` and the like) do framework bookkeeping that
    // the simulation's UVM phase services perform; their bodies are not run.
    // The same holds for the overrides in the library's component classes
    // (uvm_driver, uvm_sequencer_base and the like).
    if (library_method && method_name >= UvmRole::build_phase
        && method_name <= UvmRole::post_shutdown_phase
        && (called.owner == UvmRole::component
            || uvm_components.contains(handle))) {
        return runtime::PackedLogic4 { };
    }
    // A UVM object's name and path come from the native object and component
    // services, which its construction initialized (IEEE 1800.2-2020 5.3.4,
    // 13.1.3); the library constructors that would set them do not run.
    if ((method_name == UvmRole::get_name
            || method_name == UvmRole::get_full_name)
        && !actual_names.empty() && actual_names.back() == "@result-string"
        && !string_actuals.empty()
        && (uvm_components.contains(handle) || uvm_objects.contains(handle))) {
        string_actuals.back() = uvm_components.contains(handle)
            ? (method_name == UvmRole::get_name
                    ? uvm_components.snapshot(handle).name
                    : uvm_components.full_name(handle))
            : (method_name == UvmRole::get_name
                    ? std::string { uvm_objects.name(handle) }
                    : uvm_objects.full_name(handle));
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    // A method of a phase object the native run passes to a phase callback.
    if (called.owner == UvmRole::phase) {
        if (auto result = invoke_uvm_phase_method(
                handle, method_name, actuals, string_actuals)) {
            return std::move(*result);
        }
    }
    // `port.connect(provider)` records an analysis connection that the
    // port's write follows (IEEE 1800.2-2020 5.4.2.1, 12.2.10.1).
    if (method_name == UvmRole::port_connect
        && uvm_tlm_owners.contains(handle) && !actuals.empty()
        && actuals[0].low_word().bval == 0U && actuals[0].low_word().aval != 0U) {
        uvm_analysis_connections[handle].push_back(actuals[0].low_word().aval);
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    // The sequence-item handshake (IEEE 1800.2-2020 14.2.6, 15.2.2.1): a
    // started sequence hands each item to its sequencer in finish_item, and
    // the driver whose pull port connects to that sequencer takes it. The
    // elaborated process waits on the class heap until an item is available
    // or taken.
    const auto handle_actual = [&](const std::size_t index) {
        return index < actuals.size() && actuals[index].low_word().bval == 0U
            ? runtime::SystemVerilogClassHandle {
                  actuals[index].low_word().aval }
            : runtime::SystemVerilogClassHandle { };
    };
    if (method_name == UvmRole::sequence_start
        || method_name == UvmRole::sequence_start_item
        || method_name == UvmRole::sequence_finish_item) {
        if (method_name == UvmRole::sequence_start && handle_actual(0U) != 0U) {
            uvm_sequence_sequencers[handle] = handle_actual(0U);
        }
        const auto sequencer = uvm_sequence_sequencers.find(handle);
        if (method_name == UvmRole::sequence_finish_item && handle_actual(0U) != 0U
            && sequencer != uvm_sequence_sequencers.end()) {
            uvm_sequencer_items[sequencer->second].push_back(
                handle_actual(0U));
            uvm_pending_items.insert(handle_actual(0U));
            toggle_class_heap_change();
        }
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    if (called.owner == UvmRole::seq_item_pull_port
        || called.owner == UvmRole::seq_item_pull_imp
        || called.owner == UvmRole::seq_item_pull_export) {
        const auto sequencer = uvm_pull_sequencer(handle);
        auto& items = uvm_sequencer_items[sequencer];
        if ((method_name == UvmRole::pull_get
                || method_name == UvmRole::pull_get_next_item
                || method_name == UvmRole::pull_try_next_item
                || method_name == UvmRole::pull_peek)
            && !actuals.empty()) {
            runtime::SystemVerilogClassHandle item { };
            if (!items.empty()) {
                item = items.front();
                if (method_name != UvmRole::pull_peek) {
                    items.erase(items.begin());
                }
                if (method_name == UvmRole::pull_get) {
                    uvm_pending_items.erase(item);
                    toggle_class_heap_change();
                } else if (method_name != UvmRole::pull_peek) {
                    uvm_sequencer_current[sequencer] = item;
                }
            }
            actuals[0] = runtime::PackedLogic4::from_aval_bval(64U, item, 0U);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        if (method_name == UvmRole::pull_item_done) {
            const auto current = uvm_sequencer_current.find(sequencer);
            if (current != uvm_sequencer_current.end()) {
                uvm_pending_items.erase(current->second);
                uvm_sequencer_current.erase(current);
                toggle_class_heap_change();
            }
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
    }
    // The report-object queries that `uvm_report_enabled` and the report
    // macros make answer from the simulation's report policy.
    if (called.owner == UvmRole::report_object
        && (method_name == UvmRole::report_get_verbosity_level
            || method_name == UvmRole::report_get_action
            || method_name == UvmRole::report_enabled
            || method_name == UvmRole::get_report_object)) {
        if (method_name == UvmRole::get_report_object) {
            return runtime::PackedLogic4::from_aval_bval(64, handle, 0);
        }
        const auto word = [&](const std::size_t index,
                              const std::int64_t fallback) {
            if (index >= actuals.size()
                || actuals[index].low_word().bval != 0U) {
                return fallback;
            }
            return static_cast<std::int64_t>(static_cast<std::int32_t>(
                static_cast<std::uint32_t>(actuals[index].low_word().aval)));
        };
        const auto text = [&](const std::size_t index) {
            return index < string_actuals.size() ? string_actuals[index]
                                                 : std::string { };
        };
        const auto severity = [](const std::int64_t value) {
            return value >= 0 && value <= 3
                ? static_cast<runtime::SystemVerilogUvmReportSeverity>(value)
                : runtime::SystemVerilogUvmReportSeverity::Info;
        };
        if (method_name == UvmRole::report_enabled) {
            return runtime::PackedLogic4::from_aval_bval(1U,
                uvm_reports.enabled(handle,
                    static_cast<std::int32_t>(word(0U, 200)),
                    severity(word(1U, 0)), text(2U))
                    ? 1U
                    : 0U,
                0U);
        }
        const auto policy = uvm_reports.policy(
            handle, severity(word(0U, 0)), text(1U));
        return runtime::PackedLogic4::from_aval_bval(32U,
            method_name == UvmRole::report_get_action
                ? static_cast<std::uint32_t>(policy.action)
                : static_cast<std::uint32_t>(policy.verbosity),
            0U);
    }
    const auto report_severity = method_name == UvmRole::report_info
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Info }
        : method_name == UvmRole::report_warning
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Warning }
        : method_name == UvmRole::report_error
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Error }
        : method_name == UvmRole::report_fatal
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Fatal }
        : std::nullopt;
    if (report_severity && actuals.size() >= 3U && string_actuals.size() >= 3U) {
        runtime::SystemVerilogUvmReportRequest request;
        request.report_object = handle;
        request.severity = *report_severity;
        request.id = string_actuals[0];
        request.message = string_actuals[1];
        const auto verbosity = actuals[2].low_word();
        if (verbosity.bval != 0) {
            throw std::invalid_argument {
                "UVM report verbosity must be a known packed value"
            };
        }
        request.verbosity = static_cast<std::int32_t>(verbosity.aval);
        if (actuals.size() >= 4U && string_actuals.size() >= 4U)
            request.filename = string_actuals[3];
        if (actuals.size() >= 5U) {
            request.line = static_cast<std::uint32_t>(
                actuals[4].low_word().aval);
        }
        request.timestamp = interpreter->scheduler().now();
        (void)uvm_reports.report(request);
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    if (auto factory_result = invoke_systemverilog_uvm_factory_method(
            built.compiled_systemverilog_class_specializations,
            class_heap, uvm_factory, handle, canonical_identity, actuals)) {
        return std::move(*factory_result);
    }
    const auto uvm_base_method = called.owner == UvmRole::object;
    if (uvm_base_method && uvm_objects.contains(handle)) {
        if (method_name == UvmRole::object_get_inst_id && actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                32, uvm_objects.instance_id(handle), 0);
        }
        if (method_name == UvmRole::object_clone && actuals.empty()) {
            if (uvm_components.contains(handle))
                return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
            return runtime::PackedLogic4::from_aval_bval(
                64, uvm_objects.clone(handle), 0);
        }
        if (method_name == UvmRole::object_copy && actuals.size() == 1U) {
            uvm_objects.copy(handle, actuals.front().low_word().aval);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        if (method_name == UvmRole::object_compare && !actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                1,
                uvm_objects.compare(handle, actuals.front().low_word().aval)
                    ? 1U
                    : 0U,
                0);
        }
        if (method_name == UvmRole::object_print) {
            (void)uvm_objects.print(handle);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        if (method_name == UvmRole::object_record) {
            (void)uvm_objects.record(handle);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
    }
    const auto uvm_component_base_method = called.owner == UvmRole::component;
    if (uvm_component_base_method && uvm_components.contains(handle)) {
        if (method_name == UvmRole::component_get_parent && actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                64, uvm_components.parent(handle), 0);
        }
        if (method_name == UvmRole::component_get_num_children && actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                32, uvm_components.children(handle).size(), 0);
        }
    }
    // get_object_type of the library's uvm_object or a method overriding it
    // (the registration macros', IEEE 1800.2-2020 B.2.1).
    if (uvm_objects.contains(handle) && actuals.empty()
        && (method_name == UvmRole::object_get_object_type
            || (called.virtual_slot
                && called.virtual_slot == uvm_object_get_object_type_slot()))) {
        const auto wrapper = uvm_registry.wrapper_by_specialization(
            class_heap.object(handle).specialization_identity);
        if (wrapper != 0)
            return runtime::PackedLogic4::from_aval_bval(64, wrapper, 0);
    }
    if (const auto* method = class_hir_execution.method(
            handle, canonical_identity, virtual_dispatch)) {
        if (method->static_method) {
            throw std::invalid_argument {
                "instance class call selected a static HIR function"
            };
        }
        return class_hir_execution.invoke_function(
            *method, handle, actuals, string_actuals, actual_names,
            actual_directions, actual_references, report_process);
    }
    throw std::invalid_argument {
        "compiled HIR class method '" + std::string { canonical_identity }
        + "' is not executable"
    };
}

// The sequencer a driver's pull port reaches through its connections: the
// owner of the export at their end.
[[nodiscard]] runtime::SystemVerilogClassHandle
Simulation::Impl::uvm_pull_sequencer(runtime::SystemVerilogClassHandle port) const
{
    std::set<runtime::SystemVerilogClassHandle> visited;
    for (auto connected = uvm_analysis_connections.find(port);
         connected != uvm_analysis_connections.end()
         && !connected->second.empty() && visited.insert(port).second;
         connected = uvm_analysis_connections.find(port)) {
        port = connected->second.front();
    }
    const auto owner = uvm_tlm_owners.find(port);
    return owner != uvm_tlm_owners.end()
        ? owner->second
        : runtime::SystemVerilogClassHandle { };
}

[[nodiscard]] runtime::PackedLogic4
Simulation::Impl::invoke_source_static_function(
    const std::string_view canonical_identity,
    const std::string_view static_specialization,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names,
    const std::span<const std::uint8_t> actual_directions,
    const application_detail::SystemVerilogClassHirExecution::ActualReferences
        actual_references,
    const std::optional<runtime::simir::ProcessId> report_process)
{
    constexpr std::string_view registry_create_prefix {
        "@uvm-registry-create:"
    };
    constexpr std::string_view registry_get_prefix { "@uvm-registry-get:" };
    // The type wrapper of the class `declaration` a registry call's type
    // expression denotes: the specialization the call names, or, for the
    // bare class name, the default one (IEEE 1800-2023 8.25, 6.22.1). A
    // specialization nothing registered is no factory type.
    const auto registry_wrapper = [&](const std::string_view declaration) {
        if (!static_specialization.empty()
            && class_specialization(static_specialization)
                    .declaration_identity == declaration) {
            return uvm_registry.wrapper_by_specialization(
                static_specialization);
        }
        for (const auto& type : uvm_registry.types()) {
            if (type.declaration_identity == declaration
                && class_specialization(type.specialization_identity)
                       .default_specialization) {
                return type.wrapper;
            }
        }
        return runtime::SystemVerilogUvmTypeHandle { };
    };
    // The type wrapper of the class the type parameter of the registry
    // specialization the call names selects (`uvm_object_registry#(C)`).
    const auto registry_parameter_wrapper = [&] {
        const auto& registry = class_specialization(static_specialization);
        const auto parameter = std::ranges::find_if(registry.parameters,
            [](const semantic::sv::SpecializedClassParameter& item) {
                return item.type_parameter && item.type.has_value();
            });
        const auto named = parameter == registry.parameters.end()
            ? std::optional<std::string> { }
            : class_type_specialization(
                  *parameter->type, static_specialization);
        return named ? uvm_registry.wrapper_by_specialization(*named)
                     : runtime::SystemVerilogUvmTypeHandle { };
    };
    // The generated UVM dispatch process reads its request here.
    if (canonical_identity == "@uvm-api-request") {
        return runtime::PackedLogic4::from_aval_bval(
            32U, uvm_api_request.kind, 0U);
    }
    if (canonical_identity == "@uvm-api-receiver") {
        uvm_api_request.taken = true;
        return runtime::PackedLogic4::from_aval_bval(
            64U, uvm_api_request.receiver, 0U);
    }
    if (canonical_identity == "@uvm-api-phase") {
        return runtime::PackedLogic4::from_aval_bval(
            64U, uvm_api_request.phase, 0U);
    }
    // The name and parent an object waiting for its constructor chain was
    // allocated with (IEEE 1800.2-2020 5.3.2, 13.1.2).
    if (canonical_identity == "@uvm-pending-parent"
        || canonical_identity == "@uvm-pending-name") {
        const auto object = actuals.empty()
                || actuals[0].low_word().bval != 0U
            ? runtime::SystemVerilogClassHandle { }
            : runtime::SystemVerilogClassHandle { actuals[0].low_word().aval };
        const auto pending = uvm_pending_constructions.find(object);
        if (pending == uvm_pending_constructions.end()) {
            throw std::logic_error {
                "UVM pending construction query names an object that does "
                "not wait for its constructor"
            };
        }
        if (canonical_identity == "@uvm-pending-name") {
            string_actuals.at(1U) = pending->second.name;
            return runtime::PackedLogic4::from_aval_bval(1U, 1U, 0U);
        }
        return runtime::PackedLogic4::from_aval_bval(
            64U, pending->second.parent, 0U);
    }
    // The oldest object waiting for its constructor chain, zero without one.
    if (canonical_identity == "@uvm-pending-next") {
        return runtime::PackedLogic4::from_aval_bval(64U,
            uvm_pending_constructions.empty()
                ? runtime::SystemVerilogClassHandle { }
                : uvm_pending_constructions.begin()->first,
            0U);
    }
    // A chain generated code ran has taken the object off the list; one that
    // is still waiting belongs to a class whose constructor chain does not
    // lower to generated code, which the simulation never runs itself.
    if (canonical_identity == "@uvm-pending-check") {
        const auto object = actuals.empty()
                || actuals[0].low_word().bval != 0U
            ? runtime::SystemVerilogClassHandle { }
            : runtime::SystemVerilogClassHandle { actuals[0].low_word().aval };
        if (uvm_pending_constructions.contains(object)) {
            throw std::runtime_error {
                "the constructor chain of class '"
                + class_heap.object(object).specialization_identity
                + "' does not lower to generated code"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(1U, 1U, 0U);
    }
    // `clone()` of a uvm_object in a design process: the copy is allocated
    // without being constructed (its chain runs in the calling process),
    // then receives the original's fields (IEEE 1800.2-2020 5.3.8).
    if (canonical_identity == "@uvm-clone-allocate") {
        const auto source = actuals.size() == 1U
                && actuals[0].low_word().bval == 0U
            ? runtime::SystemVerilogClassHandle { actuals[0].low_word().aval }
            : runtime::SystemVerilogClassHandle { };
        if (source == 0U || !uvm_objects.contains(source)
            || uvm_components.contains(source)) {
            return runtime::PackedLogic4::from_aval_bval(64U, 0U, 0U);
        }
        const auto& original = class_heap.object(source);
        const auto copy = construct_class(
            "@service-construct:" + original.specialization_identity,
            original.declared_type,
            std::span<const runtime::PackedLogic4> { },
            std::span<const std::string> { },
            std::span<const std::string> { }, "$uvm-clone");
        return runtime::PackedLogic4::from_aval_bval(64U, copy, 0U);
    }
    if (canonical_identity == "@uvm-clone-finish") {
        if (actuals.size() == 2U && actuals[0].low_word().bval == 0U
            && actuals[0].low_word().aval != 0U) {
            const auto copy = actuals[0].low_word().aval;
            uvm_objects.set_name(copy,
                std::string { uvm_objects.name(actuals[1].low_word().aval) });
            uvm_objects.copy(copy, actuals[1].low_word().aval);
        }
        return runtime::PackedLogic4::from_aval_bval(1U, 1U, 0U);
    }
    if (canonical_identity == "@uvm-api-done") {
        uvm_api_request.kind = 0U;
        return runtime::PackedLogic4::from_aval_bval(1U, 1U, 0U);
    }
    // Whether the object a lowered create allocated for one of its
    // constructor chains is of class `specialization` and waits for it.
    if (constexpr std::string_view pending_prefix {
            "@uvm-pending-construction:" };
        canonical_identity.starts_with(pending_prefix)) {
        const auto object = actuals.size() == 1U
                && actuals[0].low_word().bval == 0U
            ? runtime::SystemVerilogClassHandle { actuals[0].low_word().aval }
            : runtime::SystemVerilogClassHandle { };
        const bool waiting = object != 0U
            && uvm_pending_constructions.contains(object)
            && class_heap.object(object).specialization_identity
                == canonical_identity.substr(pending_prefix.size());
        return runtime::PackedLogic4::from_aval_bval(1U, waiting ? 1U : 0U, 0U);
    }
    // `C::type_id::get()`: the registry's one object, which is the class's
    // type wrapper, as `get_type()` answers (IEEE 1800.2-2020 8.2.4.2.1,
    // B.2.1). A call naming the registry itself runs in the registry
    // specialization (`uvm_object_registry#(C)::get()`, `type_id::get()`).
    if (canonical_identity.starts_with(registry_get_prefix)) {
        const auto registry = !static_specialization.empty()
            && (class_specialization(static_specialization).uvm_role
                    == support::UvmRole::object_registry
                || class_specialization(static_specialization).uvm_role
                    == support::UvmRole::component_registry);
        const auto wrapper = registry
            ? registry_parameter_wrapper()
            : registry_wrapper(
                  canonical_identity.substr(registry_get_prefix.size()));
        if (wrapper == 0) {
            throw std::invalid_argument {
                "UVM registry get selected an unknown object type"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(64, wrapper, 0);
    }
    if (canonical_identity.starts_with(registry_create_prefix)) {
        // A lowered create names the classes whose constructor chains the
        // process runs after this call.
        std::optional<std::string> lowered;
        if (!actual_names.empty()
            && actual_names.back() == "@lowered-constructors"
            && string_actuals.size() == actual_names.size()) {
            lowered = string_actuals.back();
        }
        if (actuals.empty() && string_actuals.empty()) {
            // `type_id::create()`: every formal takes its default, the name
            // "" and no parent (IEEE 1800.2-2020 8.2.4.2.1, the
            // uvm_config_db implementation's get_type, C.4.2.3).
            actuals.push_back(runtime::PackedLogic4::from_aval_bval(1U, 0U, 0U));
            string_actuals.emplace_back();
        }
        if (actuals.empty() || string_actuals.empty()) {
            throw std::invalid_argument {
                "UVM registry create requires a string name actual"
            };
        }
        // A parameterized class registers each of its specializations; the
        // create makes the one its type expression denotes.
        auto wrapper = registry_wrapper(
            canonical_identity.substr(registry_create_prefix.size()));
        if (wrapper == 0) {
            throw std::invalid_argument {
                "UVM registry create selected an unknown object type"
            };
        }
        // The factory's overrides for the instance path the create names
        // select the class it makes (IEEE 1800.2-2020 8.2.4.2, 8.3.1.4.1):
        // the context, or the parent's full name without one, then the name.
        {
            const auto written = actuals.size() - (lowered ? 1U : 0U);
            const auto parent_handle = written > 1U
                    && actuals[1].low_word().bval == 0U
                ? runtime::SystemVerilogClassHandle {
                      actuals[1].low_word().aval }
                : runtime::SystemVerilogClassHandle { };
            std::string context = written > 2U && string_actuals.size() > 2U
                ? string_actuals[2]
                : std::string { };
            if (context.empty() && parent_handle != 0U
                && uvm_components.contains(parent_handle)) {
                context = uvm_components.full_name(parent_handle);
            }
            const auto& name = string_actuals.front();
            const auto path = context.empty() ? name
                : name.empty()                ? context
                                              : context + "." + name;
            wrapper = uvm_factory.resolve_by_type(wrapper, path).resolved;
        }
        // A component is created under its parent (IEEE 1800.2-2020
        // 8.2.4.2); without one it joins the run_test hierarchy.
        if (uvm_registry.snapshot(wrapper).kind
            == runtime::SystemVerilogUvmRegisteredKind::Component) {
            const auto parent = actuals.size() > 1U
                    && !(lowered && actual_names.size() == 2U)
                ? actuals[1].low_word().aval : std::uint64_t { };
            if (lowered) {
                if (const auto object = allocate_uvm_lowered(wrapper,
                        *lowered,
                        parent == 0 ? component_root("$simulation")
                                    : runtime::SystemVerilogUvmRootHandle { });
                    object != 0U) {
                    return runtime::PackedLogic4::from_aval_bval(64, object, 0);
                }
            }
            return runtime::PackedLogic4::from_aval_bval(64,
                uvm_registry.create_component_by_type(wrapper,
                    string_actuals.front(), parent,
                    parent == 0 ? component_root("$simulation")
                                : runtime::SystemVerilogUvmRootHandle { }),
                0);
        }
        if (lowered) {
            if (const auto object = allocate_uvm_lowered(wrapper, *lowered, 0);
                object != 0U) {
                return runtime::PackedLogic4::from_aval_bval(64, object, 0);
            }
        }
        return runtime::PackedLogic4::from_aval_bval(
            64,
            uvm_registry.create_object_by_type(
                wrapper, string_actuals.front()),
            0);
    }
    // Whether a pull port's sequencer holds an item, and whether a driver
    // took an item a sequence finished (IEEE 1800.2-2020 15.2.2.1).
    if (canonical_identity == "@uvm-sequence-item-ready"
        || canonical_identity == "@uvm-sequence-item-done") {
        if (actuals.size() != 1U) {
            throw std::invalid_argument {
                "UVM sequence-item query requires one handle"
            };
        }
        const auto operand = actuals[0].low_word().bval == 0U
            ? runtime::SystemVerilogClassHandle { actuals[0].low_word().aval }
            : runtime::SystemVerilogClassHandle { };
        if (canonical_identity == "@uvm-sequence-item-done") {
            return runtime::PackedLogic4::from_aval_bval(1U,
                uvm_pending_items.contains(operand) ? 0U : 1U, 0U);
        }
        const auto items = uvm_sequencer_items.find(
            uvm_pull_sequencer(operand));
        return runtime::PackedLogic4::from_aval_bval(1U,
            items != uvm_sequencer_items.end() && !items->second.empty()
                ? 1U : 0U, 0U);
    }
    // The index-th object implementing an analysis imp that a write on the
    // port reaches through its connections; none past the last.
    if (canonical_identity == "@uvm-analysis-target") {
        if (actuals.size() != 2U) {
            throw std::invalid_argument {
                "UVM analysis target requires a port and an index"
            };
        }
        std::vector<runtime::SystemVerilogClassHandle> targets;
        std::vector<runtime::SystemVerilogClassHandle> pending {
            actuals[0].low_word().aval };
        std::set<runtime::SystemVerilogClassHandle> visited;
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (current == 0 || !visited.insert(current).second) {
                continue;
            }
            const auto connected = uvm_analysis_connections.find(current);
            if (connected != uvm_analysis_connections.end()) {
                pending.insert(pending.end(), connected->second.rbegin(),
                    connected->second.rend());
                continue;
            }
            const auto owner = uvm_tlm_owners.find(current);
            if (owner != uvm_tlm_owners.end()
                && (uvm_tlm_implementations.contains(current)
                    || is_systemverilog_uvm_type(
                        built.compiled_systemverilog_class_specializations,
                        class_specialization(
                            class_heap.object(current).specialization_identity),
                        support::UvmRole::analysis_imp))) {
                targets.push_back(owner->second);
            }
        }
        const auto index = actuals[1].low_word().aval;
        return runtime::PackedLogic4::from_aval_bval(64,
            index < targets.size() ? targets[index]
                                   : runtime::SystemVerilogClassHandle { },
            0);
    }
    // `uvm_root::get()`, `uvm_cmdline_processor::get_inst()`,
    // `uvm_test_done_objection::get()` and `uvm_resource_pool::get()` answer
    // the one object of the class, allocated on the first call without the
    // library constructor (the native services do the work, as for the root's
    // report object below); the package variables `uvm_top`, `uvm_cmdline_proc`,
    // `uvm_test_done` and `uvm_resources` hold it.
    if (constexpr std::string_view singleton { "@uvm-singleton:" };
        canonical_identity.starts_with(singleton)) {
        const std::string identity { canonical_identity.substr(singleton.size()) };
        const auto singleton_handle = uvm_singleton(identity);
        return runtime::PackedLogic4::from_aval_bval(64, singleton_handle, 0);
    }
    // The package-level report functions report for the root (IEEE
    // 1800.2-2020 F.3.2.3).
    if (canonical_identity.starts_with("@uvm-report-")) {
        const auto word = [&](const std::size_t index, const std::int64_t fallback) {
            return index < actuals.size() && actuals[index].low_word().bval == 0U
                ? static_cast<std::int64_t>(static_cast<std::int32_t>(
                      static_cast<std::uint32_t>(actuals[index].low_word().aval)))
                : fallback;
        };
        const auto text = [&](const std::size_t index) {
            return index < string_actuals.size() ? string_actuals[index] : std::string { };
        };
        const auto member = canonical_identity.substr(
            std::string_view { "@uvm-report-" }.size());
        if (member == "object") {
            // The root's report object, allocated once without running the
            // library constructor; reports through it go to the root.
            return runtime::PackedLogic4::from_aval_bval(
                64, uvm_root_report_handle(), 0);
        }
        if (member == "enabled") {
            const auto severity = word(1U, 0);
            return runtime::PackedLogic4::from_aval_bval(32U,
                uvm_reports.enabled(0,
                    static_cast<std::int32_t>(word(0U, 200)),
                    severity >= 0 && severity <= 3
                        ? static_cast<runtime::SystemVerilogUvmReportSeverity>(severity)
                        : runtime::SystemVerilogUvmReportSeverity::Info,
                    text(2U))
                    ? 1U
                    : 0U,
                0U);
        }
        runtime::SystemVerilogUvmReportRequest request;
        request.severity = member == "info"
            ? runtime::SystemVerilogUvmReportSeverity::Info
            : member == "warning"
            ? runtime::SystemVerilogUvmReportSeverity::Warning
            : member == "error"
            ? runtime::SystemVerilogUvmReportSeverity::Error
            : runtime::SystemVerilogUvmReportSeverity::Fatal;
        request.id = text(0U);
        request.message = text(1U);
        // UVM_MEDIUM for info, UVM_NONE for the others (F.3.2.3).
        request.verbosity = static_cast<std::int32_t>(
            word(2U, member == "info" ? 200 : 0));
        request.filename = text(3U);
        request.line = static_cast<std::uint32_t>(word(4U, 0));
        request.timestamp = interpreter->scheduler().now();
        (void)uvm_reports.report(request);
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    if (canonical_identity.starts_with("@uvm-run-test-")) {
        if (auto result = invoke_uvm_run_test(
                canonical_identity, actuals, string_actuals)) {
            return std::move(*result);
        }
    }
    constexpr std::string_view config_set_prefix { "@uvm-config-db-set:" };
    constexpr std::string_view config_get_prefix { "@uvm-config-db-get:" };
    // uvm_resource_db keeps its resources in the same database; a resource's
    // scope and name are a configuration's instance path and field.
    constexpr std::string_view resource_set_prefix { "@uvm-resource-db-set:" };
    constexpr std::string_view resource_write_prefix {
        "@uvm-resource-db-write_by_name:"
    };
    constexpr std::string_view resource_read_prefix {
        "@uvm-resource-db-read_by_name:"
    };
    const auto resource_set = canonical_identity.starts_with(resource_set_prefix)
        || canonical_identity.starts_with(resource_write_prefix);
    const auto resource_get = canonical_identity.starts_with(resource_read_prefix);
    const auto resource = resource_set || resource_get;
    const auto config_set = resource_set
        || canonical_identity.starts_with(config_set_prefix);
    const auto config_get = resource_get
        || canonical_identity.starts_with(config_get_prefix);
    if (config_set || config_get) {
        const auto required = resource ? 3U : 4U;
        if (actuals.size() < required || actuals.size() > 4U
            || string_actuals.size() != actuals.size()) {
            throw std::invalid_argument {
                resource
                    ? "UVM resource_db call requires scope, name, and value"
                    : "UVM config_db call requires context, instance, field, and value"
            };
        }
        // A resource call's (scope, name, value[, accessor]) are a
        // configuration's (instance, field, value) without a context.
        const std::size_t instance = resource ? 0U : 1U;
        const auto field = instance + 1U;
        const auto slot = instance + 2U;
        const auto prefix = canonical_identity.substr(
            0, canonical_identity.find(':') + 1U);
        const auto identity = canonical_identity.substr(prefix.size());
        runtime::SystemVerilogUvmResourceType type;
        type.identity = identity;
        if (identity == "string") {
            type.kind = runtime::SystemVerilogUvmResourceValueKind::String;
        } else if (identity.starts_with("class:")) {
            type.kind = runtime::SystemVerilogUvmResourceValueKind::Object;
            type.packed_width = 64U;
        } else {
            type.kind = runtime::SystemVerilogUvmResourceValueKind::Packed;
            const auto separator = identity.rfind(':');
            if (separator != std::string_view::npos) {
                std::from_chars(
                    identity.data() + separator + 1U,
                    identity.data() + identity.size(), type.packed_width);
            }
            if (type.packed_width == 0U)
                type.packed_width = actuals[slot].width();
        }
        runtime::SystemVerilogUvmConfigContext context;
        const auto context_word = resource
            ? runtime::PackedLogic4::from_aval_bval(64, 0, 0).low_word()
            : actuals[0].low_word();
        if (context_word.bval != 0) {
            throw std::invalid_argument {
                "UVM config_db context must be a known class handle"
            };
        }
        if (context_word.aval != 0
            && uvm_components.contains(context_word.aval)) {
            const auto component = uvm_components.snapshot(context_word.aval);
            context.full_name = component.full_name;
            context.depth = component.depth;
        }
        if (config_set) {
            runtime::SystemVerilogUvmResourceValue value;
            if (type.kind
                == runtime::SystemVerilogUvmResourceValueKind::String) {
                value = string_actuals[slot];
            } else if (type.kind
                == runtime::SystemVerilogUvmResourceValueKind::Object) {
                value = static_cast<runtime::SystemVerilogClassHandle>(
                    actuals[slot].low_word().aval);
            } else {
                value = resize_packed(actuals[slot], type.packed_width);
            }
            (void)uvm_config_db.set(
                context, string_actuals[instance], string_actuals[field],
                std::move(type), std::move(value),
                runtime::SystemVerilogUvmConfigPhase::Runtime);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        const auto value = uvm_config_db.get(
            context, string_actuals[instance], string_actuals[field], identity);
        if (!value)
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        if (const auto packed = std::get_if<runtime::PackedLogic4>(&*value)) {
            actuals[slot] = *packed;
        } else if (const auto text = std::get_if<std::string>(&*value)) {
            string_actuals[slot] = *text;
        } else if (const auto object
            = std::get_if<runtime::SystemVerilogClassHandle>(&*value)) {
            actuals[slot]
                = runtime::PackedLogic4::from_aval_bval(64, *object, 0);
        }
        return runtime::PackedLogic4::from_aval_bval(1, 1, 0);
    }
    // `get_type()`: the library's uvm_object::get_type or a static method
    // hiding it (the registration macros', IEEE 1800.2-2020 B.2.1).
    const auto method_separator = canonical_identity.rfind("::");
    const auto method_name = method_separator == std::string_view::npos
        ? canonical_identity
        : canonical_identity.substr(method_separator + 2U);
    const auto hides_get_type = [&] {
        if (static_specialization.empty())
            return false;
        // The library's uvm_object of the class's base chain: the static
        // method of the class hides its get_type in the class's own list.
        const auto* current = &class_specialization(static_specialization);
        while (current->uvm_role != support::UvmRole::object) {
            if (!current->base)
                return false;
            current = &class_specialization(
                current->base->specialization_identity);
        }
        const auto& methods = current->methods;
        const auto library = std::ranges::find(methods,
            support::UvmRole::object_get_type,
            &semantic::sv::SpecializedClassMethod::uvm_role);
        return library != methods.end() && library->name == method_name;
    };
    if (actuals.empty() && method_separator != std::string_view::npos
        && (uvm_method_role(canonical_identity).role
                == support::UvmRole::object_get_type
            || hides_get_type())) {
        // `C#(4)::get_type()` is the type of that specialization (IEEE
        // 1800-2023 8.25.1); a class registers each one.
        const auto wrapper
            = uvm_registry.wrapper_by_specialization(static_specialization);
        if (wrapper != 0)
            return runtime::PackedLogic4::from_aval_bval(64, wrapper, 0);
    }
    // The registry's `get` the class executor calls on the registry
    // itself: the type wrapper of the class its type parameter names (IEEE
    // 1800.2-2020 8.2.4.2.1).
    if (actuals.empty() && !static_specialization.empty()
        && uvm_method_role(canonical_identity).role
            == support::UvmRole::registry_get) {
        const auto wrapper = registry_parameter_wrapper();
        if (wrapper == 0) {
            throw std::invalid_argument {
                "UVM registry get of '" + std::string { static_specialization }
                + "' names no registered object type"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(64, wrapper, 0);
    }
    // A static method runs in the specialization the call names, which
    // has static properties of its own (IEEE 1800-2023 8.25.1); its entry
    // there carries the method as that specialization sees it. Every call
    // names one: the lowered call and the class executor's alike.
    const auto* method = static_specialization.empty()
        ? nullptr
        : class_hir_execution.specialization_method(
            static_specialization, canonical_identity);
    if (method != nullptr && (!method->static_method || method->pure)) {
        method = nullptr;
    }
    if (method != nullptr) {
        // The member lives in the specialization of the class declaring
        // it, a base of the named one when inherited.
        std::string running;
        std::vector<std::string> followed;
        for (std::string current { static_specialization };
             !current.empty()
             && std::ranges::find(followed, current) == followed.end();) {
            const semantic::sv::ClassSpecialization* named { };
            try {
                named = &class_specialization(current);
            } catch (const std::out_of_range&) {
                break;
            }
            if (named->declaration_identity == method->owner_identity) {
                running = named->specialization_identity;
                break;
            }
            followed.push_back(std::move(current));
            current = named->base ? named->base->specialization_identity
                                  : std::string { };
        }
        return class_hir_execution.invoke_function(
            *method, 0, actuals, string_actuals, actual_names,
            actual_directions, actual_references, report_process, running);
    }
    throw std::invalid_argument {
        "compiled HIR class static method '"
        + std::string { canonical_identity } + "' is not executable"
        + (static_specialization.empty()
                  ? std::string { " (the call names no specialization)" }
                  : " in specialization '"
                      + std::string { static_specialization } + "'")
    };
}

void Simulation::Impl::invoke_source_constructor(
    const semantic::sv::ClassSpecialization& specialization,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals,
    const std::span<const std::string> actual_names,
    const std::optional<runtime::simir::ProcessId> report_process)
{
    // The unmodified UVM library constructors bootstrap their own singleton
    // scheduler, report server, resource pool, and component hierarchy. Those
    // facilities are simulation-owned services in fsim, so executing the
    // upstream constructor bodies would duplicate state and recursively pull
    // the complete reference scheduler into each source allocation. The host
    // services initialize the corresponding object/component state after the
    // user-derived constructor returns.
    if (support::uvm_library(specialization.uvm_role)) {
        return;
    }
    const bool outermost = source_function_dispatch_depth == 0U;
    const auto static_before = outermost
        ? packed_static_snapshot() : StaticPackedSnapshot { };
    ++source_function_dispatch_depth;
    bool invoked { };
    try {
        invoked = class_hir_execution.invoke_constructor(
            specialization.specialization_identity, handle, actuals,
            string_actuals, actual_names, report_process);
    } catch (...) {
        --source_function_dispatch_depth;
        if (outermost) {
            // Constructor writes to static class storage survive a failed
            // construction, just as writes made by a function do.
            try {
                notify_static_changes(static_before);
            } catch (...) {
            }
        }
        throw;
    }
    --source_function_dispatch_depth;
    if (!invoked) {
        throw std::invalid_argument {
            "compiled HIR constructor '" + specialization.declaration_identity
            + "' is not executable"
        };
    }
    if (outermost)
        notify_static_changes(static_before);
}
} // namespace fsim::app

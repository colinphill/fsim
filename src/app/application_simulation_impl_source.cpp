// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "fsim/support/uvm_identity.hpp"

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
        return invoke_source_randomize(handle, actual_names, actuals, { });
    }
    if (canonical_identity.starts_with("@builtin-rand-mode:")
        || canonical_identity.starts_with("@builtin-constraint-mode:")) {
        return invoke_source_randomization_mode(
            handle, canonical_identity, actuals);
    }
    const auto method_separator = canonical_identity.rfind("::");
    const auto method_name = method_separator == std::string_view::npos
        ? canonical_identity
        : canonical_identity.substr(method_separator + 2U);
    // The UVM library's phase callbacks of uvm_component (reached through
    // `super.build_phase(phase)` and the like) do framework bookkeeping that
    // the simulation's UVM phase services perform; their bodies are not run.
    // The same holds for the overrides in the library's component classes
    // (uvm_driver, uvm_sequencer_base and the like).
    if (support::is_uvm_library_identity(canonical_identity)
        && method_name.ends_with("_phase")
        && (canonical_identity.find("uvm_pkg::uvm_component::")
                != std::string_view::npos
            || uvm_components.contains(handle))) {
        return runtime::PackedLogic4 { };
    }
    // A UVM object's name and path come from the native object and component
    // services, which its construction initialized (IEEE 1800.2-2020 5.3.4,
    // 13.1.3); the library constructors that would set them do not run.
    if ((method_name == "get_name" || method_name == "get_full_name")
        && support::is_uvm_library_identity(canonical_identity)
        && !actual_names.empty() && actual_names.back() == "@result-string"
        && !string_actuals.empty()
        && (uvm_components.contains(handle) || uvm_objects.contains(handle))) {
        string_actuals.back() = uvm_components.contains(handle)
            ? (method_name == "get_name"
                    ? uvm_components.snapshot(handle).name
                    : uvm_components.full_name(handle))
            : (method_name == "get_name"
                    ? std::string { uvm_objects.name(handle) }
                    : uvm_objects.full_name(handle));
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    // A method of a phase object the native run passes to a phase callback.
    if (canonical_identity.find("uvm_pkg::uvm_phase::") != std::string_view::npos
        && support::is_uvm_library_identity(canonical_identity)) {
        if (auto result = invoke_uvm_phase_method(
                handle, method_name, actuals, string_actuals)) {
            return std::move(*result);
        }
    }
    // `port.connect(provider)` records an analysis connection that the
    // port's write follows (IEEE 1800.2-2020 5.4.2.1, 12.2.10.1).
    if (method_name == "connect"
        && support::is_uvm_library_identity(canonical_identity)
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
    if (support::is_uvm_library_identity(canonical_identity)
        && canonical_identity.find("::uvm_sequence_base::")
            != std::string_view::npos
        && (method_name == "start" || method_name == "start_item"
            || method_name == "finish_item")) {
        if (method_name == "start" && handle_actual(0U) != 0U) {
            uvm_sequence_sequencers[handle] = handle_actual(0U);
        }
        const auto sequencer = uvm_sequence_sequencers.find(handle);
        if (method_name == "finish_item" && handle_actual(0U) != 0U
            && sequencer != uvm_sequence_sequencers.end()) {
            uvm_sequencer_items[sequencer->second].push_back(
                handle_actual(0U));
            uvm_pending_items.insert(handle_actual(0U));
            toggle_class_heap_change();
        }
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    if (support::is_uvm_library_identity(canonical_identity)
        && canonical_identity.find("::uvm_seq_item_pull_")
            != std::string_view::npos) {
        const auto sequencer = uvm_pull_sequencer(handle);
        auto& items = uvm_sequencer_items[sequencer];
        if ((method_name == "get" || method_name == "get_next_item"
                || method_name == "try_next_item" || method_name == "peek")
            && !actuals.empty()) {
            runtime::SystemVerilogClassHandle item { };
            if (!items.empty()) {
                item = items.front();
                if (method_name != "peek") {
                    items.erase(items.begin());
                }
                if (method_name == "get") {
                    uvm_pending_items.erase(item);
                    toggle_class_heap_change();
                } else if (method_name != "peek") {
                    uvm_sequencer_current[sequencer] = item;
                }
            }
            actuals[0] = runtime::PackedLogic4::from_aval_bval(64U, item, 0U);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        if (method_name == "item_done") {
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
    if (canonical_identity.find("uvm_report_object::")
            != std::string_view::npos
        && support::is_uvm_library_identity(canonical_identity)
        && (method_name == "get_report_verbosity_level"
            || method_name == "get_report_action"
            || method_name == "uvm_report_enabled"
            || method_name == "uvm_get_report_object")) {
        if (method_name == "uvm_get_report_object") {
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
        if (method_name == "uvm_report_enabled") {
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
            method_name == "get_report_action"
                ? static_cast<std::uint32_t>(policy.action)
                : static_cast<std::uint32_t>(policy.verbosity),
            0U);
    }
    const auto report_severity = method_name == "uvm_report_info"
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Info }
        : method_name == "uvm_report_warning"
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Warning }
        : method_name == "uvm_report_error"
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Error }
        : method_name == "uvm_report_fatal"
        ? std::optional { runtime::SystemVerilogUvmReportSeverity::Fatal }
        : std::nullopt;
    if (report_severity && support::is_uvm_library_identity(canonical_identity)
        && actuals.size() >= 3U && string_actuals.size() >= 3U) {
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
    const auto uvm_base_method
        = canonical_identity.find("::uvm_object::") != std::string_view::npos;
    if (uvm_base_method && uvm_objects.contains(handle)) {
        if (method_name == "get_inst_id" && actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                32, uvm_objects.instance_id(handle), 0);
        }
        if (method_name == "clone" && actuals.empty()) {
            if (uvm_components.contains(handle))
                return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
            return runtime::PackedLogic4::from_aval_bval(
                64, uvm_objects.clone(handle), 0);
        }
        if (method_name == "copy" && actuals.size() == 1U) {
            uvm_objects.copy(handle, actuals.front().low_word().aval);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        if (method_name == "compare" && !actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                1,
                uvm_objects.compare(handle, actuals.front().low_word().aval)
                    ? 1U
                    : 0U,
                0);
        }
        if (method_name == "print") {
            (void)uvm_objects.print(handle);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        if (method_name == "record") {
            (void)uvm_objects.record(handle);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
    }
    const auto uvm_component_base_method
        = canonical_identity.find("::uvm_component::")
        != std::string_view::npos;
    if (uvm_component_base_method && uvm_components.contains(handle)) {
        if (method_name == "get_parent" && actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                64, uvm_components.parent(handle), 0);
        }
        if (method_name == "get_num_children" && actuals.empty()) {
            return runtime::PackedLogic4::from_aval_bval(
                32, uvm_components.children(handle).size(), 0);
        }
    }
    if (uvm_objects.contains(handle)
        && method_name == "get_object_type" && actuals.empty()) {
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
    if (canonical_identity.starts_with(registry_create_prefix)) {
        if (actuals.empty() || string_actuals.empty()) {
            throw std::invalid_argument {
                "UVM registry create requires a string name actual"
            };
        }
        // A parameterized class registers each specialization; the one the
        // design names explicitly is created when the bare declaration is
        // ambiguous.
        const auto declaration
            = canonical_identity.substr(registry_create_prefix.size());
        runtime::SystemVerilogUvmTypeHandle wrapper { };
        std::vector<runtime::SystemVerilogUvmTypeHandle> named;
        std::size_t registered { };
        for (const auto& type : uvm_registry.types()) {
            if (type.declaration_identity != declaration) {
                continue;
            }
            ++registered;
            wrapper = type.wrapper;
            if (!class_specialization(type.specialization_identity)
                     .default_specialization) {
                named.push_back(type.wrapper);
            }
        }
        if (registered > 1U) {
            if (named.size() != 1U) {
                throw std::invalid_argument {
                    "UVM declaration has multiple registered specializations"
                };
            }
            wrapper = named.front();
        }
        if (wrapper == 0) {
            throw std::invalid_argument {
                "UVM registry create selected an unknown object type"
            };
        }
        // A component is created under its parent (IEEE 1800.2-2020
        // 8.2.4.2); without one it joins the run_test hierarchy.
        if (uvm_registry.snapshot(wrapper).kind
            == runtime::SystemVerilogUvmRegisteredKind::Component) {
            const auto parent = actuals.size() > 1U
                ? actuals[1].low_word().aval : std::uint64_t { };
            return runtime::PackedLogic4::from_aval_bval(64,
                uvm_registry.create_component_by_type(wrapper,
                    string_actuals.front(), parent,
                    parent == 0 ? component_root("$simulation")
                                : runtime::SystemVerilogUvmRootHandle { }),
                0);
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
                && class_specialization(
                       class_heap.object(current).specialization_identity)
                           .declaration_identity.find("uvm_analysis_imp")
                    != std::string::npos) {
                targets.push_back(owner->second);
            }
        }
        const auto index = actuals[1].low_word().aval;
        return runtime::PackedLogic4::from_aval_bval(64,
            index < targets.size() ? targets[index]
                                   : runtime::SystemVerilogClassHandle { },
            0);
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
            if (uvm_root_report_object == 0) {
                uvm_root_report_object = construct_class(
                    "@no-constructor:uvm::uvm_pkg::uvm_report_object",
                    "uvm::uvm_pkg::uvm_report_object",
                    std::span<const runtime::PackedLogic4> { },
                    std::span<const std::string> { },
                    std::span<const std::string> { }, "$uvm-root");
            }
            return runtime::PackedLogic4::from_aval_bval(
                64, uvm_root_report_object, 0);
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
    const auto method_separator = canonical_identity.rfind("::");
    const auto method_name = method_separator == std::string_view::npos
        ? canonical_identity
        : canonical_identity.substr(method_separator + 2U);
    if (method_name == "get_type" && actuals.empty()
        && method_separator != std::string_view::npos) {
        const auto wrapper = uvm_registry.unique_wrapper_by_declaration(
            canonical_identity.substr(0, method_separator));
        if (wrapper != 0)
            return runtime::PackedLogic4::from_aval_bval(64, wrapper, 0);
    }
    if (const auto* method
        = class_hir_execution.static_method(canonical_identity)) {
        return class_hir_execution.invoke_function(
            *method, 0, actuals, string_actuals, actual_names,
            actual_directions, actual_references, report_process);
    }
    throw std::invalid_argument {
        "compiled HIR class static method '"
        + std::string { canonical_identity } + "' is not executable"
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
    if (support::is_uvm_library_identity(
            specialization.declaration_identity)) {
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

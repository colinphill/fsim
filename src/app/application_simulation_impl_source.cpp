// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

#include <charconv>

namespace fsim::app {
[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_function(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view canonical_identity,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names,
    const std::span<const std::uint8_t> actual_directions,
    const bool virtual_dispatch)
{
    const auto method_separator = canonical_identity.rfind("::");
    const auto method_name = method_separator == std::string_view::npos
        ? canonical_identity
        : canonical_identity.substr(method_separator + 2U);
    if (method_name == "uvm_report_info"
        && actuals.size() >= 3U && string_actuals.size() >= 3U) {
        runtime::SystemVerilogUvmReportRequest request;
        request.report_object = handle;
        request.severity = runtime::SystemVerilogUvmReportSeverity::Info;
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
            actual_directions);
    }
    throw std::invalid_argument {
        "compiled HIR class method '" + std::string { canonical_identity }
        + "' is not executable"
    };
}

[[nodiscard]] runtime::PackedLogic4
Simulation::Impl::invoke_source_static_function(
    const std::string_view canonical_identity,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names,
    const std::span<const std::uint8_t> actual_directions)
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
        const auto wrapper = uvm_registry.unique_wrapper_by_declaration(
            canonical_identity.substr(registry_create_prefix.size()));
        if (wrapper == 0) {
            throw std::invalid_argument {
                "UVM registry create selected an unknown object type"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(
            64,
            uvm_registry.create_object_by_type(
                wrapper, string_actuals.front()),
            0);
    }
    constexpr std::string_view config_set_prefix { "@uvm-config-db-set:" };
    constexpr std::string_view config_get_prefix { "@uvm-config-db-get:" };
    const auto config_set = canonical_identity.starts_with(config_set_prefix);
    const auto config_get = canonical_identity.starts_with(config_get_prefix);
    if (config_set || config_get) {
        if (actuals.size() != 4U || string_actuals.size() != 4U) {
            throw std::invalid_argument {
                "UVM config_db call requires context, instance, field, and value"
            };
        }
        const auto prefix = config_set ? config_set_prefix : config_get_prefix;
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
                type.packed_width = actuals[3].width();
        }
        runtime::SystemVerilogUvmConfigContext context;
        const auto context_word = actuals[0].low_word();
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
                value = string_actuals[3];
            } else if (type.kind
                == runtime::SystemVerilogUvmResourceValueKind::Object) {
                value = static_cast<runtime::SystemVerilogClassHandle>(
                    actuals[3].low_word().aval);
            } else {
                value = resize_packed(actuals[3], type.packed_width);
            }
            (void)uvm_config_db.set(
                context, string_actuals[1], string_actuals[2],
                std::move(type), std::move(value),
                runtime::SystemVerilogUvmConfigPhase::Runtime);
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
        const auto value = uvm_config_db.get(
            context, string_actuals[1], string_actuals[2], identity);
        if (!value)
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        if (const auto packed = std::get_if<runtime::PackedLogic4>(&*value)) {
            actuals[3] = *packed;
        } else if (const auto text = std::get_if<std::string>(&*value)) {
            string_actuals[3] = *text;
        } else if (const auto object
            = std::get_if<runtime::SystemVerilogClassHandle>(&*value)) {
            actuals[3]
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
            actual_directions);
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
    const std::span<const std::string> actual_names)
{
    // The unmodified UVM library constructors bootstrap their own singleton
    // scheduler, report server, resource pool, and component hierarchy. Those
    // facilities are simulation-owned services in fsim, so executing the
    // upstream constructor bodies would duplicate state and recursively pull
    // the complete reference scheduler into each source allocation. The host
    // services initialize the corresponding object/component state after the
    // user-derived constructor returns.
    if (specialization.declaration_identity.find("uvm_pkg::")
        != std::string::npos) {
        return;
    }
    if (class_hir_execution.invoke_constructor(
            specialization.specialization_identity, handle, actuals,
            actual_names)) {
        return;
    }
    throw std::invalid_argument {
        "compiled HIR constructor '" + specialization.declaration_identity
        + "' is not executable"
    };
}
} // namespace fsim::app

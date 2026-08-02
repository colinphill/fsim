// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;



    const SystemCInstanceDescription* HierarchyBuilder::systemc_description(
        const std::string& path,
        const std::string_view target,
        const frontend::SourceSpan& source) {
        const auto found = systemc_instances_.find(path);
        if (found == systemc_instances_.end()) {
            report(
                "FSIM-ELAB-BIND-038",
                "SystemC target '" + std::string{target}
                    + "' at '" + path
                    + "' was not constructed before HDL elaboration",
                source);
            return nullptr;
        }
        if (found->second->target != target) {
            report(
                "FSIM-ELAB-BIND-039",
                "constructed SystemC target '"
                    + found->second->target + "' at '" + path
                    + "' does not match binding target '"
                    + std::string{target} + "'",
                source);
            return nullptr;
        }
        used_systemc_instances_.insert(path);
        return found->second;
    }



    const SystemCInstanceDescription* HierarchyBuilder::construct_systemc_description(
        const frontend::Instance& instance,
        const std::string& path,
        const std::string_view target,
        const ConstantEnvironment& parent_environment,
        const frontend::Language association_language) {
        if (systemc_provider_ == nullptr) {
            if (!instance.parameter_overrides.empty()) {
                report(
                    association_language
                            == frontend::Language::Vhdl2008
                        ? "FSIM-ELAB-GENERIC-001"
                        : "FSIM-ELAB-PARAM-001",
                    "HDL generic or parameter actuals require a live "
                    "SystemC factory schema provider",
                    instance.parameter_overrides.front().span);
                return nullptr;
            }
            return systemc_description(path, target, instance.span);
        }

        std::string error;
        auto schema = systemc_provider_->schema(target, error);
        if (!schema) {
            report(
                "FSIM-ELAB-SC-PARAM-007",
                "cannot inspect SystemC construction schema for '"
                    + std::string{target} + "': " + error,
                instance.span);
            return nullptr;
        }
        auto values = specialize_systemc_construction(
            *schema,
            instance.parameter_overrides,
            parent_environment,
            association_language,
            diagnostics_);
        if (!values) {
            return nullptr;
        }
        auto constructed = systemc_provider_->instantiate(
            path, target, *values, error);
        if (!constructed) {
            report(
                "FSIM-ELAB-SC-PARAM-008",
                "cannot construct SystemC instance '" + path
                    + "': " + error,
                instance.span);
            return nullptr;
        }
        if (constructed->path != path
            || constructed->target != target) {
            report(
                "FSIM-ELAB-SC-PARAM-008",
                "SystemC factory provider returned inconsistent "
                "instance identity for '" + path + "'",
                instance.span);
            return nullptr;
        }
        owned_systemc_instances_.push_back(
            std::move(*constructed));
        const auto* description =
            &owned_systemc_instances_.back();
        if (!systemc_instances_
                 .emplace(path, description)
                 .second) {
            report(
                "FSIM-ELAB-BIND-032",
                "duplicate constructed SystemC instance path '"
                    + path + "'",
                instance.span);
            return nullptr;
        }
        used_systemc_instances_.insert(path);
        return description;
    }



    void HierarchyBuilder::instantiate_systemc(
        const SystemCInstanceDescription& instance,
        const std::string& path,
        SignalMap aliases,
        ObjectMap objects,
        const bool native_child) {
        if (!instance_paths_.insert(path).second) {
            report(
                "FSIM-ELAB-HIER-001",
                "duplicate instance path '" + path + "'",
                {});
            return;
        }
        const auto stack_identity =
            native_child
                ? instance.target + "#native:"
                    + std::to_string(instance.handle)
                : instance.target;
        if (std::find(
                stack_.begin(), stack_.end(), stack_identity)
            != stack_.end()) {
            report(
                "FSIM-ELAB-HIER-002",
                "recursive instantiation of '" + instance.target
                    + "' at '" + path + "'",
                {});
            return;
        }
        stack_.push_back(stack_identity);
        used_systemc_instances_.insert(path);

        std::unordered_set<std::uint64_t> connected_ports;
        for (const auto& port : instance.ports) {
            if (objects.contains(port.handle)) {
                connected_ports.insert(port.handle);
            }
            if (port.bound_object != 0
                && !objects.contains(port.bound_object)
                && std::none_of(
                    instance.internal_signals.begin(),
                    instance.internal_signals.end(),
                    [&](const ExternalInternalSignal& signal) {
                        return signal.handle == port.bound_object;
                    })) {
                report(
                    "FSIM-ELAB-BIND-046",
                    "SystemC port '" + path + "." + port.name
                        + "' binds an unknown signal handle",
                    {});
            }
        }

        for (const auto& port : instance.ports) {
            if (!aliases.contains(port.name)) {
                const auto declaration =
                    external_port_declaration(port);
                const auto signal =
                    add_owned_signal(declaration, path, aliases);
                if (signal) {
                    objects.emplace(port.handle, *signal);
                }
            } else if (!objects.contains(port.handle)) {
                objects.emplace(port.handle, aliases.at(port.name));
            }
        }
        for (const auto& event : instance.events) {
            frontend::Type type;
            type.domain = frontend::ValueDomain::Bit2;
            type.spelling = "systemc.event";
            const frontend::SignalDeclaration declaration{
                event.name,
                std::move(type),
                frontend::PortDirection::Unknown,
                false,
                {}};
            const auto signal =
                add_owned_signal(declaration, path, aliases);
            if (signal) {
                objects.emplace(event.handle, *signal);
            }
        }
        for (const auto& signal : instance.internal_signals) {
            std::optional<SignalId> bound_signal;
            bool use_internal_initial = false;
            bool conflicting_aliases = false;
            for (const auto& port : instance.ports) {
                if (port.bound_object != signal.handle) {
                    continue;
                }
                const auto runtime_port = objects.find(port.handle);
                if (runtime_port == objects.end()) {
                    continue;
                }
                if (bound_signal
                    && *bound_signal != runtime_port->second) {
                    report(
                        "FSIM-ELAB-BIND-046",
                        "SystemC ports bound to internal signal '"
                            + path + "." + signal.name
                            + "' connect to different parent signals",
                        {});
                    conflicting_aliases = true;
                    break;
                }
                bound_signal = runtime_port->second;
                use_internal_initial =
                    use_internal_initial
                    || !connected_ports.contains(port.handle)
                    || port.direction
                        != frontend::PortDirection::Input;
            }
            if (conflicting_aliases) {
                continue;
            }
            if (bound_signal) {
                const auto full_name = path + "." + signal.name;
                const auto local =
                    aliases.emplace(signal.name, *bound_signal);
                const auto full =
                    aliases.emplace(full_name, *bound_signal);
                if ((!local.second
                     && local.first->second != *bound_signal)
                    || (!full.second
                        && full.first->second != *bound_signal)) {
                    report(
                        "FSIM-ELAB-BIND-046",
                        "SystemC internal signal alias '" + full_name
                            + "' conflicts with another object",
                        {});
                    continue;
                }
                design_.signal_by_name_.emplace(
                    full_name, *bound_signal);
                if (path == design_.top_) {
                    design_.signal_by_name_.emplace(
                        signal.name, *bound_signal);
                }
                objects.emplace(signal.handle, *bound_signal);
                if (use_internal_initial) {
                    design_.signals_.at(*bound_signal).initial_value =
                        signal.initial_value;
                }
                continue;
            }
            const frontend::SignalDeclaration declaration{
                signal.name,
                signal.type,
                frontend::PortDirection::Unknown,
                false,
                {}};
            const auto runtime_signal =
                add_owned_signal(declaration, path, aliases);
            if (runtime_signal) {
                design_.signals_.at(*runtime_signal).initial_value =
                    signal.initial_value;
                objects.emplace(signal.handle, *runtime_signal);
            }
        }

        std::unordered_map<
            std::uint64_t, const ExternalExport*> exports_by_handle;
        for (const auto& export_object : instance.exports) {
            exports_by_handle.emplace(
                export_object.handle, &export_object);
        }
        std::unordered_set<std::uint64_t> resolving_exports;
        std::unordered_set<std::uint64_t> invalid_exports;
        const auto resolve_export =
            [&](const auto& self,
                const ExternalExport& export_object)
                -> std::optional<SignalId> {
              if (const auto resolved =
                      objects.find(export_object.handle);
                  resolved != objects.end()) {
                  return resolved->second;
              }
              if (export_object.bound_object == 0) {
                  if (invalid_exports.insert(
                          export_object.handle).second) {
                      report(
                          "FSIM-ELAB-BIND-048",
                          "SystemC export '" + path + "."
                              + export_object.name
                              + "' is unbound",
                          {});
                  }
                  return std::nullopt;
              }
              if (!resolving_exports.insert(
                      export_object.handle).second) {
                  if (invalid_exports.insert(
                          export_object.handle).second) {
                      report(
                          "FSIM-ELAB-BIND-048",
                          "SystemC export chain at '" + path + "."
                              + export_object.name
                              + "' is cyclic",
                          {});
                  }
                  return std::nullopt;
              }
              std::optional<SignalId> signal;
              if (const auto direct =
                      objects.find(export_object.bound_object);
                  direct != objects.end()) {
                  signal = direct->second;
              } else if (const auto nested =
                             exports_by_handle.find(
                                 export_object.bound_object);
                         nested != exports_by_handle.end()) {
                  signal = self(self, *nested->second);
              } else if (invalid_exports.insert(
                             export_object.handle).second) {
                  report(
                      "FSIM-ELAB-BIND-048",
                      "SystemC export '" + path + "."
                          + export_object.name
                          + "' binds an unknown object handle",
                      {});
              }
              resolving_exports.erase(export_object.handle);
              if (signal) {
                  objects.emplace(export_object.handle, *signal);
              }
              return signal;
            };
        for (const auto& export_object : instance.exports) {
            const auto signal =
                resolve_export(resolve_export, export_object);
            if (!signal) {
                continue;
            }
            const auto full_name = path + "." + export_object.name;
            const auto local =
                aliases.emplace(export_object.name, *signal);
            const auto full = aliases.emplace(full_name, *signal);
            if ((!local.second && local.first->second != *signal)
                || (!full.second && full.first->second != *signal)) {
                report(
                    "FSIM-ELAB-BIND-048",
                    "SystemC export alias '" + full_name
                        + "' conflicts with another object",
                    {});
                continue;
            }
            design_.signal_by_name_.emplace(full_name, *signal);
            if (path == design_.top_) {
                design_.signal_by_name_.emplace(
                    export_object.name, *signal);
            }
        }

        SystemCInstanceInfo info;
        info.id = static_cast<std::uint32_t>(
            design_.systemc_instances_.size());
        info.target = instance.target;
        info.instance = path;
        info.native_handle = instance.handle;
        info.construction_values =
            instance.construction_values;
        for (const auto& port : instance.ports) {
            if (const auto signal = aliases.find(port.name);
                signal != aliases.end()) {
                info.ports.push_back(
                    {port.name, port.handle, signal->second});
            }
        }
        for (const auto& event : instance.events) {
            if (const auto signal = objects.find(event.handle);
                signal != objects.end()) {
                info.events.push_back(
                    {event.name, event.handle, signal->second});
            }
        }
        for (const auto& channel : instance.primitive_channels) {
            info.primitive_channels.push_back(
                {channel.name, channel.handle});
        }
        for (const auto& signal : instance.internal_signals) {
            if (const auto runtime_signal =
                    objects.find(signal.handle);
                runtime_signal != objects.end()) {
                info.internal_signals.push_back(
                    {signal.name,
                     signal.handle,
                     runtime_signal->second});
            }
        }
        for (const auto& export_object : instance.exports) {
            if (const auto runtime_signal =
                    objects.find(export_object.handle);
                runtime_signal != objects.end()) {
                info.exports.push_back(
                    {export_object.name,
                     export_object.handle,
                     runtime_signal->second});
            }
        }
        design_.systemc_instances_.push_back(std::move(info));

        for (const auto& external : instance.processes) {
#if !defined(FSIM_HAS_BOOST_CONTEXT)
            if (external.kind != FSIM_SC_METHOD) {
                report(
                    "FSIM-ELAB-BIND-042",
                    "SystemC process '" + path + "." + external.name
                        + "' requires SC_THREAD/SC_CTHREAD suspension, "
                          "which is not executable yet",
                    {});
                continue;
            }
#endif
            runtime::simir::Process process;
            process.id = static_cast<ProcessId>(
                design_.processes_.size());
            if (static_cast<std::size_t>(process.id)
                != design_.processes_.size()) {
                report(
                    "FSIM-ELAB-008",
                    "the design has too many processes",
                    {});
                continue;
            }
            process.name = path + "." + external.name;
            process.initialize = external.initialize;
            for (const auto& sensitivity : external.sensitivity) {
                const auto signal = objects.find(sensitivity.object);
                if (signal == objects.end()) {
                    report(
                        "FSIM-ELAB-BIND-043",
                        "SystemC process sensitivity '" + process.name
                            + "' references an unknown registered object",
                        {});
                    continue;
                }
                auto edge = runtime::simir::EdgeKind::any;
                switch (sensitivity.edge) {
                case FSIM_SC_ANY_EDGE:
                    break;
                case FSIM_SC_POSEDGE:
                    edge = runtime::simir::EdgeKind::posedge;
                    break;
                case FSIM_SC_NEGEDGE:
                    edge = runtime::simir::EdgeKind::negedge;
                    break;
                default:
                    report(
                        "FSIM-ELAB-BIND-044",
                        "SystemC process '" + process.name
                            + "' has an invalid sensitivity edge",
                        {});
                    continue;
                }
                const auto& signal_info =
                    design_.signal_info_.at(signal->second);
                if (edge != runtime::simir::EdgeKind::any
                    && signal_info.width != 1) {
                    report(
                        "FSIM-ELAB-BIND-045",
                        "SystemC process edge sensitivity '"
                            + process.name
                            + "' requires a scalar object",
                        {});
                    continue;
                }
                process.static_sensitivity.push_back(
                    {signal->second, edge});
            }
            std::sort(
                process.static_sensitivity.begin(),
                process.static_sensitivity.end(),
                [](const runtime::simir::Sensitivity& left,
                   const runtime::simir::Sensitivity& right) {
                    return left.signal < right.signal
                        || (left.signal == right.signal
                            && left.edge < right.edge);
                });
            process.static_sensitivity.erase(
                std::unique(
                    process.static_sensitivity.begin(),
                    process.static_sensitivity.end()),
                process.static_sensitivity.end());
            process.operations.emplace_back(
                process.static_sensitivity.empty()
                    ? runtime::simir::Operation{
                          runtime::simir::Halt{}}
                    : runtime::simir::Operation{
                          runtime::simir::WaitSensitivity{}});
            design_.systemc_processes_.push_back(
                {process.id, external.handle});
            design_.processes_.push_back(std::move(process));
        }

        for (const auto& child : instance.native_children) {
            const auto prefix = path + ".";
            if (child.parent != instance.handle
                || child.path.size() <= prefix.size()
                || child.path.rfind(prefix, 0) != 0
                || child.path.find('.', prefix.size())
                    != std::string::npos) {
                report(
                    "FSIM-ELAB-BIND-047",
                    "native SystemC child under '" + path
                        + "' has inconsistent hierarchy metadata",
                    {});
                continue;
            }
            SignalMap child_aliases;
            ObjectMap child_objects = objects;
            for (const auto& port : child.ports) {
                if (port.bound_object == 0) {
                    continue;
                }
                const auto signal = objects.find(port.bound_object);
                if (signal == objects.end()) {
                    continue;
                }
                child_aliases.emplace(port.name, signal->second);
                child_objects.emplace(port.handle, signal->second);
            }
            instantiate_systemc(
                child,
                child.path,
                std::move(child_aliases),
                std::move(child_objects),
                true);
        }

        for (const auto& child : instance.foreign_children) {
            const auto child_path = path + "." + child.name;
            const auto* binding = binding_for(child_path);
            if (binding == nullptr) {
                report(
                    "FSIM-ELAB-BIND-040",
                    "SystemC foreign child '" + child_path
                        + "' requires an explicit VHDL or "
                          "Verilog/SystemVerilog binding",
                    {});
                continue;
            }
            const auto target = parse_target(binding->target);
            if (!target || target->language == "systemc") {
                report(
                    "FSIM-ELAB-BIND-041",
                    "SystemC foreign child '" + child_path
                        + "' must bind to an HDL target",
                    {});
                continue;
            }
            if (target->language == "vhdl"
                && !target->architecture) {
                report(
                    "FSIM-ELAB-BIND-016",
                    "an explicit VHDL binding target must name an "
                    "architecture, for example "
                    "vhdl:work.entity(rtl)",
                    {});
                continue;
            }
            const auto* selected =
                choose_bound_unit(parsed_, *target);
            if (selected == nullptr) {
                report(
                    "FSIM-ELAB-BIND-015",
                    "binding target '" + binding->target
                        + "' was not found",
                    {});
                continue;
            }
            std::vector<frontend::ParameterOverride> actuals;
            actuals.reserve(child.construction_actuals.size());
            for (const auto& [name, value] :
                 child.construction_actuals) {
                frontend::Expression expression;
                expression.kind =
                    frontend::ExpressionKind::IntegerLiteral;
                expression.text = std::to_string(value);
                actuals.push_back({
                    name,
                    std::move(expression),
                    {},
                });
            }
            auto specialized =
                specialize_selected_unit(
                    *selected,
                    actuals,
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    selected->language);
            auto child_aliases = connect_foreign_child(
                child, specialized.unit, child_path, objects);
            instantiate(
                specialized.unit,
                child_path,
                std::move(child_aliases),
                {},
                {},
                {},
                {},
                std::move(specialized.environment),
                std::move(specialized.values),
                std::move(specialized.identity_values),
                std::move(specialized.packages));
        }
        stack_.pop_back();
    }



    void HierarchyBuilder::instantiate(
        const DesignUnit& unit,
        const std::string& path,
        SignalMap aliases,
        StringMap string_aliases,
        ContainerMap container_aliases,
        std::unordered_set<SignalId> read_only_signals,
        std::unordered_set<StringObjectId> read_only_strings,
        ConstantEnvironment parameter_environment,
        std::vector<std::pair<std::string, std::string>>
            parameter_values,
        std::vector<std::pair<std::string, std::string>>
            parameter_identity_values,
        PackageEnvironment package_environment) {
        const auto parent_types =
            unit.language == frontend::Language::Vhdl2008
                ? local_vhdl_type_environment(unit)
            : unit.language
                    == frontend::Language::SystemVerilog2017
                ? local_systemverilog_type_environment(unit)
                : NamedTypeEnvironment{};
        ConstantDomainEnvironment parent_domains;
        for (const auto& parameter : unit.parameters) {
            if (parameter.kind
                    != frontend::ParameterKind::Value
                || parameter_environment.find(parameter.name)
                    == parameter_environment.end()) {
                continue;
            }
            parent_domains.insert_or_assign(
                parameter.name,
                ConstantTypeInfo{
                    parameter.type.domain,
                    unit.language
                            == frontend::Language::Vhdl2008
                        && !parameter.type
                                .enumeration_literals.empty(),
                    parameter.type.nominal_type});
        }
        if (!instance_paths_.insert(path).second) {
            report(
                "FSIM-ELAB-HIER-001",
                "duplicate instance path '" + path + "'",
                unit.span);
            return;
        }
        const auto identity = unit_identity(unit);
        if (std::find(stack_.begin(), stack_.end(), identity) != stack_.end()) {
            report(
                "FSIM-ELAB-HIER-002",
                "recursive instantiation of '" + identity + "' at '" + path
                    + "'",
                unit.span);
            return;
        }
        stack_.push_back(identity);
        register_vhdl_resolution_functions(unit);

        SignalMap local = std::move(aliases);
        StringMap local_string_objects =
            std::move(string_aliases);
        ContainerMap local_container_objects =
            std::move(container_aliases);
        std::unordered_set<std::string>
            read_only_container_objects;
        std::unordered_map<
            std::string, const frontend::Type*> visible_types;
        std::unordered_map<
            std::string, const frontend::Type*> visible_type_marks;
        const auto expose_type_mark =
            [&](const std::string_view name,
                const frontend::Type& type) {
              if (unit.language
                      == frontend::Language::SystemVerilog2017
                  || unit.language
                      == frontend::Language::Vhdl2008) {
                  visible_type_marks.try_emplace(
                      std::string{name}, &type);
              }
            };
        for (const auto& alias : unit.type_aliases) {
            expose_type_mark(alias.name, alias.type);
        }
        for (const auto& parameter : unit.parameters) {
            if (unit.language
                    == frontend::Language::SystemVerilog2017
                && parameter.kind
                    == frontend::ParameterKind::Type) {
                expose_type_mark(
                    parameter.name, parameter.type);
            } else if (unit.language
                       != frontend::Language::SystemVerilog2017) {
                expose_type_mark(
                    parameter.type.spelling, parameter.type);
            }
        }
        const auto* ports = unit_ports(parsed_, unit);
        if (ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + unit.name + "' has no matching entity",
                unit.span);
            stack_.pop_back();
            return;
        }
        for (const auto& port : *ports) {
          expose_type_mark(port.type.spelling, port.type);
          visible_types.emplace(port.name, &port.type);
          visible_types.emplace(
              path + "." + port.name, &port.type);
          if (!port.interface_type.empty()
              || port.type.spelling == "interface") {
            continue;
          }
            if (port.type.domain
                == frontend::ValueDomain::String) {
                const auto object =
                    add_owned_string_port(
                        port, path, local_string_objects);
                if (object
                    && port.direction
                        == frontend::PortDirection::Input) {
                    read_only_strings.insert(*object);
                }
                continue;
            }
            if (port.type.systemverilog_container) {
                if (!local_container_objects.contains(
                        port.name)) {
                    (void)add_owned_container_port(
                        port,
                        path,
                        local_container_objects,
                        parameter_environment);
                }
                if (port.direction
                    == frontend::PortDirection::Input) {
                    read_only_container_objects.insert(
                        port.name);
                    read_only_container_objects.insert(
                        path + "." + port.name);
                }
                continue;
            }
            if (!local.contains(port.name)) {
                (void)add_owned_signal(port, path, local);
            }
        }
        for (const auto& signal : unit.signals) {
            expose_type_mark(signal.type.spelling, signal.type);
            visible_types.emplace(signal.name, &signal.type);
            visible_types.emplace(
                path + "." + signal.name, &signal.type);
            (void)add_owned_signal(signal, path, local);
        }
        for (const auto& alias : unit.signal_aliases) {
            expose_type_mark(alias.type.spelling, alias.type);
            visible_types.emplace(alias.name, &alias.type);
            visible_types.emplace(path + "." + alias.name, &alias.type);
            const auto actual = local.find(alias.actual);
            if (actual == local.end()) {
                report(
                    "FSIM-ELAB-VHBLOCK-003",
                    "unknown signal target '" + alias.actual
                        + "' for object alias '" + alias.name + "'",
                    alias.span);
                continue;
            }
            const auto& info = design_.signal_info_.at(actual->second);
            const auto width = alias.type.width();
            const auto same_packed_range = [&]() {
              if (alias.type.packed_range.has_value()
                  != info.packed_range.has_value()) {
                  return false;
              }
              if (!alias.type.packed_range) {
                  return true;
              }
              return alias.type.packed_range->left
                         == info.packed_range->left
                  && alias.type.packed_range->right
                         == info.packed_range->right
                  && alias.type.packed_range->descending
                         == info.packed_range->descending;
            };
            if (!width || *width != info.width
                || alias.type.domain != info.source_domain
                || alias.type.is_signed != info.is_signed
                || !same_packed_range()
                || (!alias.type.nominal_type.empty()
                    && alias.type.nominal_type
                        != info.nominal_type)) {
                report(
                    "FSIM-ELAB-VHBLOCK-003",
                    "object alias '" + alias.name
                        + "' does not match signal target '"
                        + alias.actual + "' (alias width="
                        + (width ? std::to_string(*width) : "unknown")
                        + ", signal width="
                        + std::to_string(info.width)
                        + ", alias nominal='" + alias.type.nominal_type
                        + "', signal nominal='" + info.nominal_type
                        + "')",
                    alias.span);
                continue;
            }
            const frontend::SignalDeclaration formal{
                alias.name,
                alias.type,
                alias.direction,
                true,
                alias.span};
            const auto diagnostics_before = diagnostics_.size();
            validate_boundary_type(
                formal, info, path, alias.span, false);
            if (diagnostics_.size() != diagnostics_before) {
                continue;
            }
            local.emplace(alias.name, actual->second);
            if (!path.empty()) {
                local.emplace(
                    path + "." + alias.name, actual->second);
            }
            design_.signal_by_name_.emplace(
                alias.name, actual->second);
            if (!path.empty()) {
                design_.signal_by_name_.emplace(
                    path + "." + alias.name,
                    actual->second);
            }
        }
        SystemVerilogStringEnvironment string_values;
        for (const auto& variable : unit.variables) {
            visible_types.emplace(
                variable.name, &variable.type);
            visible_types.emplace(
                path + "." + variable.name, &variable.type);
            if (variable.type.systemverilog_container) {
                const auto width = variable.type.width();
                if (!width || *width == 0 || *width > 64
                    || variable.type.packed_aggregate
                        == frontend::PackedAggregateKind::UnpackedStruct
                    || variable.type.domain
                        == frontend::ValueDomain::String) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-003",
                        "container elements must be bounded integral, enum, "
                        "or packed aggregate values with an executable "
                        "width in 1..64",
                        variable.span);
                    continue;
                }
                ContainerType type;
                type.element_width =
                    static_cast<std::uint32_t>(*width);
                type.element_nominal_type =
                    variable.type.nominal_type;
                type.two_state =
                    is_two_state_domain(variable.type.domain);
                type.signed_elements = variable.type.is_signed;
                type.queue =
                    variable.type.systemverilog_container->kind
                    == frontend::SystemVerilogContainerKind::Queue;
                type.associative =
                    variable.type.systemverilog_container->kind
                    == frontend::SystemVerilogContainerKind::
                        AssociativeArray;
                type.fixed =
                    variable.type.systemverilog_container->kind
                    == frontend::SystemVerilogContainerKind::
                        StaticArray;
                if (type.associative) {
                    const auto& index_type =
                        variable.type.systemverilog_container
                            ->associative_index_type;
                    const auto index_width =
                        index_type ? index_type->width()
                                   : std::nullopt;
                    if (!index_type || !index_width
                        || *index_width == 0
                        || *index_width > 64
                        || index_type->domain
                            == frontend::ValueDomain::String
                        || index_type->domain
                            == frontend::ValueDomain::Unknown
                        || !index_type->packed_members.empty()
                        || index_type->vhdl_array) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-013",
                            "associative-array indices require a resolved "
                            "integral scalar type with width in 1..64",
                            variable.type.systemverilog_container->span);
                        continue;
                    }
                    type.index_width =
                        static_cast<std::uint32_t>(*index_width);
                    type.two_state_indices =
                        is_two_state_domain(index_type->domain);
                    type.signed_indices = index_type->is_signed;
                }
                if (variable.type.systemverilog_container
                        ->queue_maximum) {
                    std::string error;
                    const auto maximum =
                        evaluate_systemverilog_constant_expression(
                            *variable.type.systemverilog_container
                                 ->queue_maximum,
                            {},
                            parameter_environment,
                            error);
                    const auto maximum_index =
                        maximum
                            ? maximum->integer_value()
                            : std::nullopt;
                    if (!maximum_index
                        || *maximum_index < 0
                        || *maximum_index
                            >= static_cast<std::int64_t>(
                                maximum_container_elements)) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-004",
                            "bounded queue maximum index must specialize "
                            "to a known value in 0..4095",
                            variable.type.systemverilog_container
                                ->queue_maximum->span);
                        continue;
                    }
                    type.maximum_elements =
                        static_cast<std::uint32_t>(
                            *maximum_index + 1);
                }
                if (type.fixed) {
                    const auto& ranges =
                        variable.type.systemverilog_container
                            ->static_range_expressions;
                    const auto in_int32 =
                        [](const std::int64_t value) {
                          return value
                                  >= std::numeric_limits<
                                      std::int32_t>::min()
                              && value
                                  <= std::numeric_limits<
                                      std::int32_t>::max();
                        };
                    std::uint64_t total = 1;
                    bool valid_dimensions = !ranges.empty();
                    for (const auto& range : ranges) {
                        std::string left_error;
                        std::string right_error;
                        const auto left_value =
                            evaluate_systemverilog_constant_expression(
                                range.left, {}, parameter_environment,
                                left_error);
                        const auto right_value =
                            evaluate_systemverilog_constant_expression(
                                range.right, {}, parameter_environment,
                                right_error);
                        std::int64_t left{};
                        std::int64_t right{};
                        bool has_left{};
                        bool has_right{};
                        if (left_value) {
                            if (const auto converted =
                                    left_value->integer_value()) {
                                left = *converted;
                                has_left = true;
                            }
                        }
                        if (right_value) {
                            if (const auto converted =
                                    right_value->integer_value()) {
                                right = *converted;
                                has_right = true;
                            }
                        }
                        if (!has_left || !has_right || !in_int32(left)
                            || !in_int32(right)) {
                            valid_dimensions = false;
                            break;
                        }
                        const auto count = static_cast<std::uint64_t>(
                            left >= right
                                ? left - right
                                : right - left) + 1U;
                        if (count > maximum_container_elements
                            || total
                                > maximum_container_elements / count) {
                            valid_dimensions = false;
                            break;
                        }
                        total *= count;
                        type.dimensions.push_back(ContainerDimension{
                            static_cast<std::int32_t>(left),
                            static_cast<std::int32_t>(right)});
                    }
                    if (!valid_dimensions) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-020",
                            "static unpacked-array dimensions must "
                            "specialize to 32-bit ranges spanning at most "
                            "4096 total elements",
                            variable.type.systemverilog_container->span);
                        continue;
                    }
                    type.index_left = type.dimensions.front().first;
                    type.index_right = type.dimensions.front().second;
                }
                if (variable.initializer) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-012",
                        "module container declaration initializers are not "
                        "executable; use an initial block",
                        variable.initializer->span);
                    continue;
                }
                const auto index =
                    design_.container_objects_.size();
                const auto id =
                    static_cast<ContainerObjectId>(index);
                if (static_cast<std::size_t>(id) != index) {
                    throw std::length_error{
                        "too many elaborated container objects"};
                }
                const auto full_name =
                    path + "." + variable.name;
                design_.container_object_info_.push_back(
                    ContainerObjectInfo{
                        id,
                        full_name,
                        type,
                        variable.span,
                        false,
                        frontend::PortDirection::Unknown,
                        std::nullopt});
                design_.container_objects_.push_back(
                    ContainerObject{
                        full_name,
                        default_container_value(type),
                        std::nullopt});
                local_container_objects.emplace(
                    variable.name, id);
                local_container_objects.emplace(
                    full_name, id);
                design_.container_by_name_.emplace(
                    full_name, id);
                if (path == design_.top_) {
                    design_.container_by_name_.emplace(
                        variable.name, id);
                }
                continue;
            }
            if (variable.type.domain
                == frontend::ValueDomain::Integer) {
                const frontend::SignalDeclaration declaration{
                    variable.name,
                    variable.type,
                    frontend::PortDirection::Unknown,
                    false,
                    variable.span};
                const auto signal =
                    add_owned_signal(declaration, path, local);
                if (signal && variable.initializer) {
                    std::string error;
                    const auto value =
                        evaluate_systemverilog_constant_expression(
                            *variable.initializer,
                            {},
                            parameter_environment,
                            error);
                    const auto converted =
                        value
                            ? convert_systemverilog_parameter_value(
                                  *value, variable.type, error)
                            : std::nullopt;
                    if (!converted || !converted->known()) {
                        report(
                            "FSIM-ELAB-SVFILE-008",
                            "module integer initializer for '"
                                + path + "." + variable.name
                                + "' is not a known 32-bit constant: "
                                + error,
                            variable.span);
                    } else {
                        design_.signals_[*signal].initial_value =
                            PackedLogic4::from_aval_bval(
                                converted->width,
                                converted->bits,
                                0);
                    }
                }
                continue;
            }
            if (variable.type.domain
                != frontend::ValueDomain::String) {
                report(
                    "FSIM-ELAB-SVSTRING-016",
                    "module variable '" + path + "."
                        + variable.name
                        + "' is not a supported string object",
                    variable.span);
                continue;
            }
            SystemVerilogStringValue initial;
            initial.source = variable.span;
            if (variable.initializer) {
                std::string error;
                const auto value =
                    evaluate_systemverilog_string_expression(
                        *variable.initializer,
                        string_values,
                        {},
                        error);
                if (!value) {
                    report(
                        "FSIM-ELAB-SVSTRING-017",
                        "cannot evaluate module string initializer for '"
                            + path + "." + variable.name
                            + "': " + error,
                        variable.span);
                    continue;
                }
                initial = *value;
            }
            if (initial.bytes.size()
                > maximum_string_bytes) {
                report(
                    "FSIM-ELAB-SVSTRING-007",
                    "module string initializer exceeds the 4096-byte "
                    "limit",
                    variable.span);
                continue;
            }
            const auto index =
                design_.string_objects_.size();
            const auto id =
                static_cast<StringObjectId>(index);
            if (static_cast<std::size_t>(id) != index) {
                throw std::length_error{
                    "too many elaborated string objects"};
            }
            const auto full_name =
                path + "." + variable.name;
            design_.string_object_info_.push_back(
                StringObjectInfo{
                    id,
                    full_name,
                    variable.span,
                    false,
                    frontend::PortDirection::Unknown});
            design_.string_objects_.push_back(
                StringObject{full_name, initial.bytes});
            local_string_objects.emplace(
                variable.name, id);
            local_string_objects.emplace(
                full_name, id);
            design_.string_by_name_.emplace(full_name, id);
            if (path == design_.top_) {
                design_.string_by_name_.emplace(
                    variable.name, id);
            }
            string_values.emplace(
                variable.name, std::move(initial));
        }

        const auto specialization_index = design_.specializations_.size();
        const auto specialization_id =
            static_cast<SpecializationId>(specialization_index);
        if (static_cast<std::size_t>(specialization_id)
            != specialization_index) {
            throw std::length_error(
                "too many elaborated design-unit specializations");
        }
        SpecializationInfo specialization;
        specialization.id = specialization_id;
        specialization.unit = identity;
        specialization.instance = path;
        specialization.source =
            std::string{frontend::physical_source(unit.span)};
        if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
            if (const auto* entity = find_vhdl_entity(parsed_, unit);
                entity != nullptr
                && frontend::physical_source(entity->span)
                    != specialization.source) {
                specialization.source_dependencies.push_back(
                    std::string{
                        frontend::physical_source(entity->span)});
            }
        }
        for (const auto& dependency : unit.source_dependencies) {
            if (dependency != specialization.source
                && std::find(
                       specialization.source_dependencies.begin(),
                       specialization.source_dependencies.end(),
                       dependency)
                    == specialization.source_dependencies.end()) {
                specialization.source_dependencies.push_back(
                    dependency);
            }
        }
        specialization.language = unit.language;
        specialization.library =
            unit.library.empty() ? "work" : unit.library;
        specialization.is_cell = unit.is_cell;
        specialization.parameter_values = std::move(parameter_values);
        specialization.parameter_identity_values =
            std::move(parameter_identity_values);

        Lowerer lowerer{
            design_,
            local,
            read_only_signals,
            local_string_objects,
            read_only_strings,
            local_container_objects,
            read_only_container_objects,
            visible_types,
            visible_type_marks,
            unit.functions,
            unit.tasks,
            unit.procedures,
            diagnostics_};
        for (std::size_t index = 0;
             index < unit.concurrent_statements.size(); ++index) {
            auto process = lowerer.lower_concurrent(
                unit.concurrent_statements[index],
                unit.language,
                path,
                index);
            specialization.processes.push_back(process.id);
            design_.processes_.push_back(std::move(process));
        }
        for (const auto& process : unit.processes) {
            auto lowered =
                lowerer.lower_process(process, unit.language, path);
            specialization.processes.push_back(lowered.id);
            design_.processes_.push_back(std::move(lowered));
        }
        design_.specializations_.push_back(std::move(specialization));

        validate_vhdl_component_configurations(unit, path);
        for (const auto& instance : unit.instances) {
            const auto child_path = path + "." + instance.name;
            const auto* binding = binding_for(child_path);
            if (binding != nullptr) {
                const auto target = parse_target(binding->target);
                if (target
                    && target->language == "systemc") {
                    const auto* description =
                        construct_systemc_description(
                            instance,
                            child_path,
                            binding->target,
                            parameter_environment,
                            unit.language);
                    if (description == nullptr) {
                        continue;
                    }
                    auto [child_aliases, child_objects] =
                        connect_systemc_instance(
                            instance,
                            *description,
                            child_path,
                            local,
                            binding);
                    instantiate_systemc(
                        *description,
                        child_path,
                        std::move(child_aliases),
                        std::move(child_objects));
                    continue;
                }
            }
            ConfiguredVhdlInstance configured;
            const frontend::Instance* selected_instance =
                &instance;
            const DesignUnit* target = nullptr;
            if (binding == nullptr) {
                configured = instance.vhdl_configuration_instance
                    ? bind_vhdl_direct_configuration_instance(
                        unit, instance, child_path)
                    : bind_vhdl_component_instance(
                        unit,
                        instance,
                        child_path,
                        parameter_environment,
                        parent_domains,
                        parent_types,
                        unit.functions,
                        unit.procedures,
                        package_environment);
                if (!configured.valid) {
                    continue;
                }
                if (configured.applied) {
                    selected_instance = &configured.instance;
                    target = configured.target
                        ? &*configured.target
                        : nullptr;
                    if (target == nullptr) {
                        continue;
                    }
                }
            }
            if (target == nullptr) {
                target = bound_target(
                    *selected_instance,
                    unit,
                    child_path,
                    binding);
            }
            if (target == nullptr) {
                continue;
            }
            auto child_specialized = specialize_selected_unit(
                *target,
                selected_instance->parameter_overrides,
                parameter_environment,
                parent_domains,
                parent_types,
                unit.functions,
                unit.procedures,
                package_environment,
                unit.language);
            if (!configured.component_identity.empty()) {
                if (child_specialized.identity_values.empty()) {
                    child_specialized.identity_values =
                        child_specialized.values;
                }
                child_specialized.values.emplace_back(
                    "__component",
                    configured.component_name);
                child_specialized.identity_values.emplace_back(
                    "__component",
                    configured.component_identity);
            }
            if (!configured.configuration_identity.empty()
                && configured.component_identity.empty()) {
                child_specialized.identity_values.emplace_back(
                    "__configuration",
                    configured.configuration_identity);
            }
            auto child_aliases = connect_instance(
                *selected_instance,
                child_specialized.unit,
                child_path,
                local,
                local_string_objects,
                read_only_strings,
                local_container_objects,
                read_only_container_objects,
                binding,
                target->language != unit.language);
            for (auto& driver : child_aliases.vhdl_input_drivers) {
                child_specialized.unit.concurrent_statements.push_back(
                    std::move(driver));
            }
            if (configured.referenced_configuration != nullptr) {
                vhdl_configurations_by_path_[child_path] =
                    configured.referenced_configuration;
            }
            instantiate(
                child_specialized.unit,
                child_path,
                std::move(child_aliases.signals),
                std::move(child_aliases.strings),
                std::move(child_aliases.containers),
                std::move(child_aliases.read_only_signals),
                std::move(child_aliases.read_only_strings),
                std::move(child_specialized.environment),
                std::move(child_specialized.values),
                std::move(child_specialized.identity_values),
                std::move(child_specialized.packages));
            if (target->kind
                == frontend::UnitKind::SystemVerilogInterface) {
                systemverilog_interface_instances_.insert_or_assign(
                    child_path, child_specialized.unit);
            }
        }
        stack_.pop_back();
    }



    std::pair<HierarchyBuilder::SignalMap, HierarchyBuilder::ObjectMap>
    HierarchyBuilder::connect_systemc_instance(
        const frontend::Instance& instance,
        const SystemCInstanceDescription& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding) {
        std::vector<frontend::SignalDeclaration> ports;
        ports.reserve(target.ports.size());
        for (const auto& port : target.ports) {
            ports.push_back(external_port_declaration(port));
        }
        auto aliases = connect_ports(
            instance, ports, path, parent_signals, {}, {}, {}, {},
            binding, true);
        ObjectMap objects;
        for (const auto& port : target.ports) {
            if (const auto signal = aliases.signals.find(port.name);
                signal != aliases.signals.end()) {
                objects.emplace(port.handle, signal->second);
            }
        }
        return {std::move(aliases.signals), std::move(objects)};
    }

    HierarchyBuilder::SignalMap HierarchyBuilder::connect_foreign_child(
        const ForeignChild& child,
        const DesignUnit& target,
        const std::string& path,
        const ObjectMap& objects) {
        SignalMap aliases;
        const auto* target_ports = unit_ports(parsed_, target);
        if (target_ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + target.name
                    + "' has no matching entity",
                target.span);
            return aliases;
        }
        std::unordered_set<std::string> connected;
        for (const auto& foreign_port : child.ports) {
            const auto formal = std::find_if(
                target_ports->begin(), target_ports->end(),
                [&](const frontend::SignalDeclaration& port) {
                    return port.name == foreign_port.name;
                });
            if (formal == target_ports->end()) {
                report(
                    "FSIM-ELAB-BIND-034",
                    "foreign child '" + path
                        + "' declares unknown target port '"
                        + foreign_port.name + "'",
                    {});
                continue;
            }
            if (!connected.insert(foreign_port.name).second) {
                report(
                    "FSIM-ELAB-BIND-035",
                    "foreign child port '" + path + "."
                        + foreign_port.name
                        + "' is connected more than once",
                    {});
                continue;
            }
            const auto actual = objects.find(foreign_port.object);
            if (actual == objects.end()) {
                report(
                    "FSIM-ELAB-BIND-036",
                    "foreign child port '" + path + "."
                        + foreign_port.name
                        + "' references an unknown SystemC object",
                    {});
                continue;
            }
            const auto placeholder = foreign_port_declaration(foreign_port);
            const auto& actual_info = design_.signal_info_.at(actual->second);
            validate_boundary_type(placeholder, actual_info, path, {}, true);
            validate_boundary_type(*formal, actual_info, path, {}, true);
            if (placeholder.direction != formal->direction) {
                report(
                    "FSIM-ELAB-BIND-037",
                    "foreign child port direction mismatch on '"
                        + path + "." + foreign_port.name + "'",
                    {});
            }
            aliases.emplace(formal->name, actual->second);
            aliases.emplace(path + "." + formal->name, actual->second);
            design_.signal_by_name_.emplace(
                path + "." + formal->name, actual->second);
            // This foreign child is an implementation detail of the SystemC
            // parent, whose boundary driver is already recorded.
        }
        return aliases;
    }

    void HierarchyBuilder::report(
        std::string code,
        std::string message,
        frontend::SourceSpan source) {
        diagnostics_.push_back(
            {std::move(code), std::move(message), std::move(source)});
    }

} // namespace fsim::elaboration

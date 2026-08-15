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
        constructed->construction_identity_values.clear();
        constructed->construction_identity_values.reserve(values->size());
        for (std::size_t index = 0; index < values->size(); ++index) {
            constructed->construction_identity_values.emplace_back(
                values->at(index).first,
                "systemcconst-v1:type="
                    + std::to_string(
                        static_cast<unsigned>(schema->at(index).type))
                    + ";value="
                    + std::to_string(values->at(index).second));
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

        const auto parent_separator = path.rfind('.');
        design_.systemc_objects_.push_back({
            SystemCNamedObjectKind::module,
            instance.handle,
            path,
            path == active_root_ || parent_separator == std::string::npos
                ? std::string{}
                : path.substr(0, parent_separator),
            instance.target,
            std::nullopt,
            std::nullopt,
            {}});

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
                if (design_.roots_.size() == 1 && path == active_root_) {
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
            if (design_.roots_.size() == 1 && path == active_root_) {
                design_.signal_by_name_.emplace(
                    export_object.name, *signal);
            }
        }

        const auto append_value_object =
            [&](const SystemCNamedObjectKind kind,
                const std::uint64_t native_handle,
                const std::string_view local_name,
                const std::string_view type_name,
                const SignalId signal) {
              design_.systemc_objects_.push_back({
                  kind,
                  native_handle,
                  path + "." + std::string{local_name},
                  path,
                  std::string{type_name},
                  signal,
                  std::nullopt,
                  {}});
            };

        SystemCInstanceInfo info;
        info.id = static_cast<std::uint32_t>(
            design_.systemc_instances_.size());
        info.target = instance.target;
        info.instance = path;
        info.native_handle = instance.handle;
        info.construction_values =
            instance.construction_values;
        info.construction_identity_values =
            instance.construction_identity_values;
        for (const auto& port : instance.ports) {
            if (const auto signal = aliases.find(port.name);
                signal != aliases.end()) {
                info.ports.push_back(
                    {port.name, port.handle, signal->second});
                std::string_view type_name = "sc_port";
                switch (port.direction) {
                case frontend::PortDirection::Input:
                    type_name = "sc_in";
                    break;
                case frontend::PortDirection::Output:
                    type_name = "sc_out";
                    break;
                case frontend::PortDirection::Inout:
                    type_name = "sc_inout";
                    break;
                default:
                    break;
                }
                append_value_object(
                    SystemCNamedObjectKind::port,
                    port.handle,
                    port.name,
                    type_name,
                    signal->second);
            }
        }
        for (const auto& signal : instance.internal_signals) {
            if (const auto runtime_signal =
                    objects.find(signal.handle);
                runtime_signal != objects.end()) {
                info.internal_signals.push_back(
                    {signal.name,
                     signal.handle,
                     runtime_signal->second});
                append_value_object(
                    SystemCNamedObjectKind::signal,
                    signal.handle,
                    signal.name,
                    "sc_signal",
                    runtime_signal->second);
            }
        }
        for (const auto& export_object : instance.exports) {
            if (const auto runtime_signal =
                    objects.find(export_object.handle);
                runtime_signal != objects.end()) {
                info.exports.push_back(
                    {export_object.name,
                     export_object.handle,
                     runtime_signal->second,
                     export_object.writable});
                append_value_object(
                    SystemCNamedObjectKind::export_object,
                    export_object.handle,
                    export_object.name,
                    "sc_export",
                    runtime_signal->second);
            }
        }
        design_.systemc_instances_.push_back(std::move(info));

        for (const auto& external : instance.processes) {
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
            process.initialize = true;
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
            if (process.static_sensitivity.empty()) {
                process.operations.emplace_back(
                    runtime::simir::Halt{});
            } else {
                process.operations.emplace_back(
                    runtime::simir::WaitSensitivity{});
            }
            design_.systemc_processes_.push_back(
                {process.id, external.handle});
            design_.systemc_objects_.push_back({
                SystemCNamedObjectKind::process,
                external.handle,
                process.name,
                path,
                "sc_method_process",
                std::nullopt,
                process.id,
                {}});
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
                    report(
                        "FSIM-ELAB-BIND-058",
                        "native SystemC child port '" + child.path + "."
                            + port.name + "' is unbound",
                        {});
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

    void HierarchyBuilder::report(
        std::string code,
        std::string message,
        frontend::SourceSpan source) {
        diagnostics_.push_back(
            {std::move(code), std::move(message), std::move(source)});
    }

} // namespace fsim::elaboration

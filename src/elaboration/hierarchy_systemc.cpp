// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <cctype>


namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

bool valid_systemc_construction_value(
    const fsim_sc_construction_type_v1 type,
    const std::int64_t value)
{
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

bool systemc_construction_name_equal(
    const std::string_view left,
    const std::string_view right,
    const frontend::Language language)
{
    if (language != frontend::Language::Vhdl2008) {
        return left == right;
    }
    return left.size() == right.size()
        && std::ranges::equal(left, right, [](const char lhs,
                                             const char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs))
                   == std::tolower(static_cast<unsigned char>(rhs));
           });
}

frontend::SourceSpan compiled_systemc_source_span(
    const semantic::CompiledDesign& compiled,
    const semantic::SourceSpanId source)
{
    frontend::SourceSpan result;
    const auto& spans = compiled.semantics.source_spans();
    if (!source.valid() || source.value() >= spans.size()) {
        return result;
    }
    const auto& span = spans[source.value()];
    result.source_name = span.logical_name;
    result.begin = {
        static_cast<std::size_t>(span.begin.offset),
        span.begin.line,
        span.begin.column,
    };
    result.end = {
        static_cast<std::size_t>(span.end.offset),
        span.end.line,
        span.end.column,
    };
    const auto& files = compiled.semantics.source_files();
    if (span.file.valid() && span.file.value() < files.size()) {
        result.physical_source_name = files[span.file.value()].physical_name;
    }
    return result;
}

} // namespace




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

    const SystemCInstanceDescription*
    HierarchyBuilder::construct_systemc_description(
        const semantic::sv::Instance& instance,
        const semantic::SpecializedHirUnit& parent,
        const std::string& path,
        const std::string_view target)
    {
        const auto instance_source = compiled_ != nullptr
            ? compiled_systemc_source_span(*compiled_, instance.source)
            : frontend::SourceSpan { };
        if (systemc_provider_ == nullptr) {
            if (!instance.parameters.empty()) {
                report(
                    "FSIM-ELAB-PARAM-001",
                    "HDL parameter actuals require a live SystemC "
                    "factory schema provider",
                    instance_source);
                return nullptr;
            }
            return systemc_description(path, target, instance_source);
        }

        std::string error;
        const auto schema = systemc_provider_->schema(target, error);
        if (!schema) {
            report(
                "FSIM-ELAB-SC-PARAM-007",
                "cannot inspect SystemC construction schema for '"
                    + std::string { target } + "': " + error,
                instance_source);
            return nullptr;
        }

        std::vector<std::optional<std::int64_t>> actuals(schema->size());
        std::size_t next_positional { };
        bool saw_named { };
        bool saw_positional { };
        const auto diagnostics_before = diagnostics_.size();
        for (const auto& association : instance.parameters) {
            const auto source = compiled_ != nullptr
                ? compiled_systemc_source_span(
                    *compiled_, association.source)
                : instance_source;
            if (association.kind
                    != semantic::sv::ActualKind::expression
                || !association.expression) {
                report(
                    "FSIM-ELAB-SC-PARAM-004",
                    "SystemC construction actual must be one integral "
                    "compiled-HIR expression",
                    source);
                continue;
            }
            const auto value = parent.evaluate_integral_expression(
                *association.expression);
            if (!value) {
                report(
                    "FSIM-ELAB-SC-PARAM-004",
                    "cannot evaluate SystemC construction actual from "
                    "compiled HIR",
                    source);
                continue;
            }
            std::optional<std::size_t> index;
            if (association.formal) {
                saw_named = true;
                const auto found = std::ranges::find(
                    *schema, *association.formal,
                    &SystemCConstructionParameter::name);
                if (found == schema->end()) {
                    report(
                        "FSIM-ELAB-SC-PARAM-001",
                        "unknown SystemC construction parameter '"
                            + *association.formal + "'",
                        source);
                    continue;
                }
                index = static_cast<std::size_t>(
                    std::distance(schema->begin(), found));
            } else {
                saw_positional = true;
                if (next_positional >= schema->size()) {
                    report(
                        "FSIM-ELAB-SC-PARAM-001",
                        "too many positional SystemC construction "
                        "actuals",
                        source);
                    continue;
                }
                index = next_positional++;
            }
            if (actuals[*index]) {
                report(
                    "FSIM-ELAB-SC-PARAM-002",
                    "duplicate SystemC construction actual for '"
                        + schema->at(*index).name + "'",
                    source);
                continue;
            }
            actuals[*index] = *value;
        }
        if (saw_named && saw_positional) {
            report(
                "FSIM-ELAB-SC-PARAM-003",
                "named and positional SystemC construction actuals "
                "cannot be mixed",
                instance_source);
        }

        std::vector<std::pair<std::string, std::int64_t>> values;
        values.reserve(schema->size());
        for (std::size_t index { }; index < schema->size(); ++index) {
            const auto value = actuals[index]
                ? actuals[index]
                : schema->at(index).default_value;
            if (!value) {
                report(
                    "FSIM-ELAB-SC-PARAM-001",
                    "SystemC construction parameter '"
                        + schema->at(index).name + "' requires an actual",
                    instance_source);
                continue;
            }
            if (!valid_systemc_construction_value(
                    schema->at(index).type, *value)) {
                report(
                    "FSIM-ELAB-SC-PARAM-005",
                    "SystemC construction parameter '"
                        + schema->at(index).name
                        + "' violates its declared scalar subtype",
                    instance_source);
                continue;
            }
            values.emplace_back(schema->at(index).name, *value);
        }
        if (diagnostics_.size() != diagnostics_before) {
            return nullptr;
        }

        auto constructed = systemc_provider_->instantiate(
            path, target, values, error);
        if (!constructed) {
            report(
                "FSIM-ELAB-SC-PARAM-008",
                "cannot construct SystemC instance '" + path + "': "
                    + error,
                instance_source);
            return nullptr;
        }
        if (constructed->path != path
            || constructed->target != target) {
            report(
                "FSIM-ELAB-SC-PARAM-008",
                "SystemC factory provider returned inconsistent instance "
                "identity for '" + path + "'",
                instance_source);
            return nullptr;
        }
        constructed->construction_identity_values.clear();
        constructed->construction_identity_values.reserve(values.size());
        for (std::size_t index { }; index < values.size(); ++index) {
            constructed->construction_identity_values.emplace_back(
                values[index].first,
                "systemcconst-v1:type="
                    + std::to_string(static_cast<unsigned>(
                        schema->at(index).type))
                    + ";value=" + std::to_string(values[index].second));
        }
        owned_systemc_instances_.push_back(std::move(*constructed));
        const auto* description = &owned_systemc_instances_.back();
        if (!systemc_instances_.emplace(path, description).second) {
            report(
                "FSIM-ELAB-BIND-032",
                "duplicate constructed SystemC instance path '" + path
                    + "'",
                instance_source);
            return nullptr;
        }
        used_systemc_instances_.insert(path);
        return description;
    }

const SystemCInstanceDescription*
HierarchyBuilder::construct_systemc_description(
    const semantic::vhdl::Instance& instance,
    const semantic::SpecializedHirUnit& parent,
    const std::string& path,
    const std::string_view target)
{
    const auto instance_source = compiled_ != nullptr
        ? compiled_systemc_source_span(*compiled_, instance.source)
        : frontend::SourceSpan { };
    if (systemc_provider_ == nullptr) {
        if (!instance.generic_map.empty()) {
            report(
                "FSIM-ELAB-GENERIC-001",
                "HDL generic actuals require a live SystemC factory "
                "schema provider",
                compiled_ != nullptr
                    ? compiled_systemc_source_span(
                          *compiled_, instance.generic_map.front().source)
                    : instance_source);
            return nullptr;
        }
        return systemc_description(path, target, instance_source);
    }

    std::string error;
    const auto schema = systemc_provider_->schema(target, error);
    if (!schema) {
        report(
            "FSIM-ELAB-SC-PARAM-007",
            "cannot inspect SystemC construction schema for '"
                + std::string { target } + "': " + error,
            instance_source);
        return nullptr;
    }

    std::vector<std::optional<std::int64_t>> actuals(schema->size());
    std::size_t next_positional { };
    bool saw_named { };
    bool saw_positional { };
    const auto diagnostics_before = diagnostics_.size();
    for (const auto& association : instance.generic_map) {
        const auto source = compiled_ != nullptr
            ? compiled_systemc_source_span(
                  *compiled_, association.source)
            : instance_source;
        if (association.kind
                != semantic::vhdl::AssociationKind::expression
            || !association.expression) {
            report(
                "FSIM-ELAB-SC-PARAM-004",
                "SystemC construction actual must be one integral "
                "compiled-HIR expression",
                source);
            continue;
        }
        const auto value = parent.evaluate_integral_expression(
            *association.expression);
        if (!value) {
            report(
                "FSIM-ELAB-SC-PARAM-004",
                "cannot evaluate SystemC construction actual from "
                "compiled HIR",
                source);
            continue;
        }
        std::optional<std::size_t> index;
        if (association.formal) {
            saw_named = true;
            const auto found = std::ranges::find_if(
                *schema, [&](const auto& parameter) {
                    return systemc_construction_name_equal(
                        parameter.name, association.formal->spelling,
                        frontend::Language::Vhdl2008);
                });
            if (found == schema->end()) {
                report(
                    "FSIM-ELAB-GENERIC-001",
                    "unknown SystemC construction parameter '"
                        + association.formal->spelling + "'",
                    source);
                continue;
            }
            index = static_cast<std::size_t>(
                std::distance(schema->begin(), found));
        } else {
            saw_positional = true;
            if (next_positional >= schema->size()) {
                report(
                    "FSIM-ELAB-GENERIC-001",
                    "too many positional SystemC construction actuals",
                    source);
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index]) {
            report(
                "FSIM-ELAB-GENERIC-002",
                "duplicate SystemC construction actual for '"
                    + schema->at(*index).name + "'",
                source);
            continue;
        }
        actuals[*index] = *value;
    }
    if (saw_named && saw_positional) {
        report(
            "FSIM-ELAB-GENERIC-003",
            "named and positional SystemC construction actuals "
            "cannot be mixed",
            instance_source);
    }

    std::vector<std::pair<std::string, std::int64_t>> values;
    values.reserve(schema->size());
    for (std::size_t index { }; index < schema->size(); ++index) {
        const auto value = actuals[index]
            ? actuals[index]
            : schema->at(index).default_value;
        if (!value) {
            report(
                "FSIM-ELAB-GENERIC-001",
                "SystemC construction parameter '"
                    + schema->at(index).name + "' requires an actual",
                instance_source);
            continue;
        }
        if (!valid_systemc_construction_value(
                schema->at(index).type, *value)) {
            report(
                "FSIM-ELAB-GENERIC-008",
                "SystemC construction parameter '"
                    + schema->at(index).name
                    + "' violates its declared scalar subtype",
                instance_source);
            continue;
        }
        values.emplace_back(schema->at(index).name, *value);
    }
    if (diagnostics_.size() != diagnostics_before) {
        return nullptr;
    }

    auto constructed = systemc_provider_->instantiate(
        path, target, values, error);
    if (!constructed) {
        report(
            "FSIM-ELAB-SC-PARAM-008",
            "cannot construct SystemC instance '" + path + "': "
                + error,
            instance_source);
        return nullptr;
    }
    if (constructed->path != path
        || constructed->target != target) {
        report(
            "FSIM-ELAB-SC-PARAM-008",
            "SystemC factory provider returned inconsistent instance "
            "identity for '" + path + "'",
            instance_source);
        return nullptr;
    }
    constructed->construction_identity_values.clear();
    constructed->construction_identity_values.reserve(values.size());
    for (std::size_t index { }; index < values.size(); ++index) {
        constructed->construction_identity_values.emplace_back(
            values[index].first,
            "systemcconst-v1:type="
                + std::to_string(static_cast<unsigned>(
                    schema->at(index).type))
                + ";value=" + std::to_string(values[index].second));
    }
    owned_systemc_instances_.push_back(std::move(*constructed));
    const auto* description = &owned_systemc_instances_.back();
    if (!systemc_instances_.emplace(path, description).second) {
        report(
            "FSIM-ELAB-BIND-032",
            "duplicate constructed SystemC instance path '" + path
                + "'",
            instance_source);
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

        const auto add_external_signal = [&](
            const std::string& name,
            const PackedTypeMetadata& type,
            const frontend::PortDirection direction,
            const bool is_port) -> std::optional<SignalId> {
            if (const auto existing = aliases.find(name);
                existing != aliases.end()) {
                return existing->second;
            }
            const auto width = type.width();
            if (type.domain == frontend::ValueDomain::Unknown
                || !width || *width == 0U
                || *width > std::numeric_limits<std::size_t>::max()) {
                report(
                    "FSIM-ELAB-TYPE-001",
                    "SystemC object '" + path + "." + name
                        + "' has no executable packed layout",
                    {});
                return std::nullopt;
            }
            if (design_.signals_.size()
                > std::numeric_limits<SignalId>::max()) {
                report(
                    "FSIM-ELAB-011",
                    "the design has too many signals for dense 32-bit IDs",
                    {});
                return std::nullopt;
            }

            const auto id = static_cast<SignalId>(design_.signals_.size());
            const auto full_name = path + "." + name;
            aliases.emplace(name, id);
            aliases.emplace(full_name, id);
            design_.signal_by_name_.emplace(full_name, id);
            if (design_.roots_.size() == 1U && path == active_root_) {
                design_.signal_by_name_.emplace(name, id);
            }

            SignalInfo info;
            info.id = id;
            info.name = full_name;
            info.width = static_cast<std::size_t>(*width);
            info.type_name = type.spelling;
            info.source_domain = type.domain;
            info.systemverilog_scalar = type.systemverilog_scalar;
            info.systemverilog_net_type = type.systemverilog_net_type;
            info.is_signed = type.is_signed;
            info.packed_range = type.packed_range;
            info.vhdl_array = type.vhdl_array;
            info.vhdl_access = type.vhdl_access;
            info.vhdl_physical = type.vhdl_physical;
            info.packed_members = type.packed_members;
            info.integer_range = type.integer_range;
            info.nominal_type = type.nominal_type;
            info.enumeration_literals = type.enumeration_literals;
            info.enumeration_range = type.enumeration_range;
            info.is_port = is_port;
            info.direction = direction;
            design_.signal_info_.push_back(std::move(info));

            auto initial = is_two_state_domain(type.domain)
                ? Logic4::zero
                : Logic4::x;
            if (!type.systemverilog_net_type.empty()) {
                initial = Logic4::z;
            }
            design_.signals_.push_back({
                full_name,
                PackedLogic4 { static_cast<std::size_t>(*width), initial },
                ResolutionKind::none,
                value_kind(type.domain),
                std::nullopt,
                { StrengthRank::pull, StrengthRank::pull },
                std::nullopt,
                std::nullopt,
                type.systemverilog_scalar,
            });
            return id;
        };

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
                const auto signal = add_external_signal(
                    port.name, port.type, port.direction, true);
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
            const auto runtime_signal = add_external_signal(
                signal.name, signal.type,
                frontend::PortDirection::Unknown, false);
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
                const auto formal_width = port.type.width();
                if (!formal_width
                    || *formal_width
                        > std::numeric_limits<std::size_t>::max()
                    || signal->second >= design_.signal_info_.size()) {
                    report(
                        "FSIM-ELAB-TYPE-001",
                        "native SystemC child port '" + child.path + "."
                            + port.name
                            + "' has no executable binding metadata",
                        {});
                    continue;
                }
                const auto& actual_info
                    = design_.signal_info_[signal->second];
                const auto width = static_cast<std::size_t>(
                    *formal_width);
                bool incompatible { };
                if (width != actual_info.width) {
                    report(
                        "FSIM-ELAB-BIND-020",
                        "width mismatch on native SystemC child port '"
                            + child.path + "." + port.name + "': "
                            + std::to_string(width) + " versus "
                            + std::to_string(actual_info.width),
                        {});
                    incompatible = true;
                }
                if (width > 1U
                    && port.type.is_signed != actual_info.is_signed) {
                    report(
                        "FSIM-ELAB-BIND-021",
                        "signedness mismatch on native SystemC child port '"
                            + child.path + "." + port.name + "'",
                        {});
                    incompatible = true;
                }
                const bool state_domain_alias
                    = (port.type.domain
                            == frontend::ValueDomain::Logic4
                          && actual_info.source_domain
                              == frontend::ValueDomain::Logic9)
                    || (port.type.domain
                            == frontend::ValueDomain::Logic9
                        && actual_info.source_domain
                            == frontend::ValueDomain::Logic4);
                if (port.type.domain != actual_info.source_domain
                    && !state_domain_alias) {
                    report(
                        "FSIM-ELAB-BIND-019",
                        "value-domain mismatch on native SystemC child "
                        "port '" + child.path + "." + port.name + "'",
                        {});
                    incompatible = true;
                }
                if (incompatible) {
                    continue;
                }
                child_aliases.emplace(port.name, signal->second);
                child_objects.emplace(port.handle, signal->second);
                if (state_domain_alias) {
                    design_.boundary_conversions_.push_back(
                        BoundaryConversionInfo {
                            BoundaryConversionKind::state_domain_alias,
                            child.path + "." + port.name,
                            signal->second,
                            signal->second,
                            std::nullopt,
                            port.direction,
                            width,
                            actual_info.width,
                            port.type.domain,
                            actual_info.source_domain,
                            port.type.is_signed,
                            actual_info.is_signed,
                            true,
                            port.type.packed_range,
                            actual_info.packed_range,
                            port.type.integer_range,
                            actual_info.integer_range,
                            {},
                            {},
                            actual_info.declaration_span,
                        });
                }
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





    void HierarchyBuilder::report(
        std::string code,
        std::string message,
        frontend::SourceSpan source) {
        diagnostics_.push_back(
            {std::move(code), std::move(message), std::move(source)});
    }

} // namespace fsim::elaboration

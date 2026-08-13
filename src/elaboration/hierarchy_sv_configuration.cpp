// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
namespace {

    std::string_view normalized_library(const DesignUnit& unit)
    {
        return unit.library.empty() ? std::string_view { "work" }
                                    : std::string_view { unit.library };
    }

    bool selectable_systemverilog_unit(const DesignUnit& unit)
    {
        return !unit.systemverilog_extern
            && (unit.kind == frontend::UnitKind::VerilogModule
                || unit.kind == frontend::UnitKind::SystemVerilogInterface
                || unit.kind == frontend::UnitKind::SystemVerilogProgram);
    }

    std::pair<std::string_view, std::string_view> qualified_cell(
        const std::string_view spelling)
    {
        if (const auto dot = spelling.find('.'); dot != std::string_view::npos) {
            return { spelling.substr(0, dot), spelling.substr(dot + 1U) };
        }
        return { { }, spelling };
    }

    bool same_extern_type(
        const frontend::Type& prototype,
        const frontend::Type& definition)
    {
        return systemverilog_type_identity(prototype)
            == systemverilog_type_identity(definition);
    }

    void append_identity_field(
        std::string& identity,
        const std::string_view field)
    {
        identity += std::to_string(field.size());
        identity += ':';
        identity += field;
        identity += ';';
    }

} // namespace

void HierarchyBuilder::validate_systemverilog_extern_declarations()
{
    for (const auto& prototype : parsed_.units) {
        if (!prototype.systemverilog_extern) {
            continue;
        }
        const DesignUnit* definition = nullptr;
        bool ambiguous = false;
        for (const auto& candidate : parsed_.units) {
            if (candidate.systemverilog_extern
                || candidate.kind != prototype.kind
                || candidate.name != prototype.name
                || normalized_library(candidate)
                    != normalized_library(prototype)) {
                continue;
            }
            if (definition != nullptr) {
                report(
                    "FSIM-ELAB-SVEXTERN-001",
                    "extern declaration '" + prototype.name
                        + "' matches multiple definitions",
                    prototype.span);
                ambiguous = true;
                break;
            }
            definition = &candidate;
        }
        if (ambiguous) {
            continue;
        }
        if (definition == nullptr) {
            report(
                "FSIM-ELAB-SVEXTERN-001",
                "extern declaration '" + prototype.name
                    + "' has no matching definition",
                prototype.span);
            continue;
        }
        bool matches = prototype.parameters.size()
                == definition->parameters.size()
            && prototype.ports.size() == definition->ports.size();
        for (std::size_t index = 0;
            matches && index < prototype.parameters.size(); ++index) {
            const auto& left = prototype.parameters[index];
            const auto& right = definition->parameters[index];
            matches = left.name == right.name && left.kind == right.kind
                && same_extern_type(left.type, right.type);
        }
        for (std::size_t index = 0;
            matches && index < prototype.ports.size(); ++index) {
            const auto& left = prototype.ports[index];
            const auto& right = definition->ports[index];
            matches = left.name == right.name
                && left.direction == right.direction
                && left.is_port == right.is_port
                && same_extern_type(left.type, right.type);
        }
        if (!matches) {
            report(
                "FSIM-ELAB-SVEXTERN-002",
                "extern declaration '" + prototype.name
                    + "' does not match its definition header",
                prototype.span);
        }
    }
}

std::string HierarchyBuilder::systemverilog_configuration_identity(
    const DesignUnit& configuration) const
{
    std::string identity { "sv-config-v1;" };
    append_identity_field(identity, normalized_library(configuration));
    append_identity_field(identity, configuration.name);
    if (!configuration.systemverilog_configuration) {
        return identity;
    }
    const auto& declaration = *configuration.systemverilog_configuration;
    for (const auto& design : declaration.designs) {
        append_identity_field(identity, design.library);
        append_identity_field(identity, design.cell);
    }
    for (const auto& library : declaration.default_liblist) {
        append_identity_field(identity, library);
    }
    for (const auto& rule : declaration.rules) {
        append_identity_field(
            identity,
            rule.kind == frontend::SystemVerilogConfigurationRuleKind::Instance
                ? "instance"
                : "cell");
        append_identity_field(identity, rule.selector);
        append_identity_field(
            identity,
            rule.selection
                    == frontend::SystemVerilogConfigurationSelectionKind::Use
                ? "use"
                : "liblist");
        append_identity_field(identity, rule.use_library);
        append_identity_field(identity, rule.use_cell);
        append_identity_field(
            identity, rule.use_configuration ? "config" : "cell");
        for (const auto& library : rule.liblist) {
            append_identity_field(identity, library);
        }
    }
    return identity;
}

const DesignUnit* HierarchyBuilder::select_systemverilog_configuration_root(
    const DesignUnit& configuration)
{
    if (!configuration.systemverilog_configuration
        || configuration.systemverilog_configuration->designs.size() != 1U) {
        report(
            "FSIM-ELAB-SVCONFIG-001",
            "configuration '" + configuration.name
                + "' selected as one root must contain exactly one design top",
            configuration.span);
        return nullptr;
    }
    const auto& design = configuration.systemverilog_configuration->designs.front();
    const auto library = design.library.empty()
        ? normalized_library(configuration)
        : std::string_view { design.library };
    const DesignUnit* selected = nullptr;
    for (const auto& candidate : parsed_.units) {
        if (!selectable_systemverilog_unit(candidate)
            || normalized_library(candidate) != library
            || candidate.name != design.cell) {
            continue;
        }
        if (selected != nullptr) {
            report(
                "FSIM-ELAB-SVCONFIG-002",
                "configuration design top '" + std::string { library }
                    + "." + design.cell + "' is ambiguous",
                design.span);
            return nullptr;
        }
        selected = &candidate;
    }
    if (selected == nullptr) {
        report(
            "FSIM-ELAB-SVCONFIG-002",
            "configuration design top '" + std::string { library }
                + "." + design.cell + "' was not found",
            design.span);
    }
    return selected;
}

HierarchyBuilder::ConfiguredSystemVerilogInstance
HierarchyBuilder::configure_systemverilog_instance(
    const DesignUnit& unit,
    const frontend::Instance& instance,
    const std::string& path)
{
    ConfiguredSystemVerilogInstance result;
    if (unit.language != frontend::Language::SystemVerilog2017
        && unit.language != frontend::Language::Verilog2005) {
        return result;
    }

    auto parent_path = path;
    if (const auto dot = parent_path.rfind('.'); dot != std::string::npos) {
        parent_path.resize(dot);
    }
    const DesignUnit* configuration = active_systemverilog_configuration_;
    std::string base_path = active_root_;
    for (const auto& [configured_path, candidate] :
        systemverilog_configurations_by_path_) {
        if ((parent_path == configured_path
                || (parent_path.size() > configured_path.size()
                    && parent_path.starts_with(configured_path)
                    && parent_path[configured_path.size()] == '.'))
            && configured_path.size() >= base_path.size()) {
            configuration = candidate;
            base_path = configured_path;
        }
    }
    if (configuration == nullptr
        || !configuration->systemverilog_configuration
        || configuration->systemverilog_configuration->designs.empty()) {
        return result;
    }
    const auto& declaration = *configuration->systemverilog_configuration;
    const auto& design = declaration.designs.front();
    auto logical_path = design.cell;
    if (path.size() > base_path.size() && path.starts_with(base_path)) {
        logical_path += path.substr(base_path.size());
    }

    const frontend::SystemVerilogConfigurationRule* selected_rule = nullptr;
    for (const auto& rule : declaration.rules) {
        if (rule.kind
                == frontend::SystemVerilogConfigurationRuleKind::Instance
            && rule.selector == logical_path) {
            selected_rule = &rule;
            break;
        }
    }
    if (selected_rule == nullptr) {
        for (const auto& rule : declaration.rules) {
            if (rule.kind
                != frontend::SystemVerilogConfigurationRuleKind::Cell) {
                continue;
            }
            const auto [rule_library, rule_cell] = qualified_cell(rule.selector);
            const auto instance_library = normalized_library(unit);
            if (rule_cell == instance.unit_name
                && (rule_library.empty()
                    || rule_library == instance_library)) {
                selected_rule = &rule;
                break;
            }
        }
    }

    const auto find_target = [&](const std::string_view library,
                                 const std::string_view cell,
                                 const frontend::SourceSpan span)
        -> const DesignUnit* {
        const DesignUnit* selected = nullptr;
        for (const auto& candidate : parsed_.units) {
            if (!selectable_systemverilog_unit(candidate)
                || normalized_library(candidate) != library
                || candidate.name != cell) {
                continue;
            }
            if (selected != nullptr) {
                report(
                    "FSIM-ELAB-SVCONFIG-003",
                    "configured cell '" + std::string { library } + "."
                        + std::string { cell } + "' is ambiguous",
                    span);
                return nullptr;
            }
            selected = &candidate;
        }
        return selected;
    };
    const auto select_from_liblist = [&](const auto& libraries,
                                         const frontend::SourceSpan span)
        -> const DesignUnit* {
        for (const auto& library : libraries) {
            if (const auto* selected = find_target(library, instance.unit_name, span)) {
                return selected;
            }
        }
        return nullptr;
    };

    result.applied = selected_rule != nullptr
        || !declaration.default_liblist.empty();
    if (!result.applied) {
        return result;
    }
    if (selected_rule != nullptr
        && selected_rule->selection
            == frontend::SystemVerilogConfigurationSelectionKind::Use) {
        const auto library = selected_rule->use_library.empty()
            ? normalized_library(*configuration)
            : std::string_view { selected_rule->use_library };
        if (selected_rule->use_configuration) {
            for (const auto& candidate : parsed_.units) {
                if (candidate.kind
                        == frontend::UnitKind::SystemVerilogConfiguration
                    && normalized_library(candidate) == library
                    && candidate.name == selected_rule->use_cell) {
                    result.referenced_configuration = &candidate;
                    result.target = select_systemverilog_configuration_root(candidate);
                    break;
                }
            }
        } else {
            result.target = find_target(
                library, selected_rule->use_cell, selected_rule->span);
        }
    } else if (selected_rule != nullptr) {
        result.target = select_from_liblist(selected_rule->liblist, selected_rule->span);
    } else {
        result.target = select_from_liblist(
            declaration.default_liblist, declaration.span);
    }
    if (result.target == nullptr) {
        report(
            "FSIM-ELAB-SVCONFIG-003",
            "configuration '" + configuration->name
                + "' cannot select target for instance '" + path + "'",
            selected_rule != nullptr ? selected_rule->span : declaration.span);
        result.valid = false;
        return result;
    }
    result.configuration_identity = systemverilog_configuration_identity(
        result.referenced_configuration != nullptr
            ? *result.referenced_configuration
            : *configuration);
    return result;
}

std::vector<frontend::Instance>
HierarchyBuilder::systemverilog_bound_instances(
    const DesignUnit& unit,
    const std::string& path,
    const ConstantEnvironment& parameter_environment,
    const ConstantDomainEnvironment& parent_domains)
{
    if (unit.language != frontend::Language::SystemVerilog2017
        && unit.language != frontend::Language::Verilog2005) {
        return { };
    }
    auto logical_path = active_systemverilog_root_name_;
    if (path.size() > active_root_.size() && path.starts_with(active_root_)) {
        logical_path += path.substr(active_root_.size());
    }
    std::vector<frontend::Instance> result;
    for (const auto* directive : compilation_unit_systemverilog_binds_) {
        if (directive == nullptr
            || (directive->target != unit.name
                && directive->target != path
                && directive->target != logical_path)) {
            continue;
        }
        used_compilation_unit_systemverilog_binds_.insert(directive);
        const auto owner = systemverilog_bind_libraries_.find(directive);
        for (const auto& instance : directive->instances) {
            if (owner != systemverilog_bind_libraries_.end()) {
                systemverilog_bound_instance_libraries_.insert_or_assign(
                    path + "." + instance.name, owner->second);
            }
        }
        result.insert(
            result.end(), directive->instances.begin(), directive->instances.end());
    }
    substitute_parameters(
        result,
        parameter_environment,
        parent_domains,
        unit.language);
    return result;
}

} // namespace fsim::elaboration

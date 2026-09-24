// SPDX-License-Identifier: Apache-2.0
#include "elaboration_targets.hpp"
#include "hierarchy_builder_internal.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace fsim::elaboration {

HierarchyBuilder::CompiledSystemVerilogBindCollectionResult
HierarchyBuilder::collect_compiled_systemverilog_bound_instances(
    const semantic::sv::Unit& unit,
    const std::string& path)
{
    CompiledSystemVerilogBindCollectionResult result;
    const auto library_name = [](const semantic::sv::Unit& value) {
        return value.library.empty()
            ? std::string_view { "work" }
            : std::string_view { value.library };
    };
    const auto selectable = [](const semantic::sv::Unit& value) {
        return value.kind == semantic::sv::UnitKind::module
            && !value.external;
    };

    auto logical_path = path;
    if (active_compiled_systemverilog_configuration_ != nullptr
        && active_compiled_systemverilog_configuration_->configuration
        && !active_compiled_systemverilog_configuration_
            ->configuration->designs.empty()) {
        logical_path = active_compiled_systemverilog_configuration_
                           ->configuration->designs.front()
                           .cell;
        if (path.size()
                > active_compiled_systemverilog_configuration_root_.size()
            && path.starts_with(
                active_compiled_systemverilog_configuration_root_)) {
            logical_path += path.substr(
                active_compiled_systemverilog_configuration_root_.size());
        }
    }
    for (const auto& active_bind : active_compiled_systemverilog_binds_) {
        if (active_bind.directive == nullptr) {
            continue;
        }
        const auto& bind = *active_bind.directive;
        const bool instance_target = bind.target.spelling == path
            || bind.target.spelling == logical_path;
        auto scope_target = bind.target.spelling == unit.name;
        const bool systemverilog_2023_bind
            = active_bind.owner != nullptr
            && active_bind.owner->standard == "2023";
        if (!instance_target && scope_target
            && systemverilog_2023_bind) {
            auto selected_library = std::string {
                library_name(*active_bind.owner)
            };
            auto selected_cell = bind.target.spelling;
            const auto* configuration
                = active_compiled_systemverilog_configuration_;
            if (configuration != nullptr
                && configuration->configuration
                && !configuration->configuration->designs.empty()) {
                const auto& declaration
                    = *configuration->configuration;
                const semantic::sv::ConfigurationRule* selected_rule
                    = nullptr;
                for (const auto& rule : declaration.rules) {
                    if (rule.kind
                        != semantic::sv::ConfigurationRuleKind::cell) {
                        continue;
                    }
                    const auto separator = rule.selector.rfind('.');
                    const auto rule_library
                        = separator == std::string::npos
                        ? std::string_view { }
                        : std::string_view { rule.selector }.substr(
                              0U, separator);
                    const auto rule_cell
                        = separator == std::string::npos
                        ? std::string_view { rule.selector }
                        : std::string_view { rule.selector }.substr(
                              separator + 1U);
                    if (rule_cell == bind.target.spelling
                        && (rule_library.empty()
                            || rule_library == selected_library)) {
                        selected_rule = &rule;
                        break;
                    }
                }
                const auto select_from_liblist
                    = [&](const auto& libraries) {
                          for (const auto& library : libraries) {
                              const auto selected
                                  = elaboration_detail::
                                      find_exact_systemverilog_module(
                                          *compiled_, library,
                                          bind.target.spelling);
                              if (selected.unit && !selected.ambiguous) {
                                  selected_library = library;
                                  selected_cell = bind.target.spelling;
                                  return;
                              }
                          }
                          if (!libraries.empty()) {
                              selected_library = libraries.front();
                              selected_cell = bind.target.spelling;
                          }
                      };
                if (selected_rule != nullptr
                    && selected_rule->selection
                        == semantic::sv::ConfigurationSelectionKind::use) {
                    selected_library
                        = selected_rule->use_library.empty()
                        ? std::string { library_name(*configuration) }
                        : selected_rule->use_library;
                    selected_cell = selected_rule->use_cell;
                    if (selected_rule->use_configuration) {
                        auto selected_configuration
                            = selected_rule->target
                            ? compiled_->find_unit(*selected_rule->target)
                            : std::optional<
                                  semantic::CompiledUnitView> { };
                        if (selected_configuration
                            && selected_configuration->systemverilog
                                != nullptr
                            && selected_configuration->systemverilog
                                ->configuration
                            && selected_configuration->systemverilog
                                    ->configuration->designs.size()
                                == 1U) {
                            const auto& design
                                = selected_configuration->systemverilog
                                      ->configuration->designs.front();
                            selected_library = design.library.empty()
                                ? std::string {
                                      library_name(
                                          *selected_configuration
                                              ->systemverilog)
                                  }
                                : design.library;
                            selected_cell = design.cell;
                        }
                    }
                } else if (selected_rule != nullptr) {
                    select_from_liblist(selected_rule->liblist);
                } else {
                    select_from_liblist(declaration.default_liblist);
                }
            }
            scope_target = selectable(unit)
                && library_name(unit) == selected_library
                && unit.name == selected_cell;
        }
        if (!instance_target && !scope_target) {
            continue;
        }
        for (const auto bound_instance : bind.instances) {
            const auto instance = compiled_->find_instance(bound_instance);
            if (!instance || instance->systemverilog == nullptr) {
                result.diagnostic = CompiledSystemVerilogBindDiagnostic {
                    "FSIM-ELAB-HIR-001",
                    "compiled bind directive contains no "
                    "SystemVerilog instance record",
                    bind.source,
                };
                return result;
            }
            result.instances.push_back({ *instance, systemverilog_2023_bind });
        }
    }
    return result;
}

HierarchyBuilder::CompiledSystemVerilogInstanceWorklistResult
HierarchyBuilder::collect_compiled_systemverilog_instance_materializations(
    const semantic::sv::Unit& unit,
    const SystemVerilogHirMaterialization& root_materialization,
    const std::vector<hierarchy_sv_generate_detail::Occurrence>&
        generate_occurrences,
    const std::vector<SystemVerilogHirMaterialization>&
        generated_materializations,
    const std::string& path)
{
    CompiledSystemVerilogInstanceWorklistResult result;
    const auto& specialization = *root_materialization.specialization;
    const auto append_instance = [&](
                                     const semantic::InstanceId id,
                                     const semantic::SpecializedHirUnit& working_specialization,
                                     const SignalMap& working_signals,
                                     const ReadOnlySignalSet& working_read_only_signals,
                                     const StringMap& working_strings,
                                     const ReadOnlyStringSet& working_read_only_strings,
                                     const ContainerMap& working_containers,
                                     const ReadOnlyContainerSet& working_read_only_containers,
                                     const std::string_view working_path,
                                     const bool generated,
                                     const bool bound) {
        if (const auto instance
            = working_specialization.find_instance(id)) {
            if (instance->systemverilog != nullptr
                && !instance->systemverilog
                    ->array_indices.empty()) {
                for (const auto index :
                    instance->systemverilog->array_indices) {
                    result.instances.push_back({
                        *instance,
                        &working_specialization,
                        &working_signals,
                        &working_read_only_signals,
                        &working_strings,
                        &working_read_only_strings,
                        &working_containers,
                        &working_read_only_containers,
                        working_path,
                        generated,
                        bound,
                        false,
                        index,
                    });
                }
            } else {
                result.instances.push_back({
                    *instance,
                    &working_specialization,
                    &working_signals,
                    &working_read_only_signals,
                    &working_strings,
                    &working_read_only_strings,
                    &working_containers,
                    &working_read_only_containers,
                    working_path,
                    generated,
                    bound,
                    false,
                    std::nullopt,
                });
            }
        }
    };
    for (const auto instance : unit.instances) {
        append_instance(
            instance,
            specialization,
            root_materialization.signals,
            root_materialization.read_only_signals,
            root_materialization.string_objects,
            root_materialization.read_only_strings,
            root_materialization.container_objects,
            root_materialization.read_only_containers,
            path,
            false,
            false);
    }
    for (std::size_t index = 0U;
        index < generate_occurrences.size(); ++index) {
        const auto& occurrence = generate_occurrences[index];
        const auto& materialization = generated_materializations[index];
        for (const auto instance : occurrence.region->instances) {
            append_instance(
                instance,
                occurrence.specialization,
                materialization.signals,
                materialization.read_only_signals,
                materialization.string_objects,
                materialization.read_only_strings,
                materialization.container_objects,
                materialization.read_only_containers,
                occurrence.path,
                true,
                false);
        }
    }
    auto bound_instances
        = collect_compiled_systemverilog_bound_instances(unit, path);
    if (bound_instances.diagnostic) {
        result.diagnostic = std::move(bound_instances.diagnostic);
        return result;
    }
    for (const auto& bound : bound_instances.instances) {
        result.instances.push_back({
            bound.instance,
            &specialization,
            &root_materialization.signals,
            &root_materialization.read_only_signals,
            &root_materialization.string_objects,
            &root_materialization.read_only_strings,
            &root_materialization.container_objects,
            &root_materialization.read_only_containers,
            path,
            false,
            true,
            bound.systemverilog_2023_bound,
            std::nullopt,
        });
    }
    return result;
}

} // namespace fsim::elaboration

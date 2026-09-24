// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <algorithm>

namespace fsim::elaboration {

void HierarchyBuilder::canonicalize_process_operations(Process& process)
{
    if (!process_operations_shareable(process)) {
        return;
    }

    ProcessOperationGroupingKey key;
    key.operation_count = process.operations.size();
    key.register_count = process.register_count;
    key.string_register_count = process.string_register_count;
    key.container_register_count = process.container_register_count;
    key.register_value_kinds = process.register_value_kinds;
    key.operation_kinds.reserve(process.operations.size());
    for (const auto& operation : process.operations) {
        key.operation_kinds.emplace_back(
            operation_group_index(operation),
            operation_alternative_index(operation));
    }
    key.sensitivity_edges.reserve(process.static_sensitivity.size());
    for (const auto& sensitivity : process.static_sensitivity) {
        key.sensitivity_edges.push_back(sensitivity.edge);
    }
    key.trigger_regions.reserve(process.static_trigger_regions.size());
    for (const auto& region : process.static_trigger_regions) {
        key.trigger_regions.emplace_back(
            region.begin, region.end, region.mask);
    }
    key.language_standard = process.language_standard;
    key.compatibility_profile = process.compatibility_profile;

    auto& representatives = process_operation_representatives_[key];
    std::erase_if(
        representatives,
        [&](const ProcessId representative) {
            return representative >= design_.processes_.size();
        });
    for (const auto representative : representatives) {
        if (share_process_operations(
                design_.processes_[representative],
                process,
                design_.signals_,
                &operation_scratch_)) {
            return;
        }
    }
    representatives.push_back(process.id);
}

HierarchyBuilder::HierarchyBuilder(
    const semantic::ValidatedCompiledDesign compiled,
    ElaboratedDesign& design,
    std::vector<Diagnostic>& diagnostics,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries)
    : validated_compiled_(compiled),
      compiled_(&compiled.design()),
      design_(design),
      diagnostics_(diagnostics),
      systemc_provider_(systemc_provider),
      systemc_candidates_(
          systemc_provider != nullptr
              ? systemc_provider->candidates()
              : std::vector<SystemCFactoryCandidate>{}),
      systemc_libraries_(
          systemc_provider != nullptr
              ? systemc_provider->libraries()
              : std::vector<std::string>{}),
      search_libraries_(
          search_libraries.begin(), search_libraries.end()) {
    for (const auto& binding : bindings) {
        if (!bindings_.emplace(binding.instance, &binding).second) {
            report(
                "FSIM-ELAB-BIND-010",
                "duplicate binding for instance '" + binding.instance + "'",
                {});
        }
    }
    for (const auto& instance : systemc_instances) {
        if (!systemc_instances_.emplace(instance.path, &instance).second) {
            report(
                "FSIM-ELAB-BIND-032",
                "duplicate constructed SystemC instance path '"
                    + instance.path + "'",
                {});
        }
    }
    std::set<std::pair<std::string, std::string>> factories;
    for (const auto& candidate : systemc_candidates_) {
        if (!factories.emplace(candidate.library, candidate.name).second) {
            report(
                "FSIM-ELAB-BIND-018",
                "duplicate SystemC factory '" + candidate.name
                    + "' in logical library '" + candidate.library + "'",
                {});
        }
    }
    for (const auto& unit : compiled_->systemverilog_units()) {
        if (unit.kind == semantic::sv::UnitKind::package) {
            register_systemverilog_resolution_functions(unit);
        }
    }
    validate_systemverilog_extern_declarations();
}

} // namespace fsim::elaboration

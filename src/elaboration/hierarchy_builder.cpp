// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <algorithm>

namespace fsim::elaboration {

void HierarchyBuilder::canonicalize_process_operations(Process& process)
{
    if (!process_operations_shareable(process)) {
        return;
    }

    std::uint64_t bucket = UINT64_C(1469598103934665603);
    const auto mix = [&](const std::uint64_t value) {
        bucket ^= value;
        bucket *= UINT64_C(1099511628211);
    };
    mix(process.operations.size());
    mix(process.register_count);
    mix(process.string_register_count);
    mix(process.container_register_count);
    for (const auto kind : process.register_value_kinds) {
        mix(static_cast<std::uint64_t>(kind));
    }
    for (const auto& operation : process.operations) {
        mix(operation_group_index(operation));
        mix(operation_alternative_index(operation));
    }

    auto& representatives = process_operation_representatives_[bucket];
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

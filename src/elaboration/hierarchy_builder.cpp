// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {

HierarchyBuilder::HierarchyBuilder(
    const frontend::ParsedDesign& parsed,
    ElaboratedDesign& design,
    std::vector<Diagnostic>& diagnostics,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries)
    : parsed_(parsed),
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
    for (const auto& unit : parsed_.units) {
        if (unit.kind == frontend::UnitKind::SystemVerilogPackage) {
            register_systemverilog_resolution_functions(unit);
        }
        for (const auto& directive : unit.systemverilog_binds) {
            compilation_unit_systemverilog_binds_.push_back(&directive);
            systemverilog_bind_libraries_.insert_or_assign(
                &directive,
                unit.library.empty() ? std::string { "work" } : unit.library);
            systemverilog_bind_revisions_.insert_or_assign(
                &directive, unit.standard_revision);
        }
    }
    validate_systemverilog_extern_declarations();
}

} // namespace fsim::elaboration

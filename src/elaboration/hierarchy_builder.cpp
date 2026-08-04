// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

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
}

const frontend::DesignUnit* HierarchyBuilder::systemc_foreign_target(
    const ForeignChild& child,
    const SystemCInstanceDescription& parent,
    const std::string& path,
    const Binding* binding) {
    if (binding != nullptr && binding->target.has_value()) {
        const auto target = elaboration_detail::parse_target(*binding->target);
        if (!target || target->language == "systemc") {
            report(
                "FSIM-ELAB-BIND-041",
                "SystemC foreign child '" + path
                    + "' must bind to an HDL target",
                {});
            return nullptr;
        }
        if (target->language == "vhdl" && !target->architecture) {
            report(
                "FSIM-ELAB-BIND-016",
                "an explicit VHDL binding target must name an architecture, "
                "for example vhdl:work.entity(rtl)",
                {});
            return nullptr;
        }
        const auto* selected =
            elaboration_detail::choose_bound_unit(parsed_, *target);
        if (selected == nullptr) {
            report(
                "FSIM-ELAB-BIND-015",
                "binding target '" + *binding->target + "' was not found",
                {});
        }
        return selected;
    }
    if (!child.module_facade || child.implementation.empty()) {
        report(
            "FSIM-ELAB-BIND-040",
            "legacy SystemC foreign child '" + path
                + "' requires an explicit VHDL or Verilog/SystemVerilog "
                  "binding",
            {});
        return nullptr;
    }
    const auto parent_target =
        elaboration_detail::parse_target(parent.target);
    const auto library = parent_target && !parent_target->library.empty()
        ? parent_target->library
        : std::string{"work"};
    const auto inferred = inferred_target(
        library, child.implementation, path, {});
    if (!inferred) {
        return nullptr;
    }
    if (inferred->unit == nullptr) {
        report(
            "FSIM-ELAB-BIND-041",
            "SystemC HDL proxy '" + path
                + "' resolved to non-HDL candidate '" + inferred->identity
                + "'",
            {});
        return nullptr;
    }
    return inferred->unit;
}

} // namespace fsim::elaboration

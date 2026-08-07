// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_persistence.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

namespace fsim::frontend {
namespace {

void collect_class_declarations(
    const SystemVerilogClassDeclaration& owner,
    std::vector<SystemVerilogCovergroupDeclaration>& declarations) {
  declarations.insert(
      declarations.end(), owner.covergroups.begin(), owner.covergroups.end());
  for (const auto& nested : owner.nested_classes) {
    collect_class_declarations(nested, declarations);
  }
}

}  // namespace

void refresh_systemverilog_coverage_reports(
    SystemVerilogCoverageState& state) {
  state.reports.clear();
  state.reports.reserve(state.declarations.size());
  for (const auto& declaration : state.declarations) {
    std::vector<SystemVerilogCovergroupInstance> instances;
    std::ranges::copy_if(
        state.instances,
        std::back_inserter(instances),
        [&](const SystemVerilogCovergroupInstance& instance) {
          return instance.declaration_identity
              == declaration.canonical_identity;
        });
    state.reports.push_back(build_systemverilog_coverage_report(
        declaration, instances));
  }
}

SystemVerilogCoverageState capture_systemverilog_coverage_state(
    const ParsedDesign& design) {
  SystemVerilogCoverageState state;
  for (const auto& unit : design.units) {
    state.declarations.insert(
        state.declarations.end(),
        unit.systemverilog_covergroups.begin(),
        unit.systemverilog_covergroups.end());
    for (const auto& owner : unit.systemverilog_classes) {
      collect_class_declarations(owner, state.declarations);
    }
  }
  for (const auto& owner : design.systemverilog_classes) {
    collect_class_declarations(owner, state.declarations);
  }
  state.instances = design.systemverilog_covergroup_instances;
  std::ranges::sort(
      state.declarations, {},
      &SystemVerilogCovergroupDeclaration::canonical_identity);
  std::ranges::sort(
      state.instances, {}, &SystemVerilogCovergroupInstance::runtime_identity);
  refresh_systemverilog_coverage_reports(state);
  return state;
}

}  // namespace fsim::frontend

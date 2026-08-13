// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

void HierarchyBuilder::predeclare_root_globals(
    const DesignUnit& root,
    std::string path) {
  if (root.language != frontend::Language::SystemVerilog2017) {
    return;
  }
  if (root.kind == frontend::UnitKind::SystemVerilogConfiguration) {
      return;
  }
  active_root_ = path;
  auto specialized = specialize_selected_unit(
      root, {}, {}, {}, {}, {}, {}, {}, {}, root.language);
  SignalMap aliases;
  const auto* ports = unit_ports(parsed_, specialized.unit);
  const auto alias_plan = ports == nullptr
      ? SystemVerilogAliasPlan { }
      : apply_systemverilog_aliases(
            specialized.unit, *ports, { }, { }, false, false);
  const auto& alias_groups = alias_plan.whole_groups;
  std::unordered_map<std::string, std::size_t> alias_group_by_name;
  for (std::size_t group = 0; group < alias_groups.size(); ++group) {
      for (const auto& name : alias_groups[group]) {
          alias_group_by_name.emplace(name, group);
      }
  }
  std::vector<std::optional<SignalId>> alias_signals(
      alias_groups.size());
  const auto add_or_bind =
      [&](const frontend::SignalDeclaration& declaration) {
          const auto group = alias_group_by_name.find(declaration.name);
          if (group == alias_group_by_name.end()) {
              return add_owned_signal(declaration, path, aliases);
          }
          auto& signal = alias_signals[group->second];
          if (!signal) {
              signal = add_owned_signal(declaration, path, aliases);
          } else {
              const auto full_name = path + "." + declaration.name;
              aliases.insert_or_assign(declaration.name, *signal);
              aliases.insert_or_assign(full_name, *signal);
              design_.signal_by_name_.insert_or_assign(full_name, *signal);
              if (design_.roots_.size() == 1 && path == active_root_) {
                  design_.signal_by_name_.insert_or_assign(
                      declaration.name, *signal);
              }
          }
          return signal;
      };
  if (ports != nullptr) {
    for (const auto& port : *ports) {
      if (!port.interface_type.empty()
          || port.type.spelling == "interface"
          || port.type.domain == frontend::ValueDomain::String
          || port.type.systemverilog_container) {
        continue;
      }
      (void)add_or_bind(port);
    }
  }
  for (const auto& signal : specialized.unit.signals) {
      (void)add_or_bind(signal);
  }
  for (const auto& [name, signal] : aliases) {
    if (name.starts_with(path + ".")) {
      global_root_signals_.try_emplace(name, signal);
      global_root_signals_.try_emplace("$root." + name, signal);
    }
  }
  predeclared_root_signals_.emplace(path, std::move(aliases));
  prepared_systemverilog_roots_.emplace(
      std::move(path), std::move(specialized));
}

} // namespace fsim::elaboration

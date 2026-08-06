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
  active_root_ = path;
  auto specialized = specialize_selected_unit(
      root, {}, {}, {}, {}, {}, {}, {}, {}, root.language);
  SignalMap aliases;
  const auto* ports = unit_ports(parsed_, specialized.unit);
  if (ports != nullptr) {
    for (const auto& port : *ports) {
      if (!port.interface_type.empty()
          || port.type.spelling == "interface"
          || port.type.domain == frontend::ValueDomain::String
          || port.type.systemverilog_container) {
        continue;
      }
      (void)add_owned_signal(port, path, aliases);
    }
  }
  for (const auto& signal : specialized.unit.signals) {
    (void)add_owned_signal(signal, path, aliases);
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

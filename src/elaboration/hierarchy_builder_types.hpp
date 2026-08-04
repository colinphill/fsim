// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {

struct HierarchyPortAliases {
  std::unordered_map<std::string, runtime::simir::SignalId> signals;
  std::unordered_map<std::string, runtime::simir::StringObjectId> strings;
  std::unordered_map<std::string, runtime::simir::ContainerObjectId> containers;
  std::vector<frontend::Statement> vhdl_input_drivers;
  std::unordered_set<runtime::simir::SignalId> read_only_signals;
  std::unordered_set<runtime::simir::StringObjectId> read_only_strings;
};

struct HierarchyContainerBoundaryDriver {
  std::string path;
  std::optional<std::pair<std::int32_t, std::int32_t>> selected_interval;
};

struct HierarchyConfiguredVhdlInstance {
  frontend::Instance instance;
  std::optional<frontend::DesignUnit> target;
  std::optional<std::string> systemc_target;
  const frontend::VhdlComponentConfiguration* configuration_rule{};
  const frontend::DesignUnit* referenced_configuration{};
  std::string component_name;
  std::string component_identity;
  std::string configuration_identity;
  bool applied{};
  bool valid{true};
};

} // namespace fsim::elaboration

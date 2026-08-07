// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {

struct SystemVerilogCoveragePercentage {
  std::uint32_t raw_basis_points{};
  std::uint32_t basis_points{};
  std::uint32_t goal{100U};
  bool empty{true};
  bool goal_reached{};
};

struct SystemVerilogCoverageDeclarationPercentage {
  std::size_t coverage_declaration_index{};
  SystemVerilogCoverageDeclarationKind kind{
      SystemVerilogCoverageDeclarationKind::Coverpoint};
  std::string identity;
  std::uint64_t eligible_weight{};
  std::uint64_t covered_weight{};
  SystemVerilogCoveragePercentage coverage;
};

struct SystemVerilogCovergroupInstancePercentage {
  std::string runtime_identity;
  std::vector<SystemVerilogCoverageDeclarationPercentage> declarations;
  SystemVerilogCoveragePercentage coverage;
};

struct SystemVerilogCovergroupTypePercentage {
  std::string declaration_identity;
  bool per_instance{};
  bool merge_instances{};
  std::vector<SystemVerilogCovergroupInstancePercentage> instances;
  SystemVerilogCoveragePercentage coverage;
};

[[nodiscard]] SystemVerilogCovergroupInstancePercentage
calculate_systemverilog_covergroup_instance_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCovergroupInstance& instance);

[[nodiscard]] SystemVerilogCovergroupTypePercentage
calculate_systemverilog_covergroup_type_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::span<const SystemVerilogCovergroupInstance> instances);

}  // namespace fsim::frontend

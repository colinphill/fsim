// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {

struct SystemVerilogCoverageSampleValue {
  std::int64_t value{};
  std::uint64_t unknown_mask{};
  std::uint32_t width{64U};
};

struct SystemVerilogCoverageSampleResult {
  std::vector<std::string> hit_bin_identities;
  std::optional<std::string> selected_bin_identity;
  bool ignored{};
  bool illegal{};
  bool zero_weight_excluded{};
  bool threshold_reached{};
  bool hit_count_overflow{};
};

struct SystemVerilogCovergroupSampleInput {
  std::size_t coverage_declaration_index{};
  SystemVerilogCoverageSampleValue value;
};

struct SystemVerilogCovergroupSampledCoverpoint {
  std::size_t coverage_declaration_index{};
  SystemVerilogCoverageSampleResult result;
};

struct SystemVerilogCovergroupSampleResult {
  std::vector<SystemVerilogCovergroupSampledCoverpoint> coverpoints;
  std::vector<std::string> hit_cross_bin_identities;
  std::vector<std::string> excluded_cross_bin_identities;
};

[[nodiscard]] SystemVerilogCoverageSampleResult
sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::size_t coverage_declaration_index,
    const SystemVerilogCoverageSampleValue& value,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] SystemVerilogCoverageSampleResult
sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::size_t coverage_declaration_index,
    std::int64_t value,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] SystemVerilogCovergroupSampleResult
sample_systemverilog_covergroup(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::span<const SystemVerilogCovergroupSampleInput> inputs,
    std::vector<Diagnostic>& diagnostics);

}  // namespace fsim::frontend

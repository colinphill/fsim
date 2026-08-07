// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_percentage.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

struct SystemVerilogCoverageBinReport {
  std::string identity;
  std::string source_name;
  SystemVerilogCoverageBinKind kind{
      SystemVerilogCoverageBinKind::Regular};
  std::uint32_t weight{1U};
  std::uint32_t goal{100U};
  std::uint64_t at_least{1U};
  std::uint64_t hit_count{};
  std::uint64_t exclusion_count{};
  bool covered{};
  bool excluded{};
  SourceSpan span;
};

struct SystemVerilogCoverageItemReport {
  std::size_t coverage_declaration_index{};
  SystemVerilogCoverageDeclarationKind kind{
      SystemVerilogCoverageDeclarationKind::Coverpoint};
  std::string identity;
  SystemVerilogCoveragePercentage coverage;
  std::vector<SystemVerilogCoverageBinReport> bins;
  SourceSpan span;
};

struct SystemVerilogCoverageInstanceReport {
  std::string runtime_identity;
  SystemVerilogCoveragePercentage coverage;
  std::vector<SystemVerilogCoverageItemReport> items;
  std::vector<SystemVerilogCoverageIllegalBinReport> illegal_bins;
  SourceSpan span;
};

struct SystemVerilogCoverageTypeReport {
  std::string declaration_identity;
  SystemVerilogCoveragePercentage coverage;
  bool per_instance{};
  bool merge_instances{};
  std::vector<SystemVerilogCoverageInstanceReport> instances;
  SourceSpan span;
};

[[nodiscard]] SystemVerilogCoverageTypeReport
build_systemverilog_coverage_report(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::span<const SystemVerilogCovergroupInstance> instances);

[[nodiscard]] const SystemVerilogCoverageInstanceReport*
query_systemverilog_coverage_instance(
    const SystemVerilogCoverageTypeReport& report,
    std::string_view runtime_identity);

[[nodiscard]] const SystemVerilogCoverageItemReport*
query_systemverilog_coverage_item(
    const SystemVerilogCoverageTypeReport& report,
    std::string_view identity);

[[nodiscard]] const SystemVerilogCoverageBinReport*
query_systemverilog_coverage_bin(
    const SystemVerilogCoverageTypeReport& report,
    std::string_view identity);

[[nodiscard]] std::string render_systemverilog_coverage_report(
    const SystemVerilogCoverageTypeReport& report);

}  // namespace fsim::frontend

// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_limits.hpp"

#include <algorithm>
#include <ranges>
#include <limits>
#include <string>
#include <utility>

namespace fsim::frontend {
namespace {

bool exceeds_product(
    std::size_t& product,
    const std::size_t factor,
    const std::size_t limit) noexcept {
  if (factor != 0U && product > limit / factor) return true;
  product *= factor;
  return product > limit;
}

bool reject(
    std::vector<Diagnostic>& diagnostics,
    std::string message,
    const SourceSpan& span) {
  diagnostics.push_back({
      DiagnosticSeverity::Error,
      "FSIM-SV-SEM-221",
      std::move(message),
      span,
      {}});
  return false;
}

}  // namespace

bool validate_systemverilog_coverage_resources(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::vector<Diagnostic>& diagnostics) {
  if (declaration.coverage_declarations.size()
      > kSystemVerilogCoverageMaximumDeclarations) {
    return reject(
        diagnostics, "covergroup declaration count exceeds 4096",
        declaration.span);
  }
  std::size_t bin_count{};
  std::size_t work{};
  for (const auto& item : declaration.coverage_declarations) {
    if (item.bins.size() > kSystemVerilogCoverageMaximumBins - bin_count) {
      return reject(
          diagnostics, "covergroup bin inventory exceeds 65536", item.span);
    }
    bin_count += item.bins.size();
    for (const auto& bin : item.bins) {
      for (const auto& sequence : bin.transitions) {
        for (const auto& step : sequence.steps) {
          const auto repetitions = step.repetition.maximum.value_or(
              kSystemVerilogCoverageMaximumWork + 1U);
          if (repetitions > kSystemVerilogCoverageMaximumWork - work) {
            return reject(
                diagnostics, "transition work exceeds 1048576", bin.span);
          }
          work += repetitions;
        }
        for (const auto& delay : sequence.delays) {
          const auto width = static_cast<std::size_t>(delay.maximum)
              - static_cast<std::size_t>(delay.minimum) + 1U;
          if (width > kSystemVerilogCoverageMaximumWork - work) {
            return reject(
                diagnostics, "transition work exceeds 1048576", bin.span);
          }
          work += width;
        }
      }
    }
    if (item.kind != SystemVerilogCoverageDeclarationKind::Cross) continue;
    std::size_t product{1U};
    for (const auto& operand : item.cross_operands) {
      if (!operand.resolved_declaration_index
          || *operand.resolved_declaration_index
              >= declaration.coverage_declarations.size()) {
        continue;
      }
      const auto& coverpoint = declaration.coverage_declarations[
          *operand.resolved_declaration_index];
      const auto factor = std::max<std::size_t>(1U, coverpoint.bins.size());
      if (exceeds_product(
              product, factor,
              kSystemVerilogCoverageMaximumCrossProduct)) {
        return reject(
            diagnostics, "cross product exceeds 1048576", item.span);
      }
    }
  }
  return true;
}

std::size_t systemverilog_coverage_state_records(
    const SystemVerilogCovergroupInstance& instance) noexcept {
  const auto add = [](std::size_t& total, const std::size_t count) {
    total = count > std::numeric_limits<std::size_t>::max() - total
        ? std::numeric_limits<std::size_t>::max() : total + count;
  };
  std::size_t total{};
  add(total, instance.bin_hits.size());
  add(total, instance.transition_progress.size());
  add(total, instance.previous_samples.size());
  add(total, instance.cross_bin_state.size());
  add(total, instance.illegal_bin_reports.size());
  return total;
}

bool validate_systemverilog_coverage_sample_resources(
    const SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::size_t input_count,
    std::vector<Diagnostic>& diagnostics) {
  const auto current = systemverilog_coverage_state_records(instance);
  const auto cross_count = static_cast<std::size_t>(std::ranges::count_if(
      declaration.coverage_declarations,
      [](const SystemVerilogCoverageDeclaration& item) {
        return item.kind == SystemVerilogCoverageDeclarationKind::Cross;
      }));
  const auto input_work = input_count
          > kSystemVerilogCoverageMaximumStateRecords / 4U
      ? kSystemVerilogCoverageMaximumStateRecords + 1U
      : input_count * 4U;
  const auto additional = cross_count
          > kSystemVerilogCoverageMaximumStateRecords - std::min(
                input_work, kSystemVerilogCoverageMaximumStateRecords)
      ? kSystemVerilogCoverageMaximumStateRecords + 1U
      : input_work + cross_count;
  if (input_count > kSystemVerilogCoverageMaximumTransactionInputs
      || current > kSystemVerilogCoverageMaximumStateRecords
      || additional > kSystemVerilogCoverageMaximumStateRecords - std::min(
             current, kSystemVerilogCoverageMaximumStateRecords)) {
    diagnostics.push_back({
        DiagnosticSeverity::Error,
        "FSIM-SV-COV-005",
        "coverage sample exceeds transaction work or state-storage budget",
        declaration.span,
        {}});
    return false;
  }
  return true;
}

}  // namespace fsim::frontend

// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_report.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <utility>

namespace fsim::frontend {
namespace {

std::string logical_bin_identity(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& item,
    const SystemVerilogCoverageBin& bin) {
  return declaration.canonical_identity + "::" + item.name + "." + bin.name;
}

SystemVerilogCoverageBinReport coverpoint_bin_report(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& item,
    const SystemVerilogCoverageBin& bin,
    const SystemVerilogCoverageBinHit* hit,
    const std::string_view runtime_identity) {
  SystemVerilogCoverageBinReport report;
  report.identity = std::string{runtime_identity} + "::"
      + (hit ? hit->identity
             : logical_bin_identity(declaration, item, bin));
  report.source_name = bin.source_name;
  report.kind = bin.kind;
  report.weight = bin.weight;
  report.goal = bin.goal;
  report.at_least = bin.at_least;
  report.hit_count = hit ? hit->hit_count : 0U;
  report.covered = hit && hit->covered;
  report.excluded =
      bin.kind != SystemVerilogCoverageBinKind::Regular || bin.weight == 0U;
  report.span = bin.span;
  return report;
}

SystemVerilogCoverageBinReport cross_bin_report(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& item,
    const SystemVerilogCoverageBin* bin,
    const SystemVerilogCoverageCrossBinState* state,
    const std::string_view runtime_identity) {
  SystemVerilogCoverageBinReport report;
  report.identity = std::string{runtime_identity} + "::"
      + (state ? state->identity
               : logical_bin_identity(declaration, item, *bin));
  report.source_name = bin ? bin->source_name : "$auto";
  report.kind = bin ? bin->kind : SystemVerilogCoverageBinKind::Regular;
  report.weight = state ? state->weight : bin->weight;
  report.goal = state ? state->goal : bin->goal;
  report.at_least = state ? state->at_least : bin->at_least;
  report.hit_count = state ? state->hit_count : 0U;
  report.exclusion_count = state ? state->exclusion_count : 0U;
  report.covered = state && state->covered;
  report.excluded = state ? state->excluded
      : bin->kind != SystemVerilogCoverageBinKind::Regular
          || bin->weight == 0U;
  report.span = bin ? bin->span : item.span;
  return report;
}

std::vector<SystemVerilogCoverageBinReport> item_bins(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& item,
    const SystemVerilogCovergroupInstance& instance) {
  std::vector<SystemVerilogCoverageBinReport> result;
  if (item.kind == SystemVerilogCoverageDeclarationKind::Coverpoint) {
    for (const auto& bin : item.bins) {
      std::vector<const SystemVerilogCoverageBinHit*> hits;
      for (const auto& hit : instance.bin_hits) {
        if (hit.coverage_declaration_index == item.declaration_index
            && hit.bin_declaration_index == bin.declaration_index) {
          hits.push_back(&hit);
        }
      }
      std::ranges::sort(
          hits, {}, &SystemVerilogCoverageBinHit::identity);
      if (hits.empty()) {
        result.push_back(
            coverpoint_bin_report(
                declaration, item, bin, nullptr,
                instance.runtime_identity));
      } else {
        for (const auto* hit : hits) {
          result.push_back(
              coverpoint_bin_report(
                  declaration, item, bin, hit,
                  instance.runtime_identity));
        }
      }
    }
    return result;
  }
  if (item.bins.empty()) {
    std::vector<const SystemVerilogCoverageCrossBinState*> states;
    for (const auto& state : instance.cross_bin_state) {
      if (state.coverage_declaration_index == item.declaration_index) {
        states.push_back(&state);
      }
    }
    std::ranges::sort(
        states, {}, &SystemVerilogCoverageCrossBinState::identity);
    for (const auto* state : states) {
      result.push_back(
          cross_bin_report(
              declaration, item, nullptr, state,
              instance.runtime_identity));
    }
    return result;
  }
  for (const auto& bin : item.bins) {
    std::vector<const SystemVerilogCoverageCrossBinState*> states;
    for (const auto& state : instance.cross_bin_state) {
      if (state.coverage_declaration_index == item.declaration_index
          && state.bin_declaration_index == bin.declaration_index) {
        states.push_back(&state);
      }
    }
    std::ranges::sort(
        states, {}, &SystemVerilogCoverageCrossBinState::identity);
    if (states.empty()) {
      result.push_back(cross_bin_report(
          declaration, item, &bin, nullptr,
          instance.runtime_identity));
    } else {
      for (const auto* state : states) {
        result.push_back(cross_bin_report(
            declaration, item, &bin, state,
            instance.runtime_identity));
      }
    }
  }
  if (declaration.effective_cross_retain_auto_bins) {
    std::vector<const SystemVerilogCoverageCrossBinState*> automatic_states;
    for (const auto& state : instance.cross_bin_state) {
      if (state.coverage_declaration_index == item.declaration_index
          && !state.bin_declaration_index) {
        automatic_states.push_back(&state);
      }
    }
    std::ranges::sort(
        automatic_states, {}, &SystemVerilogCoverageCrossBinState::identity);
    for (const auto* state : automatic_states) {
      result.push_back(cross_bin_report(
          declaration, item, nullptr, state, instance.runtime_identity));
    }
  }
  return result;
}

std::string formatted_percentage(
    const SystemVerilogCoveragePercentage& percentage) {
  const auto whole = percentage.basis_points / 100U;
  const auto fraction = percentage.basis_points % 100U;
  auto result = std::to_string(whole) + ".";
  if (fraction < 10U) result += "0";
  return result + std::to_string(fraction) + "%";
}

}  // namespace

SystemVerilogCoverageTypeReport build_systemverilog_coverage_report(
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::span<const SystemVerilogCovergroupInstance> instances) {
  SystemVerilogCoverageTypeReport report;
  const auto percentages =
      calculate_systemverilog_covergroup_type_percentage(
          declaration, instances);
  report.declaration_identity = declaration.canonical_identity;
  report.coverage = percentages.coverage;
  report.per_instance = percentages.per_instance;
  report.merge_instances = percentages.merge_instances;
  report.span = declaration.span;
  for (const auto& instance_percentage : percentages.instances) {
    const auto instance = std::ranges::find(
        instances,
        instance_percentage.runtime_identity,
        &SystemVerilogCovergroupInstance::runtime_identity);
    if (instance == instances.end()) continue;
    SystemVerilogCoverageInstanceReport instance_report;
    instance_report.runtime_identity = instance->runtime_identity;
    instance_report.coverage = instance_percentage.coverage;
    instance_report.illegal_bins = instance->illegal_bin_reports;
    instance_report.span = instance->span;
    for (const auto& item_percentage :
         instance_percentage.declarations) {
      const auto& item = declaration.coverage_declarations[
          item_percentage.coverage_declaration_index];
      SystemVerilogCoverageItemReport item_report;
      item_report.coverage_declaration_index = item.declaration_index;
      item_report.kind = item.kind;
      item_report.identity = instance->runtime_identity + "::"
          + item_percentage.identity;
      item_report.coverage = item_percentage.coverage;
      item_report.bins = item_bins(declaration, item, *instance);
      item_report.span = item.span;
      instance_report.items.push_back(std::move(item_report));
    }
    report.instances.push_back(std::move(instance_report));
  }
  return report;
}

const SystemVerilogCoverageInstanceReport*
query_systemverilog_coverage_instance(
    const SystemVerilogCoverageTypeReport& report,
    const std::string_view runtime_identity) {
  const auto found = std::ranges::find(
      report.instances, runtime_identity,
      &SystemVerilogCoverageInstanceReport::runtime_identity);
  return found == report.instances.end() ? nullptr : &*found;
}

const SystemVerilogCoverageItemReport* query_systemverilog_coverage_item(
    const SystemVerilogCoverageTypeReport& report,
    const std::string_view identity) {
  for (const auto& instance : report.instances) {
    const auto found = std::ranges::find(
        instance.items, identity,
        &SystemVerilogCoverageItemReport::identity);
    if (found != instance.items.end()) return &*found;
  }
  return nullptr;
}

const SystemVerilogCoverageBinReport* query_systemverilog_coverage_bin(
    const SystemVerilogCoverageTypeReport& report,
    const std::string_view identity) {
  for (const auto& instance : report.instances) {
    for (const auto& item : instance.items) {
      const auto found = std::ranges::find(
          item.bins, identity,
          &SystemVerilogCoverageBinReport::identity);
      if (found != item.bins.end()) return &*found;
    }
  }
  return nullptr;
}

std::string render_systemverilog_coverage_report(
    const SystemVerilogCoverageTypeReport& report) {
  std::string text = "type " + report.declaration_identity
      + " coverage=" + formatted_percentage(report.coverage)
      + " per_instance=" + (report.per_instance ? "true" : "false")
      + " merge_instances=" + (report.merge_instances ? "true" : "false")
      + " source=" + report.span.source_name + "\n";
  for (const auto& instance : report.instances) {
    text += "  instance " + instance.runtime_identity
        + " coverage=" + formatted_percentage(instance.coverage)
        + " source=" + instance.span.source_name + "\n";
    for (const auto& item : instance.items) {
      text += "    item " + item.identity
          + " coverage=" + formatted_percentage(item.coverage)
          + " goal=" + std::to_string(item.coverage.goal)
          + " source=" + item.span.source_name + "\n";
      for (const auto& bin : item.bins) {
        text += "      bin " + bin.identity
            + " hits=" + std::to_string(bin.hit_count)
            + " exclusions=" + std::to_string(bin.exclusion_count)
            + " weight=" + std::to_string(bin.weight)
            + " goal=" + std::to_string(bin.goal)
            + " at_least=" + std::to_string(bin.at_least)
            + " covered=" + (bin.covered ? "true" : "false")
            + " excluded=" + (bin.excluded ? "true" : "false")
            + " source=" + bin.span.source_name + "\n";
      }
    }
  }
  return text;
}

}  // namespace fsim::frontend

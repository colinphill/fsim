// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_observation.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <utility>

namespace fsim::frontend {
namespace {

void add_unsigned(
    std::vector<SystemVerilogCoverageObservation>& result,
    std::string path,
    const std::uint64_t value) {
  SystemVerilogCoverageObservation observation;
  observation.path = path;
  observation.canonical_path = std::move(path);
  observation.unsigned_value = value;
  observation.vcd_compatible = true;
  result.push_back(std::move(observation));
}

void add_boolean(
    std::vector<SystemVerilogCoverageObservation>& result,
    std::string path,
    const bool value) {
  SystemVerilogCoverageObservation observation;
  observation.path = path;
  observation.canonical_path = std::move(path);
  observation.kind = SystemVerilogCoverageObservationKind::Boolean;
  observation.unsigned_value = value ? 1U : 0U;
  observation.vcd_compatible = true;
  result.push_back(std::move(observation));
}

void add_aliases(
    std::vector<SystemVerilogCoverageObservation>& result,
    const std::span<const SystemVerilogCoverageAlias> aliases) {
  const auto canonical = result;
  for (const auto& alias : aliases) {
    for (const auto& value : canonical) {
      if (!std::string_view{value.path}.starts_with(alias.canonical_root)) {
        continue;
      }
      auto aliased = value;
      aliased.path = alias.path
          + value.path.substr(alias.canonical_root.size());
      result.push_back(std::move(aliased));
    }
  }
}

std::string event_path(
    const SystemVerilogCoverageCallbackEvent& event) {
  auto path = event.runtime_identity + ".coverage";
  if (event.bin_identity) {
    path += "::" + *event.bin_identity;
  }
  return path;
}

}  // namespace

std::vector<SystemVerilogCoverageObservation>
build_systemverilog_coverage_debug_snapshot(
    const std::span<const SystemVerilogCoverageRoot> roots,
    const std::span<const SystemVerilogCoverageAlias> aliases) {
  std::vector<SystemVerilogCoverageObservation> result;
  for (const auto& root : roots) {
    add_unsigned(
        result, root.path + ".coverage_basis_points",
        root.report.coverage.basis_points);
    for (const auto& instance : root.report.instances) {
      const auto instance_path =
          root.path + ".instance[" + instance.runtime_identity + "]";
      add_unsigned(
          result, instance_path + ".coverage_basis_points",
          instance.coverage.basis_points);
      for (const auto& item : instance.items) {
        const auto item_path =
            instance_path + ".item[" + item.identity + "]";
        add_unsigned(
            result, item_path + ".coverage_basis_points",
            item.coverage.basis_points);
        for (const auto& bin : item.bins) {
          const auto bin_path = item_path + ".bin[" + bin.identity + "]";
          add_unsigned(result, bin_path + ".hits", bin.hit_count);
          add_unsigned(
              result, bin_path + ".exclusions", bin.exclusion_count);
          add_boolean(result, bin_path + ".covered", bin.covered);
          add_boolean(result, bin_path + ".excluded", bin.excluded);
        }
      }
    }
  }
  add_aliases(result, aliases);
  std::ranges::sort(result, {}, &SystemVerilogCoverageObservation::path);
  return result;
}

std::vector<SystemVerilogCoverageTraceEvent>
build_systemverilog_coverage_trace_events(
    const SystemVerilogCoverageExecutionResult& execution,
    const std::uint64_t time,
    const std::uint64_t delta,
    const std::span<const SystemVerilogCoverageAlias> aliases) {
  std::vector<SystemVerilogCoverageTraceEvent> result;
  for (const auto& callback : execution.events) {
    SystemVerilogCoverageTraceEvent event;
    event.sequence = callback.sequence;
    event.time = time;
    event.delta = delta;
    event.path = event_path(callback);
    event.canonical_path = event.path;
    event.kind = callback.kind;
    event.sampled_value = callback.value;
    event.vcd_compatible =
        callback.kind == SystemVerilogCoverageCallbackKind::Hit
        || callback.kind == SystemVerilogCoverageCallbackKind::IllegalBin;
    result.push_back(std::move(event));
  }
  const auto canonical = result;
  for (const auto& alias : aliases) {
    for (const auto& event : canonical) {
      if (!std::string_view{event.path}.starts_with(alias.canonical_root)) {
        continue;
      }
      auto aliased = event;
      aliased.path =
          alias.path + event.path.substr(alias.canonical_root.size());
      result.push_back(std::move(aliased));
    }
  }
  std::ranges::sort(
      result,
      [](const SystemVerilogCoverageTraceEvent& left,
         const SystemVerilogCoverageTraceEvent& right) {
        if (left.sequence != right.sequence) {
          return left.sequence < right.sequence;
        }
        return left.path < right.path;
      });
  return result;
}

}  // namespace fsim::frontend

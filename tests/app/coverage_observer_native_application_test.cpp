// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/frontend/coverage_source_identity.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/verilog_coverage_points.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct ObservedChange {
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};
  std::string value;
  std::int32_t covered_statements{};

  friend bool operator==(const ObservedChange&, const ObservedChange&)
      = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<ObservedChange> changes;
  std::string final_value;
  std::string vcd;
  std::string design_cache_identity;
  std::vector<std::string> native_cache_identities;
  fsim::app::NativeCacheStatistics cache;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  std::int32_t statement_coverage{};
};

void write_source(const std::filesystem::path& path) {
  std::ofstream output(path, std::ios::binary);
  output << R"(
module coverage_observer_native;
  logic [7:0] value;

  initial begin
    value = 8'h12;
    #1 value = 8'h34;
    #1 value = 8'h56;
    #1 value = 8'h78;
  end
endmodule
)";
  assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "coverage-observer-native";
  config.project.top = "sv:work.coverage_observer_native";
  config.project.tops = {
      {"sv:work.coverage_observer_native", "top"}};
  config.project.time_resolution = "1ns";
  config.build.optimization = fsim::project::Optimization::o0;
  config.build.cache_path = directory / "native-cache";
  config.coverage.enabled = true;
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

void attach_statement_coverage(
    fsim::app::BuiltProject& built,
    const fsim::project::Config& config,
    const std::filesystem::path& source_path) {
  using namespace fsim;

  assert(built.code_coverage_enabled);
  assert(!built.design.code_coverage_inventory());
  assert(!built.design.specializations().empty());
  assert(!built.design_ir.processes().empty());

  const auto has_existing_hit = [](const auto& process) {
    return std::ranges::any_of(process.operations, [](const auto& operation) {
      return runtime::simir::operation_get_if<
                 runtime::simir::CodeCoverageHit>(&operation)
          != nullptr;
    });
  };
  assert(std::ranges::none_of(built.design.processes(), has_existing_hit));

  std::ifstream source_input(source_path, std::ios::binary);
  assert(source_input.good());
  const std::string source_text(
      std::istreambuf_iterator<char> { source_input },
      std::istreambuf_iterator<char> { });
  const auto source_identity = frontend::make_code_coverage_source_identity(
      config.base_directory, source_path,
      std::as_bytes(std::span { source_text.data(), source_text.size() }));
  assert(source_identity.ok());

  const auto& source_name = built.design.specializations().front().source;
  const auto parsed = frontend::parse_text(
      source_name, source_text, frontend::Language::SystemVerilog2017);
  assert(parsed.ok() && parsed.design.units.size() == 1U
      && parsed.design.units.front().processes.size() == 1U);
  const elaboration::VerilogCoverageSource coverage_source {
      source_name, *source_identity.identity, source_text };
  const auto discovered = frontend::discover_verilog_statement_points(
      parsed.design.units.front().processes.front().statements,
      frontend::Language::SystemVerilog2017,
      std::span { &coverage_source, 1U });
  assert(discovered.ok() && !discovered.points.empty());

  std::vector<elaboration::CoverageInventoryPointDraft> points;
  points.reserve(discovered.points.size());
  for (const auto& point : discovered.points) {
    points.push_back({ point.id, runtime::CodeCoverageMetric::Statement,
        point.source_index, point.span, point.line });
  }
  const std::array sources {
      elaboration::CoverageInventorySource {
          source_name, *source_identity.identity }
  };
  std::vector<elaboration::CoverageInstanceInventoryDraft> instances;
  instances.reserve(built.design.specializations().size());
  for (const auto& specialization : built.design.specializations()) {
    instances.push_back({ specialization.id, points });
  }
  assert(built.design.attach_code_coverage_inventory(sources, instances).ok());

  const auto& inventory = built.design.code_coverage_inventory();
  assert(inventory && inventory->total_points > 0U);
  const auto& occurrence = built.design_ir.processes().front();
  const auto instance = std::ranges::find(
      inventory->instances, occurrence.specialization.value(),
      &elaboration::CoverageInstanceInventory::specialization);
  assert(instance != inventory->instances.end());
  assert(instance->specialization == occurrence.specialization.value());
  const auto& selected_id = discovered.points.back().id;
  const auto point = std::ranges::find(instance->points, selected_id,
      [](const auto& candidate) { return candidate.point.id; });
  assert(point != instance->points.end());
  const auto& covered_point = point->point;

  auto state = built.design.state();
  assert(occurrence.runtime_index < state.processes.size());
  auto& process = state.processes[occurrence.runtime_index];
  assert(!process.operations.empty());
  assert(runtime::simir::operation_get_if<runtime::simir::Halt>(
             &process.operations.back())
      != nullptr);
  const auto insertion_index = process.operations.size() - 1U;
  process.operations.insert(
      process.operations.cbegin()
          + static_cast<std::ptrdiff_t>(insertion_index),
      runtime::simir::CodeCoverageHit { covered_point.id,
          covered_point.metric, covered_point.counter });
  auto covered_design = elaboration::ElaboratedDesign::from_state(
      std::move(state));
  assert(covered_design);
  built.design = std::move(*covered_design);
}

Capture execute(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto built = fsim::app::build_project(config, diagnostics);
  if (!built) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(built && !diagnostics.has_error());
  assert(built->code_coverage_enabled);
  attach_statement_coverage(*built, config,
      config.source_sets.front().files.front());
  const auto& inventory = built->design.code_coverage_inventory();
  assert(inventory && inventory->total_points > 0U);
  assert(!built->specialization_cache_keys.empty());

  Capture capture;
  capture.design_cache_identity = built->cache_key;
  capture.native_cache_identities = built->specialization_cache_keys;

  fsim::app::Simulation simulation {
      std::move(*built), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();

  const auto value_signal = simulation.find_signal("top.value");
  assert(value_signal);
  auto& objects = simulation.systemverilog_vpi_objects();
  auto& coverage = simulation.systemverilog_vpi_coverage();
  const auto top = objects.find("top");
  assert(top && coverage.valid());
  using CoverageProperty =
      fsim::runtime::SystemVerilogVpiCoverageProperty;
  const auto statement_coverage = coverage.property(
      CoverageProperty::StatementCoverage, top.value->handle);
  assert(statement_coverage && statement_coverage.value == 1);
  const auto initial_count = coverage.property(
      CoverageProperty::CoveredCount, top.value->handle);
  assert(initial_count && initial_count.value == 0);

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter writer {
      vcd_output, std::string { simulation.time_resolution() }, 128};
  const auto trace_signal = writer.declare_signal("top.value", 8);
  writer.begin(simulation.now());
  writer.change(trace_signal, simulation.read_signal(*value_signal));

  const auto observer = simulation.add_signal_change_hook(
      [&](const auto signal, const auto& value,
          const auto time, const auto delta) {
        if (signal != *value_signal) {
          return;
        }
        const auto current_coverage = coverage.property(
            CoverageProperty::CoveredCount, top.value->handle);
        assert(current_coverage);
        capture.changes.push_back({
            time, delta, value.to_msb_string(), current_coverage.value});
        writer.set_time(time);
        writer.change(trace_signal, value);
      });

  simulation.await_all_native_compilation();
  capture.cache = simulation.native_cache_statistics();
  capture.result = simulation.run();
  simulation.remove_signal_change_hook(observer);

  const auto final_coverage = coverage.property(
      CoverageProperty::CoveredCount, top.value->handle);
  assert(final_coverage && final_coverage.value > 0);
  capture.statement_coverage = final_coverage.value;
  capture.final_value = simulation.read_signal(*value_signal).to_msb_string();
  writer.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

bool same_result(
    const fsim::runtime::RunResult& left,
    const fsim::runtime::RunResult& right) {
  return left.status == right.status && left.time == right.time
      && left.delta == right.delta
      && left.callbacks_executed == right.callbacks_executed
      && left.simulator_status == right.simulator_status;
}

void test_interpreter_and_cache_identity(
    const fsim::project::Config& config) {
  const auto interpreter = execute(
      config, fsim::app::SimulationEngine::interpreter);
  const auto cold = execute(config, fsim::app::SimulationEngine::compiled);
  const auto warm = execute(config, fsim::app::SimulationEngine::compiled);

  assert(interpreter.result.status == fsim::runtime::RunStatus::completed);
  assert(interpreter.result.time == 3U);
  assert(interpreter.final_value == "01111000");
  assert(interpreter.changes.size() == 4U);
  const std::array<std::string_view, 4> expected_values {
      "00010010", "00110100", "01010110", "01111000"};
  for (std::size_t index = 0; index < expected_values.size(); ++index) {
    assert(interpreter.changes[index].time == index);
    assert(interpreter.changes[index].value == expected_values[index]);
    if (index != 0U) {
      assert(interpreter.changes[index - 1U].covered_statements
          <= interpreter.changes[index].covered_statements);
    }
  }
  assert(interpreter.statement_coverage > 0);
  assert(interpreter.vcd.find("$var wire 8") != std::string::npos);
  assert(interpreter.vcd.find("b01111000") != std::string::npos);

  assert(same_result(interpreter.result, cold.result));
  assert(same_result(cold.result, warm.result));
  assert(interpreter.changes == cold.changes);
  assert(cold.changes == warm.changes);
  assert(interpreter.final_value == cold.final_value
      && cold.final_value == warm.final_value);
  assert(interpreter.vcd == cold.vcd && cold.vcd == warm.vcd);
  assert(interpreter.statement_coverage == cold.statement_coverage
      && cold.statement_coverage == warm.statement_coverage);
  assert(interpreter.design_cache_identity == cold.design_cache_identity
      && cold.design_cache_identity == warm.design_cache_identity);
  assert(interpreter.native_cache_identities
          == cold.native_cache_identities
      && cold.native_cache_identities == warm.native_cache_identities);

  assert(interpreter.compiled_processes == 0U);
  assert(cold.compiled_processes > 0U && cold.compiled_modules > 0U);
  assert(cold.cache.hits == 0U);
  assert(cold.cache.misses == cold.compiled_modules);
  assert(cold.cache.stores == cold.compiled_modules);
  assert(warm.compiled_processes == cold.compiled_processes);
  assert(warm.compiled_modules == cold.compiled_modules);
  assert(warm.cache.hits == warm.compiled_modules);
  assert(warm.cache.misses == 0U);
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory {
      std::filesystem::temp_directory_path()
      / ("fsim-coverage-observer-native-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "coverage_observer_native.sv";
  write_source(source);
  test_interpreter_and_cache_identity(make_config(directory.path, source));
}

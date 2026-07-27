// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

int main() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-application-test-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  const auto source = directory / "tb.sv";
  {
    std::ofstream output(source);
    output << R"(
module child(input logic value, output logic inverted);
  assign inverted = ~value;
endmodule

module tb;
  logic q;
  logic child_y;
  bit two_state;
  child u_child(.value(q), .inverted(child_y));
  initial begin
    q = 1'b0;
    #2 q = 1'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto scheduled_source = directory / "scheduled.sv";
  {
    std::ofstream output(scheduled_source);
    output << R"(
module scheduled;
  logic q;
  initial begin
    q <= 1'b0;
    q <= #2 1'b1;
    #3 $finish;
  end
endmodule

module scheduled_overflow;
  logic q;
  initial begin
    #1 q <= #18446744073709551615 1'b1;
  end
endmodule
)";
  }
  const auto sensitivity_source = directory / "sensitivity.sv";
  {
    std::ofstream output(sensitivity_source);
    output << R"(
module sensitivity;
  logic trigger;
  logic observed;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 trigger = 1'b0;
    #1 $finish;
  end
  always @(posedge trigger) observed <= trigger;
endmodule
)";
  }
  const auto partial_group_source = directory / "partial_group.sv";
  {
    std::ofstream output(partial_group_source);
    output << R"(
module partial_group;
  logic narrow;
  logic [64:0] wide;
  initial narrow = 1'b1;
  initial begin
    wide = 65'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto systemc_source = directory / "model.cpp";
  {
    std::ofstream output(systemc_source);
    output << R"(
#include <fsim/systemc_abi.h>

namespace {
void* create_model(void*, const char*, fsim_sc_handle_v1) {
  return reinterpret_cast<void*>(0x1);
}
void destroy_model(void*, void*) {}
}

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->register_factory == nullptr) {
    return FSIM_SC_ABI_MISMATCH;
  }
  return registrar->register_factory(
      registrar->context,
      "model",
      create_model,
      destroy_model,
      nullptr);
}
)";
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "application-test";
  config.project.top = "sv:work.tb";
  config.project.time_resolution = "1ns";
  config.build.cache_path = directory / "cache";
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  fsim::project::SourceSet systemc_sources;
  systemc_sources.language = fsim::project::Language::systemc;
  systemc_sources.standard = "2023-subset";
  systemc_sources.files.push_back(systemc_source);
  systemc_sources.include_directories.emplace_back(
      std::filesystem::path{FSIM_TEST_SOURCE_DIR} / "include");
  config.source_sets.push_back(std::move(systemc_sources));

  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  assert(checked);
  assert(checked->source_count == 2);
  assert(checked->parsed.units.size() == 2);

  auto first = fsim::app::build_project(config, diagnostics);
  assert(first);
  assert(first->systemc_plugins.size() == 1);
  assert(!first->cache_hit);
  auto second = fsim::app::build_project(config, diagnostics);
  assert(second);
  assert(second->cache_hit);
  const auto child_q = first->design.find_signal("tb.u_child.value");
  assert(child_q);
  const auto paths = first->design.signal_paths();
  assert(std::find_if(
             paths.begin(), paths.end(),
             [](const auto& path) {
               return path.first == "tb.u_child.value";
         })
         != paths.end());

  struct CapturedSimulation {
    fsim::runtime::RunResult result;
    std::vector<std::tuple<
        fsim::runtime::simir::SignalId,
        std::string,
        fsim::runtime::SimulationTick,
        std::uint64_t>> changes;
    std::vector<std::string> final_values;
    fsim::app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes{};
    std::size_t compiled_modules{};
    std::size_t process_count{};
  };
  const auto capture_simulation =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        CapturedSimulation captured;
        captured.process_count = project.design.processes().size();
        fsim::app::Simulation candidate(
            std::move(project), config.run.max_deltas, engine);
        captured.compiled_processes =
            candidate.compiled_process_count();
        captured.compiled_modules =
            candidate.compiled_module_count();
        captured.native_cache =
            candidate.native_cache_statistics();
        candidate.set_signal_change_hook(
            [&captured](
                const fsim::runtime::simir::SignalId signal,
                const fsim::runtime::PackedLogic4& value,
                const fsim::runtime::SimulationTick time,
                const std::uint64_t delta) {
              captured.changes.emplace_back(
                  signal, value.to_msb_string(), time, delta);
            });
        captured.result = candidate.run();
        captured.final_values.reserve(
            candidate.design().signals().size());
        for (const auto& signal : candidate.design().signals()) {
          captured.final_values.push_back(
              candidate.read_signal(signal.id).to_msb_string());
        }
        return captured;
      };
  const auto compare_captures =
      [](const CapturedSimulation& reference,
         const CapturedSimulation& hybrid) {
        assert(reference.result.status == hybrid.result.status);
        assert(reference.result.time == hybrid.result.time);
        assert(reference.result.delta == hybrid.result.delta);
        assert(
            reference.result.callbacks_executed
            == hybrid.result.callbacks_executed);
        assert(reference.changes == hybrid.changes);
        assert(reference.final_values == hybrid.final_values);
        assert(reference.compiled_processes == 0);
        assert(reference.compiled_modules == 0);
        assert(
            reference.native_cache
            == fsim::app::NativeCacheStatistics{});
#if defined(FSIM_HAS_LLVM)
        assert(hybrid.compiled_processes > 0);
        assert(
            hybrid.compiled_processes
            <= hybrid.process_count);
        assert(hybrid.compiled_modules > 0);
        assert(
            hybrid.compiled_modules
            <= hybrid.compiled_processes);
#else
        assert(hybrid.compiled_processes == 0);
        assert(hybrid.compiled_modules == 0);
#endif
      };

  auto differential_config = config;
  std::erase_if(
      differential_config.source_sets,
      [](const fsim::project::SourceSet& source_set) {
        return source_set.language
            == fsim::project::Language::systemc;
      });
  differential_config.build.cache_path =
      directory / "differential-cache";
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    differential_config.build.optimization = optimization;
    fsim::diagnostic::Engine differential_diagnostics;
    auto reference_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    auto hybrid_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    assert(reference_project);
    assert(hybrid_project);
    const auto reference = capture_simulation(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto hybrid = capture_simulation(
        std::move(*hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(reference, hybrid);
#if defined(FSIM_HAS_LLVM)
    assert(hybrid.process_count == 2);
    assert(
        hybrid.compiled_processes
        == hybrid.process_count);
    assert(hybrid.compiled_modules == 2);
#endif

    auto warm_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    assert(warm_project);
    const auto warm = capture_simulation(
        std::move(*warm_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(reference, warm);
#if defined(FSIM_HAS_LLVM)
    assert(
        hybrid.native_cache.misses
        == hybrid.compiled_modules);
    assert(
        hybrid.native_cache.stores
        == hybrid.compiled_modules);
    assert(hybrid.native_cache.hits == 0);
    assert(
        warm.native_cache.hits
        == warm.compiled_modules);
    assert(warm.native_cache.misses == 0);
    assert(warm.native_cache.load_failures == 0);
    assert(warm.native_cache.store_failures == 0);
#else
    assert(
        hybrid.native_cache
        == fsim::app::NativeCacheStatistics{});
    assert(
        warm.native_cache
        == fsim::app::NativeCacheStatistics{});
#endif
  }

  auto scheduled_config = config;
  scheduled_config.project.name = "scheduled-write-test";
  scheduled_config.project.top = "sv:work.scheduled";
  scheduled_config.build.optimization =
      fsim::project::Optimization::o2;
  scheduled_config.build.cache_path =
      directory / "scheduled-write-cache";
  scheduled_config.source_sets.clear();
  fsim::project::SourceSet scheduled_sources;
  scheduled_sources.language =
      fsim::project::Language::system_verilog;
  scheduled_sources.standard = "2017";
  scheduled_sources.library = "work";
  scheduled_sources.files.push_back(scheduled_source);
  scheduled_config.source_sets.push_back(
      std::move(scheduled_sources));
  fsim::diagnostic::Engine scheduled_diagnostics;
  auto scheduled_reference_project =
      fsim::app::build_project(
          scheduled_config, scheduled_diagnostics);
  auto scheduled_hybrid_project =
      fsim::app::build_project(
          scheduled_config, scheduled_diagnostics);
  assert(scheduled_reference_project);
  assert(scheduled_hybrid_project);
  const auto scheduled_reference = capture_simulation(
      std::move(*scheduled_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto scheduled_hybrid = capture_simulation(
      std::move(*scheduled_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      scheduled_reference, scheduled_hybrid);
  assert(scheduled_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
  assert(scheduled_hybrid.compiled_processes == 1);
  assert(scheduled_hybrid.compiled_modules == 1);
#endif
  assert(
      scheduled_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(scheduled_hybrid.result.time == 3);
  assert(scheduled_hybrid.final_values.size() == 1);
  assert(scheduled_hybrid.final_values.front() == "1");
  assert(scheduled_hybrid.changes.size() == 2);
  assert(
      std::get<1>(scheduled_hybrid.changes.front())
      == "0");
  assert(
      std::get<2>(scheduled_hybrid.changes.front())
      == 0);
  assert(
      std::get<3>(scheduled_hybrid.changes.front())
      == 0);
  assert(
      std::get<1>(scheduled_hybrid.changes.back())
      == "1");
  assert(
      std::get<2>(scheduled_hybrid.changes.back())
      == 2);
  assert(
      std::get<3>(scheduled_hybrid.changes.back())
      == 0);

  auto overflow_config = scheduled_config;
  overflow_config.project.name =
      "scheduled-write-overflow-test";
  overflow_config.project.top =
      "sv:work.scheduled_overflow";
  overflow_config.build.cache_path =
      directory / "scheduled-write-overflow-cache";
  for (const auto engine : {
           fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::compiled}) {
    fsim::diagnostic::Engine overflow_diagnostics;
    auto overflow_project = fsim::app::build_project(
        overflow_config, overflow_diagnostics);
    assert(overflow_project);
    fsim::app::Simulation overflow_simulation(
        std::move(*overflow_project),
        overflow_config.run.max_deltas,
        engine);
#if defined(FSIM_HAS_LLVM)
    assert(
        overflow_simulation.compiled_process_count()
        == (engine == fsim::app::SimulationEngine::compiled
                ? 1U
                : 0U));
#endif
    const auto overflow_q =
        overflow_simulation.find_signal("q");
    assert(overflow_q);
    bool overflow_thrown = false;
    try {
      (void)overflow_simulation.run();
    } catch (const std::overflow_error& exception) {
      overflow_thrown =
          std::string_view{exception.what()}
          == "simulation time overflow while scheduling event";
    }
    assert(overflow_thrown);
    assert(overflow_simulation.poisoned());
    assert(!overflow_simulation.finished());
    assert(overflow_simulation.now() == 1);
    assert(
        overflow_simulation.read_signal(*overflow_q)
            .to_msb_string()
        == "X");
  }

  auto sensitivity_config = config;
  sensitivity_config.project.name =
      "sensitivity-wakeup-test";
  sensitivity_config.project.top =
      "sv:work.sensitivity";
  sensitivity_config.build.optimization =
      fsim::project::Optimization::o2;
  sensitivity_config.build.cache_path =
      directory / "sensitivity-cache";
  sensitivity_config.source_sets.clear();
  fsim::project::SourceSet sensitivity_sources;
  sensitivity_sources.language =
      fsim::project::Language::system_verilog;
  sensitivity_sources.standard = "2017";
  sensitivity_sources.library = "work";
  sensitivity_sources.files.push_back(sensitivity_source);
  sensitivity_config.source_sets.push_back(
      std::move(sensitivity_sources));
  fsim::diagnostic::Engine sensitivity_diagnostics;
  auto sensitivity_reference_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  auto sensitivity_hybrid_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  assert(sensitivity_reference_project);
  assert(sensitivity_hybrid_project);
  const auto sensitivity_trigger =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.trigger");
  const auto sensitivity_observed =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.observed");
  assert(sensitivity_trigger);
  assert(sensitivity_observed);
  const auto sensitivity_reference = capture_simulation(
      std::move(*sensitivity_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto sensitivity_hybrid = capture_simulation(
      std::move(*sensitivity_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      sensitivity_reference, sensitivity_hybrid);
  assert(sensitivity_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(sensitivity_hybrid.compiled_processes == 2);
  assert(sensitivity_hybrid.compiled_modules == 1);
#endif
  assert(
      sensitivity_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(sensitivity_hybrid.result.time == 3);
  const decltype(sensitivity_reference.changes)
      expected_sensitivity_changes = {
          {*sensitivity_trigger, "0", 0, 0},
          {*sensitivity_trigger, "1", 1, 0},
          {*sensitivity_observed, "1", 1, 1},
          {*sensitivity_trigger, "0", 2, 0},
      };
  assert(
      sensitivity_reference.changes
      == expected_sensitivity_changes);
  assert(
      sensitivity_hybrid.changes
      == expected_sensitivity_changes);
  assert((
      sensitivity_hybrid.final_values
      == std::vector<std::string>{"0", "1"}));
#if defined(FSIM_HAS_LLVM)
  assert(sensitivity_hybrid.native_cache.hits == 0);
  assert(sensitivity_hybrid.native_cache.misses == 1);
  assert(sensitivity_hybrid.native_cache.stores == 1);
  auto sensitivity_warm_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  assert(sensitivity_warm_project);
  const auto sensitivity_warm = capture_simulation(
      std::move(*sensitivity_warm_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      sensitivity_reference, sensitivity_warm);
  assert(sensitivity_warm.compiled_processes == 2);
  assert(sensitivity_warm.compiled_modules == 1);
  assert(sensitivity_warm.native_cache.hits == 1);
  assert(sensitivity_warm.native_cache.misses == 0);
  assert(sensitivity_warm.native_cache.stores == 0);
#endif

  auto partial_group_config = config;
  partial_group_config.project.name =
      "partial-specialization-group-test";
  partial_group_config.project.top =
      "sv:work.partial_group";
  partial_group_config.build.optimization =
      fsim::project::Optimization::o2;
  partial_group_config.build.cache_path =
      directory / "partial-group-cache";
  partial_group_config.source_sets.clear();
  fsim::project::SourceSet partial_group_sources;
  partial_group_sources.language =
      fsim::project::Language::system_verilog;
  partial_group_sources.standard = "2017";
  partial_group_sources.library = "work";
  partial_group_sources.files.push_back(partial_group_source);
  partial_group_config.source_sets.push_back(
      std::move(partial_group_sources));
  fsim::diagnostic::Engine partial_group_diagnostics;
  auto partial_group_reference_project =
      fsim::app::build_project(
          partial_group_config, partial_group_diagnostics);
  auto partial_group_hybrid_project =
      fsim::app::build_project(
          partial_group_config, partial_group_diagnostics);
  assert(partial_group_reference_project);
  assert(partial_group_hybrid_project);
  const auto partial_group_reference = capture_simulation(
      std::move(*partial_group_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto partial_group_hybrid = capture_simulation(
      std::move(*partial_group_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      partial_group_reference, partial_group_hybrid);
  assert(partial_group_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(partial_group_hybrid.compiled_processes == 1);
  assert(partial_group_hybrid.compiled_modules == 1);
  assert(partial_group_hybrid.native_cache.misses == 1);
  assert(partial_group_hybrid.native_cache.stores == 1);
#endif
  assert(
      partial_group_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(partial_group_hybrid.result.time == 1);

  fsim::diagnostic::Engine mixed_diagnostics;
  const auto mixed_manifest =
      std::filesystem::path{FSIM_TEST_SOURCE_DIR}
      / "examples/vertical_slice/fsim.toml";
  auto mixed_config =
      fsim::project::load(mixed_manifest, mixed_diagnostics);
  assert(mixed_config);
  mixed_config->build.cache_path = directory / "mixed-cache";
  mixed_config->run.trace_file.reset();
  auto mixed_reference_project =
      fsim::app::build_project(*mixed_config, mixed_diagnostics);
  auto mixed_hybrid_project =
      fsim::app::build_project(*mixed_config, mixed_diagnostics);
  assert(mixed_reference_project);
  assert(mixed_hybrid_project);
  const auto mixed_reference = capture_simulation(
      std::move(*mixed_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto mixed_hybrid = capture_simulation(
      std::move(*mixed_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(mixed_reference, mixed_hybrid);
  assert(
      mixed_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(mixed_hybrid.result.time == 6);

  fsim::app::Simulation simulation(std::move(*first), config.run.max_deltas);
  const auto q = simulation.find_signal("q");
  const auto two_state = simulation.find_signal("two_state");
  assert(q && two_state);
  assert(simulation.read_signal(*two_state).to_msb_string() == "0");
  bool rejected_lossy_deposit = false;
  try {
    simulation.deposit_signal(
        *two_state,
        fsim::runtime::PackedLogic4::from_msb_string("X"));
  } catch (const std::invalid_argument&) {
    rejected_lossy_deposit = true;
  }
  assert(rejected_lossy_deposit);
  const auto result = simulation.run();
  assert(result.status == fsim::runtime::RunStatus::stopped);
  assert(result.time == 3);
  assert(simulation.finished());
  assert(!simulation.poisoned());
  assert(simulation.read_signal(*q).to_msb_string() == "1");

  simulation.force_signal(
      *q, fsim::runtime::PackedLogic4::from_msb_string("0"));
  simulation.deposit_signal(
      *q, fsim::runtime::PackedLogic4::from_msb_string("1"));
  assert(simulation.read_signal(*q).to_msb_string() == "0");
  simulation.release_signal(*q);
  assert(simulation.read_signal(*q).to_msb_string() == "1");

  std::string error;
  assert(fsim::app::parse_time("25ns", "1ns", error) == 25);
  assert(!fsim::app::parse_time("1ps", "1ns", error));
  const auto value = fsim::app::parse_value("10xz", 4, error);
  assert(value && value->to_msb_string() == "10XZ");

  auto compiled_debug_project =
      fsim::app::build_project(config, diagnostics);
  assert(compiled_debug_project);
  const std::string debug_commands =
      "scope\n"
      "scopes\n"
      "scope u_child\n"
      "signals\n"
      "show value\n"
      "scope ..\n"
      "break signal q\n"
      "break time 1ns\n"
      "breakpoints\n"
      "continue\n"
      "delete 1\n"
      "continue\n"
      "delete 2\n"
      "run-until 2ns\n"
      "where\n"
      "break signal child_y\n"
      "clear\n"
      "breakpoints\n"
      "step statement\n"
      "continue\n"
      "continue\n"
      "step delta\n"
      "quit\n";
  fsim::app::Simulation debug_simulation(
      std::move(*second),
      config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter);
  assert(debug_simulation.compiled_process_count() == 0);
  std::size_t observed_changes = 0;
  debug_simulation.set_signal_change_hook(
      [&observed_changes](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) { ++observed_changes; });
  debug_simulation.start();
  std::istringstream debug_input{debug_commands};
  std::ostringstream debug_output;
  std::ostringstream debug_error;
  assert(
      fsim::app::run_debug_repl(
          debug_simulation, debug_input, debug_output, debug_error)
      == 0);
  assert(debug_error.str().empty());
  const auto transcript = debug_output.str();
  assert(transcript.find("tb.u_child") != std::string::npos);
  assert(
      transcript.find("tb.u_child.value = X") != std::string::npos);
  assert(
      transcript.find("breakpoint 1 set on tb.q") != std::string::npos);
  assert(
      transcript.find("breakpoint 2 set at time 1") != std::string::npos);
  assert(
      transcript.find("hit breakpoint 1: tb.q changed to 0 at time 0")
      != std::string::npos);
  assert(
      transcript.find("hit breakpoint 2: time 1") != std::string::npos);
  assert(transcript.find("stopped at time 2") != std::string::npos);
  assert(transcript.find("time 2, delta") != std::string::npos);
  assert(transcript.find("cleared all breakpoints") != std::string::npos);
  assert(transcript.find("no breakpoints") != std::string::npos);
  assert(
      transcript.find(
          "statement and process stepping require debug SimIR source maps")
      != std::string::npos);
  assert(
      transcript.find("simulation finished at time 3")
      != std::string::npos);
  const auto first_finished =
      transcript.find("simulation has finished");
  assert(first_finished != std::string::npos);
  assert(
      transcript.find("simulation has finished", first_finished + 1)
      != std::string::npos);
  assert(debug_simulation.finished());
  assert(!debug_simulation.poisoned());
  assert(observed_changes > 0);

  fsim::app::Simulation compiled_debug_simulation(
      std::move(*compiled_debug_project),
      config.run.max_deltas,
      fsim::app::SimulationEngine::debug);
#if defined(FSIM_HAS_LLVM)
  assert(compiled_debug_simulation.compiled_process_count() == 2);
  assert(compiled_debug_simulation.compiled_module_count() == 2);
  const auto debug_native_cache =
      compiled_debug_simulation.native_cache_statistics();
  // The same specialization modules were already cached at the configured O2
  // run setting. Cold objects here therefore prove that debug forces O0.
  assert(debug_native_cache.hits == 0);
  assert(debug_native_cache.misses == 2);
  assert(debug_native_cache.stores == 2);
#else
  assert(compiled_debug_simulation.compiled_process_count() == 0);
  assert(compiled_debug_simulation.compiled_module_count() == 0);
#endif
  std::size_t compiled_observed_changes = 0;
  compiled_debug_simulation.set_signal_change_hook(
      [&compiled_observed_changes](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) { ++compiled_observed_changes; });
  compiled_debug_simulation.start();
  std::istringstream compiled_debug_input{debug_commands};
  std::ostringstream compiled_debug_output;
  std::ostringstream compiled_debug_error;
  assert(
      fsim::app::run_debug_repl(
          compiled_debug_simulation,
          compiled_debug_input,
          compiled_debug_output,
          compiled_debug_error)
      == 0);
  assert(compiled_debug_error.str().empty());
  assert(compiled_debug_output.str() == transcript);
  assert(compiled_debug_simulation.finished());
  assert(!compiled_debug_simulation.poisoned());
  assert(compiled_observed_changes == observed_changes);
  for (const auto& signal : debug_simulation.design().signals()) {
    assert(
        compiled_debug_simulation.read_signal(signal.id)
        == debug_simulation.read_signal(signal.id));
  }

  auto poisoned_project = fsim::app::build_project(config, diagnostics);
  assert(poisoned_project);
  fsim::app::Simulation poisoned_simulation(
      std::move(*poisoned_project), config.run.max_deltas);
#if defined(FSIM_HAS_LLVM)
  assert(poisoned_simulation.compiled_process_count() > 0);
#else
  assert(poisoned_simulation.compiled_process_count() == 0);
#endif
  poisoned_simulation.set_signal_change_hook(
      [](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) {
        throw std::runtime_error("fatal signal observer");
      });
  poisoned_simulation.start();
  std::istringstream poisoned_input{
      "continue\n"
      "continue\n"
      "step delta\n"
      "quit\n"};
  std::ostringstream poisoned_output;
  std::ostringstream poisoned_error;
  assert(
      fsim::app::run_debug_repl(
          poisoned_simulation,
          poisoned_input,
          poisoned_output,
          poisoned_error)
      == 0);
  assert(poisoned_simulation.poisoned());
  assert(!poisoned_simulation.finished());
  assert(
      poisoned_error.str().find("fatal signal observer")
      != std::string::npos);
  const auto unavailable =
      poisoned_output.str().find(
          "simulation is unavailable after a fatal runtime error");
  assert(unavailable != std::string::npos);
  assert(
      poisoned_output.str().find(
          "simulation is unavailable after a fatal runtime error",
          unavailable + 1)
      != std::string::npos);

  const auto manifest = directory / "fsim.toml";
  {
    std::ofstream output(manifest);
    output << R"(
schema = 1

[project]
name = "debug-cli-test"
top = "sv:work.tb"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "cli-cache"

[run]
max_deltas = 1000
)";
  }
  std::istringstream cli_input{"where\nquit\n"};
  std::ostringstream cli_output;
  std::ostringstream cli_error;
  auto services = fsim::app::make_cli_services(cli_input);
  const auto manifest_text = manifest.string();
  const std::vector<const char*> arguments{
      "fsim", "debug", "-p", manifest_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(arguments.size()),
          arguments.data(),
          services,
          cli_output,
          cli_error)
      == 0);
  assert(
      cli_output.str().find("fsim debugger: tb") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
  assert(
      cli_output.str().find(
          "(O0 hybrid, 2 compiled process(es) in "
          "2 specialization module(s))")
      != std::string::npos);
#else
  assert(
      cli_output.str().find("(reference evaluator)")
      != std::string::npos);
#endif
  assert(
      cli_output.str().find("time 0, delta 0, scope tb")
      != std::string::npos);

  const auto differently_named = directory / "different_filename.sv";
  {
    std::ofstream output(differently_named);
    output << "module actual_top; endmodule\n";
  }
  std::ostringstream direct_output;
  std::ostringstream direct_error;
  const auto direct_text = differently_named.string();
  const std::vector<const char*> direct_arguments{
      "fsim", "run", direct_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(direct_arguments.size()),
          direct_arguments.data(),
          services,
          direct_output,
          direct_error)
      == 0);
  assert(
      direct_output.str().find("simulation completed at tick 0")
      != std::string::npos);

  std::ostringstream json_output;
  std::ostringstream json_error;
  const std::vector<const char*> json_arguments{
      "fsim",
      "check",
      "--diagnostics=json",
      "--definitely-invalid"};
  assert(
      fsim::cli::run(
          static_cast<int>(json_arguments.size()),
          json_arguments.data(),
          services,
          json_output,
          json_error)
      == 2);
  assert(
      json_error.str().find("\"code\":\"FSIM-CLI-0001\"")
      != std::string::npos);

  std::ostringstream standard_output;
  std::ostringstream standard_error;
  const std::vector<const char*> standard_arguments{
      "fsim",
      "check",
      "--standard=bogus",
      direct_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(standard_arguments.size()),
          standard_arguments.data(),
          services,
          standard_output,
          standard_error)
      == 1);
  assert(
      standard_error.str().find("unsupported standard 'bogus'")
      != std::string::npos);

  const auto scaled_manifest = directory / "scaled.toml";
  const auto scaled_trace = directory / "scaled.vcd";
  {
    std::ofstream output(scaled_manifest);
    output << R"(
schema = 1
[project]
name = "scaled-vcd"
top = "sv:work.tb"
time_resolution = "2ps"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "scaled-cache"

[run]
max_deltas = 1000
trace_file = "scaled.vcd"
)";
  }
  std::ostringstream scaled_output;
  std::ostringstream scaled_error;
  const auto scaled_manifest_text = scaled_manifest.string();
  const std::vector<const char*> scaled_arguments{
      "fsim", "run", "-p", scaled_manifest_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(scaled_arguments.size()),
          scaled_arguments.data(),
          services,
          scaled_output,
          scaled_error)
      == 0);
  std::ifstream scaled_stream(scaled_trace);
  const std::string scaled_vcd{
      std::istreambuf_iterator<char>{scaled_stream},
      std::istreambuf_iterator<char>{}};
  assert(
      scaled_vcd.find("$timescale 1ps $end") != std::string::npos);
  assert(scaled_vcd.find("#4") != std::string::npos);

  const auto timescale_source = directory / "timescale.sv";
  {
    std::ofstream output(timescale_source);
    output << R"(`timescale 10ns/100ps
module timed;
  initial #2 $finish;
endmodule
)";
  }
  fsim::project::Config timescale_config;
  timescale_config.base_directory = directory;
  timescale_config.project.name = "timescale";
  timescale_config.project.top = "sv:work.timed";
  timescale_config.project.time_resolution = "auto";
  timescale_config.build.cache_path = directory / "timescale-cache";
  timescale_config.run.max_deltas = 1000;
  fsim::project::SourceSet timescale_sources;
  timescale_sources.language =
      fsim::project::Language::system_verilog;
  timescale_sources.standard = "2017";
  timescale_sources.library = "work";
  timescale_sources.files.push_back(timescale_source);
  timescale_config.source_sets.push_back(
      std::move(timescale_sources));
  fsim::diagnostic::Engine timescale_diagnostics;
  auto timed_project =
      fsim::app::build_project(
          timescale_config, timescale_diagnostics);
  assert(timed_project);
  assert(timed_project->time_resolution == "100ps");
  fsim::app::Simulation timed_simulation(
      std::move(*timed_project),
      timescale_config.run.max_deltas);
  const auto timed_result = timed_simulation.run();
  assert(timed_result.status == fsim::runtime::RunStatus::stopped);
  assert(timed_result.time == 200);
  timescale_config.project.time_resolution = "1ns";
  fsim::diagnostic::Engine coarse_time_diagnostics;
  assert(!fsim::app::build_project(
      timescale_config, coarse_time_diagnostics));
  assert(coarse_time_diagnostics.has_error());

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  std::cout << "application tests passed\n";
}

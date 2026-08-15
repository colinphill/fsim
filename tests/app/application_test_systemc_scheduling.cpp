// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <array>
#include <cassert>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_systemc_scheduling_matrix() {
  // FSIM-CONFORMANCE CF-SC-THREAD-001 source=SRC-SYSTEMC expectation=execute
  // FSIM-CONFORMANCE CF-SC-EVENT-001 source=SRC-SYSTEMC expectation=execute
  // FSIM-CONFORMANCE CF-SC-SIGNAL-001 source=SRC-SYSTEMC expectation=execute
  // FSIM-CONFORMANCE CF-SC-UPDATE-001 source=SRC-SYSTEMC expectation=execute
  constexpr std::array<std::string_view, 11> signal_names{
      "count",
      "timed",
      "thread_event_count",
      "static_count",
      "named_count",
      "timeout_count",
      "channel_value",
      "channel_updates",
      "channel_event_count",
      "cross_updates",
      "ready"};
  const std::vector<std::string> expected_values{
      "00000001",
      "00000011",
      "00000010",
      "00000001",
      "00000010",
      "00000100",
      "00000100",
      "00000011",
      "00000001",
      "00000001",
      "1"};

  struct MatrixCapture {
    fsim::runtime::RunResult result;
    std::vector<std::string> values;
    fsim::app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes{};
  };

  const auto make_config =
      [&](const bool vhdl,
          const fsim::project::Optimization optimization,
          const std::string_view cache_name) {
        auto config = base_config();
        config.project.top =
            vhdl
                ? "vhdl:work.systemc_schedule_lifecycle_vhdl_host(rtl)"
                : "sv:work.systemc_schedule_lifecycle_host";
        config.source_sets.front().language =
            vhdl ? fsim::project::Language::vhdl
                 : fsim::project::Language::system_verilog;
        config.source_sets.front().standard = vhdl ? "2008" : "2017";
        config.source_sets.front().files = {
            vhdl ? systemc_method_vhdl_source
                 : systemc_boundary_source};
        const auto prefix =
            vhdl ? "systemc_schedule_lifecycle_vhdl_host"
                 : "systemc_schedule_lifecycle_host";
        config.bindings = {
            {std::string{prefix} + ".u_threads",
             "systemc:models.accellera_threads",
             std::nullopt},
            {std::string{prefix} + ".u_channels",
             "systemc:models.kernel_channels",
             std::nullopt},
            {std::string{prefix} + ".u_lifecycle",
             "systemc:models.lifecycle_module",
             std::nullopt}};
        config.build.optimization = optimization;
        config.build.cache_path = directory / std::string{cache_name};
        return config;
      };

  const auto run =
      [&](fsim::app::BuiltProject project,
          const fsim::project::Config& config,
          const fsim::app::SimulationEngine engine) {
        std::vector<fsim::runtime::simir::SignalId> signals;
        signals.reserve(signal_names.size());
        for (const auto name : signal_names) {
          const auto signal = project.design.find_signal(name);
          assert(signal);
          signals.push_back(*signal);
        }
        fsim::app::Simulation simulation{
            std::move(project), config.run.max_deltas, engine};
        MatrixCapture capture;
        capture.result = simulation.run();
        capture.values.reserve(signals.size());
        for (const auto signal : signals) {
          capture.values.push_back(
              simulation.read_signal(signal).to_msb_string());
        }
        capture.native_cache = simulation.native_cache_statistics();
        capture.compiled_processes = simulation.compiled_process_count();
        return capture;
      };

  const auto require_semantics =
      [&](const MatrixCapture& capture, const bool vhdl) {
        assert(
            capture.result.status
            == (vhdl ? fsim::runtime::RunStatus::completed
                     : fsim::runtime::RunStatus::stopped));
        assert(capture.result.time == 5);
        if (capture.values != expected_values) {
          std::ostringstream mismatch;
          mismatch << "unexpected SystemC scheduling values:";
          for (const auto& value : capture.values) {
            mismatch << ' ' << value;
          }
          throw std::runtime_error{mismatch.str()};
        }
      };
  const auto require_equivalent =
      [&](const MatrixCapture& actual,
          const MatrixCapture& reference) {
        assert(actual.result.status == reference.result.status);
        assert(actual.result.time == reference.result.time);
        assert(
            actual.result.callbacks_executed
            == reference.result.callbacks_executed);
        assert(actual.values == reference.values);
      };

  const auto run_reference = [&](const bool vhdl) {
    const auto config = make_config(
        vhdl,
        fsim::project::Optimization::o0,
        vhdl ? "schedule-vhdl-reference" : "schedule-sv-reference");
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project && !diagnostics.has_error());
    auto capture = run(
        std::move(*project),
        config,
        fsim::app::SimulationEngine::interpreter);
    require_semantics(capture, vhdl);
    return capture;
  };
  const auto sv_reference = run_reference(false);
  const auto vhdl_reference = run_reference(true);

  const auto run_compiled_pair =
      [&](const bool vhdl,
          const fsim::project::Optimization optimization,
          const std::string_view cache_name,
          const MatrixCapture& reference) {
        const auto config = make_config(vhdl, optimization, cache_name);
        fsim::diagnostic::Engine diagnostics;
        auto cold_project = fsim::app::build_project(config, diagnostics);
        assert(cold_project && !diagnostics.has_error());
        auto cold = run(
            std::move(*cold_project),
            config,
            fsim::app::SimulationEngine::compiled);
        auto warm_project = fsim::app::build_project(config, diagnostics);
        assert(warm_project && !diagnostics.has_error());
        auto warm = run(
            std::move(*warm_project),
            config,
            fsim::app::SimulationEngine::compiled);
        require_equivalent(cold, reference);
        require_equivalent(warm, cold);
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes > 0);
        assert(cold.native_cache.misses > 0);
        assert(cold.native_cache.stores > 0);
        assert(warm.compiled_processes == cold.compiled_processes);
        assert(warm.native_cache.hits > 0);
        assert(warm.native_cache.misses == 0);
#else
        assert(cold.compiled_processes == 0);
        assert(warm.compiled_processes == 0);
#endif
      };

  run_compiled_pair(
      false,
      fsim::project::Optimization::o0,
      "schedule-sv-o0",
      sv_reference);
  run_compiled_pair(
      false,
      fsim::project::Optimization::o2,
      "schedule-sv-o2",
      sv_reference);
  run_compiled_pair(
      true,
      fsim::project::Optimization::o0,
      "schedule-vhdl-o0",
      vhdl_reference);
  run_compiled_pair(
      true,
      fsim::project::Optimization::o2,
      "schedule-vhdl-o2",
      vhdl_reference);

  const auto trace = directory / "systemc-scheduling-matrix.vcd";
  auto debug_config = make_config(
      false,
      fsim::project::Optimization::o0,
      "schedule-debug");
  debug_config.run.trace_file = trace;
  fsim::diagnostic::Engine debug_diagnostics;
  auto debug_project = fsim::app::build_project(
      debug_config, debug_diagnostics);
  assert(debug_project);
  auto debug_warm_project = fsim::app::build_project(
      debug_config, debug_diagnostics);
  assert(debug_warm_project);
  std::ostringstream debug_output;
  std::ostringstream debug_error;
  const auto thread_prefix =
      std::string{"systemc_schedule_lifecycle_host.u_threads"};
  fsim::app::NativeCacheStatistics debug_cold_cache;
  std::size_t debug_cold_processes = 0;
  {
    fsim::app::Simulation simulation{
        std::move(*debug_project),
        debug_config.run.max_deltas,
        fsim::app::SimulationEngine::debug};
    debug_cold_cache = simulation.native_cache_statistics();
    debug_cold_processes = simulation.compiled_process_count();
    fsim::app::DebuggerControl debugger{
        simulation,
        debug_output,
        debug_error,
        debug_config,
        debug_diagnostics};
    debugger.execute({"scope", thread_prefix});
    debugger.execute({"scopes"});
    debugger.execute({"signals", thread_prefix});
    debugger.execute({"run"});
  }
  fsim::app::Simulation debug_warm_simulation{
      std::move(*debug_warm_project),
      debug_config.run.max_deltas,
      fsim::app::SimulationEngine::debug};
  const auto debug_warm_cache =
      debug_warm_simulation.native_cache_statistics();
  const auto debug_warm_processes =
      debug_warm_simulation.compiled_process_count();
  const auto debug_warm_result = debug_warm_simulation.run();
  assert(debug_warm_result.status == sv_reference.result.status);
#if defined(FSIM_HAS_LLVM)
  assert(debug_cold_processes > 0);
  assert(debug_cold_cache.hits == 0);
  assert(debug_cold_cache.misses > 0);
  assert(debug_cold_cache.stores > 0);
  assert(debug_warm_processes == debug_cold_processes);
  assert(debug_warm_cache.hits > 0);
  assert(debug_warm_cache.misses == 0);
#else
  (void)debug_cold_cache;
  (void)debug_warm_cache;
  assert(debug_cold_processes == 0);
  assert(debug_warm_processes == 0);
#endif
  assert(!debug_diagnostics.has_error());
  assert(debug_error.str().empty());
  assert(debug_output.str().find(thread_prefix) != std::string::npos);
  std::ifstream trace_input(trace, std::ios::binary);
  const std::string trace_text{
      std::istreambuf_iterator<char>{trace_input},
      std::istreambuf_iterator<char>{}};
  assert(
      trace_text.find("$scope module u_threads $end")
      != std::string::npos);
  assert(trace_text.find(" thread_event_count $end") != std::string::npos);
  assert(trace_text.find(" channel_updates $end") != std::string::npos);
  assert(trace_text.find(" ready $end") != std::string::npos);

  const auto run_failure =
      [&](const std::string_view target,
          const std::string_view expected) {
        auto config = make_config(
            false,
            fsim::project::Optimization::o2,
            "schedule-negative");
        config.project.top = std::string{target};
        config.bindings.clear();
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        assert(project && !diagnostics.has_error());
        fsim::app::Simulation simulation{
            std::move(*project),
            config.run.max_deltas,
            fsim::app::SimulationEngine::compiled};
        bool rejected = false;
        std::string failure;
        try {
          (void)simulation.run();
        } catch (const std::runtime_error& error) {
          failure = error.what();
          rejected =
              std::string_view{failure}.find(expected)
              != std::string_view::npos;
        }
        if (!rejected) {
          throw std::runtime_error{
              "SystemC negative case '" + std::string{target}
              + "' did not report '" + std::string{expected}
              + "': " + (failure.empty() ? "no failure" : failure)};
        }
        assert(simulation.poisoned());
      };
  run_failure(
      "systemc:models.channel_update_failure",
      "intentional channel update failure");
  run_failure(
      "systemc:models.lifecycle_suspend_failure",
      "next_trigger() is only allowed in SC_METHODs");
  run_failure(
      "systemc:models.throwing_end_lifecycle",
      "intentional lifecycle terminal failure");

  {
    std::ofstream edited_source(systemc_source, std::ios::app);
    edited_source << "\n// scheduling/lifecycle matrix cache edit\n";
  }
  const auto edited_config = make_config(
      false,
      fsim::project::Optimization::o2,
      "schedule-sv-o2");
  fsim::diagnostic::Engine edited_diagnostics;
  auto edited_project = fsim::app::build_project(
      edited_config, edited_diagnostics);
  assert(edited_project && !edited_project->cache_hit);
  auto edited_capture = run(
      std::move(*edited_project),
      edited_config,
      fsim::app::SimulationEngine::compiled);
  require_equivalent(edited_capture, sv_reference);
#if defined(FSIM_HAS_LLVM)
  assert(edited_capture.native_cache.hits == 0);
  assert(edited_capture.native_cache.misses > 0);
  assert(edited_capture.native_cache.stores > 0);
#endif

  const auto teardown_config = make_config(
      false,
      fsim::project::Optimization::o0,
      "schedule-teardown");
  fsim::diagnostic::Engine teardown_diagnostics;
  auto teardown_project = fsim::app::build_project(
      teardown_config, teardown_diagnostics);
  assert(teardown_project && !teardown_diagnostics.has_error());
  const auto teardown_capture = run(
      std::move(*teardown_project),
      teardown_config,
      fsim::app::SimulationEngine::interpreter);
  require_equivalent(teardown_capture, sv_reference);
}

}  // namespace fsim::test

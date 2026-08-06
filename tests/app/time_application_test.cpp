// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
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

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<std::tuple<
      std::string,
      fsim::runtime::SimulationTick,
      std::uint64_t>> changes;
  std::string final_value;
  std::string vcd;
  std::string resolution;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

struct ScalarSurfaceCapture {
  std::array<std::uint64_t, 7> payloads{};
  std::string callable_checks;
  std::size_t change_count{};
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    std::string resolution = "auto") {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "time-rounding";
  config.project.top = "sv:work.time_rounding";
  config.project.time_resolution = std::move(resolution);
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  Capture capture;
  capture.resolution = project->time_resolution;
  fsim::app::Simulation simulation{
      std::move(*project), 1000, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();
  const auto marker =
      simulation.find_signal("time_rounding.marker");
  assert(marker);

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
  const auto vcd_marker = vcd.declare_signal("time_rounding.marker", 4);
  vcd.begin(simulation.now());
  vcd.change(vcd_marker, simulation.read_signal(*marker));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        if (signal != *marker) {
          return;
        }
        capture.changes.emplace_back(
            value.to_msb_string(), time, delta);
        vcd.set_time(time);
        vcd.change(vcd_marker, value);
      });
  capture.result = simulation.run();
  capture.final_value =
      simulation.read_signal(*marker).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto config = config_for(
      directory, source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 2835);
  assert(reference.resolution == "1ps");
  assert(reference.final_value == "0101");
  assert(reference.changes.size() == 6);
  constexpr std::array expected_times{
      fsim::runtime::SimulationTick{0},
      fsim::runtime::SimulationTick{0},
      fsim::runtime::SimulationTick{1},
      fsim::runtime::SimulationTick{1235},
      fsim::runtime::SimulationTick{1835},
      fsim::runtime::SimulationTick{2835},
  };
  for (std::size_t index = 0; index < expected_times.size(); ++index) {
    assert(std::get<1>(reference.changes[index]) == expected_times[index]);
  }
  assert(reference.vcd.find("#1235") != std::string::npos);
  assert(reference.vcd.find("#2835") != std::string::npos);
  assert(reference.result.status == cold.result.status);
  assert(reference.result.time == cold.result.time);
  assert(reference.changes == cold.changes);
  assert(reference.final_value == cold.final_value);
  assert(reference.vcd == cold.vcd);
  assert(reference.result.status == warm.result.status);
  assert(reference.result.time == warm.result.time);
  assert(reference.changes == warm.changes);
  assert(reference.final_value == warm.final_value);
  assert(reference.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 1);
  assert(cold.native_cache.hits == 0);
  assert(cold.native_cache.misses == 1);
  assert(cold.native_cache.stores == 1);
  assert(warm.compiled_processes == 1);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
}

void verify_resolution_diagnostic(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  auto config = config_for(
      directory,
      source,
      fsim::project::Optimization::o2,
      "10ps");
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(config, diagnostics);
  assert(!project);
  assert(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-TIME-0004";
          }));
}

void verify_overflow_diagnostic(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1s / 1s;
module time_rounding;
  trireg (small) #18446744073709551615 retained;
  assign retained = 1'b1;
endmodule
)";
    assert(output.good());
  }
  const auto config = config_for(
      directory,
      source,
      fsim::project::Optimization::o2,
      "1fs");
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(config, diagnostics);
  assert(!project);
  assert(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-TIME-0003"
                && diagnostic.message.find("overflows")
                    != std::string::npos;
          }));
}

ScalarSurfaceCapture verify_scalar_surfaces(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine,
    const std::string_view identity) {
  auto config = config_for(directory, source, optimization, "1ps");
  config.project.name = "scalar-surfaces";
  config.project.top = "sv:work.scalar_surfaces";
  config.build.cache_path = directory / ("scalar-cache-" + std::string{identity});
  config.run.trace_file = directory / ("scalar-" + std::string{identity} + ".vcd");
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  fsim::app::Simulation simulation{std::move(*project), 1000, engine};
  const auto real_signal = simulation.find_signal("scalar_surfaces.r");
  const auto short_signal = simulation.find_signal("scalar_surfaces.s");
  const auto realtime_signal = simulation.find_signal("scalar_surfaces.rt");
  const auto time_signal = simulation.find_signal("scalar_surfaces.ticks");
  const auto chandle_signal = simulation.find_signal("scalar_surfaces.foreign");
  const auto callable_checks = simulation.find_signal(
      "scalar_surfaces.callable_checks");
  const auto offset_result = simulation.find_signal(
      "scalar_surfaces.offset_result");
  const auto scaled_result = simulation.find_signal(
      "scalar_surfaces.scaled_result");
  assert(
      real_signal && short_signal && realtime_signal && time_signal
      && chandle_signal && callable_checks && offset_result && scaled_result);
  assert(simulation.read_scalar_signal(*real_signal).as_real() == 0.0);

  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  std::vector<std::pair<fsim::runtime::simir::SignalId,
                        fsim::runtime::SystemVerilogScalarValue>> changes;
  std::vector<fsim::runtime::SystemVerilogChandleEvent> chandle_events;
  ScalarSurfaceCapture capture;
  const auto chandle_observer = simulation.chandle_registry().add_observer(
      [&](const auto& event) { chandle_events.push_back(event); });
  const auto foreign = simulation.chandle_registry().create(
      {"dpi:test-resource", "fixture-resource", {}});
  {
    fsim::app::DebuggerControl debugger(
        simulation, debugger_output, debugger_error, config, diagnostics);
    debugger.execute({"trace", "all"});
    simulation.set_scalar_signal_change_hook(
        [&](const auto signal, const auto& value, const auto, const auto) {
          changes.emplace_back(signal, value);
        });
    simulation.deposit_scalar_signal(
        *real_signal,
        fsim::runtime::SystemVerilogScalarValue::real(-0.0));
    assert(
        simulation.read_scalar_signal(*real_signal).bits
        == UINT64_C(0x8000000000000000));
    simulation.deposit_scalar_signal(
        *short_signal,
        fsim::runtime::SystemVerilogScalarValue::shortreal(-1.25F));
    simulation.deposit_scalar_signal(
        *realtime_signal,
        fsim::runtime::SystemVerilogScalarValue::realtime(2.5));
    simulation.deposit_scalar_signal(
        *time_signal,
        fsim::runtime::SystemVerilogScalarValue::time(
            UINT64_C(9007199254740993)));
    simulation.deposit_scalar_signal(
        *chandle_signal,
        fsim::runtime::SystemVerilogScalarValue::chandle(foreign));
    debugger.execute({"show", "scalar_surfaces.r"});
    debugger.execute({"deposit", "scalar_surfaces.r", "1.25"});
    debugger.execute({"show", "scalar_surfaces.r"});
    debugger.execute({
        "force", "scalar_surfaces.ticks", "9007199254740995"});
    debugger.execute({"show", "scalar_surfaces.ticks"});
    debugger.execute({"release", "scalar_surfaces.ticks"});
    debugger.execute({"show", "scalar_surfaces.foreign"});
    debugger.execute({"chandles"});
    debugger.execute({"chandle", std::to_string(foreign)});
    debugger.execute({
        "deposit", "scalar_surfaces.foreign", std::to_string(foreign)});
    const auto snapshots = simulation.scalar_signal_snapshots();
    assert(snapshots.size() == 7);
    assert(
        std::ranges::any_of(snapshots, [&](const auto& snapshot) {
          return snapshot.signal == *time_signal
              && snapshot.value.bits == UINT64_C(9007199254740993);
        }));
    assert(
        std::ranges::any_of(snapshots, [&](const auto& snapshot) {
          return snapshot.signal == *chandle_signal
              && snapshot.value.as_chandle() == foreign;
        }));
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::stopped);
    assert(result.time == 1000);
    const auto callable_result =
        simulation.read_signal(*callable_checks).to_msb_string();
    if (callable_result != "11111111") {
      std::cerr << identity << " callable checks = "
                << callable_result << ", offset = "
                << *simulation.read_scalar_signal(*offset_result).as_real()
                << ", scaled = "
                << *simulation.read_scalar_signal(*scaled_result).as_real()
                << '\n';
    }
    assert(callable_result == "11111111");
    capture.callable_checks = callable_result;
    const std::array scalar_signals{
        *real_signal, *short_signal, *realtime_signal, *time_signal,
        *chandle_signal, *offset_result, *scaled_result};
    for (std::size_t index = 0; index < scalar_signals.size(); ++index) {
      capture.payloads[index] =
          simulation.read_scalar_signal(scalar_signals[index]).bits;
    }
    assert(simulation.chandle_registry().release(foreign));
    debugger.execute({"show", "scalar_surfaces.foreign"});
    debugger.execute({
        "deposit", "scalar_surfaces.foreign", std::to_string(foreign)});
    bool stale_deposit_rejected{};
    try {
      simulation.deposit_scalar_signal(
          *chandle_signal,
          fsim::runtime::SystemVerilogScalarValue::chandle(foreign));
    } catch (const std::out_of_range&) {
      stale_deposit_rejected = true;
    }
    assert(stale_deposit_rejected);
  }
  assert(simulation.chandle_registry().remove_observer(chandle_observer));
  assert(changes.size() >= 8);
  capture.change_count = changes.size();
  assert(
      chandle_events.size() == 4
          && chandle_events.front().kind
              == fsim::runtime::SystemVerilogChandleEventKind::Created
          && chandle_events.back().kind
              == fsim::runtime::SystemVerilogChandleEventKind::Released);
  assert(debugger_error.str().empty());
  assert(debugger_output.str().find("scalar_surfaces.r = -0")
         != std::string::npos);
  assert(debugger_output.str().find("scalar_surfaces.r = 1.25")
         != std::string::npos);
  assert(debugger_output.str().find("9007199254740995 (forced)")
         != std::string::npos);
  assert(debugger_output.str().find("dpi:test-resource")
         != std::string::npos);
  assert(debugger_output.str().find("fixture-resource")
         != std::string::npos);
  assert(debugger_output.str().find("stale chandle <opaque ")
         != std::string::npos);
  assert(debugger_output.str().find("invalid or stale chandle value")
         != std::string::npos);
  std::ifstream trace(*config.run.trace_file, std::ios::binary);
  const std::string vcd{
      std::istreambuf_iterator<char>{trace},
      std::istreambuf_iterator<char>{}};
  assert(vcd.find("$var real 1") != std::string::npos);
  assert(vcd.find("$var wire 64") != std::string::npos);
  assert(
      vcd.find("$var wire 64", vcd.find("$var wire 64") + 1)
      != std::string::npos);
  assert(vcd.find("r-0 ") != std::string::npos);
  assert(
      vcd.find(
          "b0000000000100000000000000000000000000000000000000000000000000001")
      != std::string::npos);
#if defined(FSIM_HAS_LLVM)
  if (engine == fsim::app::SimulationEngine::compiled) {
    assert(simulation.compiled_process_count() == 1);
  }
#endif
  return capture;
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-time-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "time_rounding.sv";
  const auto scalar_source = directory.path / "scalar_surfaces.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module time_rounding;
  logic [3:0] marker;
  realtime rounded_step;
  int variable_step;
  initial begin
    rounded_step = 0.0005;
    variable_step = 1;
    marker = 0;
    #0.0004 marker = 1;
    #(rounded_step) marker = 2;
    #1.2344ns marker = 3;
    #0.0006us marker = 4;
    #(variable_step) marker = 5;
    $finish;
  end
endmodule
)";
    assert(output.good());
  }
  {
    std::ofstream output(scalar_source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
package scalar_callable_pkg;
  parameter real OFFSET = 1.25;
  parameter string PREFIX = ":pkg";
  function automatic real offset(input real value);
    return value + OFFSET;
  endfunction
  function automatic time add_ticks(
      input time value, input time amount = 2);
    return value + amount;
  endfunction
  function automatic string decorate(input string value);
    return {value, PREFIX};
  endfunction
  function automatic chandle retain(input chandle value = null);
    return value;
  endfunction
endpackage

module scalar_surfaces #(parameter real FACTOR = 2.0);
  import scalar_callable_pkg::*;
  real r;
  shortreal s;
  realtime rt;
  time ticks;
  chandle foreign;
  real offset_result;
  realtime scaled_result;
  logic [7:0] callable_checks;
  function static real accumulate_real(input real value);
    real retained = 1.0;
    retained = retained + value;
    return retained;
  endfunction
  function automatic realtime scaled(input realtime value);
    return value * FACTOR;
  endfunction
  task automatic transfer(
      input real real_in, output real real_out,
      input time time_in, output time time_out,
      input string string_in, output string string_out,
      input chandle handle_in, output chandle handle_out);
    real_out = real_in;
    time_out = time_in;
    string_out = string_in;
    handle_out = handle_in;
  endtask
  initial begin
    real real_out;
    time time_out;
    string string_out;
    chandle handle_out;
    r = r;
    s = s;
    rt = rt;
    ticks = ticks;
    foreign = foreign;
    callable_checks = 0;
    offset_result = offset(2.75);
    callable_checks[0] = offset_result == 4.0;
    callable_checks[1] = add_ticks(40) == 42;
    callable_checks[2] = add_ticks(40, 3) == 43;
    callable_checks[3] = decorate("value") == "value:pkg";
    callable_checks[4] = retain() == null;
    scaled_result = scaled(1.5);
    callable_checks[5] = scaled_result == 3.0;
    callable_checks[6] =
        accumulate_real(2.0) == 3.0
        && accumulate_real(4.0) == 7.0;
    transfer(6.5, real_out, 77, time_out,
             "copied", string_out, null, handle_out);
    callable_checks[7] =
        real_out == 6.5 && time_out == 77
        && string_out == "copied" && handle_out == null;
    #1;
    $finish;
  end
endmodule
)";
    assert(output.good());
  }

  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o0);
  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o2);
  verify_resolution_diagnostic(directory.path, source);
  verify_overflow_diagnostic(directory.path, source);
  const auto scalar_interpreter = verify_scalar_surfaces(
      directory.path, scalar_source, fsim::project::Optimization::o0,
      fsim::app::SimulationEngine::interpreter, "interpreter");
  const auto scalar_o0 = verify_scalar_surfaces(
      directory.path, scalar_source, fsim::project::Optimization::o0,
      fsim::app::SimulationEngine::compiled, "o0");
  const auto scalar_o2 = verify_scalar_surfaces(
      directory.path, scalar_source, fsim::project::Optimization::o2,
      fsim::app::SimulationEngine::compiled, "o2");
  assert(
      scalar_interpreter.payloads == scalar_o0.payloads
      && scalar_o0.payloads == scalar_o2.payloads
      && scalar_interpreter.callable_checks == scalar_o0.callable_checks
      && scalar_o0.callable_checks == scalar_o2.callable_checks
      && scalar_interpreter.change_count == scalar_o0.change_count
      && scalar_o0.change_count == scalar_o2.change_count);
  std::cout << "time application tests passed\n";
  return 0;
}

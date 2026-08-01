// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "fsim/runtime/vcd_writer.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<std::string> final_values;
  std::string vcd;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-interface";
  config.project.top = "sv:work.interface_top";
  config.project.time_resolution = "1ns";
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

Capture execute(
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
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 5> names{
      "interface_top.link[0].data",
      "interface_top.link[0].valid",
      "interface_top.link[0].ready",
      "interface_top.link[1].data",
      "interface_top.result"};
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  std::array<fsim::runtime::VcdSignal, names.size()> traces{};
  std::ostringstream vcd_text;
  fsim::runtime::VcdWriter vcd{vcd_text, "1ns", 32};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{names[index]},
        index == 0 || index == 3 || index == 4 ? 4U : 1U);
  }
  assert(
      simulation.find_signal("interface_top.source.bus.data")
      == simulation.find_signal("interface_top.link[0].data"));
  assert(
      simulation.find_signal(
          "interface_top.source.generated.source.bus.data")
      == simulation.find_signal("interface_top.link[0].data"));
  assert(
      simulation.find_signal("interface_top.sink.bus.valid")
      == simulation.find_signal("interface_top.link[0].valid"));
  vcd.begin(simulation.now());
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(traces[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        for (std::size_t index = 0; index < signals.size(); ++index) {
          if (signals[index] == signal) {
            vcd.set_time(time);
            vcd.change(traces[index], value);
          }
        }
      });
  capture.result = simulation.run();
  for (const auto signal : signals) {
    capture.final_values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  vcd.flush();
  capture.vcd = vcd_text.str();
  return capture;
}

void verify(const Capture& capture) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  const auto expected = std::vector<std::string>{
      "1010", "1", "1", "XXXX", "1011"};
  assert(capture.final_values == expected);
  assert(capture.vcd.find("b1010") != std::string::npos);
  assert(capture.vcd.find("b1011") != std::string::npos);
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-interface-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "interfaces.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(package base_values;
  localparam int VALUE = 10;
  typedef logic [3:0] word_t;
  function automatic logic [3:0] bump(input logic [3:0] value);
    return value + 1;
  endfunction
endpackage

package public_values;
  import base_values::*;
  export base_values::VALUE, base_values::word_t, base_values::bump;
endpackage

interface bus_if #(parameter int WIDTH = 4);
  logic [WIDTH-1:0] data;
  logic valid;
  logic ready;
  logic cfg;
  function automatic logic [WIDTH-1:0] sample(
      input logic [WIDTH-1:0] increment);
    return data + increment;
  endfunction
  task automatic drive(input logic [WIDTH-1:0] value);
    data = value;
    valid = 1'b1;
  endtask
  modport initiator(output data, valid, input ready,
                    ref cfg, import function sample,
                    import task drive);
  modport target(input data, valid, output ready);
  modport service(export function sample, export task drive);
endinterface

module producer(bus_if.initiator bus);
  initial begin
    bus.drive(4'h9);
    bus.data = bus.sample(4'h1);
    bus.cfg = 1'b1;
  end
endmodule

module producer_mid(bus_if.initiator bus);
  generate
    if (1) begin : generated
      producer source(bus);
    end
  endgenerate
endmodule

module service_impl(bus_if.service bus);
  function automatic logic [3:0] sample(input logic [3:0] increment);
    return increment;
  endfunction
  task automatic drive(input logic [3:0] value);
    return;
  endtask
endmodule

module consumer(
    bus_if.target bus,
    output public_values::word_t result);
  import public_values::*;
  word_t computed;
  always_comb begin
    bus.ready = bus.valid;
    computed = bus.valid
        ? public_values::bump(bus.data)
        : public_values::VALUE;
    result = computed;
  end
endmodule

module interface_top;
  bus_if #(.WIDTH(4)) link[1:0]();
  logic [3:0] result;
  producer_mid source(link[0]);
  consumer sink(.bus(link[0]), .result(result));
  service_impl service(.bus(link[1]));
  initial #1 $finish;
endmodule
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(directory.path, source, optimization);
    const auto reference = execute(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = execute(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = execute(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference);
    verify(cold);
    verify(warm);
    assert(reference.final_values == cold.final_values);
    assert(reference.vcd == cold.vcd);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 3);
    assert(cold.cache.misses > 0);
    assert(warm.cache.hits > 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
  }
  const auto edit_source = [&](const std::string_view before,
                               const std::string_view after) {
    std::ifstream input(source, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto text = buffer.str();
    const auto position = text.find(before);
    assert(position != std::string::npos);
    text.replace(position, before.size(), after);
    std::ofstream output(source, std::ios::binary | std::ios::trunc);
    output << text;
    assert(output.good());
  };
  const auto edited_config = make_config(
      directory.path, source, fsim::project::Optimization::o2);
  edit_source(
      "return data + increment;",
      "return data + increment + 0;");
  const auto callable_edit = execute(
      edited_config, fsim::app::SimulationEngine::compiled);
  verify(callable_edit);
  edit_source(
      "export base_values::VALUE, base_values::word_t, base_values::bump;",
      "export base_values::*;");
  const auto export_edit = execute(
      edited_config, fsim::app::SimulationEngine::compiled);
  verify(export_edit);
#if defined(FSIM_HAS_LLVM)
  assert(callable_edit.cache.misses > 0);
  assert(export_edit.cache.misses > 0);
#endif
  std::cout << "SystemVerilog interface application tests passed\n";
  return 0;
}

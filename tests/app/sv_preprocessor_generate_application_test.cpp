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
  std::vector<std::string> values;
  std::string vcd;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

void write_header(
    const std::filesystem::path& header,
    const unsigned base) {
  std::ofstream output(header, std::ios::binary | std::ios::trunc);
  output << "`define COUNT 2\n"
         << "`define BASE " << base << "\n";
  assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::filesystem::path& include_directory,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-preprocessor-generate";
  config.project.top = "sv:work.preprocessor_generate_top";
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
  sources.include_directories.push_back(include_directory);
  config.source_sets.push_back(std::move(sources));
  return config;
}

fsim::project::Config make_generated_class_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  auto config = make_config(directory, source, directory, optimization);
  config.project.name = "sv-2023-generated-class";
  config.project.top = "sv:work.generated_class_revision";
  config.source_sets.front().standard = "2023";
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

  constexpr std::array<std::string_view, 7> names{
      "preprocessor_generate_top.genblk1[0].function_value",
      "preprocessor_generate_top.genblk1[0].delayed",
      "preprocessor_generate_top.genblk1[0].child_value",
      "preprocessor_generate_top.genblk1[1].function_value",
      "preprocessor_generate_top.genblk1[1].delayed",
      "preprocessor_generate_top.genblk1[1].child_value",
      "preprocessor_generate_top.genblk2.marker"};
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
        static_cast<std::uint32_t>(
            simulation.read_signal(*signal).width()));
  }
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
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  vcd.flush();
  capture.vcd = vcd_text.str();
  return capture;
}

void verify(
    const Capture& capture,
    const std::vector<std::string>& expected) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 5);
  if (capture.values != expected) {
    for (const auto& value : capture.values) {
      std::cerr << value << ' ';
    }
    std::cerr << '\n';
  }
  assert(capture.values == expected);
  assert(capture.vcd.find("#3") != std::string::npos);
}

Capture execute_generated_class(
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
  const auto observed = simulation.find_signal(
      "generated_class_revision.observed");
  const auto nested_marker = simulation.find_signal(
      "generated_class_revision.genblk2.nested_marker");
  assert(observed && nested_marker);
  capture.result = simulation.run();
  capture.values = {
      simulation.read_signal(*observed).to_msb_string(),
      simulation.read_signal(*nested_marker).to_msb_string()};
  return capture;
}

void verify_directive_compilation_units(
    const std::filesystem::path& directory,
    const std::filesystem::path& seed,
    const std::filesystem::path& selected) {
  const auto check = [&](const std::string_view mode) {
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sv-2023-directive-" + std::string {mode};
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.compilation_unit = mode;
    sources.files = {seed, selected};
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    auto checked = fsim::app::check_project(config, diagnostics);
    if (!checked) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(checked && !diagnostics.has_error());
    return checked;
  };
  const auto has_unit = [](const auto& checked, const std::string_view name) {
    return std::ranges::any_of(
        checked->parsed.units,
        [&](const auto& unit) { return unit.name == name; });
  };

  const auto shared = check("source-set");
  assert(has_unit(shared, "directive_seed"));
  assert(has_unit(shared, "directive_shared"));
  assert(!has_unit(shared, "directive_isolated"));
  assert(shared->hdl_sources.size() == 2);
  assert(shared->hdl_sources[0].compilation_unit_digest
      == shared->hdl_sources[1].compilation_unit_digest);

  const auto isolated = check("file");
  assert(has_unit(isolated, "directive_seed"));
  assert(!has_unit(isolated, "directive_shared"));
  assert(has_unit(isolated, "directive_isolated"));
  assert(isolated->hdl_sources.size() == 2);
  assert(isolated->hdl_sources[0].compilation_unit_digest
      != isolated->hdl_sources[1].compilation_unit_digest);
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-preprocessor-generate-" + std::to_string(nonce))};
  const auto include_directory = directory.path / "include";
  const auto nested_directory = include_directory / "nested";
  std::filesystem::create_directories(nested_directory);
  const auto header = nested_directory / "values.svh";
  const auto source = directory.path / "preprocessor_generate.sv";
  const auto directive_seed = directory.path / "directive_seed.sv";
  const auto directive_selected = directory.path / "directive_selected.sv";
  const auto generated_class_source = directory.path / "generated_class.sv";
  write_header(header, 2);
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(`define GHOST 1
`undefineall
`ifdef GHOST
module inactive_bad(;
`endif
`define INCLUDE_DIRECTORY nested
`define INCLUDE_FILE values.svh
`include <`INCLUDE_DIRECTORY/`INCLUDE_FILE>
`timescale 1ns/1ns

module generated_leaf #(
  parameter int WIDTH = 1,
  parameter int VALUE = 0
) (output logic [WIDTH-1:0] q);
  initial q = VALUE;
endmodule

module preprocessor_generate_top;
  generate
    for (genvar i = 0; i < `COUNT; i++) begin
      localparam int WIDTH = i + `BASE;
      typedef logic [WIDTH-1:0] word_t;
      word_t function_value;
      word_t child_value;
      wire [WIDTH-1:0] #(WIDTH) delayed = function_value;
      function automatic word_t bump(input word_t input_value);
        return input_value + 1;
      endfunction
      task automatic drive(
          output word_t target,
          input word_t input_value);
        target = bump(input_value);
      endtask
      generated_leaf #(.WIDTH(WIDTH), .VALUE(WIDTH))
          child(.q(child_value));
      initial drive(function_value, WIDTH);
    end
    if (`COUNT == 2) begin
      logic marker;
      initial marker = 1'b1;
    end
  endgenerate
  initial #5 $finish;
endmodule
)";
    assert(output.good());
  }
  {
    std::ofstream output(directive_seed, std::ios::binary);
    output << "`define CROSS_FILE\nmodule directive_seed; endmodule\n";
    assert(output.good());
  }
  {
    std::ofstream output(directive_selected, std::ios::binary);
    output << R"(`begin_keywords "1800-2023"
`ifdef (CROSS_FILE && !MISSING)
module directive_shared; endmodule
`else
module directive_isolated; endmodule
`endif
`end_keywords
)";
    assert(output.good());
  }
  {
    std::ofstream output(generated_class_source, std::ios::binary);
    output << R"(module generated_class_revision;
  logic [7:0] observed;
  if (1) begin
    interface class Contract;
      pure virtual function int value();
    endclass
    class Worker implements Contract;
      virtual function int value();
        return 13;
      endfunction
    endclass
    Worker worker;
    initial begin
      worker = new;
      observed = worker.value();
    end
  end else begin
    class Inactive extends Missing;
    endclass
  end
  if (1)
    if (1) begin
      logic nested_marker;
      initial nested_marker = 1'b1;
    end
  initial #1 $finish;
endmodule
)";
    assert(output.good());
  }
  verify_directive_compilation_units(
      directory.path, directive_seed, directive_selected);

  const std::vector<std::string> first_expected{
      "11", "11", "10", "100", "100", "011", "1"};
  const std::vector<std::string> changed_expected{
      "100", "100", "011", "0101", "0101", "0100", "1"};
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path, source, include_directory, optimization);
    const auto reference = execute(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = execute(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = execute(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference, first_expected);
    verify(cold, first_expected);
    verify(warm, first_expected);
    assert(reference.vcd == cold.vcd);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.cache.misses > 0);
    assert(warm.cache.hits > 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
  }

  write_header(header, 3);
  const auto edited_config = make_config(
      directory.path,
      source,
      include_directory,
      fsim::project::Optimization::o2);
  const auto edited = execute(
      edited_config, fsim::app::SimulationEngine::compiled);
  verify(edited, changed_expected);
#if defined(FSIM_HAS_LLVM)
  assert(edited.cache.misses > 0);
#endif

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_generated_class_config(
        directory.path, generated_class_source, optimization);
    const auto reference = execute_generated_class(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = execute_generated_class(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = execute_generated_class(
        config, fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.values == std::vector<std::string>({"00001101", "1"}));
    assert(cold.values == reference.values);
    assert(warm.values == reference.values);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.cache.misses > 0);
    assert(warm.cache.hits > 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
  }
  std::cout << "SystemVerilog preprocessor/generate application tests passed\n";
  return 0;
}

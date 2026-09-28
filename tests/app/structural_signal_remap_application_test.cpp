// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult run;
  std::array<std::string, 8> values;
  std::size_t process_count{};
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}

std::string repeated_logic9_pattern(
    const std::string_view pattern, const std::size_t width) {
  assert(!pattern.empty());
  std::string result;
  result.reserve(width);
  for (std::size_t index = 0; index < width; ++index) {
    result.push_back(pattern[index % pattern.size()]);
  }
  return result;
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::string_view root = "structural_signal_remap_app",
    const std::optional<fsim::runtime::SimulationTick> until = std::nullopt,
    const bool capture_logic9 = false) {
  fsim::app::Simulation simulation{std::move(project), 1000, engine};
  const std::array suffixes{
      std::string_view{"source_a"},
      std::string_view{"source_b"},
      std::string_view{"result_a"},
      std::string_view{"result_b"},
      std::string_view{"a.result"},
      std::string_view{"b.result"},
  };
  std::array<fsim::runtime::simir::SignalId, suffixes.size()> signals{};
  for (std::size_t index = 0; index < suffixes.size(); ++index) {
    const auto path = std::string{root} + "." + std::string{suffixes[index]};
    const auto signal = simulation.find_signal(path);
    assert(signal);
    signals[index] = *signal;
  }

  Capture capture;
  capture.process_count = simulation.design_ir().processes().size();
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.run = simulation.run(until);
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index]
        = simulation.read_signal(signals[index]).to_msb_string();
  }
  if (capture_logic9) {
    constexpr std::array logic9_suffixes{
        std::string_view{"state_result_a"},
        std::string_view{"state_result_b"},
    };
    for (std::size_t index = 0; index < logic9_suffixes.size(); ++index) {
      const auto path
          = std::string{root} + "." + std::string{logic9_suffixes[index]};
      const auto signal = simulation.find_signal(path);
      assert(signal);
      capture.values[signals.size() + index]
          = simulation.read_signal(*signal).to_msb_string();
    }
  }
  return capture;
}

void test_vhdl_projected_slice_level(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-structural-signal-remap-application-test";
  config.project.top = "vhdl:work.structural_signal_remap_vhdl_app(test)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path
      = directory
      / (optimization == fsim::project::Optimization::o0
              ? "vhdl-cache-o0"
              : "vhdl-cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project = fsim::app::build_project(config, diagnostics);
  auto compiled_project = fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);

  constexpr std::string_view root = "structural_signal_remap_vhdl_app";
  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter,
      root,
      4U,
      true);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled,
      root,
      4U,
      true);
  assert(compiled.run.status == reference.run.status);
  assert(compiled.run.time == reference.run.time);
  assert(compiled.run.delta == reference.run.delta);
  assert(compiled.run.callbacks_executed
      <= reference.run.callbacks_executed);
  assert(reference.run.callbacks_executed
          - compiled.run.callbacks_executed
      <= compiled.compiled_processes);
  assert(compiled.values == reference.values);
  assert((reference.values == std::array<std::string, 8>{
                                  "10100101",
                                  "00111100",
                                  "11111111",
                                  "01100110",
                                  "11111111",
                                  "01100110",
                                  repeated_logic9_pattern("UX01ZWLH-", 65U),
                                  repeated_logic9_pattern("HLWZ10XU-", 65U)}));
#if defined(FSIM_HAS_LLVM)
  assert(reference.process_count == 131);
  assert(compiled.compiled_processes == 4);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_level(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "structural-signal-remap-application-test";
  config.project.top = "sv:work.structural_signal_remap_app";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path
      = directory
      / (optimization == fsim::project::Optimization::o0
              ? "cache-o0"
              : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2005";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project = fsim::app::build_project(config, diagnostics);
  auto compiled_project = fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);

  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  assert(reference.run.status == fsim::runtime::RunStatus::stopped);
  assert(compiled.run.status == reference.run.status);
  assert(compiled.run.time == reference.run.time);
  assert(compiled.run.delta == reference.run.delta);
  assert(compiled.run.callbacks_executed == reference.run.callbacks_executed);
  assert(compiled.values == reference.values);
  assert((reference.values == std::array<std::string, 8>{
                                  "10100101",
                                  "00111100",
                                  "00000010",
                                  "01101001",
                                  "00000010",
                                  "01101001",
                                  "",
                                  ""}));
#if defined(FSIM_HAS_LLVM)
  assert(reference.process_count == 3);
  assert(compiled.compiled_processes == 3);
  assert(compiled.compiled_modules == 3);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_large_bound_literal_sharing(
    const std::filesystem::path& directory)
{
#if defined(FSIM_HAS_LLVM)
  const auto require = [](const bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  const auto source = directory / "bound_literal_sharing.sv";
  {
    std::ofstream output(source);
    output << R"(
module bound_literal_leaf #(parameter integer VALUE = 0)(
  output logic [7:0] result
);
  logic [7:0] memory [0:1];
  initial begin
    memory[0] = VALUE;
    result = memory[0];
  end
endmodule

module bound_literal_dummy;
  logic done;
  initial done = 1'b1;
endmodule

module bound_literal_sharing_top;
  wire [7:0] result_a;
  wire [7:0] result_b;
  bound_literal_leaf #(.VALUE(8'h21)) a(result_a);
  bound_literal_leaf #(.VALUE(8'hd4)) b(result_b);
)";
    for (std::size_t index = 0; index < 126U; ++index) {
      output << "  bound_literal_dummy dummy_" << index << "();\n";
    }
    output << "endmodule\n";
    require(output.good(), "cannot write bound-literal fixture");
  }
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "bound-literal-sharing-test";
  config.project.top = "sv:work.bound_literal_sharing_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = fsim::project::Optimization::o2;
  config.build.cache_path = directory / "bound-literal-cache";
  config.run.max_deltas = 1000U;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    print_diagnostics(diagnostics);
  }
  require(project.has_value(), "cannot build bound-literal fixture");
  auto state = std::move(project->design).state();
  std::size_t padded = 0U;
  for (auto& process : state.processes) {
    const auto has_container_write = std::ranges::any_of(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::WriteContainerObjectElement>(operation);
        });
    if (!has_container_write) {
      continue;
    }
    require(process.container_register_count != 0U,
        "bound-literal fixture has no container registers");
    require(process.static_sensitivity.empty(),
        "bound-literal fixture is recurring");
    while (process.operations.size() < 8191U) {
      process.operations.push_back(fsim::runtime::simir::DebugPoint{});
    }
    process.operations.push_back(fsim::runtime::simir::Halt{});
    ++padded;
  }
  require(padded == 2U, "bound-literal fixture did not select two leaves");
  auto restored = fsim::elaboration::ElaboratedDesign::from_state(
      std::move(state));
  require(restored.has_value(), "cannot restore bound-literal fixture");
  project->design = std::move(*restored);

  const auto run = [&](fsim::app::BuiltProject built,
                      const fsim::app::SimulationEngine engine) {
    fsim::app::Simulation simulation(std::move(built), 1000U, engine);
    require(simulation.design_ir().processes().size() >= 128U,
        "bound-literal fixture did not enable selective compilation");
    if (engine == fsim::app::SimulationEngine::compiled) {
      require(simulation.compiled_process_count() == 2U,
          "bound-literal fixture did not select both large processes");
    }
    const auto modules = simulation.compiled_module_count();
    simulation.await_all_native_compilation();
    const auto result = simulation.run();
    require(result.status == fsim::runtime::RunStatus::completed,
        "bound-literal fixture did not complete");
    const auto a = simulation.find_signal(
        "bound_literal_sharing_top.a.result");
    const auto b = simulation.find_signal(
        "bound_literal_sharing_top.b.result");
    require(a.has_value() && b.has_value(),
        "bound-literal result signals are missing");
    return std::pair { std::array {
        simulation.read_signal(*a).to_msb_string(),
        simulation.read_signal(*b).to_msb_string() }, modules };
  };
  const auto reference = run(*project,
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = run(std::move(*project),
      fsim::app::SimulationEngine::compiled);
  require(reference.first == compiled.first,
      "bound-literal shared instance values differ from interpreter");
  require((compiled.first == std::array<std::string, 2>{
      "00100001", "11010100"}),
      "bound-literal shared instance values are wrong");
  require(compiled.second == 1U,
      "bound-literal instances did not share one large native module");
#endif
}

void test_shared_container_error_process_identity(
    const std::filesystem::path& directory)
{
#if defined(FSIM_HAS_LLVM)
  const auto require = [](const bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  const auto source = directory / "shared_container_error.sv";
  {
    std::ofstream output(source);
    output << R"(
module shared_error_leaf(input logic [31:0] index);
  logic [7:0] memory [0:1];
  logic [7:0] result;
  initial begin
    memory[index] = 8'h5a;
    result = memory[0];
  end
endmodule

module shared_error_dummy;
  logic done;
  initial done = 1'b1;
endmodule

module shared_container_error_top;
  logic [31:0] index_a = 32'd0;
  logic [31:0] index_b = 32'bx;
  shared_error_leaf a(index_a);
  shared_error_leaf b(index_b);
)";
    for (std::size_t index = 0; index < 126U; ++index) {
      output << "  shared_error_dummy dummy_" << index << "();\n";
    }
    output << "endmodule\n";
    require(output.good(), "cannot write shared-container-error fixture");
  }
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "shared-container-error-test";
  config.project.top = "sv:work.shared_container_error_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = fsim::project::Optimization::o2;
  config.build.cache_path = directory / "shared-container-error-cache";
  config.run.max_deltas = 1000U;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    print_diagnostics(diagnostics);
  }
  require(project.has_value(), "cannot build shared-container-error fixture");
  auto state = std::move(project->design).state();
  std::optional<fsim::runtime::simir::ProcessId> representative_process;
  std::optional<fsim::runtime::simir::ProcessId> failing_process;
  std::size_t padded = 0U;
  for (auto& process : state.processes) {
    for (const auto& operation : process.operations) {
      const auto* write = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::WriteContainerObjectElement>(&operation);
      if (write == nullptr) {
        continue;
      }
      require(write->object < state.container_objects.size(),
          "shared-container-error object ID is invalid");
      const auto& name = state.container_objects[write->object].name;
      if (name.find(".a.memory") != std::string::npos) {
        representative_process = process.id;
      }
      if (name.find(".b.memory") != std::string::npos) {
        failing_process = process.id;
      }
    }
    const auto has_container_write = std::ranges::any_of(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::WriteContainerObjectElement>(operation);
        });
    if (!has_container_write) {
      continue;
    }
    require(process.static_sensitivity.empty(),
        "shared-container-error process is recurring");
    while (process.operations.size() < 8191U) {
      process.operations.push_back(fsim::runtime::simir::DebugPoint{});
    }
    process.operations.push_back(fsim::runtime::simir::Halt{});
    ++padded;
  }
  require(padded == 2U && representative_process.has_value()
          && failing_process.has_value()
          && *representative_process < *failing_process,
      "shared-container-error fixture did not identify two leaves");
  auto restored = fsim::elaboration::ElaboratedDesign::from_state(
      std::move(state));
  require(restored.has_value(), "cannot restore shared-container-error fixture");
  project->design = std::move(*restored);

  const auto run = [&](fsim::app::BuiltProject built,
                      const fsim::app::SimulationEngine engine) {
    fsim::app::Simulation simulation(std::move(built), 1000U, engine);
    require(simulation.design_ir().processes().size() >= 128U,
        "shared-container-error fixture did not enable selective compilation");
    if (engine == fsim::app::SimulationEngine::compiled) {
      require(simulation.compiled_process_count() == 2U,
          "shared-container-error fixture did not select both leaves");
      if (simulation.compiled_module_count() != 1U) {
        throw std::runtime_error(
            "shared-container-error module count "
            + std::to_string(simulation.compiled_module_count()));
      }
      simulation.await_all_native_compilation();
    }
    try {
      (void)simulation.run();
    } catch (const fsim::runtime::simir::InterpreterError& error) {
      return error.process();
    }
    throw std::runtime_error("shared-container-error fixture did not fail");
  };
  require(run(*project, fsim::app::SimulationEngine::interpreter)
          == *failing_process,
      "interpreted container error has the wrong process ID");
  require(run(std::move(*project), fsim::app::SimulationEngine::compiled)
          == *failing_process,
      "shared compiled container error has the wrong process ID");
#endif
}

}  // namespace

int main() {
  const auto nonce
      = std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-structural-signal-remap-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "structural_signal_remap.sv";
  {
    std::ofstream output(source);
    output << R"(
module structural_remap_leaf(
  input  logic       clock,
  input  logic [7:0] source,
  output logic [7:0] result
);
  always_ff @(posedge clock) begin
    result <= (source ^ 8'h5a) + 8'h03;
  end
endmodule

module structural_signal_remap_app;
  logic       clock;
  logic [7:0] source_a;
  logic [7:0] source_b;
  logic [7:0] result_a;
  logic [7:0] result_b;

  structural_remap_leaf a(clock, source_a, result_a);
  structural_remap_leaf b(clock, source_b, result_b);

  initial begin
    clock = 1'b0;
    source_a = 8'h12;
    source_b = 8'hc3;
    #1;
    clock = 1'b1;
    #1;
    clock = 1'b0;
    source_a = 8'ha5;
    source_b = 8'h3c;
    #1;
    clock = 1'b1;
    #1;
    $finish;
  end
endmodule
)";
  }
  const auto vhdl_source
      = directory.path / "structural_signal_remap.vhd";
  const auto state_source_a = repeated_logic9_pattern("UX01ZWLH-", 65U);
  const auto state_source_b = repeated_logic9_pattern("HLWZ10XU-", 65U);
  {
    std::ofstream output(vhdl_source);
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity projected_slice_leaf is
  port (
    clock        : in  std_logic;
    source       : in  std_logic_vector(7 downto 0);
    result       : out std_logic_vector(7 downto 0);
    state_source : in  std_logic_vector(64 downto 0);
    state_result : out std_logic_vector(64 downto 0)
  );
end entity;

architecture rtl of projected_slice_leaf is
begin
  process (clock)
  begin
    if rising_edge(clock) then
      assert not is_x(source)
        report "structurally shared input contains an unknown value"
        severity warning;
      result(3 downto 0) <= source(3 downto 0) xor "1010";
      result(7 downto 4) <= source(7 downto 4) xor "0101";
      state_result <= state_source;
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity structural_signal_remap_vhdl_app is
end entity;

architecture test of structural_signal_remap_vhdl_app is
  signal clock    : std_logic := '0';
  signal source_a : std_logic_vector(7 downto 0) := x"12";
  signal source_b : std_logic_vector(7 downto 0) := x"c3";
  signal result_a : std_logic_vector(7 downto 0);
  signal result_b : std_logic_vector(7 downto 0);
  signal result_c : std_logic_vector(7 downto 0);
  signal result_d : std_logic_vector(7 downto 0);
  signal state_source_a : std_logic_vector(64 downto 0) := ")";
    output << state_source_a << "\";\n"
           << "  signal state_source_b : std_logic_vector(64 downto 0) := \""
           << state_source_b << "\";\n"
           << R"(  signal state_result_a : std_logic_vector(64 downto 0);
  signal state_result_b : std_logic_vector(64 downto 0);
  signal state_result_c : std_logic_vector(64 downto 0);
  signal state_result_d : std_logic_vector(64 downto 0);
begin
  a : entity work.projected_slice_leaf
    port map (clock => clock, source => source_a, result => result_a,
      state_source => state_source_a, state_result => state_result_a);
  b : entity work.projected_slice_leaf
    port map (clock => clock, source => source_b, result => result_b,
      state_source => state_source_b, state_result => state_result_b);
  c : entity work.projected_slice_leaf
    port map (clock => clock, source => source_a, result => result_c,
      state_source => state_source_a, state_result => state_result_c);
  d : entity work.projected_slice_leaf
    port map (clock => clock, source => source_b, result => result_d,
      state_source => state_source_b, state_result => state_result_d);

  dummy_processes : for index in 0 to 125 generate
    dormant : process
    begin
      wait;
    end process;
  end generate;

  stimulus : process
  begin
    wait for 1 ns;
    clock <= '1';
    wait for 1 ns;
    clock <= '0';
    source_a <= x"a5";
    source_b <= x"3c";
    wait for 1 ns;
    clock <= '1';
    wait for 1 ns;
    wait;
  end process;
end architecture;
)";
  }

  test_level(
      directory.path, source, fsim::project::Optimization::o0);
  test_level(
      directory.path, source, fsim::project::Optimization::o2);
  test_vhdl_projected_slice_level(
      directory.path, vhdl_source, fsim::project::Optimization::o0);
  test_vhdl_projected_slice_level(
      directory.path, vhdl_source, fsim::project::Optimization::o2);
  test_large_bound_literal_sharing(directory.path);
  test_shared_container_error_process_identity(directory.path);
  return 0;
}

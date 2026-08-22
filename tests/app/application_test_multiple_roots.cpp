// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include "fsim/cli/driver.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>

namespace fsim::test {

void ApplicationTestFixture::test_multiple_roots() {
  const auto roots_source = directory / "multiple-roots.sv";
  {
    std::ofstream output(roots_source, std::ios::binary);
    output << R"(
module producer;
  logic [3:0] value;
  initial begin
    value = 4'ha;
    #2 value = 4'h3;
  end
endmodule
module consumer;
  logic [3:0] value;
  initial begin
    value = 4'h5;
    // Root aliases are ordinary SystemVerilog top-level hierarchy names.
    // This models a consumer of a separately selected global-signaling root.
    #3 value = source.value;
  end
endmodule
)";
    assert(output.good());
  }

  auto config = base_config();
  config.project.name = "multiple-root-application";
  config.project.top.clear();
  config.project.tops = {
      {"sv:work.producer", "source"},
      {"sv:work.consumer", "sink"}};
  config.build.cache_path = directory / "multiple-root-cache";
  config.source_sets.clear();
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files = {roots_source};
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine reference_diagnostics;
  auto reference_project =
      fsim::app::build_project(config, reference_diagnostics);
  assert(reference_project && !reference_diagnostics.has_error());
  assert((
      reference_project->design.roots()
      == std::vector<std::string>{"source", "sink"}));
  assert(reference_project->design.top() == "source");
  assert(reference_project->design_ir.roots()
         == reference_project->design.roots());
  assert(reference_project->design_ir.instances().size() == 2);
  assert(!reference_project->design_ir.instances()[0].parent);
  assert(!reference_project->design_ir.instances()[1].parent);
  const auto reference_key = reference_project->cache_key;
  const auto reference = capture_simulation(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  assert(reference.result.status == fsim::runtime::RunStatus::completed);
  assert(reference.result.time == 3);
  assert((reference.final_values
          == std::vector<std::string>{"0011", "0011"}));
  assert(reference.normalized_vcd.find("source") != std::string::npos);
  assert(reference.normalized_vcd.find("sink") != std::string::npos);

  fsim::diagnostic::Engine compiled_diagnostics;
  auto compiled_project =
      fsim::app::build_project(config, compiled_diagnostics);
  assert(compiled_project && !compiled_diagnostics.has_error());
  assert(compiled_project->cache_hit);
  assert(compiled_project->cache_key == reference_key);
  const auto compiled = capture_simulation(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(reference, compiled);

  auto o0_config = config;
  o0_config.build.optimization = fsim::project::Optimization::o0;
  o0_config.build.cache_path = directory / "multiple-root-o0-cache";
  fsim::diagnostic::Engine o0_diagnostics;
  auto o0_project = fsim::app::build_project(o0_config, o0_diagnostics);
  assert(o0_project && !o0_diagnostics.has_error());
  const auto o0 = capture_simulation(
      std::move(*o0_project), fsim::app::SimulationEngine::compiled);
  compare_captures(reference, o0);

  auto reordered = config;
  std::ranges::reverse(reordered.project.tops);
  reordered.build.cache_path = directory / "multiple-root-reordered-cache";
  fsim::diagnostic::Engine reordered_diagnostics;
  const auto reordered_project =
      fsim::app::build_project(reordered, reordered_diagnostics);
  assert(reordered_project && !reordered_diagnostics.has_error());
  assert(reordered_project->cache_key != reference_key);
  assert((
      reordered_project->design.roots()
      == std::vector<std::string>{"sink", "source"}));

  fsim::diagnostic::Engine debugger_diagnostics;
  auto debugger_project =
      fsim::app::build_project(config, debugger_diagnostics);
  assert(debugger_project && !debugger_diagnostics.has_error());
  fsim::app::Simulation debugger{
      std::move(*debugger_project),
      config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  debugger.start();
  std::istringstream debugger_input{
      "scope sink\nshow value\nscope source\nshow value\nquit\n"};
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  assert(
      fsim::app::run_debug_repl(
          debugger,
          debugger_input,
          debugger_output,
          debugger_error)
      == 0);
  assert(debugger_error.str().empty());
  assert(debugger_output.str().find("sink.value = X")
         != std::string::npos);
  assert(debugger_output.str().find("source.value = X")
         != std::string::npos);

  const auto vhdl_root_source = directory / "multiple-root-vhdl.vhd";
  const auto sv_root_source = directory / "multiple-root-mixed.sv";
  {
    std::ofstream output(vhdl_root_source, std::ios::binary);
    output << R"(
entity vhdl_root is
end entity vhdl_root;

architecture rtl of vhdl_root is
  signal observed : bit;
begin
  process
  begin
    observed <= '1';
    wait for 2 ns;
    observed <= '0';
    wait;
  end process;
end architecture rtl;
)";
    assert(output.good());
  }
  {
    std::ofstream output(sv_root_source, std::ios::binary);
    output << R"(
module sv_root;
  logic observed;
  initial begin
    observed = 1'b0;
    #1 observed = 1'b1;
  end
endmodule
)";
    assert(output.good());
  }

  auto mixed = base_config();
  mixed.project.name = "multiple-root-mixed-language";
  mixed.project.top.clear();
  mixed.project.tops = {
      {"vhdl:work.vhdl_root(rtl)", "vhdl_side"},
      {"sv:work.sv_root", "sv_side"}};
  mixed.build.cache_path = directory / "multiple-root-mixed-cache";
  mixed.source_sets.clear();
  fsim::project::SourceSet vhdl_sources;
  vhdl_sources.language = fsim::project::Language::vhdl;
  vhdl_sources.standard = "2008";
  vhdl_sources.library = "work";
  vhdl_sources.files = {vhdl_root_source};
  mixed.source_sets.push_back(std::move(vhdl_sources));
  fsim::project::SourceSet sv_sources;
  sv_sources.language = fsim::project::Language::system_verilog;
  sv_sources.standard = "2017";
  sv_sources.library = "work";
  sv_sources.files = {sv_root_source};
  mixed.source_sets.push_back(std::move(sv_sources));

  fsim::diagnostic::Engine mixed_reference_diagnostics;
  auto mixed_reference_project =
      fsim::app::build_project(mixed, mixed_reference_diagnostics);
  if (!mixed_reference_project || mixed_reference_diagnostics.has_error()) {
    fsim::diagnostic::print_text(
        std::cerr, mixed_reference_diagnostics);
  }
  assert(
      mixed_reference_project
      && !mixed_reference_diagnostics.has_error());
  assert((
      mixed_reference_project->design.roots()
      == std::vector<std::string>{"vhdl_side", "sv_side"}));
  const auto vhdl_observed =
      mixed_reference_project->design.find_signal("vhdl_side.observed");
  const auto sv_observed =
      mixed_reference_project->design.find_signal("sv_side.observed");
  assert(vhdl_observed && sv_observed);
  const auto mixed_reference = capture_simulation(
      std::move(*mixed_reference_project),
      fsim::app::SimulationEngine::interpreter);
  assert(mixed_reference.result.time == 2);
  assert(mixed_reference.final_values.at(*vhdl_observed) == "0");
  assert(mixed_reference.final_values.at(*sv_observed) == "1");

  fsim::diagnostic::Engine mixed_compiled_diagnostics;
  auto mixed_compiled_project =
      fsim::app::build_project(mixed, mixed_compiled_diagnostics);
  assert(
      mixed_compiled_project
      && !mixed_compiled_diagnostics.has_error());
  const auto mixed_compiled = capture_simulation(
      std::move(*mixed_compiled_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(mixed_reference, mixed_compiled);

  const auto manifest = directory / "multiple-roots.toml";
  const auto filtered_trace = directory / "multiple-roots-filtered.vcd";
  {
    std::ofstream output(manifest, std::ios::binary);
    output << R"(
schema = 3

[project]
name = "multiple-root-cli"
time_resolution = "1ns"

[[project.top]]
target = "sv:work.producer"
alias = "source"

[[project.top]]
target = "sv:work.consumer"
alias = "sink"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["multiple-roots.sv"]

[build]
cache_path = "multiple-root-cli-cache"

[run]
max_deltas = 1000
trace_file = "multiple-roots-filtered.vcd"
trace_filters = ["sink.*"]
)";
    assert(output.good());
  }
  std::istringstream cli_input;
  std::ostringstream cli_output;
  std::ostringstream cli_error;
  auto services = fsim::app::make_cli_services(cli_input);
  const auto manifest_text = manifest.string();
  const std::vector<const char*> arguments{
      "fsim", "run", "-p", manifest_text.c_str()};
  const auto cli_status = fsim::cli::run(
      static_cast<int>(arguments.size()),
      arguments.data(),
      services,
      cli_output,
      cli_error);
  if (cli_status != 0) {
    std::cerr << cli_error.str();
  }
  assert(cli_status == 0);
  assert(cli_error.str().empty());
  std::ifstream trace_input(filtered_trace, std::ios::binary);
  const std::string filtered_vcd{
      std::istreambuf_iterator<char>{trace_input},
      std::istreambuf_iterator<char>{}};
  assert(filtered_vcd.find("sink") != std::string::npos);
  assert(
      filtered_vcd.find("$scope module source $end")
      == std::string::npos);
  assert(
      filtered_vcd.find("fsim-verilog-scope path=source ")
      == std::string::npos);
}

} // namespace fsim::test
